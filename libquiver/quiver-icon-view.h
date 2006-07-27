
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

typedef guint (*QuiverIconViewGetNItemsFunc) (QuiverIconView *iconview,gpointer user_data);
typedef gchar* (*QuiverIconViewGetTextFunc) (QuiverIconView *iconview,guint cell,gpointer user_data);
typedef GdkPixbuf* (*QuiverIconViewGetIconPixbufFunc) (QuiverIconView *iconview,guint cell,gpointer user_data);
typedef GdkPixbuf* (*QuiverIconViewGetThumbnailPixbufFunc) (QuiverIconView *iconview,guint cell,gpointer user_data);
typedef GdkPixbuf* (*QuiverIconViewGetOverlayPixbufFunc) (QuiverIconView *iconview,guint cell, QuiverIconOverlayType type,gpointer user_data);
//typedef GdkPixbuf* (*QuiverIconViewGetOverlayPixbufFunc) (QuiverIconView *iconview, guint cell, QuiverIconOverlayType type, gpointer user_data);

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

	void (*cell_activated) (QuiverIconView *iconview,guint cell);
	void (*cursor_changed) (QuiverIconView *iconview,guint cell);
	void (*selection_changed) (QuiverIconView *iconview);

	/* Padding for future expansion */
	
	void (*_gtk_reserved1) (void);
	void (*_gtk_reserved2) (void);
	void (*_gtk_reserved3) (void);
	void (*_gtk_reserved4) (void);

};



GType	   quiver_icon_view_get_type (void) G_GNUC_CONST;
GtkWidget *quiver_icon_view_new ();

void quiver_icon_view_set_n_columns(QuiverIconView *iconview,guint n_columns);
void quiver_icon_view_set_n_rows(QuiverIconView *iconview,guint n_rows);
void quiver_icon_view_set_smooth_scroll(QuiverIconView *iconview,gboolean smooth_scroll);

void quiver_icon_view_set_icon_size(QuiverIconView *iconview, guint width,guint height);
void quiver_icon_view_set_cell_padding(QuiverIconView *iconview,guint padding);
void quiver_icon_view_get_icon_size(QuiverIconView *iconview, guint* width,guint* height);
guint quiver_icon_view_get_cell_padding(QuiverIconView *iconview);
guint quiver_icon_view_get_cell_width(QuiverIconView *iconview);
guint quiver_icon_view_get_cell_height(QuiverIconView *iconview);
void quiver_icon_view_activate_cell(QuiverIconView *iconview,guint cell);

gint quiver_icon_view_get_cursor_cell(QuiverIconView *iconview);
void quiver_icon_view_set_cursor_cell(QuiverIconView *iconview,gint new_cursor_cell);

void quiver_icon_view_set_selection(QuiverIconView *iconview,const GList *selection);
GList* quiver_icon_view_get_selection(QuiverIconView *iconview);

void quiver_icon_view_get_visible_range(QuiverIconView *iconview,guint *first, guint *last);
void quiver_icon_view_invalidate_cell(QuiverIconView *iconview,guint cell);

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

/*
#include <string>
#include <list>
using namespace std;


//GtkWidget* quiver_icon_view_new		(void);
GtkWidget* quiver_icon_view_new		(list<string> *files);

*/
#endif

