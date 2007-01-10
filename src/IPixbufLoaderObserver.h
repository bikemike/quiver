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

	virtual void SignalBytesRead(long bytes_read,long total) = 0;
	
	
private:
                                            
};

#endif
