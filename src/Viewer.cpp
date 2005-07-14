#include <config.h>
#include "Viewer.h"
#include "Timer.h"
//#include "ath.h"
#include "icons/nav_button.xpm"


using namespace std;



void Viewer::event_nav_button_clicked (GtkWidget *widget, GdkEventButton *event, void *data)
{
	((Viewer*)data)->EventNavButtonClicked(widget,event,NULL);
}

void Viewer::EventNavButtonClicked (GtkWidget *widget, GdkEventButton *event, void *data)
{
	m_pNavigationControl->Show(GTK_VIEWPORT(m_pViewport),m_pixbuf,event->x_root,event->y_root);
}



Viewer::Viewer()
{
	m_pNavigationControl = new NavigationControl();
	
	
	m_bPressed = false;
	m_pixbuf = NULL;
	m_pixbuf_real = NULL;

	m_bConfigureTimeoutStarted = false;
	m_bConfigureTimeoutEnded = false;
	m_bConfigureTimeoutRestarted = false;

	m_dAdjustmentValueLastH = 0;
	m_dAdjustmentValueLastV = 0;
	
	m_iZoomType = ZOOM_FIT;
	
	//initialize the widget
	//m_pScrolledWindow = gtk_scrolled_window_new (NULL,NULL);
	m_pDrawingArea = gtk_drawing_area_new();

 	gtk_widget_add_events (m_pDrawingArea, GDK_POINTER_MOTION_MASK|GDK_POINTER_MOTION_HINT_MASK);
	gtk_widget_add_events (m_pDrawingArea, GDK_BUTTON_PRESS_MASK|GDK_BUTTON_RELEASE_MASK);

	
	m_pViewport = gtk_viewport_new(NULL,NULL);

	m_pAdjustmentH = gtk_viewport_get_hadjustment (GTK_VIEWPORT(m_pViewport));
	m_pAdjustmentV = gtk_viewport_get_vadjustment(GTK_VIEWPORT(m_pViewport));



	m_pScrollbarV = gtk_vscrollbar_new (m_pAdjustmentV);
	m_pScrollbarH = gtk_hscrollbar_new (m_pAdjustmentH);
	

	
	//cout <<	"st: " << gtk_viewport_get_shadow_type (GTK_VIEWPORT(m_pViewport)) << endl;
	// the following removes the silly line border from the viewport
	gtk_viewport_set_shadow_type(GTK_VIEWPORT(m_pViewport),GTK_SHADOW_NONE);
	
	
	gtk_widget_set_size_request(m_pViewport,20,20);
	
	/*
	m_pAdjustmentH = gtk_viewport_get_hadjustment(GTK_VIEWPORT(m_pViewport));
	m_pAdjustmentV = gtk_viewport_get_vadjustment(GTK_VIEWPORT(m_pViewport));
	*/
	gtk_container_add(GTK_CONTAINER(m_pViewport),m_pDrawingArea);


	m_pNavigationBox = gtk_event_box_new ();

	gtk_widget_set_no_show_all(m_pScrollbarV,TRUE);
	gtk_widget_set_no_show_all(m_pScrollbarH,TRUE);
	gtk_widget_set_no_show_all(m_pNavigationBox,TRUE);

	g_signal_connect (G_OBJECT (m_pNavigationBox), 
			  "button_press_event",
			  G_CALLBACK (event_nav_button_clicked), 
			  this);


	GdkPixbuf *pixbuf = gdk_pixbuf_new_from_xpm_data ((const char**) nav_button_xpm);
	GtkWidget *image = gtk_image_new_from_pixbuf (pixbuf);


	g_object_unref (G_OBJECT (pixbuf));

	
	gtk_container_add (GTK_CONTAINER (m_pNavigationBox),image);
	gtk_widget_show(image);
	

	m_pTable = gtk_table_new (2, 2, FALSE);
	

	//gtk_paned_pack1 (GTK_PANED (image_pane_paned1), table, FALSE, FALSE);

	gtk_table_attach (GTK_TABLE (m_pTable), m_pViewport, 0, 1, 0, 1,
			  (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
			  (GtkAttachOptions) (GTK_EXPAND | GTK_FILL), 0, 0);


	gtk_table_attach (GTK_TABLE (m_pTable), m_pScrollbarV, 1, 2, 0, 1,
			  (GtkAttachOptions) (GTK_FILL),
			  (GtkAttachOptions) (GTK_EXPAND | GTK_FILL), 0, 0);
	gtk_table_attach (GTK_TABLE (m_pTable), m_pScrollbarH, 0, 1, 1, 2,
			  (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
			  (GtkAttachOptions) (GTK_FILL), 0, 0);
	gtk_table_attach (GTK_TABLE (m_pTable), m_pNavigationBox, 1, 2, 1, 2,
			  (GtkAttachOptions) (0),
			  (GtkAttachOptions) (0), 0, 0);


	g_signal_connect (G_OBJECT (m_pDrawingArea), "expose_event",  
				G_CALLBACK (Viewer::event_expose), this);
	g_signal_connect (G_OBJECT(m_pDrawingArea),"configure_event",
		      G_CALLBACK (Viewer::event_configure), this);
	g_signal_connect (G_OBJECT (m_pDrawingArea), "scroll_event",
				G_CALLBACK (Viewer::event_scroll), this);	
	g_signal_connect (G_OBJECT (m_pDrawingArea), "motion_notify_event",
				G_CALLBACK (Viewer::event_motion_notify), this);
	g_signal_connect (G_OBJECT (m_pDrawingArea), "button_press_event",
				G_CALLBACK (Viewer::event_button_press), this);
	g_signal_connect (G_OBJECT (m_pDrawingArea), "button_release_event",
				G_CALLBACK (Viewer::event_button_release), this);		

	m_pointLastPointer.x = m_pointLastPointer.y=0;
	
	ScrollbarHide(VERTICAL);
	ScrollbarHide(HORIZONTAL);	
	
	//gtk_widget_show(m_pTable);
	
	//GTK_WIDGET_UNSET_FLAGS (m_pDrawingArea, GTK_DOUBLE_BUFFERED);
	
}

gboolean Viewer::EventDelete( GtkWidget *widget,GdkEvent  *event, gpointer   data )
{
	    /* If you return FALSE in the "delete_event" signal handler,
     * GTK will emit the "destroy" signal. Returning TRUE means
     * you don't want the window to be destroyed.
     * This is useful for popping up 'are you sure you want to quit?'
     * type dialogs. */

    /* Change TRUE to FALSE and the main window will be destroyed with
     * a "delete_event". */

    return FALSE;
	
}
 


gboolean Viewer::EventConfigure(GtkWidget * widget, GdkEventConfigure * event, gpointer data)
{
	GdkModifierType mask;
	int x,y;
	gdk_window_get_pointer(widget->window,&x,&y,&mask);
	
	GdkInterpType interptype = GDK_INTERP_NEAREST;
	
	
	if (GDK_BUTTON1_MASK & mask || GDK_BUTTON2_MASK & mask || !m_bConfigureTimeoutEnded)
	{
		// button one is pressed.. do a low quality (fast) resize
		if (!m_bConfigureTimeoutStarted)
		{
			m_bConfigureTimeoutStarted = true;
			// run timeout func every 10ms
			g_timeout_add(500,timeout_event_configure,this);
		}
		m_bConfigureTimeoutEnded = false;
	}
	
	if ( m_bConfigureTimeoutEnded )
	{
		m_bConfigureTimeoutEnded = false;
		interptype = GDK_INTERP_BILINEAR;
	}
	else
	{
		m_bConfigureTimeoutRestarted = true;
	}


	if (ZOOM_FIT == m_iZoomType)
	{
		int wind_w = m_pDrawingArea->allocation.width;
		int wind_h = m_pDrawingArea->allocation.height;

		if (NULL == m_pixbuf_real)
			return TRUE;

		int rw = gdk_pixbuf_get_width(m_pixbuf_real);
		int rh = gdk_pixbuf_get_height(m_pixbuf_real);

		if (NULL == m_pixbuf)
			return TRUE;
		int img_w = gdk_pixbuf_get_width(m_pixbuf);
		int img_h = gdk_pixbuf_get_height(m_pixbuf);


		
		bool bResizeNeeded = false;
		//cout << "should we resize" << endl;
		if (m_pixbuf == m_pixbuf_real)
		{
			if (wind_w < rw || wind_h < rh)
			{
				//needs resize on pixbuf_real
				// decrease
				bResizeNeeded = true;
				//printf("0x%08x Viewer::EventConfigure 0: %d\n",m_pixbuf,G_OBJECT(m_pixbuf)->ref_count);
			}

		}
		else
		{
			if (wind_w < rw || wind_h < rh)
			{
				//printf("0x%08x Viewer::EventConfigure 1: %d\n",m_pixbuf,G_OBJECT(m_pixbuf)->ref_count);
				g_object_unref(m_pixbuf);
				m_pixbuf = m_pixbuf_real;
				bResizeNeeded = true;
			}
			else
			{
				if (img_w <= rw && img_h <= rh)
				{
					//printf("0x%08x Viewer::EventConfigure 2: %d\n",m_pixbuf,G_OBJECT(m_pixbuf)->ref_count);
					g_object_unref(m_pixbuf);
					m_pixbuf = m_pixbuf_real;					
				} 
/*				if (wind_w > img_w && wind_h > img_h)
				{
					// need resize on m_pixbuf_real
					// increase size
					//printf("0x%08x Viewer::EventConfigure 3: %d\n",m_pixbuf,G_OBJECT(m_pixbuf)->ref_count);
					g_object_unref(m_pixbuf);
					m_pixbuf = m_pixbuf_real;
					bResizeNeeded = true;
				}
*/
			}
		}
		
		
		
		if (bResizeNeeded)
		{
			GdkGeometry geo;
			geo.max_width = wind_w;
			geo.max_height = wind_h;
			geo.min_aspect = geo.max_aspect = rw/(double)rh;
			int new_width,new_height;
		
			int geoflags = GDK_HINT_MAX_SIZE|GDK_HINT_ASPECT;
			gdk_window_constrain_size(&geo,geoflags,rw,rh,&new_width,&new_height);
	
			GdkPixbuf * tmp = gdk_pixbuf_scale_simple (m_pixbuf, new_width, new_height, interptype);
			//printf("0x%08x Viewer::EventConfigure 3: %d\n",m_pixbuf,G_OBJECT(m_pixbuf)->ref_count);
			m_pixbuf = tmp;
			//printf("0x%08x Viewer::EventConfigure 4: %d\n",m_pixbuf,G_OBJECT(m_pixbuf)->ref_count);
		}
	}
	else
	{
		UpdateSize();
	}
  	return TRUE;
}
 
gboolean Viewer::EventExpose(GtkWidget *widget, GdkEventExpose *event, gpointer data)
{
	//cout << event->area.x << "," << event->area.y << "," << event->area.width << "," << event->area.height << endl;
		
	if (NULL != m_pixbuf)
	{
		int w,h;

		w = gdk_pixbuf_get_width(m_pixbuf);
		h = gdk_pixbuf_get_height(m_pixbuf);
	   	GtkWidget* viewport = m_pDrawingArea;//gtk_bin_get_child(GTK_BIN(m_pScrolledWindow));
		// center the image
		int x_off=0, y_off = 0;
		if (w < viewport->allocation.width)
		{
			x_off = (viewport->allocation.width - w) / 2;
	 		gdk_draw_rectangle (widget->window,
			      widget->style->black_gc,
			      TRUE,
			      0, 0,
			      x_off+1,
			      widget->allocation.height);
	 		gdk_draw_rectangle (widget->window,
			      widget->style->black_gc,
			      TRUE,
			      w+x_off, 0,
			      x_off+1,
			      widget->allocation.height);				
		}

		if (h < viewport->allocation.height)
		{
			y_off = (viewport->allocation.height - h) / 2;
	 		gdk_draw_rectangle (widget->window,
			      widget->style->black_gc,
			      TRUE,
			      x_off, 0,
			      w,
			      y_off+1);

	 		gdk_draw_rectangle (widget->window,
			      widget->style->black_gc,
			      TRUE,
			      x_off, h+y_off,
			      w,
			      y_off+1);			
		}

		/*
		x_off -= m_pAdjustmentH->value;
		y_off -= m_pAdjustmentV->value;
		*/
		
		GdkRectangle image_area;
		GdkRectangle paint_area;
		
		image_area.x = x_off;
		image_area.y = y_off;
		image_area.width = w;
		image_area.height = h;
		
		

		if (gdk_rectangle_intersect (&event->area, &image_area, &paint_area))
		{

			gdk_draw_pixbuf	(widget->window,
	                     m_pDrawingArea->style->fg_gc[GTK_WIDGET_STATE (m_pDrawingArea)],
	                     m_pixbuf,paint_area.x-x_off,paint_area.y-y_off,paint_area.x,paint_area.y,paint_area.width,paint_area.height,
	                     GDK_RGB_DITHER_NONE,0,0);

		}
	}
	else
	{
 		gdk_draw_rectangle (widget->window,
		      widget->style->black_gc,
		      TRUE,
		      0, 0,
		      widget->allocation.width,
		      widget->allocation.height);			
	}
	return FALSE;
}

gboolean Viewer::EventMotionNotify( GtkWidget *widget, GdkEventMotion *event, gpointer data )
{
	int x=0, y=0;
	
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
	
	
	x -= (int)m_pAdjustmentH->value;
	y -= (int)m_pAdjustmentV->value;
	
	if (state & GDK_BUTTON1_MASK)
	{
		if (!m_bPressed)
		{
			m_bPressed = true;
			m_pointLastPointer.x = x;
			m_pointLastPointer.y = y;
		}
		gdouble value;

		value = gtk_adjustment_get_value(m_pAdjustmentH);
		value += m_pointLastPointer.x - x;
		
		if (value < m_pAdjustmentH->lower)
			value = m_pAdjustmentH->lower;
		if (value > m_pAdjustmentH->upper - m_pAdjustmentH->page_size)
			value = m_pAdjustmentH->upper - m_pAdjustmentH->page_size;

		gtk_adjustment_set_value(m_pAdjustmentH,value);

		value = gtk_adjustment_get_value(m_pAdjustmentV);
		value += m_pointLastPointer.y - y;
		
		if (value < m_pAdjustmentV->lower)
			value = m_pAdjustmentV->lower;
		if (value > m_pAdjustmentV->upper - m_pAdjustmentV->page_size)
			value = m_pAdjustmentV->upper  - m_pAdjustmentV->page_size;
		
		gtk_adjustment_set_value(m_pAdjustmentV,value);

/*		gdk_draw_line (m_pDrawingArea->window,
               m_pDrawingArea->style->white_gc,
               m_pointLastPointer.x, m_pointLastPointer.y, x, y);
*/
        m_pointLastPointer.x = x;
        m_pointLastPointer.y = y;

	}
	return FALSE;
}

gboolean Viewer::EventEvent( GtkWidget *widget,GdkEvent  *event,gpointer data )
{
	//	printf("got an event MAN%d\n", event->type);
	return TRUE;
	
}

gboolean Viewer::EventButtonPress( GtkWidget *widget,GdkEventButton  *event,gpointer data )
{
	if (1 == event->button)
	{
		int x = (int)event->x;
		int y = (int)event->y;
		
		m_pointLastPointer.x = (int)( x - m_pAdjustmentH->value);
		m_pointLastPointer.y =(int)( y - m_pAdjustmentV->value);
		
		m_bPressed = true;	
	}

	return FALSE;
}

gboolean Viewer::EventButtonRelease ( GtkWidget *widget,GdkEventButton  *event,gpointer data )
{
	if (1 == event->button)
	{
		m_bPressed = false;
	}
	return FALSE;
	
}

gboolean Viewer::EventScroll( GtkWidget *widget, GdkEventScroll *event, gpointer data )
{
	g_signal_stop_emission_by_name(widget,"scroll_event");
	return FALSE;	
}

gboolean Viewer::event_scroll( GtkWidget *widget, GdkEventScroll *event, gpointer data )
{
	return ((Viewer*)data)->EventScroll(widget,event,NULL);
}

gboolean Viewer::event_motion_notify( GtkWidget *widget, GdkEventMotion *event, gpointer data )
{
	return ((Viewer*)data)->EventMotionNotify(widget,event,NULL);
}
gboolean Viewer::event_expose (GtkWidget *widget, GdkEventExpose *event, gpointer data)
{
	return ((Viewer*)data)->EventExpose(widget,event,NULL);
}
gboolean Viewer::event_configure( GtkWidget *widget, GdkEventConfigure *event, gpointer data )
{
	return ((Viewer*)data)->EventConfigure(widget,event,NULL);
}

gboolean Viewer::event_event( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	return ((Viewer*)data)->EventEvent(widget,event,NULL);
}

gboolean Viewer::event_button_press ( GtkWidget *widget, GdkEventButton *event, gpointer data )
{
		return ((Viewer*)data)->EventButtonPress(widget,event,NULL);
}

gboolean  Viewer::event_button_release ( GtkWidget *widget, GdkEventButton *event, gpointer data )
{
	return ((Viewer*)data)->EventButtonRelease(widget,event,NULL);
}


void Viewer::SignalAreaPrepared(GdkPixbufLoader *loader)
{
	gdk_threads_enter ();

	//Timer t("Viewer::SignalAreaPrepared()");
/*
 	GdkCursor * cursor = gdk_cursor_new_for_display(gtk_widget_get_display(m_pDrawingArea),GDK_WATCH);
	gdk_window_set_cursor(m_pDrawingArea->window, cursor);
	g_object_unref(cursor);
*/	
//	cout << "viewer area prepared" << endl;


	GdkPixbuf * pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);
	g_object_ref(pixbuf);
	//printf("0x%08x Viewer::SignalAreaPrepared 1 (after ref): %d\n",pixbuf,G_OBJECT(pixbuf)->ref_count);

	int w_old,h_old;
	if (NULL != m_pixbuf)
	{		
	  	w_old = gdk_pixbuf_get_width(m_pixbuf);
		h_old = gdk_pixbuf_get_height(m_pixbuf);
	}
	else
	{
		w_old = 0;
		h_old = 0;
	}

  	int w = gdk_pixbuf_get_width(pixbuf);
	int h = gdk_pixbuf_get_height(pixbuf);	

	GdkRectangle screen;		
	screen.x = 0;
	screen.y = 0;
	screen.width = m_pTable->allocation.width;
	screen.height = m_pTable->allocation.height;

	switch (m_iZoomType)
	{
		case ZOOM_FIT:
			if (w > screen.width || h > screen.height)
			{
				bool bDiff = false;
				GdkPixbuf * tmp = m_pixbuf;
				if (m_pixbuf_real != m_pixbuf && NULL != m_pixbuf_real)
				{
					//printf("0x%08x Viewer::SignalAreaPrepared 2: %d\n",m_pixbuf_real,G_OBJECT(m_pixbuf_real)->ref_count);
					g_object_unref(m_pixbuf_real);
					bDiff = true;
				}
				m_pixbuf_real = pixbuf;
				
				double image_ratio = (double)w/(double)h;
				//double window_ratio = (double)w/(double)h;
			
				int new_width;
				int new_height;
			
						
				new_width = screen.width;
				new_height =(int) ( screen.width / image_ratio);
				if (new_width > screen.width || new_height > screen.height)
				{
					new_width = (int) (screen.height * image_ratio);
					new_height = screen.height;
				}
				
				m_pixbuf = gdk_pixbuf_new(gdk_pixbuf_get_colorspace(m_pixbuf_real),
									gdk_pixbuf_get_has_alpha(m_pixbuf_real),
                                    gdk_pixbuf_get_bits_per_sample ( m_pixbuf_real),
                                    new_width,
                                    new_height);
				if (NULL != tmp && bDiff)
				{
					//printf("0x%08x Viewer::SignalAreaPrepared 1: %d\n",tmp,G_OBJECT(tmp)->ref_count);
					g_object_unref(tmp);
				}
				else
				{
					cout << " not different!" << endl;				
				}
				//gtk_widget_set_size_request (m_pDrawingArea,new_width,new_height);
			 	break;  	
			}
			//fall into ZOOM_NONE CASE
		case ZOOM_NONE:

			GdkPixbuf * tmp = m_pixbuf;
			m_pixbuf = m_pixbuf_real = pixbuf;
			if (NULL != tmp)
			{
				//printf("0x%08x Viewer::SignalAreaPrepared 1: %d\n",tmp,G_OBJECT(tmp)->ref_count);
				g_object_unref(tmp);
			}
			break;
	}
	UpdateSize();
	
	// invalidate the old non-image area
				
		// we dont need to redraw border
		// in the last image that is still border in this image
		
       	GtkWidget* viewport = m_pViewport;

		

		GdkRectangle old_rect;
		GdkRectangle new_rect;

/*
		GdkRectangle screen;		
		screen.x = 0;
		screen.y = 0;
		screen.width = m_pDrawingArea->allocation.width;
		screen.height = m_pDrawingArea->allocation.height;*/

		new_rect.x = (viewport->allocation.width - w) / 2;
		new_rect.y = (viewport->allocation.height - h) / 2;;
		new_rect.width = w;
		new_rect.height = h;

		old_rect.x = (viewport->allocation.width - w_old) / 2;
		old_rect.y = (viewport->allocation.height - h_old) / 2;
		old_rect.width = w_old;
		old_rect.height = h_old;

		if (new_rect.x > old_rect.x || new_rect.y > old_rect.y)
		{
			GdkRegion * old_reg  = gdk_region_new();
			GdkRegion * new_reg  = gdk_region_new();
			GdkRegion * old_rect_reg = gdk_region_rectangle(&old_rect);
			GdkRegion * new_rect_reg = gdk_region_rectangle(&new_rect);
			
			gdk_region_union_with_rect(old_reg,&screen);
			gdk_region_subtract(old_reg,old_rect_reg);
			
			gdk_region_union_with_rect(new_reg,&screen);
			gdk_region_subtract(new_reg,new_rect_reg);
			
			gdk_region_subtract(new_reg,old_reg);

			gdk_window_invalidate_region(m_pDrawingArea->window,new_reg,false);
			
			gdk_region_destroy(new_reg);
			gdk_region_destroy(old_reg);
			gdk_region_destroy(old_rect_reg);
			gdk_region_destroy(new_rect_reg);
			
		}



	//gdk_flush();
	//#include <gdk/gdkx.h>
	//GdkDisplay *display = ;
	XFlush ( GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()) );

	gdk_threads_leave ();
}

