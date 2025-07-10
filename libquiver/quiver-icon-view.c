#include <config.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h> // Correct header for GDK_KEY_ constants in GTK4
#include "quiver-icon-view.h"
#include "quiver-marshallers.h"
#include <math.h>
#include <stdlib.h>
#include <sys/time.h>
#include "quiver-pixbuf-utils.h"
#include <glib.h>

#define QUIVER_ICON_VIEW_ICON_WIDTH              128
#define QUIVER_ICON_VIEW_ICON_HEIGHT             128
#define QUIVER_ICON_VIEW_CELL_PADDING            8
#define QUIVER_ICON_VIEW_ICON_SHADOW_SIZE        5
#define QUIVER_ICON_VIEW_ICON_BORDER_SIZE        1
#define SMOOTH_SCROLL_TIMEOUT                    35

#define QUIVER_PARAM_READWRITE G_PARAM_READWRITE|G_PARAM_STATIC_STRINGS

G_DEFINE_TYPE_WITH_CODE(QuiverIconView,quiver_icon_view,GTK_TYPE_WIDGET,G_IMPLEMENT_INTERFACE(GTK_TYPE_SCROLLABLE,NULL));

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

enum {
   PROP_0,
   PROP_HADJUSTMENT,
   PROP_VADJUSTMENT,
   PROP_HSCROLL_POLICY,
   PROP_VSCROLL_POLICY,
};

typedef struct _VelocityTimeStruct {
	gint hvelocity;
	gint vvelocity;
	gdouble time;
} VelocityTimeStruct;

struct _QuiverIconViewPrivate {
	GtkAdjustment *hadjustment;
	GtkAdjustment *vadjustment;
	guint hscroll_policy : 1;
	guint vscroll_policy : 1;
	gdouble last_hadjustment;
	gdouble last_vadjustment;
	guint icon_width;
	guint icon_height;
	guint icon_border_size;
	guint cell_padding;
	gint start_x, start_y;
	gint last_x, last_y;
	gint rubberband_x1, rubberband_y1;
	gint rubberband_x2, rubberband_y2;
	gboolean scroll_draw;
	gboolean drag_mode_start;
	gboolean drag_mode_enabled;
	cairo_rectangle_int_t rubberband_rect;
	cairo_rectangle_int_t rubberband_rect_old;
	gint rubberband_scroll_x;
	gint rubberband_scroll_y;
	guint timeout_id_rubberband_scroll;
	struct timeval last_motion_time;
	GList* velocity_time_list;
	gulong cursor_cell;
	gulong prelight_cell;
	gulong cursor_cell_first;
	gboolean mouse_button_is_down;
	QuiverIconViewScrollType scroll_type;
	gulong smooth_scroll_cell;
	gdouble smooth_scroll_hadjust;
	gdouble smooth_scroll_vadjust;
	QuiverIconViewDragBehavior drag_behavior;
	guint timeout_id_smooth_scroll;
	guint timeout_id_smooth_scroll_slowdown;
	guint n_columns;
	guint n_rows;
	QuiverIconViewGetNItemsFunc callback_get_n_items;
	gpointer callback_get_n_items_data;
	GDestroyNotify callback_get_n_items_data_destroy;
	QuiverIconViewGetIconPixbufFunc callback_get_icon_pixbuf;
	gpointer callback_get_icon_pixbuf_data;
	GDestroyNotify callback_get_icon_pixbuf_data_destroy;
	QuiverIconViewGetThumbnailPixbufFunc callback_get_thumbnail_pixbuf;
	gpointer callback_get_thumbnail_pixbuf_data;
	GDestroyNotify callback_get_thumbnail_pixbuf_data_destroy;
	QuiverIconViewGetTextFunc callback_get_text;
	gpointer callback_get_text_data;
	GDestroyNotify callback_get_text_data_destroy;
	QuiverIconViewGetOverlayPixbufFunc callback_get_overlay_pixbuf;
	gpointer callback_get_overlay_pixbuf_data;
	GDestroyNotify callback_get_overlay_pixbuf_data_destroy;
	CellItem *cell_items;
	gulong n_cell_items;
    GtkGesture *click_gesture;
    GtkGesture *drag_gesture;
    GtkEventController *motion_controller;
    GtkEventController *scroll_controller;
    GtkEventController *key_controller;
};

static void quiver_icon_view_realize(GtkWidget *widget);
static void quiver_icon_view_unrealize(GtkWidget *widget);
static void quiver_icon_view_map(GtkWidget *widget);
static void quiver_icon_view_unmap(GtkWidget *widget);
static void quiver_icon_view_size_allocate(GtkWidget *widget, int width, int height, int baseline);
static void quiver_icon_view_measure(GtkWidget *widget, GtkOrientation orientation, int for_size, int *minimum, int *natural, int *minimum_baseline, int *natural_baseline);
static void quiver_icon_view_snapshot(GtkWidget* widget, GtkSnapshot* snapshot);
static void quiver_icon_view_handle_click_pressed(GtkGestureClick *gesture, int n_press, double x, double y, QuiverIconView *iconview);
static void quiver_icon_view_handle_click_released(GtkGestureClick *gesture, int n_press, double x, double y, QuiverIconView *iconview);
static void quiver_icon_view_handle_click_stopped(GtkGestureClick *gesture, QuiverIconView *iconview);
static void quiver_icon_view_handle_drag_begin(GtkGestureDrag *gesture, double start_x, double start_y, QuiverIconView *iconview);
static void quiver_icon_view_handle_drag_update(GtkGestureDrag *gesture, double offset_x, double offset_y, QuiverIconView *iconview);
static void quiver_icon_view_handle_drag_end(GtkGestureDrag *gesture, double offset_x, double offset_y, QuiverIconView *iconview);
static void quiver_icon_view_handle_motion(GtkEventControllerMotion *controller, double x, double y, QuiverIconView *iconview);
static void quiver_icon_view_handle_leave(GtkEventControllerMotion *controller, QuiverIconView *iconview);
static gboolean quiver_icon_view_handle_scroll(GtkEventControllerScroll *controller, double dx, double dy, QuiverIconView *iconview);
static gboolean quiver_icon_view_handle_key_pressed(GtkEventControllerKey *controller, guint keyval, guint keycode, GdkModifierType state, QuiverIconView *iconview);
static void quiver_icon_view_set_hadjustment(QuiverIconView *iconview, GtkAdjustment *hadjustment);
static void quiver_icon_view_set_vadjustment(QuiverIconView *iconview, GtkAdjustment *vadjustment);
static void remove_timeout_smooth_scroll(QuiverIconView *iconview);
static void quiver_icon_view_adjustment_value_changed(GtkAdjustment *adjustment, QuiverIconView *iconview);
static void quiver_icon_view_scroll_to_cell_smooth(QuiverIconView *iconview, gulong cell);
static void quiver_icon_view_scroll_to_adjustment_smooth(QuiverIconView *iconview, gint hadjust, gint vadjust);
static gboolean quiver_icon_view_smooth_scroll_step(QuiverIconView* iconview);
static gboolean quiver_icon_view_timeout_smooth_scroll(gpointer data);
static gboolean quiver_icon_view_timeout_smooth_scroll_slowdown(gpointer data);
static void quiver_icon_view_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void quiver_icon_view_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static void quiver_icon_view_finalize(GObject *object);
static void quiver_icon_view_get_col_row_count(QuiverIconView *iconview,guint *cols, guint *rows);
static void quiver_icon_view_set_adjustment_upper (GtkAdjustment *adj, gdouble upper, gboolean always_emit_changed);
static void quiver_icon_view_set_cursor_cell_full(QuiverIconView *iconview,gulong new_cursor_cell,GdkModifierType state,gboolean is_mouse);
static void quiver_icon_view_scroll_to_cell_force_top(QuiverIconView *iconview,gulong cell,gboolean force_top);
static void quiver_icon_view_scroll_to_cell(QuiverIconView *iconview,gulong cell);
static void quiver_icon_view_set_select_all(QuiverIconView *iconview, gboolean selected);
static void quiver_icon_view_shift_select_cells(QuiverIconView *iconview,gulong new_cursor_cell);
static void quiver_icon_view_update_rubber_band(QuiverIconView *iconview);
static void quiver_icon_view_update_rubber_band_selection(QuiverIconView *iconview);
static void quiver_icon_view_update_icon_size(QuiverIconView *iconview);
static gulong quiver_icon_view_get_n_items(QuiverIconView* iconview);
static GdkPixbuf* quiver_icon_view_get_thumbnail_pixbuf(QuiverIconView* iconview,gulong cell, gint* actual_width, gint *actual_height);
static GdkPixbuf* quiver_icon_view_get_icon_pixbuf(QuiverIconView* iconview,gulong cell);
static void quiver_icon_view_draw_drop_shadow(QuiverIconView *iconview, cairo_t* cr, GtkStateFlags state_flags, int rect_x,int rect_y, int rect_w, int rect_h);
static void quiver_icon_view_click_cell(QuiverIconView *iconview,gulong cell);
static gboolean rubberband_scroll_timeout (gpointer data);
static void quiver_icon_view_draw_icons (GtkWidget *widget, cairo_t* cr, cairo_rectangle_int_t r);

