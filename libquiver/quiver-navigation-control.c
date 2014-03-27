#include <config.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include "quiver-navigation-control.h"
#include "quiver-marshallers.h"
#include <math.h>
#include <sys/time.h>
#include "quiver-pixbuf-utils.h"

//#include "gtkintl.h"


#define QUIVER_NAVIGATION_CONTROL_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), QUIVER_TYPE_NAVIGATION_CONTROL, QuiverNavigationControlPrivate))

/* set up some defaults */

#define QUIVER_PARAM_READWRITE G_PARAM_READWRITE|G_PARAM_STATIC_NAME|G_PARAM_STATIC_NICK|G_PARAM_STATIC_BLURB

G_DEFINE_TYPE_WITH_CODE(QuiverNavigationControl,quiver_navigation_control,GTK_TYPE_WIDGET, G_IMPLEMENT_INTERFACE(GTK_TYPE_SCROLLABLE, NULL));

/* signals */
enum {
	SIGNAL_COUNT
};

/* properties */
enum {
   PROP_0,
   PROP_HADJUSTMENT,
   PROP_VADJUSTMENT,
   PROP_HSCROLL_POLICY,
   PROP_VSCROLL_POLICY,
};


struct _QuiverNavigationControlPrivate
{
	GtkAdjustment *hadjustment;
	GtkAdjustment *vadjustment;
	guint hscroll_policy : 1;
	guint vscroll_policy : 1;

	GdkPixbuf *pixbuf;

	cairo_rectangle_int_t view_area_rect;

};
/* end private data structures */


/* start private function prototypes */

static void      quiver_navigation_control_realize        (GtkWidget *widget);

static void      quiver_navigation_control_size_allocate  (GtkWidget     *widget,
                                             GtkAllocation *allocation);

static void      quiver_navigation_control_size_request (GtkWidget *widget, GtkRequisition *requisition);
static void      quiver_navigation_control_get_preferred_width (GtkWidget *widget, gint* min_width, gint* natural_width);
static void      quiver_navigation_control_get_preferred_height (GtkWidget *widget, gint* min_height, gint* natural_height);


static void      quiver_navigation_control_send_configure (QuiverNavigationControl *navcontrol);

static gboolean  quiver_navigation_control_configure_event (GtkWidget *widget,
                    GdkEventConfigure *event);

/*
static gboolean  quiver_navigation_control_expose_event (GtkWidget *navcontrol,
                    GdkEventExpose *event);
*/
static gboolean  quiver_navigation_control_draw(GtkWidget* navcontrol, cairo_t* cr);

static gboolean  quiver_navigation_control_button_press_event  (GtkWidget *widget,
                    GdkEventButton *event);

static gboolean  quiver_navigation_control_button_release_event (GtkWidget *widget,
                    GdkEventButton *event);
static gboolean  quiver_navigation_control_motion_notify_event (GtkWidget *widget,
                    GdkEventMotion *event);

static void     quiver_navigation_control_finalize(GObject *object);


static void      quiver_navigation_control_set_property (GObject *object,
                    guint               prop_id,
                    const GValue       *value,
                    GParamSpec         *pspec);
static void      quiver_navigation_control_get_property (GObject    *object,
                    guint       prop_id,
                    GValue     *value,
                    GParamSpec *pspec);


/* start utility function prototypes*/
static void      quiver_navigation_control_set_hadjustment 
                     (QuiverNavigationControl *navcontrol, GtkAdjustment *hadjustment);
static void      quiver_navigation_control_set_vadjustment
                     (QuiverNavigationControl *navcontrol, GtkAdjustment *vadjustment);

static void      quiver_navigation_control_adjustment_changed (GtkAdjustment *adjustment,
                    gpointer userdata);
/* end utility function prototypes*/

/* end private function prototypes */

/* start private globals */

static guint navcontrol_signals[SIGNAL_COUNT] = {0};

/* end private globals */


