#include <config.h>

#include "ImageLoader.h"
#include "ImageDecoder.h"
#include "IPixbufLoaderObserver.h"
#include "QuiverUtils.h"
#include "QuiverVideoOps.h"

#include <gio/gio.h>
#include <gst/gst.h>
#include <vector>

#if HAVE_GDK_PIXBUF
#include <gdk-pixbuf/gdk-pixbuf-animation.h>
#endif

#include <libquiver/quiver-pixbuf-utils.h>
#include <string.h>
#include <sched.h>

using namespace std;

#if HAVE_GDK_PIXBUF
/* returns TRUE for mime types that we want to decode through GdkPixbuf even
 * when a faster backend is available, so that animated images (currently
 * GIF) are decoded with their animation intact. */
static bool quiver_is_animation_capable_mime(const char *mime)
{
	return (NULL != mime && 0 == g_ascii_strcasecmp(mime, "image/gif"));
}
/* True when @pb has exactly the width/height/channels of the previously
 * captured frame and identical pixel content.  GdkPixbufAnimationIter hands
 * back the same pixbuf for consecutive frames and only mutates its buffer,
 * so this must be checked before the iterator advances again. */
static bool quiver_pixbuf_matches(const GdkPixbuf *pb, const std::vector<guchar> &prev,
                                  gint prev_w, gint prev_h, gint prev_nch)
{
	if (prev.empty())
		return false;
	gint w = gdk_pixbuf_get_width(pb);
	gint h = gdk_pixbuf_get_height(pb);
	gint nch = gdk_pixbuf_get_n_channels(pb);
	if (w != prev_w || h != prev_h || nch != prev_nch)
		return false;
	gint rs = gdk_pixbuf_get_rowstride(pb);
	if (rs < (w * nch) || (gsize)h * (gsize)rs > prev.size())
		return false;
	const guchar *pix = gdk_pixbuf_get_pixels(pb);
	for (gint y = 0; y < h; y++)
	{
		if (memcmp(pix + (gsize)y * rs, prev.data() + (gsize)y * rs, (gsize)w * nch) != 0)
			return false;
	}
	return true;
}

/* Extract backend-neutral frame textures from a GdkPixbuf animation.  Each
 * texture inside *frames carries one owned reference; the caller releases the
 * arrays with quiver_animation_frames_free().  Per-frame delays are in
 * milliseconds (first frame = 0, displayed immediately).  Hard caps bound
 * pathological files, mirroring the glycin decoder. */
static void LoadAnimatedFramesFromPixbuf(GdkPixbufAnimation *anim,
                                         GdkTexture ***frames,
                                         gint **delays_ms,
                                         gsize *n_frames)
{
	*frames = NULL;
	*delays_ms = NULL;
	*n_frames = 0;

	if (NULL == anim || gdk_pixbuf_animation_is_static_image(anim))
		return;

	const gsize kMaxFrames = 512;
	const guint64 kMaxPixels = 64u * 1024 * 1024;

	GdkPixbufAnimationIter *iter = gdk_pixbuf_animation_get_iter(anim, NULL);
	if (NULL == iter)
		return;

	std::vector<GdkTexture*> vec_frames;
	std::vector<gint> vec_delays;
	guint64 total_pixels = 0;

	/* The GIF frame table often pads long stretches with pixel-identical
	 * repeats of the same composited image (e.g. every entry of a static
	 * region carries its own 100 ms delay, or an encoder stores each frame
	 * several times).  Playing every raw table entry bakes those repeats into
	 * minutes of frozen output, so consecutive identical frames are collapsed
	 * into one texture that keeps the delay of the first entry in the run.
	 * The iterator reuses one pixbuf, so a snapshot of the previous frame is
	 * kept for comparison. */
	std::vector<guchar> prev_pixels;
	gint prev_width = 0, prev_height = 0, prev_nch = 0;

	auto push_frame = [&](GdkPixbuf *pb, gint delay_ms) -> bool {
		GdkTexture *tex = QuiverUtils::PixbufToTexture(pb);
		if (NULL == tex)
			return false;
		gint w = gdk_pixbuf_get_width(pb);
		gint h = gdk_pixbuf_get_height(pb);
		gint rs = gdk_pixbuf_get_rowstride(pb);
		gint nch = gdk_pixbuf_get_n_channels(pb);
		if (rs > 0 && h > 0)
		{
			const guchar *pix = gdk_pixbuf_get_pixels(pb);
			if (pix)
			{
				prev_pixels.assign((gsize)h * (gsize)rs, 0);
				memcpy(prev_pixels.data(), pix, (gsize)h * (gsize)rs);
				prev_width = w;
				prev_height = h;
				prev_nch = nch;
			}
		}
		total_pixels += (guint64)w * (guint64)h;
		vec_frames.push_back(tex);
		vec_delays.push_back(delay_ms < 0 ? 0 : delay_ms);
		return true;
	};

	GdkPixbuf *first_pb = gdk_pixbuf_animation_iter_get_pixbuf(iter);
	if (NULL != first_pb)
	{
		gint delay_ms = gdk_pixbuf_animation_iter_get_delay_time(iter);
		push_frame(first_pb, delay_ms);
	}

	while (vec_frames.size() < kMaxFrames
		&& gdk_pixbuf_animation_iter_advance(iter, NULL))
	{
		GdkPixbuf *frame_pb = gdk_pixbuf_animation_iter_get_pixbuf(iter);
		if (NULL == frame_pb)
			break;

		gint delay_ms = gdk_pixbuf_animation_iter_get_delay_time(iter);
		delay_ms = delay_ms < 0 ? 0 : delay_ms;

		/* Pixel-identical to the previously captured frame?  Drop this
		 * repeat: it is padding for a static region rather than a new
		 * drawing state, so it must not extend the visible hold time. */
		if (quiver_pixbuf_matches(frame_pb, prev_pixels, prev_width, prev_height, prev_nch))
			continue;

		total_pixels += (guint64)gdk_pixbuf_get_width(frame_pb) * (guint64)gdk_pixbuf_get_height(frame_pb);
		if (total_pixels > kMaxPixels)
			break;

		if (!push_frame(frame_pb, delay_ms))
			break;
	}

	g_object_unref(iter);

	if (vec_frames.size() < 2)
	{
		for (size_t i = 0; i < vec_frames.size(); i++)
			g_object_unref(vec_frames[i]);
		return;
	}

	*frames = (GdkTexture**)g_new0(GdkTexture*, vec_frames.size());
	*delays_ms = (gint*)g_new0(gint, vec_frames.size());
	for (size_t i = 0; i < vec_frames.size(); i++)
	{
		(*frames)[i] = vec_frames[i];
		(*delays_ms)[i] = vec_delays[i];
	}
	*n_frames = vec_frames.size();
}
#endif

