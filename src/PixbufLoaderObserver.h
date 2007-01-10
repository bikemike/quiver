#ifndef FILE_PIXBUFLOADEROBSERVER_H
#define FILE_PIXBUFLOADEROBSERVER_H

//#include <iostream>
#include "QuiverFile.h"
#include "IPixbufLoaderObserver.h"

#include <set>

struct _GdkPixbufLoader;
typedef _GdkPixbufLoader GdkPixbufLoader;

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
	virtual void SetPixbufAtSize(GdkPixbuf * pixbuf, gint width, gint height, bool bResetViewMode = true );

	virtual void SignalBytesRead(long bytes_read,long total);
protected:
	//std::set<GdkPixbufLoader*> m_setPixbufLoaders;

private:

                                            
};

#endif