/* start private functions */
static void 
quiver_navigation_control_class_init (QuiverNavigationControlClass *klass)
{
	GtkWidgetClass *widget_class;
	GObjectClass *obj_class;

	widget_class = GTK_WIDGET_CLASS (klass);
	obj_class = G_OBJECT_CLASS (klass);

	widget_class->realize              = quiver_navigation_control_realize;
	widget_class->size_allocate        = quiver_navigation_control_size_allocate;
	widget_class->get_preferred_width  = quiver_navigation_control_get_preferred_width;
	widget_class->get_preferred_height = quiver_navigation_control_get_preferred_height;
	widget_class->draw                 = quiver_navigation_control_draw;
	widget_class->configure_event      = quiver_navigation_control_configure_event;
	widget_class->button_press_event   = quiver_navigation_control_button_press_event;
	
	obj_class->set_property            = quiver_navigation_control_set_property;
	obj_class->get_property            = quiver_navigation_control_get_property;

		/* Override properties */
	g_object_class_override_property (obj_class, PROP_HADJUSTMENT, "hadjustment");
	g_object_class_override_property (obj_class, PROP_VADJUSTMENT, "vadjustment");
	g_object_class_override_property (obj_class, PROP_HSCROLL_POLICY, "hscroll-policy");
	g_object_class_override_property (obj_class, PROP_VSCROLL_POLICY, "vscroll-policy");

	widget_class->button_release_event = quiver_navigation_control_button_release_event;
	widget_class->motion_notify_event  = quiver_navigation_control_motion_notify_event;

	obj_class->finalize                = quiver_navigation_control_finalize;

#define GTK_PARAM_READWRITE G_PARAM_READWRITE|G_PARAM_STATIC_NAME|G_PARAM_STATIC_NICK|G_PARAM_STATIC_BLURB

	g_object_class_install_property (obj_class,
		PROP_HADJUSTMENT,
		g_param_spec_object ("hadjustment",
		/*P_*/("Horizontal Adjustment"),
		/*P_*/("The Horizontal GtkAdjustment connected to the Navigation Control"),
		GTK_TYPE_ADJUSTMENT,
		GTK_PARAM_READWRITE));
	
	g_object_class_install_property (obj_class,
		PROP_VADJUSTMENT,
		g_param_spec_object ("vadjustment",
		/*P_*/("Vertical Adjustment"),
		/*P_*/("The Vertical GtkAdjustment connected to the Navigation Control"),
		GTK_TYPE_ADJUSTMENT,
		GTK_PARAM_READWRITE));

	g_type_class_add_private (obj_class, sizeof (QuiverNavigationControlPrivate));
}

static void 
quiver_navigation_control_init(QuiverNavigationControl *navcontrol)
{
	navcontrol->priv = QUIVER_NAVIGATION_CONTROL_GET_PRIVATE(navcontrol);

	navcontrol->priv->hadjustment = NULL;
	navcontrol->priv->vadjustment = NULL;
	
	navcontrol->priv->hscroll_policy = 0;
	navcontrol->priv->vscroll_policy = 0;

	navcontrol->priv->pixbuf = NULL;

	navcontrol->priv->view_area_rect.x = -1;

	gtk_widget_set_can_focus(navcontrol,TRUE);
}



static void
quiver_navigation_control_finalize(GObject *object)
{
	GObjectClass *parent,*obj_class;
	QuiverNavigationControlClass *klass; 
	QuiverNavigationControl *navcontrol;

	navcontrol = QUIVER_NAVIGATION_CONTROL(object);
	klass = QUIVER_NAVIGATION_CONTROL_GET_CLASS(navcontrol);
	obj_class = G_OBJECT_CLASS (klass);
	
	if (NULL != navcontrol->priv->hadjustment)
	{
		g_signal_handlers_disconnect_by_func (navcontrol->priv->hadjustment,
			quiver_navigation_control_adjustment_changed,
			navcontrol);
			
		g_object_unref(navcontrol->priv->hadjustment);
		navcontrol->priv->hadjustment = NULL;
	}

	if (NULL != navcontrol->priv->vadjustment)
	{
		g_signal_handlers_disconnect_by_func (navcontrol->priv->vadjustment,
			quiver_navigation_control_adjustment_changed,
			navcontrol);
			
		g_object_unref(navcontrol->priv->vadjustment);
		navcontrol->priv->vadjustment = NULL;
	}

		
	parent = g_type_class_peek_parent(klass);
	if (parent)
	{
		parent->finalize(object);
	}
}

