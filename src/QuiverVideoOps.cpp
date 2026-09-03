#include <config.h>

#include "QuiverVideoOps.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/display.h>
#include <libswscale/swscale.h>
}

#include <string.h>
#include <string>
#include <utility>

#include <libquiver/quiver-pixbuf-utils.h>

namespace QuiverVideoOps
{

/* Videos may carry a rotation (e.g. portrait phones record a rotated frame
 * into a landscape container).  The rotation is conveyed either as a
 * "rotate" stream metadata tag, or - for modern files - as a display-matrix
 * side data present on the decoded frame.  Detect it and return the angle in
 * degrees (0/90/180/270). */
static int frame_rotation_deg(AVFrame* frame, AVDictionary* metadata)
{
	AVDictionaryEntry* e = av_dict_get(metadata, "rotate", NULL, 0);
	if (e != NULL)
	{
		double deg = g_ascii_strtod(e->value, NULL);
		if (deg < 0) deg += 360.0;
		while (deg >= 360.0) deg -= 360.0;
		int d = (int)(deg + 0.5) % 360;
		if (d == 0) return 0;
		if (d == 90) return 90;
		if (d == 180) return 180;
		if (d == 270) return 270;
		return 0;
	}

	if (frame != NULL)
	{
		AVFrameSideData* sd = av_frame_get_side_data(frame, AV_FRAME_DATA_DISPLAYMATRIX);
		if (sd != NULL && sd->size >= 36)
		{
			double deg = av_display_rotation_get((int32_t*)sd->data);
			if (deg < 0) deg += 360.0;
			while (deg >= 360.0) deg -= 360.0;
			int d = (int)(deg + 0.5) % 360;
			if (d == 0) return 0;
			if (d == 90) return 90;
			if (d == 180) return 180;
			if (d == 270) return 270;
		}
	}
	return 0;
}
/* avformat_open_input() expects a native filesystem path, not a URI: it will
 * not percent-decode "file:///...". QuiverFile::GetURI() returns a file:// URI
 * (with percent-encoding for spaces etc.), so translate it to a local path. */
static gboolean uri_to_path(const gchar* uri, std::string& path)
{
	char* local = g_filename_from_uri(uri, NULL, NULL);
	if (local == NULL)
		return FALSE;
	path.assign(local);
	g_free(local);
	return TRUE;
}

/* Open the file, find its first video stream and open a decoder for it.
 * On failure all members are reset and ok is FALSE. */
struct VideoSession {
	AVFormatContext* fmt = NULL;
	int video_stream = -1;
	AVStream* st = NULL;
	AVCodecContext* ctx = NULL;
	bool ok = false;
};

static VideoSession open_session(const gchar* uri)
{
	VideoSession s;
	std::string path;
	if (!uri_to_path(uri, path))
		return s;

	if (avformat_open_input(&s.fmt, path.c_str(), NULL, NULL) != 0)
	{
		s.fmt = NULL;
		return s;
	}
	avformat_find_stream_info(s.fmt, NULL);

	for (unsigned i = 0; i < s.fmt->nb_streams; ++i)
	{
		if (s.fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			s.video_stream = (int)i;
			break;
		}
	}
	if (s.video_stream < 0)
	{
		avformat_close_input(&s.fmt);
		s.fmt = NULL;
		return s;
	}

	s.st = s.fmt->streams[s.video_stream];
	const AVCodec* dec = avcodec_find_decoder(s.st->codecpar->codec_id);
	if (dec == NULL)
	{
		avformat_close_input(&s.fmt);
		s.fmt = NULL;
		return s;
	}

	s.ctx = avcodec_alloc_context3(dec);
	if (s.ctx == NULL ||
	    avcodec_parameters_to_context(s.ctx, s.st->codecpar) < 0)
	{
		if (s.ctx != NULL) avcodec_free_context(&s.ctx);
		s.ctx = NULL;
		avformat_close_input(&s.fmt);
		s.fmt = NULL;
		return s;
	}

	s.ctx->thread_count = 1;
	if (avcodec_open2(s.ctx, dec, NULL) < 0)
	{
		avcodec_free_context(&s.ctx);
		s.ctx = NULL;
		avformat_close_input(&s.fmt);
		s.fmt = NULL;
		return s;
	}

	s.ok = true;
	return s;
}

static void close_session(VideoSession& s)
{
	if (s.ctx != NULL) avcodec_free_context(&s.ctx);
	if (s.fmt != NULL) avformat_close_input(&s.fmt);
	s = VideoSession();
}

/* Decode and discard frames until the first one is produced, returning the
 * first frame's rotation (0/90/180/270).  Returns 0 if no frame is decodable. */
static int probe_rotation(VideoSession& s)
{
	AVPacket* pkt = av_packet_alloc();
	AVFrame* fr = av_frame_alloc();
	int rotation = 0;

	while (av_read_frame(s.fmt, pkt) >= 0)
	{
		if (pkt->stream_index != s.video_stream)
		{
			av_packet_unref(pkt);
			continue;
		}
		avcodec_send_packet(s.ctx, pkt);
		av_packet_unref(pkt);
		while (avcodec_receive_frame(s.ctx, fr) == 0)
		{
			rotation = frame_rotation_deg(fr, s.st->metadata);
			av_frame_unref(fr);
			goto out;
		}
	}
out:
	av_packet_free(&pkt);
	av_frame_free(&fr);
	return rotation;
}

static GdkPixbuf* frame_to_pixbuf(AVFrame* frame, int width, int height,
                                  gint target_width, gint target_height,
                                  int rotation)
{
	if (frame == NULL || frame->data[0] == NULL || width < 1 || height < 1)
		return NULL;

	struct SwsContext* sws = sws_getContext(
		width, height, (AVPixelFormat)frame->format,
		width, height, AV_PIX_FMT_RGB24,
		SWS_BILINEAR, NULL, NULL, NULL);
	if (sws == NULL)
		return NULL;

	GdkPixbuf* raw = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, width, height);
	if (raw == NULL)
	{
		sws_freeContext(sws);
		return NULL;
	}

