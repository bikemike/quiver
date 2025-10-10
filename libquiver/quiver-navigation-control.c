#include <config.h>
#include <gtk/gtk.h>
#include "quiver-navigation-control.h"
#include <math.h>

#define QUIVER_PARAM_READWRITE G_PARAM_READWRITE|G_PARAM_STATIC_STRINGS

struct _QuiverNavigationControlPrivate
{
	GtkAdjustment *hadjustment;
	GtkAdjustment *vadjustment;
	GtkScrollablePolicy hscroll_policy;
	GtkScrollablePolicy vscroll_policy;

	GdkPixbuf *pixbuf;
	cairo_rectangle_int_t view_area_rect;

    GtkGesture *click_gesture;
    GtkGesture *drag_gesture;
    gdouble drag_start_x;
    gdouble drag_start_y;
};

static void quiver_navigation_control_scrollable_interface_init (GtkScrollableInterface *iface);

G_DEFINE_TYPE_WITH_CODE(QuiverNavigationControl, quiver_navigation_control, GTK_TYPE_WIDGET,
                        G_ADD_PRIVATE(QuiverNavigationControl)
                        G_IMPLEMENT_INTERFACE(GTK_TYPE_SCROLLABLE, quiver_navigation_control_scrollable_interface_init));


/* Properties */
enum {
   PROP_0,
   PROP_HADJUSTMENT,
   PROP_VADJUSTMENT,
   PROP_HSCROLL_POLICY,
   PROP_VSCROLL_POLICY,
   PROP_N_PROPS
};

/* Function Prototypes */
static void quiver_navigation_control_realize(GtkWidget *widget);
static void quiver_navigation_control_unrealize(GtkWidget *widget);
static void quiver_navigation_control_map(GtkWidget *widget);
static void quiver_navigation_control_unmap(GtkWidget *widget);
static void quiver_navigation_control_size_allocate(GtkWidget *widget, int width, int height, int baseline);
static void quiver_navigation_control_measure(GtkWidget *widget, GtkOrientation orientation, int for_size, int *minimum, int *natural, int *minimum_baseline, int *natural_baseline);
static void quiver_navigation_control_snapshot(GtkWidget *widget, GtkSnapshot *snapshot);

static void quiver_navigation_control_set_hadjustment(QuiverNavigationControl *navcontrol, GtkAdjustment *hadjustment);
static void quiver_navigation_control_set_vadjustment(QuiverNavigationControl *navcontrol, GtkAdjustment *vadjustment);
static void quiver_navigation_control_adjustment_value_or_page_changed(GtkAdjustment *adjustment, gpointer userdata);

static void quiver_navigation_control_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void quiver_navigation_control_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static void quiver_navigation_control_dispose(GObject *object);
static void quiver_navigation_control_finalize(GObject *object);


static void quiver_navigation_control_update_view_area_rect(QuiverNavigationControl *navcontrol);
static void quiver_navigation_control_update_adjustments_from_xy(QuiverNavigationControl *navcontrol, double x, double y);
static void quiver_navigation_control_handle_click_pressed(GtkGestureClick *gesture, int n_press, double x, double y, QuiverNavigationControl *navcontrol);
static void quiver_navigation_control_handle_drag_begin(GtkGestureDrag *gesture, double start_x, double start_y, QuiverNavigationControl *navcontrol);
static void quiver_navigation_control_handle_drag_update(GtkGestureDrag *gesture, double offset_x, double offset_y, QuiverNavigationControl *navcontrol);
static void quiver_navigation_control_handle_drag_end(GtkGestureDrag *gesture, double offset_x, double offset_y, QuiverNavigationControl *navcontrol);


