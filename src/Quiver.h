#ifndef FILE_QUIVER_H
#define FILE_QUIVER_H


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
#include "ExifView.h"

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

	gboolean EventMotionNotify( GtkWidget *widget, GdkEventMotion *event, gpointer data );

	
	static gboolean timeout_event_motion_notify (gpointer data);
	static gboolean event_motion_notify( GtkWidget *widget, GdkEventMotion *event, gpointer data );

	static gboolean idle_quiver_init (gpointer data);
	gboolean IdleQuiverInit(gpointer data);

	static gboolean event_delete( GtkWidget *widget, GdkEvent  *event, gpointer   data );
	static void event_destroy( GtkWidget *widget,gpointer   data );

	static gboolean event_window_state( GtkWidget *widget, GdkEventWindowState *event, gpointer data );
	
	// gtk_ui_manager signals
	static void signal_connect_proxy (GtkUIManager *manager,GtkAction *action,GtkWidget*proxy, gpointer data);
	static void signal_disconnect_proxy (GtkUIManager *manager,GtkAction *action,GtkWidget*proxy, gpointer data);
	static void signal_item_select (GtkItem *proxy,gpointer data);
	static void signal_item_deselect (GtkItem *proxy,gpointer data);
	
	void SignalConnectProxy(GtkUIManager *manager,GtkAction *action,GtkWidget*proxy, gpointer data);
	void SignalDisconnectProxy(GtkUIManager *manager,GtkAction *action,GtkWidget*proxy, gpointer data);	
	void SignalItemSelect (GtkItem *proxy,gpointer data);
	void SignalItemDeselect (GtkItem *proxy,gpointer data);
	
	gboolean EventWindowState( GtkWidget *widget, GdkEventWindowState *event, gpointer data );

	gboolean EventDelete( GtkWidget *widget,GdkEvent  *event,gpointer data );
	void EventDestroy(GtkWidget *widget,gpointer data);
	
	bool LoadSettings();
	void SaveSettings();
	
	void ShowBrowser();
	void ShowViewer();

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

