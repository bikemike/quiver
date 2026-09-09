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
#include <memory>
#include <algorithm>

#include <libquiver/quiver-pixbuf-utils.h>
#include "QuiverUtils.h"

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

struct VideoInterruptContext {
	gint64 deadline_us = 0;
	VideoAbortFn abort_fn = NULL;
	gpointer abort_data = NULL;
};

static int video_interrupt_cb(void* opaque)
{
	if (opaque == NULL) return 0;
	VideoInterruptContext* ctx = static_cast<VideoInterruptContext*>(opaque);
	if (ctx->abort_fn != NULL && ctx->abort_fn(ctx->abort_data))
		return 1;
	if (ctx->deadline_us > 0 && g_get_monotonic_time() > ctx->deadline_us)
		return 1;
	return 0;
}

/* Open the file, find its first video stream and open a decoder for it.
 * On failure all members are reset and ok is FALSE. */
struct VideoSession {
	AVFormatContext* fmt = NULL;
	int video_stream = -1;
	AVStream* st = NULL;
	AVCodecContext* ctx = NULL;
	bool ok = false;
	std::shared_ptr<VideoInterruptContext> cb_ctx;
};

static VideoSession open_session(const gchar* uri, VideoAbortFn abort_fn = NULL, gpointer abort_data = NULL)
{
	VideoSession s;
	std::string path;
	if (!uri_to_path(uri, path))
		return s;

	s.cb_ctx = std::make_shared<VideoInterruptContext>();
	s.cb_ctx->deadline_us = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;
	s.cb_ctx->abort_fn = abort_fn;
	s.cb_ctx->abort_data = abort_data;

	s.fmt = avformat_alloc_context();
	if (s.fmt == NULL)
		return s;

	s.fmt->interrupt_callback.callback = video_interrupt_cb;
	s.fmt->interrupt_callback.opaque = s.cb_ctx.get();

	AVDictionary* opts = NULL;
	av_dict_set(&opts, "probesize", "5000000", 0); // 5 MB
	av_dict_set(&opts, "analyzeduration", "2000000", 0); // 2 seconds

	if (avformat_open_input(&s.fmt, path.c_str(), NULL, &opts) != 0)
	{
		av_dict_free(&opts);
		s.fmt = NULL;
		return s;
	}
	av_dict_free(&opts);

	s.fmt->fps_probe_size = 0;
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
	int packet_count = 0;

	while (packet_count++ < 50 && av_read_frame(s.fmt, pkt) >= 0)
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

static void configure_sws_colorspace(struct SwsContext* sws, AVFrame* frame)
{
	int in_full, out_full, brightness, contrast, saturation;
	const int* inv_table;
	const int* table;

	if (sws_getColorspaceDetails(sws, (int**)&inv_table, &in_full,
	                             (int**)&table, &out_full,
	                             &brightness, &contrast, &saturation) < 0)
	{
		return;
	}

	const int* in_table = inv_table;
	if (frame->colorspace == AVCOL_SPC_BT709)
		in_table = sws_getCoefficients(SWS_CS_ITU709);
	else if (frame->colorspace == AVCOL_SPC_BT2020_NCL || frame->colorspace == AVCOL_SPC_BT2020_CL)
		in_table = sws_getCoefficients(SWS_CS_BT2020);
	else if (frame->colorspace == AVCOL_SPC_SMPTE170M || frame->colorspace == AVCOL_SPC_BT470BG)
		in_table = sws_getCoefficients(SWS_CS_ITU601);
	else if (frame->width >= 1280 || frame->height >= 720)
		in_table = sws_getCoefficients(SWS_CS_ITU709);
	else
		in_table = sws_getCoefficients(SWS_CS_ITU601);

	in_full = (frame->color_range == AVCOL_RANGE_JPEG) ? 1 : 0;
	out_full = 1; // RGB target is full-range

	sws_setColorspaceDetails(sws, in_table, in_full,
	                         table, out_full,
	                         brightness, contrast, saturation);
}

static GdkTexture* frame_to_texture(AVFrame* frame, int width, int height,
                                    gint target_width, gint target_height,
                                    int rotation)
{
	if (frame == NULL || frame->data[0] == NULL || width < 1 || height < 1)
		return NULL;

	int out_w = width;
	int out_h = height;
	if (target_width > 0 && target_height > 0 &&
	    (width > target_width || height > target_height))
	{
		guint new_w = width, new_h = height;
		quiver_rect_get_bound_size(target_width, target_height,
		                           &new_w, &new_h, FALSE);
		out_w = (int)new_w;
		out_h = (int)new_h;
	}

	struct SwsContext* sws = sws_getContext(
		width, height, (AVPixelFormat)frame->format,
		out_w, out_h, AV_PIX_FMT_RGBA,
		SWS_BILINEAR, NULL, NULL, NULL);
	if (sws == NULL)
		return NULL;

	configure_sws_colorspace(sws, frame);

	uint8_t* dst_data[4] = { NULL, NULL, NULL, NULL };
	int dst_linesize[4] = { 0, 0, 0, 0 };
	int ret = av_image_alloc(dst_data, dst_linesize, out_w, out_h, AV_PIX_FMT_RGBA, 32);
	if (ret < 0 || dst_data[0] == NULL)
	{
		sws_freeContext(sws);
		return NULL;
	}

	sws_scale(sws, (const uint8_t* const*)frame->data, frame->linesize,
	          0, height, dst_data, dst_linesize);
	sws_freeContext(sws);

	if (rotation != 0)
	{
		int rot_w = (rotation == 90 || rotation == 270) ? out_h : out_w;
		int rot_h = (rotation == 90 || rotation == 270) ? out_w : out_h;
		gsize rot_stride = (gsize)rot_w * 4;
		uint32_t *src_pixels = (uint32_t*)dst_data[0];
		uint32_t *rot_pixels = (uint32_t*)g_malloc0((size_t)rot_w * rot_h * 4);

		for (int y = 0; y < rot_h; ++y)
		{
			for (int x = 0; x < rot_w; ++x)
			{
				int sx = 0, sy = 0;
				if (rotation == 90)
				{
					sx = y;
					sy = out_h - 1 - x;
				}
				else if (rotation == 180)
				{
					sx = out_w - 1 - x;
					sy = out_h - 1 - y;
				}
				else if (rotation == 270)
				{
					sx = out_w - 1 - y;
					sy = x;
				}
				if (sx >= 0 && sx < out_w && sy >= 0 && sy < out_h)
				{
					rot_pixels[y * rot_w + x] = src_pixels[sy * (dst_linesize[0] / 4) + sx];
				}
			}
		}
		av_free(dst_data[0]);

		GBytes *bytes = g_bytes_new_take(rot_pixels, (gsize)rot_w * rot_h * 4);
		GdkTexture *tex = gdk_memory_texture_new(rot_w, rot_h, GDK_MEMORY_R8G8B8A8, bytes, rot_stride);
		g_bytes_unref(bytes);
		return tex;
	}
	else
	{
		GBytes *bytes = g_bytes_new_with_free_func(dst_data[0], (gsize)dst_linesize[0] * out_h, (GDestroyNotify)av_free, dst_data[0]);
		GdkTexture *tex = gdk_memory_texture_new(out_w, out_h, GDK_MEMORY_R8G8B8A8, bytes, dst_linesize[0]);
		g_bytes_unref(bytes);
		return tex;
	}
}

static GdkTexture* grab_frame_texture(const gchar* uri,
	gint64 position_ns, gint target_width, gint target_height,
	gint* aspect_n, gint* aspect_d,
	VideoAbortFn abort_fn, gpointer abort_data)
{
	GdkTexture* result = NULL;

	VideoSession s = open_session(uri, abort_fn, abort_data);
	if (!s.ok)
		return NULL;
	if (abort_fn != NULL && abort_fn(abort_data))
	{
		close_session(s);
		return NULL;
	}
	AVStream* st = s.st;
	AVCodecContext* ctx = s.ctx;
	AVFormatContext* fmt = s.fmt;
	int video_stream = s.video_stream;

	if (aspect_n != NULL && aspect_d != NULL)
	{
		AVRational par = (st->sample_aspect_ratio.num != 0)
			? st->sample_aspect_ratio : (AVRational){1, 1};
		*aspect_n = par.num;
		*aspect_d = par.den;
	}

	if (position_ns >= 0)
	{
		AVRational ns_base = {1, 1000000000};
		int64_t seek_pts = av_rescale_q(position_ns, ns_base, st->time_base);
		av_seek_frame(fmt, video_stream, seek_pts, AVSEEK_FLAG_BACKWARD);
		avcodec_flush_buffers(ctx);
	}

	AVFrame* frame = av_frame_alloc();
	AVFrame* last_frame = (position_ns >= 0) ? av_frame_alloc() : NULL;
	AVPacket* pkt = av_packet_alloc();
	if (frame == NULL || pkt == NULL || (position_ns >= 0 && last_frame == NULL))
	{
		av_packet_free(&pkt);
		av_frame_free(&frame);
		if (last_frame != NULL)
			av_frame_free(&last_frame);
		close_session(s);
		return NULL;
	}

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
			if (abort_fn != NULL && abort_fn(abort_data))
			{
				av_frame_unref(frame);
				result = NULL;
				goto done;
			}

			int frame_w = frame->width;
			int frame_h = frame->height;
			int rotation = frame_rotation_deg(frame, st->metadata);

			if (position_ns < 0)
			{
				result = frame_to_texture(frame, frame_w, frame_h,
				                          target_width, target_height, rotation);
				av_frame_unref(frame);
				goto done;
			}
			else
			{
				AVRational ns_base = {1, 1000000000};
				int64_t pts_ns = (frame->pts != AV_NOPTS_VALUE)
					? av_rescale_q(frame->pts, st->time_base, ns_base)
					: 0;
				if (pts_ns >= position_ns)
				{
					result = frame_to_texture(frame, frame_w, frame_h,
					                          target_width, target_height, rotation);
					av_frame_unref(frame);
					goto done;
				}
				if (last_frame != NULL)
				{
					av_frame_unref(last_frame);
					av_frame_ref(last_frame, frame);
				}
				av_frame_unref(frame);
			}
		}
	}