static void
quiver_navigation_control_realize (GtkWidget *widget)
{
	QuiverNavigationControl *navcontrol;
	GdkWindowAttr attributes;
	gint attributes_mask;
	GtkAllocation allocation = {0};

	g_return_if_fail (QUIVER_IS_NAVIGATION_CONTROL (widget));

	navcontrol = QUIVER_NAVIGATION_CONTROL (widget);
	gtk_widget_set_realized (widget, TRUE);

	attributes.window_type = GDK_WINDOW_CHILD;
	gtk_widget_get_allocation(widget, &allocation);
	attributes.x = allocation.x;
	attributes.y = allocation.y;
	attributes.width = gtk_widget_get_allocated_width(widget);
	attributes.height = gtk_widget_get_allocated_height(widget);
	attributes.wclass = GDK_INPUT_OUTPUT;
	attributes.visual = gtk_widget_get_visual (widget);
	//FIXME: color map
	//attributes.colormap = gtk_widget_get_colormap (widget);
	//attributes.event_mask = GDK_VISIBILITY_NOTIFY_MASK;
	attributes.event_mask = gtk_widget_get_events (widget) | 
					   GDK_EXPOSURE_MASK |
					   GDK_KEY_PRESS_MASK | GDK_KEY_RELEASE_MASK |
					   GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
					   GDK_POINTER_MOTION_MASK | GDK_POINTER_MOTION_HINT_MASK |
					   GDK_LEAVE_NOTIFY_MASK | GDK_SCROLL_MASK;

	attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL;//FIXME: | GDK_WA_COLORMAP;

	gtk_widget_set_window(widget, gdk_window_new (gtk_widget_get_parent_window (widget), &attributes, attributes_mask));
	gdk_window_set_user_data (gtk_widget_get_window(widget), navcontrol);

	gtk_widget_set_style(widget, gtk_style_attach(gtk_widget_get_style(widget), gtk_widget_get_window(widget)));
	gtk_style_set_background (gtk_widget_get_style(widget), gtk_widget_get_window(widget), GTK_STATE_NORMAL);

	quiver_navigation_control_send_configure (QUIVER_NAVIGATION_CONTROL (widget));
}

/*
static void
quiver_navigation_control_unrealize(GtkWidget *widget)
{

}
*/

static void
quiver_navigation_control_size_allocate (GtkWidget     *widget,
				GtkAllocation *allocation)
{
	g_return_if_fail (QUIVER_IS_NAVIGATION_CONTROL (widget));
	g_return_if_fail (allocation != NULL);

	gtk_widget_set_allocation(widget, allocation);

	if (gtk_widget_get_realized (widget))
	{
		gdk_window_move_resize (gtk_widget_get_window(widget),
			allocation->x, allocation->y,
			allocation->width, allocation->height);
	
		quiver_navigation_control_send_configure (QUIVER_NAVIGATION_CONTROL (widget));

	}


}

static void
quiver_navigation_control_size_request (GtkWidget *widget, GtkRequisition *requisition)
{
	QuiverNavigationControl *navcontrol;
	navcontrol = QUIVER_NAVIGATION_CONTROL(widget);
}

static void 
quiver_navigation_control_get_preferred_width (GtkWidget *widget, gint* min_width, gint* natural_width)
{
	GtkRequisition requisition = {0};
	quiver_navigation_control_size_request(widget, &requisition);
	*min_width = *natural_width = requisition.width;
}