// this matrix calculates the orientation needed
// to get from [source] orientation to a [dest]
// orientation. for instance, to get from a 6 to
// an 8, a 180 is needed (which is a 3)
static int reorientation_matrix[9][9] =
{
	{1,1,2,3,4,5,6,7,8},
	{1,1,2,3,4,5,6,7,8},
	{2,2,1,4,3,8,7,6,5},
	{3,3,4,1,2,7,8,5,6},
	{4,4,3,2,1,6,5,8,7},
	{5,5,6,7,8,1,2,3,4},
	{8,8,7,6,5,2,1,4,3},
	{7,7,8,5,6,3,4,1,2},
	{6,6,5,8,7,4,3,2,1},
	
};

static int combine_matrix[9][9] =
{
	{1,1,2,3,4,5,6,7,8,},
	{1,1,2,3,4,5,6,7,8,},
	{2,2,1,4,3,8,7,6,5,},
	{3,3,4,1,2,7,8,5,6,},
	{4,4,3,2,1,6,5,8,7,},
	{5,5,6,7,8,1,2,3,4,},
	{6,6,5,8,7,4,3,2,1,},
	{7,7,8,5,6,3,4,1,2,},
	{8,8,7,6,5,2,1,4,3,},
};

static int inverse_matrix[9] = {1,1,2,3,4,7,8,5,6};

static GdkTexture* reorient_texture(GdkTexture *tex, int orientation)
{
	if (!tex || orientation <= 1)
		return tex;

	GdkTexture *new_tex = QuiverUtils::TextureExifReorientate(tex, orientation);
	g_object_unref(tex);
	return new_tex;
}

ImageLoader::ImageLoader() : m_ImageCache(4)
{
	//Timer t("ImageLoader::ImageLoader()");
	pthread_cond_init(&m_Condition,NULL);
	pthread_mutex_init(&m_CommandMutex, NULL);

	g_mutex_init(&m_csObservers);
	
	AddPixbufLoaderObserver(this);
	m_iLoadOrientation = 1;
	m_pThumbnailCache = nullptr;

	m_bStopThread = false;
	m_bWorking = false;
	m_bThreadJoined = false;
	m_bQuickPreview = true;
	//Timer t("ImageLoader::Start()");
	pthread_create(&m_pthread_id, NULL, run, this);
}

void ImageLoader::StopThread()
{
	pthread_mutex_lock (&m_CommandMutex);
	m_bStopThread = true;
	pthread_cond_signal(&m_Condition);
	pthread_mutex_unlock (&m_CommandMutex);

	if (!m_bThreadJoined.exchange(true))
	{
		pthread_join(m_pthread_id, NULL);
	}
}

ImageLoader::~ImageLoader()
{
	StopThread();

	pthread_cond_destroy(&m_Condition);
	pthread_mutex_destroy(&m_CommandMutex);

	RemovePixbufLoaderObserver(this);

	g_mutex_clear(&m_csObservers);
}


int ImageLoader::Run()
{
	while (true)
	{
		pthread_mutex_lock (&m_CommandMutex);

		// wait for work.  the predicate and the wait share m_CommandMutex
		// so that LoadImage() cannot lose a wakeup to a thread that is
		// between the empty check and cond_wait
		while (0 == m_Commands.size() && !m_bStopThread)
		{
			m_bWorking = false;
			pthread_cond_wait(&m_Condition, &m_CommandMutex);
			m_bWorking = true;
		}

		if (m_bStopThread)
		{
			pthread_mutex_unlock (&m_CommandMutex);
			break;
		}

		while (2 < m_Commands.size())
		{
			m_Commands.pop_front();
		}
		
		if (!m_Commands.empty() && CACHE == m_Commands.front().params.state && LOAD == m_Commands.back().params.state)
		{
			m_Commands.pop_front();
		}
		
		if (!m_Commands.empty())
		{
			m_Command = m_Commands.front();
			m_Commands.pop_front();
		}
		
		pthread_mutex_unlock (&m_CommandMutex);
		
		Load();
		
		sched_yield();
	}
	
	return 0;
}

bool ImageLoader::IsWorking()
{
	return m_bWorking;
}