static void quiver_navigation_control_class_init(QuiverNavigationControlClass *klass) {
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);
	GObjectClass *obj_class = G_OBJECT_CLASS(klass);

	widget_class->realize = quiver_navigation_control_realize;
    widget_class->unrealize = quiver_navigation_control_unrealize;
    widget_class->map = quiver_navigation_control_map;
    widget_class->unmap = quiver_navigation_control_unmap;
	widget_class->size_allocate = quiver_navigation_control_size_allocate;
	widget_class->measure = quiver_navigation_control_measure;
    widget_class->snapshot = quiver_navigation_control_snapshot;

	obj_class->set_property = quiver_navigation_control_set_property;
	obj_class->get_property = quiver_navigation_control_get_property;
    obj_class->dispose = quiver_navigation_control_dispose;
	obj_class->finalize = quiver_navigation_control_finalize;

	g_object_class_override_property(obj_class, PROP_HADJUSTMENT, "hadjustment");
	g_object_class_override_property(obj_class, PROP_VADJUSTMENT, "vadjustment");
	g_object_class_override_property(obj_class, PROP_HSCROLL_POLICY, "hscroll-policy");
	g_object_class_override_property(obj_class, PROP_VSCROLL_POLICY, "vscroll-policy");
}

static void quiver_navigation_control_scrollable_interface_init (GtkScrollableInterface *iface)
{
  /* If you want to support smooth scrolling, you need to implement get_scroll_info */
}

static GtkAdjustment* new_default_adjustment(void) {
    return gtk_adjustment_new(0.0, 0.0, 100.0, 1.0, 10.0, 10.0);
}

static void quiver_navigation_control_init(QuiverNavigationControl *navcontrol) {
	QuiverNavigationControlPrivate *priv = quiver_navigation_control_get_instance_private(navcontrol);

	priv->pixbuf = NULL;
	priv->view_area_rect.x = -1;
    priv->hadjustment = NULL;
    priv->vadjustment = NULL;
    priv->hscroll_policy = GTK_SCROLL_MINIMUM;
    priv->vscroll_policy = GTK_SCROLL_MINIMUM;


    priv->click_gesture = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(priv->click_gesture), GDK_BUTTON_PRIMARY);
    g_signal_connect(priv->click_gesture, "pressed", G_CALLBACK(quiver_navigation_control_handle_click_pressed), navcontrol);
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(priv->click_gesture), GTK_PHASE_CAPTURE);


    priv->drag_gesture = gtk_gesture_drag_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(priv->drag_gesture), GDK_BUTTON_PRIMARY);
    g_signal_connect(priv->drag_gesture, "drag-begin", G_CALLBACK(quiver_navigation_control_handle_drag_begin), navcontrol);
    g_signal_connect(priv->drag_gesture, "drag-update", G_CALLBACK(quiver_navigation_control_handle_drag_update), navcontrol);
    g_signal_connect(priv->drag_gesture, "drag-end", G_CALLBACK(quiver_navigation_control_handle_drag_end), navcontrol);
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(priv->drag_gesture), GTK_PHASE_CAPTURE);


	gtk_widget_set_can_focus(GTK_WIDGET(navcontrol), TRUE);
}

static void quiver_navigation_control_dispose(GObject *object) {
    QuiverNavigationControl *navcontrol = QUIVER_NAVIGATION_CONTROL(object);
    QuiverNavigationControlPrivate *priv = quiver_navigation_control_get_instance_private(navcontrol);

    g_clear_object(&priv->click_gesture);
    g_clear_object(&priv->drag_gesture);
    g_clear_object(&priv->pixbuf);
    g_clear_object(&priv->hadjustment);
    g_clear_object(&priv->vadjustment);

    G_OBJECT_CLASS(quiver_navigation_control_parent_class)->dispose(object);
}

static void quiver_navigation_control_finalize(GObject *object) {
	G_OBJECT_CLASS(quiver_navigation_control_parent_class)->finalize(object);
}

static void quiver_navigation_control_realize(GtkWidget *widget) {
    GTK_WIDGET_CLASS(quiver_navigation_control_parent_class)->realize(widget);
}

