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

#define QUIVER_NAV_CONTROL_MAX_SIZE 140

#define QUIVER_PARAM_READWRITE G_PARAM_READWRITE|G_PARAM_STATIC_NAME|G_PARAM_STATIC_NICK|G_PARAM_STATIC_BLURB

struct _QuiverNavigationControlPrivate
{
	GtkAdjustment *hadjustment;
	GtkAdjustment *vadjustment;
	guint hscroll_policy : 1;
	guint vscroll_policy : 1;

	GdkTexture *texture;
#if HAVE_GDK_PIXBUF
	GdkPixbuf *pixbuf;
#endif

	GdkRectangle view_area_rect;

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

#if HAVE_GDK_PIXBUF
	navcontrol->priv->pixbuf = NULL;
#endif

	navcontrol->priv->view_area_rect.x = -1;

	gtk_widget_set_focusable(GTK_WIDGET(navcontrol), FALSE);
	gtk_widget_set_can_target(GTK_WIDGET(navcontrol), TRUE);
	gtk_widget_set_cursor_from_name(GTK_WIDGET(navcontrol), "grab");

	quiver_navigation_control_setup_controllers(navcontrol);
}



static void
quiver_navigation_control_finalize(GObject *object)
{
	GObjectClass *parent;
	QuiverNavigationControlClass *klass; 
	QuiverNavigationControl *navcontrol;

	navcontrol = QUIVER_NAVIGATION_CONTROL(object);
	klass = QUIVER_NAVIGATION_CONTROL_GET_CLASS(navcontrol);
	
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

		
	if (NULL != navcontrol->priv->texture)
	{
		g_object_unref(navcontrol->priv->texture);
		navcontrol->priv->texture = NULL;
	}
#if HAVE_GDK_PIXBUF
	if (NULL != navcontrol->priv->pixbuf)
	{
		g_object_unref(navcontrol->priv->pixbuf);
		navcontrol->priv->pixbuf = NULL;
	}
#endif

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

	if (alloc_w <= 0 || alloc_h <= 0)
		return;

	graphene_rect_t bounds;
	graphene_rect_init(&bounds, 0, 0, (float)alloc_w, (float)alloc_h);

	GskRoundedRect rounded_bounds;
	gsk_rounded_rect_init_from_rect(&rounded_bounds, &bounds, 6.0f);
	gtk_snapshot_push_rounded_clip(snapshot, &rounded_bounds);

	if (navcontrol->priv->texture)
	{
		gtk_snapshot_append_texture(snapshot, navcontrol->priv->texture, &bounds);
	}

	if (navcontrol->priv->view_area_rect.x >= 0 &&
	    navcontrol->priv->view_area_rect.width > 0 &&
	    navcontrol->priv->view_area_rect.height > 0)
	{
		graphene_rect_t view_rect;
		graphene_rect_init(&view_rect,
			(float)navcontrol->priv->view_area_rect.x,
			(float)navcontrol->priv->view_area_rect.y,
			(float)navcontrol->priv->view_area_rect.width,
			(float)navcontrol->priv->view_area_rect.height);

		GdkRGBA fill_color = { 1.0f, 0.2f, 0.2f, 0.15f };
		gtk_snapshot_append_color(snapshot, &fill_color, &view_rect);

		GskRoundedRect rounded_rect;
		gsk_rounded_rect_init_from_rect(&rounded_rect, &view_rect, 0.0f);

		float widths[4] = { 2.0f, 2.0f, 2.0f, 2.0f };
		GdkRGBA colors[4] = {
			{ 1.0f, 0.15f, 0.15f, 0.95f },
			{ 1.0f, 0.15f, 0.15f, 0.95f },
			{ 1.0f, 0.15f, 0.15f, 0.95f },
			{ 1.0f, 0.15f, 0.15f, 0.95f }
		};
		gtk_snapshot_append_border(snapshot, &rounded_rect, widths, colors);
	}

	gtk_snapshot_pop(snapshot);
}