	uint8_t* dst = gdk_pixbuf_get_pixels(raw);
	int dst_stride = gdk_pixbuf_get_rowstride(raw);

	sws_scale(sws, (const uint8_t* const*)frame->data, frame->linesize,
	          0, height, &dst, &dst_stride);
	sws_freeContext(sws);

	GdkPixbuf* out;
	if (target_width > 0 && target_height > 0 &&
	    (width > target_width || height > target_height))
	{
		guint new_w = width, new_h = height;
		quiver_rect_get_bound_size(target_width, target_height,
		                           &new_w, &new_h, FALSE);
		out = gdk_pixbuf_scale_simple(
			raw, new_w, new_h, GDK_INTERP_BILINEAR);
		g_object_unref(raw);
		if (out == NULL)
			return NULL;
	}
	else
	{
		out = raw;
	}

	/* apply display-matrix / rotation so the still matches playback */
	if (rotation != 0)
	{
		GdkPixbufRotation rot =
			(rotation == 90)  ? GDK_PIXBUF_ROTATE_COUNTERCLOCKWISE :
			(rotation == 270) ? GDK_PIXBUF_ROTATE_CLOCKWISE :
			GDK_PIXBUF_ROTATE_UPSIDEDOWN;
		GdkPixbuf* rotated = gdk_pixbuf_rotate_simple(out, rot);
		g_object_unref(out);
		if (rotated != NULL)
			out = rotated;
	}

	return out;
}

/* Open the file, find the video stream, decode the requested frame and
 * return a new pixbuf (or NULL).  Frees all libav resources it allocates. */
