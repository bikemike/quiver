#include <config.h>
#include "ImageCache.h"

#include <gtk/gtk.h>
#include <sys/time.h>

#include <iostream>
using namespace std;


static unsigned long CurrentTimeInMilliseconds();

ImageCache::ImageCache(unsigned int size)
{
	m_iCacheSize = size;
	
	pthread_mutex_init(&m_MutexImageCache, NULL);

}

ImageCache::~ImageCache()
{
	pthread_mutex_lock (&m_MutexImageCache);
	
	ImageCacheMap::iterator itr;
	for (itr = m_mapImageCache.begin(); itr != m_mapImageCache.end(); ++itr)
	{
		g_object_unref(itr->second.pPixbuf);
	}
	m_mapImageCache.clear();
	
	pthread_mutex_unlock (&m_MutexImageCache);

	pthread_mutex_destroy(&m_MutexImageCache);

}

bool ImageCache::RemovePixbuf(std::string filename)
{
	pthread_mutex_lock (&m_MutexImageCache);
	
	ImageCacheMap::iterator itr = m_mapImageCache.find(filename);
	bool rval = false;
	if (m_mapImageCache.end() != itr)
	{
		g_object_unref(itr->second.pPixbuf);
		m_mapImageCache.erase(itr);
		rval = true;
	}
	pthread_mutex_unlock (&m_MutexImageCache);
	return rval;
}

void ImageCache::AddPixbuf(string filename,GdkPixbuf * pb)
{
	AddPixbuf(filename,pb,CurrentTimeInMilliseconds());
}

unsigned int ImageCache::GetSize()
{
	unsigned int size;
	
	pthread_mutex_lock (&m_MutexImageCache);
	
	size = m_iCacheSize;
	
	pthread_mutex_unlock (&m_MutexImageCache);
	
	return size;
}

void ImageCache::SetSize(unsigned int size)
{
	// remove the oldest elements from the cache

	ImageCacheMap::iterator oldest,itr;
	
	pthread_mutex_lock (&m_MutexImageCache);
	
	while (size < m_mapImageCache.size())
	{
		oldest = m_mapImageCache.begin();
		for (itr = oldest; itr != m_mapImageCache.end(); ++itr)
		{
			if (itr->second.time < oldest->second.time)
			{
				oldest = itr;
			}
		}
		if (oldest != m_mapImageCache.end())
		{
			g_object_unref(oldest->second.pPixbuf);
			m_mapImageCache.erase(oldest);
		}
	}
	
	m_iCacheSize = size;

	pthread_mutex_unlock (&m_MutexImageCache);

}

void ImageCache::AddPixbuf(string filename,GdkPixbuf * pb, unsigned long time)
{
	// if item is already here, just update the time of it
	pthread_mutex_lock (&m_MutexImageCache);

	ImageCacheMap::iterator itr = m_mapImageCache.find(filename);

	if (m_mapImageCache.end() != itr)
	{
		// adding the same file again so unref the old pixbuf 
		// before setting the new one
		g_object_ref( pb );
		g_object_unref(itr->second.pPixbuf);
		itr->second.pPixbuf = pb;
		itr->second.time = time;
		pthread_mutex_unlock (&m_MutexImageCache);

		return;
	}

	CacheItem c = {0};
	c.pPixbuf = pb;
	c.time = time;
	g_object_ref(c.pPixbuf);
	

	if (m_mapImageCache.size() >= m_iCacheSize)
	{
		//remove the oldest item
		ImageCacheMap::iterator oldest;
		oldest = m_mapImageCache.begin();
		for (itr = oldest; itr != m_mapImageCache.end(); ++itr)
		{
			if (itr->second.time < oldest->second.time)
			{
				oldest = itr;
			}
		}

		if (m_mapImageCache.end() != oldest)
		{		
			g_object_unref(oldest->second.pPixbuf);
			m_mapImageCache.erase(oldest);	
		}
		
	}
	//add the new item
	m_mapImageCache.insert(pair<string,CacheItem>(filename,c));

	pthread_mutex_unlock (&m_MutexImageCache);
}


bool ImageCache::InCache(std::string filename)
{
	bool rval;
	pthread_mutex_lock (&m_MutexImageCache);
	
	ImageCacheMap::iterator itr = m_mapImageCache.find(filename);
	rval = (m_mapImageCache.end() != itr);
	pthread_mutex_unlock (&m_MutexImageCache);
	
	return rval;
}

GdkPixbuf* ImageCache::GetPixbuf(string filename)
{
	//get an item from the cache and update the time on it
	GdkPixbuf *pixbuf = NULL;

	pthread_mutex_lock (&m_MutexImageCache);
	
	ImageCacheMap::iterator itr = m_mapImageCache.find(filename);
	if (m_mapImageCache.end() != itr)
	{
		itr->second.time = CurrentTimeInMilliseconds();
		pixbuf = itr->second.pPixbuf;
		
		g_object_ref(pixbuf);
	}
	pthread_mutex_unlock (&m_MutexImageCache);

	return pixbuf;
}

void ImageCache::Clear()
{
	pthread_mutex_lock (&m_MutexImageCache);

	ImageCacheMap::iterator itr;
	for (itr = m_mapImageCache.begin(); itr != m_mapImageCache.end(); ++itr)
	{
		g_object_unref(itr->second.pPixbuf);
	}
	
	m_mapImageCache.clear();
	
	pthread_mutex_unlock (&m_MutexImageCache);
}


static unsigned long CurrentTimeInMilliseconds()
{
	timeval tv_time;

	// get time val
	gettimeofday(&tv_time,NULL);

	// convert to milliseconds and return
	return (unsigned long)tv_time.tv_sec * 1000 + (unsigned long)tv_time.tv_usec/1000;

}


