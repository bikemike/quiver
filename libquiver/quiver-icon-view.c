#include <config.h>
#include <gtk/gtk.h>
#include "quiver-icon-view.h"
#include "quiver-marshallers.h"
#include <math.h>
#include <stdlib.h>
#include <sys/time.h>
#include "quiver-pixbuf-utils.h"
#include <glib.h>

#define QUIVER_ICON_VIEW_GET_PRIVATE(obj) (quiver_icon_view_get_instance_private (QUIVER_ICON_VIEW (obj)))

#define QUIVER_ICON_VIEW_ICON_WIDTH              128
#define QUIVER_ICON_VIEW_ICON_HEIGHT             128
#define QUIVER_ICON_VIEW_CELL_PADDING            8 
#define QUIVER_ICON_VIEW_ICON_SHADOW_SIZE        5
#define QUIVER_ICON_VIEW_ICON_BORDER_SIZE        1

#define QUIVER_PARAM_READWRITE G_PARAM_READWRITE|G_PARAM_STATIC_STRINGS

typedef struct _CellItem CellItem;
struct _CellItem {
	gboolean selected;
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

struct _QuiverIconViewPrivate {
	GtkAdjustment *hadjustment;
	GtkAdjustment *vadjustment;
	GtkScrollablePolicy hscroll_policy;
	GtkScrollablePolicy vscroll_policy;
	guint icon_width;
	guint icon_height;
	guint cell_padding;
	gulong cursor_cell;
	gulong prelight_cell;
	QuiverIconViewScrollType scroll_type;
	QuiverIconViewDragBehavior drag_behavior;
	guint n_columns;
	guint n_rows;
	QuiverIconViewGetNItemsFunc callback_get_n_items;
	gpointer callback_get_n_items_data;
	GDestroyNotify callback_get_n_items_data_destroy;
	CellItem *cell_items;
	gulong n_cell_items;
};

G_DEFINE_TYPE_WITH_CODE(QuiverIconView, quiver_icon_view, GTK_TYPE_WIDGET,
                        G_ADD_PRIVATE(QuiverIconView)
                        G_IMPLEMENT_INTERFACE(GTK_TYPE_SCROLLABLE, NULL));

static void quiver_icon_view_snapshot (GtkWidget *widget, GtkSnapshot *snapshot);
static void quiver_icon_view_measure (GtkWidget *widget, GtkOrientation orientation, int for_size, int *minimum, int *natural, int *minimum_baseline, int *natural_baseline);
static void quiver_icon_view_size_allocate (GtkWidget *widget, int width, int height, int baseline);
static void quiver_icon_view_finalize(GObject *object);
static void quiver_icon_view_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void quiver_icon_view_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static void quiver_icon_view_adjustment_value_changed (GtkAdjustment *adjustment, QuiverIconView *iconview);
static void quiver_icon_view_update_icon_size(QuiverIconView *iconview);
void quiver_icon_view_set_hadjustment (QuiverIconView *iconview, GtkAdjustment *hadj);
void quiver_icon_view_set_vadjustment (QuiverIconView *iconview, GtkAdjustment *vadj);

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
	g_object_class_install_property (obj_class, PROP_HADJUSTMENT, g_param_spec_object ("hadjustment", "Horizontal adjustment", "Horizontal adjustment", GTK_TYPE_ADJUSTMENT, QUIVER_PARAM_READWRITE));
	g_object_class_install_property (obj_class, PROP_VADJUSTMENT, g_param_spec_object ("vadjustment", "Vertical adjustment", "Vertical adjustment", GTK_TYPE_ADJUSTMENT, QUIVER_PARAM_READWRITE));
	g_object_class_install_property (obj_class, PROP_HSCROLL_POLICY, g_param_spec_enum ("hscroll-policy", "Horizontal Scroll Policy", "Horizontal Scroll Policy", GTK_TYPE_SCROLLABLE_POLICY, GTK_SCROLL_MINIMUM, QUIVER_PARAM_READWRITE));
	g_object_class_install_property (obj_class, PROP_VSCROLL_POLICY, g_param_spec_enum ("vscroll-policy", "Vertical Scroll Policy", "Vertical Scroll Policy", GTK_TYPE_SCROLLABLE_POLICY, GTK_SCROLL_MINIMUM, QUIVER_PARAM_READWRITE));
	iconview_signals[SIGNAL_CELL_CLICKED] = g_signal_new ("cell_clicked", G_TYPE_FROM_CLASS (obj_class), G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (QuiverIconViewClass, cell_clicked), NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_UINT);
	iconview_signals[SIGNAL_CELL_ACTIVATED] = g_signal_new ("cell_activated", G_TYPE_FROM_CLASS (obj_class), G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (QuiverIconViewClass, cell_activated), NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_UINT);
	iconview_signals[SIGNAL_CURSOR_CHANGED] = g_signal_new ("cursor_changed", G_TYPE_FROM_CLASS (obj_class), G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (QuiverIconViewClass, cursor_changed), NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_UINT);
	iconview_signals[SIGNAL_SELECTION_CHANGED] = g_signal_new ("selection_changed", G_TYPE_FROM_CLASS (obj_class), G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (QuiverIconViewClass, selection_changed), NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void quiver_icon_view_init(QuiverIconView *iconview)
{
	iconview->priv = QUIVER_ICON_VIEW_GET_PRIVATE(iconview);
	iconview->priv->icon_width = QUIVER_ICON_VIEW_ICON_WIDTH;
	iconview->priv->icon_height = QUIVER_ICON_VIEW_ICON_HEIGHT;
	iconview->priv->cell_padding = QUIVER_ICON_VIEW_CELL_PADDING;
	iconview->priv->cursor_cell = G_MAXULONG;
	iconview->priv->prelight_cell = G_MAXULONG;
	iconview->priv->cell_items = g_new0(CellItem, 1);
	gtk_widget_set_focusable(GTK_WIDGET(iconview), TRUE);
}

static void quiver_icon_view_finalize(GObject *object)
{
	QuiverIconView *iconview = QUIVER_ICON_VIEW(object);
	g_free (iconview->priv->cell_items);
	if (iconview->priv->hadjustment) g_object_unref(iconview->priv->hadjustment);
	if (iconview->priv->vadjustment) g_object_unref(iconview->priv->vadjustment);
	G_OBJECT_CLASS (quiver_icon_view_parent_class)->finalize (object);
}

static void quiver_icon_view_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	QuiverIconView *iconview = QUIVER_ICON_VIEW (object);
	switch (prop_id) {
		case PROP_HADJUSTMENT: quiver_icon_view_set_hadjustment(iconview, (GtkAdjustment*)g_value_get_object (value)); break;
		case PROP_VADJUSTMENT: quiver_icon_view_set_vadjustment(iconview, (GtkAdjustment*)g_value_get_object (value)); break;
		case PROP_HSCROLL_POLICY: iconview->priv->hscroll_policy = (GtkScrollablePolicy)g_value_get_enum(value); gtk_widget_queue_resize(GTK_WIDGET(iconview)); break;
		case PROP_VSCROLL_POLICY: iconview->priv->vscroll_policy = (GtkScrollablePolicy)g_value_get_enum(value); gtk_widget_queue_resize(GTK_WIDGET(iconview)); break;
		default: G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec); break;
	}
}

