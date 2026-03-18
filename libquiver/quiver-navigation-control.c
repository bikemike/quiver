#include <config.h>
#include <gtk/gtk.h>
#include "quiver-navigation-control.h"
#include "quiver-marshallers.h"
#include <math.h>
#include <sys/time.h>
#include "quiver-pixbuf-utils.h"

#define QUIVER_NAVIGATION_CONTROL_GET_PRIVATE(obj) (quiver_navigation_control_get_instance_private (QUIVER_NAVIGATION_CONTROL (obj)))
#define QUIVER_PARAM_READWRITE G_PARAM_READWRITE|G_PARAM_STATIC_STRINGS

enum {
   PROP_0,
   PROP_HADJUSTMENT,
   PROP_VADJUSTMENT,
   PROP_HSCROLL_POLICY,
   PROP_VSCROLL_POLICY,
};

struct _QuiverNavigationControlPrivate {
	GtkAdjustment *hadjustment;
	GtkAdjustment *vadjustment;
	GtkScrollablePolicy hscroll_policy;
	GtkScrollablePolicy vscroll_policy;
	GdkPixbuf *pixbuf;
	cairo_rectangle_int_t view_area_rect;
};

G_DEFINE_TYPE_WITH_CODE(QuiverNavigationControl, quiver_navigation_control, GTK_TYPE_WIDGET,
                        G_ADD_PRIVATE(QuiverNavigationControl)
                        G_IMPLEMENT_INTERFACE(GTK_TYPE_SCROLLABLE, NULL));

static void quiver_navigation_control_snapshot (GtkWidget *widget, GtkSnapshot *snapshot);
static void quiver_navigation_control_measure (GtkWidget *widget, GtkOrientation orientation, int for_size, int *minimum, int *natural, int *minimum_baseline, int *natural_baseline);
static void quiver_navigation_control_size_allocate (GtkWidget *widget, int width, int height, int baseline);
static void quiver_navigation_control_finalize(GObject *object);
static void quiver_navigation_control_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void quiver_navigation_control_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static void quiver_navigation_control_adjustment_changed (GtkAdjustment *adjustment, gpointer userdata);
static void quiver_navigation_control_set_hadjustment (QuiverNavigationControl *navcontrol, GtkAdjustment *hadjustment);
static void quiver_navigation_control_set_vadjustment (QuiverNavigationControl *navcontrol, GtkAdjustment *vadjustment);

static void quiver_navigation_control_class_init (QuiverNavigationControlClass *klass)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
	GObjectClass *obj_class = G_OBJECT_CLASS (klass);
	widget_class->snapshot = quiver_navigation_control_snapshot;
	widget_class->measure = quiver_navigation_control_measure;
	widget_class->size_allocate = quiver_navigation_control_size_allocate;
	obj_class->finalize = quiver_navigation_control_finalize;
	obj_class->set_property = quiver_navigation_control_set_property;
	obj_class->get_property = quiver_navigation_control_get_property;
	g_object_class_install_property (obj_class, PROP_HADJUSTMENT, g_param_spec_object ("hadjustment", "Horizontal adjustment", "Horizontal adjustment", GTK_TYPE_ADJUSTMENT, QUIVER_PARAM_READWRITE));
	g_object_class_install_property (obj_class, PROP_VADJUSTMENT, g_param_spec_object ("vadjustment", "Vertical adjustment", "Vertical adjustment", GTK_TYPE_ADJUSTMENT, QUIVER_PARAM_READWRITE));
	g_object_class_install_property (obj_class, PROP_HSCROLL_POLICY, g_param_spec_enum ("hscroll-policy", "Horizontal Scroll Policy", "Horizontal Scroll Policy", GTK_TYPE_SCROLLABLE_POLICY, GTK_SCROLL_MINIMUM, QUIVER_PARAM_READWRITE));
	g_object_class_install_property (obj_class, PROP_VSCROLL_POLICY, g_param_spec_enum ("vscroll-policy", "Vertical Scroll Policy", "Vertical Scroll Policy", GTK_TYPE_SCROLLABLE_POLICY, GTK_SCROLL_MINIMUM, QUIVER_PARAM_READWRITE));
}

static void quiver_navigation_control_init(QuiverNavigationControl *navcontrol)
{
	navcontrol->priv = QUIVER_NAVIGATION_CONTROL_GET_PRIVATE(navcontrol);
	navcontrol->priv->view_area_rect.x = -1;
	gtk_widget_set_focusable(GTK_WIDGET(navcontrol), TRUE);
}

static void quiver_navigation_control_finalize(GObject *object)
{
	QuiverNavigationControl *navcontrol = QUIVER_NAVIGATION_CONTROL(object);
	if (navcontrol->priv->hadjustment) {
		g_signal_handlers_disconnect_by_func (navcontrol->priv->hadjustment, (void*)quiver_navigation_control_adjustment_changed, navcontrol);
		g_object_unref(navcontrol->priv->hadjustment);
	}
	if (navcontrol->priv->vadjustment) {
		g_signal_handlers_disconnect_by_func (navcontrol->priv->vadjustment, (void*)quiver_navigation_control_adjustment_changed, navcontrol);
		g_object_unref(navcontrol->priv->vadjustment);
	}
	if (navcontrol->priv->pixbuf) g_object_unref(navcontrol->priv->pixbuf);
	G_OBJECT_CLASS (quiver_navigation_control_parent_class)->finalize (object);
}

