#include <config.h>

#include "ImageLoader.h"
#include "QuiverUtils.h"
#include <libgnomevfs/gnome-vfs.h>

using namespace std;

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

ImageLoader::ImageLoader() : m_ImageCache(4)
{
	//Timer t("ImageLoader::ImageLoader()");
	pthread_cond_init(&m_Condition,NULL);
	pthread_mutex_init(&m_ConditionMutex, NULL);
	pthread_mutex_init(&m_CommandMutex, NULL);
	
	AddPixbufLoaderObserver(this);

	m_bStopThread = false;
	//Timer t("ImageLoader::Start()");
	pthread_create(&m_pthread_id, NULL, run, this);
}

ImageLoader::~ImageLoader()
{
	m_bStopThread = true;
	
	pthread_mutex_lock (&m_ConditionMutex);
	pthread_cond_signal(&m_Condition);
	pthread_mutex_unlock (&m_ConditionMutex);

	// this call to gdk_threads_leave is made to make sure we dont get into
	// a deadlock between this thread(gui) and the imageloader thread which calls
	// gdk_threads_enter 
	gdk_threads_leave();
	pthread_join(m_pthread_id,NULL);
	
	pthread_cond_destroy(&m_Condition);
	pthread_mutex_destroy(&m_ConditionMutex);
	pthread_mutex_destroy(&m_CommandMutex);

	RemovePixbufLoaderObserver(this);
}


