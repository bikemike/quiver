#include <config.h>
#include "PixbufLoaderObserver.h"

//using namespace std;

#if HAVE_GDK_PIXBUF
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
#endif

PixbufLoaderObserver::PixbufLoaderObserver()
{
}

PixbufLoaderObserver::~PixbufLoaderObserver()
{
}

#if HAVE_GDK_PIXBUF
void PixbufLoaderObserver::ConnectSignalSizePrepared(GdkPixbufLoader * loader)
{
	g_signal_connect (loader,"size-prepared",G_CALLBACK (signal_size_prepared), this);	
}

void PixbufLoaderObserver::ConnectSignals(GdkPixbufLoader *loader)
{
	g_signal_connect (loader,"area-prepared",G_CALLBACK (signal_area_prepared), this);
	g_signal_connect (loader,"area-updated",G_CALLBACK (signal_area_updated), this);
	g_signal_connect (loader,"closed",G_CALLBACK (signal_closed), this);
	g_signal_connect (loader,"size-prepared",G_CALLBACK (signal_size_prepared), this);	

}

void PixbufLoaderObserver::SetPixbuf(GdkPixbuf * pixbuf)
{ (void)pixbuf; 
}

void PixbufLoaderObserver::SetPixbufAtSize(GdkPixbuf * pixbuf, gint width, gint height, bool bResetViewMode /* = false */)
{ (void)bResetViewMode;  (void)height;  (void)width;  (void)pixbuf; 
}
#endif

void PixbufLoaderObserver::SetTexture(GdkTexture * texture)
{ (void)texture; }

void PixbufLoaderObserver::SetTextureAtSize(GdkTexture * texture, gint width, gint height, bool bResetViewMode /* = false */)
{ (void)bResetViewMode;  (void)height;  (void)width;  (void)texture; }


void PixbufLoaderObserver::SignalBytesRead(long bytes_read,long total)
{ (void)total;  (void)bytes_read; 
}

#if HAVE_GDK_PIXBUF
void PixbufLoaderObserver::SignalAreaPrepared(GdkPixbufLoader *loader)
{ (void)loader; 
}
void PixbufLoaderObserver::SignalAreaUpdated(GdkPixbufLoader *loader,gint x, gint y, gint width,gint height)
{ (void)height;  (void)width;  (void)y;  (void)x;  (void)loader; 
}
void PixbufLoaderObserver::SignalClosed(GdkPixbufLoader *loader)
{ (void)loader; 
}
void PixbufLoaderObserver::SignalSizePrepared(GdkPixbufLoader *loader,gint width, gint height)
{ (void)height;  (void)width;  (void)loader; 
}

static void signal_area_prepared (GdkPixbufLoader *loader,gpointer user_data)
{
	((PixbufLoaderObserver*)user_data)->SignalAreaPrepared(loader);
}

static void signal_area_updated(GdkPixbufLoader *loader,
                                        gint x,
                                        gint y,
                                        gint width,
                                        gint height,
                                        gpointer user_data)
{
	((PixbufLoaderObserver*)user_data)->SignalAreaUpdated(loader,x,y,width,height);
}
                                        
static void signal_closed(GdkPixbufLoader *loader,
                                        gpointer user_data)

{
	((PixbufLoaderObserver*)user_data)->SignalClosed(loader);
}                                       
static void signal_size_prepared(GdkPixbufLoader *loader,
                                        gint width,
                                        gint height,
                                        gpointer user_data)
{
	((PixbufLoaderObserver*)user_data)->SignalSizePrepared(loader,width,height);
}                                           
#endif                                           




