#ifndef FILE_IMAGECACHE_H
#define FILE_IMAGECACHE_H

#include <gtk/gtk.h>
#include <sys/time.h>
#include <map>
#include <string>
#include <pthread.h>


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
	ImageCache(unsigned int size);
	~ImageCache();

	// request a pixbuf 
	// returns null if not in cache
	GdkPixbuf * GetPixbuf(std::string filename);
	bool InCache(std::string filename);
	bool RemovePixbuf(std::string filename);
	
	void AddPixbuf(std::string filename,GdkPixbuf * pb);
	void AddPixbuf(std::string filename,GdkPixbuf * pb, unsigned long time);
	unsigned int GetSize();
	void SetSize(unsigned int size);


	
private:
	std::map<std::string,CacheItem> m_mapImageCache;
	unsigned int m_iCacheSize;
	
	pthread_mutex_t m_MutexImageCache;
	
};

#endif
