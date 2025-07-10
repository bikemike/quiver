#include <config.h>
#include <gtk/gtk.h>
// #include <gdk/gdkkeysyms.h> // Not strictly needed if not handling keys directly, else use gdkkeysyms-compat.h
#include "quiver-navigation-control.h"
#include "quiver-marshallers.h" // If custom signals are used
#include <math.h>
// #include <sys/time.h> // Not used in this file
#include "quiver-pixbuf-utils.h" // If any pixbuf utilities are used

#define QUIVER_PARAM_READWRITE G_PARAM_READWRITE|G_PARAM_STATIC_STRINGS

G_DEFINE_TYPE_WITH_CODE(QuiverNavigationControl,quiver_navigation_control,GTK_TYPE_WIDGET, G_IMPLEMENT_INTERFACE(GTK_TYPE_SCROLLABLE, NULL));

/* Signals - If any custom signals are needed, define them here */
// enum {
// 	SIGNAL_NAV_ACTION, // Example
// 	SIGNAL_COUNT
// };
// static guint navcontrol_signals[SIGNAL_COUNT] = {0};


/* Properties */
enum {
   PROP_0,
   PROP_HADJUSTMENT,
   PROP_VADJUSTMENT,
   PROP_HSCROLL_POLICY,
   PROP_VSCROLL_POLICY,
   // Add other properties here if needed
};

struct _QuiverNavigationControlPrivate
{
	GtkAdjustment *hadjustment;
	GtkAdjustment *vadjustment;
	guint hscroll_policy : 1;
	guint vscroll_policy : 1;

	GdkPixbuf *pixbuf; // The overview pixbuf
	cairo_rectangle_int_t view_area_rect; // Represents the visible area on the pixbuf

    // Event controllers if direct input is handled by this widget
    GtkGesture *click_gesture; // For handling clicks to navigate
    GtkGesture *drag_gesture;  // For dragging the view_area_rect
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
static void quiver_navigation_control_adjustment_changed(GtkAdjustment *adjustment, gpointer userdata);
static void quiver_navigation_control_adjustment_value_or_page_changed(GtkAdjustment *adjustment, gpointer userdata);


static void quiver_navigation_control_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void quiver_navigation_control_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static void quiver_navigation_control_finalize(GObject *object);

static void quiver_navigation_control_update_view_area_rect(QuiverNavigationControl *navcontrol);
static void quiver_navigation_control_handle_press(GtkGestureClick *gesture, int n_press, double x, double y, QuiverNavigationControl *navcontrol);
static void quiver_navigation_control_handle_drag_update(GtkGestureDrag *gesture, double offset_x, double offset_y, QuiverNavigationControl *navcontrol);
static void quiver_navigation_control_handle_drag_begin(GtkGestureDrag *gesture, double start_x, double start_y, QuiverNavigationControl *navcontrol);


static void quiver_navigation_control_class_init(QuiverNavigationControlClass *klass) {
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);
	GObjectClass *obj_class = G_OBJECT_CLASS(klass);

	widget_class->realize = quiver_navigation_control_realize;
    widget_class->unrealize = quiver_navigation_control_unrealize;
    widget_class->map = quiver_navigation_control_map;
    widget_class->unmap = quiver_navigation_control_unmap;
	widget_class->size_allocate = quiver_navigation_control_size_allocate;
	widget_class->measure = quiver_navigation_control_measure;
	gtk_widget_class_set_draw_func(widget_class, quiver_navigation_control_snapshot);

	obj_class->set_property = quiver_navigation_control_set_property;
	obj_class->get_property = quiver_navigation_control_get_property;
	obj_class->finalize = quiver_navigation_control_finalize;

	g_object_class_override_property(obj_class, PROP_HADJUSTMENT, "hadjustment");
	g_object_class_override_property(obj_class, PROP_VADJUSTMENT, "vadjustment");
	g_object_class_override_property(obj_class, PROP_HSCROLL_POLICY, "hscroll-policy");
	g_object_class_override_property(obj_class, PROP_VSCROLL_POLICY, "vscroll-policy");
}

