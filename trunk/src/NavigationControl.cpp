#include <config.h>
#include "NavigationControl.h"

using namespace std;


NavigationControl::NavigationControl()
{
	m_GdkGeometry.max_width = m_GdkGeometry.max_height = 160;
	
	m_pNavigationWindow = gtk_window_new (GTK_WINDOW_POPUP);
	gtk_window_set_wmclass (GTK_WINDOW (m_pNavigationWindow), "",
                                "quiver_navigation_control");
	
	m_pDrawingArea = gtk_drawing_area_new ();

 	gtk_widget_add_events (m_pDrawingArea, GDK_POINTER_MOTION_MASK|GDK_POINTER_MOTION_HINT_MASK);
	gtk_widget_add_events (m_pDrawingArea, GDK_BUTTON_PRESS_MASK|GDK_BUTTON_RELEASE_MASK);

	GtkWidget * frame = gtk_frame_new(NULL);
	gtk_frame_set_shadow_type(GTK_FRAME(frame),GTK_SHADOW_ETCHED_IN);	

	
	gtk_container_add (GTK_CONTAINER (frame), m_pDrawingArea);
	gtk_container_add (GTK_CONTAINER (m_pNavigationWindow), frame);

	g_signal_connect (G_OBJECT (m_pDrawingArea), "expose_event",  
				G_CALLBACK (NavigationControl::event_expose), this);
	g_signal_connect (G_OBJECT (m_pDrawingArea), "motion_notify_event",  
				G_CALLBACK (NavigationControl::event_motion_notify), this);
	g_signal_connect (G_OBJECT (m_pDrawingArea), "button_release_event",  
				G_CALLBACK (NavigationControl::event_button_release), this);

	m_pLineGC = NULL;
	m_pNavPixbuf = NULL;
	m_LastRect.x = m_CurrRect.x = -1;

}


NavigationControl::~NavigationControl()
{
	gtk_widget_destroy(m_pNavigationWindow);

	if (NULL != m_pLineGC)
	{
		g_object_unref(m_pLineGC);
	}

	if (NULL != m_pNavPixbuf)
	{
		g_object_unref(m_pNavPixbuf);
	}
}

GtkWidget * NavigationControl::GetWidget()
{
	return m_pNavigationWindow;
}

gboolean NavigationControl::event_expose(GtkWidget *widget, GdkEventExpose *event, gpointer data)
{
	return ((NavigationControl*)data)->EventExpose(widget,event,data);
}


gboolean NavigationControl::EventExpose(GtkWidget *widget, GdkEventExpose *event, gpointer data)
{
	if (NULL != m_pNavPixbuf)
	{
		gdk_draw_pixbuf	(m_pDrawingArea->window,
	                     m_pDrawingArea->style->fg_gc[GTK_WIDGET_STATE (m_pDrawingArea)],
	                     m_pNavPixbuf,event->area.x,event->area.y,event->area.x,event->area.y,event->area.width,event->area.height,
	                     GDK_RGB_DITHER_NONE,0,0);

		if (-1 != m_CurrRect.x)
		{
			gdk_draw_rectangle (m_pDrawingArea->window, 
			   m_pLineGC, FALSE, 
			    m_CurrRect.x, 
			    m_CurrRect.y, 
			    m_CurrRect.width-1,
			    m_CurrRect.height-1);
		}
	}

	return TRUE;
}

void NavigationControl::Show(GtkViewport * viewport,GdkPixbuf * pixbuf,double x,double y)
{
	m_LastRect.x = -1;
	m_CurrRect.x = -1;
	m_pViewport = viewport;
	gtk_widget_hide_all (m_pNavigationWindow);

	if (NULL == pixbuf)
	{
		return;
	}
	cout << " not null" << endl;
	int w,h;
	if (m_pNavPixbufReal != pixbuf)
	{
		if (NULL != m_pNavPixbuf)
		{
			g_object_unref(m_pNavPixbuf);
		}
		
		m_pNavPixbufReal = pixbuf;
		
		w = gdk_pixbuf_get_width(pixbuf);
		h = gdk_pixbuf_get_height(pixbuf);
		int new_width,new_height;
		m_GdkGeometry.min_aspect = m_GdkGeometry.max_aspect = w/(double)h;
	
		int geoflags = GDK_HINT_MAX_SIZE|GDK_HINT_ASPECT;
		gdk_window_constrain_size(&m_GdkGeometry,geoflags,w,h,&new_width,&new_height);

		m_pNavPixbuf = gdk_pixbuf_scale_simple (pixbuf, new_width, new_height, GDK_INTERP_BILINEAR);

	}
	w = gdk_pixbuf_get_width(m_pNavPixbuf);
	h = gdk_pixbuf_get_height(m_pNavPixbuf);
	
	
	gtk_widget_set_size_request(m_pDrawingArea,w,h);
  	gtk_window_resize (GTK_WINDOW (m_pNavigationWindow),w, h);
  	
  	int ww, wh, pos_x,pos_y;
  	ww = w+2;
  	wh = h+2;
  	
  	pos_x = (int)x - ww/2;
  	pos_y = (int)y - wh/2;
  		
	
  	if (gdk_screen_width () < pos_x + ww)
  		pos_x = gdk_screen_width() - ww;
  	else if (0 > pos_x)
  		pos_x = 0;
	
  	if (gdk_screen_height () < pos_y + wh)
  		pos_y = gdk_screen_height() - wh;
  	else if (0 > pos_y)
  		pos_y = 0;
	
	gtk_window_move (GTK_WINDOW (m_pNavigationWindow), pos_x,pos_y);
	gtk_widget_show_all (m_pNavigationWindow);
	
	if (m_pLineGC != NULL)
		g_object_unref(m_pLineGC);

	m_pLineGC = gdk_gc_new (m_pNavigationWindow->window);

	gdk_gc_set_function (m_pLineGC, GDK_INVERT);
	gdk_gc_set_line_attributes (m_pLineGC,3,GDK_LINE_SOLID, GDK_CAP_BUTT,GDK_JOIN_MITER);
	


	GdkCursor *cursor;
/*
	GdkBitmap * nil;
	char zero[] = { 0x0 };	
	GdkColor blank = { 0, 0, 0, 0 };	

	nil = gdk_bitmap_create_from_data (NULL,zero,1,1);
	cursor = gdk_cursor_new_from_pixmap (nil,nil,&blank,&blank,0,0);

	g_object_unref(nil);
*/	
	cursor = gdk_cursor_new (GDK_FLEUR); 
	gdk_pointer_grab (m_pNavigationWindow->window,  TRUE, (GdkEventMask)(GDK_BUTTON_RELEASE_MASK  | GDK_POINTER_MOTION_HINT_MASK    | GDK_BUTTON_MOTION_MASK   | GDK_EXTENSION_EVENTS_ALL),
			  m_pDrawingArea->window, 
			  cursor,
			  GDK_CURRENT_TIME);
	gdk_cursor_unref (cursor);

	
}


