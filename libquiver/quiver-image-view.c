#include <config.h>
#include <gtk/gtk.h>
#include <math.h>
#include <string.h>
#include "quiver-pixbuf-utils.h"
#include "quiver-image-view.h"
#include "quiver-marshallers.h"

#define QUIVER_IMAGE_VIEW_MAG_MAX              50.
#define QUIVER_IMAGE_VIEW_MIN_IMAGE_SIZE       32

struct _QuiverImageView
{
	GtkWidget parent_instance;
	GdkPixbuf *pixbuf;
	GdkPixbuf *pixbuf_scaled;
	gint pixbuf_width;
	gint pixbuf_height;
	QuiverImageViewMode view_mode;
	gdouble magnification;
	GtkAdjustment *hadjustment;
	GtkAdjustment *vadjustment;
	GtkScrollablePolicy hscroll_policy;
	GtkScrollablePolicy vscroll_policy;
};

enum {
	SIGNAL_ACTIVATED,
	SIGNAL_RELOAD,
	SIGNAL_MAGNIFICATION_CHANGED,
	SIGNAL_VIEW_MODE_CHANGED,
	SIGNAL_COUNT
};

static guint imageview_signals[SIGNAL_COUNT] = {0};

enum {
   PROP_0,
   PROP_HADJUSTMENT,
   PROP_VADJUSTMENT,
   PROP_HSCROLL_POLICY,
   PROP_VSCROLL_POLICY,
};

G_DEFINE_TYPE_WITH_CODE(QuiverImageView, quiver_image_view, GTK_TYPE_WIDGET,
                        G_IMPLEMENT_INTERFACE(GTK_TYPE_SCROLLABLE, NULL));

static void quiver_image_view_snapshot (GtkWidget *widget, GtkSnapshot *snapshot)
{
	QuiverImageView *self = QUIVER_IMAGE_VIEW (widget);
	int width = gtk_widget_get_width (widget);
	int height = gtk_widget_get_height (widget);

	GdkRGBA black = {0, 0, 0, 1};
	gtk_snapshot_append_color (snapshot, &black, &GRAPHENE_RECT_INIT(0, 0, width, height));

	GdkPixbuf *pixbuf = self->pixbuf_scaled ? self->pixbuf_scaled : self->pixbuf;
	if (pixbuf) {
		int pw = gdk_pixbuf_get_width (pixbuf);
		int ph = gdk_pixbuf_get_height (pixbuf);
		float x = MAX(0, (width - pw) / 2.0f);
		float y = MAX(0, (height - ph) / 2.0f);
		GdkTexture *texture = gdk_texture_new_for_pixbuf (pixbuf);
		gtk_snapshot_append_texture (snapshot, texture, &GRAPHENE_RECT_INIT(x, y, pw, ph));
		g_object_unref (texture);
	}
}

static void quiver_image_view_measure (GtkWidget *widget, GtkOrientation orientation, int for_size, int *minimum, int *natural, int *minimum_baseline, int *natural_baseline)
{
	*minimum = *natural = QUIVER_IMAGE_VIEW_MIN_IMAGE_SIZE;
}

static void quiver_image_view_size_allocate (GtkWidget *widget, int width, int height, int baseline)
{
	QuiverImageView *self = QUIVER_IMAGE_VIEW (widget);
	// Update adjustments and scaled pixbuf here...
}

static void quiver_image_view_finalize (GObject *object)
{
	QuiverImageView *self = QUIVER_IMAGE_VIEW (object);
	g_clear_object (&self->pixbuf);
	g_clear_object (&self->pixbuf_scaled);
	g_clear_object (&self->hadjustment);
	g_clear_object (&self->vadjustment);
	G_OBJECT_CLASS (quiver_image_view_parent_class)->finalize (object);
}

static void quiver_image_view_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	QuiverImageView *self = QUIVER_IMAGE_VIEW (object);
	switch (prop_id) {
		case PROP_HADJUSTMENT:
			g_set_object (&self->hadjustment, g_value_get_object (value));
			break;
		case PROP_VADJUSTMENT:
			g_set_object (&self->vadjustment, g_value_get_object (value));
			break;
		case PROP_HSCROLL_POLICY:
			self->hscroll_policy = (GtkScrollablePolicy)g_value_get_enum (value);
			break;
		case PROP_VSCROLL_POLICY:
			self->vscroll_policy = (GtkScrollablePolicy)g_value_get_enum (value);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

static void quiver_image_view_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	QuiverImageView *self = QUIVER_IMAGE_VIEW (object);
	switch (prop_id) {
		case PROP_HADJUSTMENT: g_value_set_object (value, self->hadjustment); break;
		case PROP_VADJUSTMENT: g_value_set_object (value, self->vadjustment); break;
		case PROP_HSCROLL_POLICY: g_value_set_enum (value, self->hscroll_policy); break;
		case PROP_VSCROLL_POLICY: g_value_set_enum (value, self->vscroll_policy); break;
		default: G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec); break;
	}
}