static void quiver_icon_view_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	QuiverIconView *iconview = QUIVER_ICON_VIEW (object);
	switch (prop_id) {
		case PROP_HADJUSTMENT: g_value_set_object(value, iconview->priv->hadjustment); break;
		case PROP_VADJUSTMENT: g_value_set_object(value, iconview->priv->vadjustment); break;
		case PROP_HSCROLL_POLICY: g_value_set_enum(value, iconview->priv->hscroll_policy); break;
		case PROP_VSCROLL_POLICY: g_value_set_enum(value, iconview->priv->vscroll_policy); break;
		default: G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec); break;
	}
}

static void quiver_icon_view_measure (GtkWidget *widget, GtkOrientation orientation, int for_size, int *minimum, int *natural, int *minimum_baseline, int *natural_baseline)
{
    QuiverIconView *iconview = QUIVER_ICON_VIEW(widget);
    if (orientation == GTK_ORIENTATION_HORIZONTAL) *minimum = *natural = quiver_icon_view_get_cell_width(iconview);
    else *minimum = *natural = quiver_icon_view_get_cell_height(iconview);
}

static void quiver_icon_view_size_allocate (GtkWidget *widget, int width, int height, int baseline) { quiver_icon_view_update_icon_size(QUIVER_ICON_VIEW(widget)); }
static void quiver_icon_view_snapshot (GtkWidget *widget, GtkSnapshot *snapshot) {
	int width = gtk_widget_get_width(widget);
	int height = gtk_widget_get_height(widget);
	GdkRGBA bg = {0.3, 0.3, 0.3, 1};
	gtk_snapshot_append_color(snapshot, &bg, &GRAPHENE_RECT_INIT(0, 0, width, height));
}

void quiver_icon_view_set_hadjustment (QuiverIconView *iconview, GtkAdjustment *hadj) {
	if (iconview->priv->hadjustment == hadj) return;
	if (iconview->priv->hadjustment) {
		g_signal_handlers_disconnect_by_func (iconview->priv->hadjustment, (void*)quiver_icon_view_adjustment_value_changed, iconview);
		g_object_unref (iconview->priv->hadjustment);
	}
	iconview->priv->hadjustment = hadj ? (GtkAdjustment*)g_object_ref(hadj) : NULL;
	if (iconview->priv->hadjustment) {
		g_signal_connect (iconview->priv->hadjustment, "value-changed", G_CALLBACK (quiver_icon_view_adjustment_value_changed), iconview);
		quiver_icon_view_update_icon_size(iconview);
	}
	g_object_notify(G_OBJECT(iconview), "hadjustment");
}

