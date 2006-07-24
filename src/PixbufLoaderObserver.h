#ifndef FILE_PIXBUFLOADEROBSERVER_H
#define FILE_PIXBUFLOADEROBSERVER_H

#include <gtk/gtk.h>
//#include <iostream>
#include "QuiverFile.h"
#include "IPixbufLoaderObserver.h"


class PixbufLoaderObserver : public IPixbufLoaderObserver
{
public:

	PixbufLoaderObserver();
	virtual ~PixbufLoaderObserver();

	virtual void ConnectSignals(GdkPixbufLoader *loader);
	virtual void ConnectSignalSizePrepared(GdkPixbufLoader * loader);

	virtual void SignalAreaPrepared(GdkPixbufLoader *loader);
	virtual void SignalAreaUpdated(GdkPixbufLoader *loader,gint x, gint y, gint width,gint height);
	virtual void SignalClosed(GdkPixbufLoader *loader);
	virtual void SignalSizePrepared(GdkPixbufLoader *loader,gint width, gint height);

	// custom calls
	virtual void SetPixbuf(GdkPixbuf * pixbuf);
	virtual void SetPixbufAtSize(GdkPixbuf * pixbuf, gint width, gint height);
	// the image that will be displayed immediately
	virtual void SetQuiverFile(QuiverFile quiverFile);

		
	// the image that is being cached for future use
	virtual void SetCacheQuiverFile(QuiverFile quiverFile);
	virtual void SignalBytesRead(long bytes_read,long total);
private:

                                            
};

#endif
