#include <config.h>

#include "ImageLoader.h"
#include <libgnomevfs/gnome-vfs.h>

using namespace std;

ImageLoader::ImageLoader() : m_ImageCache(4)
{
	//Timer t("ImageLoader::ImageLoader()");
	pthread_cond_init(&m_pthread_cond_id,NULL);
	pthread_mutex_init(&m_pthread_mutex_id, NULL);
	
	pthread_mutex_init(&m_CommandMutex, NULL);
}

ImageLoader::~ImageLoader()
{
	pthread_cond_destroy(&m_pthread_cond_id);
	pthread_mutex_destroy(&m_pthread_mutex_id);
	pthread_mutex_destroy(&m_CommandMutex);
}


void ImageLoader::Start()
{
	//Timer t("ImageLoader::Start()");
	pthread_create(&m_pthread_id, NULL, run, this);
	pthread_detach(m_pthread_id);
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
			
			pthread_mutex_lock (&m_pthread_mutex_id);
			pthread_cond_wait(&m_pthread_cond_id,&m_pthread_mutex_id);
			pthread_mutex_unlock (&m_pthread_mutex_id);

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
				if (m_Command.filename == m_Commands.front().filename)
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


void ImageLoader::LoadImage(string image)
{
	
	pthread_mutex_lock (&m_CommandMutex);

	Command c;
	
	c.state = LOAD;
	c.filename = image;
	m_Commands.push_back(c);
	
	pthread_mutex_lock (&m_pthread_mutex_id);
	pthread_cond_signal(&m_pthread_cond_id);
	pthread_mutex_unlock (&m_pthread_mutex_id);
	
	pthread_mutex_unlock (&m_CommandMutex);
}

void ImageLoader::CacheImage(string image)
{
	pthread_mutex_lock (&m_CommandMutex);

	Command c;
	
	c.state = CACHE;
	c.filename = image;
	m_Commands.push_back(c);
	
	pthread_mutex_lock (&m_pthread_mutex_id);
	pthread_cond_signal(&m_pthread_cond_id);
	pthread_mutex_unlock (&m_pthread_mutex_id);
	
	pthread_mutex_unlock (&m_CommandMutex);
}

void ImageLoader::AddPixbufLoaderObserver(PixbufLoaderObserver * loader_observer)
{
	m_observers.push_back(loader_observer);	
}

void ImageLoader::Load()
{
	//GError *tmp_error;
	//tmp_error = NULL;

	if (LOAD == m_Command.state)
	{
		
		GdkPixbuf * pixbuf = m_ImageCache.GetPixbuf(m_Command.filename);
		
		if ( NULL == pixbuf)
		{
			if (!m_Command.filename.empty())
			{
				//cout << "load from file: " << m_Command.filename << endl;
				//cout << "not in cache" << endl;
				GdkPixbufLoader* loader = gdk_pixbuf_loader_new ();	
				
				list<PixbufLoaderObserver*>::iterator itr;
				for (itr = m_observers.begin();itr != m_observers.end() ; ++itr)
				{
					(*itr)->ConnectSignals(loader);
				}

				bool rval = LoadPixbuf(loader);
				
				if (rval)
				{
					GdkPixbuf* pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);
					if (NULL == pixbuf  )
					{
						cout << "pixbuf is null! : " << m_Command.filename << endl;
					}
					else
					{
						m_ImageCache.AddPixbuf(m_Command.filename,pixbuf);
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
	
			list<PixbufLoaderObserver*>::iterator itr;
			for (itr = m_observers.begin();itr != m_observers.end() ; ++itr)
			{
				(*itr)->SetPixbuf(pixbuf);
			}
		}
	}	
	else if (CACHE == m_Command.state)
	{
		struct timespec   ts;
		struct timeval    tp;
	
/*
	    gettimeofday(&tp, NULL);
	
	    ts.tv_sec  = tp.tv_sec;
	    ts.tv_nsec = tp.tv_usec * 1000;
	    //ts.tv_sec += 1;
		//    1,000,000,000 of a second
	
	
	    //printf("%d ns\n",ts.tv_nsec);
	    ts.tv_nsec += 250 * 1000000; // wait for 50 ms
	    //printf("%d ns\n",ts.tv_nsec);
		//pthread_cond_timedwait(&cond, &mutex, &ts);
		//cout << "waiting " << endl;
		int rc = pthread_cond_timedwait(&m_pthread_cond_id,&m_pthread_mutex_id,&ts);
		if (rc != ETIMEDOUT) {
			m_interrupted = true;
			//cout << "interrupted" << endl;
		}
		//cout << " done wait " << endl;
		
*/		
		if (!m_ImageCache.InCache(m_Command.filename))
		{
			if (!m_Command.filename.empty())
			{
				//cout << "cache : " << m_Command.filename << endl;
				GdkPixbufLoader* ldr = gdk_pixbuf_loader_new ();	
			
				list<PixbufLoaderObserver*>::iterator itr;
				for (itr = m_observers.begin();itr != m_observers.end() ; ++itr)
				{
					(*itr)->ConnectSignalSizePrepared(ldr);
				}
				
				bool rval = LoadPixbuf(ldr);

				if (rval)
				{
					GdkPixbuf* pixbuf = gdk_pixbuf_loader_get_pixbuf(ldr);
					if (NULL != pixbuf)
					{
						if (CACHE_LOAD == m_Command.state)
						{
							list<PixbufLoaderObserver*>::iterator itr;
							for (itr = m_observers.begin();itr != m_observers.end() ; ++itr)
							{
								(*itr)->SetPixbuf(pixbuf);
							}
							m_ImageCache.AddPixbuf(m_Command.filename,pixbuf);
							//cout << "cache loaded image" << endl;
						}
						else
						{
							m_ImageCache.AddPixbuf(m_Command.filename,pixbuf,0);
						}
					}
				}
				
				//gdk_pixbuf_loader_close(ldr,&tmp_error);
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
	//cout << "Loading: " << filename << endl;
	bool retval = false; // did not load

	GError *tmp_error;
	tmp_error = NULL;
	
	if (CommandsPending())
	{	
		gdk_pixbuf_loader_close(loader,&tmp_error);	
		return retval;
	}
	
	int size = 8192;
	

	gchar buffer[size];
	GnomeVFSHandle   *handle;
	GnomeVFSResult    result;
	GnomeVFSFileSize  bytes_read;
	

	result = gnome_vfs_open (&handle, m_Command.filename.c_str(), GNOME_VFS_OPEN_READ);
	
	while (GNOME_VFS_OK == result) {
		if (CommandsPending())
		{
			gdk_pixbuf_loader_close(loader,&tmp_error);		
			gnome_vfs_close(handle);
			return retval;
		}
		result = gnome_vfs_read (handle, buffer, 
								 size, &bytes_read);
		gdk_pixbuf_loader_write (loader,(guchar*)buffer, bytes_read, &tmp_error);
	}
	
	gdk_pixbuf_loader_close(loader,&tmp_error);
	
	if (GNOME_VFS_ERROR_EOF == result)
		retval = true;
	gnome_vfs_close(handle);
	return retval;
}


void* ImageLoader::run(void * data)
{
	((ImageLoader*)data)->Run();
	return 0;
}