static void quiver_navigation_control_unrealize(GtkWidget *widget) {
    GTK_WIDGET_CLASS(quiver_navigation_control_parent_class)->unrealize(widget);
}

static void quiver_navigation_control_map(GtkWidget *widget) {
    QuiverNavigationControl *navcontrol = QUIVER_NAVIGATION_CONTROL(widget);
    QuiverNavigationControlPrivate *priv = quiver_navigation_control_get_instance_private(navcontrol);
    GTK_WIDGET_CLASS(quiver_navigation_control_parent_class)->map(widget);

    gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(g_object_ref(priv->click_gesture)));
    gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(g_object_ref(priv->drag_gesture)));
}

static void quiver_navigation_control_unmap(GtkWidget *widget) {
    QuiverNavigationControl *navcontrol = QUIVER_NAVIGATION_CONTROL(widget);
    QuiverNavigationControlPrivate *priv = quiver_navigation_control_get_instance_private(navcontrol);

    gtk_widget_remove_controller(widget, GTK_EVENT_CONTROLLER(priv->click_gesture));
    gtk_widget_remove_controller(widget, GTK_EVENT_CONTROLLER(priv->drag_gesture));

    GTK_WIDGET_CLASS(quiver_navigation_control_parent_class)->unmap(widget);
}

static void quiver_navigation_control_size_allocate(GtkWidget *widget, int width, int height, int baseline) {
    GTK_WIDGET_CLASS(quiver_navigation_control_parent_class)->size_allocate(widget, width, height, baseline);
	quiver_navigation_control_update_view_area_rect(QUIVER_NAVIGATION_CONTROL(widget));
}

static void quiver_navigation_control_measure(GtkWidget *widget, GtkOrientation orientation, int for_size, int *minimum, int *natural, int *minimum_baseline, int *natural_baseline) {
	QuiverNavigationControlPrivate *priv = quiver_navigation_control_get_instance_private(QUIVER_NAVIGATION_CONTROL(widget));
    gint p_width = 50;
    gint p_height = 50;

    if (priv->pixbuf) {
        p_width = gdk_pixbuf_get_width(priv->pixbuf);
        p_height = gdk_pixbuf_get_height(priv->pixbuf);
    }

    if (orientation == GTK_ORIENTATION_HORIZONTAL) {
        *minimum = *natural = p_width;
    } else {
        *minimum = *natural = p_height;
    }

    if (minimum_baseline) *minimum_baseline = -1;
    if (natural_baseline) *natural_baseline = -1;
}

