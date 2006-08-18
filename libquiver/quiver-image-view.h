
#ifndef QUIVER_IMAGE_VIEW_H
#define QUIVER_IMAGE_VIEW_H


#include <gtk/gtk.h>

G_BEGIN_DECLS

#define QUIVER_TYPE_IMAGE_VIEW            (quiver_image_view_get_type ())
#define QUIVER_IMAGE_VIEW(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), QUIVER_TYPE_IMAGE_VIEW, QuiverImageView))
#define QUIVER_IMAGE_VIEW_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), QUIVER_TYPE_IMAGE_VIEW, QuiverImageViewClass))
#define QUIVER_IS_IMAGE_VIEW(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), QUIVER_TYPE_IMAGE_VIEW))
#define QUIVER_IS_IMAGE_VIEW_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), QUIVER_TYPE_IMAGE_VIEW))
#define QUIVER_IMAGE_VIEW_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), QUIVER_TYPE_IMAGE_VIEW, QuiverImageViewClass))

typedef struct _QuiverImageView        QuiverImageView;
typedef struct _QuiverImageViewClass   QuiverImageViewClass;
typedef struct _QuiverImageViewPrivate QuiverImageViewPrivate;


typedef enum _QuiverImageViewMouseMode {
	QUIVER_IMAGE_VIEW_MOUSE_MODE_DRAG,
	QUIVER_IMAGE_VIEW_MOUSE_MODE_SELECT,
	QUIVER_IMAGE_VIEW_MOUSE_MODE_COUNT
} QuiverImageViewMouseMode;

typedef enum _QuiverImageViewMode {
	QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW,
	QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW_STRETCH,
	QUIVER_IMAGE_VIEW_MODE_ACTUAL_SIZE,
	QUIVER_IMAGE_VIEW_MODE_ZOOM,
	QUIVER_IMAGE_VIEW_MODE_COUNT
} QuiverImageViewMode;

typedef enum _QuiverImageViewTransitionType{
	QUIVER_IMAGE_VIEW_TRANSITION_TYPE_NONE,
	QUIVER_IMAGE_VIEW_TRANSITION_TYPE_CROSSFADE,
	QUIVER_IMAGE_VIEW_TRANSITION_TYPE_COUNT
} QuiverImageViewTransitionType;

typedef enum QuiverImageViewMagnificationMode{
	QUIVER_IMAGE_VIEW_MAGNIFICATION_MODE_DEFAULT,
	QUIVER_IMAGE_VIEW_MAGNIFICATION_MODE_SMOOTH,
	QUIVER_IMAGE_VIEW_MAGNIFICATION_MODE_COUNT
} QuiverImageViewMagnificationMode;


struct _QuiverImageView
{
	GtkWidget parent;

	/* private */
	QuiverImageViewPrivate *priv;
};

struct _QuiverImageViewClass
{
	GtkWidgetClass parent_class;

	void  (*set_scroll_adjustments)   (QuiverImageView	    *imageview,
					 GtkAdjustment  *hadjustment,
					 GtkAdjustment  *vadjustment);

	void (*activated) (QuiverImageView *imageview);
	void (*reload) (QuiverImageView *imageview);

	/* Padding for future expansion */
	
	void (*_gtk_reserved1) (void);
	void (*_gtk_reserved2) (void);
	void (*_gtk_reserved3) (void);
	void (*_gtk_reserved4) (void);

};



GType	   quiver_image_view_get_type (void) G_GNUC_CONST;
GtkWidget *quiver_image_view_new ();

void quiver_image_view_set_n_columns(QuiverImageView *imageview,guint n_columns);
void quiver_image_view_set_n_rows(QuiverImageView *imageview,guint n_rows);
void quiver_image_view_set_smooth_scroll(QuiverImageView *imageview,gboolean smooth_scroll);

void quiver_image_view_set_size(QuiverImageView *imageview, guint width,guint height);

GdkPixbuf* quiver_image_view_get_pixbuf(QuiverImageView *imageview);
void quiver_image_view_set_pixbuf(QuiverImageView *imageview, GdkPixbuf *pixbuf);
void quiver_image_view_set_pixbuf_at_size(QuiverImageView *imageview, GdkPixbuf *pixbuf,int width, int height);
void quiver_image_view_set_pixbuf_at_size_ex(QuiverImageView *imageview, GdkPixbuf *pixbuf,int width , int height, gboolean reset_view_mode);

QuiverImageViewMode quiver_image_view_get_view_mode(QuiverImageView *imageview);
void quiver_image_view_set_view_mode(QuiverImageView *imageview,QuiverImageViewMode mode);
void quiver_image_view_set_transition_type(QuiverImageView *imageview,QuiverImageViewTransitionType type);
void quiver_image_view_set_magnification(QuiverImageView *imageview,gdouble amount);
void quiver_image_view_set_magnification_mode(QuiverImageView *imageview,QuiverImageViewMagnificationMode mode);
gdouble quiver_image_view_get_magnification(QuiverImageView *imageview);

void quiver_image_view_get_pixbuf_display_size_for_mode(QuiverImageView *imageview, QuiverImageViewMode mode, gint *width, gint *height);
void quiver_image_view_get_pixbuf_display_size_for_mode_alt(QuiverImageView *imageview, QuiverImageViewMode mode, gint in_width, gint in_height, gint *out_width, gint *out_height);

void quiver_image_view_rotate(QuiverImageView *imageview, gboolean clockwise);
void quiver_image_view_flip(QuiverImageView *imageview, gboolean horizontal);

void quiver_image_view_connect_pixbuf_loader_signals(QuiverImageView *imageview,GdkPixbufLoader *loader);
void quiver_image_view_connect_pixbuf_size_prepared_signal(QuiverImageView *imageview,GdkPixbufLoader *loader);

GtkAdjustment * quiver_image_view_get_hadjustment(QuiverImageView *imageview);
GtkAdjustment * quiver_image_view_get_vadjustment(QuiverImageView *imageview);

void quiver_image_view_activate(QuiverImageView *iconview);

/*
void quiver_image_view_set_n_items_func (QuiverImageView *imageview, 
         QuiverImageViewGetNItemsFunc func,gpointer data,GtkDestroyNotify destroy);

void quiver_image_view_set_icon_pixbuf_func (QuiverImageView *imageview,
         QuiverImageViewGetIconPixbufFunc func,gpointer data,GtkDestroyNotify destroy);

void quiver_image_view_set_thumbnail_pixbuf_func (QuiverImageView *imageview,
         QuiverImageViewGetThumbnailPixbufFunc func,gpointer data,GtkDestroyNotify destroy);

void quiver_image_view_set_text_func (QuiverImageView *imageview,
         QuiverImageViewGetTextFunc func,gpointer data,GtkDestroyNotify destroy);

void quiver_image_view_set_overlay_pixbuf_func (QuiverImageView *imageview,
         QuiverImageViewGetOverlayPixbufFunc func,gpointer data,GtkDestroyNotify destroy);

*/
G_END_DECLS

/*
#include <string>
#include <list>
using namespace std;


//GtkWidget* quiver_image_view_new		(void);
GtkWidget* quiver_image_view_new		(list<string> *files);

*/
#endif

