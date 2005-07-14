#ifndef FILE_VIEWER_H
#define FILE_VIEWER_H

#include <gtk/gtk.h>
#include <gdk/gdkx.h>

#include <fstream>
#include <iostream>
#include <string>

#include "PixbufLoaderObserver.h"
#include "NavigationControl.h"

class Viewer : public PixbufLoaderObserver

{
public:
	typedef enum _ZoomType
	{
		ZOOM_NONE,
		ZOOM_FIT,
	}ZoomType;
	
	typedef enum _ScrollbarType
	{
		HORIZONTAL,
		VERTICAL,
		BOTH,
	}ScrollbarType;
		

	//constructor
	Viewer();
	
	
	//member functions
	GtkWidget *GetWidget() {return m_pTable;}


	//void SetImage(std::string filename);
	void DrawImage();
	void SetZoomType(ZoomType type);
	ZoomType GetZoomType();
	
	void ScrollbarShow(ScrollbarType t);
	void ScrollbarHide(ScrollbarType t);
	
	void UpdateSize();

//	GdkPixbuf* GetNextPixbuf();
//	GdkPixbuf* GetPrevPixbuf();	

	//events
	void EventDestroy(GtkWidget *widget,gpointer data);
	gboolean EventDelete( GtkWidget *widget,GdkEvent  *event,gpointer data );
	gboolean EventMotionNotify(GtkWidget * widget, GdkEventMotion *event,gpointer data);
	gboolean EventExpose(GtkWidget *widget, GdkEventExpose *event, gpointer data);
	gboolean EventConfigure(GtkWidget * widget, GdkEventConfigure * event, gpointer data);
	gboolean EventEvent( GtkWidget *widget,GdkEvent  *event,gpointer data );
	
	gboolean EventScroll( GtkWidget *widget, GdkEventScroll *event, gpointer data );
	
	gboolean EventButtonPress ( GtkWidget *widget,GdkEventButton  *event,gpointer data );
	gboolean EventButtonRelease ( GtkWidget *widget,GdkEventButton  *event,gpointer data );

	void EventNavButtonClicked (GtkWidget *widget, GdkEventButton *event, void *data);
	
	gboolean IdleEventConfigure(gpointer data);
	gboolean TimeoutEventConfigure(gpointer data);
	
	
	virtual void SignalAreaPrepared(GdkPixbufLoader *loader);
	virtual void SignalAreaUpdated(GdkPixbufLoader *loader,gint x, gint y, gint width,gint height);
	virtual void SignalClosed(GdkPixbufLoader *loader);
	virtual void SignalSizePrepared(GdkPixbufLoader *loader,gint width, gint height);
	virtual void SetPixbuf(GdkPixbuf * pixbuf);



private:


	//event callback converters ( c to c++ )
	static gboolean event_expose(GtkWidget *widget, GdkEventExpose *event, gpointer data);
	static gboolean event_scroll( GtkWidget *widget, GdkEventScroll *event, gpointer data );
	static gboolean event_event( GtkWidget *widget, GdkEvent *event, gpointer data );
	static gboolean event_motion_notify( GtkWidget *widget, GdkEventMotion *event, gpointer data );
	static gboolean event_configure( GtkWidget *widget, GdkEventConfigure *event, gpointer data );
	static gboolean event_button_press ( GtkWidget *widget, GdkEventButton *event, gpointer data );
	static gboolean event_button_release ( GtkWidget *widget, GdkEventButton *event, gpointer data );

	static gboolean timeout_event_configure (gpointer data);
	static gboolean idle_event_configure (gpointer data);

	static void event_nav_button_clicked (GtkWidget *widget, GdkEventButton *event, void *data);



	//private member variables

	GtkWidget * m_pDrawingArea;

	GdkPixbuf * m_pixbuf;
	GdkPixbuf * m_pixbuf_real;
	GtkWidget * m_pTable;
	GtkWidget * m_pViewport;

	GtkAdjustment * m_pAdjustmentH;
	GtkAdjustment * m_pAdjustmentV;
	GtkWidget * m_pScrollbarH;
	GtkWidget * m_pScrollbarV;

	GtkWidget *m_pNavigationBox;
	
	gdouble m_dAdjustmentValueLastH;
	gdouble m_dAdjustmentValueLastV;
	
	NavigationControl * m_pNavigationControl;
	
	bool m_bPressed;

	GdkPoint m_pointLastPointer;


	double m_dZoomLevel;
	ZoomType m_iZoomType;
	
	int m_iLastLine;
	
	bool m_bConfigureTimeoutStarted;
	bool m_bConfigureTimeoutEnded;
	bool m_bConfigureTimeoutRestarted;
	
};

#endif
