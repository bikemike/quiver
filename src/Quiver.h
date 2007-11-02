#ifndef FILE_QUIVER_H
#define FILE_QUIVER_H


#define GDK_PIXBUF_ENABLE_BACKEND

#include <iostream>
#include <fstream>
#include <sstream>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include "PixbufLoaderObserver.h"
#include "Viewer.h"
#include "ImageList.h"
#include "ImageLoader.h"
#include "Statusbar.h"
#include "Browser.h"
#include "Query.h"
#include "ExifView.h"
#include "Database.h"

class QuiverImpl;
typedef boost::shared_ptr<QuiverImpl> QuiverImplPtr;

class Quiver
{
public:

	// constructors
	Quiver(std::list<std::string> &images);
	~Quiver();

	// member functions
	void Init();
	
	gboolean TimeoutEventMotionNotify(gpointer data);

	static gboolean idle_quiver_init (gpointer data);
	gboolean IdleQuiverInit(gpointer data);

	static gboolean event_delete( GtkWidget *widget, GdkEvent  *event, gpointer   data );
	static void event_destroy( GtkWidget *widget,gpointer   data );

	gboolean EventDelete( GtkWidget *widget,GdkEvent  *event,gpointer data );
	void EventDestroy(GtkWidget *widget,gpointer data);
	
	bool LoadSettings();
	void SaveSettings();
	
	void ShowBrowser();
	void ShowViewer();
	void ShowQuery();

	void SetWindowTitle(std::string s);

	void ImageChanged();
	
	void SetImageList(std::list<std::string> &list);


	void OnAbout();
	void OnOpenFile();
	void OnOpenFolder();
	void OnFullScreen();
	void OnShowProperties(bool bShow);
	void OnSlideShow(bool bStart);
	void OnShowToolbar(bool bShow);
	void OnShowStatusbar(bool bShow);
	void OnShowMenubar(bool bShow);
	void OnQuit();
	
	void Close();
		
private:
	QuiverImplPtr m_QuiverImplPtr;

};

#endif