/**
 * zoom stuff from gthumb
 * 
	double     x_level, y_level;
	double     new_zoom_level;		
	int        gdk_width, gdk_height;

	buf = image_viewer_get_current_pixbuf (viewer);

	gdk_width = GTK_WIDGET (viewer)->allocation.width - viewer->frame_border2;
	gdk_height = GTK_WIDGET (viewer)->allocation.height - viewer->frame_border2;
	x_level = (double) gdk_width / gdk_pixbuf_get_width (buf);
	y_level = (double) gdk_height / gdk_pixbuf_get_height (buf);

	new_zoom_level = (x_level < y_level) ? x_level : y_level;
	if (new_zoom_level > 0.0) {
		viewer->doing_zoom_fit = TRUE;
		image_viewer_set_zoom (viewer, new_zoom_level);
		viewer->doing_zoom_fit = FALSE;
	}
	
*/
	


void Viewer::SignalAreaUpdated(GdkPixbufLoader *loader,gint x, gint y, gint width,gint height)
{

	gdk_threads_enter ();
	//cout << x << "," << y << "," << width << "," << height << endl;
	//Timer t("Viewer::SignalAreaUpdated()");
		
  	int w = gdk_pixbuf_get_width(m_pixbuf);
	int h = gdk_pixbuf_get_height(m_pixbuf);

   	GtkWidget* viewport = m_pDrawingArea;//gtk_bin_get_child(GTK_BIN(m_pScrolledWindow));

	// center the image
	int x_off=0, y_off = 0;
	if (w < viewport->allocation.width)
	{
		x_off = (viewport->allocation.width - w) / 2;
	}
	if (h < viewport->allocation.height)
	{
		y_off = (viewport->allocation.height - h) / 2;
	}



   	GdkRectangle r;
	
	r.x = x_off + x;
	r.y= y_off + y;
	r.width = width;
	r.height = height;

	gdk_window_invalidate_rect(m_pDrawingArea->window,&r,false);
	
	//gdk_flush();
	XFlush ( GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()) );
	
	gdk_threads_leave ();	
}