static guint iconview_signals[SIGNAL_COUNT] = {0};

static void quiver_icon_view_class_init(QuiverIconViewClass *klass) {
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);
	GObjectClass *obj_class = G_OBJECT_CLASS(klass);

	widget_class->realize = quiver_icon_view_realize;
	widget_class->unrealize = quiver_icon_view_unrealize;
	widget_class->map = quiver_icon_view_map;
	widget_class->unmap = quiver_icon_view_unmap;
	widget_class->size_allocate = quiver_icon_view_size_allocate;
	widget_class->measure = quiver_icon_view_measure;
	gtk_widget_class_set_draw_func(widget_class, quiver_icon_view_snapshot);

	obj_class->set_property = quiver_icon_view_set_property;
	obj_class->get_property = quiver_icon_view_get_property;
	obj_class->finalize = quiver_icon_view_finalize;

	g_object_class_override_property(obj_class, PROP_HADJUSTMENT, "hadjustment");
	g_object_class_override_property(obj_class, PROP_VADJUSTMENT, "vadjustment");
	g_object_class_override_property(obj_class, PROP_HSCROLL_POLICY, "hscroll-policy");
	g_object_class_override_property(obj_class, PROP_VSCROLL_POLICY, "vscroll-policy");

	iconview_signals[SIGNAL_CELL_CLICKED] = g_signal_new("cell_clicked", G_TYPE_FROM_CLASS(obj_class), G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET(QuiverIconViewClass, cell_clicked), NULL, NULL, g_cclosure_marshal_VOID__UINT, G_TYPE_NONE, 1, G_TYPE_UINT);
	iconview_signals[SIGNAL_CELL_ACTIVATED] = g_signal_new("cell_activated", G_TYPE_FROM_CLASS(obj_class), G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET(QuiverIconViewClass, cell_activated), NULL, NULL, g_cclosure_marshal_VOID__UINT, G_TYPE_NONE, 1, G_TYPE_UINT);
	iconview_signals[SIGNAL_CURSOR_CHANGED] = g_signal_new("cursor_changed", G_TYPE_FROM_CLASS(obj_class), G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET(QuiverIconViewClass, cursor_changed), NULL, NULL, g_cclosure_marshal_VOID__UINT, G_TYPE_NONE, 1, G_TYPE_UINT);
	iconview_signals[SIGNAL_SELECTION_CHANGED] = g_signal_new("selection_changed", G_TYPE_FROM_CLASS(obj_class), G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET(QuiverIconViewClass, selection_changed), NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
}

static GtkAdjustment* new_default_adjustment (void) {
  return gtk_adjustment_new (0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
}

static void quiver_icon_view_init(QuiverIconView *iconview) {
	QuiverIconViewPrivate *priv = quiver_icon_view_get_instance_private(iconview);
    iconview->priv = priv;

	priv->icon_width = QUIVER_ICON_VIEW_ICON_WIDTH;
	priv->icon_height = QUIVER_ICON_VIEW_ICON_HEIGHT;
	priv->icon_border_size = QUIVER_ICON_VIEW_ICON_BORDER_SIZE;
	priv->cell_padding = QUIVER_ICON_VIEW_CELL_PADDING;
	priv->scroll_draw = TRUE;
	priv->scroll_type = QUIVER_ICON_VIEW_SCROLL_NORMAL;
	priv->smooth_scroll_cell = G_MAXULONG;
	priv->drag_behavior = QUIVER_ICON_VIEW_DRAG_BEHAVIOR_RUBBER_BAND;
	priv->cursor_cell = G_MAXULONG;
	priv->prelight_cell = G_MAXULONG;
	priv->cursor_cell_first = G_MAXULONG;
    priv->n_cell_items = 0;
	priv->cell_items = (CellItem*)g_malloc0( sizeof(CellItem)*(priv->n_cell_items+1) );

	gtk_widget_set_can_focus(GTK_WIDGET(iconview), TRUE);

    priv->click_gesture = gtk_gesture_click_new();
    g_signal_connect(priv->click_gesture, "pressed", G_CALLBACK(quiver_icon_view_handle_click_pressed), iconview);
    g_signal_connect(priv->click_gesture, "released", G_CALLBACK(quiver_icon_view_handle_click_released), iconview);
    g_signal_connect_swapped(priv->click_gesture, "stopped", G_CALLBACK(quiver_icon_view_handle_click_stopped), iconview);

    priv->drag_gesture = gtk_gesture_drag_new();
    g_signal_connect(priv->drag_gesture, "drag-begin", G_CALLBACK(quiver_icon_view_handle_drag_begin), iconview);
    g_signal_connect(priv->drag_gesture, "drag-update", G_CALLBACK(quiver_icon_view_handle_drag_update), iconview);
    g_signal_connect(priv->drag_gesture, "drag-end", G_CALLBACK(quiver_icon_view_handle_drag_end), iconview);

    priv->motion_controller = gtk_event_controller_motion_new();
    g_signal_connect(priv->motion_controller, "motion", G_CALLBACK(quiver_icon_view_handle_motion), iconview);
    g_signal_connect(priv->motion_controller, "leave", G_CALLBACK(quiver_icon_view_handle_leave), iconview);

    priv->scroll_controller = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL | GTK_EVENT_CONTROLLER_SCROLL_HORIZONTAL);
    g_signal_connect(priv->scroll_controller, "scroll", G_CALLBACK(quiver_icon_view_handle_scroll), iconview);

    priv->key_controller = gtk_event_controller_key_new();
    g_signal_connect(priv->key_controller, "key-pressed", G_CALLBACK(quiver_icon_view_handle_key_pressed), iconview);
}