int ImageLoader::Run()
{
	while (true)
	{
		pthread_mutex_lock (&m_CommandMutex);

		if (0 == m_Commands.size() && !m_bStopThread)
		{
			// unlock mutex
			pthread_mutex_unlock (&m_CommandMutex);
			
			pthread_mutex_lock (&m_ConditionMutex);
			pthread_cond_wait(&m_Condition,&m_ConditionMutex);
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
		LoadQuickPreview();
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

void ImageLoader::AddPixbufLoaderObserver(IPixbufLoaderObserver * loader_observer)
{
	m_observers.push_back(loader_observer);	
}

void ImageLoader::RemovePixbufLoaderObserver(IPixbufLoaderObserver * loader_observer)
{
	m_observers.remove(loader_observer);	
}



void ImageLoader::LoadQuickPreview()
{
	if (!m_Command.params.no_thumb_preview)
	{
		
		GdkPixbuf *thumb_pixbuf = NULL;
		
		if (m_Command.quiverFile.HasThumbnail(true))
		{
			thumb_pixbuf = m_Command.quiverFile.GetThumbnail(true);
		}
		else if (m_Command.quiverFile.HasThumbnail(false))
		{
			thumb_pixbuf = m_Command.quiverFile.GetThumbnail(false);
		}
	
	
		if (NULL != thumb_pixbuf)
		{
			
			int width = m_Command.quiverFile.GetWidth();
			int height = m_Command.quiverFile.GetHeight();
			
			if (4 < m_Command.params.orientation)
			{
				swap(width,height);
			}
			
			list<IPixbufLoaderObserver*>::iterator itr;
			for (itr = m_observers.begin();itr != m_observers.end() ; ++itr)
			{
				gdk_threads_enter();
				(*itr)->SetPixbufAtSize(thumb_pixbuf,width,height);
				gdk_threads_leave();
			}
			g_object_unref(thumb_pixbuf);
		}
	}
}

void ImageLoader::Load()
{

	if (m_Command.params.reload)
	{
		m_ImageCache.RemovePixbuf(m_Command.quiverFile.GetURI());
	}

	if (0 < m_Command.params.max_width && 0 < m_Command.params.max_height)
	{
		GdkPixbuf * pixbuf = m_ImageCache.GetPixbuf(m_Command.quiverFile.GetURI());
		if (NULL != pixbuf)
		{
			int real_width,real_height;
			int orientation = m_Command.params.orientation;
			gint width,height;
			width = gdk_pixbuf_get_width(pixbuf);
			height = gdk_pixbuf_get_height(pixbuf);
			
			real_width = m_Command.quiverFile.GetWidth();
			real_height = m_Command.quiverFile.GetHeight();
			
			// we do not need to free the data returned from this - it is not a copy
			gint* pOrientation = (gint*)g_object_get_data(G_OBJECT (pixbuf), "quiver-orientation");
			if (NULL != pOrientation)
			{
				if(m_Command.params.orientation != *pOrientation)
				{
					if ( (4 < m_Command.params.orientation && 4 >= *pOrientation)
						|| (4 >= m_Command.params.orientation && 4 < *pOrientation) )
					{
						// swap because the cached image orientation has a different 
						// ratio for width/height than the requested orientation
						swap(width,height);
					}
					
				}
			}

			if (4 < orientation)
			{
				// swap because the actual image has a different 
				// ratio for width/height than the requested orientation
				swap(real_width,real_height);
			}
			
			if ( width < m_Command.params.max_width && height < m_Command.params.max_height && width < real_width && height < real_height)
			{
				m_ImageCache.RemovePixbuf(m_Command.quiverFile.GetURI());
			}
					
			g_object_unref(pixbuf);
		}
	}

	if (LOAD == m_Command.params.state)
	{
		GdkPixbuf * pixbuf = m_ImageCache.GetPixbuf(m_Command.quiverFile.GetURI());
		
		if ( NULL == pixbuf)
		{
			if (0 != strcmp(m_Command.quiverFile.GetURI(),""))
			{
				LoadQuickPreview();

				GdkPixbufLoader* loader = gdk_pixbuf_loader_new ();	
				
				list<IPixbufLoaderObserver*>::iterator itr;
				for (itr = m_observers.begin();itr != m_observers.end() ; ++itr)
				{
					if (m_Command.params.reload && m_Command.params.fullsize)
					{
						// don't connect any signals
						// we presume the image is already being shown
						// and the full size is needed

					}
					else if ( 1 < m_Command.params.orientation )
					{
						(*itr)->ConnectSignalSizePrepared(loader);
					}
					else
					{
						(*itr)->ConnectSignals(loader);
					}
					
				}

				bool rval = LoadPixbuf(loader);

				
				if (rval)
				{
					GdkPixbuf* pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);
					
					if (NULL == pixbuf  )
					{
						if (m_Command.params.fullsize)
							cout << "pixbuf is null! : " << m_Command.quiverFile.GetURI() << endl;
					}
					else
					{
						g_object_ref(pixbuf);

						if (1 < m_Command.params.orientation)
						{
							GdkPixbuf* pixbuf_rotated;
							pixbuf_rotated = QuiverUtils::GdkPixbufExifReorientate(pixbuf,m_Command.params.orientation);
							if (NULL != pixbuf_rotated)
							{
								g_object_unref(pixbuf);
								pixbuf = pixbuf_rotated;
							}
						}
						
						list<IPixbufLoaderObserver*>::iterator itr;
						for (itr = m_observers.begin();itr != m_observers.end() ; ++itr)
						{

							gdk_threads_enter();
							gint width,height;
							width = m_Command.quiverFile.GetWidth();
							height = m_Command.quiverFile.GetHeight();
							if (4 < m_Command.params.orientation)
							{
								swap(width,height);
							}
							if (m_Command.params.reload)
							{
								(*itr)->SetPixbufAtSize(pixbuf,width,height,false);
							}
							else
							{
								// FIXME: this was originally only for rotated images
								// but it has now become the call for all images and
								// we don't rely on the gdk_pixbuf_loader signals
								// but this may cause the images to be drawn twice..
								//if (1 < m_Command.params.orientation)
								{
									(*itr)->SetPixbufAtSize(pixbuf,width,height);
								}
							}
							gdk_flush();
							gdk_threads_leave();
						}

						gint *pOrientation = g_new(int,1);
						*pOrientation = m_Command.params.orientation;
						g_object_set_data_full (G_OBJECT (pixbuf), "quiver-orientation", pOrientation,g_free);

						m_ImageCache.AddPixbuf(m_Command.quiverFile.GetURI(),pixbuf);
						g_object_unref(pixbuf);
					}
				}
				else
				{
					// error loading 
				}
				g_object_unref(loader);
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
			for (itr = m_observers.begin();itr != m_observers.end() ; ++itr)
			{
				gdk_threads_enter();
				gint width,height;
				width = m_Command.quiverFile.GetWidth();
				height = m_Command.quiverFile.GetHeight();
				if (4 < m_Command.params.orientation)
				{
					swap(width,height);
				}
				(*itr)->SetPixbufAtSize(pixbuf,width,height);
				gdk_threads_leave();
			}
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
					for (itr = m_observers.begin();itr != m_observers.end() ; ++itr)
					{
						(*itr)->ConnectSignalSizePrepared(ldr);
					}
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
							for (itr = m_observers.begin();itr != m_observers.end() ; ++itr)
							{
								gdk_threads_enter();
								gint width,height;
								width = m_Command.quiverFile.GetWidth();
								height = m_Command.quiverFile.GetHeight();
								if (4 < m_Command.params.orientation)
								{
									swap(width,height);
								}
								(*itr)->SetPixbufAtSize(pixbuf,width,height);

								gdk_flush();
								gdk_threads_leave();
							}
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
	bool retval = false; // did not load

	GError *tmp_error;
	tmp_error = NULL;
	
	if (CommandsPending())
	{	
		gdk_threads_enter();
		gdk_pixbuf_loader_close(loader,NULL);	
		gdk_threads_leave();
		return retval;
	}
	Timer loadTimer(true); // true for quite mode
	
	int size = 8192;
	//int size = 16384;
	//int size = 32768;
	long bytes_read_inc=0, bytes_total=0;

	guchar buffer[size];
	GnomeVFSHandle   *handle;
	GnomeVFSResult    result;
	GnomeVFSFileSize  bytes_read;

	bytes_total = m_Command.quiverFile.GetFileSize();
	

	result = gnome_vfs_open (&handle, m_Command.quiverFile.GetURI(), GNOME_VFS_OPEN_READ);
	list<IPixbufLoaderObserver*>::iterator itr;
	while (GNOME_VFS_OK == result) 
	{
		if (CommandsPending())
		{
			gdk_threads_enter();
			gdk_pixbuf_loader_close(loader,NULL);
			gdk_threads_leave();
			gnome_vfs_close(handle);
			return retval;
		}
		result = gnome_vfs_read (handle, buffer, 
								 size, &bytes_read);
		
		bytes_read_inc += bytes_read;
		// notify others of bytes read
		gdk_threads_enter();
		
		for (itr = m_observers.begin();itr != m_observers.end() ; ++itr)
		{
			if (0 < bytes_read_inc && bytes_read_inc <= bytes_total)
			{
				(*itr)->SignalBytesRead(bytes_read_inc,bytes_total);
			}
		}
		
		tmp_error = NULL;

		gdk_pixbuf_loader_write (loader, buffer, bytes_read, &tmp_error);
		gdk_flush();
		gdk_threads_leave();

		if (NULL != tmp_error)
		{
			//printf("Error loading image: %s\n",tmp_error->message);
			break;
		}
	}

	m_Command.quiverFile.SetLoadTimeInSeconds( loadTimer.GetRunningTimeInSeconds() );
	
	tmp_error = NULL;
	
	gdk_threads_enter();
	gdk_pixbuf_loader_close(loader,&tmp_error);
	gdk_flush();
	gdk_threads_leave();

	if (GNOME_VFS_ERROR_EOF == result)
		retval = true;
	gnome_vfs_close(handle);

	
	
	return retval;
}


static void get_bound_size(guint bound_width,guint bound_height,guint *width,guint *height,gboolean fill_if_smaller)
{
	guint new_width;
	guint w = *width;
	guint h = *height;

	if (!fill_if_smaller && 
			(w < bound_width && h < bound_height) )
	{
		return;
	}

	if (w != bound_width || h != bound_height)
	{
		gdouble ratio = (double)w/h;
		guint new_height;

		new_height = (guint)(bound_width/ratio +.5);
		if (new_height < bound_height)
		{
			*width = bound_width;
			*height = new_height;
		}
		else
		{
			new_width = (guint)(bound_height *ratio + .5); 
			*width = MIN(new_width, bound_width);
			*height = bound_height;
		}
	}
}	

void ImageLoader::SignalSizePrepared(GdkPixbufLoader *loader,gint width, gint height)
{
	m_Command.quiverFile.SetWidth(width);
	m_Command.quiverFile.SetHeight(height);
	
	int max_width, max_height;
	
	max_width = m_Command.params.max_width;
	max_height = m_Command.params.max_height;
	
	if (4 < m_Command.params.orientation)
	{
		swap(max_width,max_height);
		swap(width,height);
	}
	
	if (10 > max_width)
	{
		max_width = 10;
	}
	if (10 > max_height)
	{
		max_height = 10;
	}
	
	if (0 < max_width && 0 < max_height)
	{
		if (max_width < width || max_height < height)
		{
			// adjust the image size
			guint new_width = width;
			guint new_height = height;
			
			get_bound_size((guint)m_Command.params.max_width,(guint)m_Command.params.max_height,&new_width, &new_height,FALSE);

			if (4 < m_Command.params.orientation)
			{
				swap(new_width,new_height);
			}
			gdk_pixbuf_loader_set_size(loader,new_width,new_height);
		}
	}
}

void* ImageLoader::run(void * data)
{
	((ImageLoader*)data)->Run();
	return 0;
}