void Viewer::SignalClosed(GdkPixbufLoader *loader)
{

}

void Viewer::SignalSizePrepared(GdkPixbufLoader *loader,gint width, gint height)
{
	gdk_threads_enter ();
	//cout << "got " << width << " " << height;

	if (ZOOM_FIT == m_iZoomType)
	{
		GdkGeometry geo;
		geo.max_width = m_pDrawingArea->allocation.width;
		geo.max_height = m_pDrawingArea->allocation.height;

		if (geo.max_width < width || geo.max_height < height)
		{
			geo.min_aspect = geo.max_aspect = width/(double)height;

			int new_width,new_height;
			int geoflags = GDK_HINT_MAX_SIZE|GDK_HINT_ASPECT;
			gdk_window_constrain_size(&geo,geoflags,width,height,&new_width,&new_height);

			//cout << "Viewer::SignalSizePrepared -> setting " << new_width << " " << new_height << endl;

			gdk_pixbuf_loader_set_size(loader,new_width,new_height);

		}

	}
	//gdk_flush();
	//XFlush ( GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()) );
	
	gdk_threads_leave ();
}

Viewer::ZoomType Viewer::GetZoomType()
{
	return m_iZoomType;
}

void Viewer::SetZoomType(ZoomType type)
{
	m_iZoomType = type;
	
	switch (m_iZoomType)
	{
		case ZOOM_FIT:
			ScrollbarHide(BOTH);
			gtk_widget_set_size_request (m_pDrawingArea,1,1);
			break;
		case ZOOM_NONE:
			if (m_pixbuf != m_pixbuf_real)
			{
				//printf("0x%08x Viewer::SetZoomType : %d\n",m_pixbuf,G_OBJECT(m_pixbuf)->ref_count);
				g_object_unref(m_pixbuf);
				m_pixbuf = m_pixbuf_real;
			}
			UpdateSize();
			break;
	}
}

