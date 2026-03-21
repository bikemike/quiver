#include <config.h>
#include <gtk/gtk.h>
#include "quiver-icon-view.h"
#include "quiver-marshallers.h"
#include <math.h>
#include <stdlib.h>
#include "quiver-pixbuf-utils.h"

#define QUIVER_ICON_VIEW_ICON_WIDTH              128
#define QUIVER_ICON_VIEW_ICON_HEIGHT             128
#define QUIVER_ICON_VIEW_CELL_PADDING            8 

struct _QuiverIconView {
	GtkWidget parent_instance;
	GtkAdjustment *hadjustment;
	GtkAdjustment *vadjustment;
	GtkScrollablePolicy hscroll_policy;
	GtkScrollablePolicy vscroll_policy;
	guint icon_width;
	guint icon_height;
	guint cell_padding;
	gulong cursor_cell;
	QuiverIconViewGetNItemsFunc callback_get_n_items;
	gpointer callback_get_n_items_data;
	GDestroyNotify callback_get_n_items_data_destroy;
};

enum {
	SIGNAL_CELL_CLICKED,
	SIGNAL_CELL_ACTIVATED,
	SIGNAL_CURSOR_CHANGED,
	SIGNAL_SELECTION_CHANGED,
	SIGNAL_COUNT
};

static guint iconview_signals[SIGNAL_COUNT] = {0};

enum {
   PROP_0,
   PROP_HADJUSTMENT,
   PROP_VADJUSTMENT,
   PROP_HSCROLL_POLICY,
   PROP_VSCROLL_POLICY,
};

G_DEFINE_TYPE_WITH_CODE(QuiverIconView, quiver_icon_view, GTK_TYPE_WIDGET,
                        G_IMPLEMENT_INTERFACE(GTK_TYPE_SCROLLABLE, NULL));

static void quiver_icon_view_snapshot (GtkWidget *widget, GtkSnapshot *snapshot)
{
	int width = gtk_widget_get_width (widget);
	int height = gtk_widget_get_height (widget);
	GdkRGBA bg = {0.3, 0.3, 0.3, 1};
	gtk_snapshot_append_color (snapshot, &bg, &GRAPHENE_RECT_INIT(0, 0, width, height));
}

static void quiver_icon_view_measure (GtkWidget *widget, GtkOrientation orientation, int for_size, int *minimum, int *natural, int *minimum_baseline, int *natural_baseline)
{
    QuiverIconView *self = QUIVER_ICON_VIEW(widget);
    if (orientation == GTK_ORIENTATION_HORIZONTAL) *minimum = *natural = self->icon_width + self->cell_padding;
    else *minimum = *natural = self->icon_height + self->cell_padding;
}

static void quiver_icon_view_size_allocate (GtkWidget *widget, int width, int height, int baseline) {}

static void quiver_icon_view_finalize (GObject *object)
{
	QuiverIconView *self = QUIVER_ICON_VIEW (object);
	g_clear_object (&self->hadjustment);
	g_clear_object (&self->vadjustment);
	G_OBJECT_CLASS (quiver_icon_view_parent_class)->finalize (object);
}

void quiver_icon_view_set_hadjustment (QuiverIconView *self, GtkAdjustment *hadj);
void quiver_icon_view_set_vadjustment (QuiverIconView *self, GtkAdjustment *vadj);

static void quiver_icon_view_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	QuiverIconView *self = QUIVER_ICON_VIEW (object);
	switch (prop_id) {
		case PROP_HADJUSTMENT: quiver_icon_view_set_hadjustment (self, (GtkAdjustment*)g_value_get_object (value)); break;
		case PROP_VADJUSTMENT: quiver_icon_view_set_vadjustment (self, (GtkAdjustment*)g_value_get_object (value)); break;
		case PROP_HSCROLL_POLICY: self->hscroll_policy = (GtkScrollablePolicy)g_value_get_enum (value); break;
		case PROP_VSCROLL_POLICY: self->vscroll_policy = (GtkScrollablePolicy)g_value_get_enum (value); break;
		default: G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec); break;
	}
}

static void quiver_icon_view_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	QuiverIconView *self = QUIVER_ICON_VIEW (object);
	switch (prop_id) {
		case PROP_HADJUSTMENT: g_value_set_object (value, self->hadjustment); break;
		case PROP_VADJUSTMENT: g_value_set_object (value, self->vadjustment); break;
		case PROP_HSCROLL_POLICY: g_value_set_enum (value, self->hscroll_policy); break;
		case PROP_VSCROLL_POLICY: g_value_set_enum (value, self->vscroll_policy); break;
		default: G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec); break;
	}
}