static void quiver_navigation_control_snapshot(GtkWidget *widget, GtkSnapshot *snapshot) {
	QuiverNavigationControl *navcontrol = QUIVER_NAVIGATION_CONTROL(widget);
	QuiverNavigationControlPrivate *priv = quiver_navigation_control_get_instance_private(navcontrol);
    int width = gtk_widget_get_width(widget);
    int height = gtk_widget_get_height(widget);
    GdkRGBA bg_color;

    bg_color.red = 0.9f; bg_color.green = 0.9f; bg_color.blue = 0.9f; bg_color.alpha = 1.0f;
    gtk_snapshot_append_color(snapshot, &bg_color, &GRAPHENE_RECT_INIT(0,0,(float)width,(float)height));

	if (priv->pixbuf) {
        int pixbuf_w = gdk_pixbuf_get_width(priv->pixbuf);
        int pixbuf_h = gdk_pixbuf_get_height(priv->pixbuf);
        double scale_x = (double)width / pixbuf_w;
        double scale_y = (double)height / pixbuf_h;
        double scale = MIN(scale_x, scale_y);

        int scaled_w = (int)(pixbuf_w * scale);
        int scaled_h = (int)(pixbuf_h * scale);
        int dest_x = (width - scaled_w) / 2;
        int dest_y = (height - scaled_h) / 2;

        GdkPixbuf *scaled_pb = gdk_pixbuf_scale_simple(priv->pixbuf, scaled_w, scaled_h, GDK_INTERP_BILINEAR);
        if (scaled_pb) {
            cairo_t *cr_pb = gtk_snapshot_append_cairo(snapshot, &GRAPHENE_RECT_INIT((float)dest_x, (float)dest_y, (float)scaled_w, (float)scaled_h));
            gdk_cairo_set_source_pixbuf(cr_pb, scaled_pb, 0, 0);
            cairo_paint(cr_pb);
            cairo_destroy(cr_pb);
            g_object_unref(scaled_pb);
        }


        if (priv->view_area_rect.x != -1) {
            cairo_t *cr_rect = gtk_snapshot_append_cairo(snapshot, &GRAPHENE_RECT_INIT(0,0,(float)width,(float)height));
            GdkRGBA rect_fill_color, rect_stroke_color;
            rect_fill_color.red = 0.3f; rect_fill_color.green = 0.3f; rect_fill_color.blue = 1.0f; rect_fill_color.alpha = 0.3f;
            rect_stroke_color.red = 0.2f; rect_stroke_color.green = 0.2f; rect_stroke_color.blue = 0.8f; rect_stroke_color.alpha = 0.7f;

            double scaled_rect_x = dest_x + (priv->view_area_rect.x * scale);
            double scaled_rect_y = dest_y + (priv->view_area_rect.y * scale);
            double scaled_rect_w = priv->view_area_rect.width * scale;
            double scaled_rect_h = priv->view_area_rect.height * scale;

            gdk_cairo_set_source_rgba(cr_rect, &rect_fill_color);
            cairo_rectangle(cr_rect, scaled_rect_x, scaled_rect_y, scaled_rect_w, scaled_rect_h);
            cairo_fill_preserve(cr_rect);

            gdk_cairo_set_source_rgba(cr_rect, &rect_stroke_color);
            cairo_set_line_width(cr_rect, 1.0);
            cairo_stroke(cr_rect);
            cairo_destroy(cr_rect);
        }
	}
}

static void quiver_navigation_control_set_hadjustment(QuiverNavigationControl *navcontrol, GtkAdjustment *hadjustment) {
	QuiverNavigationControlPrivate *priv = quiver_navigation_control_get_instance_private(navcontrol);
	if (priv->hadjustment && priv->hadjustment != hadjustment) {
		g_signal_handlers_disconnect_by_func(priv->hadjustment, G_CALLBACK(quiver_navigation_control_adjustment_value_or_page_changed), navcontrol);
		g_object_unref(priv->hadjustment);
        priv->hadjustment = NULL;
	}
    if (hadjustment) {
        priv->hadjustment = g_object_ref(hadjustment);
        g_signal_connect_object(priv->hadjustment, "value-changed", G_CALLBACK(quiver_navigation_control_adjustment_value_or_page_changed), navcontrol, 0);
        g_signal_connect_object(priv->hadjustment, "changed", G_CALLBACK(quiver_navigation_control_adjustment_value_or_page_changed), navcontrol, 0);
    } else {
        priv->hadjustment = new_default_adjustment();
    }
	quiver_navigation_control_update_view_area_rect(navcontrol);
	g_object_notify_by_pspec(G_OBJECT(navcontrol), g_object_class_find_property(G_OBJECT_GET_CLASS(navcontrol),"hadjustment"));
}

