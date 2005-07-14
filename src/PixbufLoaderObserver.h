#ifndef FILE_PIXBUFLOADEROBSERVER_H
#define FILE_PIXBUFLOADEROBSERVER_H

#include <gtk/gtk.h>
#include <iostream>


class PixbufLoaderObserver
{
public:

	PixbufLoaderObserver();
	
	void ConnectSignals(GdkPixbufLoader *loader);
	void ConnectSignalSizePrepared(GdkPixbufLoader * loader);

	virtual void SignalAreaPrepared(GdkPixbufLoader *loader);
	virtual void SignalAreaUpdated(GdkPixbufLoader *loader,gint x, gint y, gint width,gint height);
	virtual void SignalClosed(GdkPixbufLoader *loader);
	virtual void SignalSizePrepared(GdkPixbufLoader *loader,gint width, gint height);

	// excplicit override of signals
	virtual void SetPixbuf(GdkPixbuf * pixbuf);
	
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