void quiver_icon_view_set_vadjustment (QuiverIconView *iconview, GtkAdjustment *vadj) {
	if (iconview->priv->vadjustment == vadj) return;
	if (iconview->priv->vadjustment) {
		g_signal_handlers_disconnect_by_func (iconview->priv->vadjustment, (void*)quiver_icon_view_adjustment_value_changed, iconview);
		g_object_unref (iconview->priv->vadjustment);
	}
	iconview->priv->vadjustment = vadj ? (GtkAdjustment*)g_object_ref(vadj) : NULL;
	if (iconview->priv->vadjustment) {
		g_signal_connect (iconview->priv->vadjustment, "value-changed", G_CALLBACK (quiver_icon_view_adjustment_value_changed), iconview);
		quiver_icon_view_update_icon_size(iconview);
	}
	g_object_notify(G_OBJECT(iconview), "vadjustment");
}

static void quiver_icon_view_adjustment_value_changed (GtkAdjustment *adjustment, QuiverIconView *iconview) { gtk_widget_queue_draw(GTK_WIDGET(iconview)); }
static void quiver_icon_view_update_icon_size(QuiverIconView *iconview) {}

GtkWidget *quiver_icon_view_new() { return g_object_new(QUIVER_TYPE_ICON_VIEW, NULL); }
guint quiver_icon_view_get_cell_width(QuiverIconView *iv) { return iv->priv->icon_width + iv->priv->cell_padding + 12; }
guint quiver_icon_view_get_cell_height(QuiverIconView *iv) { return iv->priv->icon_height + iv->priv->cell_padding + 12; }
void quiver_icon_view_set_n_columns(QuiverIconView *iv, guint c) { iv->priv->n_columns = c; }
void quiver_icon_view_set_n_rows(QuiverIconView *iv, guint r) { iv->priv->n_rows = r; }
void quiver_icon_view_set_scroll_type(QuiverIconView *iv, QuiverIconViewScrollType t) { iv->priv->scroll_type = t; }
void quiver_icon_view_set_drag_behavior(QuiverIconView *iv, QuiverIconViewDragBehavior b) { iv->priv->drag_behavior = b; }
void quiver_icon_view_set_icon_size(QuiverIconView *iv, guint w, guint h) { iv->priv->icon_width = w; iv->priv->icon_height = h; quiver_icon_view_update_icon_size(iv); }
void quiver_icon_view_set_cell_padding(QuiverIconView *iv, guint p) { iv->priv->cell_padding = p; }
void quiver_icon_view_get_icon_size(QuiverIconView *iv, guint* w, guint* h) { *w = iv->priv->icon_width; *h = iv->priv->icon_height; }
guint quiver_icon_view_get_cell_padding(QuiverIconView *iv) { return iv->priv->cell_padding; }
void quiver_icon_view_activate_cell(QuiverIconView *iv, gulong c) {}
gulong quiver_icon_view_get_cursor_cell(QuiverIconView *iv) { return iv->priv->cursor_cell; }
void quiver_icon_view_set_cursor_cell(QuiverIconView *iv, gulong c) { iv->priv->cursor_cell = c; }
gulong quiver_icon_view_get_prelight_cell(QuiverIconView* iv) { return iv->priv->prelight_cell; }
gulong quiver_icon_view_get_cell_for_xy(QuiverIconView *iv, gint x, gint y) { return G_MAXULONG; }
void quiver_icon_view_get_cell_mouse_position(QuiverIconView* iv, guint c, gint *x, gint *y) {}
void quiver_icon_view_set_selection(QuiverIconView *iv, const GList *s) {}
GList* quiver_icon_view_get_selection(QuiverIconView *iv) { return NULL; }
void quiver_icon_view_get_visible_range(QuiverIconView *iv, gulong *f, gulong *l) {}
void quiver_icon_view_invalidate_window(QuiverIconView *iv) { gtk_widget_queue_draw(GTK_WIDGET(iv)); }
void quiver_icon_view_invalidate_cell(QuiverIconView *iv, gulong c) { gtk_widget_queue_draw(GTK_WIDGET(iv)); }
void quiver_icon_view_set_n_items_func (QuiverIconView *iv, QuiverIconViewGetNItemsFunc f, gpointer d, GDestroyNotify dn) { iv->priv->callback_get_n_items = f; iv->priv->callback_get_n_items_data = d; iv->priv->callback_get_n_items_data_destroy = dn; }
void quiver_icon_view_set_icon_pixbuf_func (QuiverIconView *iv, QuiverIconViewGetIconPixbufFunc f, gpointer d, GDestroyNotify dn) {}
void quiver_icon_view_set_thumbnail_pixbuf_func (QuiverIconView *iv, QuiverIconViewGetThumbnailPixbufFunc f, gpointer d, GDestroyNotify dn) {}
void quiver_icon_view_set_text_func (QuiverIconView *iv, QuiverIconViewGetTextFunc f, gpointer d, GDestroyNotify dn) {}
void quiver_icon_view_set_overlay_pixbuf_func (QuiverIconView *iv, QuiverIconViewGetOverlayPixbufFunc f, gpointer d, GDestroyNotify dn) {}