static void quiver_navigation_control_set_vadjustment(QuiverNavigationControl *navcontrol, GtkAdjustment *vadjustment) {
	QuiverNavigationControlPrivate *priv = quiver_navigation_control_get_instance_private(navcontrol);
	if (priv->vadjustment && priv->vadjustment != vadjustment) {
		g_signal_handlers_disconnect_by_func(priv->vadjustment, G_CALLBACK(quiver_navigation_control_adjustment_value_or_page_changed), navcontrol);
		g_object_unref(priv->vadjustment);
        priv->vadjustment = NULL;
	}
    if (vadjustment) {
        priv->vadjustment = g_object_ref(vadjustment);
        g_signal_connect_object(priv->vadjustment, "value-changed", G_CALLBACK(quiver_navigation_control_adjustment_value_or_page_changed), navcontrol, 0);
        g_signal_connect_object(priv->vadjustment, "changed", G_CALLBACK(quiver_navigation_control_adjustment_value_or_page_changed), navcontrol, 0);
    } else {
        priv->vadjustment = new_default_adjustment();
    }
	quiver_navigation_control_update_view_area_rect(navcontrol);
	g_object_notify_by_pspec(G_OBJECT(navcontrol), g_object_class_find_property(G_OBJECT_GET_CLASS(navcontrol),"vadjustment"));
}


static void quiver_navigation_control_adjustment_value_or_page_changed(GtkAdjustment *adjustment, gpointer userdata) {
    quiver_navigation_control_update_view_area_rect(QUIVER_NAVIGATION_CONTROL(userdata));
}


static void quiver_navigation_control_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
	QuiverNavigationControl *navcontrol = QUIVER_NAVIGATION_CONTROL(object);
    QuiverNavigationControlPrivate *priv = quiver_navigation_control_get_instance_private(navcontrol);
	switch (prop_id) {
		case PROP_HADJUSTMENT:
			quiver_navigation_control_set_hadjustment(navcontrol, (GtkAdjustment*)g_value_get_object(value));
			break;
		case PROP_VADJUSTMENT:
			quiver_navigation_control_set_vadjustment(navcontrol, (GtkAdjustment*)g_value_get_object(value));
			break;
		case PROP_HSCROLL_POLICY:
            priv->hscroll_policy = g_value_get_enum(value);
			break;
		case PROP_VSCROLL_POLICY:
            priv->vscroll_policy = g_value_get_enum(value);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}

static void quiver_navigation_control_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
	QuiverNavigationControlPrivate *priv = quiver_navigation_control_get_instance_private(QUIVER_NAVIGATION_CONTROL(object));
	switch (prop_id) {
		case PROP_HADJUSTMENT:
			g_value_set_object(value, priv->hadjustment);
			break;
		case PROP_VADJUSTMENT:
			g_value_set_object(value, priv->vadjustment);
			break;
        case PROP_HSCROLL_POLICY:
            g_value_set_enum(value, priv->hscroll_policy);
            break;
        case PROP_VSCROLL_POLICY:
            g_value_set_enum(value, priv->vscroll_policy);
            break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}

static void quiver_navigation_control_update_view_area_rect(QuiverNavigationControl *navcontrol) {
    QuiverNavigationControlPrivate *priv = quiver_navigation_control_get_instance_private(navcontrol);
    GtkWidget *widget = GTK_WIDGET(navcontrol);

    if (!priv->pixbuf || !priv->hadjustment || !priv->vadjustment || !gtk_widget_get_visible(widget)) {
        if (priv->view_area_rect.x != -1) {
            priv->view_area_rect.x = -1;
            gtk_widget_queue_draw(GTK_WIDGET(navcontrol));
        }
        return;
    }

    gdouble h_value = gtk_adjustment_get_value(priv->hadjustment);
    gdouble h_page_size = gtk_adjustment_get_page_size(priv->hadjustment);
    gdouble h_upper = gtk_adjustment_get_upper(priv->hadjustment);

    gdouble v_value = gtk_adjustment_get_value(priv->vadjustment);
    gdouble v_page_size = gtk_adjustment_get_page_size(priv->vadjustment);
    gdouble v_upper = gtk_adjustment_get_upper(priv->vadjustment);

    gint full_pixbuf_w = gdk_pixbuf_get_width(priv->pixbuf);
    gint full_pixbuf_h = gdk_pixbuf_get_height(priv->pixbuf);

    if (h_upper <= 0 || v_upper <= 0 || full_pixbuf_w <= 0 || full_pixbuf_h <=0 ) {
        if (priv->view_area_rect.x != -1) {
            priv->view_area_rect.x = -1;
            gtk_widget_queue_draw(GTK_WIDGET(navcontrol));
        }
        return;
    }

    priv->view_area_rect.x = (h_value / h_upper) * full_pixbuf_w;
    priv->view_area_rect.y = (v_value / v_upper) * full_pixbuf_h;
    priv->view_area_rect.width = (h_page_size / h_upper) * full_pixbuf_w;
    priv->view_area_rect.height = (v_page_size / v_upper) * full_pixbuf_h;

    if (priv->view_area_rect.x < 0) priv->view_area_rect.x = 0;
    if (priv->view_area_rect.y < 0) priv->view_area_rect.y = 0;
    if (priv->view_area_rect.x + priv->view_area_rect.width > full_pixbuf_w) {
        priv->view_area_rect.width = full_pixbuf_w - priv->view_area_rect.x;
    }
    if (priv->view_area_rect.y + priv->view_area_rect.height > full_pixbuf_h) {
        priv->view_area_rect.height = full_pixbuf_h - priv->view_area_rect.y;
    }
    if (priv->view_area_rect.width < 0) priv->view_area_rect.width = 0;
    if (priv->view_area_rect.height < 0) priv->view_area_rect.height = 0;


    gtk_widget_queue_draw(GTK_WIDGET(navcontrol));
}

