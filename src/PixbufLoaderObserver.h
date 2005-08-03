#ifndef FILE_PIXBUFLOADEROBSERVER_H
#define FILE_PIXBUFLOADEROBSERVER_H

#include <gtk/gtk.h>
//#include <iostream>
#include "QuiverFile.h"


class PixbufLoaderObserver
{
public:

	PixbufLoaderObserver();
	virtual ~PixbufLoaderObserver();
	
	void ConnectSignals(GdkPixbufLoader *loader);
	void ConnectSignalSizePrepared(GdkPixbufLoader * loader);

	virtual void SignalAreaPrepared(GdkPixbufLoader *loader);
	virtual void SignalAreaUpdated(GdkPixbufLoader *loader,gint x, gint y, gint width,gint height);
	virtual void SignalClosed(GdkPixbufLoader *loader);
	virtual void SignalSizePrepared(GdkPixbufLoader *loader,gint width, gint height);

	// custom calls
	virtual void SetPixbuf(GdkPixbuf * pixbuf);
	virtual void SetPixbufFromThread(GdkPixbuf * pixbuf);
	virtual void SetQuiverFile(QuiverFile quiverFile);
	
private:
	static void signal_area_prepared (GdkPixbufLoader *loader,gpointer user_data);
	
	static void signal_area_updated(GdkPixbufLoader *loader,
                                            gint x,
                                            gint y,
                                            gint width,
                                            gint height,
                                            gpointer user_data);
                                            
	static void signal_closed(GdkPixbufLoader *loader,
                                            gpointer user_data);
                                            
	static void signal_size_prepared(GdkPixbufLoader *loader,
                                            gint width,
                                            gint height,
                                            gpointer user_data);
                                            
};

#endif