static void quiver_icon_view_finalize(GObject *object) {
	QuiverIconView *iconview = QUIVER_ICON_VIEW(object);
    QuiverIconViewPrivate *priv = iconview->priv;

	remove_timeout_smooth_scroll(iconview);
	if (priv->timeout_id_rubberband_scroll != 0) {
		g_source_remove (priv->timeout_id_rubberband_scroll);
        priv->timeout_id_rubberband_scroll = 0;
	}
	if (priv->timeout_id_smooth_scroll_slowdown != 0) {
		g_source_remove(priv->timeout_id_smooth_scroll_slowdown);
        priv->timeout_id_smooth_scroll_slowdown = 0;
	}
	g_list_foreach(priv->velocity_time_list, (GFunc)g_free, NULL);
	g_list_free(priv->velocity_time_list);
    priv->velocity_time_list = NULL;
	g_free (priv->cell_items);
    priv->cell_items = NULL;

    if (priv->click_gesture) { g_object_unref(priv->click_gesture); priv->click_gesture = NULL; }
    if (priv->drag_gesture) { g_object_unref(priv->drag_gesture); priv->drag_gesture = NULL; }
    if (priv->motion_controller) { g_object_unref(priv->motion_controller); priv->motion_controller = NULL; }
    if (priv->scroll_controller) { g_object_unref(priv->scroll_controller); priv->scroll_controller = NULL; }
    if (priv->key_controller) { g_object_unref(priv->key_controller); priv->key_controller = NULL; }

    if (priv->hadjustment) {
        g_signal_handlers_disconnect_by_func(priv->hadjustment, G_CALLBACK(quiver_icon_view_adjustment_value_changed), iconview);
        g_object_unref(priv->hadjustment);
        priv->hadjustment = NULL;
    }
    if (priv->vadjustment) {
        g_signal_handlers_disconnect_by_func(priv->vadjustment, G_CALLBACK(quiver_icon_view_adjustment_value_changed), iconview);
        g_object_unref(priv->vadjustment);
        priv->vadjustment = NULL;
    }

	G_OBJECT_CLASS(quiver_icon_view_parent_class)->finalize(object);
}

static void quiver_icon_view_realize(GtkWidget *widget) {
    GTK_WIDGET_CLASS(quiver_icon_view_parent_class)->realize(widget);
    gtk_widget_queue_resize(widget);
}

static void quiver_icon_view_unrealize(GtkWidget *widget) {
    GTK_WIDGET_CLASS(quiver_icon_view_parent_class)->unrealize(widget);
}

static void quiver_icon_view_map(GtkWidget *widget) {
    QuiverIconView *iconview = QUIVER_ICON_VIEW(widget);
    QuiverIconViewPrivate *priv = iconview->priv;
    GTK_WIDGET_CLASS(quiver_icon_view_parent_class)->map(widget);

    gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(g_object_ref(priv->click_gesture)));
    gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(g_object_ref(priv->drag_gesture)));
    gtk_widget_add_controller(widget, g_object_ref(priv->motion_controller));
    gtk_widget_add_controller(widget, g_object_ref(priv->scroll_controller));
    gtk_widget_add_controller(widget, g_object_ref(priv->key_controller));

    gtk_gesture_set_state(priv->click_gesture, GTK_EVENT_SEQUENCE_CLAIMED);
    gtk_gesture_set_state(priv->drag_gesture, GTK_EVENT_SEQUENCE_CLAIMED);
}

static void quiver_icon_view_unmap(GtkWidget *widget) {
    QuiverIconView *iconview = QUIVER_ICON_VIEW(widget);
    QuiverIconViewPrivate *priv = iconview->priv;
    gtk_widget_remove_controller(widget, GTK_EVENT_CONTROLLER(priv->click_gesture));
    gtk_widget_remove_controller(widget, GTK_EVENT_CONTROLLER(priv->drag_gesture));
    gtk_widget_remove_controller(widget, priv->motion_controller);
    gtk_widget_remove_controller(widget, priv->scroll_controller);
    gtk_widget_remove_controller(widget, priv->key_controller);
    GTK_WIDGET_CLASS(quiver_icon_view_parent_class)->unmap(widget);
}

static void quiver_icon_view_size_allocate(GtkWidget *widget, int width, int height, int baseline) {
	QuiverIconView *iconview = QUIVER_ICON_VIEW(widget);
    QuiverIconViewPrivate *priv = iconview->priv;
    GTK_WIDGET_CLASS(quiver_icon_view_parent_class)->size_allocate(widget, width, height, baseline);
	if (gtk_widget_get_realized (widget)) {
        gtk_widget_queue_resize(widget);
		quiver_icon_view_update_icon_size(iconview);
		quiver_icon_view_scroll_to_cell_force_top(iconview,priv->cursor_cell,TRUE);
	}
}

static void quiver_icon_view_measure(GtkWidget *widget, GtkOrientation orientation, int for_size, int *minimum, int *natural, int *minimum_baseline, int *natural_baseline) {
	QuiverIconView *iconview = QUIVER_ICON_VIEW(widget);
    QuiverIconViewPrivate *priv = iconview->priv;
    gint cell_w = quiver_icon_view_get_cell_width(iconview);
    gint cell_h = quiver_icon_view_get_cell_height(iconview);
    gulong n_items = quiver_icon_view_get_n_items(iconview);
    if (cell_w <= 0) cell_w = 1;
    if (cell_h <= 0) cell_h = 1;

	if (orientation == GTK_ORIENTATION_HORIZONTAL) {
        if (priv->n_columns > 0) {
            *minimum = *natural = cell_w * priv->n_columns;
        } else {
            if (for_size > 0 && cell_h > 0 && priv->hscroll_policy == GTK_POLICY_NEVER) {
                guint num_rows_for_size = MAX(1, for_size / cell_h);
                guint num_cols_needed = (n_items > 0) ? (n_items + num_rows_for_size -1) / num_rows_for_size : 1;
                 *minimum = *natural = cell_w * num_cols_needed;
            } else {
                *minimum = *natural = cell_w * (n_items > 0 ? 1 : 1);
            }
        }
	} else {
        if (priv->n_rows > 0) {
            *minimum = *natural = cell_h * priv->n_rows;
        } else if (priv->n_columns > 0 && n_items > 0) {
             guint num_rows_calc = (n_items + priv->n_columns -1) / priv->n_columns;
            *minimum = *natural = cell_h * num_rows_calc;
        } else {
            if (for_size > 0 && cell_w > 0 && priv->vscroll_policy == GTK_POLICY_NEVER) {
                guint num_cols_for_size = MAX(1, for_size / cell_w);
                guint num_rows_needed = (n_items > 0) ? (n_items + num_cols_for_size -1) / num_cols_for_size : 1;
                *minimum = *natural = cell_h * num_rows_needed;
            } else {
                *minimum = *natural = cell_h * (n_items > 0 ? 1 : 1);
            }
        }
	}
    if (minimum_baseline) *minimum_baseline = -1;
    if (natural_baseline) *natural_baseline = -1;
}

static void quiver_icon_view_snapshot(GtkWidget* widget, GtkSnapshot* snapshot) {
    QuiverIconView *iconview = QUIVER_ICON_VIEW(widget);
    int width = gtk_widget_get_width(widget);
    int height = gtk_widget_get_height(widget);
    GtkStyleContext *context = gtk_widget_get_style_context(widget);
    GdkRGBA bg_color;

    // Try to get theme background color, then a generic background, then fallback
    if (!gtk_style_context_lookup_color(context, "theme_bg_color", &bg_color)) {
        if (!gtk_style_context_lookup_color(context, "background_color", &bg_color)) {
            // Fallback color if lookup fails
            gdk_rgba_parse(&bg_color, "rgb(200,200,200)"); // Light gray
        }
    }
    gtk_snapshot_append_color(snapshot, &bg_color, &GRAPHENE_RECT_INIT(0, 0, width, height));

    cairo_t *cr = gtk_snapshot_append_cairo(snapshot, &GRAPHENE_RECT_INIT(0,0, width, height));
    cairo_rectangle_int_t widget_bounds = {0, 0, width, height};
    quiver_icon_view_draw_icons (GTK_WIDGET(iconview), cr, widget_bounds);
    cairo_destroy(cr);
}

