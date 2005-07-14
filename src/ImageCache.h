#ifndef FILE_IMAGECACHE_H
#define FILE_IMAGECACHE_H

#include <gtk/gtk.h>
#include "sys/time.h"
#include <map>
#include <string>


typedef struct _CacheItem
{
	GdkPixbuf * pPixbuf;
	unsigned long time;
	
} CacheItem;

class ImageCache
{
	// number in cache

public:
	//constructor
	ImageCache(int size);
	~ImageCache();

	// request a pixbuf 
	// returns null if not in cache
	GdkPixbuf * ImageCache::GetPixbuf(std::string filename);
	bool ImageCache::InCache(std::string filename);
	
	void ImageCache::AddPixbuf(std::string filename,GdkPixbuf * pb);
	void ImageCache::AddPixbuf(std::string filename,GdkPixbuf * pb, unsigned long time);

	
private:
	std::map<std::string,CacheItem> m_mapImageCache;
	unsigned int m_iCacheSize;
	
	unsigned long CurrentTimeMillis();
};

#endif
