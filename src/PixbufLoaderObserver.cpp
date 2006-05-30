#include <config.h>
#include "PixbufLoaderObserver.h"

//using namespace std;

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


PixbufLoaderObserver::PixbufLoaderObserver()
{
}

PixbufLoaderObserver::~PixbufLoaderObserver()
{
}


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
{
	//printf("PixbufLoaderObserver::SetPixbuf\n");
}

void PixbufLoaderObserver::SignalBytesRead(long bytes_read,long total)
{
}

void PixbufLoaderObserver::SetCacheQuiverFile(QuiverFile quiverFile)
{
}
void PixbufLoaderObserver::SetQuiverFile(QuiverFile quiverFile)
{
	//printf("PixbufLoaderObserver::SetQuiverFile\n");
}

void PixbufLoaderObserver::SignalAreaPrepared(GdkPixbufLoader *loader)
{
	//printf("PixbufLoaderObserver::SignalAreaPrepared\n");
}
void PixbufLoaderObserver::SignalAreaUpdated(GdkPixbufLoader *loader,gint x, gint y, gint width,gint height)
{
	//printf("PixbufLoaderObserver::SignalAreaUpdated x=%d, y=%d, width=%d, height=%d\n",x,y,width,height);
}
void PixbufLoaderObserver::SignalClosed(GdkPixbufLoader *loader)
{
	//printf("PixbufLoaderObserver::SignalClosed\n");
}
void PixbufLoaderObserver::SignalSizePrepared(GdkPixbufLoader *loader,gint width, gint height)
{
	//printf("PixbufLoaderObserver::SignalSizePrepared: width=%d, height=%d\n",width,height);
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