static void
quiver_navigation_control_get_preferred_height (GtkWidget *widget, gint* min_height, gint* natural_height)
{
	GtkRequisition requisition = {0};
	quiver_navigation_control_size_request(widget, &requisition);
	*min_height = *natural_height = requisition.height;
}

static void
quiver_navigation_control_send_configure (QuiverNavigationControl *navcontrol)
{
	GtkWidget *widget;
	GdkEvent *event = gdk_event_new (GDK_CONFIGURE);
	GtkAllocation allocation = {0};

	widget = GTK_WIDGET (navcontrol);

	event->configure.window = g_object_ref (gtk_widget_get_window(widget));
	event->configure.send_event = TRUE;
	gtk_widget_get_allocation(widget, &allocation);
	event->configure.x = allocation.x;
	event->configure.y = allocation.y;
	event->configure.width = gtk_widget_get_allocated_width(widget);
	event->configure.height = gtk_widget_get_allocated_height(widget);

	gtk_widget_event (widget, event);
	gdk_event_free (event);
}

static gboolean
quiver_navigation_control_configure_event( GtkWidget *widget, GdkEventConfigure *event )
{
	QuiverNavigationControl *navcontrol;
	navcontrol = QUIVER_NAVIGATION_CONTROL(widget);
	
	if (NULL != navcontrol->priv->hadjustment && NULL != navcontrol->priv->vadjustment)
	{
		quiver_navigation_control_adjustment_changed(navcontrol->priv->hadjustment, (gpointer)navcontrol);
	}

	return TRUE;
}

// FIXME: 
/*
static gboolean
quiver_navigation_control_expose_event (GtkWidget *widget, GdkEventExpose *event)
{
	QuiverNavigationControl *navcontrol;
	navcontrol = QUIVER_NAVIGATION_CONTROL(widget);

	int i;
	int n_rectangles =0;
	GdkRectangle *rects = NULL;

	gdk_region_get_rectangles(event->region, &rects, &n_rectangles);

	for (i = 0; i < n_rectangles; i++)
	{
		gdk_draw_rectangle(
			gtk_widget_get_window(widget),
			widget->style->bg_gc[GTK_WIDGET_STATE (navcontrol)],
			TRUE,
			rects[i].x,
			rects[i].y,
			rects[i].width,
			rects[i].height
			);
			
		if (NULL != navcontrol->priv->pixbuf)
		{
			gint w,h;
			w  = gdk_pixbuf_get_width(navcontrol->priv->pixbuf);
			h  = gdk_pixbuf_get_height(navcontrol->priv->pixbuf);

			GdkRectangle pixbuf_rect = {0};
			pixbuf_rect.x = 0;
			pixbuf_rect.y = 0;
			pixbuf_rect.width = w;
			pixbuf_rect.height = h;
			
			GdkRectangle intersect_rect = {0};
			gdk_rectangle_intersect(&rects[i],&pixbuf_rect, &intersect_rect);
			
			gdk_draw_pixbuf	(gtk_widget_get_window(widget),
				widget->style->fg_gc[GTK_WIDGET_STATE (navcontrol)],
				navcontrol->priv->pixbuf,
				intersect_rect.x,
				intersect_rect.y,
				intersect_rect.x,
				intersect_rect.y,
				intersect_rect.width,
				intersect_rect.height,
				GDK_RGB_DITHER_NONE,0,0);
			
		}
	}
	g_free(rects);

	if (-1 != navcontrol->priv->view_area_rect.x)
	{
#define LINE_WIDTH 2
		GdkGC *area_line_gc = gdk_gc_new (gtk_widget_get_window(widget));
	
		gdk_gc_set_function (area_line_gc, GDK_INVERT);
		gdk_gc_set_line_attributes (area_line_gc,LINE_WIDTH,GDK_LINE_SOLID, GDK_CAP_BUTT,GDK_JOIN_MITER);

		gdk_draw_rectangle (gtk_widget_get_window(widget), 
		   area_line_gc, FALSE, 
		    navcontrol->priv->view_area_rect.x + LINE_WIDTH/2, 
		    navcontrol->priv->view_area_rect.y + LINE_WIDTH/2, 
		    navcontrol->priv->view_area_rect.width - LINE_WIDTH,
		    navcontrol->priv->view_area_rect.height - LINE_WIDTH
		    );

		g_object_unref(area_line_gc);
		
	}
	
	return TRUE;
}
*/