bool ImageLoader::CommandsPending()
{
	// return true if there are pending commands
	// this can be used to abort the current command
	// and start a new one
	
	bool rval = false;
	bool bLoadPreview = false;
	
	pthread_mutex_lock (&m_CommandMutex);
	
	while (2 < m_Commands.size())
	{
		m_Commands.pop_front();
	}
	
	if (!m_Commands.empty() && CACHE == m_Commands.front().params.state && LOAD == m_Commands.back().params.state)
	{
		m_Commands.pop_front();
	}
	
	if (!m_Commands.empty())
	{
		if (LOAD == m_Commands.front().params.state || LOAD == m_Commands.back().params.state)
		{
			if (LOAD == m_Commands.front().params.state && CACHE == m_Command.params.state)
			{
				if (0 == strcmp(m_Command.quiverFile.GetURI(), m_Commands.front().quiverFile.GetURI()))
				{
					m_Command.params.state = CACHE_LOAD;
					m_Commands.pop_front();
					bLoadPreview = true;
				}
				else
				{
					rval = true;
				}
			}
			else
			{
				rval = true;
			}
		}
	}

	
	pthread_mutex_unlock (&m_CommandMutex);

	// must do this outside of the mutext lock because it locks the gui thread
	if (bLoadPreview)
	{
		m_Command.params.loaded_quick_preview = LoadQuickPreview();
	}
	
	return rval;
}

gboolean ImageLoader::abort_video_load(gpointer data)
{
	ImageLoader* self = (ImageLoader*)data;
	return self->CommandsPending() ? TRUE : FALSE;
}

void ImageLoader::ReCacheImage(QuiverFile f)
{
	LoadParams p = {};
	p.state = CACHE;
	p.reload = true;
	LoadImage(f,p);
}

void ImageLoader::CacheImage(QuiverFile f)
{
	LoadParams p = {};
	p.state = CACHE;
	LoadImage(f,p);
}

void ImageLoader::CacheImageAtSize(QuiverFile f, int width, int height)
{
	LoadParams p = {};
	p.state = CACHE;
	p.max_width = width;
	p.max_height = height;
	LoadImage(f,p);
}

void ImageLoader::ReloadImage(QuiverFile f)
{
	LoadParams p = {};
	p.state = LOAD;
	p.reload = true;

	// now we can load it
	LoadImage(f,p);
}

void ImageLoader::LoadImage(QuiverFile f)
{
	LoadParams p = {};
	p.state = LOAD;

	LoadImage(f,p);
}

void ImageLoader::LoadImageAtSize(QuiverFile f, int width, int height)
{
	LoadParams p = {};
	p.state = LOAD;
	p.max_width = width;
	p.max_height = height;
	LoadImage(f,p);
}

void ImageLoader::LoadImage(QuiverFile f,LoadParams load_params)
{
	if (f.IsFolder())
		return;
	
	Command c;
	
	c.quiverFile = f;
	if (0 == load_params.orientation)
	{
		load_params.orientation = f.GetOrientation();
	}
	
	c.params = load_params;
	
	pthread_mutex_lock (&m_CommandMutex);
	m_Commands.push_back(c);
	pthread_cond_signal(&m_Condition);
	pthread_mutex_unlock (&m_CommandMutex);
}


#if HAVE_GDK_PIXBUF
GdkPixbuf* ImageLoader::GetCachedPixbuf(QuiverFile f)
{
	return m_ImageCache.GetPixbuf(f.GetURI());
}
#endif

GdkTexture* ImageLoader::GetCachedTexture(QuiverFile f)
{
	return m_ImageCache.GetTexture(f.GetURI());
}

void ImageLoader::AddPixbufLoaderObserver(IPixbufLoaderObserver * loader_observer)
{
	g_mutex_lock(&m_csObservers);
	m_observers.push_back(loader_observer);	
	g_mutex_unlock(&m_csObservers);
}

void ImageLoader::RemovePixbufLoaderObserver(IPixbufLoaderObserver * loader_observer)
{
	g_mutex_lock(&m_csObservers);
	m_observers.remove(loader_observer);	
	g_mutex_unlock(&m_csObservers);
}



bool ImageLoader::LoadQuickPreview()
{
	bool rval = false;
	if (m_bQuickPreview && !m_Command.params.no_thumb_preview)
	{
		GdkTexture *thumb_tex = NULL;
		
		if (m_pThumbnailCache)
		{
			thumb_tex = m_pThumbnailCache->GetTexture(m_Command.quiverFile.GetURI());
		}
		if (NULL == thumb_tex && m_Command.quiverFile.HasThumbnail(256))
		{
			thumb_tex = m_Command.quiverFile.GetThumbnailTexture(256);
			if (NULL != thumb_tex && m_pThumbnailCache)
			{
				m_pThumbnailCache->AddTexture(m_Command.quiverFile.GetURI(), thumb_tex);
			}
		}
		if (NULL == thumb_tex && m_Command.quiverFile.HasThumbnail(128))
		{
			thumb_tex = m_Command.quiverFile.GetThumbnailTexture(128);
			if (NULL != thumb_tex && m_pThumbnailCache)
			{
				m_pThumbnailCache->AddTexture(m_Command.quiverFile.GetURI(), thumb_tex);
			}
		}
	
		if (NULL != thumb_tex)
		{
			int width = m_Command.quiverFile.GetWidth();
			int height = m_Command.quiverFile.GetHeight();
			
			// Videos may have m_iWidth/m_iHeight cached from thumbnail
			// generation at a downscaled bound size (e.g. 256x144) rather
			// than the real frame dimensions.  Probe the true dimensions so
			// that the quick-preview thumbnail can be rendered at the video's
			// actual size.
			bool bVideo = m_Command.quiverFile.IsVideo();
			if (bVideo)
			{
				if (m_Command.quiverFile.IsWidthHeightSet())
				{
					width = m_Command.quiverFile.GetWidth();
					height = m_Command.quiverFile.GetHeight();
				}
				else
				{
					gint vw = 0, vh = 0;
					if (QuiverVideoOps::Probe(m_Command.quiverFile.GetURI(), NULL, &vw, &vh, NULL, NULL, abort_video_load, this)
						&& vw > 0 && vh > 0)
					{
						width = vw;
						height = vh;
						m_Command.quiverFile.SetWidth(vw);
						m_Command.quiverFile.SetHeight(vh);
					}
				}
			}
			
			if (4 < m_iLoadOrientation)
			{
				swap(width,height);
			}

			if (m_iLoadOrientation != m_Command.quiverFile.GetOrientation())
			{
				// thumbnail has already been rotated by the exif orientation
				// so we must revert that and calculate the new rotation 
				int orientation = inverse_matrix[m_Command.quiverFile.GetOrientation()];
				int new_orientation = combine_matrix[m_iLoadOrientation][orientation];

				GdkTexture* tex_rotated = QuiverUtils::TextureExifReorientate(thumb_tex, new_orientation);
				if (NULL != tex_rotated)
				{
					g_object_unref(thumb_tex);
					thumb_tex = tex_rotated;
				}
			}

			// For videos the quick-preview thumbnail is a small frame grab;
			// scale it up to the video's real dimensions so that it fills the
			// frame even in "actual size" mode, where the image view renders
			// the texture at its native pixel size until the full-res grab lands.
			if (bVideo
				&& width > 0 && height > 0
				&& (gint)gdk_texture_get_width(thumb_tex) < width
				&& (gint)gdk_texture_get_height(thumb_tex) < height)
			{
				GdkTexture* tex_scaled = QuiverUtils::ScaleTexture(thumb_tex, width, height);
				if (NULL != tex_scaled)
				{
					g_object_unref(thumb_tex);
					thumb_tex = tex_scaled;
				}
			}
			
			list<IPixbufLoaderObserver*>::iterator itr;
			g_mutex_lock(&m_csObservers);
			for (itr = m_observers.begin();itr != m_observers.end() ; ++itr)
			{
				(*itr)->SetTextureAtSize(thumb_tex,width,height);
			}
			g_mutex_unlock(&m_csObservers);
			g_object_unref(thumb_tex);
			
			rval = true;
		}
	}
	return rval;
}