static void
quiver_navigation_control_size_allocate (GtkWidget     *widget,
				int width,
				int height,
				int baseline)
{
	(void)baseline;
	(void)width;
	(void)height;
	g_return_if_fail (QUIVER_IS_NAVIGATION_CONTROL (widget));

	QuiverNavigationControl *navcontrol = QUIVER_NAVIGATION_CONTROL(widget);

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
	(void)minimum_baseline;
	(void)natural_baseline;
	QuiverNavigationControl *navcontrol = QUIVER_NAVIGATION_CONTROL(widget);

	int tw = 0, th = 0;
	if (navcontrol->priv->texture)
	{
		tw = gdk_texture_get_width(navcontrol->priv->texture);
		th = gdk_texture_get_height(navcontrol->priv->texture);
	}
#if HAVE_GDK_PIXBUF
	else if (navcontrol->priv->pixbuf)
	{
		tw = gdk_pixbuf_get_width(navcontrol->priv->pixbuf);
		th = gdk_pixbuf_get_height(navcontrol->priv->pixbuf);
	}
#endif

	if (tw <= 0 || th <= 0)
	{
		*minimum = *natural = 0;
		return;
	}

	int target_w, target_h;
	if (tw >= th)
	{
		target_w = QUIVER_NAV_CONTROL_MAX_SIZE;
		target_h = MAX(1, (QUIVER_NAV_CONTROL_MAX_SIZE * th) / tw);
	}
	else
	{
		target_h = QUIVER_NAV_CONTROL_MAX_SIZE;
		target_w = MAX(1, (QUIVER_NAV_CONTROL_MAX_SIZE * tw) / th);
	}

	if (orientation == GTK_ORIENTATION_HORIZONTAL)
	{
		if (for_size >= 0)
		{
			int calc_w = MAX(1, (for_size * tw) / th);
			*minimum = *natural = MIN(calc_w, target_w);
		}
		else
		{
			*minimum = *natural = target_w;
		}
	}
	else // GTK_ORIENTATION_VERTICAL
	{
		if (for_size >= 0)
		{
			int calc_h = MAX(1, (for_size * th) / tw);
			*minimum = *natural = MIN(calc_h, target_h);
		}
		else
		{
			*minimum = *natural = target_h;
		}
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
			g_value_set_object (value, navcontrol->priv->hadjustment);
			break;
			
		case PROP_VADJUSTMENT:
			g_value_set_object (value, navcontrol->priv->vadjustment);
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
	GtkWidget *widget = GTK_WIDGET(navcontrol);
	int w = gtk_widget_get_width(widget);
	int h = gtk_widget_get_height(widget);
	if (w <= 0 || h <= 0)
	{
		if (navcontrol->priv->texture)
		{
			w = gdk_texture_get_width(navcontrol->priv->texture);
			h = gdk_texture_get_height(navcontrol->priv->texture);
		}
#if HAVE_GDK_PIXBUF
		else if (navcontrol->priv->pixbuf)
		{
			w = gdk_pixbuf_get_width(navcontrol->priv->pixbuf);
			h = gdk_pixbuf_get_height(navcontrol->priv->pixbuf);
		}
#endif
	}
	if (w <= 0 || h <= 0)
		return;

	GtkAdjustment *hadj = navcontrol->priv->hadjustment;
	GtkAdjustment *vadj = navcontrol->priv->vadjustment;
	if (!hadj || !vadj)
		return;

	double upper_h = gtk_adjustment_get_upper(hadj);
	double upper_v = gtk_adjustment_get_upper(vadj);
	if (upper_h <= 0.0 || upper_v <= 0.0)
		return;

	double page_h = gtk_adjustment_get_page_size(hadj);
	double page_v = gtk_adjustment_get_page_size(vadj);

	double xval = ((double)x / (double)w) * upper_h - page_h / 2.0;
	double yval = ((double)y / (double)h) * upper_v - page_v / 2.0;

	double max_x = upper_h - page_h;
	if (max_x < 0.0) max_x = 0.0;
	if (xval > max_x) xval = max_x;
	if (xval < 0.0) xval = 0.0;

	double max_y = upper_v - page_v;
	if (max_y < 0.0) max_y = 0.0;
	if (yval > max_y) yval = max_y;
	if (yval < 0.0) yval = 0.0;

	gtk_adjustment_set_value(hadj, xval);
	gtk_adjustment_set_value(vadj, yval);
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

void quiver_navigation_control_set_texture (QuiverNavigationControl *navcontrol, GdkTexture *texture)
{
	GtkWidget *widget = GTK_WIDGET(navcontrol);
	if (navcontrol->priv->texture == texture)
		return;

	if (NULL != navcontrol->priv->texture)
	{
		g_object_unref(navcontrol->priv->texture);
		navcontrol->priv->texture = NULL;
	}

	if (NULL != texture)
	{
		g_object_ref(texture);
		navcontrol->priv->texture = texture;
		
		int tw = gdk_texture_get_width(texture);
		int th = gdk_texture_get_height(texture);
		int target_w, target_h;
		if (tw >= th && tw > 0)
		{
			target_w = QUIVER_NAV_CONTROL_MAX_SIZE;
			target_h = MAX(1, (QUIVER_NAV_CONTROL_MAX_SIZE * th) / tw);
		}
		else if (th > 0)
		{
			target_h = QUIVER_NAV_CONTROL_MAX_SIZE;
			target_w = MAX(1, (QUIVER_NAV_CONTROL_MAX_SIZE * tw) / th);
		}
		else
		{
			target_w = target_h = QUIVER_NAV_CONTROL_MAX_SIZE;
		}
		gtk_widget_set_size_request(widget, target_w, target_h);
	}
	else
	{
		gtk_widget_set_size_request(widget, 0, 0);
	}

	gtk_widget_queue_resize(widget);
	gtk_widget_queue_draw(widget);
}

#if HAVE_GDK_PIXBUF
void quiver_navigation_control_set_pixbuf (QuiverNavigationControl *navcontrol, GdkPixbuf *pixbuf)
{
	GtkWidget *widget = GTK_WIDGET(navcontrol);
	if (NULL != navcontrol->priv->pixbuf)
	{
		g_object_unref(navcontrol->priv->pixbuf);
		navcontrol->priv->pixbuf = NULL;
	}

	if (NULL != pixbuf)
	{
		g_object_ref(pixbuf);
		navcontrol->priv->pixbuf = pixbuf;
		
		if (navcontrol->priv->texture)
		{
			g_object_unref(navcontrol->priv->texture);
		}
		navcontrol->priv->texture = quiver_pixbuf_to_texture(pixbuf);

		int tw = gdk_pixbuf_get_width(pixbuf);
		int th = gdk_pixbuf_get_height(pixbuf);
		int target_w, target_h;
		if (tw >= th && tw > 0)
		{
			target_w = QUIVER_NAV_CONTROL_MAX_SIZE;
			target_h = MAX(1, (QUIVER_NAV_CONTROL_MAX_SIZE * th) / tw);
		}
		else if (th > 0)
		{
			target_h = QUIVER_NAV_CONTROL_MAX_SIZE;
			target_w = MAX(1, (QUIVER_NAV_CONTROL_MAX_SIZE * tw) / th);
		}
		else
		{
			target_w = target_h = QUIVER_NAV_CONTROL_MAX_SIZE;
		}
		gtk_widget_set_size_request(widget, target_w, target_h);

		gtk_widget_queue_resize(widget);
		gtk_widget_queue_draw(widget);
	}
}
#endif


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
	(void)adjustment; 
	QuiverNavigationControl *navcontrol;
	navcontrol = QUIVER_NAVIGATION_CONTROL(userdata);

	GtkWidget *widget = GTK_WIDGET(navcontrol);
	int w = gtk_widget_get_width(widget);
	int h = gtk_widget_get_height(widget);
	if (w <= 0 || h <= 0)
	{
		if (navcontrol->priv->texture)
		{
			w = gdk_texture_get_width(navcontrol->priv->texture);
			h = gdk_texture_get_height(navcontrol->priv->texture);
		}
#if HAVE_GDK_PIXBUF
		else if (navcontrol->priv->pixbuf)
		{
			w = gdk_pixbuf_get_width(navcontrol->priv->pixbuf);
			h = gdk_pixbuf_get_height(navcontrol->priv->pixbuf);
		}
#endif
	}
	if (w <= 0 || h <= 0)
		return;

	GtkAdjustment *hadj = navcontrol->priv->hadjustment;
	GtkAdjustment *vadj = navcontrol->priv->vadjustment;
	if (!hadj || !vadj)
		return;

	double upper_h = gtk_adjustment_get_upper(hadj);
	double upper_v = gtk_adjustment_get_upper(vadj);
	if (upper_h <= 0.0 || upper_v <= 0.0)
		return;

	double hval = gtk_adjustment_get_value(hadj);
	double vval = gtk_adjustment_get_value(vadj);
	double page_h = gtk_adjustment_get_page_size(hadj);
	double page_v = gtk_adjustment_get_page_size(vadj);

	int b_x = (int)(w * (hval / upper_h));
	int b_y = (int)(h * (vval / upper_v));
	int b_w = (int)(w * (page_h / upper_h) + 0.5);
	int b_h = (int)(h * (page_v / upper_v) + 0.5);

	// Clamp rectangle inside [0, 0, w, h]
	if (b_x < 0) b_x = 0;
	if (b_y < 0) b_y = 0;
	if (b_w > w) b_w = w;
	if (b_h > h) b_h = h;
	if (b_x + b_w > w) b_x = w - b_w;
	if (b_y + b_h > h) b_y = h - b_h;

	navcontrol->priv->view_area_rect.x = b_x;
	navcontrol->priv->view_area_rect.y = b_y;
	navcontrol->priv->view_area_rect.width = b_w;
	navcontrol->priv->view_area_rect.height = b_h;

	gtk_widget_queue_draw(widget);
}

/* start controller callbacks */

static void
quiver_navigation_control_gesture_drag_begin (GtkGestureDrag *gesture,
				      double x,
				      double y,
				      QuiverNavigationControl *navcontrol)
{
	(void)gesture;
	gtk_widget_set_cursor_from_name(GTK_WIDGET(navcontrol), "grabbing");
	quiver_navigation_control_update_adjustments(navcontrol, (int)x, (int)y);
}

static void
quiver_navigation_control_gesture_drag_update (GtkGestureDrag *gesture,
				       double x,
				       double y,
				       QuiverNavigationControl *navcontrol)
{
	double start_x, start_y;
	gtk_gesture_drag_get_start_point(gesture, &start_x, &start_y);
	int abs_x = (int)(start_x + x);
	int abs_y = (int)(start_y + y);

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
	gtk_widget_set_cursor_from_name(GTK_WIDGET(navcontrol), "grab");
}

static void
quiver_navigation_control_setup_controllers (QuiverNavigationControl *navcontrol)
{
	GtkWidget *widget = GTK_WIDGET(navcontrol);

	GtkGesture *drag = gtk_gesture_drag_new();
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(drag), GDK_BUTTON_PRIMARY);
	g_signal_connect(drag, "drag-begin",
		G_CALLBACK(quiver_navigation_control_gesture_drag_begin), navcontrol);
	g_signal_connect(drag, "drag-update",
		G_CALLBACK(quiver_navigation_control_gesture_drag_update), navcontrol);
	g_signal_connect(drag, "drag-end",
		G_CALLBACK(quiver_navigation_control_gesture_drag_end), navcontrol);
	gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(drag));
}

