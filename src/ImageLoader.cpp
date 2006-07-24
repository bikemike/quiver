#include <config.h>

#include "ImageLoader.h"
#include "QuiverUtils.h"
#include <libgnomevfs/gnome-vfs.h>

using namespace std;

ImageLoader::ImageLoader() : m_ImageCache(4)
{
	//Timer t("ImageLoader::ImageLoader()");
	pthread_cond_init(&m_Condition,NULL);
	pthread_mutex_init(&m_ConditionMutex, NULL);
	pthread_mutex_init(&m_CommandMutex, NULL);
	
	AddPixbufLoaderObserver(this);
}

ImageLoader::~ImageLoader()
{
	pthread_cond_destroy(&m_Condition);
	pthread_mutex_destroy(&m_ConditionMutex);
	pthread_mutex_destroy(&m_CommandMutex);
}


void ImageLoader::Start()
{
	//Timer t("ImageLoader::Start()");
	pthread_create(&m_pthread_id, NULL, run, this);
	pthread_detach(m_pthread_id);
}

void ImageLoader::ReloadImage(QuiverFile f)
{
	// to reload, we must first remove the item from the cache
	m_ImageCache.RemovePixbuf(f.GetURI());
	
	
	if (f.GetOrientation() >= 5)
	{
		CacheImage(f);
		usleep(10000);
	}
	// now we can load it
	LoadImage(f);
}

int ImageLoader::Run()
{
	while (true)
	{
		
		pthread_mutex_lock (&m_CommandMutex);

		if (0 == m_Commands.size())
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

		pthread_mutex_lock (&m_CommandMutex);
		
		while (2 < m_Commands.size())
		{
			m_Commands.pop_front();
		}
		
		if (CACHE == m_Commands.front().state && LOAD == m_Commands.back().state)
		{
			m_Commands.pop_front();
		}
		
		m_Command = m_Commands.front();
		m_Commands.pop_front();
		
		pthread_mutex_unlock (&m_CommandMutex);
		Load();
	}
	
	return 0;
}