void ImageLoader::Load()
{
	m_iLoadOrientation = m_Command.params.orientation;
	
	if (m_Command.params.reload)
	{
		m_ImageCache.RemoveTexture(m_Command.quiverFile.GetURI());
	}

	// check to see if the image should be removed from the cache
	GdkTexture * texture = m_ImageCache.GetTexture(m_Command.quiverFile.GetURI());
	if (NULL != texture)
	{
		int real_width,real_height;

		gint width,height;
		width = gdk_texture_get_width(texture);
		height = gdk_texture_get_height(texture);
		
		real_width = m_Command.quiverFile.GetWidth();
		real_height = m_Command.quiverFile.GetHeight();
		
		const gint* pOrientation = (const gint*)g_object_get_data(G_OBJECT (texture), "quiver-orientation");
		if (NULL != pOrientation)
		{
			if(m_iLoadOrientation != *pOrientation)
			{
				if ( (4 < m_iLoadOrientation && 4 >= *pOrientation)
					|| (4 >= m_iLoadOrientation && 4 < *pOrientation) )
				{
					// swap because the cached image orientation has a different 
					// ratio for width/height than the requested orientation
					swap(width,height);
				}
				
			}
		}

		if (4 < m_iLoadOrientation)
		{
			// swap because the actual image has a different 
			// ratio for width/height than the requested orientation
			swap(real_width,real_height);
		}

		if ((0 == m_Command.params.max_width && 0 == m_Command.params.max_height &&
			 (width < real_width || height < real_height)) ||
			( width < m_Command.params.max_width &&
			 height < m_Command.params.max_height &&
			 width < real_width && height < real_height))
		{
			m_ImageCache.RemoveTexture(m_Command.quiverFile.GetURI());
		}
				
		g_object_unref(texture);
	}

	if (LOAD == m_Command.params.state)
	{
		texture = m_ImageCache.GetTexture(m_Command.quiverFile.GetURI());
		
		if ( NULL == texture)
		{
			/* A session-level failure stamp for a *transient* decode miss must
			 * not permanently brick a file: always attempt a real decode for
			 * a user-visible LOAD and clear the stamp on success.  The stamp
			 * still guards the background cache preloads against re-decoding
			 * genuinely broken files. */
			if (0 != strcmp(m_Command.quiverFile.GetURI(),""))
			{
				bool bLoadWasFailed = m_ImageCache.HasFailed(m_Command.quiverFile.GetURI());
				bool bLoadedQuickPreview = LoadQuickPreview();
				bool bAborted = false;
				bool bGlycinTransformed = false;
				GdkTexture **anim_frames = NULL;
				gint *anim_delays = NULL;
				gsize anim_count = 0;

				if (m_Command.quiverFile.IsVideo())
				{
					Timer loadTimer;
					gint n=1, d=1;
					gint vw = 0, vh = 0;
					texture = ImageDecoder::DecodeVideoTexture(m_Command.quiverFile.GetURI(), &n, &d,
						-1, m_Command.params.max_width, m_Command.params.max_height,
						abort_video_load, this, &vw, &vh);
					if (NULL == texture && CommandsPending())
						bAborted = true;
					if (NULL != texture)
					{
						/* The grab is already pixel-aspect corrected, so its
						 * size is the display size. */
						guint tex_width  = gdk_texture_get_width(texture);
						guint tex_height = gdk_texture_get_height(texture);

						if (vw <= 0 || vh <= 0)
						{
							vw = (gint)tex_width;
							vh = (gint)tex_height;
						}
						if (vw > 0 && vh > 0
							&& (!m_Command.quiverFile.IsWidthHeightSet()
								|| vw != m_Command.quiverFile.GetWidth()
								|| vh != m_Command.quiverFile.GetHeight()))
						{
							m_Command.quiverFile.SetWidth(vw);
							m_Command.quiverFile.SetHeight(vh);
						}

						m_Command.quiverFile.SetLoadTimeInSeconds(loadTimer.GetRunningTimeInSeconds());
					}
				}
				else
				{

#if HAVE_GLYCIN
					if (ImageDecoder::GetBackend() != ImageDecoderBackend::PIXBUF)
					{
						GFile* gfile = g_file_new_for_uri(m_Command.quiverFile.GetURI());
						if (gfile)
						{
							int w = -1, h = -1;
							if (-1 == m_Command.quiverFile.GetWidth() || -1 == m_Command.quiverFile.GetHeight())
							{
								ImageDecoder::GetDimensions(gfile, m_Command.quiverFile.GetMimeType(), &w, &h);
								if (w > 0 && h > 0)
								{
									m_Command.quiverFile.SetWidth(w);
									m_Command.quiverFile.SetHeight(h);
								}
							}

							Timer loadTimer;
							GError *pDecodeError = NULL;
							texture = ImageDecoder::DecodeFileAnimation(gfile, &anim_frames, &anim_delays, &anim_count, &pDecodeError);
							if (NULL != texture)
							{
								if (NULL != pDecodeError)
								{
									g_error_free(pDecodeError);
									pDecodeError = NULL;
								}
								m_Command.quiverFile.SetLoadTimeInSeconds(loadTimer.GetRunningTimeInSeconds());
								bGlycinTransformed = (g_object_get_data(G_OBJECT(texture), "glycin-transformed") != NULL);
							}
							g_object_unref(gfile);
							if (NULL == texture && NULL != pDecodeError)
							{
								g_warning("ImageLoader: could not decode %s: %s",
									m_Command.quiverFile.GetURI(), pDecodeError->message);
								g_error_free(pDecodeError);
								/* The user has already navigated elsewhere while this
								 * in-flight (non-cancellable) decode was running; treat
								 * the failure as superseded so it neither poisons the
								 * failure cache nor paints the error label. */
								if (CommandsPending())
									bAborted = true;
							}
						}
					}
#endif

#if HAVE_GDK_PIXBUF
					if (NULL == texture && !CommandsPending())
					{
						const char *mime = m_Command.quiverFile.GetMimeType();
						bool bAnimatedMime = quiver_is_animation_capable_mime(mime);

						GdkPixbufLoader* loader = gdk_pixbuf_loader_new_with_mime_type (mime, NULL);

						list<IPixbufLoaderObserver*>::iterator itr;
						g_mutex_lock(&m_csObservers);
						for (itr = m_observers.begin();itr != m_observers.end() ; ++itr)
						{
							if (m_Command.params.reload && m_Command.params.fullsize)
							{
							}
							else if (!bAnimatedMime)
							{
								(*itr)->ConnectSignalSizePrepared(loader);
							}
						}
						g_mutex_unlock(&m_csObservers);

						bool rval = LoadPixbuf(loader, &bAborted);

						if (rval)
						{
							GdkPixbufAnimation *anim = gdk_pixbuf_loader_get_animation(loader);
							if (NULL != anim && !gdk_pixbuf_animation_is_static_image(anim))
							{
								/* Animated image: extract backend-neutral frame
								 * textures; the first frame becomes the still. */
								gint anim_w = gdk_pixbuf_animation_get_width(anim);
								gint anim_h = gdk_pixbuf_animation_get_height(anim);
								if (anim_w > 0 && anim_h > 0 && !m_Command.quiverFile.IsWidthHeightSet())
								{
									m_Command.quiverFile.SetWidth(anim_w);
									m_Command.quiverFile.SetHeight(anim_h);
								}

								GdkPixbuf *static_pb = gdk_pixbuf_animation_get_static_image(anim);
								if (NULL != static_pb)
								{
									texture = QuiverUtils::PixbufToTexture(static_pb);
								}

								if (NULL != texture)
								{
									LoadAnimatedFramesFromPixbuf(anim, &anim_frames, &anim_delays, &anim_count);
								}
							}
							else
							{
								GdkPixbuf *pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);
								if (NULL != pixbuf)
								{
									texture = QuiverUtils::PixbufToTexture(pixbuf);
								}
							}
						}
						g_object_unref(loader);
					}
#endif
				}
					
				if (NULL != texture)
				{
					int orientation = m_iLoadOrientation;
					if (bGlycinTransformed)
					{
						int file_ori = m_Command.quiverFile.GetOrientation();
						int needed_ori = reorientation_matrix[file_ori][orientation];
						if (needed_ori > 1)
						{
							texture = reorient_texture(texture, needed_ori);
						}
					}
					else if (orientation > 1)
					{
						texture = reorient_texture(texture, orientation);
					}

if (NULL != anim_frames && orientation > 1)
					{
						/* The animation frames cannot be cleanly re-orientated
						 * with the still texture, so fall back to the rotated
						 * first frame only. */
						quiver_animation_frames_free(anim_frames, anim_delays, anim_count);
						anim_frames = NULL;
						anim_delays = NULL;
						anim_count = 0;
					}
					
					gint width,height;
					width = m_Command.quiverFile.GetWidth();
					height = m_Command.quiverFile.GetHeight();
					if (4 < orientation)
					{
						swap(width,height);
					}

					gint *pOrientation = g_new(int,1);
					*pOrientation = orientation;
					g_object_set_data_full (G_OBJECT (texture), "quiver-orientation", pOrientation,g_free);

if (NULL != anim_frames && anim_count >= 2)
						{
							m_ImageCache.AddTexture(m_Command.quiverFile.GetURI(), texture, anim_frames, anim_delays, anim_count);
							quiver_animation_frames_free(anim_frames, anim_delays, anim_count);
							anim_frames = NULL;
							anim_delays = NULL;
							anim_count = 0;
						}
						else
						{
							if (NULL != anim_frames)
							{
								quiver_animation_frames_free(anim_frames, anim_delays, anim_count);
								anim_frames = NULL;
								anim_delays = NULL;
								anim_count = 0;
							}
							m_ImageCache.AddTexture(m_Command.quiverFile.GetURI(), texture);
						}
						if (bLoadWasFailed)
					{
						m_ImageCache.RemoveFailure(m_Command.quiverFile.GetURI());
					}

					bool bResetViewMode = m_Command.params.reload ? false : !bLoadedQuickPreview;
					NotifyObservers(texture, width, height, bResetViewMode);
					g_object_unref(texture);
				}
				else
				{
					/* Only a real failure of the *current* command may poison the
					 * failure cache or paint the error label; a failure for a load
					 * that has since been superseded is discarded. */
					if (!bAborted && !CommandsPending())
					{
						m_ImageCache.AddFailure(m_Command.quiverFile.GetURI());
						list<IPixbufLoaderObserver*>::iterator itr;
						g_mutex_lock(&m_csObservers);
						for (itr = m_observers.begin();itr != m_observers.end() ; ++itr)
						{
							(*itr)->SetTexture(NULL);
						}
						g_mutex_unlock(&m_csObservers);
					}
				}
			}
			else
			{
				list<IPixbufLoaderObserver*>::iterator itr;
				g_mutex_lock(&m_csObservers);
				for (itr = m_observers.begin();itr != m_observers.end() ; ++itr)
				{
					(*itr)->SetTexture(NULL);
				}
				g_mutex_unlock(&m_csObservers);
			}
		}
		else
		{
			// load from cache
			gint *pOrientation = (gint*)g_object_get_data(G_OBJECT (texture), "quiver-orientation");

			if (NULL != pOrientation && m_Command.params.orientation != *pOrientation)
			{
				int new_orientation = reorientation_matrix[*pOrientation][m_Command.params.orientation];

				GdkTexture* texture_rotated = reorient_texture(texture, new_orientation);
				if (NULL != texture_rotated)
				{
					texture = texture_rotated;

					gint *pNewOrientation = g_new(int,1);
					*pNewOrientation = m_Command.params.orientation;
					g_object_set_data_full (G_OBJECT (texture), "quiver-orientation", pNewOrientation, g_free);

					GdkTexture **cached_frames = NULL;
					gint *cached_delays = NULL;
					gsize cached_count = m_ImageCache.GetAnimationFrames(m_Command.quiverFile.GetURI(), &cached_frames, &cached_delays);
					if (cached_count >= 2)
					{
						m_ImageCache.AddTexture(m_Command.quiverFile.GetURI(), texture, cached_frames, cached_delays, cached_count);
					}
					else
					{
						m_ImageCache.AddTexture(m_Command.quiverFile.GetURI(), texture);
					}
					if (NULL != cached_frames)
					{
						quiver_animation_frames_free(cached_frames, cached_delays, cached_count);
					}
				}
			}

			gint width,height;
			width = m_Command.quiverFile.GetWidth();
			height = m_Command.quiverFile.GetHeight();
			if (4 < m_Command.params.orientation)
			{
				swap(width,height);
			}
			bool bResetViewMode = !m_Command.params.loaded_quick_preview;
			NotifyObservers(texture, width, height, bResetViewMode);
			g_object_unref(texture);
		}
	}	
	else if (CACHE == m_Command.params.state)
	{
		if (!m_ImageCache.InCache(m_Command.quiverFile.GetURI()))
		{
			if (0 != strcmp(m_Command.quiverFile.GetURI(),""))
			{
				bool bAborted = false;
				bool bGlycinTransformed = false;
				GdkTexture *cache_texture = NULL;
				GdkTexture **anim_frames = NULL;
				gint *anim_delays = NULL;
				gsize anim_count = 0;

				if (m_Command.quiverFile.IsVideo())
				{
					Timer loadTimer;
					gint n=1, d=1;
					gint vw = 0, vh = 0;
					cache_texture = ImageDecoder::DecodeVideoTexture(m_Command.quiverFile.GetURI(), &n, &d,
						-1, m_Command.params.max_width, m_Command.params.max_height,
						abort_video_load, this, &vw, &vh);
					if (NULL == cache_texture && CommandsPending())
						bAborted = true;
					if (NULL != cache_texture)
					{
						if (vw <= 0 || vh <= 0)
						{
							vw = (gint)gdk_texture_get_width(cache_texture);
							vh = (gint)gdk_texture_get_height(cache_texture);
							if (n > d)
								vw = (gint)((vw * n) / double(d) + .5);
							else if (d > n)
								vh = (gint)((vh * d) / double(n) + .5);
						}

						if (!m_Command.quiverFile.IsWidthHeightSet())
						{
							m_Command.quiverFile.SetWidth(vw);
							m_Command.quiverFile.SetHeight(vh);
						}

						m_Command.quiverFile.SetLoadTimeInSeconds(loadTimer.GetRunningTimeInSeconds());
					}
				}
				else
				{

#if HAVE_GLYCIN
					if (ImageDecoder::GetBackend() != ImageDecoderBackend::PIXBUF)
					{
						GFile* gfile = g_file_new_for_uri(m_Command.quiverFile.GetURI());
						if (gfile)
						{
							int w = -1, h = -1;
							if (-1 == m_Command.quiverFile.GetWidth() || -1 == m_Command.quiverFile.GetHeight())
							{
								ImageDecoder::GetDimensions(gfile, m_Command.quiverFile.GetMimeType(), &w, &h);
								if (w > 0 && h > 0)
								{
									m_Command.quiverFile.SetWidth(w);
									m_Command.quiverFile.SetHeight(h);
								}
							}

							Timer loadTimer;
							GError *pDecodeError = NULL;
							cache_texture = ImageDecoder::DecodeFileAnimation(gfile, &anim_frames, &anim_delays, &anim_count, &pDecodeError);
							if (NULL != cache_texture)
							{
								if (NULL != pDecodeError)
								{
									g_error_free(pDecodeError);
									pDecodeError = NULL;
								}
								m_Command.quiverFile.SetLoadTimeInSeconds(loadTimer.GetRunningTimeInSeconds());
								bGlycinTransformed = (g_object_get_data(G_OBJECT(cache_texture), "glycin-transformed") != NULL);
							}
							if (NULL == cache_texture && NULL != pDecodeError)
							{
								g_warning("ImageLoader: could not decode %s: %s",
									m_Command.quiverFile.GetURI(), pDecodeError->message);
								g_error_free(pDecodeError);
							}
							g_object_unref(gfile);
						}
					}
#endif

#if HAVE_GDK_PIXBUF
					if (NULL == cache_texture && !CommandsPending())
					{
						const char *mime = m_Command.quiverFile.GetMimeType();
						bool bAnimatedMime = quiver_is_animation_capable_mime(mime);

						GdkPixbufLoader* ldr = gdk_pixbuf_loader_new_with_mime_type (mime, NULL);

						if (!m_Command.params.fullsize && !bAnimatedMime)
						{
							list<IPixbufLoaderObserver*>::iterator itr;
							g_mutex_lock(&m_csObservers);
							for (itr = m_observers.begin();itr != m_observers.end() ; ++itr)
							{
								(*itr)->ConnectSignalSizePrepared(ldr);
							}
							g_mutex_unlock(&m_csObservers);
						}
										
						bool rval = LoadPixbuf(ldr, &bAborted);

						if (rval)
						{
							GdkPixbufAnimation *anim = gdk_pixbuf_loader_get_animation(ldr);
							if (NULL != anim && !gdk_pixbuf_animation_is_static_image(anim))
							{
								gint anim_w = gdk_pixbuf_animation_get_width(anim);
								gint anim_h = gdk_pixbuf_animation_get_height(anim);
								if (anim_w > 0 && anim_h > 0 && !m_Command.quiverFile.IsWidthHeightSet())
								{
									m_Command.quiverFile.SetWidth(anim_w);
									m_Command.quiverFile.SetHeight(anim_h);
								}

								GdkPixbuf *static_pb = gdk_pixbuf_animation_get_static_image(anim);
								if (NULL != static_pb)
								{
									cache_texture = QuiverUtils::PixbufToTexture(static_pb);
								}

								if (NULL != cache_texture)
								{
									LoadAnimatedFramesFromPixbuf(anim, &anim_frames, &anim_delays, &anim_count);
								}
							}
							else
							{
								GdkPixbuf *pb = gdk_pixbuf_loader_get_pixbuf(ldr);
								if (NULL != pb)
								{
									cache_texture = QuiverUtils::PixbufToTexture(pb);
								}
							}
						}

						g_object_unref(ldr);
					}
#endif

					if (NULL != cache_texture)
					{
						int orientation = m_Command.params.orientation;
						if (bGlycinTransformed)
						{
							int file_ori = m_Command.quiverFile.GetOrientation();
							int needed_ori = reorientation_matrix[file_ori][orientation];
							if (needed_ori > 1)
							{
								cache_texture = reorient_texture(cache_texture, needed_ori);
							}
						}
						else if (orientation > 1)
						{
							cache_texture = reorient_texture(cache_texture, orientation);
						}

if (NULL != anim_frames && orientation > 1)
						{
							/* Animated frames cannot be re-orientated cleanly;
							 * cache only the rotated first frame. */
							quiver_animation_frames_free(anim_frames, anim_delays, anim_count);
							anim_frames = NULL;
							anim_delays = NULL;
							anim_count = 0;
						}
						
						gint *pOrientation = g_new(int,1);
						*pOrientation = m_Command.params.orientation;
						g_object_set_data_full (G_OBJECT (cache_texture), "quiver-orientation", pOrientation, g_free);
					}
				}

				if (NULL != cache_texture)
				{
					if (NULL != anim_frames && anim_count >= 2)
					{
						if (CACHE_LOAD == m_Command.params.state)
						{
							m_ImageCache.AddTexture(m_Command.quiverFile.GetURI(), cache_texture, anim_frames, anim_delays, anim_count);
						}
						else
						{
							m_ImageCache.AddTexture(m_Command.quiverFile.GetURI(), cache_texture, anim_frames, anim_delays, anim_count, 0);
						}
						quiver_animation_frames_free(anim_frames, anim_delays, anim_count);
						anim_frames = NULL;
						anim_delays = NULL;
						anim_count = 0;
					}
					else
					{
						if (NULL != anim_frames)
						{
							quiver_animation_frames_free(anim_frames, anim_delays, anim_count);
							anim_frames = NULL;
							anim_delays = NULL;
							anim_count = 0;
						}
						if (CACHE_LOAD == m_Command.params.state)
						{
							m_ImageCache.AddTexture(m_Command.quiverFile.GetURI(), cache_texture);
						}
						else
						{
							m_ImageCache.AddTexture(m_Command.quiverFile.GetURI(), cache_texture, 0);
						}
					}

					if (CACHE_LOAD == m_Command.params.state)
					{
						gint width,height;
						width = m_Command.quiverFile.GetWidth();
						height = m_Command.quiverFile.GetHeight();
						if (4 < m_Command.params.orientation)
						{
							swap(width,height);
						}
						bool bResetViewMode = !m_Command.params.loaded_quick_preview;
						NotifyObservers(cache_texture, width, height, bResetViewMode);
					}
					g_object_unref(cache_texture);
				}
				else
				{
					if (!bAborted && !CommandsPending())
					{
						m_ImageCache.AddFailure(m_Command.quiverFile.GetURI());
						/* Only a LOAD of the current file paints the error label;
						 * a background cache preload failure must not blame the
						 * image the user is actually looking at. */
						if (LOAD == m_Command.params.state)
						{
							list<IPixbufLoaderObserver*>::iterator itr;
							g_mutex_lock(&m_csObservers);
							for (itr = m_observers.begin();itr != m_observers.end() ; ++itr)
							{
								(*itr)->SetTexture(NULL);
							}
							g_mutex_unlock(&m_csObservers);
						}
					}
				}
			}
		}
	}
}