static void quiver_icon_view_handle_click_pressed (GtkGestureClick *gesture, int n_press, double x, double y, QuiverIconView *iconview) {
    QuiverIconViewPrivate *priv = iconview->priv;
    priv->start_x = x;
    priv->start_y = y;
    priv->last_x = x;
    priv->last_y = y;
	gettimeofday(&priv->last_motion_time,NULL);
	g_list_foreach(priv->velocity_time_list, (GFunc)g_free, NULL);
	g_list_free(priv->velocity_time_list);
	priv->velocity_time_list = NULL;

	if (!gtk_widget_has_focus (GTK_WIDGET(iconview))) {
		gtk_widget_grab_focus (GTK_WIDGET(iconview));
	}
    if (gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture)) == GDK_BUTTON_PRIMARY) {
        priv->mouse_button_is_down = TRUE;
        if (priv->drag_behavior == QUIVER_ICON_VIEW_DRAG_BEHAVIOR_RUBBER_BAND) {
            gint hadjust = (priv->hadjustment) ? (gint)gtk_adjustment_get_value(priv->hadjustment) : 0;
            gint vadjust = (priv->vadjustment) ? (gint)gtk_adjustment_get_value(priv->vadjustment) : 0;
            priv->rubberband_x1 = priv->rubberband_x2 = x + hadjust;
            priv->rubberband_y1 = priv->rubberband_y2 = y + vadjust;
        }
        if (priv->drag_behavior == QUIVER_ICON_VIEW_DRAG_BEHAVIOR_SCROLL) {
            priv->drag_mode_start = TRUE;
            if (0 != priv->timeout_id_smooth_scroll_slowdown) {
                g_source_remove(priv->timeout_id_smooth_scroll_slowdown);
                priv->timeout_id_smooth_scroll_slowdown = 0;
            }
            gettimeofday(&priv->last_motion_time, NULL);
        }
    }
}

static void quiver_icon_view_handle_click_released (GtkGestureClick *gesture, int n_press, double x, double y, QuiverIconView *iconview) {
    QuiverIconViewPrivate *priv = iconview->priv;
    gulong cell = quiver_icon_view_get_cell_for_xy(iconview, (gint)x, (gint)y);

    if (gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture)) == GDK_BUTTON_PRIMARY) {
        if (!priv->drag_mode_enabled) {
            if (cell != G_MAXULONG) {
                if (n_press == 2) {
                    quiver_icon_view_activate_cell(iconview, cell);
                } else if (n_press == 1) {
                    if (cell == priv->cursor_cell) {
                        quiver_icon_view_click_cell(iconview, cell);
                    }
                    GdkEvent *event = gtk_gesture_get_last_event(GTK_GESTURE(gesture), NULL);
                    GdkModifierType current_state = event ? gdk_event_get_modifier_state(event) : (GdkModifierType)0;
                    quiver_icon_view_set_cursor_cell_full(iconview, cell, current_state, TRUE);
                }
            }
        }
    }
    priv->mouse_button_is_down = FALSE;
}

static void quiver_icon_view_handle_click_stopped(GtkGestureClick *gesture, QuiverIconView *iconview) {
    iconview->priv->mouse_button_is_down = FALSE;
}

static void quiver_icon_view_handle_drag_begin (GtkGestureDrag *gesture, double start_x, double start_y, QuiverIconView *iconview) {
    QuiverIconViewPrivate *priv = iconview->priv;
    priv->start_x = start_x;
    priv->start_y = start_y;
    priv->last_x = start_x;
    priv->last_y = start_y;

    if (gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture)) == GDK_BUTTON_PRIMARY) {
        priv->drag_mode_start = FALSE;
        priv->drag_mode_enabled = TRUE;
        if (priv->drag_behavior == QUIVER_ICON_VIEW_DRAG_BEHAVIOR_RUBBER_BAND) {
            gint hadjust = (priv->hadjustment) ? (gint)gtk_adjustment_get_value(priv->hadjustment) : 0;
            gint vadjust = (priv->vadjustment) ? (gint)gtk_adjustment_get_value(priv->vadjustment) : 0;
            priv->rubberband_x1 = priv->rubberband_x2 = start_x + hadjust;
            priv->rubberband_y1 = priv->rubberband_y2 = start_y + vadjust;
            gulong cell = quiver_icon_view_get_cell_for_xy(iconview, start_x, start_y);
            GdkEvent *event = gtk_gesture_get_last_event(GTK_GESTURE(gesture),NULL);
            GdkModifierType current_state = event ? gdk_event_get_modifier_state(event) : (GdkModifierType)0;
            if (cell == G_MAXULONG && !(current_state & GDK_CONTROL_MASK)) {
                 quiver_icon_view_set_select_all(iconview, FALSE);
            }
        } else if (priv->drag_behavior == QUIVER_ICON_VIEW_DRAG_BEHAVIOR_SCROLL) {
            gettimeofday(&priv->last_motion_time, NULL);
            g_list_foreach(priv->velocity_time_list, (GFunc)g_free, NULL);
            g_list_free(priv->velocity_time_list);
            priv->velocity_time_list = NULL;
            if (0 != priv->timeout_id_smooth_scroll_slowdown) {
                g_source_remove(priv->timeout_id_smooth_scroll_slowdown);
                priv->timeout_id_smooth_scroll_slowdown = 0;
            }
        }
    }
}

