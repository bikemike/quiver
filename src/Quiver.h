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
#include "IBrowserEventHandler.h"


class Quiver : PixbufLoaderObserver
{
public:

  	enum QuiverAppMode{
  		QUIVER_APP_MODE_NORMAL,
  		QUIVER_APP_MODE_SCREENSAVER
  	};

	// constructors
	Quiver(std::list<std::string> &images);
	~Quiver();

	// member functions
	
	// run the application
	void Init();
	int Show(QuiverAppMode);	
	
	
	//events
	//	static gboolean quiver_event_callback( GtkWidget *widget, GdkEvent *event, gpointer data );
	
	gboolean Quiver::TimeoutEventMotionNotify(gpointer data);

	gboolean Quiver::EventMotionNotify( GtkWidget *widget, GdkEventMotion *event, gpointer data );

	
	static gboolean Quiver::timeout_event_motion_notify (gpointer data);
	static gboolean Quiver::event_motion_notify( GtkWidget *widget, GdkEventMotion *event, gpointer data );

	static gboolean Quiver::timeout_advance_slideshow (gpointer data);
	gboolean Quiver::TimeoutAdvanceSlideshow(gpointer data);
	
	static gboolean Quiver::idle_quiver_init (gpointer data);
	gboolean Quiver::IdleQuiverInit(gpointer data);

	static gboolean event_key_press(GtkWidget *widget, GdkEventKey *event, gpointer data );
	static gboolean event_key_release(GtkWidget *widget, GdkEventKey *event, gpointer data );
	static gboolean event_button_press ( GtkWidget *widget, GdkEventButton *event, gpointer data );
	static gboolean event_button_release ( GtkWidget *widget, GdkEventButton *event, gpointer data );
	static gboolean event_scroll( GtkWidget *widget, GdkEventScroll *event, gpointer data );
	static gboolean event_delete( GtkWidget *widget, GdkEvent  *event, gpointer   data );
	static void event_destroy( GtkWidget *widget,gpointer   data );

	static gboolean event_window_state( GtkWidget *widget, GdkEventWindowState *event, gpointer data );
	
	// drag n drop target
	static void signal_drag_data_received (GtkWidget *widget,GdkDragContext *drag_context, gint x,gint y,
                                            GtkSelectionData *data, guint info, guint time,gpointer user_data);
	void SignalDragDataReceived (GtkWidget *widget,GdkDragContext *drag_context, gint x,gint y,
                                            GtkSelectionData *data, guint info, guint time,gpointer user_data);
											
	// drag n' drop source
	static void  signal_drag_data_delete  (GtkWidget *widget,GdkDragContext *context,gpointer data);
	static void signal_drag_data_get  (GtkWidget *widget, GdkDragContext *context, 
		GtkSelectionData *selection_data, guint info, guint time,gpointer data);
	
	static void signal_drag_begin (GtkWidget *widget,GdkDragContext *drag_context,gpointer user_data);
	static void  signal_drag_end(GtkWidget *widget,GdkDragContext *drag_context,gpointer user_data);
	
	// gtk_ui_manager signals
	static void signal_connect_proxy (GtkUIManager *manager,GtkAction *action,GtkWidget*proxy, gpointer data);
	static void signal_disconnect_proxy (GtkUIManager *manager,GtkAction *action,GtkWidget*proxy, gpointer data);
	static void signal_item_select (GtkItem *proxy,gpointer data);
	static void signal_item_deselect (GtkItem *proxy,gpointer data);
	
	void SignalConnectProxy(GtkUIManager *manager,GtkAction *action,GtkWidget*proxy, gpointer data);
	void SignalDisconnectProxy(GtkUIManager *manager,GtkAction *action,GtkWidget*proxy, gpointer data);	
	void SignalItemSelect (GtkItem *proxy,gpointer data);
	void SignalItemDeselect (GtkItem *proxy,gpointer data);
	
	
	
	// drag signals
	void  SignalDragEnd(GtkWidget *widget,GdkDragContext *drag_context,gpointer user_data);
	void  SignalDragBegin (GtkWidget *widget,GdkDragContext *drag_context,gpointer user_data);
	void  SignalDragDataDelete  (GtkWidget *widget,GdkDragContext *context,gpointer data);
	void SignalDragDataGet  (GtkWidget *widget, GdkDragContext *context,GtkSelectionData *selection_data, guint info, guint time,gpointer data);
		
	// drag/drop targets
	enum {
    	QUIVER_TARGET_STRING,
		QUIVER_TARGET_URI
  	};
  	
	static GtkTargetEntry quiver_drag_target_table[];
		
		
		