/* end controller callbacks */

void quiver_navigation_control_rotate (QuiverNavigationControl *navcontrol, gboolean clockwise)
{
	g_return_if_fail (QUIVER_IS_NAVIGATION_CONTROL (navcontrol));
	if (NULL == navcontrol->priv->texture)
		return;

	GdkTexture *current_texture = navcontrol->priv->texture;
	int src_w = gdk_texture_get_width(current_texture);
	int src_h = gdk_texture_get_height(current_texture);
	if (src_w <= 0 || src_h <= 0)
		return;

	int dst_w = src_h;
	int dst_h = src_w;
	gsize src_stride = (gsize)src_w * 4;
	gsize dst_stride = (gsize)dst_w * 4;
	guint32 *src_buf = (guint32*)g_malloc(src_stride * src_h);
	guint32 *dst_buf = (guint32*)g_malloc(dst_stride * dst_h);
	GdkTextureDownloader *dl = gdk_texture_downloader_new(current_texture);
	gdk_texture_downloader_set_format(dl, GDK_MEMORY_R8G8B8A8);
	gdk_texture_downloader_download_into(dl, (guchar*)src_buf, src_stride);
	gdk_texture_downloader_free(dl);

	for (int y = 0; y < dst_h; ++y)
	{
		for (int x = 0; x < dst_w; ++x)
		{
			int sx, sy;
			if (clockwise)
			{
				sx = y;
				sy = src_h - 1 - x;
			}
			else
			{
				sx = src_w - 1 - y;
				sy = x;
			}
			if (sx >= 0 && sx < src_w && sy >= 0 && sy < src_h)
				dst_buf[y * dst_w + x] = src_buf[sy * src_w + sx];
		}
	}
	g_free(src_buf);

	GBytes *bytes = g_bytes_new_take(dst_buf, dst_stride * dst_h);
	GdkTexture *rotated_tex = gdk_memory_texture_new(dst_w, dst_h, GDK_MEMORY_R8G8B8A8, bytes, dst_stride);
	g_bytes_unref(bytes);

	quiver_navigation_control_set_texture(navcontrol, rotated_tex);
	g_object_unref(rotated_tex);
}