done:
	if (result == NULL && last_frame != NULL && last_frame->width > 0)
	{
		int last_w = last_frame->width;
		int last_h = last_frame->height;
		int last_rotation = frame_rotation_deg(last_frame, st->metadata);
		result = frame_to_texture(last_frame, last_w, last_h,
		                          target_width, target_height, last_rotation);
	}
	av_packet_free(&pkt);
	av_frame_free(&frame);
	if (last_frame != NULL)
		av_frame_free(&last_frame);
	close_session(s);
	return result;
}

#if HAVE_GDK_PIXBUF
static GdkPixbuf* frame_to_pixbuf(AVFrame* frame, int width, int height,
                                  gint target_width, gint target_height,
                                  int rotation)
{
	if (frame == NULL || frame->data[0] == NULL || width < 1 || height < 1)
		return NULL;

	int out_w = width;
	int out_h = height;
	if (target_width > 0 && target_height > 0 &&
	    (width > target_width || height > target_height))
	{
		guint new_w = width, new_h = height;
		quiver_rect_get_bound_size(target_width, target_height,
		                           &new_w, &new_h, FALSE);
		out_w = (int)new_w;
		out_h = (int)new_h;
	}

	struct SwsContext* sws = sws_getContext(
		width, height, (AVPixelFormat)frame->format,
		out_w, out_h, AV_PIX_FMT_RGB24,
		SWS_BILINEAR, NULL, NULL, NULL);
	if (sws == NULL)
		return NULL;

	configure_sws_colorspace(sws, frame);

	uint8_t* dst_data[4] = { NULL, NULL, NULL, NULL };
	int dst_linesize[4] = { 0, 0, 0, 0 };
	int ret = av_image_alloc(dst_data, dst_linesize, out_w, out_h, AV_PIX_FMT_RGB24, 32);
	if (ret < 0 || dst_data[0] == NULL)
	{
		sws_freeContext(sws);
		return NULL;
	}

	sws_scale(sws, (const uint8_t* const*)frame->data, frame->linesize,
	          0, height, dst_data, dst_linesize);
	sws_freeContext(sws);

	GdkPixbuf* out = gdk_pixbuf_new_from_data(
		dst_data[0], GDK_COLORSPACE_RGB, FALSE, 8,
		out_w, out_h, dst_linesize[0],
		[](guchar* pixels, gpointer data) {
			(void)data;
			av_free(pixels);
		}, NULL);

	if (out == NULL)
	{
		av_free(dst_data[0]);
		return NULL;
	}

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

static GdkPixbuf* grab_frame_pixbuf(const gchar* uri,
	gint64 position_ns, gint target_width, gint target_height,
	gint* aspect_n, gint* aspect_d,
	VideoAbortFn abort_fn, gpointer abort_data)
{
	GdkPixbuf* result = NULL;

	VideoSession s = open_session(uri, abort_fn, abort_data);
	if (!s.ok)
		return NULL;
	if (abort_fn != NULL && abort_fn(abort_data))
	{
		close_session(s);
		return NULL;
	}
	AVStream* st = s.st;
	AVCodecContext* ctx = s.ctx;
	AVFormatContext* fmt = s.fmt;
	int video_stream = s.video_stream;

	if (aspect_n != NULL && aspect_d != NULL)
	{
		AVRational par = (st->sample_aspect_ratio.num != 0)
			? st->sample_aspect_ratio : (AVRational){1, 1};
		*aspect_n = par.num;
		*aspect_d = par.den;
	}

	if (position_ns >= 0)
	{
		AVRational ns_base = {1, 1000000000};
		int64_t seek_pts = av_rescale_q(position_ns, ns_base, st->time_base);
		av_seek_frame(fmt, video_stream, seek_pts, AVSEEK_FLAG_BACKWARD);
		avcodec_flush_buffers(ctx);
	}

	AVFrame* frame = av_frame_alloc();
	AVFrame* last_frame = (position_ns >= 0) ? av_frame_alloc() : NULL;
	AVPacket* pkt = av_packet_alloc();
	if (frame == NULL || pkt == NULL || (position_ns >= 0 && last_frame == NULL))
	{
		av_packet_free(&pkt);
		av_frame_free(&frame);
		if (last_frame != NULL)
			av_frame_free(&last_frame);
		close_session(s);
		return NULL;
	}

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
			if (abort_fn != NULL && abort_fn(abort_data))
			{
				av_frame_unref(frame);
				result = NULL;
				goto done_pixbuf;
			}

			int frame_w = frame->width;
			int frame_h = frame->height;
			int rotation = frame_rotation_deg(frame, st->metadata);

			if (position_ns < 0)
			{
				result = frame_to_pixbuf(frame, frame_w, frame_h,
				                         target_width, target_height, rotation);
				av_frame_unref(frame);
				goto done_pixbuf;
			}
			else
			{
				AVRational ns_base = {1, 1000000000};
				int64_t pts_ns = (frame->pts != AV_NOPTS_VALUE)
					? av_rescale_q(frame->pts, st->time_base, ns_base)
					: 0;
				if (pts_ns >= position_ns)
				{
					result = frame_to_pixbuf(frame, frame_w, frame_h,
					                         target_width, target_height, rotation);
					av_frame_unref(frame);
					goto done_pixbuf;
				}
				if (last_frame != NULL)
				{
					av_frame_unref(last_frame);
					av_frame_ref(last_frame, frame);
				}
				av_frame_unref(frame);
			}
		}
	}