/* Deliver the decoded image (or the cached animation for it) to every
 * registered observer.  When the file was decoded as animated, each observer
 * receives its own copy of the cached frame list instead of the still texture
 * so the image views can play it. */
void ImageLoader::NotifyObservers(GdkTexture *texture, gint width, gint height, bool bResetViewMode)
{
	GdkTexture **frames = NULL;
	gint *delays = NULL;
	gsize count = m_ImageCache.GetAnimationFrames(m_Command.quiverFile.GetURI(), &frames, &delays);

	list<IPixbufLoaderObserver*>::iterator itr;
	g_mutex_lock(&m_csObservers);
	for (itr = m_observers.begin(); itr != m_observers.end(); ++itr)
	{
		if (count >= 2)
		{
			/* The receiver owns this copy and releases it with
			 * quiver_animation_frames_free(). */
			GdkTexture **of = (GdkTexture**)g_new0(GdkTexture*, count);
			gint *od = (gint*)g_new0(gint, count);
			for (gsize i = 0; i < count; i++)
			{
				of[i] = (NULL != frames[i]) ? (GdkTexture*)g_object_ref(frames[i]) : NULL;
				od[i] = delays ? delays[i] : 0;
			}
			(*itr)->SetAnimationFrames(of, od, count, width, height, bResetViewMode);
		}
		else
		{
			(*itr)->SetTextureAtSize(texture, width, height, bResetViewMode);
		}
	}
	g_mutex_unlock(&m_csObservers);

	if (NULL != frames)
		quiver_animation_frames_free(frames, delays, count);
}

