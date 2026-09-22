
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
typedef GdkTexture* (*QuiverIconViewGetThumbnailTextureFunc) (QuiverIconView *iconview,gulong cell,gint* actual_width, gint *actual_height, gpointer user_data);
typedef GdkTexture* (*QuiverIconViewGetIconTextureFunc) (QuiverIconView *iconview,gulong cell,gpointer user_data);
typedef GdkTexture* (*QuiverIconViewGetOverlayTextureFunc) (QuiverIconView *iconview,gulong cell, QuiverIconOverlayType type,gpointer user_data);
#if HAVE_GDK_PIXBUF
typedef GdkPixbuf* (*QuiverIconViewGetIconPixbufFunc) (QuiverIconView *iconview,gulong cell,gpointer user_data);
typedef GdkPixbuf* (*QuiverIconViewGetThumbnailPixbufFunc) (QuiverIconView *iconview,gulong cell,gint* actual_width, gint *actual_height, gpointer user_data);
typedef GdkPixbuf* (*QuiverIconViewGetOverlayPixbufFunc) (QuiverIconView *iconview,gulong cell, QuiverIconOverlayType type,gpointer user_data);
#endif

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
void quiver_icon_view_set_thumbnails_square(QuiverIconView *iconview, gboolean square);
gboolean quiver_icon_view_get_thumbnails_square(QuiverIconView *iconview);

/* Filmstrip (sprocket-hole) decoration.
 *
 * The icon view can draw a repeating sprocket-hole strip along the left and
 * right edges of each video thumbnail at snapshot time.  This is decoration
 * only: it is rendered on top of the thumbnail in the snapshot and is never
 * baked into (or saved from) the thumbnail texture, so the files stored by
 * the app (including the freedesktop cache) always stay undecorated.
 *
 * A per-cell callback supplies the strip for each side.  The sides are two
 * independent slots so an app can supply a different (e.g. mirrored) asset
 * per side if it wants to, though nothing requires it: an app that only has
 * one shared pattern can ignore the side argument.  A value of NULL from the
 * callback means "no strip on this side".  The callback may be (un)set at any
 * time; setting it to NULL removes the decoration.
 */
typedef enum _QuiverIconViewFilmstripSide
{
	QUIVER_ICON_VIEW_FILMSTRIP_LEFT = 0,
	QUIVER_ICON_VIEW_FILMSTRIP_RIGHT,
	QUIVER_ICON_VIEW_FILMSTRIP_COUNT
} QuiverIconViewFilmstripSide;

/* Called for each side of a cell.  Provides the natural (source) thumbnail
 * size and the size the thumbnail is drawn at in the cell, so the callback
 * can pick an appropriately-tweaked asset and scale it proportionally with
 * the thumbnail.  Returns a texture to tile on that side, or NULL for none. */
typedef GdkTexture* (*QuiverIconViewGetFilmstripTextureFunc) (
	QuiverIconView *iconview, gulong cell,
	gint thumb_natural_w, gint thumb_natural_h,
	gint thumb_drawn_w, gint thumb_drawn_h,
	QuiverIconViewFilmstripSide side,
	gpointer user_data);

void quiver_icon_view_set_get_filmstrip_texture_func(
	QuiverIconView *iconview,
	QuiverIconViewGetFilmstripTextureFunc func,
	gpointer user_data, GDestroyNotify destroy_notify);

gboolean quiver_icon_view_get_filmstrip_enabled(QuiverIconView *iconview);
void quiver_icon_view_set_filmstrip_enabled(QuiverIconView *iconview, gboolean enabled);
void quiver_icon_view_get_icon_size(QuiverIconView *iconview, guint* width,guint* height);
guint quiver_icon_view_get_cell_padding(QuiverIconView *iconview);
guint quiver_icon_view_get_cell_width(QuiverIconView *iconview);
guint quiver_icon_view_get_cell_height(QuiverIconView *iconview);
void quiver_icon_view_activate_cell(QuiverIconView *iconview,gulong cell);
GdkModifierType quiver_icon_view_get_last_activate_modifiers(QuiverIconView *iconview);
void quiver_icon_view_set_last_activate_modifiers(QuiverIconView *iconview, GdkModifierType mods);

