#include <config.h>
#include "ImageCache.h"

#include <gtk/gtk.h>
#include <sys/time.h>

#include <iostream>
using namespace std;

static unsigned long CurrentTimeInMilliseconds();

void ImageCache::FreeCacheItem(CacheItem &item)
{
	if (item.pTexture != NULL)
	{
		g_object_unref(item.pTexture);
		item.pTexture = NULL;
	}
	if (item.pPixbuf != NULL)
	{
		g_object_unref(item.pPixbuf);
		item.pPixbuf = NULL;
	}
}

ImageCache::ImageCache(unsigned int size)
	: m_iCacheSize(size)
{
}

ImageCache::~ImageCache()
{
	Clear();
}

bool ImageCache::RemovePixbuf(std::string filename)
{
	return RemoveTexture(filename);
}

bool ImageCache::RemoveTexture(std::string filename)
{
	std::lock_guard<std::mutex> lock(m_MutexImageCache);

	ImageCacheMap::iterator itr = m_mapImageCache.find(filename);
	bool rval = false;
	if (m_mapImageCache.end() != itr)
	{
		FreeCacheItem(itr->second);
		m_mapImageCache.erase(itr);
		rval = true;
	}
	return rval;
}

void ImageCache::AddPixbuf(string filename, GdkPixbuf * pb)
{
	AddPixbuf(filename, pb, CurrentTimeInMilliseconds());
}

void ImageCache::AddTexture(string filename, GdkTexture * texture)
{
	AddTexture(filename, texture, CurrentTimeInMilliseconds());
}

unsigned int ImageCache::GetSize()
{
	std::lock_guard<std::mutex> lock(m_MutexImageCache);
	return m_iCacheSize;
}

void ImageCache::SetSize(unsigned int size)
{
	std::lock_guard<std::mutex> lock(m_MutexImageCache);

	ImageCacheMap::iterator oldest, itr;
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
			FreeCacheItem(oldest->second);
			m_mapImageCache.erase(oldest);
		}
	}

	m_iCacheSize = size;
}

void ImageCache::AddTexture(string filename, GdkTexture * texture, unsigned long time)
{
	std::lock_guard<std::mutex> lock(m_MutexImageCache);

	ImageCacheMap::iterator itr = m_mapImageCache.find(filename);
	if (m_mapImageCache.end() != itr)
	{
		FreeCacheItem(itr->second);
		itr->second.pTexture = texture ? (GdkTexture*)g_object_ref(texture) : NULL;
		itr->second.pPixbuf = NULL;
		itr->second.time = time;
		return;
	}

	if (m_mapImageCache.size() >= m_iCacheSize)
	{
		ImageCacheMap::iterator oldest = m_mapImageCache.begin();
		for (itr = oldest; itr != m_mapImageCache.end(); ++itr)
		{
			if (itr->second.time < oldest->second.time)
			{
				oldest = itr;
			}
		}

		if (m_mapImageCache.end() != oldest)
		{
			FreeCacheItem(oldest->second);
			m_mapImageCache.erase(oldest);
		}
	}

	CacheItem c = {};
	c.pTexture = texture ? (GdkTexture*)g_object_ref(texture) : NULL;
	c.pPixbuf = NULL;
	c.time = time;
	m_mapImageCache.insert(pair<string, CacheItem>(filename, c));
}

G_GNUC_BEGIN_IGNORE_DEPRECATIONS
void ImageCache::AddPixbuf(string filename, GdkPixbuf * pb, unsigned long time)
{
	std::lock_guard<std::mutex> lock(m_MutexImageCache);

	ImageCacheMap::iterator itr = m_mapImageCache.find(filename);
	if (m_mapImageCache.end() != itr)
	{
		FreeCacheItem(itr->second);
		itr->second.pPixbuf = pb ? (GdkPixbuf*)g_object_ref(pb) : NULL;
		itr->second.pTexture = pb ? gdk_texture_new_for_pixbuf(pb) : NULL;
		itr->second.time = time;
		return;
	}

	if (m_mapImageCache.size() >= m_iCacheSize)
	{
		ImageCacheMap::iterator oldest = m_mapImageCache.begin();
		for (itr = oldest; itr != m_mapImageCache.end(); ++itr)
		{
			if (itr->second.time < oldest->second.time)
			{
				oldest = itr;
			}
		}

		if (m_mapImageCache.end() != oldest)
		{
			FreeCacheItem(oldest->second);
			m_mapImageCache.erase(oldest);
		}
	}

	CacheItem c = {};
	c.pPixbuf = pb ? (GdkPixbuf*)g_object_ref(pb) : NULL;
	c.pTexture = pb ? gdk_texture_new_for_pixbuf(pb) : NULL;
	c.time = time;
	m_mapImageCache.insert(pair<string, CacheItem>(filename, c));
}

bool ImageCache::InCache(std::string filename)
{
	std::lock_guard<std::mutex> lock(m_MutexImageCache);
	ImageCacheMap::iterator itr = m_mapImageCache.find(filename);
	return (m_mapImageCache.end() != itr) || (m_setLoadFailures.find(filename) != m_setLoadFailures.end());
}

GdkTexture* ImageCache::GetTexture(string filename)
{
	std::lock_guard<std::mutex> lock(m_MutexImageCache);

	ImageCacheMap::iterator itr = m_mapImageCache.find(filename);
	if (m_mapImageCache.end() != itr)
	{
		itr->second.time = CurrentTimeInMilliseconds();
		if (itr->second.pTexture == NULL && itr->second.pPixbuf != NULL)
		{
			itr->second.pTexture = gdk_texture_new_for_pixbuf(itr->second.pPixbuf);
		}
		if (itr->second.pTexture != NULL)
		{
			return (GdkTexture*)g_object_ref(itr->second.pTexture);
		}
	}
	return NULL;
}

GdkPixbuf* ImageCache::GetPixbuf(string filename)
{
	std::lock_guard<std::mutex> lock(m_MutexImageCache);

	ImageCacheMap::iterator itr = m_mapImageCache.find(filename);
	if (m_mapImageCache.end() != itr)
	{
		itr->second.time = CurrentTimeInMilliseconds();
		if (itr->second.pPixbuf == NULL && itr->second.pTexture != NULL)
		{
			itr->second.pPixbuf = gdk_pixbuf_get_from_texture(itr->second.pTexture);
		}
		if (itr->second.pPixbuf != NULL)
		{
			return (GdkPixbuf*)g_object_ref(itr->second.pPixbuf);
		}
	}
	return NULL;
}
G_GNUC_END_IGNORE_DEPRECATIONS

void ImageCache::Clear()
{
	std::lock_guard<std::mutex> lock(m_MutexImageCache);

	for (auto &pair : m_mapImageCache)
	{
		FreeCacheItem(pair.second);
	}

	m_mapImageCache.clear();
	m_setLoadFailures.clear();
}

void ImageCache::AddFailure(std::string filename)
{
	std::lock_guard<std::mutex> lock(m_MutexImageCache);
	m_setLoadFailures.insert(filename);
}

bool ImageCache::HasFailed(std::string filename)
{
	std::lock_guard<std::mutex> lock(m_MutexImageCache);
	return m_setLoadFailures.find(filename) != m_setLoadFailures.end();
}

static unsigned long CurrentTimeInMilliseconds()
{
	timeval tv_time;
	gettimeofday(&tv_time, NULL);
	return (unsigned long)tv_time.tv_sec * 1000 + (unsigned long)tv_time.tv_usec / 1000;
}