GtkTableChild * GetGtkTableChild(GtkTable * table,GtkWidget	*widget_to_get);

void Viewer::ScrollbarShow(ScrollbarType t)
{
	GtkTableChild * child = GetGtkTableChild(GTK_TABLE(m_pTable),m_pDrawingArea);
	switch (t)
	{
		case VERTICAL:
			
			if (!GTK_WIDGET_VISIBLE(m_pScrollbarH))
			{
				child->bottom_attach = 2;
				gtk_widget_show (m_pNavigationBox);	
			}
			else
			{
				child->right_attach = 1;
			}
			gtk_widget_show (m_pScrollbarV);
			
			break;
			
		case HORIZONTAL:
			
			if (!GTK_WIDGET_VISIBLE(m_pScrollbarV))
			{
				child->right_attach = 2;
				gtk_widget_show (m_pNavigationBox);	
				
			}
			else
			{

				child->bottom_attach = 1;
			}
			
			gtk_widget_show (m_pScrollbarH);
			
			break;
		case BOTH:
			gtk_widget_show (m_pScrollbarV); 	
			gtk_widget_show (m_pScrollbarH);
			gtk_widget_show (m_pNavigationBox);
			child->bottom_attach = 1;
			child->right_attach = 1;
			
			break;
	}
}

