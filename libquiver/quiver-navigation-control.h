
#ifndef QUIVER_NAVIGATION_CONTROL_H
#define QUIVER_NAVIGATION_CONTROL_H


#include <gtk/gtk.h>

G_BEGIN_DECLS

#define QUIVER_TYPE_NAVIGATION_CONTROL            (quiver_navigation_control_get_type ())
#define QUIVER_NAVIGATION_CONTROL(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), QUIVER_TYPE_NAVIGATION_CONTROL, QuiverNavigationControl))
#define QUIVER_NAVIGATION_CONTROL_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), QUIVER_TYPE_NAVIGATION_CONTROL, QuiverNavigationControlClass))
#define QUIVER_IS_NAVIGATION_CONTROL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), QUIVER_TYPE_NAVIGATION_CONTROL))
#define QUIVER_IS_NAVIGATION_CONTROL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), QUIVER_TYPE_NAVIGATION_CONTROL))
#define QUIVER_NAVIGATION_CONTROL_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), QUIVER_TYPE_NAVIGATION_CONTROL, QuiverNavigationControlClass))

typedef struct _QuiverNavigationControl        QuiverNavigationControl;
typedef struct _QuiverNavigationControlClass   QuiverNavigationControlClass;
typedef struct _QuiverNavigationControlPrivate QuiverNavigationControlPrivate;

struct _QuiverNavigationControl
{
	GtkWidget parent;

	/* private */
	QuiverNavigationControlPrivate *priv;
};

struct _QuiverNavigationControlClass
{
	GtkWidgetClass parent_class;

	/* Padding for future expansion */
	
	void (*_reserved1) (void);
	void (*_reserved2) (void);
	void (*_reserved3) (void);
	void (*_reserved4) (void);
	void (*_reserved5) (void);

};



GType	   quiver_navigation_control_get_type (void) G_GNUC_CONST;
GtkWidget *quiver_navigation_control_new ();
GtkWidget *quiver_navigation_control_new_with_adjustments (GtkAdjustment *hadjust, GtkAdjustment *vadjust);

void       quiver_navigation_control_set_pixbuf(QuiverNavigationControl *navcontrol, GdkPixbuf *pixbuf);


G_END_DECLS

#endif

