#include <config.h>

#include "ImageLoader.h"
#include "QuiverUtils.h"
#include "QuiverVideoOps.h"

#include <gio/gio.h>
#include <gst/gst.h>

#include <libquiver/quiver-pixbuf-utils.h>
#include <string.h>

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

ImageLoader::ImageLoader() : m_ImageCache(4)
{
	//Timer t("ImageLoader::ImageLoader()");
	pthread_cond_init(&m_Condition,NULL);
	pthread_mutex_init(&m_ConditionMutex, NULL);
	pthread_mutex_init(&m_CommandMutex, NULL);

	m_csObservers = g_mutex_new();
	
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
	m_bStopThread = true;
	
	pthread_mutex_lock (&m_ConditionMutex);
	pthread_cond_signal(&m_Condition);
	pthread_mutex_unlock (&m_ConditionMutex);

	pthread_join(m_pthread_id,NULL);
	
	pthread_cond_destroy(&m_Condition);
	pthread_mutex_destroy(&m_ConditionMutex);
	pthread_mutex_destroy(&m_CommandMutex);

	RemovePixbufLoaderObserver(this);

	g_mutex_free(m_csObservers);
}


int ImageLoader::Run()
{
	while (true)
	{
		pthread_mutex_lock (&m_CommandMutex);

		if (0 == m_Commands.size() && !m_bStopThread)
		{
			m_bWorking = false;
			// unlock mutex
			pthread_mutex_unlock (&m_CommandMutex);
			
			pthread_mutex_lock (&m_ConditionMutex);
			pthread_cond_wait(&m_Condition,&m_ConditionMutex);

			m_bWorking = true;

			pthread_mutex_unlock (&m_ConditionMutex);

		}
		else
		{
			pthread_mutex_unlock (&m_CommandMutex);
		}
		
		if (m_bStopThread)
		{
			break;
		}

		pthread_mutex_lock (&m_CommandMutex);
		
		while (2 < m_Commands.size())
		{
			m_Commands.pop_front();
		}
		
		if (CACHE == m_Commands.front().params.state && LOAD == m_Commands.back().params.state)
		{
			m_Commands.pop_front();
		}
		
		m_Command = m_Commands.front();
		m_Commands.pop_front();
		
		pthread_mutex_unlock (&m_CommandMutex);
		
		Load();
		
		pthread_yield();
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
	
	if (CACHE == m_Commands.front().params.state && LOAD == m_Commands.back().params.state)
	{
		m_Commands.pop_front();
	}
	
	if (0 < m_Commands.size())
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

void ImageLoader::ReCacheImage(QuiverFile f)
{
	LoadParams p = {0};
	p.state = CACHE;
	p.reload = true;
	LoadImage(f,p);
}

void ImageLoader::CacheImage(QuiverFile f)
{
	LoadParams p = {0};
	p.state = CACHE;
	LoadImage(f,p);
}

void ImageLoader::CacheImageAtSize(QuiverFile f, int width, int height)
{
	LoadParams p = {0};
	p.state = CACHE;
	p.max_width = width;
	p.max_height = height;
	LoadImage(f,p);
}

void ImageLoader::ReloadImage(QuiverFile f)
{
	LoadParams p = {0};
	p.state = LOAD;
	p.reload = true;

	// now we can load it
	LoadImage(f,p);
}

void ImageLoader::LoadImage(QuiverFile f)
{
	LoadParams p = {0};
	p.state = LOAD;

	LoadImage(f,p);
}

void ImageLoader::LoadImageAtSize(QuiverFile f, int width, int height)
{
	LoadParams p = {0};
	p.state = LOAD;
	p.max_width = width;
	p.max_height = height;
	LoadImage(f,p);
}

void ImageLoader::LoadImage(QuiverFile f,LoadParams load_params)
{
	pthread_mutex_lock (&m_CommandMutex);
	
	Command c;
	
	c.quiverFile = f;
	if (0 == load_params.orientation)
	{
		load_params.orientation = f.GetOrientation();
	}
	
	c.params = load_params;
	
	m_Commands.push_back(c);
	pthread_mutex_lock (&m_ConditionMutex);
	pthread_cond_signal(&m_Condition);
	pthread_mutex_unlock (&m_ConditionMutex);
	pthread_mutex_unlock (&m_CommandMutex);
}


GdkPixbuf* ImageLoader::GetCachedPixbuf(QuiverFile f)
{
	return m_ImageCache.GetPixbuf(f.GetURI());
}

void ImageLoader::AddPixbufLoaderObserver(IPixbufLoaderObserver * loader_observer)
{
	g_mutex_lock(m_csObservers);
	m_observers.push_back(loader_observer);	
	g_mutex_unlock(m_csObservers);
}

void ImageLoader::RemovePixbufLoaderObserver(IPixbufLoaderObserver * loader_observer)
{
	g_mutex_lock(m_csObservers);
	m_observers.remove(loader_observer);	
	g_mutex_unlock(m_csObservers);
}



bool ImageLoader::LoadQuickPreview()
{
	bool rval = false;
	if (m_bQuickPreview && !m_Command.params.no_thumb_preview)
	{
		GdkPixbuf *thumb_pixbuf = NULL;
		
		//FIXME: should not hard code 128/256. make a new
		// function to get the largest available thumbnail
		if (m_Command.quiverFile.HasThumbnail(256))
		{
			thumb_pixbuf = m_Command.quiverFile.GetThumbnail(256);
		}
		else if (m_Command.quiverFile.HasThumbnail(128))
		{
			thumb_pixbuf = m_Command.quiverFile.GetThumbnail(128);
		}
	
	
		if (NULL != thumb_pixbuf)
		{
			
			int width = m_Command.quiverFile.GetWidth();
			int height = m_Command.quiverFile.GetHeight();
			
			if (4 < m_iLoadOrientation)
			{
				swap(width,height);
			}

			if (m_iLoadOrientation != m_Command.quiverFile.GetOrientation())
			{
				// thumbnail has already been rotated by the exif orientaiton
				// so we must revert that and calculate the new rotation 
				int orientation = inverse_matrix[m_Command.quiverFile.GetOrientation()];
				int new_orientation = combine_matrix[m_iLoadOrientation][orientation];

				GdkPixbuf* pixbuf_rotated = QuiverUtils::GdkPixbufExifReorientate(thumb_pixbuf, new_orientation);
				if (NULL != pixbuf_rotated)
				{
					g_object_unref(thumb_pixbuf);
					thumb_pixbuf = pixbuf_rotated;
				}
			}
			
			list<IPixbufLoaderObserver*>::iterator itr;
			g_mutex_lock(m_csObservers);
			for (itr = m_observers.begin();itr != m_observers.end() ; ++itr)
			{
				(*itr)->SetPixbufAtSize(thumb_pixbuf,width,height);
			}
			g_mutex_unlock(m_csObservers);
			g_object_unref(thumb_pixbuf);
			
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
		m_ImageCache.RemovePixbuf(m_Command.quiverFile.GetURI());
	}

	// check to see if the image should be removed from the cache
	GdkPixbuf * pixbuf = m_ImageCache.GetPixbuf(m_Command.quiverFile.GetURI());
	if (NULL != pixbuf)
	{
		int real_width,real_height;

		gint width,height;
		width = gdk_pixbuf_get_width(pixbuf);
		height = gdk_pixbuf_get_height(pixbuf);
		
		real_width = m_Command.quiverFile.GetWidth();
		real_height = m_Command.quiverFile.GetHeight();
		
		const gint* pOrientation = (const gint*)g_object_get_data(G_OBJECT (pixbuf), "quiver-orientation");
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

		if ((0 == m_Command.params.max_width && 0 == m_Command.params.max_height) ||
			( width < m_Command.params.max_width &&
			 height < m_Command.params.max_height &&
			 width < real_width && height < real_height))
		{
			m_ImageCache.RemovePixbuf(m_Command.quiverFile.GetURI());
		}
				
		g_object_unref(pixbuf);
	}

	if (LOAD == m_Command.params.state)
	{
		GdkPixbuf * pixbuf = m_ImageCache.GetPixbuf(m_Command.quiverFile.GetURI());
		
		if ( NULL == pixbuf)
		{
			if (0 != strcmp(m_Command.quiverFile.GetURI(),""))
			{
				bool bLoadedQuickPreview = LoadQuickPreview();

				if (m_Command.quiverFile.IsVideo())
				{
					gint n=1, d=1;
					GdkPixbuf* video_pixbuf = QuiverVideoOps::LoadPixbuf(m_Command.quiverFile.GetURI(), &n, &d);
					if (NULL != video_pixbuf)
					{
						guint pixbuf_width  = gdk_pixbuf_get_width(video_pixbuf);
						guint pixbuf_height = gdk_pixbuf_get_height(video_pixbuf);

						if (n > d)
							pixbuf_width = (guint)((pixbuf_width * n) / float(d) + .5);
						else
							pixbuf_height = (guint)((pixbuf_height * d) / float(n) + .5);

						if (!m_Command.quiverFile.IsWidthHeightSet())
						{
							m_Command.quiverFile.SetWidth(pixbuf_width);
							m_Command.quiverFile.SetHeight(pixbuf_height);
						}
						
						// FIXME: fullsize parameter doesnt seem to work
						if (20 <= m_Command.params.max_width && 20 <= m_Command.params.max_height)
						{
							quiver_rect_get_bound_size(m_Command.params.max_width, m_Command.params.max_height, &pixbuf_width,&pixbuf_height,FALSE);
							pixbuf = gdk_pixbuf_scale_simple (
								video_pixbuf,
								pixbuf_width,
								pixbuf_height,
								GDK_INTERP_BILINEAR);

							g_object_unref(video_pixbuf);
						}
						else if (n != d)
						{
							pixbuf = gdk_pixbuf_scale_simple (
								video_pixbuf,
								pixbuf_width,
								pixbuf_height,
								GDK_INTERP_BILINEAR);

							g_object_unref(video_pixbuf);
						}
						else
						{
							pixbuf = video_pixbuf;
						}

					}
				}
				else
				{

					GdkPixbufLoader* loader = gdk_pixbuf_loader_new ();	
					
					list<IPixbufLoaderObserver*>::iterator itr;
					g_mutex_lock(m_csObservers);
					for (itr = m_observers.begin();itr != m_observers.end() ; ++itr)
					{
						if (m_Command.params.reload && m_Command.params.fullsize)
						{
							// don't connect any signals
							// we presume the image is already being shown
							// and the full size is needed

						}
						else // if ( 1 < m_Command.params.orientation )
						{
							(*itr)->ConnectSignalSizePrepared(loader);
						}
						/*
						 * else
						{
							// FIXME: maybe we should show images loading
							// if they are loading at full size or loading
							// over the network
							(*itr)->ConnectSignals(loader);
						}
						*/
						
					}
					g_mutex_unlock(m_csObservers);

					bool rval = LoadPixbuf(loader);

					if (rval)
					{
						pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);
						if (NULL != pixbuf)
							g_object_ref(pixbuf);
						g_object_unref(loader);
					}
				}
					
				if (NULL != pixbuf  )
				{
					// set up a temp orientation as m_iLoadOrientation could 
					// change at any time
					int orientation = m_iLoadOrientation;
					if (1 < orientation)
					{
						GdkPixbuf* pixbuf_rotated;
						pixbuf_rotated = QuiverUtils::GdkPixbufExifReorientate(pixbuf,orientation);
						if (NULL != pixbuf_rotated)
						{
							g_object_unref(pixbuf);
							pixbuf = pixbuf_rotated;
						}
					}
					
					list<IPixbufLoaderObserver*>::iterator itr;
					g_mutex_lock(m_csObservers);
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
							// FIXME: if the rotation changes at some point, we
							// need to rotate this image
							(*itr)->SetPixbufAtSize(pixbuf,width,height,false);
						}
						else
						{
							bool bResetViewMode = !bLoadedQuickPreview;
							(*itr)->SetPixbufAtSize(pixbuf,width,height,bResetViewMode);
						}
					}
					g_mutex_unlock(m_csObservers);

					gint *pOrientation = g_new(int,1);
					*pOrientation = orientation;
					g_object_set_data_full (G_OBJECT (pixbuf), "quiver-orientation", pOrientation,g_free);

					m_ImageCache.AddPixbuf(m_Command.quiverFile.GetURI(),pixbuf);
					g_object_unref(pixbuf);
				}
			}
			else
			{
				// error loading 
			}
		}
		else
		{
			// load from cache
			// we do not need to free the data returned from this - it is not a copy
			gint *pOrientation = (gint*)g_object_get_data(G_OBJECT (pixbuf), "quiver-orientation");
		
			
			if (NULL != pOrientation && m_Command.params.orientation != *pOrientation)
			{
				int new_orientation = reorientation_matrix[*pOrientation][m_Command.params.orientation];

				GdkPixbuf* pixbuf_rotated = QuiverUtils::GdkPixbufExifReorientate(pixbuf,new_orientation);
				if (NULL != pixbuf_rotated)
				{
					g_object_unref(pixbuf);
					pixbuf = pixbuf_rotated;

					gint *pNewOrientation = g_new(int,1);
					*pNewOrientation = m_Command.params.orientation;
					g_object_set_data_full (G_OBJECT (pixbuf), "quiver-orientation", pNewOrientation,g_free);

					m_ImageCache.AddPixbuf(m_Command.quiverFile.GetURI(),pixbuf);
				}
			}

			list<IPixbufLoaderObserver*>::iterator itr;
			g_mutex_lock(m_csObservers);
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
				(*itr)->SetPixbufAtSize(pixbuf,width,height,bResetViewMode);
			}
			g_mutex_unlock(m_csObservers);
			g_object_unref(pixbuf);
		}
	}	
	else if (CACHE == m_Command.params.state)
	{

		if (!m_ImageCache.InCache(m_Command.quiverFile.GetURI()))
		{
			if (0 != strcmp(m_Command.quiverFile.GetURI(),""))
			{
				//cout << "cache : " << m_Command.filename << endl;
				GdkPixbufLoader* ldr = gdk_pixbuf_loader_new ();	
			
				if (!m_Command.params.fullsize)
				{
					list<IPixbufLoaderObserver*>::iterator itr;
					g_mutex_lock(m_csObservers);
					for (itr = m_observers.begin();itr != m_observers.end() ; ++itr)
					{
						(*itr)->ConnectSignalSizePrepared(ldr);
					}
					g_mutex_unlock(m_csObservers);
				}
								
				bool rval = LoadPixbuf(ldr);

				if (rval)
				{
					GdkPixbuf* pixbuf = gdk_pixbuf_loader_get_pixbuf(ldr);

					if (NULL != pixbuf)
					{
						g_object_ref(pixbuf);
						if (1 < m_Command.params.orientation) // TODO: change to get rotate option
						{
							GdkPixbuf* pixbuf_rotated = QuiverUtils::GdkPixbufExifReorientate(pixbuf,m_Command.params.orientation);
							
							if (NULL != pixbuf_rotated)
							{
								g_object_unref(pixbuf);
								pixbuf = pixbuf_rotated;
							}
						}
						
						// save the orientation so we can find out later
						// what orientation was performed 
						gint *pOrientation = g_new(int,1);
						*pOrientation = m_Command.params.orientation;

						g_object_set_data_full (G_OBJECT (pixbuf), "quiver-orientation", pOrientation,g_free);

						if (CACHE_LOAD == m_Command.params.state)
						{
							list<IPixbufLoaderObserver*>::iterator itr;
							g_mutex_lock(m_csObservers);
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
								(*itr)->SetPixbufAtSize(pixbuf,width,height,bResetViewMode);

							}
							g_mutex_unlock(m_csObservers);
							m_ImageCache.AddPixbuf(m_Command.quiverFile.GetURI(),pixbuf);
							//cout << "cache loaded image" << endl;
						}
						else
						{
							m_ImageCache.AddPixbuf(m_Command.quiverFile.GetURI(),pixbuf,0);
						}
						g_object_unref(pixbuf);
					}
				}
				
				g_object_unref(ldr);
			}

		}
		else
		{
			//cout << "in cache : " << m_Command.filename << endl;
		}
	}
	else
	{
	}
}

