#ifndef FILE_IMAGECACHE_H
#define FILE_IMAGECACHE_H

#include <string>
#include <set>
#include <unordered_map>
#include <mutex>

#if HAVE_GDK_PIXBUF
struct _GdkPixbuf;
typedef struct _GdkPixbuf GdkPixbuf;
#endif
struct _GdkTexture;
typedef struct _GdkTexture GdkTexture;

typedef struct _CacheItem
{
	GdkTexture * pTexture;
#if HAVE_GDK_PIXBUF
	GdkPixbuf * pPixbuf;
#endif
	unsigned long time;
} CacheItem;

typedef std::unordered_map<std::string, CacheItem> ImageCacheMap;

class ImageCache
{
public:
	// constructor
	ImageCache(unsigned int size);
	~ImageCache();

#if HAVE_GDK_PIXBUF
	// request a pixbuf (backward compatibility)
	// returns null if not in cache
	GdkPixbuf * GetPixbuf(std::string filename);
	void AddPixbuf(std::string filename, GdkPixbuf * pb);
	void AddPixbuf(std::string filename, GdkPixbuf * pb, unsigned long time);
	bool RemovePixbuf(std::string filename);
#endif

	// request a texture (modern fast path)
	// returns null if not in cache
	GdkTexture * GetTexture(std::string filename);
	void AddTexture(std::string filename, GdkTexture * texture);
	void AddTexture(std::string filename, GdkTexture * texture, unsigned long time);
	bool RemoveTexture(std::string filename);

	bool InCache(std::string filename);
	unsigned int GetSize();
	void SetSize(unsigned int size);

	void Clear();

	void AddFailure(std::string filename);
	bool HasFailed(std::string filename);

private:
	static void FreeCacheItem(CacheItem &item);

	ImageCacheMap m_mapImageCache;
	std::set<std::string> m_setLoadFailures;
	unsigned int m_iCacheSize;

	std::mutex m_MutexImageCache;
};

#endif
