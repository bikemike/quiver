#include <config.h>
#include "ImageCache.h"

#include <iostream>
using namespace std;


ImageCache::ImageCache(int size)
{
	m_iCacheSize = size;
}

ImageCache::~ImageCache()
{
	map<string,CacheItem>::iterator itr;
	for (itr = m_mapImageCache.begin(); itr != m_mapImageCache.end(); ++itr)
	{
		g_object_unref(itr->second.pPixbuf);
	}
}

void ImageCache::AddPixbuf(string filename,GdkPixbuf * pb)
{
	AddPixbuf(filename,pb,CurrentTimeMillis());
}

void ImageCache::AddPixbuf(string filename,GdkPixbuf * pb, unsigned long time)
{
	// if item is already here, just update the time of it
	map<string,CacheItem>::iterator itr = m_mapImageCache.find(filename);

	if (m_mapImageCache.end() != itr)
	{
		// adding the same file again so unref the old pixbuf 
		// before setting the new one
		g_object_unref(itr->second.pPixbuf);
		itr->second.pPixbuf = pb;
		g_object_ref( itr->second.pPixbuf );
		itr->second.time = time;
		return;
	}

	CacheItem c;
	c.pPixbuf = pb;
	// add a reference to the item so it doesnt get deleted
	// when the viewer switches images
	//printf("0x%08x refcount: %d\n",c.pPixbuf,G_OBJECT(c.pPixbuf)->ref_count);
	g_object_ref(c.pPixbuf);
	//printf("0x%08x imagecache::addpixbuf refcount after: %d\n",c.pPixbuf,G_OBJECT(c.pPixbuf)->ref_count);
	
	c.time = time;
	

	if (m_mapImageCache.size() >= m_iCacheSize)
	{
		//remove the oldest item
		map<string,CacheItem>::iterator oldest;
		
		for (itr = oldest = m_mapImageCache.begin(); itr != m_mapImageCache.end(); ++itr)
		{
			if (itr->second.time < oldest->second.time)
			{
				oldest = itr;
			}
		}
		
		// remove oldest (making sure to free the reference from the pixbuf)
		//cout << "unreffing " << oldest->first << endl;

		//printf("cached pixbuf: 0x%08x\n",oldest->second.pPixbuf);
		if  (1 < G_OBJECT(oldest->second.pPixbuf)->ref_count)
		{
			cout << "ERROR: MEMORY LEAK ON (" << G_OBJECT(oldest->second.pPixbuf)->ref_count << "): " << oldest->first << endl;

			// FIXME! THIS IS A HACK
			// we should really find the real location where it is not releasing the reference

			while (1 < G_OBJECT(oldest->second.pPixbuf)->ref_count)
			{
				g_object_unref(oldest->second.pPixbuf);
			}
				
		}
		
		//printf("0x%08x ImageCache::AddPixbuf oldest %d\n",oldest->second.pPixbuf,G_OBJECT(oldest->second.pPixbuf)->ref_count);
		//cout << "trying to unref oldest: " << oldest->first << endl;
		g_object_unref(oldest->second.pPixbuf);
		//cout << "unreffed " << oldest->first << endl;
		
		m_mapImageCache.erase(oldest->first);
	}
	//add the new item
	m_mapImageCache.insert(pair<string,CacheItem>(filename,c));
}


bool ImageCache::InCache(std::string filename)
{
	map<string,CacheItem>::iterator itr = m_mapImageCache.find(filename);
	return (m_mapImageCache.end() != itr);
}

GdkPixbuf * ImageCache::GetPixbuf(string filename)
{
	//get an item from the cache and update the time on it
	GdkPixbuf * pixbuf = NULL;
	
	map<string,CacheItem>::iterator itr = m_mapImageCache.find(filename);
	if (m_mapImageCache.end() != itr)
	{
		// FIXME:
		// we shouldnt set the time unless we're getting the image for
		// display in the viewer
		itr->second.time = CurrentTimeMillis();
		pixbuf = itr->second.pPixbuf;
		
		// FIXME: - WE SHOULD ADD A REF
	}

	return pixbuf;
}


unsigned long ImageCache::CurrentTimeMillis()
{
	timeval tv_time;

	// get time val
	gettimeofday(&tv_time,NULL);

	// convert to milliseconds and return
	return (unsigned long)tv_time.tv_sec * 1000 + (unsigned long)tv_time.tv_usec/1000;

}


