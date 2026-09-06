#ifndef FILE_IPIXBUFLOADEROBSERVER_H
#define FILE_IPIXBUFLOADEROBSERVER_H

#include <gtk/gtk.h>
//#include <iostream>
//#include "QuiverFile.h"


class IPixbufLoaderObserver
{
public:
	virtual ~IPixbufLoaderObserver(){};
	virtual void ConnectSignals(GdkPixbufLoader *loader) = 0;
	virtual void ConnectSignalSizePrepared(GdkPixbufLoader * loader) = 0;

	/*
	virtual void SignalAreaPrepared(GdkPixbufLoader *loader) = 0;
	virtual void SignalAreaUpdated(GdkPixbufLoader *loader,gint x, gint y, gint width,gint height) = 0;
	virtual void SignalClosed(GdkPixbufLoader *loader) = 0;
	virtual void SignalSizePrepared(GdkPixbufLoader *loader,gint width, gint height) = 0;
	*/

	// custom calls
	virtual void SetPixbuf(GdkPixbuf * pixbuf) = 0;
	virtual void SetPixbufAtSize(GdkPixbuf * pixbuf,gint width, gint height, bool bResetViewMode = true) = 0;

	// modern texture calls (with default fallback to SetPixbuf / SetPixbufAtSize)
	virtual void SetTexture(GdkTexture * texture)
	{
		GdkPixbuf *pb = NULL;
		if (texture)
		{
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
			pb = gdk_pixbuf_get_from_texture(texture);
G_GNUC_END_IGNORE_DEPRECATIONS
		}
		SetPixbuf(pb);
		if (pb)
			g_object_unref(pb);
	}

	virtual void SetTextureAtSize(GdkTexture * texture, gint width, gint height, bool bResetViewMode = true)
	{
		GdkPixbuf *pb = NULL;
		if (texture)
		{
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
			pb = gdk_pixbuf_get_from_texture(texture);
G_GNUC_END_IGNORE_DEPRECATIONS
		}
		SetPixbufAtSize(pb, width, height, bResetViewMode);
		if (pb)
			g_object_unref(pb);
	}

	virtual void SignalBytesRead(long bytes_read,long total) = 0;
	
	
private:
                                            
};

#endif