void Viewer::ScrollbarHide(ScrollbarType t)
{

	GtkTableChild * child = GetGtkTableChild(GTK_TABLE(m_pTable),m_pDrawingArea);
	switch (t)
	{
		case VERTICAL:
			if (!GTK_WIDGET_VISIBLE(m_pScrollbarH))
			{
				child->bottom_attach = 1;
				gtk_widget_hide (m_pNavigationBox);					
			}
			else
			{
				child->right_attach = 2;
			}
			gtk_widget_hide (m_pScrollbarV);
			
			break;
			
		case HORIZONTAL:
			
			if (!GTK_WIDGET_VISIBLE(m_pScrollbarV))
			{
				child->right_attach = 1;
				gtk_widget_hide (m_pNavigationBox);				
			}
			else
			{
				child->bottom_attach = 2;
			}
			
			gtk_widget_hide (m_pScrollbarH);
			
			break;
		case BOTH:
			gtk_widget_hide (m_pScrollbarV); 	
			gtk_widget_hide (m_pScrollbarH);
			gtk_widget_hide (m_pNavigationBox);

			child->bottom_attach = 1;
			child->right_attach = 1;
			
			break;
	}
}

void Viewer::UpdateSize()
{
	if (NULL != m_pixbuf)
	{
		int w,h,gdk_width,gdk_height;
		
		
		w = gdk_pixbuf_get_width(m_pixbuf);
		h = gdk_pixbuf_get_height(m_pixbuf);
		
		if (ZOOM_FIT != m_iZoomType)
		{
			gtk_widget_set_size_request (m_pDrawingArea,w,h);
		}
		else
		{
			gtk_widget_set_size_request (m_pDrawingArea,1,1);
		}	
		gdk_width = m_pDrawingArea->allocation.width;
		gdk_height = m_pDrawingArea->allocation.height;
		
		int width = m_pTable->allocation.width;
		int height = m_pTable->allocation.height;
		int sb_height = m_pScrollbarH->allocation.height;
		int sb_width = m_pScrollbarV->allocation.width;
		
		if (w > width)
		{
			ScrollbarShow(HORIZONTAL);
			height -= sb_height;
		}
		else
		{
			ScrollbarHide(HORIZONTAL);
		}
		

		
		if (h > height)
		{
			ScrollbarShow(VERTICAL);
			if (w > width - sb_width)
			{
				ScrollbarShow(HORIZONTAL);	
			}
			
		}
		else
		{
			ScrollbarHide(VERTICAL);
		}
	}

}