static void quiver_icon_view_handle_drag_update (GtkGestureDrag *gesture, double offset_x, double offset_y, QuiverIconView *iconview) {
    QuiverIconViewPrivate *priv = iconview->priv;
    double current_x = priv->start_x + offset_x;
    double current_y = priv->start_y + offset_y;
    GtkWidget *widget = GTK_WIDGET(iconview);

    if (gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture)) == GDK_BUTTON_PRIMARY) {
        if (priv->drag_behavior == QUIVER_ICON_VIEW_DRAG_BEHAVIOR_RUBBER_BAND) {
            gint hadjust = (priv->hadjustment) ? (gint)gtk_adjustment_get_value(priv->hadjustment) : 0;
            gint vadjust = (priv->vadjustment) ? (gint)gtk_adjustment_get_value(priv->vadjustment) : 0;
            priv->rubberband_x2 = current_x + hadjust;
            priv->rubberband_y2 = current_y + vadjust;
            quiver_icon_view_update_rubber_band(iconview);
            priv->rubberband_scroll_x = 0;
            priv->rubberband_scroll_y = 0;
            int widget_width = gtk_widget_get_width(widget);
            int widget_height = gtk_widget_get_height(widget);
            if (current_x < 0 || current_x > widget_width) {
                if (current_x < 0) priv->rubberband_scroll_x = current_x;
                else priv->rubberband_scroll_x = current_x - widget_width;
            }
            if (current_y < 0 || current_y > widget_height) {
                if (current_y < 0) priv->rubberband_scroll_y = current_y;
                else priv->rubberband_scroll_y = current_y - widget_height;
            }
            if (priv->rubberband_scroll_x != 0 || priv->rubberband_scroll_y != 0) {
                if (priv->timeout_id_rubberband_scroll == 0) {
                    priv->timeout_id_rubberband_scroll = g_timeout_add (30, rubberband_scroll_timeout, iconview);
                }
            } else {
                if (priv->timeout_id_rubberband_scroll != 0) {
                    g_source_remove (priv->timeout_id_rubberband_scroll);
                    priv->timeout_id_rubberband_scroll = 0;
                }
            }
        } else if (priv->drag_behavior == QUIVER_ICON_VIEW_DRAG_BEHAVIOR_SCROLL) {
            struct timeval new_motion_time = {0};
            gettimeofday(&new_motion_time,NULL);
            gdouble old_time = (gdouble)priv->last_motion_time.tv_sec + ((gdouble)priv->last_motion_time.tv_usec)/1000000.;
            gdouble current_time_sec = (gdouble)new_motion_time.tv_sec + ((gdouble)new_motion_time.tv_usec)/1000000.;
            VelocityTimeStruct* vt = g_new(VelocityTimeStruct, 1);
            double dx_from_last = current_x - priv->last_x;
            double dy_from_last = current_y - priv->last_y;
            if (current_time_sec > old_time) {
                vt->time = current_time_sec - old_time;
                vt->hvelocity = (gint)(dx_from_last / vt->time);
                vt->hvelocity = MIN(12000,vt->hvelocity);
                vt->hvelocity = MAX(-12000,vt->hvelocity);
                vt->vvelocity =  (gint)(dy_from_last / vt->time);
                vt->vvelocity = MIN(12000,vt->vvelocity);
                vt->vvelocity = MAX(-12000,vt->vvelocity);
            } else { vt->time = 0.001; vt->hvelocity = 0; vt->vvelocity = 0; }
            if (g_list_length(priv->velocity_time_list) == 3 ) {
                GList* last_link = g_list_last(priv->velocity_time_list);
                g_free(last_link->data);
                priv->velocity_time_list = g_list_remove_link(priv->velocity_time_list, last_link);
            }
            priv->velocity_time_list = g_list_prepend(priv->velocity_time_list, vt);
            priv->last_motion_time = new_motion_time;
            if(priv->hadjustment && priv->vadjustment) {
                gdouble hadjust_val = gtk_adjustment_get_value(priv->hadjustment);
                gdouble vadjust_val = gtk_adjustment_get_value(priv->vadjustment);
                hadjust_val -= dx_from_last;
                vadjust_val -= dy_from_last;
                double lower_h = gtk_adjustment_get_lower(priv->hadjustment);
                double upper_h = gtk_adjustment_get_upper(priv->hadjustment) - gtk_adjustment_get_page_size(priv->hadjustment);
                double lower_v = gtk_adjustment_get_lower(priv->vadjustment);
                double upper_v = gtk_adjustment_get_upper(priv->vadjustment) - gtk_adjustment_get_page_size(priv->vadjustment);
                gtk_adjustment_set_value(priv->hadjustment, CLAMP(hadjust_val, lower_h, upper_h));
                gtk_adjustment_set_value(priv->vadjustment, CLAMP(vadjust_val, lower_v, upper_v));
            }
        }
    }
    priv->last_x = current_x;
    priv->last_y = current_y;
}

static void quiver_icon_view_handle_drag_end (GtkGestureDrag *gesture, double offset_x, double offset_y, QuiverIconView *iconview) {
    QuiverIconViewPrivate *priv = iconview->priv;
    if (gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture)) == GDK_BUTTON_PRIMARY || gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture)) == 0 ) {
        priv->drag_mode_enabled = FALSE;
        priv->drag_mode_start = FALSE;
        if (priv->drag_behavior == QUIVER_ICON_VIEW_DRAG_BEHAVIOR_RUBBER_BAND) {
            if (priv->timeout_id_rubberband_scroll != 0) {
                g_source_remove (priv->timeout_id_rubberband_scroll);
                priv->timeout_id_rubberband_scroll = 0;
            }
            gtk_widget_queue_draw(GTK_WIDGET(iconview));
        } else if (priv->drag_behavior == QUIVER_ICON_VIEW_DRAG_BEHAVIOR_SCROLL) {
            struct timeval new_motion_time = {0}; gettimeofday(&new_motion_time,NULL);
            gdouble old_time = (gdouble)priv->last_motion_time.tv_sec + ((gdouble)priv->last_motion_time.tv_usec)/1000000.;
            gdouble current_time_sec = (gdouble)new_motion_time.tv_sec + ((gdouble)new_motion_time.tv_usec)/1000000.;
            if (g_list_length(priv->velocity_time_list) >= 1 && (current_time_sec - old_time) < 0.2 ) {
                remove_timeout_smooth_scroll(iconview);
                if (0 != priv->timeout_id_smooth_scroll_slowdown) {
                     g_source_remove(priv->timeout_id_smooth_scroll_slowdown);
                }
                priv->timeout_id_smooth_scroll_slowdown = g_timeout_add(SMOOTH_SCROLL_TIMEOUT, quiver_icon_view_timeout_smooth_scroll_slowdown, iconview);
            }
        }
    }
    priv->mouse_button_is_down = FALSE;
}

static void quiver_icon_view_handle_motion (GtkEventControllerMotion *controller, double x, double y, QuiverIconView *iconview) {
    QuiverIconViewPrivate *priv = iconview->priv;
    gulong new_cell = quiver_icon_view_get_cell_for_xy (iconview, (gint)x, (gint)y);
	if (priv->prelight_cell != new_cell) {
		if (G_MAXULONG != priv->prelight_cell) {
			quiver_icon_view_invalidate_cell(iconview, priv->prelight_cell);
		}
		priv->prelight_cell = new_cell;
		if (G_MAXULONG != priv->prelight_cell) {
			quiver_icon_view_invalidate_cell(iconview, priv->prelight_cell);
		}
	}
}

static void quiver_icon_view_handle_leave (GtkEventControllerMotion *controller, QuiverIconView *iconview) {
    QuiverIconViewPrivate *priv = iconview->priv;
    if (G_MAXULONG != priv->prelight_cell) {
		quiver_icon_view_invalidate_cell(iconview, priv->prelight_cell);
		priv->prelight_cell = G_MAXULONG;
	}
}

