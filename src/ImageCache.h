#ifndef FILE_IMAGECACHE_H
#define FILE_IMAGECACHE_H

#include <string>
#include <pthread.h>


struct _GdkPixbuf;
typedef _GdkPixbuf GdkPixbuf;

typedef struct _CacheItem
{
	GdkPixbuf * pPixbuf;
	unsigned long time;
	
} CacheItem;

// comment this define out if __gnu_cxx is not available
#ifdef HAVE_CXX0X
#include <unordered_map>
typedef std::unordered_map<std::string,CacheItem>  ImageCacheMap;
#elif defined(HAVE_TR1)
#include <tr1/unordered_map>
typedef std::tr1::unordered_map<std::string,CacheItem>  ImageCacheMap;
#elif defined(HAVE_EXT)
#include <ext/hash_map>
#ifndef QUIVER_STRING_HASH
#define QUIVER_STRING_HASH
namespace __gnu_cxx {
	template<> struct hash<std::string>
	{
		size_t operator()(const std::string &s) const
		{ return __stl_hash_string(s.c_str()); }
	};
}
#endif
typedef __gnu_cxx::hash_map<std::string,CacheItem>  ImageCacheMap;
#else
#include <map>
typedef std::map<std::string,CacheItem>  ImageCacheMap;
#endif


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
	
	void Clear();


	
private:
	ImageCacheMap m_mapImageCache;
	unsigned int m_iCacheSize;
	
	pthread_mutex_t m_MutexImageCache;
	
};

#endif
