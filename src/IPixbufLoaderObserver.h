#ifndef FILE_IPIXBUFLOADEROBSERVER_H
#define FILE_IPIXBUFLOADEROBSERVER_H

#include <config.h>
#include <gtk/gtk.h>
#if HAVE_GDK_PIXBUF
#include <gdk-pixbuf/gdk-pixbuf.h>
#endif

class IPixbufLoaderObserver
{
public:
	virtual ~IPixbufLoaderObserver(){};

#if HAVE_GDK_PIXBUF
	virtual void ConnectSignals(GdkPixbufLoader *loader) = 0;
	virtual void ConnectSignalSizePrepared(GdkPixbufLoader * loader) = 0;

	// custom calls
	virtual void SetPixbuf(GdkPixbuf * pixbuf) = 0;
	virtual void SetPixbufAtSize(GdkPixbuf * pixbuf,gint width, gint height, bool bResetViewMode = true) = 0;
#endif

	// modern texture calls
	virtual void SetTexture(GdkTexture * texture)
	{
#if HAVE_GDK_PIXBUF
		GdkPixbuf *pb = NULL;
		if (texture)
		{
			int w = gdk_texture_get_width(texture);
			int h = gdk_texture_get_height(texture);
			if (w > 0 && h > 0)
			{
				pb = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, w, h);
				if (pb)
				{
					GdkTextureDownloader *dl = gdk_texture_downloader_new(texture);
					gdk_texture_downloader_set_format(dl, GDK_MEMORY_R8G8B8A8);
					gdk_texture_downloader_download_into(dl, gdk_pixbuf_get_pixels(pb), (gsize)gdk_pixbuf_get_rowstride(pb));
					gdk_texture_downloader_free(dl);
				}
			}
		}
		SetPixbuf(pb);
		if (pb)
			g_object_unref(pb);
#else
		(void)texture;
#endif
	}

	virtual void SetTextureAtSize(GdkTexture * texture, gint width, gint height, bool bResetViewMode = true)
	{
#if HAVE_GDK_PIXBUF
		GdkPixbuf *pb = NULL;
		if (texture)
		{
			int w = gdk_texture_get_width(texture);
			int h = gdk_texture_get_height(texture);
			if (w > 0 && h > 0)
			{
				pb = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, w, h);
				if (pb)
				{
					GdkTextureDownloader *dl = gdk_texture_downloader_new(texture);
					gdk_texture_downloader_set_format(dl, GDK_MEMORY_R8G8B8A8);
					gdk_texture_downloader_download_into(dl, gdk_pixbuf_get_pixels(pb), (gsize)gdk_pixbuf_get_rowstride(pb));
					gdk_texture_downloader_free(dl);
				}
			}
		}
		SetPixbufAtSize(pb, width, height, bResetViewMode);
		if (pb)
			g_object_unref(pb);
#else
		(void)texture;
		(void)width;
		(void)height;
		(void)bResetViewMode;
#endif
	}

	virtual void SignalBytesRead(long bytes_read,long total) = 0;
};

#endif