static void quiver_icon_view_class_init (QuiverIconViewClass *klass)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
	GObjectClass *obj_class = G_OBJECT_CLASS (klass);
	widget_class->snapshot = quiver_icon_view_snapshot;
	widget_class->measure = quiver_icon_view_measure;
	widget_class->size_allocate = quiver_icon_view_size_allocate;
	obj_class->finalize = quiver_icon_view_finalize;
	obj_class->set_property = quiver_icon_view_set_property;
	obj_class->get_property = quiver_icon_view_get_property;
	g_object_class_override_property (obj_class, PROP_HADJUSTMENT, "hadjustment");
	g_object_class_override_property (obj_class, PROP_VADJUSTMENT, "vadjustment");
	g_object_class_override_property (obj_class, PROP_HSCROLL_POLICY, "hscroll-policy");
	g_object_class_override_property (obj_class, PROP_VSCROLL_POLICY, "vscroll-policy");

	iconview_signals[SIGNAL_CELL_CLICKED] = g_signal_new ("cell_clicked", G_TYPE_FROM_CLASS (obj_class), G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (QuiverIconViewClass, cell_clicked), NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_UINT);
	iconview_signals[SIGNAL_CELL_ACTIVATED] = g_signal_new ("cell_activated", G_TYPE_FROM_CLASS (obj_class), G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (QuiverIconViewClass, cell_activated), NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_UINT);
	iconview_signals[SIGNAL_CURSOR_CHANGED] = g_signal_new ("cursor_changed", G_TYPE_FROM_CLASS (obj_class), G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (QuiverIconViewClass, cursor_changed), NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_UINT);
	iconview_signals[SIGNAL_SELECTION_CHANGED] = g_signal_new ("selection_changed", G_TYPE_FROM_CLASS (obj_class), G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (QuiverIconViewClass, selection_changed), NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void quiver_icon_view_init (QuiverIconView *self)
{
	self->icon_width = QUIVER_ICON_VIEW_ICON_WIDTH;
	self->icon_height = QUIVER_ICON_VIEW_ICON_HEIGHT;
	self->cell_padding = QUIVER_ICON_VIEW_CELL_PADDING;
	self->cursor_cell = G_MAXULONG;
	gtk_widget_set_focusable (GTK_WIDGET (self), TRUE);
}

GtkWidget *quiver_icon_view_new (void) { return g_object_new (QUIVER_TYPE_ICON_VIEW, NULL); }
void quiver_icon_view_set_hadjustment (QuiverIconView *self, GtkAdjustment *hadj) {
	if (g_set_object (&self->hadjustment, hadj)) gtk_widget_queue_draw (GTK_WIDGET (self));
}
void quiver_icon_view_set_vadjustment (QuiverIconView *self, GtkAdjustment *vadj) {
	if (g_set_object (&self->vadjustment, vadj)) gtk_widget_queue_draw (GTK_WIDGET (self));
}

// Public API stubs
void quiver_icon_view_set_n_columns(QuiverIconView *iv, guint c) {}
void quiver_icon_view_set_n_rows(QuiverIconView *iv, guint r) {}
void quiver_icon_view_set_scroll_type(QuiverIconView *iv, QuiverIconViewScrollType t) {}
void quiver_icon_view_set_drag_behavior(QuiverIconView *iv, QuiverIconViewDragBehavior b) {}
void quiver_icon_view_set_icon_size(QuiverIconView *iv, guint w, guint h) { iv->icon_width = w; iv->icon_height = h; }
void quiver_icon_view_set_cell_padding(QuiverIconView *iv, guint p) { iv->cell_padding = p; }
void quiver_icon_view_get_icon_size(QuiverIconView *iv, guint* w, guint* h) { *w = iv->icon_width; *h = iv->icon_height; }
guint quiver_icon_view_get_cell_padding(QuiverIconView *iv) { return iv->cell_padding; }
guint quiver_icon_view_get_cell_width(QuiverIconView *iv) { return iv->icon_width + iv->cell_padding; }
guint quiver_icon_view_get_cell_height(QuiverIconView *iv) { return iv->icon_height + iv->cell_padding; }
void quiver_icon_view_activate_cell(QuiverIconView *iv, gulong c) {}
gulong quiver_icon_view_get_cursor_cell(QuiverIconView *iv) { return iv->cursor_cell; }
void quiver_icon_view_set_cursor_cell(QuiverIconView *iv, gulong c) { iv->cursor_cell = c; }
gulong quiver_icon_view_get_prelight_cell(QuiverIconView* iv) { return G_MAXULONG; }
gulong quiver_icon_view_get_cell_for_xy(QuiverIconView *iv, gint x, gint y) { return G_MAXULONG; }
void quiver_icon_view_get_cell_mouse_position(QuiverIconView* iv, guint c, gint *x, gint *y) {}
void quiver_icon_view_set_selection(QuiverIconView *iv, const GList *s) {}
GList* quiver_icon_view_get_selection(QuiverIconView *iv) { return NULL; }
void quiver_icon_view_get_visible_range(QuiverIconView *iv, gulong *f, gulong *l) {}
void quiver_icon_view_invalidate_window(QuiverIconView *iv) { gtk_widget_queue_draw (GTK_WIDGET (iv)); }
void quiver_icon_view_invalidate_cell(QuiverIconView *iv, gulong c) { gtk_widget_queue_draw (GTK_WIDGET (iv)); }
void quiver_icon_view_set_n_items_func (QuiverIconView *iv, QuiverIconViewGetNItemsFunc f, gpointer d, GDestroyNotify dn) {
    if (iv->callback_get_n_items_data_destroy) iv->callback_get_n_items_data_destroy (iv->callback_get_n_items_data);
    iv->callback_get_n_items = f; iv->callback_get_n_items_data = d; iv->callback_get_n_items_data_destroy = dn;
}
void quiver_icon_view_set_icon_pixbuf_func (QuiverIconView *iv, QuiverIconViewGetIconPixbufFunc f, gpointer d, GDestroyNotify dn) {}
void quiver_icon_view_set_thumbnail_pixbuf_func (QuiverIconView *iv, QuiverIconViewGetThumbnailPixbufFunc f, gpointer d, GDestroyNotify dn) {}
void quiver_icon_view_set_text_func (QuiverIconView *iv, QuiverIconViewGetTextFunc f, gpointer d, GDestroyNotify dn) {}
void quiver_icon_view_set_overlay_pixbuf_func (QuiverIconView *iv, QuiverIconViewGetOverlayPixbufFunc f, gpointer d, GDestroyNotify dn) {}