void Viewer::SetPixbuf(GdkPixbuf * pixbuf)
{

	gdk_threads_enter ();

	GObject * obj = G_OBJECT(pixbuf);
	//printf("0x%08x Viewer::SetPixbuf 0: %d\n",pixbuf,G_OBJECT(pixbuf)->ref_count);
	
	g_object_ref(pixbuf);
	
	if (m_pixbuf != m_pixbuf_real)
	{
		if (NULL != m_pixbuf_real)
		{
			g_object_unref(m_pixbuf_real);
		}
	}

	if (NULL != m_pixbuf)
	{
		//printf("0x%08x Viewer::SetPixbuf 2: %d\n",m_pixbuf,G_OBJECT(m_pixbuf)->ref_count);
		g_object_unref(m_pixbuf);
	}
	
	m_pixbuf_real = m_pixbuf = pixbuf;
	
	//printf("0x%08x Viewer::SetPixbuf 0 (after ref): %d\n",pixbuf,G_OBJECT(pixbuf)->ref_count);
//	cout << "setting pixbuf......." << endl;

	GdkEvent * e = gdk_event_new(GDK_CONFIGURE);
	gboolean rval = FALSE;
	//cout << " emitting event " << endl;
	g_signal_emit_by_name(m_pDrawingArea,"configure_event",e,&rval);
	
	GdkRectangle r;
	r.x = r.y = 0;
	r.width = m_pDrawingArea->allocation.width;
	r.height = m_pDrawingArea->allocation.height;
	gdk_window_invalidate_rect(m_pDrawingArea->window,&r,false);


	gdk_threads_leave ();
}


