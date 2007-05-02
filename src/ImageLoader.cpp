#include <config.h>

#include "ImageLoader.h"
#include "QuiverUtils.h"
#include <libgnomevfs/gnome-vfs.h>
#include <libquiver/quiver-pixbuf-utils.h>

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

void ImageLoader::AddPixbufLoaderObserver(IPixbufLoaderObserver * loader_observer)
{
	m_observers.push_back(loader_observer);	
}

void ImageLoader::RemovePixbufLoaderObserver(IPixbufLoaderObserver * loader_observer)
{
	m_observers.remove(loader_observer);	
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
			
			list<IPixbufLoaderObserver*>::iterator itr;
			for (itr = m_observers.begin();itr != m_observers.end() ; ++itr)
			{
				(*itr)->SetPixbufAtSize(thumb_pixbuf,width,height);
			}
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

	if (0 < m_Command.params.max_width && 0 < m_Command.params.max_height)
	{
		GdkPixbuf * pixbuf = m_ImageCache.GetPixbuf(m_Command.quiverFile.GetURI());
		if (NULL != pixbuf)
		{
			int real_width,real_height;

			gint width,height;
			width = gdk_pixbuf_get_width(pixbuf);
			height = gdk_pixbuf_get_height(pixbuf);
			
			real_width = m_Command.quiverFile.GetWidth();
			real_height = m_Command.quiverFile.GetHeight();
			
			// we do not need to free the data returned from this - it is not a copy
			gint* pOrientation = (gint*)g_object_get_data(G_OBJECT (pixbuf), "quiver-orientation");
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
				bool bLoadedQuickPreview = LoadQuickPreview();

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
						// set up a temp orientation as m_iLoadOrientation could 
						// change at any time
						int orientation = m_iLoadOrientation;
						if (1 < orientation)
						{
							GdkPixbuf* pixbuf_rotated;
							pixbuf_rotated = QuiverUtils::GdkPixbufExifReorientate(pixbuf,m_iLoadOrientation);
							if (NULL != pixbuf_rotated)
							{
								g_object_unref(pixbuf);
								pixbuf = pixbuf_rotated;
							}
						}
						
						list<IPixbufLoaderObserver*>::iterator itr;
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
		gdk_pixbuf_loader_close(loader,NULL);	
		return retval;
	}
	Timer loadTimer; // true for quite mode
	
	//int size = 8192;
	//int size = 16384;
	//int size = 32768;
	int size = 65536;
	//int size = 131070;
	
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
			gdk_pixbuf_loader_close(loader,NULL);
			gnome_vfs_close(handle);
			return retval;
		}
		result = gnome_vfs_read (handle, buffer, 
								 size, &bytes_read);
		
		bytes_read_inc += bytes_read;
		// notify others of bytes read
		
		for (itr = m_observers.begin();itr != m_observers.end() ; ++itr)
		{
			if (0 < bytes_read_inc && bytes_read_inc <= bytes_total)
			{
				(*itr)->SignalBytesRead(bytes_read_inc,bytes_total);
			}
		}
		
		tmp_error = NULL;

		gdk_pixbuf_loader_write (loader, buffer, bytes_read, &tmp_error);

		if (NULL != tmp_error)
		{
			//printf("Error loading image: %s\n",tmp_error->message);
			break;
		}
		usleep(50);
	}

	m_Command.quiverFile.SetLoadTimeInSeconds( loadTimer.GetRunningTimeInSeconds() );
	
	tmp_error = NULL;
	
	gdk_pixbuf_loader_close(loader,&tmp_error);

	if (GNOME_VFS_ERROR_EOF == result)
		retval = true;
	gnome_vfs_close(handle);

	
	
	return retval;
}


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