static GtkAdjustment* new_default_adjustment(void) {
    return gtk_adjustment_new(0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
}

static void quiver_navigation_control_init(QuiverNavigationControl *navcontrol) {
	QuiverNavigationControlPrivate *priv = quiver_navigation_control_get_instance_private(navcontrol);
    navcontrol->priv = priv;

	priv->pixbuf = NULL;
	priv->view_area_rect.x = -1; // Indicates invalid/uninitialized

    priv->click_gesture = gtk_gesture_click_new();
    g_signal_connect(priv->click_gesture, "pressed", G_CALLBACK(quiver_navigation_control_handle_press), navcontrol);
    // No "released" needed if press is enough to trigger navigation update

    priv->drag_gesture = gtk_gesture_drag_new();
    g_signal_connect(priv->drag_gesture, "drag-begin", G_CALLBACK(quiver_navigation_control_handle_drag_begin), navcontrol);
    g_signal_connect(priv->drag_gesture, "drag-update", G_CALLBACK(quiver_navigation_control_handle_drag_update), navcontrol);
    // No "drag-end" needed if update is enough

	gtk_widget_set_can_focus(GTK_WIDGET(navcontrol), TRUE);
}

static void quiver_navigation_control_finalize(GObject *object) {
	QuiverNavigationControl *navcontrol = QUIVER_NAVIGATION_CONTROL(object);
	QuiverNavigationControlPrivate *priv = navcontrol->priv;

	if (priv->hadjustment) {
        g_signal_handlers_disconnect_by_func(priv->hadjustment, G_CALLBACK(quiver_navigation_control_adjustment_value_or_page_changed), navcontrol);
		g_object_unref(priv->hadjustment);
		priv->hadjustment = NULL;
	}
	if (priv->vadjustment) {
        g_signal_handlers_disconnect_by_func(priv->vadjustment, G_CALLBACK(quiver_navigation_control_adjustment_value_or_page_changed), navcontrol);
		g_object_unref(priv->vadjustment);
		priv->vadjustment = NULL;
	}
	if (priv->pixbuf) {
		g_object_unref(priv->pixbuf);
		priv->pixbuf = NULL;
	}
    if (priv->click_gesture) { g_object_unref(priv->click_gesture); priv->click_gesture = NULL; }
    if (priv->drag_gesture) { g_object_unref(priv->drag_gesture); priv->drag_gesture = NULL; }

	G_OBJECT_CLASS(quiver_navigation_control_parent_class)->finalize(object);
}

static void quiver_navigation_control_realize(GtkWidget *widget) {
    GTK_WIDGET_CLASS(quiver_navigation_control_parent_class)->realize(widget);
    // No special realization needed beyond parent
}

static void quiver_navigation_control_unrealize(GtkWidget *widget) {
    GTK_WIDGET_CLASS(quiver_navigation_control_parent_class)->unrealize(widget);
}

static void quiver_navigation_control_map(GtkWidget *widget) {
    QuiverNavigationControl *navcontrol = QUIVER_NAVIGATION_CONTROL(widget);
    QuiverNavigationControlPrivate *priv = navcontrol->priv;
    GTK_WIDGET_CLASS(quiver_navigation_control_parent_class)->map(widget);
    gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(g_object_ref(priv->click_gesture)));
    gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(g_object_ref(priv->drag_gesture)));
    gtk_gesture_set_state(priv->click_gesture, GTK_EVENT_SEQUENCE_CLAIMED);
    gtk_gesture_set_state(priv->drag_gesture, GTK_EVENT_SEQUENCE_CLAIMED);
}

static void quiver_navigation_control_unmap(GtkWidget *widget) {
    QuiverNavigationControl *navcontrol = QUIVER_NAVIGATION_CONTROL(widget);
    QuiverNavigationControlPrivate *priv = navcontrol->priv;
    gtk_widget_remove_controller(widget, GTK_EVENT_CONTROLLER(priv->click_gesture));
    gtk_widget_remove_controller(widget, GTK_EVENT_CONTROLLER(priv->drag_gesture));
    GTK_WIDGET_CLASS(quiver_navigation_control_parent_class)->unmap(widget);
}

static void quiver_navigation_control_size_allocate(GtkWidget *widget, int width, int height, int baseline) {
    GTK_WIDGET_CLASS(quiver_navigation_control_parent_class)->size_allocate(widget, width, height, baseline);
	quiver_navigation_control_update_view_area_rect(QUIVER_NAVIGATION_CONTROL(widget));
    gtk_widget_queue_draw(widget);
}