	gboolean EventButtonPress ( GtkWidget *widget, GdkEventButton *event, gpointer data );
	gboolean EventButtonRelease ( GtkWidget *widget, GdkEventButton *event, gpointer data );
	gboolean EventWindowState( GtkWidget *widget, GdkEventWindowState *event, gpointer data );
	gboolean EventScroll( GtkWidget *widget, GdkEventScroll *event, gpointer data );
	gboolean EventKeyPress( GtkWidget *widget, GdkEventKey *event, gpointer data );
	gboolean EventKeyRelease( GtkWidget *widget, GdkEventKey *event, gpointer data );
	gboolean EventDelete( GtkWidget *widget,GdkEvent  *event,gpointer data );
	void EventDestroy(GtkWidget *widget,gpointer data);
	
	
	
	bool LoadSettings();
	void SaveSettings();
	
	void ShowBrowser();
	void ShowViewer();

	void SetWindowTitle(std::string s);

	void Quiver::CurrentImage();
	void ImageChanged();
	
	void SlideshowStart();
	void SlideshowStop();
	bool SlideshowRunning();
	void SlideshowAddTimeout();
	
	void SetImageList(std::list<std::string> &list);

	// PixbufLoaderObserver  - override to set timeout for slideshow
	void Quiver::SignalClosed(GdkPixbufLoader *loader); 
	void Quiver::SetPixbuf(GdkPixbuf*); 


	// action c callbacks
	static void action_file_save(GtkAction *action,gpointer data);
	static void action_image_next(GtkAction *action,gpointer data);
	static void action_image_last(GtkAction *action,gpointer data);
	static void action_image_first(GtkAction *action,gpointer data);
	static void action_image_previous(GtkAction *action,gpointer data);
	static void action_quit(GtkAction *action,gpointer data);
	static void action_full_screen(GtkAction *action,gpointer data);
	static void action_slide_show(GtkAction *action,gpointer data);
	static void action_image_trash(GtkAction *action,gpointer data);
	static void action_about(GtkAction *action,gpointer data);

	static void action_ui_mode_change(GtkAction *action,gpointer data);

	
	// action c++ callbacks
	void ActionFileSave(GtkAction *action,gpointer data);

	void ActionImageNext(GtkAction *action,gpointer data);
	void ActionImagePrevious(GtkAction *action,gpointer data);
	void ActionImageFirst(GtkAction *action,gpointer data);
	void ActionImageLast(GtkAction *action,gpointer data);
	void ActionQuit(GtkAction *action,gpointer data);
	void ActionFullScreen(GtkAction *action,gpointer data);
	void ActionSlideShow(GtkAction *action,gpointer data);
	void ActionImageTrash(GtkAction *action,gpointer data);
	void ActionAbout(GtkAction *action,gpointer data);
	void ActionUIModeChange(GtkAction *action,gpointer data);
	void ActionViewProperties(GtkAction *action,gpointer data);
	
		
private:
	//Viewer
	//Browser
	
	//StatusBar
	//Toolbar
	//Menu

	Browser m_Browser;
	Viewer m_Viewer;
	ExifView m_ExifView;

	Statusbar m_Statusbar;
	GtkWidget *m_pQuiverWindow;
	GtkWidget *m_pMenubar;
	GtkWidget *m_pToolbar;
	GtkWidget *m_pNBProperties;
	
	guint m_iMergedViewerUI;
	guint m_iMergedBrowserUI;
	
			
	ImageList m_ImageList;
	
	int m_iAppX;
	int m_iAppY;
	int m_iAppWidth;
	int m_iAppHeight;
	
	
	bool m_bTimeoutEventMotionNotifyRunning;
	bool m_bTimeoutEventMotionNotifyMouseMoved;
	
	bool m_bSlideshowRunning;
	guint m_iTimeoutSlideshowID;
	
	QuiverAppMode m_QuiverAppMode;
	
	
	GdkWindowState m_WindowState;
	ImageLoader m_ImageLoader;

	//gui actions
	static GtkActionEntry Quiver::action_entries[];
	static GtkToggleActionEntry Quiver::action_entries_toggle[];
	static GtkRadioActionEntry Quiver::action_entries_radio[];
	
	GtkUIManager* m_pUIManager;
	
	std::list<std::string> m_listImages;

	//class BrowserEventHandler;
	class BrowserEventHandler : public IBrowserEventHandler
	{
	public:
		BrowserEventHandler(Quiver *parent){this->parent = parent;};
		virtual void HandleSelectionChanged(BrowserEventPtr event_ptr);
		virtual void HandleItemActivated(BrowserEventPtr event_ptr);
		virtual void HandleCursorChanged(BrowserEventPtr event_ptr);
	private:
		Quiver *parent;
	};
	
	IBrowserEventHandlerPtr m_BrowserEventHandler;
	

};

// helper functions


#endif