static void quiver_navigation_control_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	QuiverNavigationControl *navcontrol = QUIVER_NAVIGATION_CONTROL(object);
	switch (prop_id) {
		case PROP_HADJUSTMENT: quiver_navigation_control_set_hadjustment (navcontrol, (GtkAdjustment*)g_value_get_object (value)); break;
		case PROP_VADJUSTMENT: quiver_navigation_control_set_vadjustment (navcontrol, (GtkAdjustment*)g_value_get_object (value)); break;
		case PROP_HSCROLL_POLICY: navcontrol->priv->hscroll_policy = (GtkScrollablePolicy)g_value_get_enum(value); gtk_widget_queue_resize(GTK_WIDGET(navcontrol)); break;
		case PROP_VSCROLL_POLICY: navcontrol->priv->vscroll_policy = (GtkScrollablePolicy)g_value_get_enum(value); gtk_widget_queue_resize(GTK_WIDGET(navcontrol)); break;
		default: G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec); break;
	}
}

static void quiver_navigation_control_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	QuiverNavigationControl *navcontrol = QUIVER_NAVIGATION_CONTROL(object);
	switch (prop_id) {
		case PROP_HADJUSTMENT: g_value_set_object(value, navcontrol->priv->hadjustment); break;
		case PROP_VADJUSTMENT: g_value_set_object(value, navcontrol->priv->vadjustment); break;
		case PROP_HSCROLL_POLICY: g_value_set_enum(value, navcontrol->priv->hscroll_policy); break;
		case PROP_VSCROLL_POLICY: g_value_set_enum(value, navcontrol->priv->vscroll_policy); break;
		default: G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec); break;
	}
}

static void quiver_navigation_control_measure (GtkWidget *widget, GtkOrientation orientation, int for_size, int *minimum, int *natural, int *minimum_baseline, int *natural_baseline)
{
    QuiverNavigationControl *navcontrol = QUIVER_NAVIGATION_CONTROL(widget);
    if (navcontrol->priv->pixbuf) {
        if (orientation == GTK_ORIENTATION_HORIZONTAL) *minimum = *natural = gdk_pixbuf_get_width(navcontrol->priv->pixbuf);
        else *minimum = *natural = gdk_pixbuf_get_height(navcontrol->priv->pixbuf);
    } else {
        *minimum = *natural = 0;
    }
}

static void quiver_navigation_control_size_allocate (GtkWidget *widget, int width, int height, int baseline) {}

static void quiver_navigation_control_snapshot (GtkWidget *widget, GtkSnapshot *snapshot)
{
	QuiverNavigationControl *navcontrol = QUIVER_NAVIGATION_CONTROL(widget);
    if (navcontrol->priv->pixbuf) {
        GdkTexture *texture = gdk_texture_new_for_pixbuf(navcontrol->priv->pixbuf);
        gtk_snapshot_append_texture(snapshot, texture, &GRAPHENE_RECT_INIT(0, 0, gdk_pixbuf_get_width(navcontrol->priv->pixbuf), gdk_pixbuf_get_height(navcontrol->priv->pixbuf)));
        g_object_unref(texture);
    }
}

GtkWidget *quiver_navigation_control_new () { return g_object_new(QUIVER_TYPE_NAVIGATION_CONTROL, NULL); }
GtkWidget *quiver_navigation_control_new_with_adjustments (GtkAdjustment *h, GtkAdjustment *v) { return g_object_new(QUIVER_TYPE_NAVIGATION_CONTROL, "hadjustment", h, "vadjustment", v, NULL); }

void quiver_navigation_control_set_pixbuf (QuiverNavigationControl *navcontrol, GdkPixbuf *pixbuf)
{
	if (navcontrol->priv->pixbuf) g_object_unref(navcontrol->priv->pixbuf);
	navcontrol->priv->pixbuf = pixbuf ? (GdkPixbuf*)g_object_ref(pixbuf) : NULL;
    gtk_widget_queue_resize(GTK_WIDGET(navcontrol));
}

static void quiver_navigation_control_set_hadjustment (QuiverNavigationControl *navcontrol, GtkAdjustment *adj)
{
	if (navcontrol->priv->hadjustment == adj) return;
	if (navcontrol->priv->hadjustment) {
		g_signal_handlers_disconnect_by_func (navcontrol->priv->hadjustment, (void*)quiver_navigation_control_adjustment_changed, navcontrol);
		g_object_unref(navcontrol->priv->hadjustment);
	}
	navcontrol->priv->hadjustment = adj ? (GtkAdjustment*)g_object_ref(adj) : NULL;
	if (navcontrol->priv->hadjustment) {
		g_signal_connect (navcontrol->priv->hadjustment, "value-changed", G_CALLBACK (quiver_navigation_control_adjustment_changed), navcontrol);
		g_signal_connect (navcontrol->priv->hadjustment, "changed", G_CALLBACK (quiver_navigation_control_adjustment_changed), navcontrol);
	}
}

static void quiver_navigation_control_set_vadjustment (QuiverNavigationControl *navcontrol, GtkAdjustment *adj)
{
	if (navcontrol->priv->vadjustment == adj) return;
	if (navcontrol->priv->vadjustment) {
		g_signal_handlers_disconnect_by_func (navcontrol->priv->vadjustment, (void*)quiver_navigation_control_adjustment_changed, navcontrol);
		g_object_unref(navcontrol->priv->vadjustment);
	}
	navcontrol->priv->vadjustment = adj ? (GtkAdjustment*)g_object_ref(adj) : NULL;
	if (navcontrol->priv->vadjustment) {
		g_signal_connect (navcontrol->priv->vadjustment, "value-changed", G_CALLBACK (quiver_navigation_control_adjustment_changed), navcontrol);
		g_signal_connect (navcontrol->priv->vadjustment, "changed", G_CALLBACK (quiver_navigation_control_adjustment_changed), navcontrol);
	}
}

static void quiver_navigation_control_adjustment_changed (GtkAdjustment *adjustment, gpointer userdata)
{
    gtk_widget_queue_draw(GTK_WIDGET(userdata));
}