static void quiver_navigation_control_measure(GtkWidget *widget, GtkOrientation orientation, int for_size, int *minimum, int *natural, int *minimum_baseline, int *natural_baseline) {
	QuiverNavigationControlPrivate *priv = QUIVER_NAVIGATION_CONTROL(widget)->priv;
    if (priv->pixbuf) {
        if (orientation == GTK_ORIENTATION_HORIZONTAL) {
            *minimum = *natural = gdk_pixbuf_get_width(priv->pixbuf);
        } else {
            *minimum = *natural = gdk_pixbuf_get_height(priv->pixbuf);
        }
    } else {
        *minimum = *natural = 50; // Default size if no pixbuf
    }
    if (minimum_baseline) *minimum_baseline = -1;
    if (natural_baseline) *natural_baseline = -1;
}

static void quiver_navigation_control_snapshot(GtkWidget *widget, GtkSnapshot *snapshot) {
	QuiverNavigationControl *navcontrol = QUIVER_NAVIGATION_CONTROL(widget);
	QuiverNavigationControlPrivate *priv = navcontrol->priv;
    int width = gtk_widget_get_width(widget);
    int height = gtk_widget_get_height(widget);
    GtkStyleContext *context = gtk_widget_get_style_context(widget);
    GdkRGBA bg_color;

    if (!gtk_style_context_lookup_color(context, "theme_bg_color", &bg_color)) {
        if (!gtk_style_context_lookup_color(context, "background_color", &bg_color)) {
            gdk_rgba_parse(&bg_color, "rgb(220,220,220)"); // Slightly different fallback for visual distinction if needed
        }
    }
    gtk_snapshot_append_color(snapshot, &bg_color, &GRAPHENE_RECT_INIT(0,0,width,height));

	if (priv->pixbuf) {
        cairo_t *cr = gtk_snapshot_append_cairo(snapshot, &GRAPHENE_RECT_INIT(0,0,width,height));
        gdk_cairo_set_source_pixbuf(cr, priv->pixbuf, 0, 0);
        cairo_paint(cr);

        if (priv->view_area_rect.x != -1) { // If valid
            // Draw the view_area_rect with some transparency or outline
            GdkRGBA rect_color;
            // Example: semi-transparent white rectangle
            rect_color.red = 1.0; rect_color.green = 1.0; rect_color.blue = 1.0; rect_color.alpha = 0.3;
            gdk_cairo_set_source_rgba(cr, &rect_color);
            cairo_rectangle(cr, priv->view_area_rect.x, priv->view_area_rect.y, priv->view_area_rect.width, priv->view_area_rect.height);
            cairo_fill_preserve(cr); // Fill
            rect_color.alpha = 0.7; // More opaque for border
            gdk_cairo_set_source_rgba(cr, &rect_color);
            cairo_set_line_width(cr, 1.0);
            cairo_stroke(cr); // Stroke
        }
        cairo_destroy(cr);
	}
}

static void quiver_navigation_control_set_hadjustment(QuiverNavigationControl *navcontrol, GtkAdjustment *hadjustment) {
	QuiverNavigationControlPrivate *priv = navcontrol->priv;
	if (priv->hadjustment && priv->hadjustment != hadjustment) {
		g_signal_handlers_disconnect_by_func(priv->hadjustment, G_CALLBACK(quiver_navigation_control_adjustment_value_or_page_changed), navcontrol);
		g_object_unref(priv->hadjustment);
	}
    priv->hadjustment = hadjustment ? g_object_ref(hadjustment) : new_default_adjustment();
	g_signal_connect_object(priv->hadjustment, "value-changed", G_CALLBACK(quiver_navigation_control_adjustment_value_or_page_changed), navcontrol, 0);
    g_signal_connect_object(priv->hadjustment, "changed", G_CALLBACK(quiver_navigation_control_adjustment_value_or_page_changed), navcontrol, 0); // For page_size, upper, lower changes
	quiver_navigation_control_update_view_area_rect(navcontrol);
	g_object_notify(G_OBJECT(navcontrol), "hadjustment");
}

static void quiver_navigation_control_set_vadjustment(QuiverNavigationControl *navcontrol, GtkAdjustment *vadjustment) {
	QuiverNavigationControlPrivate *priv = navcontrol->priv;
	if (priv->vadjustment && priv->vadjustment != vadjustment) {
		g_signal_handlers_disconnect_by_func(priv->vadjustment, G_CALLBACK(quiver_navigation_control_adjustment_value_or_page_changed), navcontrol);
		g_object_unref(priv->vadjustment);
	}
    priv->vadjustment = vadjustment ? g_object_ref(vadjustment) : new_default_adjustment();
	g_signal_connect_object(priv->vadjustment, "value-changed", G_CALLBACK(quiver_navigation_control_adjustment_value_or_page_changed), navcontrol, 0);
    g_signal_connect_object(priv->vadjustment, "changed", G_CALLBACK(quiver_navigation_control_adjustment_value_or_page_changed), navcontrol, 0);
	quiver_navigation_control_update_view_area_rect(navcontrol);
	g_object_notify(G_OBJECT(navcontrol), "vadjustment");
}