static gboolean
quiver_navigation_control_draw(GtkWidget* navcontrol, cairo_t* cr)
{
// FIXME: implement
}

void quiver_navigation_control_update_adjustments(QuiverNavigationControl *navcontrol, gint x, gint y)
{
	int w,h;
	double xval, yval;

	GtkAdjustment *hadj = navcontrol->priv->hadjustment;
	GtkAdjustment *vadj = navcontrol->priv->vadjustment;
	
	
	if (NULL != navcontrol->priv->pixbuf)
	{	
	
		w  = gdk_pixbuf_get_width(navcontrol->priv->pixbuf);
		h  = gdk_pixbuf_get_height(navcontrol->priv->pixbuf);
		
		
		xval = x/(double)w*gtk_adjustment_get_upper(hadj) - gtk_adjustment_get_page_size(hadj)/2;
		yval = y/(double)h*gtk_adjustment_get_upper(vadj) - gtk_adjustment_get_page_size(vadj)/2;
	
		if (gtk_adjustment_get_upper(hadj) - gtk_adjustment_get_page_size(hadj) < xval)
			xval = gtk_adjustment_get_upper(hadj) - gtk_adjustment_get_page_size(hadj);
		else if (x < 0)
			xval = 0;
	
		if (gtk_adjustment_get_upper(vadj) - gtk_adjustment_get_page_size(vadj) < yval)
			yval = gtk_adjustment_get_upper(vadj) - gtk_adjustment_get_page_size(vadj);
		else if (y < 0)
			yval = 0;
			
		gtk_adjustment_set_value(hadj,xval);
		gtk_adjustment_set_value(vadj,yval);
	}
}

static gboolean
quiver_navigation_control_button_press_event  (GtkWidget *widget, GdkEventButton *event)
{
	QuiverNavigationControl *navcontrol;
	navcontrol = QUIVER_NAVIGATION_CONTROL(widget);

	int x,y;

	x = (int)event->x;
	y = (int)event->y;
	quiver_navigation_control_update_adjustments(navcontrol, x, y);
	return TRUE;
}


static gboolean
quiver_navigation_control_button_release_event (GtkWidget *widget, GdkEventButton *event)
{
	return TRUE;
}
                   
static gboolean
quiver_navigation_control_motion_notify_event (GtkWidget *widget, GdkEventMotion *event)
{
	QuiverNavigationControl *navcontrol;
	navcontrol = QUIVER_NAVIGATION_CONTROL(widget);
	
	if ( !(GDK_BUTTON1_MASK & event->state) )
	{
		return FALSE;
	}
	
	int x,y;
	GdkModifierType state;
	
	if (event->is_hint)
	{
		gdk_window_get_pointer (gtk_widget_get_window(event), &x, &y, &state);
	}		
	else
	{
		x = (int)event->x;
		y = (int)event->y;
		state = (GdkModifierType)event->state;
	}
	
	quiver_navigation_control_update_adjustments(navcontrol, x, y);

	return TRUE;
}