#if HAVE_GDK_PIXBUF
bool ImageLoader::LoadPixbuf(GdkPixbufLoader *loader, bool* bAborted /* = NULL */)
{
	//cout << "Loading: " <<  m_Command.quiverFile.GetURI() << endl;
	bool retval = true; // did not load

	GError *tmp_error;
	tmp_error = NULL;
	
	if (CommandsPending())
	{	
		if (NULL != bAborted) *bAborted = true;
		gdk_pixbuf_loader_close(loader,NULL);	
		return false;
	}
	Timer loadTimer; // true for quite mode
	
	//int size = 8192;
	//int size = 16384;
	//int size = 32768;
	const int size = 65536;
	//int size = 131070;
	
	long bytes_read_inc=0, bytes_total=0;

	bytes_total = m_Command.quiverFile.GetFileSize();
	
	GFile* gfile = g_file_new_for_uri(m_Command.quiverFile.GetURI());

	GInputStream* inStream = G_INPUT_STREAM(g_file_read(gfile, NULL, NULL));

	if (NULL != inStream)
	{
		guchar buffer[size];
		gssize bytes_read = 0; 
		list<IPixbufLoaderObserver*>::iterator itr;

		while (0 < (bytes_read = g_input_stream_read(inStream, buffer, size, NULL, NULL)))
		{
			if (CommandsPending())
			{
				if (NULL != bAborted) *bAborted = true;
				gdk_pixbuf_loader_close(loader,NULL);
				g_object_unref(inStream);
				g_object_unref(gfile);
				return false;
			}

			bytes_read_inc += bytes_read;

			// notify others of bytes read
			g_mutex_lock(&m_csObservers);
			for (itr = m_observers.begin();itr != m_observers.end() ; ++itr)
			{
				if (0 < bytes_read_inc && bytes_read_inc <= bytes_total)
				{
					(*itr)->SignalBytesRead(bytes_read_inc,bytes_total);
				}
			}
			g_mutex_unlock(&m_csObservers);
			
			tmp_error = NULL;

			gdk_pixbuf_loader_write (loader, buffer, bytes_read, &tmp_error);

			if (NULL != tmp_error)
			{
				retval = false;
				g_error_free(tmp_error);
				tmp_error = NULL;
				break;
			}
			usleep(50);
		}

		if (-1 == bytes_read)
		{
			retval = false;
		}
		g_object_unref(inStream);
	}
	else
	{
		retval = false;
	}

	m_Command.quiverFile.SetLoadTimeInSeconds( loadTimer.GetRunningTimeInSeconds() );
	
	tmp_error = NULL;
	
	gdk_pixbuf_loader_close(loader,&tmp_error);
	if (NULL != tmp_error)
	{
		retval = false;
		g_error_free(tmp_error);
	}

	g_object_unref(gfile);
	
	return retval;
}

void ImageLoader::SignalSizePrepared(GdkPixbufLoader *loader,gint width, gint height)
{
	if (0 == width || 0 == height)
		return;
	m_Command.quiverFile.SetWidth(width);
	m_Command.quiverFile.SetHeight(height);
	
	int max_width, max_height;
	int orientation = m_iLoadOrientation;
	if (LOAD != m_Command.params.state)
	{
		orientation = m_Command.params.orientation;
	}
	
	max_width = m_Command.params.max_width;
	max_height = m_Command.params.max_height;
	
	if (4 < orientation)
	{
		swap(width,height);
	}
	
	
	if (10 < max_width && 10 < max_height)
	{
		if (max_width < width || max_height < height)
		{
			// adjust the image size
			guint new_width = width;
			guint new_height = height;
			
			quiver_rect_get_bound_size(max_width,max_height,&new_width, &new_height,FALSE);

			if (4 < m_Command.params.orientation)
			{
				swap(new_width,new_height);
				swap(width,height);
			}
			gdk_pixbuf_loader_set_size(loader,new_width,new_height);
		}
	}
}
#endif

void* ImageLoader::run(void * data)
{
	((ImageLoader*)data)->Run();
	return 0;
}