static void quiver_navigation_control_adjustment_value_or_page_changed(GtkAdjustment *adjustment, gpointer userdata) {
    quiver_navigation_control_update_view_area_rect(QUIVER_NAVIGATION_CONTROL(userdata));
}


static void quiver_navigation_control_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
	QuiverNavigationControl *navcontrol = QUIVER_NAVIGATION_CONTROL(object);
	switch (prop_id) {
		case PROP_HADJUSTMENT:
			quiver_navigation_control_set_hadjustment(navcontrol, (GtkAdjustment*)g_value_get_object(value));
			break;
		case PROP_VADJUSTMENT:
			quiver_navigation_control_set_vadjustment(navcontrol, (GtkAdjustment*)g_value_get_object(value));
			break;
		case PROP_HSCROLL_POLICY: // These policies don't directly affect this simple widget
		case PROP_VSCROLL_POLICY: // but are part of GtkScrollable interface
			// g_object_notify_by_pspec(object, pspec); // No internal state to change
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}

static void quiver_navigation_control_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
	QuiverNavigationControlPrivate *priv = QUIVER_NAVIGATION_CONTROL(object)->priv;
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
    QuiverNavigationControlPrivate *priv = navcontrol->priv;
    if (!priv->pixbuf || !priv->hadjustment || !priv->vadjustment) {
        priv->view_area_rect.x = -1; // Mark as invalid
        gtk_widget_queue_draw(GTK_WIDGET(navcontrol));
        return;
    }

    gdouble h_upper = gtk_adjustment_get_upper(priv->hadjustment);
    gdouble v_upper = gtk_adjustment_get_upper(priv->vadjustment);

    if (h_upper == 0 || v_upper == 0) { // Avoid division by zero if adjustments not configured
        priv->view_area_rect.x = -1;
        gtk_widget_queue_draw(GTK_WIDGET(navcontrol));
        return;
    }

    gdouble h_value = gtk_adjustment_get_value(priv->hadjustment);
    gdouble v_value = gtk_adjustment_get_value(priv->vadjustment);
    gdouble h_page_size = gtk_adjustment_get_page_size(priv->hadjustment);
    gdouble v_page_size = gtk_adjustment_get_page_size(priv->vadjustment);

    gint pixbuf_w = gdk_pixbuf_get_width(priv->pixbuf);
    gint pixbuf_h = gdk_pixbuf_get_height(priv->pixbuf);

    priv->view_area_rect.x = (h_value / h_upper) * pixbuf_w;
    priv->view_area_rect.y = (v_value / v_upper) * pixbuf_h;
    priv->view_area_rect.width = (h_page_size / h_upper) * pixbuf_w;
    priv->view_area_rect.height = (v_page_size / v_upper) * pixbuf_h;

    // Clamp to pixbuf boundaries
    if (priv->view_area_rect.x < 0) priv->view_area_rect.x = 0;
    if (priv->view_area_rect.y < 0) priv->view_area_rect.y = 0;
    if (priv->view_area_rect.x + priv->view_area_rect.width > pixbuf_w) priv->view_area_rect.width = pixbuf_w - priv->view_area_rect.x;
    if (priv->view_area_rect.y + priv->view_area_rect.height > pixbuf_h) priv->view_area_rect.height = pixbuf_h - priv->view_area_rect.y;


    gtk_widget_queue_draw(GTK_WIDGET(navcontrol));
}

