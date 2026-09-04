#include <config.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include "quiver-navigation-control.h"
#include "quiver-marshallers.h"
#include <math.h>
#include <sys/time.h>
#include "quiver-pixbuf-utils.h"

//#include "gtkintl.h"


#define QUIVER_NAVIGATION_CONTROL_GET_PRIVATE(obj) (quiver_navigation_control_get_instance_private (QUIVER_NAVIGATION_CONTROL (obj)))

/* set up some defaults */

#define QUIVER_PARAM_READWRITE G_PARAM_READWRITE|G_PARAM_STATIC_NAME|G_PARAM_STATIC_NICK|G_PARAM_STATIC_BLURB

struct _QuiverNavigationControlPrivate
{
	GtkAdjustment *hadjustment;
	GtkAdjustment *vadjustment;
	guint hscroll_policy : 1;
	guint vscroll_policy : 1;

	GdkPixbuf *pixbuf;

	cairo_rectangle_int_t view_area_rect;

};
G_DEFINE_TYPE_WITH_CODE(QuiverNavigationControl,quiver_navigation_control,GTK_TYPE_WIDGET, G_ADD_PRIVATE(QuiverNavigationControl) G_IMPLEMENT_INTERFACE(GTK_TYPE_SCROLLABLE, NULL));

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


/* end private data structures */


/* start private function prototypes */

static void      quiver_navigation_control_snapshot        (GtkWidget *widget, GtkSnapshot *snapshot);

static void      quiver_navigation_control_size_allocate  (GtkWidget     *widget,
                                             int width,
                                             int height,
                                             int baseline);

static void      quiver_navigation_control_measure (GtkWidget *widget,
					GtkOrientation orientation,
					int for_size,
					int *minimum,
					int *natural,
					int *minimum_baseline,
					int *natural_baseline);

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

/* start controller callback prototypes */
static void quiver_navigation_control_gesture_pressed (GtkGestureClick *gesture,
					       int n_press,
					       double x,
					       double y,
					       QuiverNavigationControl *navcontrol);
static void quiver_navigation_control_gesture_released (GtkGestureClick *gesture,
					       int n_press,
					       double x,
					       double y,
					       QuiverNavigationControl *navcontrol);
static void quiver_navigation_control_gesture_drag_begin (GtkGestureDrag *gesture,
						  double x,
						  double y,
						  QuiverNavigationControl *navcontrol);
static void quiver_navigation_control_gesture_drag_update (GtkGestureDrag *gesture,
						   double x,
						   double y,
						   QuiverNavigationControl *navcontrol);
static void quiver_navigation_control_gesture_drag_end (GtkGestureDrag *gesture,
						double x,
						double y,
						QuiverNavigationControl *navcontrol);
static void quiver_navigation_control_motion_controller_cb (GtkEventControllerMotion *controller,
						   double x,
						   double y,
						   QuiverNavigationControl *navcontrol);
static void quiver_navigation_control_setup_controllers (QuiverNavigationControl *navcontrol);
/* end controller callback prototypes */

/* end private function prototypes */

/* start private globals */

// static guint navcontrol_signals[SIGNAL_COUNT];

/* end private globals */


/* start private functions */
static void 
quiver_navigation_control_class_init (QuiverNavigationControlClass *klass)
{
	GtkWidgetClass *widget_class;
	GObjectClass *obj_class;

	widget_class = GTK_WIDGET_CLASS (klass);
	obj_class = G_OBJECT_CLASS (klass);

	widget_class->snapshot      = quiver_navigation_control_snapshot;
	widget_class->measure       = quiver_navigation_control_measure;
	widget_class->size_allocate = quiver_navigation_control_size_allocate;

	obj_class->set_property            = quiver_navigation_control_set_property;
	obj_class->get_property            = quiver_navigation_control_get_property;

		/* Override properties */
	g_object_class_override_property (obj_class, PROP_HADJUSTMENT, "hadjustment");
	g_object_class_override_property (obj_class, PROP_VADJUSTMENT, "vadjustment");
	g_object_class_override_property (obj_class, PROP_HSCROLL_POLICY, "hscroll-policy");
	g_object_class_override_property (obj_class, PROP_VSCROLL_POLICY, "vscroll-policy");

	obj_class->finalize                = quiver_navigation_control_finalize;

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

	gtk_widget_set_focusable(GTK_WIDGET(navcontrol),TRUE);

	quiver_navigation_control_setup_controllers(navcontrol);
}



