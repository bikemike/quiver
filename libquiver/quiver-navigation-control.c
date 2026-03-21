#include <config.h>
#include <gtk/gtk.h>
#include "quiver-navigation-control.h"
#include "quiver-marshallers.h"
#include <math.h>
#include "quiver-pixbuf-utils.h"

struct _QuiverNavigationControl {
	GtkWidget parent_instance;
	GtkAdjustment *hadjustment;
	GtkAdjustment *vadjustment;
	GtkScrollablePolicy hscroll_policy;
	GtkScrollablePolicy vscroll_policy;
	GdkPixbuf *pixbuf;
};

enum {
   PROP_0,
   PROP_HADJUSTMENT,
   PROP_VADJUSTMENT,
   PROP_HSCROLL_POLICY,
   PROP_VSCROLL_POLICY,
};

G_DEFINE_TYPE_WITH_CODE(QuiverNavigationControl, quiver_navigation_control, GTK_TYPE_WIDGET,
                        G_IMPLEMENT_INTERFACE(GTK_TYPE_SCROLLABLE, NULL));

static void quiver_navigation_control_snapshot (GtkWidget *widget, GtkSnapshot *snapshot)
{
	QuiverNavigationControl *self = QUIVER_NAVIGATION_CONTROL (widget);
    if (self->pixbuf) {
        GdkTexture *texture = gdk_texture_new_for_pixbuf (self->pixbuf);
        gtk_snapshot_append_texture (snapshot, texture, &GRAPHENE_RECT_INIT(0, 0, gdk_pixbuf_get_width(self->pixbuf), gdk_pixbuf_get_height(self->pixbuf)));
        g_object_unref (texture);
    }
}

static void quiver_navigation_control_measure (GtkWidget *widget, GtkOrientation orientation, int for_size, int *minimum, int *natural, int *minimum_baseline, int *natural_baseline)
{
    QuiverNavigationControl *self = QUIVER_NAVIGATION_CONTROL(widget);
    if (self->pixbuf) {
        if (orientation == GTK_ORIENTATION_HORIZONTAL) *minimum = *natural = gdk_pixbuf_get_width(self->pixbuf);
        else *minimum = *natural = gdk_pixbuf_get_height(self->pixbuf);
    } else {
        *minimum = *natural = 0;
    }
}

static void quiver_navigation_control_size_allocate (GtkWidget *widget, int width, int height, int baseline) {}

static void quiver_navigation_control_finalize (GObject *object)
{
	QuiverNavigationControl *self = QUIVER_NAVIGATION_CONTROL (object);
	g_clear_object (&self->hadjustment);
	g_clear_object (&self->vadjustment);
	g_clear_object (&self->pixbuf);
	G_OBJECT_CLASS (quiver_navigation_control_parent_class)->finalize (object);
}

static void quiver_navigation_control_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	QuiverNavigationControl *self = QUIVER_NAVIGATION_CONTROL (object);
	switch (prop_id) {
		case PROP_HADJUSTMENT: g_set_object (&self->hadjustment, g_value_get_object (value)); break;
		case PROP_VADJUSTMENT: g_set_object (&self->vadjustment, g_value_get_object (value)); break;
		case PROP_HSCROLL_POLICY: self->hscroll_policy = (GtkScrollablePolicy)g_value_get_enum (value); break;
		case PROP_VSCROLL_POLICY: self->vscroll_policy = (GtkScrollablePolicy)g_value_get_enum (value); break;
		default: G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec); break;
	}
}

static void quiver_navigation_control_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	QuiverNavigationControl *self = QUIVER_NAVIGATION_CONTROL (object);
	switch (prop_id) {
		case PROP_HADJUSTMENT: g_value_set_object (value, self->hadjustment); break;
		case PROP_VADJUSTMENT: g_value_set_object (value, self->vadjustment); break;
		case PROP_HSCROLL_POLICY: g_value_set_enum (value, self->hscroll_policy); break;
		case PROP_VSCROLL_POLICY: g_value_set_enum (value, self->vscroll_policy); break;
		default: G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec); break;
	}
}

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
	g_object_class_override_property (obj_class, PROP_HADJUSTMENT, "hadjustment");
	g_object_class_override_property (obj_class, PROP_VADJUSTMENT, "vadjustment");
	g_object_class_override_property (obj_class, PROP_HSCROLL_POLICY, "hscroll-policy");
	g_object_class_override_property (obj_class, PROP_VSCROLL_POLICY, "vscroll-policy");
}

static void quiver_navigation_control_init (QuiverNavigationControl *self)
{
	gtk_widget_set_focusable (GTK_WIDGET (self), TRUE);
}

GtkWidget *quiver_navigation_control_new (void) { return g_object_new (QUIVER_TYPE_NAVIGATION_CONTROL, NULL); }
GtkWidget *quiver_navigation_control_new_with_adjustments (GtkAdjustment *h, GtkAdjustment *v) { return g_object_new (QUIVER_TYPE_NAVIGATION_CONTROL, "hadjustment", h, "vadjustment", v, NULL); }
void quiver_navigation_control_set_pixbuf (QuiverNavigationControl *self, GdkPixbuf *pixbuf) {
	g_set_object (&self->pixbuf, pixbuf);
    gtk_widget_queue_resize (GTK_WIDGET (self));
}