void quiver_navigation_control_flip (QuiverNavigationControl *navcontrol, gboolean horizontal)
{
	g_return_if_fail (QUIVER_IS_NAVIGATION_CONTROL (navcontrol));
	if (NULL == navcontrol->priv->texture)
		return;

	GdkTexture *current_texture = navcontrol->priv->texture;
	int src_w = gdk_texture_get_width(current_texture);
	int src_h = gdk_texture_get_height(current_texture);
	if (src_w <= 0 || src_h <= 0)
		return;

	gsize stride = (gsize)src_w * 4;
	guint32 *src_buf = (guint32*)g_malloc(stride * src_h);
	guint32 *dst_buf = (guint32*)g_malloc(stride * src_h);
	GdkTextureDownloader *dl = gdk_texture_downloader_new(current_texture);
	gdk_texture_downloader_set_format(dl, GDK_MEMORY_R8G8B8A8);
	gdk_texture_downloader_download_into(dl, (guchar*)src_buf, stride);
	gdk_texture_downloader_free(dl);

	for (int y = 0; y < src_h; ++y)
	{
		for (int x = 0; x < src_w; ++x)
		{
			int sx = horizontal ? (src_w - 1 - x) : x;
			int sy = horizontal ? y : (src_h - 1 - y);
			dst_buf[y * src_w + x] = src_buf[sy * src_w + sx];
		}
	}
	g_free(src_buf);

	GBytes *bytes = g_bytes_new_take(dst_buf, stride * src_h);
	GdkTexture *flipped_tex = gdk_memory_texture_new(src_w, src_h, GDK_MEMORY_R8G8B8A8, bytes, stride);
	g_bytes_unref(bytes);

	quiver_navigation_control_set_texture(navcontrol, flipped_tex);
	g_object_unref(flipped_tex);
}

/* end private functions */