static void
quiver_navigation_control_finalize(GObject *object)
{
	GObjectClass *parent,*obj_class;
 (void)obj_class;
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
quiver_navigation_control_snapshot (GtkWidget *widget, GtkSnapshot *snapshot)
{
	QuiverNavigationControl *navcontrol = QUIVER_NAVIGATION_CONTROL(widget);

	int alloc_w = gtk_widget_get_width(widget);
	int alloc_h = gtk_widget_get_height(widget);

	{
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
		GtkStyleContext *context = gtk_widget_get_style_context(widget);
		graphene_rect_t gbounds;
		gbounds.origin.x = 0;
		gbounds.origin.y = 0;
		gbounds.size.width = alloc_w;
		gbounds.size.height = alloc_h;
		cairo_t *cr = gtk_snapshot_append_cairo(snapshot, &gbounds);
		gtk_render_background(context, cr, 0, 0, alloc_w, alloc_h);
		cairo_destroy(cr);
G_GNUC_END_IGNORE_DEPRECATIONS
	}

	(void)navcontrol;
}

static void
quiver_navigation_control_size_allocate (GtkWidget     *widget,
				int width,
				int height,
				int baseline)
{
	(void)baseline;
	g_return_if_fail (QUIVER_IS_NAVIGATION_CONTROL (widget));

	QuiverNavigationControl *navcontrol = QUIVER_NAVIGATION_CONTROL(widget);

	(void)width;
	(void)height;

	if (NULL != navcontrol->priv->hadjustment && NULL != navcontrol->priv->vadjustment)
	{
		quiver_navigation_control_adjustment_changed(navcontrol->priv->hadjustment, (gpointer)navcontrol);
	}

	gtk_widget_queue_draw(widget);
}

static void
quiver_navigation_control_measure (GtkWidget *widget,
				GtkOrientation orientation,
				int for_size,
				int *minimum,
				int *natural,
				int *minimum_baseline,
				int *natural_baseline)
{
	(void)for_size;
	(void)widget;
	(void)minimum_baseline;
	(void)natural_baseline;

	if (orientation == GTK_ORIENTATION_HORIZONTAL)
	{
		*minimum = *natural = 0;
	}
	else
	{
		*minimum = *natural = 0;
	}
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

		gtk_widget_queue_draw(widget);
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
{ (void)adjustment; 
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
	
	gtk_widget_queue_draw(widget);

}

/* start controller callbacks */

static void
quiver_navigation_control_gesture_pressed (GtkGestureClick *gesture,
				   int n_press,
				   double x,
				   double y,
				   QuiverNavigationControl *navcontrol)
{
	(void)gesture;
	(void)n_press;
	int ix = (int)x;
	int iy = (int)y;
	quiver_navigation_control_update_adjustments(navcontrol, ix, iy);
}

static void
quiver_navigation_control_gesture_released (GtkGestureClick *gesture,
				   int n_press,
				   double x,
				   double y,
				   QuiverNavigationControl *navcontrol)
{
	(void)gesture;
	(void)n_press;
	(void)x;
	(void)y;
	(void)navcontrol;
}

static void
quiver_navigation_control_gesture_drag_begin (GtkGestureDrag *gesture,
				      double x,
				      double y,
				      QuiverNavigationControl *navcontrol)
{
	(void)gesture;
	(void)x;
	(void)y;
	(void)navcontrol;
}

static void
quiver_navigation_control_gesture_drag_update (GtkGestureDrag *gesture,
				       double x,
				       double y,
				       QuiverNavigationControl *navcontrol)
{
	(void)gesture;

	double start_x, start_y;
	gtk_gesture_drag_get_start_point(gesture, &start_x, &start_y);
	int abs_x = (int)start_x + (int)x;
	int abs_y = (int)start_y + (int)y;

	quiver_navigation_control_update_adjustments(navcontrol, abs_x, abs_y);
}

static void
quiver_navigation_control_gesture_drag_end (GtkGestureDrag *gesture,
				    double x,
				    double y,
				    QuiverNavigationControl *navcontrol)
{
	(void)gesture;
	(void)x;
	(void)y;
	(void)navcontrol;
}

static void
quiver_navigation_control_motion_controller_cb (GtkEventControllerMotion *controller,
				       double x,
				       double y,
				       QuiverNavigationControl *navcontrol)
{
	(void)controller;
	(void)x;
	(void)y;
	(void)navcontrol;
}

static void
quiver_navigation_control_setup_controllers (QuiverNavigationControl *navcontrol)
{
	GtkWidget *widget = GTK_WIDGET(navcontrol);

	GtkGesture *click = gtk_gesture_click_new();
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_PRIMARY);
	g_signal_connect(click, "pressed",
		G_CALLBACK(quiver_navigation_control_gesture_pressed), navcontrol);
	g_signal_connect(click, "released",
		G_CALLBACK(quiver_navigation_control_gesture_released), navcontrol);
	gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(click));

	GtkGesture *drag = gtk_gesture_drag_new();
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(drag), GDK_BUTTON_PRIMARY);
	g_signal_connect(drag, "drag-begin",
		G_CALLBACK(quiver_navigation_control_gesture_drag_begin), navcontrol);
	g_signal_connect(drag, "drag-update",
		G_CALLBACK(quiver_navigation_control_gesture_drag_update), navcontrol);
	g_signal_connect(drag, "drag-end",
		G_CALLBACK(quiver_navigation_control_gesture_drag_end), navcontrol);
	gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(drag));

	GtkEventController *motion = gtk_event_controller_motion_new();
	g_signal_connect(motion, "motion",
		G_CALLBACK(quiver_navigation_control_motion_controller_cb), navcontrol);
	gtk_widget_add_controller(widget, motion);
}

/* end controller callbacks */

/* end private functions */
