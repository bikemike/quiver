#ifndef FILE_NAVIGATION_CONTROL_H
#define FILE_NAVIGATION_CONTROL_H

#include <gtk/gtk.h>
#include <fstream>
#include <iostream>
#include <string>


class NavigationControl
{

public:

	NavigationControl();
	~NavigationControl();
	
	void SetMaxSize(int w, int h);
	
	GtkWidget * GetWidget();
	
	void Show(GtkViewport * viewport,GdkPixbuf * pixbuf,double x,double y);


	gboolean EventExpose(GtkWidget *widget, GdkEventExpose *event, gpointer data);

	gboolean EventMotionNotify(GtkWidget * widget, GdkEventMotion *event,gpointer data);
	gboolean EventButtonRelease ( GtkWidget *widget,GdkEventButton  *event,gpointer data );



private:

	static gboolean event_expose(GtkWidget *widget, GdkEventExpose *event, gpointer data);
	static gboolean event_motion_notify( GtkWidget *widget, GdkEventMotion *event, gpointer data );
	static gboolean event_button_release ( GtkWidget *widget, GdkEventButton *event, gpointer data );

	GdkPixbuf * m_pNavPixbuf;
	GdkPixbuf * m_pNavPixbufReal;
	GtkWidget * m_pDrawingArea;
	GtkWidget * m_pNavigationWindow;
	GtkViewport * m_pViewport;
	GdkGeometry m_GdkGeometry;
	GdkGC *m_pLineGC;
	
	GdkRectangle m_LastRect;
	GdkRectangle m_CurrRect;
	
};


#endif