bool ImageLoader::LoadPixbuf(GdkPixbufLoader *loader)
{
	//cout << "Loading: " <<  m_Command.quiverFile.GetURI() << endl;
	bool retval = true; // did not load

	GError *tmp_error;
	tmp_error = NULL;
	
	if (CommandsPending())
	{	
		gdk_pixbuf_loader_close(loader,NULL);	
		return false;
	}
	Timer loadTimer; // true for quite mode
	
	//int size = 8192;
	//int size = 16384;
	//int size = 32768;
	int size = 65536;
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
				gdk_pixbuf_loader_close(loader,NULL);
				g_object_unref(inStream);
				g_object_unref(gfile);
				return false;
			}

			bytes_read_inc += bytes_read;

			// notify others of bytes read
			g_mutex_lock(m_csObservers);
			for (itr = m_observers.begin();itr != m_observers.end() ; ++itr)
			{
				if (0 < bytes_read_inc && bytes_read_inc <= bytes_total)
				{
					(*itr)->SignalBytesRead(bytes_read_inc,bytes_total);
				}
			}
			g_mutex_unlock(m_csObservers);
			
			tmp_error = NULL;

			gdk_pixbuf_loader_write (loader, buffer, bytes_read, &tmp_error);

			if (NULL != tmp_error)
			{
				retval = false;
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
	}

	g_object_unref(gfile);
	
	return retval;
}