static gboolean quiver_icon_view_handle_scroll (GtkEventControllerScroll *controller, double dx, double dy, QuiverIconView *iconview) {
    QuiverIconViewPrivate *priv = iconview->priv;
    gdouble new_hadjust_val = priv->hadjustment ? gtk_adjustment_get_value(priv->hadjustment) : 0;
    gdouble new_vadjust_val = priv->vadjustment ? gtk_adjustment_get_value(priv->vadjustment) : 0;

    if (0 != priv->timeout_id_smooth_scroll && priv->smooth_scroll_cell == G_MAXULONG) {
        new_hadjust_val = priv->smooth_scroll_hadjust;
        new_vadjust_val = priv->smooth_scroll_vadjust;
    }
    remove_timeout_smooth_scroll(iconview);

    if (priv->hadjustment && priv->vadjustment) {
        if (1 == priv->n_rows) {
            if (dy > 0) new_hadjust_val += gtk_adjustment_get_step_increment(priv->hadjustment);
            else if (dy < 0) new_hadjust_val -= gtk_adjustment_get_step_increment(priv->hadjustment);
            if (dx > 0) new_hadjust_val += gtk_adjustment_get_step_increment(priv->hadjustment);
            else if (dx < 0) new_hadjust_val -= gtk_adjustment_get_step_increment(priv->hadjustment);
            double lower_h = gtk_adjustment_get_lower(priv->hadjustment);
            double upper_h = gtk_adjustment_get_upper(priv->hadjustment) - gtk_adjustment_get_page_size(priv->hadjustment);
            new_hadjust_val = CLAMP(new_hadjust_val, lower_h, upper_h);
        } else {
            if (dy > 0) new_vadjust_val += gtk_adjustment_get_step_increment(priv->vadjustment);
            else if (dy < 0) new_vadjust_val -= gtk_adjustment_get_step_increment(priv->vadjustment);
            if (dx > 0) new_hadjust_val += gtk_adjustment_get_step_increment(priv->hadjustment);
            else if (dx < 0) new_hadjust_val -= gtk_adjustment_get_step_increment(priv->hadjustment);
            double lower_v = gtk_adjustment_get_lower(priv->vadjustment);
            double upper_v = gtk_adjustment_get_upper(priv->vadjustment) - gtk_adjustment_get_page_size(priv->vadjustment);
            new_vadjust_val = CLAMP(new_vadjust_val, lower_v, upper_v);
            double lower_h = gtk_adjustment_get_lower(priv->hadjustment);
            double upper_h = gtk_adjustment_get_upper(priv->hadjustment) - gtk_adjustment_get_page_size(priv->hadjustment);
            new_hadjust_val = CLAMP(new_hadjust_val, lower_h, upper_h);
        }
        if (QUIVER_ICON_VIEW_SCROLL_SMOOTH == priv->scroll_type || QUIVER_ICON_VIEW_SCROLL_SMOOTH_CENTER == priv->scroll_type) {
            quiver_icon_view_scroll_to_adjustment_smooth(iconview, new_hadjust_val, new_vadjust_val);
        } else {
            gtk_adjustment_set_value(priv->hadjustment, new_hadjust_val);
            gtk_adjustment_set_value(priv->vadjustment, new_vadjust_val);
        }
    }
    return TRUE;
}

static gboolean quiver_icon_view_handle_key_pressed(GtkEventControllerKey *controller, guint keyval, guint keycode, GdkModifierType state, QuiverIconView *iconview) {
    QuiverIconViewPrivate *priv = iconview->priv;
	gulong n_cells  = quiver_icon_view_get_n_items(iconview);
	guint cols,rows;
	quiver_icon_view_get_col_row_count(iconview,&cols,&rows);
	gboolean event_handled = TRUE;
	guint cell_height = quiver_icon_view_get_cell_height(iconview);
    guint widget_height = gtk_widget_get_height(GTK_WIDGET(iconview));
    if (cols == 0 && n_cells > 0) cols = 1;
    if (cols == 0) return FALSE;
	guint rows_per_page = (cell_height > 0 && widget_height > 0) ? (widget_height / cell_height) : 1;
	if (rows_per_page == 0) rows_per_page = 1;
    guint n_cells_per_page = cols * rows_per_page;
    if (n_cells_per_page == 0 && n_cells > 0) n_cells_per_page = n_cells;
	gulong new_cursor_cell = priv->cursor_cell;
    if (new_cursor_cell == G_MAXULONG && n_cells > 0) new_cursor_cell = 0;
    else if (n_cells == 0) return FALSE;

	switch(keyval) {
		case GDK_KEY_Return: case GDK_KEY_KP_Enter: if (new_cursor_cell != G_MAXULONG) quiver_icon_view_activate_cell(iconview, new_cursor_cell); break;
		case GDK_KEY_a: case GDK_KEY_A: if (state & GDK_CONTROL_MASK) quiver_icon_view_set_select_all(iconview,TRUE); else event_handled = FALSE; break;
		case GDK_KEY_space:
			if (state & GDK_CONTROL_MASK && new_cursor_cell != G_MAXULONG && new_cursor_cell < priv->n_cell_items) {
				priv->cell_items[new_cursor_cell].selected = !priv->cell_items[new_cursor_cell].selected;
				g_signal_emit(iconview,iconview_signals[SIGNAL_SELECTION_CHANGED],0);
				quiver_icon_view_invalidate_cell(iconview,new_cursor_cell);
			} else if (new_cursor_cell != G_MAXULONG && new_cursor_cell < priv->n_cell_items) {
                priv->cell_items[new_cursor_cell].selected = !priv->cell_items[new_cursor_cell].selected;
                g_signal_emit(iconview,iconview_signals[SIGNAL_SELECTION_CHANGED],0);
                quiver_icon_view_invalidate_cell(iconview,new_cursor_cell);
            } else { event_handled = FALSE; }
			break;
		case GDK_KEY_Left: case GDK_KEY_KP_Left: if (new_cursor_cell != G_MAXULONG && new_cursor_cell > 0) new_cursor_cell--; break;
		case GDK_KEY_Right: case GDK_KEY_KP_Right: if (new_cursor_cell != G_MAXULONG && new_cursor_cell < n_cells -1 ) new_cursor_cell++; break;
		case GDK_KEY_Up: case GDK_KEY_KP_Up: if (new_cursor_cell != G_MAXULONG && new_cursor_cell >= cols) new_cursor_cell -= cols; else if (new_cursor_cell != G_MAXULONG && new_cursor_cell > 0) new_cursor_cell = 0; break;
		case GDK_KEY_Down: case GDK_KEY_KP_Down: if (new_cursor_cell != G_MAXULONG && new_cursor_cell + cols < n_cells) new_cursor_cell += cols; else if (new_cursor_cell != G_MAXULONG && new_cursor_cell < n_cells -1) new_cursor_cell = n_cells -1; break;
		case GDK_KEY_Home: case GDK_KEY_KP_Home: new_cursor_cell = 0; break;
		case GDK_KEY_End: case GDK_KEY_KP_End: if (n_cells > 0) new_cursor_cell = n_cells -1; else new_cursor_cell = G_MAXULONG; break;
		case GDK_KEY_Page_Up: case GDK_KEY_KP_Page_Up: if (new_cursor_cell != G_MAXULONG && new_cursor_cell >= n_cells_per_page) new_cursor_cell -= n_cells_per_page; else new_cursor_cell = 0; break;
		case GDK_KEY_Page_Down: case GDK_KEY_KP_Page_Down: if (new_cursor_cell != G_MAXULONG) { new_cursor_cell += n_cells_per_page; if (new_cursor_cell >= n_cells && n_cells > 0) new_cursor_cell = n_cells - 1; } else if (n_cells > 0) { new_cursor_cell = MIN(n_cells_per_page, n_cells-1); } break;
		default: event_handled = FALSE; break;
	}
    if (event_handled) {
        if (new_cursor_cell < n_cells) {
            if (new_cursor_cell != priv->cursor_cell) {
                quiver_icon_view_set_cursor_cell_full(iconview, new_cursor_cell, state, FALSE);
            }
        } else if (n_cells > 0 && priv->cursor_cell != G_MAXULONG) {
            // No change
        } else if (n_cells == 0 && priv->cursor_cell != G_MAXULONG) {
            quiver_icon_view_set_cursor_cell_full(iconview, G_MAXULONG, state, FALSE);
        } else if (new_cursor_cell == G_MAXULONG && new_cursor_cell != priv->cursor_cell) {
             quiver_icon_view_set_cursor_cell_full(iconview, G_MAXULONG, state, FALSE);
        }
    }
    return event_handled;
}

