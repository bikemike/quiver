
#ifndef QUIVER_ICON_VIEW_H
#define QUIVER_ICON_VIEW_H


#include <gtk/gtk.h>

G_BEGIN_DECLS

#define QUIVER_TYPE_ICON_VIEW            (quiver_icon_view_get_type ())
#define QUIVER_ICON_VIEW(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), QUIVER_TYPE_ICON_VIEW, QuiverIconView))
#define QUIVER_ICON_VIEW_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), QUIVER_TYPE_ICON_VIEW, QuiverIconViewClass))
#define QUIVER_IS_ICON_VIEW(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), QUIVER_TYPE_ICON_VIEW))
#define QUIVER_IS_ICON_VIEW_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), QUIVER_TYPE_ICON_VIEW))
#define QUIVER_ICON_VIEW_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), QUIVER_TYPE_ICON_VIEW, QuiverIconViewClass))

typedef struct _QuiverIconView        QuiverIconView;
typedef struct _QuiverIconViewClass   QuiverIconViewClass;
typedef struct _QuiverIconViewPrivate QuiverIconViewPrivate;

enum _QuiverIconOverlayType
{
	QUIVER_ICON_OVERLAY_ICON,        /* top right */
	QUIVER_ICON_OVERLAY_LINK,        /* bottom left */
	QUIVER_ICON_OVERLAY_READ_ONLY,   /* */
	QUIVER_ICON_OVERLAY_FS_TYPE,
	QUIVER_ICON_OVERLAY_CATEGORY,    /* top right -1 */
	QUIVER_ICON_OVERLAY_RATING,      /* */
	QUIVER_ICON_OVERLAY_AUTO_ROTATE, /* */
	QUIVER_ICON_OVERLAY_NEW,         /* */
	QUIVER_ICON_OVERLAY_COUNT        /* */
};
typedef enum _QuiverIconOverlayType QuiverIconOverlayType;

enum _QuiverIconViewDragBehavior
{
	QUIVER_ICON_VIEW_DRAG_BEHAVIOR_RUBBER_BAND,
	QUIVER_ICON_VIEW_DRAG_BEHAVIOR_SCROLL,
	QUIVER_ICON_VIEW_DRAG_BEHAVIOR_COUNT,
	
};

enum _QuiverIconViewScrollType
{
	QUIVER_ICON_VIEW_SCROLL_NORMAL,
	QUIVER_ICON_VIEW_SCROLL_SMOOTH,
	QUIVER_ICON_VIEW_SCROLL_SMOOTH_CENTER,
};

typedef enum _QuiverIconViewScrollType QuiverIconViewScrollType;


typedef enum _QuiverIconViewDragBehavior QuiverIconViewDragBehavior;

typedef gulong (*QuiverIconViewGetNItemsFunc) (QuiverIconView *iconview,gpointer user_data);
typedef gchar* (*QuiverIconViewGetTextFunc) (QuiverIconView *iconview,gulong cell,gpointer user_data);
typedef GdkPixbuf* (*QuiverIconViewGetIconPixbufFunc) (QuiverIconView *iconview,gulong cell,gpointer user_data);
typedef GdkPixbuf* (*QuiverIconViewGetThumbnailPixbufFunc) (QuiverIconView *iconview,gulong cell,gint* actual_width, gint *actual_height, gpointer user_data);
typedef GdkPixbuf* (*QuiverIconViewGetOverlayPixbufFunc) (QuiverIconView *iconview,gulong cell, QuiverIconOverlayType type,gpointer user_data);

typedef struct _CellItem CellItem;
struct _CellItem {
	gboolean selected;
    // Add other per-cell state if needed, e.g., GdkPixbuf *cached_thumb;
};

struct _QuiverIconView {
    GtkWidget parent;
};

struct _QuiverIconViewPrivate {
	GtkAdjustment *hadjustment;
	GtkAdjustment *vadjustment;
	GtkScrollablePolicy hscroll_policy;
	GtkScrollablePolicy vscroll_policy;

	gdouble last_hadjustment_val; // Store last adjustment values for diff calculation
	gdouble last_vadjustment_val;

	guint icon_width;
	guint icon_height;
	guint icon_border_size; // For drawing borders around icons
	guint cell_padding;     // Padding within a cell, around an icon

    // Rubberband selection state
	gboolean rubberband_active;
	cairo_rectangle_int_t rubberband_rect; // Current rubberband rectangle in widget coordinates
    double rubberband_start_x;
    double rubberband_start_y;

	// Scrolling and drag state
	gboolean scroll_draw; // From old code, might mean "draw immediately on scroll"
	// gboolean drag_mode_start; // Replaced by gesture states
	gboolean drag_mode_enabled; // General flag if a drag operation is in progress (either type)

	gint rubberband_scroll_x_direction; // -1, 0, 1 for auto-scroll during rubberband
	gint rubberband_scroll_y_direction; // -1, 0, 1
	guint timeout_id_rubberband_scroll;

	// struct timeval last_motion_time; // For legacy smooth scroll
	// GList* velocity_time_list;       // For legacy smooth scroll

	gulong cursor_cell;     // Index of the cell with keyboard focus/cursor
	gulong prelight_cell;   // Index of the cell under mouse hover
	gulong selection_anchor_cell; // For shift-click range selection

	gboolean mouse_button_is_down; // Still useful to track general mouse down state across gestures