gulong quiver_icon_view_get_cursor_cell(QuiverIconView *iconview);
void quiver_icon_view_set_cursor_cell(QuiverIconView *iconview,gulong new_cursor_cell);
void quiver_icon_view_set_cursor_cell_silent(QuiverIconView *iconview,gulong new_cursor_cell);

gulong quiver_icon_view_get_prelight_cell(QuiverIconView* iconview);
gulong quiver_icon_view_get_drop_cell(QuiverIconView* iconview);
void quiver_icon_view_set_drop_cell(QuiverIconView* iconview, gulong drop_cell);
gulong quiver_icon_view_get_cell_for_xy(QuiverIconView *iconview,gint x, gint y);

typedef void (*QuiverIconViewScrollCallback)(QuiverIconView *iconview, gulong cell, gpointer user_data);

void quiver_icon_view_get_cell_mouse_position(QuiverIconView* iconview, guint cell, gint *x, gint *y);
gboolean quiver_icon_view_get_cell_rect(QuiverIconView *iconview, gulong cell, GdkRectangle *rect);
gboolean quiver_icon_view_get_cell_target_rect(QuiverIconView *iconview, gulong cell, GdkRectangle *rect);
gboolean quiver_icon_view_is_cell_visible(QuiverIconView *iconview, gulong cell);

void quiver_icon_view_scroll_to_cell_with_callback(
	QuiverIconView *iconview,
	gulong cell,
	QuiverIconViewScrollCallback callback,
	gpointer user_data);

void quiver_icon_view_set_selection(QuiverIconView *iconview,const GList *selection);
GList* quiver_icon_view_get_selection(QuiverIconView *iconview);
gboolean quiver_icon_view_is_cell_selected(QuiverIconView *iconview, gulong cell);

void quiver_icon_view_get_visible_range(QuiverIconView *iconview,gulong *first, gulong *last);

void quiver_icon_view_invalidate_window(QuiverIconView *iconview);

void quiver_icon_view_invalidate_cell(QuiverIconView *iconview,gulong cell);

void quiver_icon_view_set_n_items_func (QuiverIconView *iconview, 
         QuiverIconViewGetNItemsFunc func,gpointer data,GDestroyNotify destroy);

void quiver_icon_view_set_thumbnail_texture_func (QuiverIconView *iconview,
         QuiverIconViewGetThumbnailTextureFunc func,gpointer data,GDestroyNotify destroy);

void quiver_icon_view_set_icon_texture_func (QuiverIconView *iconview,
         QuiverIconViewGetIconTextureFunc func,gpointer data,GDestroyNotify destroy);

void quiver_icon_view_set_overlay_texture_func (QuiverIconView *iconview,
         QuiverIconViewGetOverlayTextureFunc func,gpointer data,GDestroyNotify destroy);

void quiver_icon_view_set_text_func (QuiverIconView *iconview,
         QuiverIconViewGetTextFunc func,gpointer data,GDestroyNotify destroy);

#if HAVE_GDK_PIXBUF
void quiver_icon_view_set_icon_pixbuf_func (QuiverIconView *iconview,
         QuiverIconViewGetIconPixbufFunc func,gpointer data,GDestroyNotify destroy);

void quiver_icon_view_set_thumbnail_pixbuf_func (QuiverIconView *iconview,
         QuiverIconViewGetThumbnailPixbufFunc func,gpointer data,GDestroyNotify destroy);

void quiver_icon_view_set_overlay_pixbuf_func (QuiverIconView *iconview,
         QuiverIconViewGetOverlayPixbufFunc func,gpointer data,GDestroyNotify destroy);
#endif

GdkPaintable* quiver_icon_view_create_drag_icon (QuiverIconView *iconview,
                                                 gint *hot_x,
                                                 gint *hot_y);

G_END_DECLS


#endif