static void quiver_icon_view_set_hadjustment(QuiverIconView *iconview, GtkAdjustment *adj) {
    QuiverIconViewPrivate *priv = iconview->priv;
	if (priv->hadjustment && (priv->hadjustment != adj)) {
		g_signal_handlers_disconnect_by_func (priv->hadjustment, G_CALLBACK(quiver_icon_view_adjustment_value_changed), iconview);
		g_object_unref (priv->hadjustment);
	}
    priv->hadjustment = adj ? g_object_ref(adj) : new_default_adjustment();
    g_signal_connect (priv->hadjustment, "value-changed", G_CALLBACK (quiver_icon_view_adjustment_value_changed), iconview);
	quiver_icon_view_update_icon_size(iconview);
	g_object_notify(G_OBJECT(iconview), "hadjustment");
}

static void quiver_icon_view_set_vadjustment(QuiverIconView *iconview, GtkAdjustment *adj) {
    QuiverIconViewPrivate *priv = iconview->priv;
	if (priv->vadjustment && (priv->vadjustment != adj)) {
		g_signal_handlers_disconnect_by_func (priv->vadjustment, G_CALLBACK(quiver_icon_view_adjustment_value_changed), iconview);
		g_object_unref (priv->vadjustment);
	}
    priv->vadjustment = adj ? g_object_ref(adj) : new_default_adjustment();
	g_signal_connect (priv->vadjustment, "value-changed", G_CALLBACK (quiver_icon_view_adjustment_value_changed), iconview);
    quiver_icon_view_update_icon_size(iconview);
	g_object_notify(G_OBJECT(iconview), "vadjustment");
}

static void quiver_icon_view_adjustment_value_changed (GtkAdjustment *adjustment, QuiverIconView *iconview) {
	QuiverIconViewPrivate *priv = iconview->priv;
    if (!priv->hadjustment || !priv->vadjustment) return; // Ensure adjustments are set

	gdouble hadj = gtk_adjustment_get_value(priv->hadjustment);
	gdouble vadj = gtk_adjustment_get_value(priv->vadjustment);

	if (gtk_widget_get_realized (GTK_WIDGET(iconview))) {
		if (priv->scroll_draw) {
            gtk_widget_queue_draw(GTK_WIDGET(iconview));
		}
		priv->last_vadjustment = vadj;
		priv->last_hadjustment = hadj;
	}
}

