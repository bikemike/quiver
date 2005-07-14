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


class Quiver
{
public:
	// constructors
	Quiver(std::list<std::string> images);
	~Quiver();

	// member functions
	
	// run the application
	void Init();
	int Show(void);	
	
	
	//events
	//	static gboolean quiver_event_callback( GtkWidget *widget, GdkEvent *event, gpointer data );
	
	gboolean Quiver::TimeoutEventMotionNotify(gpointer data);
	gboolean Quiver::EventMotionNotify( GtkWidget *widget, GdkEventMotion *event, gpointer data );
	
	static gboolean Quiver::event_motion_notify( GtkWidget *widget, GdkEventMotion *event, gpointer data );
	static gboolean Quiver::timeout_event_motion_notify (gpointer data);
	
	
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
	
	
	void  SignalDragEnd(GtkWidget *widget,GdkDragContext *drag_context,gpointer user_data);
	
	void  SignalDragBegin (GtkWidget *widget,GdkDragContext *drag_context,gpointer user_data);
	
	void  SignalDragDataDelete  (GtkWidget *widget,GdkDragContext *context,gpointer data);
	void SignalDragDataGet  (GtkWidget *widget, GdkDragContext *context, 
		GtkSelectionData *selection_data, guint info, guint time,gpointer data);
		
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
	void FullScreen();
	void SetWindowTitle(std::string s);
	void NextImage();
	void PreviousImage();
	void Quiver::CurrentImage();
	
	void SetImageList(std::list<std::string> &list);
		
private:
		//Viewer
		//Browser
		
		//StatusBar
		//Toolbar
		//Menu

		Viewer m_Viewer;
		GtkWidget *m_pQuiverWindow;
		ImageList * m_pImageList;
		
		int m_iAppX;
		int m_iAppY;
		int m_iAppWidth;
		int m_iAppHeight;
		
		
		bool m_bTimeoutEventMotionNotifyRunning;
		bool m_bTimeoutEventMotionNotifyMouseMoved;
		
		
		GdkWindowState m_WindowState;
		ImageLoader im;

};

#endif