static void quiver_navigation_control_update_adjustments_from_xy(QuiverNavigationControl *navcontrol, double event_x, double event_y) {
    QuiverNavigationControlPrivate *priv = quiver_navigation_control_get_instance_private(navcontrol);
    GtkWidget *widget = GTK_WIDGET(navcontrol);

    if (!priv->pixbuf || !priv->hadjustment || !priv->vadjustment) return;

    int widget_w = gtk_widget_get_width(widget);
    int widget_h = gtk_widget_get_height(widget);
    int pixbuf_w = gdk_pixbuf_get_width(priv->pixbuf);
    int pixbuf_h = gdk_pixbuf_get_height(priv->pixbuf);

    if (pixbuf_w == 0 || pixbuf_h == 0) return;

    double scale_x_factor = (double)widget_w / pixbuf_w;
    double scale_y_factor = (double)widget_h / pixbuf_h;
    double scale = MIN(scale_x_factor, scale_y_factor);

    int scaled_pb_w = (int)(pixbuf_w * scale);
    int scaled_pb_h = (int)(pixbuf_h * scale);
    int dest_x_offset = (widget_w - scaled_pb_w) / 2;
    int dest_y_offset = (widget_h - scaled_pb_h) / 2;

    double pb_event_x = event_x - dest_x_offset;
    double pb_event_y = event_y - dest_y_offset;

    double original_pb_x = pb_event_x / scale;
    double original_pb_y = pb_event_y / scale;


    gdouble target_h_value = (original_pb_x / pixbuf_w) * gtk_adjustment_get_upper(priv->hadjustment) - gtk_adjustment_get_page_size(priv->hadjustment) / 2.0;
    gdouble target_v_value = (original_pb_y / pixbuf_h) * gtk_adjustment_get_upper(priv->vadjustment) - gtk_adjustment_get_page_size(priv->vadjustment) / 2.0;

    target_h_value = CLAMP(target_h_value, gtk_adjustment_get_lower(priv->hadjustment), gtk_adjustment_get_upper(priv->hadjustment) - gtk_adjustment_get_page_size(priv->hadjustment));
    target_v_value = CLAMP(target_v_value, gtk_adjustment_get_lower(priv->vadjustment), gtk_adjustment_get_upper(priv->vadjustment) - gtk_adjustment_get_page_size(priv->vadjustment));

    gtk_adjustment_set_value(priv->hadjustment, target_h_value);
    gtk_adjustment_set_value(priv->vadjustment, target_v_value);
}