    QuiverIconViewScrollType scroll_type; // User preference for scroll behavior
	// gulong smooth_scroll_cell; // Legacy
	// gdouble smooth_scroll_hadjust; // Legacy
	// gdouble smooth_scroll_vadjust; // Legacy
	QuiverIconViewDragBehavior drag_behavior; // Rubberband or scroll content on drag

	// guint timeout_id_smooth_scroll; // Legacy
	// guint timeout_id_smooth_scroll_slowdown; // Legacy

	guint n_columns_actual; // Calculated number of columns based on width
	guint n_rows_actual;    // Calculated number of rows based on height and items

    // User-configurable fixed columns/rows
    guint n_columns_fixed;
	guint n_rows_fixed;

    // Callbacks for data model
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

	CellItem *cell_items;   // Array to store selection state for each item
	gulong n_cell_items_allocated; // Allocated size of cell_items array

    // Event Controllers
    GtkGesture *click_gesture;
    GtkGesture *drag_gesture;
    GtkEventController *motion_controller;
    GtkEventController *scroll_controller;
    GtkEventController *key_controller;
};

struct _QuiverIconViewClass
{
	GtkWidgetClass parent_class;

	void  (*set_scroll_adjustments)   (QuiverIconView	    *iconview,
					 GtkAdjustment  *hadjustment,
					 GtkAdjustment  *vadjustment);

	void (*cell_clicked) (QuiverIconView *iconview,gulong cell);
	void (*cell_activated) (QuiverIconView *iconview,gulong cell);
	void (*cursor_changed) (QuiverIconView *iconview,gulong cell);
	void (*selection_changed) (QuiverIconView *iconview);

	/* Padding for future expansion */
	
	void (*_reserved1) (void);
	void (*_reserved2) (void);
	void (*_reserved3) (void);
	void (*_reserved4) (void);

};



GType	   quiver_icon_view_get_type (void) G_GNUC_CONST;
GtkWidget *quiver_icon_view_new ();

GtkAdjustment * quiver_icon_view_get_hadjustment(QuiverIconView *iconview);
GtkAdjustment * quiver_icon_view_get_vadjustment(QuiverIconView *iconview);
void quiver_icon_view_set_hadjustment(QuiverIconView *iconview, GtkAdjustment *hadjustment);
void quiver_icon_view_set_vadjustment(QuiverIconView *iconview, GtkAdjustment *vadjustment);

void quiver_icon_view_set_n_columns(QuiverIconView *iconview,guint n_columns);
void quiver_icon_view_set_n_rows(QuiverIconView *iconview,guint n_rows);
void quiver_icon_view_set_scroll_type(QuiverIconView *iconview, QuiverIconViewScrollType scroll_type);

void quiver_icon_view_set_drag_behavior(QuiverIconView *iconview,QuiverIconViewDragBehavior behavior);

void quiver_icon_view_set_icon_size(QuiverIconView *iconview, guint width,guint height);
void quiver_icon_view_set_cell_padding(QuiverIconView *iconview,guint padding);
void quiver_icon_view_get_icon_size(QuiverIconView *iconview, guint* width,guint* height);
guint quiver_icon_view_get_cell_padding(QuiverIconView *iconview);
guint quiver_icon_view_get_cell_width(QuiverIconView *iconview);
guint quiver_icon_view_get_cell_height(QuiverIconView *iconview);
void quiver_icon_view_activate_cell(QuiverIconView *iconview,gulong cell);

gulong quiver_icon_view_get_cursor_cell(QuiverIconView *iconview);
void quiver_icon_view_set_cursor_cell(QuiverIconView *iconview,gulong new_cursor_cell);

gulong quiver_icon_view_get_prelight_cell(QuiverIconView* iconview);
gulong quiver_icon_view_get_cell_for_xy(QuiverIconView *iconview,gint x, gint y);

void quiver_icon_view_get_cell_mouse_position(QuiverIconView* iconview, guint cell, gint *x, gint *y);

void quiver_icon_view_set_selection(QuiverIconView *iconview,const GList *selection);
GList* quiver_icon_view_get_selection(QuiverIconView *iconview);

void quiver_icon_view_get_visible_range(QuiverIconView *iconview,gulong *first, gulong *last);

void quiver_icon_view_invalidate_window(QuiverIconView *iconview);

void quiver_icon_view_invalidate_cell(QuiverIconView *iconview,gulong cell);

void quiver_icon_view_select_all_cells(QuiverIconView *iconview, gboolean select);

void quiver_icon_view_set_n_items_func (QuiverIconView *iconview, 
         QuiverIconViewGetNItemsFunc func,gpointer data,GDestroyNotify destroy);

void quiver_icon_view_set_icon_pixbuf_func (QuiverIconView *iconview,
         QuiverIconViewGetIconPixbufFunc func,gpointer data,GDestroyNotify destroy);

void quiver_icon_view_set_thumbnail_pixbuf_func (QuiverIconView *iconview,
         QuiverIconViewGetThumbnailPixbufFunc func,gpointer data,GDestroyNotify destroy);

void quiver_icon_view_set_text_func (QuiverIconView *iconview,
         QuiverIconViewGetTextFunc func,gpointer data,GDestroyNotify destroy);

void quiver_icon_view_set_overlay_pixbuf_func (QuiverIconView *iconview,
         QuiverIconViewGetOverlayPixbufFunc func,gpointer data,GDestroyNotify destroy);

G_END_DECLS


#endif