bool ImageLoader::CommandsPending()
{
	// return true if there are pending commands
	// this can be used to abort the current command
	// and start a new one
	
	bool rval = false;
	
	pthread_mutex_lock (&m_CommandMutex);
	
	while (2 < m_Commands.size())
	{
		m_Commands.pop_front();
	}
	
	if (CACHE == m_Commands.front().state && LOAD == m_Commands.back().state)
	{
		m_Commands.pop_front();
	}
	
	if (0 < m_Commands.size())
	{
		if (LOAD == m_Commands.front().state || LOAD == m_Commands.back().state)
		{
			if (LOAD == m_Commands.front().state && CACHE == m_Command.state)
			{
				if (0 == strcmp(m_Command.quiverFile.GetURI(), m_Commands.front().quiverFile.GetURI()))
				{
					m_Command.state = CACHE_LOAD;
					m_Commands.pop_front();		
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
	
	return rval;
}


void ImageLoader::LoadImage(QuiverFile f)
{
	pthread_mutex_lock (&m_CommandMutex);
	Command c;
	
	c.state = LOAD;
	c.quiverFile = f;
	m_Commands.push_back(c);

	pthread_mutex_lock (&m_ConditionMutex);
	pthread_cond_signal(&m_Condition);
	pthread_mutex_unlock (&m_ConditionMutex);
	pthread_mutex_unlock (&m_CommandMutex);
}

void ImageLoader::CacheImage(QuiverFile f)
{
	pthread_mutex_lock (&m_CommandMutex);

	Command c;
	
	c.state = CACHE;
	c.quiverFile = f;
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

void ImageLoader::Load()
{
	//GError *tmp_error;
	//tmp_error = NULL;

	if (LOAD == m_Command.state)
	{
		GdkPixbuf * pixbuf = m_ImageCache.GetPixbuf(m_Command.quiverFile.GetURI());
		
		if ( NULL == pixbuf)
		{
			if (0 != strcmp(m_Command.quiverFile.GetURI(),""))
			{
				GdkPixbuf *thumb_pixbuf;
				if (m_Command.quiverFile.HasThumbnail(true))
				{
					thumb_pixbuf = m_Command.quiverFile.GetThumbnail(true);
					if (NULL != thumb_pixbuf)
					{
						list<IPixbufLoaderObserver*>::iterator itr;
						for (itr = m_observers.begin();itr != m_observers.end() ; ++itr)
						{
							gdk_threads_enter();
							(*itr)->SetPixbufAtSize(thumb_pixbuf,m_Command.quiverFile.GetWidth(),m_Command.quiverFile.GetHeight());
							gdk_threads_leave();
						}
						g_object_unref(thumb_pixbuf);
					}
					
					
				}
				else if (m_Command.quiverFile.HasThumbnail(false))
				{
					thumb_pixbuf = m_Command.quiverFile.GetThumbnail(false);
					if (NULL != thumb_pixbuf)
					{
						list<IPixbufLoaderObserver*>::iterator itr;
						for (itr = m_observers.begin();itr != m_observers.end() ; ++itr)
						{
							gdk_threads_enter();
							(*itr)->SetPixbufAtSize(thumb_pixbuf,m_Command.quiverFile.GetWidth(),m_Command.quiverFile.GetHeight());
							gdk_threads_leave();
						}
						g_object_unref(thumb_pixbuf);
					}
				}
				
				//cout << "load from file: " << m_Command.quiverFile.GetURI() << endl;
				//cout << "not in cache" << endl;
				// TODO: update all of these calls to call gdk_flush();
				// - what does that mean? i should have commented why 
				//   there is a need to have gdk_flush
				gdk_threads_enter();
				GdkPixbufLoader* loader = gdk_pixbuf_loader_new ();	
				gdk_threads_leave();
				
				list<IPixbufLoaderObserver*>::iterator itr;
				for (itr = m_observers.begin();itr != m_observers.end() ; ++itr)
				{
					gdk_threads_enter();
					(*itr)->SetQuiverFile(m_Command.quiverFile);
					gdk_flush();
					gdk_threads_leave();
					if (1 < m_Command.quiverFile.GetOrientation())
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
					gdk_threads_enter();
					GdkPixbuf* pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);
					gdk_threads_leave();
					
					if (NULL == pixbuf  )
					{
						cout << "pixbuf is null! : " << m_Command.quiverFile.GetURI() << endl;
					}
					else
					{
						g_object_ref(pixbuf);
						if (1 < m_Command.quiverFile.GetOrientation()) // TODO: change to get rotate option
						{
							gdk_threads_enter();
							GdkPixbuf* pixbuf_rotated = QuiverUtils::GdkPixbufExifReorientate(pixbuf,m_Command.quiverFile.GetOrientation());
							gdk_threads_leave();
							
							if (NULL != pixbuf_rotated)
							{
								g_object_unref(pixbuf);
								pixbuf = pixbuf_rotated;
								list<IPixbufLoaderObserver*>::iterator itr;
								for (itr = m_observers.begin();itr != m_observers.end() ; ++itr)
								{
									gdk_threads_enter();
									(*itr)->SetQuiverFile(m_Command.quiverFile);
									gdk_threads_leave();
									
									gdk_threads_enter();
									(*itr)->SetPixbuf(pixbuf);
									gdk_flush();
									gdk_threads_leave();

								}
							}
						}

						m_ImageCache.AddPixbuf(m_Command.quiverFile.GetURI(),pixbuf);
						g_object_unref(pixbuf);
					}
				}
				else
				{
				}
				g_object_unref(loader);
			}
		
		}
		else
		{
			// we'll need to set up the signals ourselves
			//cout << "load from cache: "  << m_Command.filename << endl;
	
			list<IPixbufLoaderObserver*>::iterator itr;
			for (itr = m_observers.begin();itr != m_observers.end() ; ++itr)
			{
				gdk_threads_enter();
				(*itr)->SetQuiverFile(m_Command.quiverFile);
				gdk_threads_leave();
				
				gdk_threads_enter();
				(*itr)->SetPixbuf(pixbuf);
				gdk_threads_leave();
			}
			g_object_unref(pixbuf);
		}
	}	
	else if (CACHE == m_Command.state)
	{

		if (!m_ImageCache.InCache(m_Command.quiverFile.GetURI()))
		{
			if (0 != strcmp(m_Command.quiverFile.GetURI(),""))
			{
				//cout << "cache : " << m_Command.filename << endl;
				gdk_threads_enter();
				GdkPixbufLoader* ldr = gdk_pixbuf_loader_new ();	
				gdk_threads_leave();
			
				list<IPixbufLoaderObserver*>::iterator itr;
				for (itr = m_observers.begin();itr != m_observers.end() ; ++itr)
				{
					gdk_threads_enter();
					(*itr)->SetCacheQuiverFile(m_Command.quiverFile);
					(*itr)->ConnectSignalSizePrepared(ldr);
					gdk_flush();
					gdk_threads_leave();
				}
				
				bool rval = LoadPixbuf(ldr);

				if (rval)
				{
					gdk_threads_enter();
					GdkPixbuf* pixbuf = gdk_pixbuf_loader_get_pixbuf(ldr);
					gdk_threads_leave();
					if (NULL != pixbuf)
					{
						g_object_ref(pixbuf);
						if (1 < m_Command.quiverFile.GetOrientation()) // TODO: change to get rotate option
						{
							gdk_threads_enter();
							GdkPixbuf* pixbuf_rotated = QuiverUtils::GdkPixbufExifReorientate(pixbuf,m_Command.quiverFile.GetOrientation());
							gdk_threads_leave();
							
							if (NULL != pixbuf_rotated)
							{
								g_object_unref(pixbuf);
								pixbuf = pixbuf_rotated;
							}
						}
						if (CACHE_LOAD == m_Command.state)
						{
							list<IPixbufLoaderObserver*>::iterator itr;
							for (itr = m_observers.begin();itr != m_observers.end() ; ++itr)
							{
								gdk_threads_enter();
								(*itr)->SetQuiverFile(m_Command.quiverFile);
								(*itr)->SetPixbuf(pixbuf);
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
						gdk_threads_enter();
						g_object_unref(pixbuf);
						gdk_threads_leave();
					}
				}
				
				//gdk_pixbuf_loader_close(ldr,&tmp_error);
				gdk_threads_enter();
				g_object_unref(ldr);
				gdk_flush();
				gdk_threads_leave();
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
		gdk_pixbuf_loader_close(loader,&tmp_error);	
		gdk_threads_leave();
		return retval;
	}
	Timer loadTimer(true); // true for quite mode
	
	//int size = 8192;
	//int size = 16384;
	int size = 32768;
	long bytes_read_inc=0, bytes_total=0;

	gchar buffer[size];
	GnomeVFSHandle   *handle;
	GnomeVFSResult    result;
	GnomeVFSFileSize  bytes_read;
	bytes_total = m_Command.quiverFile.GetFileInfo()->size;
	

	result = gnome_vfs_open (&handle, m_Command.quiverFile.GetURI(), GNOME_VFS_OPEN_READ);
	list<IPixbufLoaderObserver*>::iterator itr;
	while (GNOME_VFS_OK == result) {
		if (CommandsPending())
		{
			gdk_threads_enter();
			gdk_pixbuf_loader_close(loader,&tmp_error);
			gdk_threads_leave();
			
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
				gdk_threads_enter();
				
				(*itr)->SignalBytesRead(bytes_read_inc,bytes_total);
				gdk_threads_leave();
			}
		}
		
		gdk_threads_enter();
		gdk_pixbuf_loader_write (loader,(guchar*)buffer, bytes_read, &tmp_error);

		gdk_flush();
		gdk_threads_leave();
	}

	m_Command.quiverFile.SetLoadTimeInSeconds( loadTimer.GetRunningTimeInSeconds() );
	

	gdk_threads_enter();
	gdk_pixbuf_loader_close(loader,&tmp_error);
	gdk_flush();
	gdk_threads_leave();

	if (GNOME_VFS_ERROR_EOF == result)
		retval = true;
	gnome_vfs_close(handle);

	
	
	return retval;
}

void ImageLoader::SignalSizePrepared(GdkPixbufLoader *loader,gint width, gint height)
{
	m_Command.quiverFile.SetWidth(width);
	m_Command.quiverFile.SetHeight(height);
}

void* ImageLoader::run(void * data)
{
	((ImageLoader*)data)->Run();
	return 0;
}