static void quiver_navigation_control_handle_click_pressed(GtkGestureClick *gesture, int n_press, double x, double y, QuiverNavigationControl *navcontrol) {
    if (gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture)) == GDK_BUTTON_PRIMARY) {

        if (!gtk_widget_has_focus(GTK_WIDGET(navcontrol))) {
            gtk_widget_grab_focus(GTK_WIDGET(navcontrol));
        }
        quiver_navigation_control_update_adjustments_from_xy(navcontrol, x, y);
    }
}

static void quiver_navigation_control_handle_drag_begin(GtkGestureDrag *gesture, double start_x, double start_y, QuiverNavigationControl *navcontrol){
    QuiverNavigationControlPrivate *priv = quiver_navigation_control_get_instance_private(navcontrol);
    if (gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture)) == GDK_BUTTON_PRIMARY) {
        if (!gtk_widget_has_focus(GTK_WIDGET(navcontrol))) {
            gtk_widget_grab_focus(GTK_WIDGET(navcontrol));
        }
        priv->drag_start_x = start_x;
        priv->drag_start_y = start_y;
        quiver_navigation_control_update_adjustments_from_xy(navcontrol, start_x, start_y);
        gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    } else {
        gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_DENIED);
    }
}

static void quiver_navigation_control_handle_drag_update(GtkGestureDrag *gesture, double offset_x, double offset_y, QuiverNavigationControl *navcontrol) {
    QuiverNavigationControlPrivate *priv = quiver_navigation_control_get_instance_private(navcontrol);
    if (gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture)) == GDK_BUTTON_PRIMARY) {
        double current_x = priv->drag_start_x + offset_x;
        double current_y = priv->drag_start_y + offset_y;
        quiver_navigation_control_update_adjustments_from_xy(navcontrol, current_x, current_y);
    }
}
static void quiver_navigation_control_handle_drag_end(GtkGestureDrag *gesture, double offset_x, double offset_y, QuiverNavigationControl *navcontrol) {
    if (gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture)) == GDK_BUTTON_PRIMARY) {
    }
}


/* Public API */
GtkWidget *quiver_navigation_control_new (void) {
	return g_object_new(QUIVER_TYPE_NAVIGATION_CONTROL, NULL);
}

GtkWidget *quiver_navigation_control_new_with_adjustments (GtkAdjustment *hadjust, GtkAdjustment *vadjust) {
	return g_object_new(QUIVER_TYPE_NAVIGATION_CONTROL, "hadjustment", hadjust, "vadjustment" , vadjust , NULL);
}

void quiver_navigation_control_set_pixbuf (QuiverNavigationControl *navcontrol, GdkPixbuf *pixbuf) {
	g_return_if_fail (QUIVER_IS_NAVIGATION_CONTROL (navcontrol));
	QuiverNavigationControlPrivate *priv = quiver_navigation_control_get_instance_private(navcontrol);

	if (priv->pixbuf == pixbuf && pixbuf != NULL) return;

    g_clear_object(&priv->pixbuf);
	priv->pixbuf = pixbuf ? g_object_ref(pixbuf) : NULL;

    gtk_widget_queue_resize(GTK_WIDGET(navcontrol));
	quiver_navigation_control_update_view_area_rect(navcontrol);
}

GtkAdjustment * quiver_navigation_control_get_hadjustment(QuiverNavigationControl *navcontrol)
{
	g_return_val_if_fail (QUIVER_IS_NAVIGATION_CONTROL (navcontrol), NULL);
	QuiverNavigationControlPrivate *priv = quiver_navigation_control_get_instance_private(navcontrol);
	return priv->hadjustment;
}

GtkAdjustment * quiver_navigation_control_get_vadjustment(QuiverNavigationControl *navcontrol)
{
	g_return_val_if_fail (QUIVER_IS_NAVIGATION_CONTROL (navcontrol), NULL);
	QuiverNavigationControlPrivate *priv = quiver_navigation_control_get_instance_private(navcontrol);
	return priv->vadjustment;
}