static void quiver_image_view_class_init (QuiverImageViewClass *klass)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
	GObjectClass *obj_class = G_OBJECT_CLASS (klass);

	widget_class->snapshot = quiver_image_view_snapshot;
	widget_class->measure = quiver_image_view_measure;
	widget_class->size_allocate = quiver_image_view_size_allocate;

	obj_class->finalize = quiver_image_view_finalize;
	obj_class->set_property = quiver_image_view_set_property;
	obj_class->get_property = quiver_image_view_get_property;

	g_object_class_override_property (obj_class, PROP_HADJUSTMENT, "hadjustment");
	g_object_class_override_property (obj_class, PROP_VADJUSTMENT, "vadjustment");
	g_object_class_override_property (obj_class, PROP_HSCROLL_POLICY, "hscroll-policy");
	g_object_class_override_property (obj_class, PROP_VSCROLL_POLICY, "vscroll-policy");

	imageview_signals[SIGNAL_ACTIVATED] = g_signal_new ("activated", G_TYPE_FROM_CLASS (obj_class), G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (QuiverImageViewClass, activated), NULL, NULL, NULL, G_TYPE_NONE, 0);
	imageview_signals[SIGNAL_RELOAD] = g_signal_new ("reload", G_TYPE_FROM_CLASS (obj_class), G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (QuiverImageViewClass, reload), NULL, NULL, NULL, G_TYPE_NONE, 0);
	imageview_signals[SIGNAL_MAGNIFICATION_CHANGED] = g_signal_new ("magnification-changed", G_TYPE_FROM_CLASS (obj_class), G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (QuiverImageViewClass, magnification_changed), NULL, NULL, NULL, G_TYPE_NONE, 0);
	imageview_signals[SIGNAL_VIEW_MODE_CHANGED] = g_signal_new ("view-mode-changed", G_TYPE_FROM_CLASS (obj_class), G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (QuiverImageViewClass, view_mode_changed), NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void quiver_image_view_init (QuiverImageView *self)
{
	self->view_mode = QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW;
	self->magnification = 1.0;
	gtk_widget_set_focusable (GTK_WIDGET (self), TRUE);
}

GtkWidget *quiver_image_view_new (void) { return g_object_new (QUIVER_TYPE_IMAGE_VIEW, NULL); }
void quiver_image_view_set_pixbuf (QuiverImageView *self, GdkPixbuf *pixbuf) {
	g_set_object (&self->pixbuf, pixbuf);
	if (pixbuf) {
		self->pixbuf_width = gdk_pixbuf_get_width (pixbuf);
		self->pixbuf_height = gdk_pixbuf_get_height (pixbuf);
	}
	gtk_widget_queue_draw (GTK_WIDGET (self));
}
void quiver_image_view_set_pixbuf_at_size (QuiverImageView *self, GdkPixbuf *pixbuf, int width, int height) {
	quiver_image_view_set_pixbuf (self, pixbuf);
	self->pixbuf_width = width;
	self->pixbuf_height = height;
}

// Stubs for remaining public API to satisfy header
void quiver_image_view_set_smooth_scroll(QuiverImageView *iv, gboolean s) {}
GdkPixbuf* quiver_image_view_get_pixbuf(QuiverImageView *iv) { return iv->pixbuf; }
void quiver_image_view_set_pixbuf_at_size_ex(QuiverImageView *iv, GdkPixbuf *pb, int w, int h, gboolean r) { quiver_image_view_set_pixbuf_at_size(iv, pb, w, h); }
QuiverImageViewMode quiver_image_view_get_view_mode(QuiverImageView *iv) { return iv->view_mode; }
QuiverImageViewMode quiver_image_view_get_view_mode_unmagnified(QuiverImageView *iv) { return iv->view_mode; }
void quiver_image_view_set_view_mode(QuiverImageView *iv, QuiverImageViewMode m) { iv->view_mode = m; }
void quiver_image_view_reset_view_mode(QuiverImageView *iv, gboolean i) {}
void quiver_image_view_set_enable_transitions(QuiverImageView *iv, gboolean e) {}
gboolean quiver_image_view_is_in_transition(QuiverImageView *iv) { return FALSE; }
void quiver_image_view_set_magnification(QuiverImageView *iv, gdouble m) { iv->magnification = m; }
void quiver_image_view_set_magnification_mode(QuiverImageView *iv, QuiverImageViewMagnificationMode m) {}
gdouble quiver_image_view_get_magnification(QuiverImageView *iv) { return iv->magnification; }
gboolean quiver_image_view_can_magnify(QuiverImageView *iv, gboolean in) { return TRUE; }
void quiver_image_view_get_pixbuf_display_size_for_mode(QuiverImageView *iv, QuiverImageViewMode m, gint *w, gint *h) {}
void quiver_image_view_get_pixbuf_display_size_for_mode_alt(QuiverImageView *iv, QuiverImageViewMode m, gint iw, gint ih, gint *ow, gint *oh) {}
void quiver_image_view_rotate(QuiverImageView *iv, gboolean c) {}
void quiver_image_view_flip(QuiverImageView *iv, gboolean h) {}
void quiver_image_view_connect_pixbuf_loader_signals(QuiverImageView *iv, GdkPixbufLoader *l) {}
void quiver_image_view_connect_pixbuf_size_prepared_signal(QuiverImageView *iv, GdkPixbufLoader *l) {}
GtkAdjustment * quiver_image_view_get_hadjustment(QuiverImageView *iv) { return iv->hadjustment; }
GtkAdjustment * quiver_image_view_get_vadjustment(QuiverImageView *iv) { return iv->vadjustment; }
void quiver_image_view_activate(QuiverImageView *iv) { g_signal_emit (iv, imageview_signals[SIGNAL_ACTIVATED], 0); }