done_pixbuf:
	if (result == NULL && last_frame != NULL && last_frame->width > 0)
	{
		int last_w = last_frame->width;
		int last_h = last_frame->height;
		int last_rotation = frame_rotation_deg(last_frame, st->metadata);
		result = frame_to_pixbuf(last_frame, last_w, last_h,
		                         target_width, target_height, last_rotation);
	}
	av_packet_free(&pkt);
	av_frame_free(&frame);
	if (last_frame != NULL)
		av_frame_free(&last_frame);
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
	return grab_frame_pixbuf(uri, position_ns, target_width, target_height,
	                         pixel_aspect_ratio_numerator, pixel_aspect_ratio_denominator,
	                         abort_fn, abort_data);
}
#endif

GdkTexture* LoadTexture(const gchar *uri,
	gint* pixel_aspect_ratio_numerator,
	gint* pixel_aspect_ratio_denominator,
	gint64 position_ns,
	gint target_width,
	gint target_height,
	VideoAbortFn abort_fn,
	gpointer abort_data)
{
	return grab_frame_texture(uri, position_ns, target_width, target_height,
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
	{
		AVRational ns_base = {1, 1000000000};
		*duration_ns = (s.fmt->duration > 0)
			? av_rescale_q(s.fmt->duration, AV_TIME_BASE_Q, ns_base)
			: 0;
	}

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
