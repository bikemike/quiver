#ifndef FILE_STATUSBAR_H
#define FILE_STATUSBAR_H

#include "PixbufLoaderObserver.h"

class Statusbar : public PixbufLoaderObserver
{
public:
	Statusbar();
	GtkWidget * GetWidget();
	
	void SetPosition(int pos, int n);
	
	void PushText(std::string s);
	void PopText();
	void SetText(std::string s);
	
	void SetText();
	
	void SetLoadTime();
	void SetLoadTime(double seconds);
	void SetZoomPercent(int percent);
	
	
	// from pixbufloaderobserver
	void SignalAreaPrepared(GdkPixbufLoader *loader);
	void SignalAreaUpdated(GdkPixbufLoader *loader,gint x, gint y, gint width,gint height);
	void SignalClosed(GdkPixbufLoader *loader);
	void SignalSizePrepared(GdkPixbufLoader *loader,gint width, gint height);
	void SetPixbuf(GdkPixbuf * pixbuf);
	void SetPixbufFromThread(GdkPixbuf * pixbuf);
	void SetQuiverFile(QuiverFile quiverFile);
	void SetCacheQuiverFile(QuiverFile quiverFile);
	void SignalBytesRead(long bytes_read,long total);
	

	
private:

	QuiverFile m_CurrentQuiverFile;

	GtkWidget * m_pWidget;
	GtkWidget * m_pStatusbar;
	GtkWidget * m_pLabelLoadTime;

	GtkWidget * m_pLabelListPosition;
	GtkWidget * m_pProgressbar;
	GtkWidget * m_pLabelZoom;


	
	guint m_iDefaultContext;
};

#endif