static void quiver_icon_view_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
	QuiverIconView *iconview = QUIVER_ICON_VIEW(object);
    QuiverIconViewPrivate *priv = iconview->priv;
	switch (prop_id) {
		case PROP_HADJUSTMENT:
			quiver_icon_view_set_hadjustment(iconview, (GtkAdjustment*)g_value_get_object (value));
			break;
		case PROP_VADJUSTMENT:
			quiver_icon_view_set_vadjustment(iconview, (GtkAdjustment*)g_value_get_object (value));
			break;
		case PROP_HSCROLL_POLICY:
			priv->hscroll_policy = g_value_get_enum(value);
			gtk_widget_queue_resize(GTK_WIDGET(iconview));
			break;
		case PROP_VSCROLL_POLICY:
			priv->vscroll_policy = g_value_get_enum(value);
			gtk_widget_queue_resize(GTK_WIDGET(iconview));
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

static void quiver_icon_view_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
	QuiverIconView *iconview = QUIVER_ICON_VIEW(object);
    QuiverIconViewPrivate *priv = iconview->priv;
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
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

static void quiver_icon_view_get_col_row_count(QuiverIconView *iconview,guint *cols, guint *rows) {
	QuiverIconViewPrivate *priv = iconview->priv;
	guint c,r;
	GtkWidget *widget = GTK_WIDGET(iconview);
	gulong n_cells  = quiver_icon_view_get_n_items(iconview);
	guint cell_width = quiver_icon_view_get_cell_width(iconview);
    guint widget_width = gtk_widget_get_width(widget);

	r = 0;
	c = (cell_width > 0) ? (widget_width / cell_width) : 1;
	if (0 == c && n_cells > 0) c = 1;
    else if (0 == c && n_cells == 0) c = 0;

	if (n_cells > 0 && c > 0) r = (n_cells -1) / c + 1;
    else if (n_cells > 0 && c == 0) r = n_cells;
    else r = 0;

	if (0 != priv->n_columns) {
		c = priv->n_columns;
        if (c > 0 && n_cells > 0) r = (n_cells-1) / c + 1; else if (n_cells > 0) r = n_cells; else r = 0;
	} else if (0 != priv->n_rows) {
		r = priv->n_rows;
        if (r > 0 && n_cells > 0) c = (n_cells-1) / r + 1; else if (n_cells > 0) c = n_cells; else c = 0;
	}

    if (c == 0 && n_cells > 0) c = 1;
    if (r == 0 && n_cells > 0) r = 1;

	if (NULL != cols) *cols = c;
	if (NULL != rows) *rows = r;
}

// Implementations for other static utility functions (stubs for now for brevity)
static void quiver_icon_view_draw_icons (GtkWidget *widget, cairo_t* cr, cairo_rectangle_int_t r) { /* TODO */ }
static void remove_timeout_smooth_scroll(QuiverIconView *iconview){ if (0 != iconview->priv->timeout_id_smooth_scroll) { g_source_remove (iconview->priv->timeout_id_smooth_scroll); iconview->priv->timeout_id_smooth_scroll = 0; iconview->priv->smooth_scroll_cell = G_MAXULONG; iconview->priv->smooth_scroll_hadjust = 0.; iconview->priv->smooth_scroll_vadjust = 0.;}}
static void quiver_icon_view_scroll_to_cell_smooth(QuiverIconView *iconview, gulong cell){/* TODO */}
static void quiver_icon_view_scroll_to_adjustment_smooth(QuiverIconView *iconview, gint hadjust, gint vadjust){/* TODO */}
static gboolean quiver_icon_view_smooth_scroll_step(QuiverIconView* iconview){ return FALSE; }
static gboolean quiver_icon_view_timeout_smooth_scroll(gpointer data){ return FALSE; }
static gboolean quiver_icon_view_timeout_smooth_scroll_slowdown(gpointer data){ return FALSE; }
static void quiver_icon_view_set_adjustment_upper (GtkAdjustment *adj, gdouble upper, gboolean always_emit_changed){/* TODO */}
static void quiver_icon_view_set_cursor_cell_full(QuiverIconView *iconview,gulong new_cursor_cell,GdkModifierType state,gboolean is_mouse){/* TODO */}
static void quiver_icon_view_scroll_to_cell_force_top(QuiverIconView *iconview,gulong cell,gboolean force_top){/* TODO */}
static void quiver_icon_view_scroll_to_cell(QuiverIconView *iconview,gulong cell){ quiver_icon_view_scroll_to_cell_force_top(iconview, cell, FALSE); }
static void quiver_icon_view_set_select_all(QuiverIconView *iconview, gboolean selected){/* TODO */}
static void quiver_icon_view_shift_select_cells(QuiverIconView *iconview,gulong new_cursor_cell){/* TODO */}
static void quiver_icon_view_update_rubber_band(QuiverIconView *iconview){/* TODO */}
static void quiver_icon_view_update_rubber_band_selection(QuiverIconView *iconview){/* TODO */}
static void quiver_icon_view_update_icon_size(QuiverIconView *iconview){ if(gtk_widget_get_realized(GTK_WIDGET(iconview))) gtk_widget_queue_resize(GTK_WIDGET(iconview));}
static gulong quiver_icon_view_get_n_items(QuiverIconView* iconview){ if(iconview->priv->callback_get_n_items) return iconview->priv->callback_get_n_items(iconview, iconview->priv->callback_get_n_items_data); return 0;}
static GdkPixbuf* quiver_icon_view_get_thumbnail_pixbuf(QuiverIconView* iconview,gulong cell, gint* actual_width, gint *actual_height){ if(iconview->priv->callback_get_thumbnail_pixbuf) return iconview->priv->callback_get_thumbnail_pixbuf(iconview, cell, actual_width, actual_height, iconview->priv->callback_get_thumbnail_pixbuf_data); return NULL;}
static GdkPixbuf* quiver_icon_view_get_icon_pixbuf(QuiverIconView* iconview,gulong cell){ if(iconview->priv->callback_get_icon_pixbuf) return iconview->priv->callback_get_icon_pixbuf(iconview, cell, iconview->priv->callback_get_icon_pixbuf_data); return NULL;}
static void quiver_icon_view_draw_drop_shadow(QuiverIconView *iconview, cairo_t* cr, GtkStateFlags state_flags, int rect_x,int rect_y, int rect_w, int rect_h){/* TODO */}
static void quiver_icon_view_click_cell(QuiverIconView *iconview,gulong cell) { g_signal_emit(iconview,iconview_signals[SIGNAL_CELL_CLICKED],0,cell); }
static gboolean rubberband_scroll_timeout (gpointer data) {
	QuiverIconView *iconview = QUIVER_ICON_VIEW(data); QuiverIconViewPrivate *priv = iconview->priv;
    if (!priv->hadjustment || !priv->vadjustment) return G_SOURCE_REMOVE;
	gdouble xvalue = gtk_adjustment_get_value(priv->hadjustment) + priv->rubberband_scroll_x;
    xvalue = CLAMP(xvalue, gtk_adjustment_get_lower(priv->hadjustment), gtk_adjustment_get_upper(priv->hadjustment) - gtk_adjustment_get_page_size(priv->hadjustment));
	gdouble yvalue = gtk_adjustment_get_value(priv->vadjustment) + priv->rubberband_scroll_y;
    yvalue = CLAMP(yvalue, gtk_adjustment_get_lower(priv->vadjustment), gtk_adjustment_get_upper(priv->vadjustment) - gtk_adjustment_get_page_size(priv->vadjustment));
	gtk_adjustment_set_value (priv->hadjustment,xvalue); gtk_adjustment_set_value (priv->vadjustment, yvalue);
	quiver_icon_view_update_rubber_band (iconview); return G_SOURCE_CONTINUE;
}

// Public API functions
GtkWidget * quiver_icon_view_new() { return g_object_new(QUIVER_TYPE_ICON_VIEW, "hadjustment", new_default_adjustment(), "vadjustment", new_default_adjustment(), NULL); }
void quiver_icon_view_set_n_columns(QuiverIconView *iconview,guint n_columns){ QuiverIconViewPrivate *priv = iconview->priv; priv->n_columns = n_columns; priv->n_rows = 0; quiver_icon_view_update_icon_size(iconview); gtk_widget_queue_draw(GTK_WIDGET(iconview));}
void quiver_icon_view_set_n_rows(QuiverIconView *iconview,guint n_rows){ QuiverIconViewPrivate *priv = iconview->priv; priv->n_rows = n_rows; priv->n_columns = 0; quiver_icon_view_update_icon_size(iconview); gtk_widget_queue_draw(GTK_WIDGET(iconview));}
void quiver_icon_view_set_scroll_type(QuiverIconView *iconview, QuiverIconViewScrollType scroll_type){QuiverIconViewPrivate *priv = iconview->priv; priv->scroll_type = scroll_type;}
void quiver_icon_view_set_drag_behavior(QuiverIconView *iconview,QuiverIconViewDragBehavior behavior){QuiverIconViewPrivate *priv = iconview->priv; priv->drag_behavior = behavior;}
void quiver_icon_view_set_icon_size(QuiverIconView *iconview, guint width,guint height){QuiverIconViewPrivate *priv = iconview->priv; priv->icon_width  = width; priv->icon_height = height; quiver_icon_view_update_icon_size(iconview); gtk_widget_queue_resize(GTK_WIDGET(iconview));}
void quiver_icon_view_set_cell_padding(QuiverIconView *iconview,guint padding){QuiverIconViewPrivate *priv = iconview->priv; priv->cell_padding = padding; gtk_widget_queue_resize(GTK_WIDGET(iconview));}
void quiver_icon_view_get_icon_size(QuiverIconView *iconview, guint* width,guint* height){*width  = iconview->priv->icon_width; *height = iconview->priv->icon_height;}
guint quiver_icon_view_get_cell_padding(QuiverIconView *iconview){return iconview->priv->cell_padding;}
guint quiver_icon_view_get_cell_width(QuiverIconView *iconview) { QuiverIconViewPrivate *priv = iconview->priv; return priv->icon_width + priv->cell_padding*2 + QUIVER_ICON_VIEW_ICON_SHADOW_SIZE*2 + priv->icon_border_size*2; }
guint quiver_icon_view_get_cell_height(QuiverIconView *iconview) { QuiverIconViewPrivate *priv = iconview->priv; return priv->icon_height + priv->cell_padding*2 + QUIVER_ICON_VIEW_ICON_SHADOW_SIZE*2 + priv->icon_border_size*2; }
void quiver_icon_view_activate_cell(QuiverIconView *iconview,gulong cell){ g_signal_emit(iconview,iconview_signals[SIGNAL_CELL_ACTIVATED],0,cell); }
gulong quiver_icon_view_get_cursor_cell(QuiverIconView *iconview){ return iconview->priv->cursor_cell; }
void quiver_icon_view_set_cursor_cell(QuiverIconView *iconview,gulong new_cursor_cell){ quiver_icon_view_set_cursor_cell_full(iconview,new_cursor_cell,(GdkModifierType)0,FALSE); }
gulong quiver_icon_view_get_prelight_cell(QuiverIconView* iconview){ return iconview->priv->prelight_cell; }
gulong quiver_icon_view_get_cell_for_xy(QuiverIconView *iconview,gint x, gint y) { /* Full implementation needed */ return G_MAXULONG; }
void quiver_icon_view_get_cell_mouse_position(QuiverIconView* iconview, guint cell, gint *x, gint *y){*x=0;*y=0;}
void quiver_icon_view_set_selection(QuiverIconView *iconview,const GList *selection){}
GList* quiver_icon_view_get_selection(QuiverIconView *iconview){ return NULL;}
void quiver_icon_view_get_visible_range(QuiverIconView *iconview,gulong *first, gulong *last){}
void quiver_icon_view_invalidate_window(QuiverIconView *iconview){ gtk_widget_queue_draw(GTK_WIDGET(iconview));}
void quiver_icon_view_invalidate_cell(QuiverIconView *iconview, gulong cell){}
void quiver_icon_view_set_n_items_func (QuiverIconView *iconview, QuiverIconViewGetNItemsFunc func,gpointer data,GDestroyNotify destroy){}
void quiver_icon_view_set_icon_pixbuf_func (QuiverIconView *iconview, QuiverIconViewGetIconPixbufFunc func,gpointer data,GDestroyNotify destroy){}
void quiver_icon_view_set_thumbnail_pixbuf_func (QuiverIconView *iconview, QuiverIconViewGetThumbnailPixbufFunc func,gpointer data,GDestroyNotify destroy){}
void quiver_icon_view_set_text_func (QuiverIconView *iconview, QuiverIconViewGetTextFunc func,gpointer data,GDestroyNotify destroy){}
void quiver_icon_view_set_overlay_pixbuf_func (QuiverIconView *iconview, QuiverIconViewGetOverlayPixbufFunc func,gpointer data,GDestroyNotify destroy){}