#if (GTK_MAJOR_VERSION < 2) \
	|| (GTK_MAJOR_VERSION == 2 && GTK_MINOR_VERSION < 12) \
	|| (GTK_MAJOR_VERSION == 2 && GTK_MINOR_VERSION == 12 && GTK_MICRO_VERSION < 3) 
// the following is a temporary work around for bug:
// http://bugzilla.gnome.org/show_bug.cgi?id=494515
static gint hack_calculate_size(gdouble ratio, gint size, gint new_size)
{
	int nw = new_size;
	int size_calc = (int)(size * ratio);
	while ( size_calc < nw)
	{
		ratio = (gdouble)new_size/size;
		size_calc = (int)(size * ratio);
		++new_size;
	}
	return new_size;
}
#endif

void ImageLoader::SignalSizePrepared(GdkPixbufLoader *loader,gint width, gint height)
{
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
#if (GTK_MAJOR_VERSION < 2) \
	|| (GTK_MAJOR_VERSION == 2 && GTK_MINOR_VERSION < 12) \
	|| (GTK_MAJOR_VERSION == 2 && GTK_MINOR_VERSION == 12 && GTK_MICRO_VERSION < 3)
// the following is a temporary work around for bug:
// http://bugzilla.gnome.org/show_bug.cgi?id=494515
			int scale_factor = 2;
			
			if (m_Command.quiverFile.GetMimeType() && 0 == strncmp("image/jpeg",m_Command.quiverFile.GetMimeType() , 11) )
			{
				for (scale_factor=2; scale_factor <=8; scale_factor*=2)
				{
					if (width/scale_factor < (gint)new_width || height/scale_factor < (gint)new_height)
					{
						scale_factor /= 2;
						break;
					}
				}
				width = width/scale_factor;
				height = height/scale_factor;
			}

			gdouble xscale = (gdouble)new_width/width;
			new_width = hack_calculate_size(xscale, width, new_width);
			gdouble yscale = (gdouble)new_height/height;
			new_height = hack_calculate_size(yscale, height, new_height);
#endif	
			gdk_pixbuf_loader_set_size(loader,new_width,new_height);
		}
	}
}

void* ImageLoader::run(void * data)
{
	((ImageLoader*)data)->Run();
	return 0;
}


