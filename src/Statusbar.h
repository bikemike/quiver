#ifndef FILE_STATUSBAR_H
#define FILE_STATUSBAR_H

#include <time.h>
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
	

	void SetDateTime();
	void SetDateTime(time_t time); 
	
	
	void SetLoadTime();
	void SetLoadTime(double seconds);
	
	void SetImageSize();
	void SetImageSize(int width, int height);
	
	void SetMagnification(int percent);
	

	
	void StartProgressPulse();
	void StopProgressPulse();

	virtual void SetQuiverFile(QuiverFile quiverFile);
	
	// from pixbufloaderobserver
	virtual void SignalAreaPrepared(GdkPixbufLoader *loader);
	virtual void SignalAreaUpdated(GdkPixbufLoader *loader,gint x, gint y, gint width,gint height);
	virtual void SignalClosed(GdkPixbufLoader *loader);
	virtual void SignalSizePrepared(GdkPixbufLoader *loader,gint width, gint height);
	virtual void SetPixbuf(GdkPixbuf * pixbuf);

	virtual void SignalBytesRead(long bytes_read,long total);
	virtual void SetPixbufAtSize(GdkPixbuf * pixbuf,gint width, gint height, bool bResetViewMode = true);
	

	class StatusbarImpl;
	typedef boost::shared_ptr<StatusbarImpl> StatusbarImplPtr;

private:
	StatusbarImplPtr m_StatusbarImplPtr;

};

typedef boost::shared_ptr<Statusbar> StatusbarPtr;

#endif
