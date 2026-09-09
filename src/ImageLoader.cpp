#include <config.h>

#include "ImageLoader.h"
#include "ImageDecoder.h"
#include "QuiverUtils.h"
#include "QuiverVideoOps.h"

#include <gio/gio.h>
#include <gst/gst.h>

#include <libquiver/quiver-pixbuf-utils.h>
#include <string.h>
#include <sched.h>

using namespace std;

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

	m_bStopThread = false;
	m_bWorking = false;
	m_bQuickPreview = true;
	//Timer t("ImageLoader::Start()");
	pthread_create(&m_pthread_id, NULL, run, this);
}

ImageLoader::~ImageLoader()
{
	pthread_mutex_lock (&m_CommandMutex);
	m_bStopThread = true;
	pthread_cond_signal(&m_Condition);
	pthread_mutex_unlock (&m_CommandMutex);

	pthread_join(m_pthread_id,NULL);
	
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
		
		if (m_Command.quiverFile.HasThumbnail(256))
		{
			thumb_tex = m_Command.quiverFile.GetThumbnailTexture(256);
		}
		if (NULL == thumb_tex && m_Command.quiverFile.HasThumbnail(128))
		{
			thumb_tex = m_Command.quiverFile.GetThumbnailTexture(128);
		}
	
		if (NULL != thumb_tex)
		{
			int width = m_Command.quiverFile.GetWidth();
			int height = m_Command.quiverFile.GetHeight();
			
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
			if (0 != strcmp(m_Command.quiverFile.GetURI(),"") && !m_ImageCache.HasFailed(m_Command.quiverFile.GetURI()))
			{
				bool bLoadedQuickPreview = LoadQuickPreview();
				bool bAborted = false;
				bool bGlycinTransformed = false;

				if (m_Command.quiverFile.IsVideo())
				{
					Timer loadTimer;
					gint n=1, d=1;
					texture = ImageDecoder::DecodeVideoTexture(m_Command.quiverFile.GetURI(), &n, &d,
						-1, m_Command.params.max_width, m_Command.params.max_height,
						abort_video_load, this);
					if (NULL == texture && CommandsPending())
						bAborted = true;
					if (NULL != texture)
					{
						guint tex_width  = gdk_texture_get_width(texture);
						guint tex_height = gdk_texture_get_height(texture);

						if (n > d)
							tex_width = (guint)((tex_width * n) / float(d) + .5);
						else
							tex_height = (guint)((tex_height * d) / float(n) + .5);

						if (!m_Command.quiverFile.IsWidthHeightSet())
						{
							m_Command.quiverFile.SetWidth(tex_width);
							m_Command.quiverFile.SetHeight(tex_height);
						}

						m_Command.quiverFile.SetLoadTimeInSeconds(loadTimer.GetRunningTimeInSeconds());
					}
				}
				else
				{
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
							texture = ImageDecoder::DecodeFileTexture(gfile, m_Command.quiverFile.GetMimeType(), NULL, NULL);
							if (NULL != texture)
							{
								m_Command.quiverFile.SetLoadTimeInSeconds(loadTimer.GetRunningTimeInSeconds());
								bGlycinTransformed = (g_object_get_data(G_OBJECT(texture), "glycin-transformed") != NULL);
							}
							g_object_unref(gfile);
						}
					}

#if HAVE_GDK_PIXBUF
					if (NULL == texture && !CommandsPending())
					{
						GdkPixbufLoader* loader = gdk_pixbuf_loader_new_with_mime_type (m_Command.quiverFile.GetMimeType(), NULL);	
						
						list<IPixbufLoaderObserver*>::iterator itr;
						g_mutex_lock(&m_csObservers);
						for (itr = m_observers.begin();itr != m_observers.end() ; ++itr)
						{
							if (m_Command.params.reload && m_Command.params.fullsize)
							{
							}
							else
							{
								(*itr)->ConnectSignalSizePrepared(loader);
							}
						}
						g_mutex_unlock(&m_csObservers);

						bool rval = LoadPixbuf(loader, &bAborted);

						if (rval)
						{
							GdkPixbuf *pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);
							if (NULL != pixbuf)
							{
								texture = QuiverUtils::PixbufToTexture(pixbuf);
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
					
					list<IPixbufLoaderObserver*>::iterator itr;
					g_mutex_lock(&m_csObservers);
					for (itr = m_observers.begin();itr != m_observers.end() ; ++itr)
					{
						gint width,height;
						width = m_Command.quiverFile.GetWidth();
						height = m_Command.quiverFile.GetHeight();
						if (4 < orientation)
						{
							swap(width,height);
						}
						if (m_Command.params.reload)
						{
							(*itr)->SetTextureAtSize(texture,width,height,false);
						}
						else
						{
							bool bResetViewMode = !bLoadedQuickPreview;
							(*itr)->SetTextureAtSize(texture,width,height,bResetViewMode);
						}
					}
					g_mutex_unlock(&m_csObservers);

					gint *pOrientation = g_new(int,1);
					*pOrientation = orientation;
					g_object_set_data_full (G_OBJECT (texture), "quiver-orientation", pOrientation,g_free);

					m_ImageCache.AddTexture(m_Command.quiverFile.GetURI(),texture);
					g_object_unref(texture);
				}
				else
				{
					if (!bAborted)
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

					m_ImageCache.AddTexture(m_Command.quiverFile.GetURI(), texture);
				}
			}

			list<IPixbufLoaderObserver*>::iterator itr;
			g_mutex_lock(&m_csObservers);
			for (itr = m_observers.begin();itr != m_observers.end() ; ++itr)
			{
				gint width,height;
				width = m_Command.quiverFile.GetWidth();
				height = m_Command.quiverFile.GetHeight();
				if (4 < m_Command.params.orientation)
				{
					swap(width,height);
				}
				bool bResetViewMode = !m_Command.params.loaded_quick_preview;
				(*itr)->SetTextureAtSize(texture,width,height,bResetViewMode);
			}
			g_mutex_unlock(&m_csObservers);
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

				if (m_Command.quiverFile.IsVideo())
				{
					Timer loadTimer;
					gint n=1, d=1;
					cache_texture = ImageDecoder::DecodeVideoTexture(m_Command.quiverFile.GetURI(), &n, &d,
						-1, m_Command.params.max_width, m_Command.params.max_height,
						abort_video_load, this);
					if (NULL == cache_texture && CommandsPending())
						bAborted = true;
					if (NULL != cache_texture)
					{
						guint tex_width  = gdk_texture_get_width(cache_texture);
						guint tex_height = gdk_texture_get_height(cache_texture);

						if (n > d)
							tex_width = (guint)((tex_width * n) / float(d) + .5);
						else
							tex_height = (guint)((tex_height * d) / float(n) + .5);

						if (!m_Command.quiverFile.IsWidthHeightSet())
						{
							m_Command.quiverFile.SetWidth(tex_width);
							m_Command.quiverFile.SetHeight(tex_height);
						}

						m_Command.quiverFile.SetLoadTimeInSeconds(loadTimer.GetRunningTimeInSeconds());
					}
				}
				else
				{
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
							cache_texture = ImageDecoder::DecodeFileTexture(gfile, m_Command.quiverFile.GetMimeType(), NULL, NULL);
							if (NULL != cache_texture)
							{
								m_Command.quiverFile.SetLoadTimeInSeconds(loadTimer.GetRunningTimeInSeconds());
								bGlycinTransformed = (g_object_get_data(G_OBJECT(cache_texture), "glycin-transformed") != NULL);
							}
							g_object_unref(gfile);
						}
					}

#if HAVE_GDK_PIXBUF
					if (NULL == cache_texture && !CommandsPending())
					{
						GdkPixbufLoader* ldr = gdk_pixbuf_loader_new_with_mime_type (m_Command.quiverFile.GetMimeType(), NULL);	
					
						if (!m_Command.params.fullsize)
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
							GdkPixbuf *pb = gdk_pixbuf_loader_get_pixbuf(ldr);
							if (NULL != pb)
							{
								cache_texture = QuiverUtils::PixbufToTexture(pb);
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
						
						gint *pOrientation = g_new(int,1);
						*pOrientation = m_Command.params.orientation;
						g_object_set_data_full (G_OBJECT (cache_texture), "quiver-orientation", pOrientation, g_free);
					}
				}

				if (NULL != cache_texture)
				{
					if (CACHE_LOAD == m_Command.params.state)
					{
						list<IPixbufLoaderObserver*>::iterator itr;
						g_mutex_lock(&m_csObservers);
						for (itr = m_observers.begin();itr != m_observers.end() ; ++itr)
						{
							gint width,height;
							width = m_Command.quiverFile.GetWidth();
							height = m_Command.quiverFile.GetHeight();
							if (4 < m_Command.params.orientation)
							{
								swap(width,height);
							}
							bool bResetViewMode = !m_Command.params.loaded_quick_preview;
							(*itr)->SetTextureAtSize(cache_texture,width,height,bResetViewMode);
						}
						g_mutex_unlock(&m_csObservers);
						m_ImageCache.AddTexture(m_Command.quiverFile.GetURI(),cache_texture);
					}
					else
					{
						m_ImageCache.AddTexture(m_Command.quiverFile.GetURI(),cache_texture,0);
					}
					g_object_unref(cache_texture);
				}
				else
				{
					if (!bAborted)
					{
						m_ImageCache.AddFailure(m_Command.quiverFile.GetURI());
						if (CACHE_LOAD == m_Command.params.state || LOAD == m_Command.params.state)
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