GtkTableChild * GetGtkTableChild(GtkTable * table,GtkWidget	*widget_to_get)
{
	GtkTableChild *table_child = NULL;
	GList *list;
	for (list = table->children; list; list = list->next)
	{
		table_child = (GtkTableChild*)list->data;
		if (table_child->widget == widget_to_get)
			break;
	}
	return table_child;
}

gboolean Viewer::IdleEventConfigure(gpointer data)
{
	return FALSE;
}

gboolean Viewer::TimeoutEventConfigure(gpointer data)
{
	GdkModifierType mask;
	int x,y;
	
	gboolean retval = FALSE;
	
	
	gdk_window_get_pointer(m_pDrawingArea->window,&x,&y,&mask);
	
	if (GDK_BUTTON1_MASK & mask || GDK_BUTTON2_MASK & mask || m_bConfigureTimeoutRestarted)
	{
		m_bConfigureTimeoutRestarted = false;
		retval = TRUE;
	}
	else
	{
		//cout << "button not pressed anymore " << endl;

		m_bConfigureTimeoutStarted = false;
		m_bConfigureTimeoutEnded = true;

		GdkEvent * e = gdk_event_new(GDK_CONFIGURE);
		gboolean rval = FALSE;
		//cout << " emitting event " << endl;
		g_signal_emit_by_name(m_pDrawingArea,"configure_event",e,&rval);
		
		GdkRectangle r;
		r.x = r.y = 0;
		r.width = m_pDrawingArea->allocation.width;
		r.height = m_pDrawingArea->allocation.height;
		gdk_window_invalidate_rect(m_pDrawingArea->window,&r,false);

		gdk_event_free(e);
	}
	
	return retval;
}

gboolean Viewer::timeout_event_configure (gpointer data)
{
	return ((Viewer*)data)->TimeoutEventConfigure(data);
}

gboolean Viewer::idle_event_configure (gpointer data)
{
	return ((Viewer*)data)->IdleEventConfigure(data);
}