gboolean NavigationControl::EventMotionNotify(GtkWidget * widget, GdkEventMotion *event,gpointer data)
{
	int x,y;
	GdkModifierType state;
	
	if (event->is_hint)
	{
		//printf("hint\n");
		gdk_window_get_pointer (event->window, &x, &y, &state);
	}		
	else
	{
		//printf("event\n");
		x = (int)event->x;
		y = (int)event->y;
		state = (GdkModifierType)event->state;
		
	}

	GtkAdjustment * hadj = gtk_viewport_get_hadjustment(m_pViewport);
	GtkAdjustment * vadj = gtk_viewport_get_vadjustment(m_pViewport);
	int w,h;
	w = gdk_pixbuf_get_width(m_pNavPixbuf);
	h = gdk_pixbuf_get_height(m_pNavPixbuf);
	
	double xval, yval;
	xval = x/(double)w*hadj->upper - hadj->page_size/2;
	yval = y/(double)h*vadj->upper - vadj->page_size/2;

	if (hadj->upper - hadj->page_size < xval)
		xval = hadj->upper - hadj->page_size;
	else if (x < 0)
		xval = 0;

	if (vadj->upper - vadj->page_size < yval)
		yval = vadj->upper - vadj->page_size;
	else if (y < 0)
		yval = 0;
		
		
	// calc box offsets and width
	int b_x = x - m_CurrRect.width/2;
	int b_y = y - m_CurrRect.height/2;
	int b_w = (int)(hadj->page_size * (w/hadj->upper));
	int b_h = (int)(vadj->page_size * (h/vadj->upper));
	
	if (0 > b_x)
		b_x = 0;
	else if (b_x + b_w > w)
		b_x = w - b_w;

	if (0 > b_y)
		b_y = 0;
	else if (b_y + b_h > h)
		b_y = h - b_h;
		
	// set up the box size
	if (-1 == m_CurrRect.x)
	{
		m_CurrRect.width = m_LastRect.width = b_w;
		m_CurrRect.height = m_LastRect.width = b_h;
		m_LastRect.x = m_CurrRect.x = b_x;
		m_LastRect.y = m_CurrRect.y = b_y;

	}
	else
	{
		m_LastRect.x = m_CurrRect.x;
		m_LastRect.y = m_CurrRect.y;
	
		m_CurrRect.x = b_x;	
		m_CurrRect.y = b_y;
	}
	
	GdkRectangle inv_new;
	inv_new.x = 0;
	inv_new.y = 0;
	inv_new.width = w;
	inv_new.height = h;
	
	//gdk_window_invalidate_rect(m_pDrawingArea->window,&inv_old,false);
	//gdk_window_invalidate_rect(m_pDrawingArea->window,&inv_old,false);
	gdk_window_invalidate_rect(m_pDrawingArea->window,&inv_new,false);

	gtk_adjustment_set_value(hadj,xval);
	gtk_adjustment_set_value(vadj,yval);
	
	return TRUE;
}

gboolean NavigationControl::EventButtonRelease ( GtkWidget *widget,GdkEventButton  *event,gpointer data )
{
	gdk_pointer_ungrab (GDK_CURRENT_TIME);

	gtk_widget_hide_all (m_pNavigationWindow);
	return TRUE;	
}


gboolean NavigationControl::event_motion_notify( GtkWidget *widget, GdkEventMotion *event, gpointer data )
{
	return ((NavigationControl*)data)->EventMotionNotify(widget,event,data);
}
gboolean NavigationControl::event_button_release ( GtkWidget *widget, GdkEventButton *event, gpointer data )
{
	return ((NavigationControl*)data)->EventButtonRelease(widget,event,data);
}

void NavigationControl::SetMaxSize(int w, int h)
{
	m_GdkGeometry.max_width = w;
	m_GdkGeometry.max_height = h;
}