static GdkPixbuf* grab_frame(const gchar* uri,
	gint64 position_ns, gint target_width, gint target_height,
	gint* aspect_n, gint* aspect_d,
	VideoAbortFn abort_fn, gpointer abort_data)
{
	GdkPixbuf* result = NULL;

	VideoSession s = open_session(uri);
	if (!s.ok)
		return NULL;
	if (abort_fn != NULL && abort_fn(abort_data))
	{
		close_session(s);
		return NULL;
	}
	AVStream* st = s.st;
	AVCodecParameters* params = st->codecpar;
	AVCodecContext* ctx = s.ctx;
	AVFormatContext* fmt = s.fmt;
	int video_stream = s.video_stream;

	/* pixel aspect ratio */
	if (aspect_n != NULL && aspect_d != NULL)
	{
		AVRational par = (st->sample_aspect_ratio.num != 0)
			? st->sample_aspect_ratio : (AVRational){1, 1};
		*aspect_n = par.num;
		*aspect_d = par.den;
	}

	/* seek to a requested timestamp */
	if (position_ns >= 0)
	{
		av_seek_frame(fmt, video_stream, position_ns, AVSEEK_FLAG_BACKWARD);
		avcodec_flush_buffers(ctx);
	}

	AVFrame* frame = av_frame_alloc();
	AVPacket* pkt = av_packet_alloc();
	if (frame == NULL || pkt == NULL)
	{
		av_packet_free(&pkt);
		av_frame_free(&frame);
		close_session(s);
		return NULL;
	}

	int frame_w = params->width;
	int frame_h = params->height;

	while (av_read_frame(fmt, pkt) >= 0)
	{
		if (pkt->stream_index != video_stream)
		{
			av_packet_unref(pkt);
			continue;
		}

		avcodec_send_packet(ctx, pkt);
		av_packet_unref(pkt);

		while (avcodec_receive_frame(ctx, frame) == 0)
		{
			/* mid-decode cancellation */
			if (abort_fn != NULL && abort_fn(abort_data))
			{
				av_frame_unref(frame);
				result = NULL;
				goto done;
			}

			frame_w = frame->width;
			frame_h = frame->height;
			int rotation = frame_rotation_deg(frame, st->metadata);

			if (position_ns < 0)
			{
				/* first frame: grab it and stop */
				result = frame_to_pixbuf(frame, frame_w, frame_h,
				                         target_width, target_height, rotation);
				av_frame_unref(frame);
				goto done;
			}
			else
			{
				/* seeks land on the nearest keyframe; decode forward until we
				 * pass the desired timestamp, then keep that frame. */
				int64_t pts_ns = (frame->pts != AV_NOPTS_VALUE)
					? av_rescale_q(frame->pts, st->time_base, AV_TIME_BASE_Q)
					: 0;
				if (pts_ns >= position_ns)
				{
					result = frame_to_pixbuf(frame, frame_w, frame_h,
					                         target_width, target_height, rotation);
					av_frame_unref(frame);
					goto done;
				}
				av_frame_unref(frame);
			}
		}
	}

done:
	av_packet_free(&pkt);
	av_frame_free(&frame);
	close_session(s);
	return result;
}

GdkPixbuf* LoadPixbuf(const gchar *uri,
	gint* pixel_aspect_ratio_numerator,
	gint* pixel_aspect_ratio_denominator,
	gint64 position_ns,
	gint target_width,
	gint target_height,
	VideoAbortFn abort_fn,
	gpointer abort_data)
{
	return grab_frame(uri, position_ns, target_width, target_height,
	                  pixel_aspect_ratio_numerator, pixel_aspect_ratio_denominator,
	                  abort_fn, abort_data);
}

gboolean Probe(const gchar *uri,
	gint64* duration_ns,
	gint* width,
	gint* height,
	gint* pixel_aspect_ratio_numerator,
	gint* pixel_aspect_ratio_denominator)
{
	VideoSession s = open_session(uri);
	if (!s.ok)
		return FALSE;

	if (duration_ns != NULL)
		*duration_ns = (s.fmt->duration > 0) ? s.fmt->duration : 0;

	AVRational par = (s.st->sample_aspect_ratio.num != 0)
		? s.st->sample_aspect_ratio : (AVRational){1, 1};
	int w = s.st->codecpar->width;
	int h = s.st->codecpar->height;

	/* the display size is the coded size, plus pixel aspect ratio, plus any
	 * rotation.  rotation can only be known by decoding a frame (it lives in
	 * the display-matrix side data), so decode the first frame for it. */
	int rotation = probe_rotation(s);
	if (rotation == 90 || rotation == 270)
		std::swap(w, h);

	if (width != NULL) *width = w;
	if (height != NULL) *height = h;
	if (pixel_aspect_ratio_numerator != NULL && pixel_aspect_ratio_denominator != NULL)
	{
		*pixel_aspect_ratio_numerator = par.num;
		*pixel_aspect_ratio_denominator = par.den;
	}

	close_session(s);
	return TRUE;
}

}
