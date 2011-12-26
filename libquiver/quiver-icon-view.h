
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

struct _QuiverIconView
{
	GtkWidget parent;

	/* private */
	QuiverIconViewPrivate *priv;
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

void quiver_icon_view_set_n_items_func (QuiverIconView *iconview, 
         QuiverIconViewGetNItemsFunc func,gpointer data,GtkDestroyNotify destroy);

void quiver_icon_view_set_icon_pixbuf_func (QuiverIconView *iconview,
         QuiverIconViewGetIconPixbufFunc func,gpointer data,GtkDestroyNotify destroy);

void quiver_icon_view_set_thumbnail_pixbuf_func (QuiverIconView *iconview,
         QuiverIconViewGetThumbnailPixbufFunc func,gpointer data,GtkDestroyNotify destroy);

void quiver_icon_view_set_text_func (QuiverIconView *iconview,
         QuiverIconViewGetTextFunc func,gpointer data,GtkDestroyNotify destroy);

void quiver_icon_view_set_overlay_pixbuf_func (QuiverIconView *iconview,
         QuiverIconViewGetOverlayPixbufFunc func,gpointer data,GtkDestroyNotify destroy);

G_END_DECLS


#endif