static void
quiver_navigation_control_set_property (GObject *object,
                    guint               prop_id,
                    const GValue       *value,
                    GParamSpec         *pspec)
{
	QuiverNavigationControl *navcontrol;
	navcontrol = QUIVER_NAVIGATION_CONTROL(object);

	switch (prop_id)
	{
		case PROP_HADJUSTMENT:
			quiver_navigation_control_set_hadjustment (navcontrol, g_value_get_object (value));
			break;

		case PROP_VADJUSTMENT:
			quiver_navigation_control_set_vadjustment (navcontrol, g_value_get_object (value));
			break;

		case PROP_HSCROLL_POLICY:
			navcontrol->priv->hscroll_policy = g_value_get_enum(value);
			gtk_widget_queue_resize(GTK_WIDGET(navcontrol));
			break;

		case PROP_VSCROLL_POLICY:
			navcontrol->priv->vscroll_policy = g_value_get_enum(value);
			gtk_widget_queue_resize(GTK_WIDGET(navcontrol));
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}

}
static void      
quiver_navigation_control_get_property (GObject    *object,
                    guint       prop_id,
                    GValue     *value,
                    GParamSpec *pspec)
{
	QuiverNavigationControl *navcontrol;
	navcontrol = QUIVER_NAVIGATION_CONTROL(object);

	switch (prop_id)
	{
		case PROP_HADJUSTMENT:
			g_value_set_boxed (value, navcontrol->priv->hadjustment);
			break;
			
		case PROP_VADJUSTMENT:
			g_value_set_boxed (value, navcontrol->priv->vadjustment);
			break;
		case PROP_HSCROLL_POLICY:
			g_value_set_enum(value, navcontrol->priv->hscroll_policy);
			break;
		case PROP_VSCROLL_POLICY:
			g_value_set_enum(value, navcontrol->priv->vscroll_policy);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}





GtkWidget *quiver_navigation_control_new ()
{
	return g_object_new(QUIVER_TYPE_NAVIGATION_CONTROL,NULL);
}

GtkWidget *quiver_navigation_control_new_with_adjustments (GtkAdjustment *hadjust, GtkAdjustment *vadjust)
{
	g_return_val_if_fail (GTK_IS_ADJUSTMENT (hadjust), NULL);
	g_return_val_if_fail (GTK_IS_ADJUSTMENT (vadjust), NULL);

	return g_object_new(QUIVER_TYPE_NAVIGATION_CONTROL,"hadjustment", hadjust, "vadjustment" , vadjust , NULL);
}

void quiver_navigation_control_set_pixbuf (QuiverNavigationControl *navcontrol, GdkPixbuf *pixbuf)
{
	GtkWidget *widget = GTK_WIDGET(navcontrol);
	gint width, height;
	if (NULL != navcontrol->priv->pixbuf)
	{
		g_object_unref(navcontrol->priv->pixbuf);
		navcontrol->priv->pixbuf = NULL;
	}

	if (NULL != pixbuf)
	{
		g_object_ref(pixbuf);
		navcontrol->priv->pixbuf = pixbuf;
		
		width  = gdk_pixbuf_get_width(pixbuf);
		height = gdk_pixbuf_get_height(pixbuf);
	
		gtk_widget_set_size_request(GTK_WIDGET(navcontrol),width,height);

		if (gtk_widget_get_realized(widget))
		{
			cairo_rectangle_int_t pixbuf_rect = {0};
			pixbuf_rect.x = 0;
			pixbuf_rect.y = 0;
			pixbuf_rect.width = width;
			pixbuf_rect.height = height;
			gdk_window_invalidate_rect(gtk_widget_get_window(widget),&pixbuf_rect,FALSE);
		}
	}
}


static void
quiver_navigation_control_set_hadjustment (QuiverNavigationControl *navcontrol, GtkAdjustment *hadjustment)
{
	g_return_if_fail (GTK_IS_ADJUSTMENT (hadjustment));
	
	if (NULL != navcontrol->priv->hadjustment)
	{
		g_signal_handlers_disconnect_by_func (navcontrol->priv->hadjustment,
			quiver_navigation_control_adjustment_changed,
			navcontrol);

		g_object_unref(navcontrol->priv->hadjustment);
		navcontrol->priv->hadjustment = NULL;
	}

	g_object_ref(hadjustment);
	navcontrol->priv->hadjustment = hadjustment;

	g_signal_connect (navcontrol->priv->hadjustment, "value_changed",
		G_CALLBACK (quiver_navigation_control_adjustment_changed), navcontrol);
	g_signal_connect (navcontrol->priv->hadjustment, "changed",
		G_CALLBACK (quiver_navigation_control_adjustment_changed), navcontrol);
}

static void
quiver_navigation_control_set_vadjustment (QuiverNavigationControl *navcontrol, GtkAdjustment *vadjustment)
{
	g_return_if_fail (GTK_IS_ADJUSTMENT (vadjustment));
	
	if (NULL != navcontrol->priv->vadjustment)
	{
		g_signal_handlers_disconnect_by_func (navcontrol->priv->vadjustment,
			quiver_navigation_control_adjustment_changed,
			navcontrol);

		g_object_unref(navcontrol->priv->vadjustment);
		navcontrol->priv->vadjustment = NULL;
	}

	g_object_ref(vadjustment);
	navcontrol->priv->vadjustment = vadjustment;

	g_signal_connect (navcontrol->priv->vadjustment, "value_changed",
		G_CALLBACK (quiver_navigation_control_adjustment_changed), navcontrol);
	g_signal_connect (navcontrol->priv->vadjustment, "changed",
		G_CALLBACK (quiver_navigation_control_adjustment_changed), navcontrol);
}

static void
quiver_navigation_control_adjustment_changed (GtkAdjustment *adjustment, gpointer userdata)
{
	QuiverNavigationControl *navcontrol;
	navcontrol = QUIVER_NAVIGATION_CONTROL(userdata);

	if (NULL == navcontrol->priv->pixbuf)
	{
		return;
	}

	GtkWidget *widget = GTK_WIDGET(navcontrol);
	GtkAdjustment *hadj = navcontrol->priv->hadjustment;
	GtkAdjustment *vadj = navcontrol->priv->vadjustment;
	
	gint hval = (gint)(gtk_adjustment_get_value(hadj) + .5);
	gint vval = (gint)(gtk_adjustment_get_value(vadj) + .5);

	int w,h;
	w = gdk_pixbuf_get_width(navcontrol->priv->pixbuf);
	h = gdk_pixbuf_get_height(navcontrol->priv->pixbuf);

	
	cairo_rectangle_int_t view_area_rect_old = navcontrol->priv->view_area_rect;
		
	// calc box offsets and width
	int b_x = w * (hval / gtk_adjustment_get_upper(hadj));
	int b_y = h * (vval / gtk_adjustment_get_upper(vadj));
	
	int b_w = w * (gtk_adjustment_get_page_size(hadj)/gtk_adjustment_get_upper(hadj));
	int b_h = h * (gtk_adjustment_get_page_size(vadj)/gtk_adjustment_get_upper(vadj));
	
	// set up the box size
	if (-1 == navcontrol->priv->view_area_rect.x)
	{
		view_area_rect_old.x = navcontrol->priv->view_area_rect.x = b_x;
		view_area_rect_old.y = navcontrol->priv->view_area_rect.y = b_y;
		navcontrol->priv->view_area_rect.width = view_area_rect_old.width = b_w;
		navcontrol->priv->view_area_rect.height = view_area_rect_old.height = b_h;
	}
	else
	{
		navcontrol->priv->view_area_rect.x = b_x;	
		navcontrol->priv->view_area_rect.y = b_y;
		navcontrol->priv->view_area_rect.width = b_w + 1;
		navcontrol->priv->view_area_rect.height = b_h + 1;
	}
	
	if (gtk_widget_get_realized(widget))
	{

		/* FIXME:
		GdkRegion *invalid_region;
		invalid_region = gdk_region_new();
		
		gdk_region_union_with_rect(invalid_region, &navcontrol->priv->view_area_rect);
		gdk_region_union_with_rect(invalid_region, &view_area_rect_old);
		
		
		gdk_window_invalidate_region(gtk_widget_get_window(widget),invalid_region,FALSE);
	
		gdk_region_destroy(invalid_region);
		*/
		gdk_window_invalidate_rect(gtk_widget_get_window(widget),NULL,FALSE);
	}

}


