#include <config.h>
#include "ImageCache.h"
#include "IPixbufLoaderObserver.h"
#include "QuiverUtils.h"

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
#if HAVE_GDK_PIXBUF
	if (item.pPixbuf != NULL)
	{
		g_object_unref(item.pPixbuf);
		item.pPixbuf = NULL;
	}
#endif
	if (item.animation_frames != NULL)
	{
		quiver_animation_frames_free(item.animation_frames, item.animation_delays, item.animation_count);
		item.animation_frames = NULL;
		item.animation_delays = NULL;
		item.animation_count = 0;
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

#if HAVE_GDK_PIXBUF
bool ImageCache::RemovePixbuf(std::string filename)
{
	return RemoveTexture(filename);
}
#endif

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

#if HAVE_GDK_PIXBUF
void ImageCache::AddPixbuf(string filename, GdkPixbuf * pb)
{
	AddPixbuf(filename, pb, CurrentTimeInMilliseconds());
}
#endif

void ImageCache::AddTexture(string filename, GdkTexture * texture)
{
	AddTexture(filename, texture, CurrentTimeInMilliseconds());
}

void ImageCache::AddTexture(string filename, GdkTexture * texture, GdkTexture ** frames, gint * delays, gsize count)
{
	AddTexture(filename, texture, frames, delays, count, CurrentTimeInMilliseconds());
}

void ImageCache::AddTexture(string filename, GdkTexture * texture, GdkTexture ** frames, gint * delays, gsize count, unsigned long time)
{
	std::lock_guard<std::mutex> lock(m_MutexImageCache);

	ImageCacheMap::iterator itr = m_mapImageCache.find(filename);
	if (m_mapImageCache.end() != itr)
	{
		FreeCacheItem(itr->second);
		itr->second.pTexture = texture ? (GdkTexture*)g_object_ref(texture) : NULL;
#if HAVE_GDK_PIXBUF
		itr->second.pPixbuf = NULL;
#endif
		if (frames != NULL && count > 0)
		{
			itr->second.animation_count = count;
			itr->second.animation_frames = (GdkTexture**)g_new0(GdkTexture*, count);
			itr->second.animation_delays = (gint*)g_new0(gint, count);
			for (gsize i = 0; i < count; i++)
			{
				itr->second.animation_frames[i] = frames[i] ? (GdkTexture*)g_object_ref(frames[i]) : NULL;
				itr->second.animation_delays[i] = delays ? delays[i] : 0;
			}
		}
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
#if HAVE_GDK_PIXBUF
	c.pPixbuf = NULL;
#endif
	if (frames != NULL && count > 0)
	{
		c.animation_count = count;
		c.animation_frames = (GdkTexture**)g_new0(GdkTexture*, count);
		c.animation_delays = (gint*)g_new0(gint, count);
		for (gsize i = 0; i < count; i++)
		{
			c.animation_frames[i] = frames[i] ? (GdkTexture*)g_object_ref(frames[i]) : NULL;
			c.animation_delays[i] = delays ? delays[i] : 0;
		}
	}
	c.time = time;
	m_mapImageCache.insert(pair<string, CacheItem>(filename, c));
}

gsize ImageCache::GetAnimationFrames(string filename, GdkTexture *** frames, gint ** delays)
{
	std::lock_guard<std::mutex> lock(m_MutexImageCache);

	*frames = NULL;
	*delays = NULL;

	ImageCacheMap::iterator itr = m_mapImageCache.find(filename);
	if (m_mapImageCache.end() != itr)
	{
		itr->second.time = CurrentTimeInMilliseconds();
		gsize count = itr->second.animation_count;
		if (count >= 2 && itr->second.animation_frames != NULL)
		{
			GdkTexture **f = (GdkTexture**)g_new0(GdkTexture*, count);
			gint *d = (gint*)g_new0(gint, count);
			for (gsize i = 0; i < count; i++)
			{
				f[i] = itr->second.animation_frames[i] ? (GdkTexture*)g_object_ref(itr->second.animation_frames[i]) : NULL;
				d[i] = itr->second.animation_delays ? itr->second.animation_delays[i] : 0;
			}
			*frames = f;
			*delays = d;
			return count;
		}
	}
	return 0;
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
#if HAVE_GDK_PIXBUF
	c.pPixbuf = NULL;
#endif
	c.time = time;
	m_mapImageCache.insert(pair<string, CacheItem>(filename, c));
}

#if HAVE_GDK_PIXBUF
void ImageCache::AddPixbuf(string filename, GdkPixbuf * pb, unsigned long time)
{
	std::lock_guard<std::mutex> lock(m_MutexImageCache);

	ImageCacheMap::iterator itr = m_mapImageCache.find(filename);
	if (m_mapImageCache.end() != itr)
	{
		FreeCacheItem(itr->second);
		itr->second.pPixbuf = pb ? (GdkPixbuf*)g_object_ref(pb) : NULL;
		itr->second.pTexture = pb ? QuiverUtils::PixbufToTexture(pb) : NULL;
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
	c.pTexture = pb ? QuiverUtils::PixbufToTexture(pb) : NULL;
	c.time = time;
	m_mapImageCache.insert(pair<string, CacheItem>(filename, c));
}
#endif

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
#if HAVE_GDK_PIXBUF
		if (itr->second.pTexture == NULL && itr->second.pPixbuf != NULL)
		{
			itr->second.pTexture = QuiverUtils::PixbufToTexture(itr->second.pPixbuf);
		}
#endif
		if (itr->second.pTexture != NULL)
		{
			return (GdkTexture*)g_object_ref(itr->second.pTexture);
		}
	}
	return NULL;
}

#if HAVE_GDK_PIXBUF
GdkPixbuf* ImageCache::GetPixbuf(string filename)
{
	std::lock_guard<std::mutex> lock(m_MutexImageCache);

	ImageCacheMap::iterator itr = m_mapImageCache.find(filename);
	if (m_mapImageCache.end() != itr)
	{
		itr->second.time = CurrentTimeInMilliseconds();
		if (itr->second.pPixbuf == NULL && itr->second.pTexture != NULL)
		{
			int w = gdk_texture_get_width(itr->second.pTexture);
			int h = gdk_texture_get_height(itr->second.pTexture);
			if (w > 0 && h > 0)
			{
				GdkPixbuf* pb = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, w, h);
				if (pb)
				{
					GdkTextureDownloader *dl = gdk_texture_downloader_new(itr->second.pTexture);
					gdk_texture_downloader_set_format(dl, GDK_MEMORY_R8G8B8A8);
					gdk_texture_downloader_download_into(dl, gdk_pixbuf_get_pixels(pb), (gsize)gdk_pixbuf_get_rowstride(pb));
					gdk_texture_downloader_free(dl);
					itr->second.pPixbuf = pb;
				}
			}
		}
		if (itr->second.pPixbuf != NULL)
		{
			return (GdkPixbuf*)g_object_ref(itr->second.pPixbuf);
		}
	}
	return NULL;
}
#endif

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

void ImageCache::RemoveFailure(std::string filename)
{
	std::lock_guard<std::mutex> lock(m_MutexImageCache);
	m_setLoadFailures.erase(filename);
}

static unsigned long CurrentTimeInMilliseconds()
{
	timeval tv_time;
	gettimeofday(&tv_time, NULL);
	return (unsigned long)tv_time.tv_sec * 1000 + (unsigned long)tv_time.tv_usec / 1000;
}