static void quiver_navigation_control_update_adjustments_from_xy(QuiverNavigationControl *navcontrol, double x, double y) {
    QuiverNavigationControlPrivate *priv = navcontrol->priv;
    if (!priv->pixbuf || !priv->hadjustment || !priv->vadjustment) return;

    gint pixbuf_w = gdk_pixbuf_get_width(priv->pixbuf);
    gint pixbuf_h = gdk_pixbuf_get_height(priv->pixbuf);

    if (pixbuf_w == 0 || pixbuf_h == 0) return;

    // Calculate target adjustment value based on click position, centering the page there
    gdouble target_h_value = (x / pixbuf_w) * gtk_adjustment_get_upper(priv->hadjustment) - gtk_adjustment_get_page_size(priv->hadjustment) / 2.0;
    gdouble target_v_value = (y / pixbuf_h) * gtk_adjustment_get_upper(priv->vadjustment) - gtk_adjustment_get_page_size(priv->vadjustment) / 2.0;

    target_h_value = CLAMP(target_h_value, gtk_adjustment_get_lower(priv->hadjustment), gtk_adjustment_get_upper(priv->hadjustment) - gtk_adjustment_get_page_size(priv->hadjustment));
    target_v_value = CLAMP(target_v_value, gtk_adjustment_get_lower(priv->vadjustment), gtk_adjustment_get_upper(priv->vadjustment) - gtk_adjustment_get_page_size(priv->vadjustment));

    gtk_adjustment_set_value(priv->hadjustment, target_h_value);
    gtk_adjustment_set_value(priv->vadjustment, target_v_value);
}

static void quiver_navigation_control_handle_press(GtkGestureClick *gesture, int n_press, double x, double y, QuiverNavigationControl *navcontrol) {
    // If the press is part of a drag, drag_begin will handle it.
    // This handles simple clicks.
    if (gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture)) == GDK_BUTTON_PRIMARY) {
         if (!gtk_widget_has_focus(GTK_WIDGET(navcontrol))) {
            gtk_widget_grab_focus(GTK_WIDGET(navcontrol));
        }
        quiver_navigation_control_update_adjustments_from_xy(navcontrol, x, y);
    }
}
static void quiver_navigation_control_handle_drag_begin(GtkGestureDrag *gesture, double start_x, double start_y, QuiverNavigationControl *navcontrol){
    if (gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture)) == GDK_BUTTON_PRIMARY) {
        if (!gtk_widget_has_focus(GTK_WIDGET(navcontrol))) {
            gtk_widget_grab_focus(GTK_WIDGET(navcontrol));
        }
        // The actual navigation update happens in drag_update
        // Store initial drag point if needed for specific drag interaction, but simple update is fine
    }
}

static void quiver_navigation_control_handle_drag_update(GtkGestureDrag *gesture, double offset_x, double offset_y, QuiverNavigationControl *navcontrol) {
    if (gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture)) == GDK_BUTTON_PRIMARY) {
        double start_x, start_y;
        gtk_gesture_drag_get_start_point(gesture, &start_x, &start_y);
        quiver_navigation_control_update_adjustments_from_xy(navcontrol, start_x + offset_x, start_y + offset_y);
    }
}


/* Public API */
GtkWidget *quiver_navigation_control_new () {
	return g_object_new(QUIVER_TYPE_NAVIGATION_CONTROL, NULL);
}

GtkWidget *quiver_navigation_control_new_with_adjustments (GtkAdjustment *hadjust, GtkAdjustment *vadjust) {
	g_return_val_if_fail (GTK_IS_ADJUSTMENT (hadjust), NULL);
	g_return_val_if_fail (GTK_IS_ADJUSTMENT (vadjust), NULL);
	return g_object_new(QUIVER_TYPE_NAVIGATION_CONTROL, "hadjustment", hadjust, "vadjustment" , vadjust , NULL);
}

void quiver_navigation_control_set_pixbuf (QuiverNavigationControl *navcontrol, GdkPixbuf *pixbuf) {
	g_return_if_fail (QUIVER_IS_NAVIGATION_CONTROL (navcontrol));
	QuiverNavigationControlPrivate *priv = navcontrol->priv;

	if (priv->pixbuf) {
		g_object_unref(priv->pixbuf);
	}
	priv->pixbuf = pixbuf ? g_object_ref(pixbuf) : NULL;

    if (priv->pixbuf) {
        gtk_widget_set_size_request(GTK_WIDGET(navcontrol),
                                    gdk_pixbuf_get_width(priv->pixbuf),
                                    gdk_pixbuf_get_height(priv->pixbuf));
    } else {
        gtk_widget_set_size_request(GTK_WIDGET(navcontrol), -1, -1); // Reset size request
    }
	quiver_navigation_control_update_view_area_rect(navcontrol);
	gtk_widget_queue_draw(GTK_WIDGET(navcontrol));
}
