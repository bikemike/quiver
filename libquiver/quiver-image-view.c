#include <config.h>
#include <gtk/gtk.h>
#include <gtk/gtksnapshot.h> // Explicit include for snapshot functions

#include <math.h>
#include <string.h>
#include <sys/time.h>

#include "quiver-pixbuf-utils.h"
#include "quiver-image-view.h"
#include "quiver-marshallers.h"

//#include "gtkintl.h"


#define QUIVER_IMAGE_VIEW_GET_PRIVATE(obj) (quiver_image_view_get_instance_private ((obj)))

/* set up some defaults */
#define QUIVER_IMAGE_VIEW_MAG_MAX              50.
#define QUIVER_IMAGE_VIEW_MIN_IMAGE_SIZE       32
#define QUIVER_IMAGE_VIEW_SCALE_HQ_TIMEOUT     200

#define TRANSITION_FPS           30.
#define TRANSITION_MIN_TIMEOUT   5.
#define TRANSITION_TIME          .5   //seconds

#define SMOOTH_SCROLL_TIMEOUT                    35 // 35 ms ~= 28fps

#define QUIVER_PARAM_READWRITE G_PARAM_READWRITE|G_PARAM_STATIC_NAME|G_PARAM_STATIC_NICK|G_PARAM_STATIC_BLURB

G_DEFINE_TYPE_WITH_CODE(QuiverImageView,quiver_image_view,GTK_TYPE_WIDGET, G_IMPLEMENT_INTERFACE(GTK_TYPE_SCROLLABLE, NULL) G_ADD_PRIVATE(QuiverImageView));

#if (GLIB_MAJOR_VERSION < 2) || (GLIB_MAJOR_VERSION == 2 && GLIB_MINOR_VERSION < 10)
#define g_object_ref_sink(o) G_STMT_START{	\
	  g_object_ref (o);				\
	  gtk_object_sink ((GtkObject*)o);		\
}G_STMT_END
#endif


/* start private data structures */


/* signals */
enum {
	SIGNAL_ACTIVATED,
	SIGNAL_RELOAD,
	SIGNAL_MAGNIFICATION_CHANGED,
	SIGNAL_VIEW_MODE_CHANGED,
	SIGNAL_COUNT
};

static guint imageview_signals[SIGNAL_COUNT] = {0};

/* properties */
/* properties */
enum {
   PROP_0,
   PROP_HADJUSTMENT,
   PROP_VADJUSTMENT,
   PROP_HSCROLL_POLICY,
   PROP_VSCROLL_POLICY,

/*
   PROP_N_ITEMS,
   PROP_ICON_PIXBUF,
   PROP_THUMBNAIL_PIXBUF,
   PROP_TEXT
*/
};

typedef struct _VelocityTimeStruct 
{
	gdouble velocity;
	gdouble angle;
	gdouble time;

} VelocityTimeStruct;




/* end private data structures */


/* start private function prototypes */
static void quiver_image_view_map (GtkWidget *widget);
static void quiver_image_view_unmap (GtkWidget *widget);
static void quiver_image_view_dispose (GObject *object); // For GObject resource cleanup

static void quiver_image_view_realize        (GtkWidget *widget);
static void quiver_image_view_size_allocate (GtkWidget *widget, int width, int height, int baseline);
static void quiver_image_view_measure (GtkWidget *widget, GtkOrientation orientation, int for_size, int *minimum, int *natural, int *minimum_baseline, int *natural_baseline);
static void quiver_image_view_snapshot (GtkWidget *widget, GtkSnapshot *snapshot);

// New Event Controller Callbacks
static void quiver_image_view_handle_click_pressed (GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data);
static void quiver_image_view_handle_click_released (GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data);
static void quiver_image_view_handle_drag_begin (GtkGestureDrag *gesture, double start_x, double start_y, gpointer user_data);
static void quiver_image_view_handle_drag_update (GtkGestureDrag *gesture, double offset_x, double offset_y, gpointer user_data);
static void quiver_image_view_handle_drag_end (GtkGestureDrag *gesture, double offset_x, double offset_y, gpointer user_data);
static gboolean quiver_image_view_handle_scroll (GtkEventControllerScroll *controller, double dx, double dy, gpointer user_data);
static void quiver_image_view_handle_motion (GtkEventControllerMotion *controller, double x, double y, gpointer user_data);


static void      quiver_image_view_set_hadjustment (QuiverImageView *imageview,
                    GtkAdjustment *hadjustment);
static void      quiver_image_view_set_vadjustment (QuiverImageView *imageview,
                    GtkAdjustment *vadjustment);

static void      quiver_image_view_adjustment_value_changed (GtkAdjustment *adjustment,
                    QuiverImageView *imageview);

static void      quiver_image_view_set_property (GObject *object,
                    guint               prop_id,
                    const GValue       *value,
                    GParamSpec         *pspec);
static void      quiver_image_view_get_property (GObject    *object,
                    guint       prop_id,
                    GValue     *value,
                    GParamSpec *pspec);

static void     quiver_image_view_finalize(GObject *object);

/* start utility function prototypes*/
static void quiver_image_view_send_reload_event(QuiverImageView *imageview);
static guint quiver_image_view_get_width(QuiverImageView *imageview);
static guint quiver_image_view_get_height(QuiverImageView *imageview);
static void
quiver_image_view_set_adjustment_upper (GtkAdjustment *adj,
				 gdouble        upper,
				 gboolean       always_emit_changed);


static void quiver_image_view_add_scale_hq_timeout(QuiverImageView *imageview);
static gboolean quiver_image_view_timeout_scale_hq(gpointer data);
static void quiver_image_view_prepare_transition_pixbufs(QuiverImageView *imageview);
static void quiver_image_view_create_next_transition_pixbuf(QuiverImageView *imageview);
static void quiver_image_view_create_scaled_pixbuf(QuiverImageView *imageview,GdkInterpType interptype);

static gboolean quiver_image_view_timeout_scroll(gpointer data);
static void quiver_image_view_add_scroll_timeout(QuiverImageView *imageview);
static void quiver_image_view_scroll(QuiverImageView *imageview);

static void quiver_image_view_start_animation(QuiverImageView *imageview);
static void quiver_image_view_add_animation_timeout(QuiverImageView *imageview);
static gboolean quiver_image_view_timeout_animation(gpointer data);

static void quiver_image_view_add_transition_timeout(QuiverImageView *imageview);
static gboolean quiver_image_view_timeout_transition(gpointer data);
static void quiver_image_view_transition_start(QuiverImageView *imageview);
static void quiver_image_view_transition_stop(QuiverImageView *imageview);

static void quiver_image_view_set_magnification_full(QuiverImageView *imageview,gdouble new_mag);
static void quiver_image_view_add_magnification_timeout(QuiverImageView *imageview);
static gboolean quiver_image_view_timeout_magnification(gpointer data);
//static void quiver_image_view_magnification_start(QuiverImageView *imageview);

static void quiver_image_view_get_pixbuf_display_size(QuiverImageView *imageview, gint *width, gint *height);
static void quiver_image_view_get_pixbuf_display_size_alt(QuiverImageView *imageview,QuiverImageViewMode mode, gint in_width, gint in_height, gint *out_width, gint *out_height);

static void quiver_image_view_set_default_adjustment_values(QuiverImageView *imageview);

static void quiver_image_view_invalidate_old_image_area(QuiverImageView *imageview,gint new_width, gint new_height);
static void quiver_image_view_invalidate_image_area(QuiverImageView *imageview,cairo_rectangle_int_t *rect);

static void quiver_image_view_set_view_mode_full(QuiverImageView *imageview,QuiverImageViewMode mode,gboolean invalidate);

static void quiver_image_view_update_size(QuiverImageView *imageview);

static void quiver_image_view_prepare_for_new_pixbuf(QuiverImageView *imageview, gint new_width, gint new_height);

/* start pixbuf loader callbacks */
static void pixbuf_loader_size_prepared(GdkPixbufLoader *loader,gint width, gint height,gpointer userdata);
static void pixbuf_loader_area_prepared(GdkPixbufLoader *loader,gpointer userdata);
static void pixbuf_loader_area_updated (GdkPixbufLoader *loader,gint x, gint y, gint width,gint height,gpointer userdata);
static void pixbuf_loader_closed(GdkPixbufLoader *loader,gpointer userdata);
/* end pixbuf loader callbacks */

/* end utility function prototypes*/

// Static function forward declarations (if used before definition)
static void draw_pixbuf(QuiverImageView *imageview, cairo_t *cr);
static void quiver_image_view_send_reload_event(QuiverImageView *imageview);
static void quiver_image_view_prepare_transition_pixbufs(QuiverImageView *imageview);
static void quiver_image_view_create_next_transition_pixbuf(QuiverImageView *imageview);
static void quiver_image_view_create_scaled_pixbuf(QuiverImageView *imageview,GdkInterpType interptype);


/* start private globals */

//static guint imageview_signals[SIGNAL_COUNT] = {0};

/* end private globals */


/* start private functions */
static void 
quiver_image_view_class_init (QuiverImageViewClass *klass)
{
	GtkWidgetClass *widget_class;
	GObjectClass *obj_class;

	widget_class = GTK_WIDGET_CLASS (klass);
	obj_class = G_OBJECT_CLASS (klass);

	widget_class->realize              = quiver_image_view_realize;
	widget_class->size_allocate        = quiver_image_view_size_allocate;
	widget_class->measure = quiver_image_view_measure;
    widget_class->snapshot             = quiver_image_view_snapshot; // GTK4 way

    widget_class->map = quiver_image_view_map;
    widget_class->unmap = quiver_image_view_unmap;
    obj_class->dispose = quiver_image_view_dispose;


	obj_class->finalize                = quiver_image_view_finalize;
	obj_class->set_property            = quiver_image_view_set_property;
	obj_class->get_property            = quiver_image_view_get_property;

	/* Override properties */
	g_object_class_override_property (obj_class, PROP_HADJUSTMENT, "hadjustment");
	g_object_class_override_property (obj_class, PROP_VADJUSTMENT, "vadjustment");
	g_object_class_override_property (obj_class, PROP_HSCROLL_POLICY, "hscroll-policy");
	g_object_class_override_property (obj_class, PROP_VSCROLL_POLICY, "vscroll-policy");

	imageview_signals[SIGNAL_ACTIVATED] = g_signal_new (/*FIXME: I_*/("activated"),
		G_TYPE_FROM_CLASS (obj_class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (QuiverImageViewClass, activated),
		NULL, NULL,
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE, 0);

	imageview_signals[SIGNAL_RELOAD] = g_signal_new (/*FIXME: I_*/("reload"),
		G_TYPE_FROM_CLASS (obj_class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (QuiverImageViewClass, activated),
		NULL, NULL,
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE, 0);
		
	imageview_signals[SIGNAL_MAGNIFICATION_CHANGED] = g_signal_new (/*FIXME: I_*/("magnification-changed"),
		G_TYPE_FROM_CLASS (obj_class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (QuiverImageViewClass, magnification_changed),
		NULL, NULL,
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE, 0);

	imageview_signals[SIGNAL_VIEW_MODE_CHANGED] = g_signal_new (/*FIXME: I_*/("view-mode-changed"),
		G_TYPE_FROM_CLASS (obj_class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (QuiverImageViewClass, view_mode_changed),
		NULL, NULL,
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE, 0);

}

static void 
quiver_image_view_init(QuiverImageView *imageview)
{
	QuiverImageViewPrivate *priv = quiver_image_view_get_instance_private(imageview);

	priv->pixbuf        = NULL;
	priv->pixbuf_scaled = NULL;
	priv->pixbuf_animation = NULL;
	priv->pixbuf_animation_iter = NULL;
	
	priv->pixbuf_width  = 0;
	priv->pixbuf_height = 0;

	priv->pixbuf_width_next  = 0;
	priv->pixbuf_height_next = 0;
	
	priv->view_mode = QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW;
	priv->view_mode_last = QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW;
	
	priv->transitions_enabled = FALSE;
	priv->transition_n_frames = 10;
	priv->transition_pixbuf_old = NULL;
	priv->transition_pixbuf_new = NULL;
	priv->transition_pixbufs_intermediate = NULL;
	priv->transition_timeout_id = 0;
	priv->idle_transition_create_id = 0;


	priv->magnification = 1; // magnification level as a percent (1 = 100%)
	priv->magnification_timeout_id = 0; //
	priv->magnification_final = 1;
	
	priv->timeout_scale_hq_id = 0;

	priv->hadjustment  = NULL;
	priv->vadjustment  = NULL;

	priv->scroll_timeout_id = 0;
	priv->last_hadjustment = 0.0;
	priv->last_vadjustment = 0.0;

	priv->area_updated = FALSE;
	priv->animation_timeout_id = FALSE;

	priv->scroll_draw   = TRUE;
	priv->smooth_scroll = FALSE;
	
	priv->reload_event_sent = FALSE;
	
	priv->mouse_move_mode = QUIVER_IMAGE_VIEW_MOUSE_MODE_DRAG;

	priv->rubberband_mode_start = FALSE;
	priv->rubberband_mode = FALSE;
	priv->dragging_view = FALSE;
	priv->press_x = 0;
	priv->press_y = 0;

	gtk_widget_set_can_focus(GTK_WIDGET(imageview), TRUE);

    gtk_widget_set_size_request(GTK_WIDGET(imageview),QUIVER_IMAGE_VIEW_MIN_IMAGE_SIZE,QUIVER_IMAGE_VIEW_MIN_IMAGE_SIZE);

    // Create and connect event controllers
    priv->click_gesture = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(priv->click_gesture), 0); // Any button
    g_signal_connect(priv->click_gesture, "pressed", G_CALLBACK(quiver_image_view_handle_click_pressed), imageview);
    g_signal_connect(priv->click_gesture, "released",G_CALLBACK(quiver_image_view_handle_click_released), imageview);

    priv->drag_gesture = gtk_gesture_drag_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(priv->drag_gesture), GDK_BUTTON_PRIMARY);
    g_signal_connect(priv->drag_gesture, "drag-begin", G_CALLBACK(quiver_image_view_handle_drag_begin), imageview);
    g_signal_connect(priv->drag_gesture, "drag-update", G_CALLBACK(quiver_image_view_handle_drag_update), imageview);
    g_signal_connect(priv->drag_gesture, "drag-end", G_CALLBACK(quiver_image_view_handle_drag_end), imageview);

    priv->scroll_controller = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
     g_signal_connect(priv->scroll_controller, "scroll", G_CALLBACK(quiver_image_view_handle_scroll), imageview);

    priv->motion_controller = gtk_event_controller_motion_new();
    g_signal_connect(priv->motion_controller, "motion", G_CALLBACK(quiver_image_view_handle_motion), imageview);
}

static void
quiver_image_view_map(GtkWidget *widget)
{
    QuiverImageView *self = QUIVER_IMAGE_VIEW(widget);
    QuiverImageViewPrivate *priv = quiver_image_view_get_instance_private(self);

    GTK_WIDGET_CLASS(quiver_image_view_parent_class)->map(widget);

    gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(priv->click_gesture));
    gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(priv->drag_gesture));
    gtk_widget_add_controller(widget, priv->scroll_controller);
    gtk_widget_add_controller(widget, priv->motion_controller);
}

static void
quiver_image_view_unmap(GtkWidget *widget)
{
    QuiverImageView *self = QUIVER_IMAGE_VIEW(widget);
    QuiverImageViewPrivate *priv = quiver_image_view_get_instance_private(self);

    gtk_widget_remove_controller(widget, GTK_EVENT_CONTROLLER(priv->click_gesture));
    gtk_widget_remove_controller(widget, GTK_EVENT_CONTROLLER(priv->drag_gesture));
    gtk_widget_remove_controller(widget, priv->scroll_controller);
    gtk_widget_remove_controller(widget, priv->motion_controller);

    GTK_WIDGET_CLASS(quiver_image_view_parent_class)->unmap(widget);
}

static void
quiver_image_view_dispose(GObject *object)
{
    QuiverImageView *self = QUIVER_IMAGE_VIEW(object);
    QuiverImageViewPrivate *priv = quiver_image_view_get_instance_private(self);

    g_clear_object(&priv->click_gesture);
    g_clear_object(&priv->drag_gesture);
    g_clear_object(&priv->scroll_controller);
    g_clear_object(&priv->motion_controller);

    G_OBJECT_CLASS(quiver_image_view_parent_class)->dispose(object);
}


static void
quiver_image_view_realize (GtkWidget *widget)
{
	QuiverImageView *imageview;

	g_return_if_fail (QUIVER_IS_IMAGE_VIEW (widget));

	imageview = QUIVER_IMAGE_VIEW (widget);

    GTK_WIDGET_CLASS(quiver_image_view_parent_class)->realize(widget);

    g_object_set_data(G_OBJECT(widget), "quiver-image-view", imageview);
}


static void
quiver_image_view_set_property (GObject *object,
                    guint               prop_id,
                    const GValue       *value,
                    GParamSpec         *pspec)
{
	QuiverImageView *imageview = QUIVER_IMAGE_VIEW(object);
    QuiverImageViewPrivate *priv = quiver_image_view_get_instance_private(imageview);

	switch (prop_id)
	{
		case PROP_HADJUSTMENT:
			quiver_image_view_set_hadjustment(imageview, g_value_get_object (value));
			break;
		case PROP_VADJUSTMENT:
			quiver_image_view_set_vadjustment(imageview, g_value_get_object (value));
			break;
		case PROP_HSCROLL_POLICY:
			priv->hscroll_policy = g_value_get_enum(value);
			gtk_widget_queue_resize(GTK_WIDGET(imageview));
			break;
		case PROP_VSCROLL_POLICY:
			priv->vscroll_policy = g_value_get_enum(value);
			gtk_widget_queue_resize(GTK_WIDGET(imageview));
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}

}

static void      
quiver_image_view_get_property (GObject    *object,
                    guint       prop_id,
                    GValue     *value,
                    GParamSpec *pspec)
{
	QuiverImageView *imageview = QUIVER_IMAGE_VIEW(object);
    QuiverImageViewPrivate *priv = quiver_image_view_get_instance_private(imageview);

	switch (prop_id)
	{
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



static void
quiver_image_view_finalize(GObject *object)
{
	QuiverImageView *imageview;

	imageview = QUIVER_IMAGE_VIEW(object);

	quiver_image_view_prepare_for_new_pixbuf(imageview,0,0);

	G_OBJECT_CLASS(quiver_image_view_parent_class)->finalize(object);
}


static void
quiver_image_view_size_allocate (GtkWidget     *widget,
				 int width, int height, int baseline)
{
	g_return_if_fail (QUIVER_IS_IMAGE_VIEW (widget));

    GTK_WIDGET_CLASS(quiver_image_view_parent_class)->size_allocate(widget, width, height, baseline);

	if (gtk_widget_get_mapped (widget))
	{
		quiver_image_view_update_size (QUIVER_IMAGE_VIEW (widget));
	}

}

static void quiver_image_view_measure (GtkWidget *widget, GtkOrientation orientation, int for_size, int *minimum, int *natural, int *minimum_baseline, int *natural_baseline)
{
    if (orientation == GTK_ORIENTATION_HORIZONTAL) {
        *minimum = *natural = 20; // Minimum width
    } else {
        *minimum = *natural = 20; // Minimum height
    }
    if (minimum_baseline) *minimum_baseline = -1;
    if (natural_baseline) *natural_baseline = -1;
}


static void quiver_image_view_snapshot (GtkWidget *widget, GtkSnapshot *snapshot)
{
	QuiverImageView *imageview = QUIVER_IMAGE_VIEW(widget);
    QuiverImageViewPrivate *priv = quiver_image_view_get_instance_private(imageview);

    cairo_t *cr = gtk_snapshot_append_cairo(snapshot, &GRAPHENE_RECT_INIT(0, 0, gtk_widget_get_width(widget), gtk_widget_get_height(widget)));

	cairo_set_source_rgb(cr,0.,0.,0.);
	cairo_paint(cr);

	if (NULL != priv->pixbuf)
	{
		draw_pixbuf(imageview, cr);
	}

    if (priv->rubberband_mode) {
        cairo_save(cr);
        cairo_rectangle_int_t normalized_rect = priv->rubberband_rect;
        if (normalized_rect.width < 0) {
            normalized_rect.x += normalized_rect.width;
            normalized_rect.width *= -1;
        }
        if (normalized_rect.height < 0) {
            normalized_rect.y += normalized_rect.height;
            normalized_rect.height *= -1;
        }

        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.7);
        double dashes[] = {4.0, 4.0};
        cairo_set_dash(cr, dashes, 2, 0);
        cairo_set_line_width(cr, 1.0);
        cairo_rectangle(cr, normalized_rect.x, normalized_rect.y, normalized_rect.width, normalized_rect.height);
        cairo_stroke(cr);
        cairo_restore(cr);
    }

    cairo_destroy(cr);
}

// Event handler implementations
static void
quiver_image_view_handle_click_pressed (GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data)
{
    QuiverImageView *self = QUIVER_IMAGE_VIEW(user_data);
    QuiverImageViewPrivate *priv = quiver_image_view_get_instance_private(self);

    gtk_widget_grab_focus(GTK_WIDGET(self));

    priv->press_x = x;
    priv->press_y = y;

    if (priv->mouse_move_mode == QUIVER_IMAGE_VIEW_MOUSE_MODE_SELECT) {
        priv->rubberband_mode_start = TRUE;
        priv->rubberband_rect.x = x;
        priv->rubberband_rect.y = y;
        priv->rubberband_rect.width = 0;
        priv->rubberband_rect.height = 0;
        priv->rubberband_rect_old = priv->rubberband_rect;
    }
}

static void
quiver_image_view_handle_click_released (GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data)
{
    QuiverImageView *self = QUIVER_IMAGE_VIEW(user_data);
    QuiverImageViewPrivate *priv = quiver_image_view_get_instance_private(self);

    if (gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture)) == GDK_BUTTON_PRIMARY) {
        if (n_press >= 2) {
            quiver_image_view_activate(self);
        }
    }

    if (priv->rubberband_mode) {
        priv->rubberband_mode = FALSE;
        priv->rubberband_mode_start = FALSE;
        cairo_rectangle_int_t final_rb_area = priv->rubberband_rect;
        if (final_rb_area.width < 0) {
            final_rb_area.x += final_rb_area.width;
            final_rb_area.width *= -1;
        }
        if (final_rb_area.height < 0) {
            final_rb_area.y += final_rb_area.height;
            final_rb_area.height *= -1;
        }
        final_rb_area.width = MAX(1, final_rb_area.width);
        final_rb_area.height = MAX(1, final_rb_area.height);
        quiver_image_view_invalidate_image_area(self, &final_rb_area);
    }
}

static void
quiver_image_view_handle_drag_begin (GtkGestureDrag *gesture, double start_x, double start_y, gpointer user_data)
{
    QuiverImageView *self = QUIVER_IMAGE_VIEW(user_data);
    QuiverImageViewPrivate *priv = quiver_image_view_get_instance_private(self);

    if (priv->mouse_move_mode == QUIVER_IMAGE_VIEW_MOUSE_MODE_DRAG) {
        priv->dragging_view = TRUE;
        gtk_widget_grab_focus(GTK_WIDGET(self));
    }
}

static void
quiver_image_view_handle_drag_update (GtkGestureDrag *gesture, double offset_x, double offset_y, gpointer user_data)
{
    QuiverImageView *self = QUIVER_IMAGE_VIEW(user_data);
    QuiverImageViewPrivate *priv = quiver_image_view_get_instance_private(self);
    double start_x, start_y;
    gtk_gesture_drag_get_start_point(gesture, &start_x, &start_y);

    if (priv->dragging_view) {
        gdouble new_hadjustment = gtk_adjustment_get_value(priv->hadjustment) - (start_x + offset_x - priv->press_x);
        gdouble new_vadjustment = gtk_adjustment_get_value(priv->vadjustment) - (start_y + offset_y - priv->press_y);

        new_hadjustment = MAX(0, MIN(new_hadjustment, gtk_adjustment_get_upper(priv->hadjustment) - gtk_adjustment_get_page_size(priv->hadjustment)));
        new_vadjustment = MAX(0, MIN(new_vadjustment, gtk_adjustment_get_upper(priv->vadjustment) - gtk_adjustment_get_page_size(priv->vadjustment)));

        gtk_adjustment_set_value(priv->hadjustment, new_hadjustment);
        gtk_adjustment_set_value(priv->vadjustment, new_vadjustment);

        priv->press_x = start_x + offset_x;
        priv->press_y = start_y + offset_y;
    }
}

static void
quiver_image_view_handle_drag_end (GtkGestureDrag *gesture, double offset_x, double offset_y, gpointer user_data)
{
    QuiverImageView *self = QUIVER_IMAGE_VIEW(user_data);
    QuiverImageViewPrivate *priv = quiver_image_view_get_instance_private(self);

    if (priv->dragging_view) {
        priv->dragging_view = FALSE;
        if (!priv->scroll_draw) {
             quiver_image_view_add_scale_hq_timeout(self);
        }
    }
}

static gboolean
quiver_image_view_handle_scroll (GtkEventControllerScroll *controller, double dx, double dy, gpointer user_data)
{
    QuiverImageView *self = QUIVER_IMAGE_VIEW(user_data);
    QuiverImageViewPrivate *priv = quiver_image_view_get_instance_private(self);
    GdkModifierType modifiers = gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(controller));

    if (modifiers & GDK_CONTROL_MASK) {
        if (dy < 0) {
            if (QUIVER_IMAGE_VIEW_MODE_ZOOM != priv->view_mode) {
                quiver_image_view_set_view_mode_full(self, QUIVER_IMAGE_VIEW_MODE_ZOOM, FALSE);
            }
            quiver_image_view_set_magnification(self, quiver_image_view_get_magnification(self) * 1.3);
        } else if (dy > 0) {
            if (QUIVER_IMAGE_VIEW_MODE_ZOOM != priv->view_mode) {
                quiver_image_view_set_view_mode_full(self, QUIVER_IMAGE_VIEW_MODE_ZOOM, FALSE);
            }
            quiver_image_view_set_magnification(self, quiver_image_view_get_magnification(self) / 1.3);
        }
        return TRUE;
    }
    return FALSE;
}

static void
quiver_image_view_handle_motion (GtkEventControllerMotion *controller, double x, double y, gpointer user_data)
{
    QuiverImageView *self = QUIVER_IMAGE_VIEW(user_data);
    QuiverImageViewPrivate *priv = quiver_image_view_get_instance_private(self);

    if (priv->rubberband_mode_start && !priv->dragging_view) {
        priv->rubberband_mode = TRUE;
        priv->rubberband_mode_start = FALSE;
    }

    if (priv->rubberband_mode && !priv->dragging_view) {
        priv->rubberband_rect_old = priv->rubberband_rect;
        priv->rubberband_rect.width = x - priv->press_x;
        priv->rubberband_rect.height = y - priv->press_y;

        gtk_widget_queue_draw(GTK_WIDGET(self));
    }
}


static GtkAdjustment*
new_default_adjustment (void)
{
  return gtk_adjustment_new (0.0, 0.0, 100.0, 10.0, 20.0, 0.0);
}

static void      quiver_image_view_set_hadjustment (QuiverImageView *imageview,
                    GtkAdjustment *hadjustment)
{
	QuiverImageViewPrivate *priv = quiver_image_view_get_instance_private(imageview);
	gboolean need_adjust = FALSE;

	g_return_if_fail (QUIVER_IS_IMAGE_VIEW (imageview));

	if (hadjustment)
		g_return_if_fail (GTK_IS_ADJUSTMENT (hadjustment));
	else
		hadjustment = new_default_adjustment ();

	if (priv->hadjustment && (priv->hadjustment != hadjustment))
	{
		g_signal_handlers_disconnect_by_func (priv->hadjustment,
			quiver_image_view_adjustment_value_changed,
			imageview);
		g_object_unref (priv->hadjustment);
	}

	if (priv->hadjustment != hadjustment)
	{
		priv->hadjustment = hadjustment;
		g_object_ref_sink (priv->hadjustment);
		guint width = quiver_image_view_get_width(imageview);
		quiver_image_view_set_adjustment_upper (priv->hadjustment, width, FALSE);

		g_signal_connect (priv->hadjustment, "value_changed",
		G_CALLBACK (quiver_image_view_adjustment_value_changed),
			imageview);
		need_adjust = TRUE;
	}

	if (need_adjust && hadjustment)
		quiver_image_view_adjustment_value_changed (NULL, imageview);
}

void quiver_image_view_set_vadjustment (QuiverImageView *imageview,
                    GtkAdjustment *vadjustment)
{
	QuiverImageViewPrivate *priv = quiver_image_view_get_instance_private(imageview);
	gboolean need_adjust = FALSE;

	g_return_if_fail (QUIVER_IS_IMAGE_VIEW (imageview));

	if (vadjustment)
		g_return_if_fail (GTK_IS_ADJUSTMENT (vadjustment));
	else
		vadjustment = new_default_adjustment ();

	if (priv->vadjustment && (priv->vadjustment != vadjustment))
	{
		g_signal_handlers_disconnect_by_func (priv->vadjustment,
			quiver_image_view_adjustment_value_changed,
			imageview);
		g_object_unref (priv->vadjustment);
	}

	if (priv->vadjustment != vadjustment)
	{
		priv->vadjustment = vadjustment;
		g_object_ref_sink (priv->vadjustment);
		guint height = quiver_image_view_get_height(imageview);
		quiver_image_view_set_adjustment_upper (priv->vadjustment, height, FALSE);

		g_signal_connect (priv->vadjustment, "value_changed",
		G_CALLBACK (quiver_image_view_adjustment_value_changed),
			imageview);
		need_adjust = TRUE;
	}

	if (need_adjust && vadjustment)
		quiver_image_view_adjustment_value_changed (NULL, imageview);
}

void quiver_image_view_add_scale_hq_timeout(QuiverImageView *imageview)
{
	QuiverImageViewPrivate *priv = quiver_image_view_get_instance_private(imageview);
	if (0 != priv->timeout_scale_hq_id)
	{
		g_source_remove(priv->timeout_scale_hq_id);
	}
	priv->timeout_scale_hq_id = g_timeout_add(QUIVER_IMAGE_VIEW_SCALE_HQ_TIMEOUT,quiver_image_view_timeout_scale_hq,imageview);
}

static gboolean 
quiver_image_view_timeout_scale_hq(gpointer data)
{
	QuiverImageView *imageview = (QuiverImageView*)data;
	QuiverImageViewPrivate *priv = quiver_image_view_get_instance_private(imageview);
	GtkWidget *widget = GTK_WIDGET(imageview);

	quiver_image_view_create_scaled_pixbuf(imageview,GDK_INTERP_BILINEAR);
	if (gtk_widget_get_mapped (widget))
	{
        gtk_widget_queue_draw(widget);
	}
	priv->timeout_scale_hq_id = 0;

	return FALSE;
}


static void 
quiver_image_view_scroll(QuiverImageView *imageview)
{
	QuiverImageViewPrivate *priv = quiver_image_view_get_instance_private(imageview);
	GtkWidget *widget = GTK_WIDGET(imageview);
	gdouble hadj,vadj;
	hadj = floor(gtk_adjustment_get_value(priv->hadjustment));
	vadj = floor(gtk_adjustment_get_value(priv->vadjustment));
	
	if (gtk_widget_get_mapped (GTK_WIDGET(imageview)))
	{
		if (priv->scroll_draw)
		{
			quiver_image_view_transition_stop(imageview);

			quiver_image_view_create_scaled_pixbuf(imageview,GDK_INTERP_NEAREST);

            gtk_widget_queue_draw(widget);

			quiver_image_view_add_scale_hq_timeout(imageview);
		}
		priv->last_vadjustment = vadj;
		priv->last_hadjustment = hadj;
	}
	priv->scroll_timeout_id = 0;
}

static gboolean 
quiver_image_view_timeout_scroll(gpointer data)
{
	QuiverImageView *imageview = (QuiverImageView*)data;
	QuiverImageViewPrivate *priv = quiver_image_view_get_instance_private(imageview);

	quiver_image_view_scroll(imageview);

	priv->scroll_timeout_id = 0;

	return FALSE;
}

static void quiver_image_view_add_scroll_timeout(QuiverImageView *imageview)
{
	QuiverImageViewPrivate *priv = quiver_image_view_get_instance_private(imageview);
	if (0 == priv->scroll_timeout_id)
	{
		priv->scroll_timeout_id = g_timeout_add(2,quiver_image_view_timeout_scroll,imageview);
	}

}


/* start callbacks */
static void
quiver_image_view_adjustment_value_changed (GtkAdjustment *adjustment,
           QuiverImageView *imageview)
{
	QuiverImageViewPrivate *priv = quiver_image_view_get_instance_private(imageview);
	if (priv->scroll_draw)
	{
		quiver_image_view_add_scroll_timeout(imageview);
	}
	else
	{
		quiver_image_view_scroll(imageview);
	}
}


/* start utility functions*/
static guint
quiver_image_view_get_width(QuiverImageView *imageview)
{
	return 1;
}

static guint
quiver_image_view_get_height(QuiverImageView *imageview)
{
	return 1;
}

static void
quiver_image_view_set_adjustment_upper (GtkAdjustment *adj,
				 gdouble        upper,
				 gboolean       always_emit_changed)
{
  gboolean changed = FALSE;
  gboolean value_changed = FALSE;
  
  gdouble min = MAX (0., upper - gtk_adjustment_get_page_size(adj));

  if (upper != gtk_adjustment_get_upper(adj))
    {
      gtk_adjustment_set_upper(adj, upper);
      changed = TRUE;
    }
      
  if (gtk_adjustment_get_value(adj) > min)
    {
      gtk_adjustment_set_value(adj, min);
      value_changed = TRUE;
    }
  
  if (changed || always_emit_changed) {
    // gtk_adjustment_changed (adj); // In GTK4, "changed" signal is emitted automatically
  }
  if (value_changed) {
    // gtk_adjustment_value_changed (adj); // In GTK4, "value-changed" signal is emitted automatically
  }
}

static void
quiver_image_view_update_size(QuiverImageView *imageview)
{
	QuiverImageViewPrivate *priv = quiver_image_view_get_instance_private(imageview);
	GtkWidget *widget = GTK_WIDGET(imageview);
	gint width,height;

	quiver_image_view_get_pixbuf_display_size(imageview,&width,&height);

	GtkAdjustment *hadjustment, *vadjustment;

	hadjustment = priv->hadjustment;
	vadjustment = priv->vadjustment;

	gtk_adjustment_set_page_size(hadjustment, gtk_widget_get_width(widget));
	gtk_adjustment_set_page_increment(hadjustment, gtk_widget_get_width(widget) * 0.9);
	gtk_adjustment_set_step_increment(hadjustment, gtk_widget_get_width(widget) * 0.1);
	gtk_adjustment_set_lower(hadjustment, 0);
	gtk_adjustment_set_upper(hadjustment, MAX (gtk_widget_get_width(widget), width));

	if (gtk_adjustment_get_value(hadjustment) > gtk_adjustment_get_upper(hadjustment) - gtk_adjustment_get_page_size(hadjustment))
		gtk_adjustment_set_value (hadjustment, MAX (0, gtk_adjustment_get_upper(hadjustment) - gtk_adjustment_get_page_size(hadjustment)));

	gtk_adjustment_set_page_size(vadjustment, gtk_widget_get_height(widget));
	gtk_adjustment_set_page_increment(vadjustment, gtk_widget_get_height(widget) * 0.9);
	gtk_adjustment_set_step_increment(vadjustment, gtk_widget_get_height(widget) * 0.1);
	gtk_adjustment_set_lower(vadjustment, 0);
	gtk_adjustment_set_upper(vadjustment, MAX (gtk_widget_get_height(widget), height));

	if (gtk_adjustment_get_value(vadjustment) > gtk_adjustment_get_upper(vadjustment) - gtk_adjustment_get_page_size(vadjustment))
		gtk_adjustment_set_value (vadjustment, MAX (0, gtk_adjustment_get_upper(vadjustment) - gtk_adjustment_get_page_size(vadjustment)));
	
}

static void quiver_image_view_start_animation(QuiverImageView *imageview)
{
	QuiverImageViewPrivate *priv = quiver_image_view_get_instance_private(imageview);

	priv->pixbuf_animation_iter = gdk_pixbuf_animation_get_iter(priv->pixbuf_animation,NULL);
	GdkPixbuf *pixbuf = gdk_pixbuf_animation_iter_get_pixbuf(priv->pixbuf_animation_iter);
	if (NULL != priv->pixbuf)
	{
		g_object_unref(priv->pixbuf);
	}
	priv->pixbuf = gdk_pixbuf_copy(pixbuf);

	quiver_image_view_transition_stop(imageview);

	quiver_image_view_create_scaled_pixbuf(imageview,GDK_INTERP_BILINEAR);
	
	quiver_image_view_invalidate_image_area(imageview,NULL);
	
	quiver_image_view_add_animation_timeout(imageview);
	
}

static void quiver_image_view_add_animation_timeout(QuiverImageView *imageview)
{
	QuiverImageViewPrivate *priv = quiver_image_view_get_instance_private(imageview);
	gint delay;
	delay = gdk_pixbuf_animation_iter_get_delay_time(priv->pixbuf_animation_iter);
	if (-1 != delay)
	{
		priv->animation_timeout_id =
			g_timeout_add(delay,quiver_image_view_timeout_animation,imageview);
	}
	else
	{
		priv->animation_timeout_id = 0;
		if (NULL != priv->pixbuf_animation_iter)
		{
			g_object_unref(priv->pixbuf_animation_iter);
			priv->pixbuf_animation_iter = NULL;
		}
	}


}

static gboolean quiver_image_view_timeout_animation(gpointer data)
{
	QuiverImageView* imageview = (QuiverImageView*)data;
	QuiverImageViewPrivate *priv = quiver_image_view_get_instance_private(imageview);
	GtkWidget *widget = GTK_WIDGET(imageview);

	
	if (gdk_pixbuf_animation_iter_advance(priv->pixbuf_animation_iter,NULL))
	{

		GdkPixbuf *pixbuf = gdk_pixbuf_animation_iter_get_pixbuf(priv->pixbuf_animation_iter);
		if (NULL != priv->pixbuf)
			g_object_unref(priv->pixbuf);

		priv->pixbuf = gdk_pixbuf_copy(pixbuf);
		if (gtk_widget_get_mapped (widget))
		{
			quiver_image_view_create_scaled_pixbuf(imageview,GDK_INTERP_BILINEAR);
			quiver_image_view_invalidate_image_area(imageview,NULL);
		}
	}
	quiver_image_view_add_animation_timeout(imageview);
	
	return FALSE;
}

static gboolean
quiver_image_view_idle_transition_create(gpointer data)
{
	gboolean rval = TRUE;
	QuiverImageView* imageview = (QuiverImageView*)data;
	QuiverImageViewPrivate *priv = quiver_image_view_get_instance_private(imageview);
	
	if (NULL == priv->transition_pixbufs_intermediate)
	{
		quiver_image_view_prepare_transition_pixbufs(imageview);
	}
	else
	{
		quiver_image_view_create_next_transition_pixbuf(imageview);
		GList* nth
			= g_list_next(priv->transition_pixbufs_intermediate);
		if (NULL != nth)
		{
			priv->transition_pixbufs_intermediate = nth;
		}
		else
		{
			priv->transition_pixbufs_intermediate
				= g_list_first(priv->transition_pixbufs_intermediate);
			quiver_image_view_add_transition_timeout(imageview);
			priv->idle_transition_create_id = 0;
			rval = FALSE;
		}
	}
	return rval;
}

static void quiver_image_view_transition_start(QuiverImageView *imageview)
{
	QuiverImageViewPrivate *priv = quiver_image_view_get_instance_private(imageview);
	priv->idle_transition_create_id =
		g_idle_add(quiver_image_view_idle_transition_create, imageview);
}

static void quiver_image_view_transition_stop(QuiverImageView *imageview)
{
	QuiverImageViewPrivate *priv = quiver_image_view_get_instance_private(imageview);
	if (0 != priv->idle_transition_create_id)
	{
		g_source_remove(priv->idle_transition_create_id);
		priv->idle_transition_create_id = 0;
	}

	GList* nth = g_list_first(priv->transition_pixbufs_intermediate);
	while (NULL != nth)
	{
		GdkPixbuf* pixbuf = (GdkPixbuf*)nth->data;
		g_object_unref(pixbuf);
		nth = g_list_next(nth);
	}
	g_list_free(priv->transition_pixbufs_intermediate);
	priv->transition_pixbufs_intermediate = NULL;

	if (NULL != priv->transition_pixbuf_old)
	{
		g_object_unref(priv->transition_pixbuf_old);
		priv->transition_pixbuf_old = NULL;

	}
	
	if (NULL != priv->transition_pixbuf_new)
	{
		g_object_unref(priv->transition_pixbuf_new);
		priv->transition_pixbuf_new = NULL;
	}


	if (0 != priv->transition_timeout_id)
	{
		g_source_remove(priv->transition_timeout_id);
		priv->transition_timeout_id = 0;
	}
}

static void quiver_image_view_add_transition_timeout(QuiverImageView *imageview)
{
	QuiverImageViewPrivate *priv = quiver_image_view_get_instance_private(imageview);
	priv->transition_timeout_id
		= g_timeout_add(35,quiver_image_view_timeout_transition,imageview);
}

static gboolean quiver_image_view_timeout_transition(gpointer data)
{
	gboolean rval = FALSE;
	QuiverImageView *imageview = (QuiverImageView*)data;
	QuiverImageViewPrivate *priv = quiver_image_view_get_instance_private(imageview);
	GtkWidget *widget;
	widget = GTK_WIDGET(imageview);

	gint width, height;
	width = 0;
	height = 0;

	if (NULL != priv->transition_pixbufs_intermediate)
	{
		
		GList* first 
			= g_list_first(priv->transition_pixbufs_intermediate);
		gint pos = 
			g_list_position(first, priv->transition_pixbufs_intermediate);
		gint size = g_list_length(first);

		GdkPixbuf* pixbuf 
			= (GdkPixbuf*)priv->transition_pixbufs_intermediate->data;

		if ( pos == size -1 )
		{
			if (NULL != priv->pixbuf_scaled)
			{
				g_object_unref(priv->pixbuf_scaled);
				priv->pixbuf_scaled = NULL;
			}
			quiver_image_view_transition_stop(imageview);

			quiver_image_view_create_scaled_pixbuf(imageview,GDK_INTERP_BILINEAR);


			rval = FALSE;
		}
		else
		{
			if (NULL != priv->pixbuf_scaled)
			{
				g_object_unref(priv->pixbuf_scaled);
				priv->pixbuf_scaled = NULL;
			}
			g_object_ref(pixbuf);
			priv->pixbuf_scaled = pixbuf;

			if (NULL != priv->pixbuf_scaled)
			{
				width = gdk_pixbuf_get_width(priv->pixbuf_scaled);
				height = gdk_pixbuf_get_height(priv->pixbuf_scaled);
			}

			GList* next = 
				g_list_next(priv->transition_pixbufs_intermediate);
			if (NULL != next)
			{
				priv->transition_pixbufs_intermediate
					= next;
			}
			quiver_image_view_add_transition_timeout(imageview);
		}
		
		if (gtk_widget_get_mapped (widget))
		{
            gtk_widget_queue_draw(widget);
		}
	}

	return rval;	
}

static void quiver_image_view_add_magnification_timeout(QuiverImageView *imageview)
{
	QuiverImageViewPrivate *priv = quiver_image_view_get_instance_private(imageview);
	if (0 == priv->magnification_timeout_id)
	{
		priv->magnification_timeout_id = g_timeout_add(30,quiver_image_view_timeout_magnification,imageview);
	}
}
static gboolean quiver_image_view_timeout_magnification(gpointer data)
{
	gboolean rval = TRUE;
	QuiverImageView *imageview = (QuiverImageView*)data;
	QuiverImageViewPrivate *priv = quiver_image_view_get_instance_private(imageview);
	
	gdouble mag_diff = priv->magnification_final - priv->magnification;
	gdouble percent_diff = priv->magnification_final / priv->magnification;

	if (1 < percent_diff || -1 > percent_diff)
	{
		percent_diff = 1/percent_diff;
	}
	if (0 > percent_diff)
	{
		percent_diff *= -1;
	}
	
	percent_diff = 100 - percent_diff*100;
	
	if (percent_diff < 5.)
	{
		priv->magnification_timeout_id = 0;
		quiver_image_view_set_magnification_full(imageview,priv->magnification_final);
		rval = FALSE;
	}
	else
	{
		quiver_image_view_set_magnification_full(imageview,priv->magnification + mag_diff/2);
		if (0 != priv->magnification_timeout_id)
		{
			priv->magnification_timeout_id = 0;
			quiver_image_view_add_magnification_timeout(imageview);
		}
		rval = FALSE;
	}
	
	return rval;
}

static void quiver_image_view_get_pixbuf_display_size(QuiverImageView *imageview, gint *width, gint *height)
{
	QuiverImageViewPrivate *priv = quiver_image_view_get_instance_private(imageview);
	*width = priv->pixbuf_width;
	*height = priv->pixbuf_height;
	quiver_image_view_get_pixbuf_display_size_alt(imageview,priv->view_mode,*width, *height,width,height);
}

static void quiver_image_view_get_pixbuf_display_size_alt(QuiverImageView *imageview,QuiverImageViewMode mode, gint in_width, gint in_height, gint *out_width, gint *out_height)
{
	QuiverImageViewPrivate *priv = quiver_image_view_get_instance_private(imageview);
	GtkWidget *widget;
	widget = GTK_WIDGET(imageview);
	
	*out_width = in_width;
	*out_height = in_height;

	switch (mode)
	{
		case QUIVER_IMAGE_VIEW_MODE_ZOOM:
			{
				gdouble magnification = priv->magnification;
				*out_width  = (gint)(*out_width * magnification);
				*out_height  = (gint)(*out_height * magnification);
			}
			break;
		case QUIVER_IMAGE_VIEW_MODE_ACTUAL_SIZE:
			break;

		case QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW:
			quiver_rect_get_bound_size(gtk_widget_get_width(widget),gtk_widget_get_height(widget),(guint*)out_width,(guint*)out_height,FALSE);
			break;
		case QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW_STRETCH:
			quiver_rect_get_bound_size(gtk_widget_get_width(widget),gtk_widget_get_height(widget),(guint*)out_width,(guint*)out_height,TRUE);
			break;
		case QUIVER_IMAGE_VIEW_MODE_FILL_SCREEN:
			{
				gint w1,h1;
				w1 = in_width;
				h1 = in_height;
				
				quiver_rect_get_bound_size(gtk_widget_get_width(widget),gtk_widget_get_height(widget),(guint*)&w1,(guint*)&h1,FALSE);
				if (w1 < gtk_widget_get_width(widget) && h1 < gtk_widget_get_height(widget))
				{
					*out_width = w1;
					*out_height = h1;
				}
				else if (w1 < gtk_widget_get_width(widget))
				{
					quiver_rect_get_bound_size(gtk_widget_get_width(widget),in_height,(guint*)out_width,(guint*)out_height,FALSE);
				}
				else if (h1 < gtk_widget_get_height(widget))
				{
					quiver_rect_get_bound_size(in_height,gtk_widget_get_height(widget),(guint*)out_width,(guint*)out_height,FALSE);
				}
				else
				{
					*out_width = w1;
					*out_height = h1;
				}
			}
		default:
			break;
	}
}

static void quiver_image_view_set_default_adjustment_values(QuiverImageView *imageview)
{
	QuiverImageViewPrivate *priv = quiver_image_view_get_instance_private(imageview);
	gdouble hval, vval;
	hval = (gtk_adjustment_get_upper(priv->hadjustment) - gtk_adjustment_get_lower(priv->hadjustment))/2 - gtk_adjustment_get_page_size(priv->hadjustment)/2;
	vval = (gtk_adjustment_get_upper(priv->vadjustment) - gtk_adjustment_get_lower(priv->vadjustment))/2 - gtk_adjustment_get_page_size(priv->vadjustment)/2;
	gtk_adjustment_set_value(priv->hadjustment,hval);
	gtk_adjustment_set_value(priv->vadjustment,vval);
}

void quiver_image_view_get_pixbuf_display_size_for_mode(QuiverImageView *imageview, QuiverImageViewMode mode, gint *width, gint *height)
{
	QuiverImageViewPrivate *priv = quiver_image_view_get_instance_private(imageview);
	*width = priv->pixbuf_width;
	*height = priv->pixbuf_height;
	quiver_image_view_get_pixbuf_display_size_alt(imageview,priv->view_mode,*width, *height,width,height);
}

void quiver_image_view_get_pixbuf_display_size_for_mode_alt(QuiverImageView *imageview,QuiverImageViewMode mode, gint in_width, gint in_height, gint *out_width, gint *out_height)
{
	QuiverImageViewPrivate *priv = quiver_image_view_get_instance_private(imageview);
	GtkWidget *widget;
	widget = GTK_WIDGET(imageview);

	*out_width = in_width;
	*out_height = in_height;

	switch (mode)
	{
		case QUIVER_IMAGE_VIEW_MODE_ZOOM:
			{
				gdouble magnification = priv->magnification;
				*out_width  = (gint)(*out_width * magnification);
				*out_height  = (gint)(*out_height * magnification);
			}
			break;
		case QUIVER_IMAGE_VIEW_MODE_ACTUAL_SIZE:
			break;

		case QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW:
			quiver_rect_get_bound_size(gtk_widget_get_width(widget),gtk_widget_get_height(widget),(guint*)out_width,(guint*)out_height,FALSE);
			break;
		case QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW_STRETCH:
			quiver_rect_get_bound_size(gtk_widget_get_width(widget),gtk_widget_get_height(widget),(guint*)out_width,(guint*)out_height,TRUE);
			break;
		case QUIVER_IMAGE_VIEW_MODE_FILL_SCREEN:
			{
				gint w1,h1;
				w1 = in_width;
				h1 = in_height;

				quiver_rect_get_bound_size(gtk_widget_get_width(widget),gtk_widget_get_height(widget),(guint*)&w1,(guint*)&h1,FALSE);
				if (w1 < gtk_widget_get_width(widget) && h1 < gtk_widget_get_height(widget))
				{
					*out_width = w1;
					*out_height = h1;
				}
				else if (w1 < gtk_widget_get_width(widget))
				{
					quiver_rect_get_bound_size(gtk_widget_get_width(widget),in_height,(guint*)out_width,(guint*)out_height,FALSE);
				}
				else if (h1 < gtk_widget_get_height(widget))
				{
					quiver_rect_get_bound_size(in_height,gtk_widget_get_height(widget),(guint*)out_width,(guint*)out_height,FALSE);
				}
				else
				{
					*out_width = w1;
					*out_height = h1;
				}
			}
		default:
			break;
	}
}


static void quiver_image_view_invalidate_old_image_area(QuiverImageView *imageview, gint new_width, gint new_height)
{
	QuiverImageViewPrivate *priv = quiver_image_view_get_instance_private(imageview);
	GtkWidget *widget;
	cairo_rectangle_int_t old_rect;
	cairo_rectangle_int_t new_rect;
	gint old_width,old_height;

	cairo_region_t *old_region,*new_region;

	widget = GTK_WIDGET(imageview);

	if ( !gtk_widget_get_mapped (widget) )
	{
		return;
	}

	quiver_image_view_get_pixbuf_display_size(imageview,&old_width,&old_height);
	
	QuiverImageViewMode mode = priv->view_mode;
	
	if (QUIVER_IMAGE_VIEW_MODE_ZOOM == priv->view_mode)
	{
		mode = priv->view_mode_last;
	}
	quiver_image_view_get_pixbuf_display_size_for_mode_alt(imageview, mode, new_width, new_height, &new_width, &new_height);

	old_rect.x = MAX(0,(gint)((gtk_widget_get_width(widget) - old_width)/2.));
	old_rect.y = MAX(0,(gint)((gtk_widget_get_height(widget) - old_height)/2.));
	old_rect.width = MIN(old_width,gtk_widget_get_width(widget));
	old_rect.height = MIN(old_height,gtk_widget_get_height(widget));

	new_rect.x = MAX(0,(gint)((gtk_widget_get_width(widget) - new_width)/2.));
	new_rect.y = MAX(0,(gint)((gtk_widget_get_height(widget) - new_height)/2.));
	new_rect.width = MIN(new_width,gtk_widget_get_width(widget));
	new_rect.height = MIN(new_height,gtk_widget_get_height(widget));

	old_region = cairo_region_create_rectangle(&old_rect);
	new_region = cairo_region_create_rectangle(&new_rect);
	cairo_region_subtract(old_region,new_region);
    gtk_widget_queue_draw(widget);
	cairo_region_destroy(old_region);
	cairo_region_destroy(new_region);

}


static void quiver_image_view_invalidate_image_area(QuiverImageView *imageview,cairo_rectangle_int_t *sub_rect)
{
	GtkWidget *widget;
	cairo_rectangle_int_t invalid_rect;
	cairo_rectangle_int_t pixbuf_rect;
	cairo_rectangle_int_t sub_rect_tmp;
	gint width,height;

	widget = GTK_WIDGET(imageview);

	if ( !gtk_widget_get_mapped (widget) )
	{
		return;
	}
	
	quiver_image_view_get_pixbuf_display_size(imageview,&width,&height);

	pixbuf_rect.x = MAX(0,(gint)((gtk_widget_get_width(widget) - width)/2.));
	pixbuf_rect.y = MAX(0,(gint)((gtk_widget_get_height(widget) - height)/2.));
	pixbuf_rect.width = MIN(width,gtk_widget_get_width(widget));
	pixbuf_rect.height = MIN(height,gtk_widget_get_height(widget));

	if (NULL != sub_rect)
	{
		sub_rect_tmp = *sub_rect;
		sub_rect_tmp.x += pixbuf_rect.x;
		sub_rect_tmp.y += pixbuf_rect.y;
		gdk_rectangle_intersect(&sub_rect_tmp,&pixbuf_rect,&invalid_rect);
	}
	else
	{
		invalid_rect = pixbuf_rect;
	}
    gtk_widget_queue_draw(widget);
}


/* end private functions */

/* start public functions */
GtkWidget *
quiver_image_view_new()
{
	return g_object_new(QUIVER_TYPE_IMAGE_VIEW,NULL);
}

void quiver_image_view_set_smooth_scroll(QuiverImageView *imageview,gboolean smooth_scroll)
{
	QuiverImageViewPrivate *priv = quiver_image_view_get_instance_private(imageview);
	priv->smooth_scroll = smooth_scroll;
}

GdkPixbuf* quiver_image_view_get_pixbuf(QuiverImageView *imageview)
{
	QuiverImageViewPrivate *priv = quiver_image_view_get_instance_private(imageview);
	return priv->pixbuf;
}

void quiver_image_view_set_pixbuf(QuiverImageView *imageview, GdkPixbuf *pixbuf)
{
	gint width , height;
	
	width  = 0;
	height = 0;
	if (NULL != pixbuf)
	{
		width  = gdk_pixbuf_get_width(pixbuf);
		height = gdk_pixbuf_get_height(pixbuf);
	}
	else
	{
		width  = 0;
		height = 0;
	}

	quiver_image_view_set_pixbuf_at_size(imageview, pixbuf,width,height);

}

void quiver_image_view_set_pixbuf_at_size(QuiverImageView *imageview, GdkPixbuf *pixbuf,int width , int height)
{
	quiver_image_view_set_pixbuf_at_size_ex(imageview, pixbuf, width , height, TRUE);
}

void quiver_image_view_set_pixbuf_at_size_ex(QuiverImageView *imageview, GdkPixbuf *pixbuf,int width , int height, gboolean reset_view_mode)
{
	QuiverImageViewPrivate *priv = quiver_image_view_get_instance_private(imageview);
	GtkWidget* widget = GTK_WIDGET(imageview);
	gdouble old_mag;
	old_mag = quiver_image_view_get_magnification(imageview);
	
	GdkPixbuf* old_pixbuf = NULL;
	
	if (priv->transitions_enabled && reset_view_mode)
	{
		if (NULL == priv->pixbuf_scaled)
		{
            quiver_image_view_create_scaled_pixbuf(imageview,GDK_INTERP_NEAREST);
		}
		
		if (NULL == priv->pixbuf_scaled)
		{
			if (NULL != priv->pixbuf)
			{
				old_pixbuf = priv->pixbuf;
				g_object_ref(old_pixbuf);
			}
		}
		else
		{
			old_pixbuf = priv->pixbuf_scaled;
			g_object_ref(old_pixbuf);
		}
	}
	
	if (reset_view_mode || 0 == priv->transition_timeout_id)
	{
		quiver_image_view_prepare_for_new_pixbuf(imageview,width,height);
	}
	else
	{
		if (NULL != priv->pixbuf)
		{
			g_object_unref(priv->pixbuf);
			priv->pixbuf = NULL;
		}
	}
	
	if (NULL != old_pixbuf)
	{
		priv->transition_pixbuf_old = old_pixbuf;
		priv->pixbuf_scaled = old_pixbuf;
		g_object_ref(old_pixbuf);
	}

	if (NULL != pixbuf)
	{
		g_object_ref(pixbuf);

		priv->pixbuf = pixbuf;
	}
	priv->pixbuf_width = width;
	priv->pixbuf_height = height;
	
	if (reset_view_mode)
	{
		quiver_image_view_reset_view_mode(imageview,FALSE);
		
		priv->scroll_draw = FALSE;
		quiver_image_view_update_size(imageview);

		quiver_image_view_set_default_adjustment_values(imageview);
		
		priv->scroll_draw = TRUE;
		priv->magnification = 0;
		old_mag = 0;
	}

	priv->magnification = quiver_image_view_get_magnification(imageview);
	if (old_mag != priv->magnification)
	{
		g_signal_emit(imageview,imageview_signals[SIGNAL_MAGNIFICATION_CHANGED],0);
	}

	if (1 == gtk_widget_get_width(widget) || 1 == gtk_widget_get_height(widget))
		return;

	if (priv->transitions_enabled && reset_view_mode)
	{
		quiver_image_view_transition_start(imageview);
	}
	else if (priv->transitions_enabled && 0 != priv->transition_timeout_id)
	{
	}
	else
	{
		quiver_image_view_create_scaled_pixbuf(imageview,GDK_INTERP_NEAREST);
	
		quiver_image_view_invalidate_image_area(imageview,NULL);
	
		quiver_image_view_add_scale_hq_timeout(imageview);
	}	
	
}

void quiver_image_view_reset_view_mode(QuiverImageView *imageview,gboolean invalidate)
{
	QuiverImageViewPrivate *priv = quiver_image_view_get_instance_private(imageview);
	if (QUIVER_IMAGE_VIEW_MODE_ZOOM == priv->view_mode)
	{
		quiver_image_view_set_view_mode_full(imageview,priv->view_mode_last,invalidate);
	}
}

QuiverImageViewMode quiver_image_view_get_view_mode(QuiverImageView *imageview)
{
	QuiverImageViewPrivate *priv = quiver_image_view_get_instance_private(imageview);
	return priv->view_mode;
}

QuiverImageViewMode quiver_image_view_get_view_mode_unmagnified(QuiverImageView *imageview)
{
	QuiverImageViewPrivate *priv = quiver_image_view_get_instance_private(imageview);
	if (QUIVER_IMAGE_VIEW_MODE_ZOOM == priv->view_mode)
	{
		return priv->view_mode_last;
	}

	return priv->view_mode;
}

void quiver_image_view_set_view_mode(QuiverImageView *imageview,QuiverImageViewMode mode)
{
	quiver_image_view_set_view_mode_full(imageview,mode,TRUE);
}


static void quiver_image_view_set_view_mode_full(QuiverImageView *imageview,QuiverImageViewMode mode,gboolean invalidate)
{
	QuiverImageViewPrivate *priv = quiver_image_view_get_instance_private(imageview);
	
	GtkWidget *widget;
	QuiverImageViewMode old_mode;
	
	gdouble old_mag = priv->magnification;
	
	if (QUIVER_IMAGE_VIEW_MODE_ZOOM != priv->view_mode
		&& QUIVER_IMAGE_VIEW_MODE_ZOOM == mode)
	{
		priv->magnification = quiver_image_view_get_magnification(imageview);
		priv->view_mode_last = priv->view_mode;
	}
	
	if (QUIVER_IMAGE_VIEW_MODE_ZOOM == priv->view_mode
		&& QUIVER_IMAGE_VIEW_MODE_ZOOM != mode)
	{	
		if (0 != priv->magnification_timeout_id)
		{
			g_source_remove(priv->magnification_timeout_id);
			priv->magnification_timeout_id = 0;
		}
	}

	widget = GTK_WIDGET(imageview);

	gint ow,oh,nw,nh;
	
	quiver_image_view_get_pixbuf_display_size(imageview,&ow, &oh);

	priv->magnification = quiver_image_view_get_magnification(imageview);
	old_mode = priv->view_mode;
	priv->view_mode = mode;
	if (old_mode != mode)
	{
		g_signal_emit(imageview,imageview_signals[SIGNAL_VIEW_MODE_CHANGED],0);
	}

	priv->magnification = quiver_image_view_get_magnification(imageview);

	if (old_mag != priv->magnification)
	{
		g_signal_emit(imageview,imageview_signals[SIGNAL_MAGNIFICATION_CHANGED],0);
	}


	if (!invalidate)
	{
		return;
	}

	quiver_image_view_get_pixbuf_display_size(imageview,&nw, &nh);

	if (ow != nw || oh != nh)
	{

		priv->scroll_draw   = FALSE;
		quiver_image_view_update_size(imageview);
		quiver_image_view_set_default_adjustment_values(imageview);
		priv->scroll_draw   = TRUE;

		quiver_image_view_transition_stop(imageview);

		quiver_image_view_create_scaled_pixbuf(imageview,GDK_INTERP_BILINEAR);
		if ( gtk_widget_get_mapped (widget) )
		{
            gtk_widget_queue_draw(widget);
		}
	}
}

void quiver_image_view_set_enable_transitions(QuiverImageView *imageview,gboolean enable)
{
	QuiverImageViewPrivate *priv = quiver_image_view_get_instance_private(imageview);
	priv->transitions_enabled = enable;
}

gboolean quiver_image_view_is_in_transition(QuiverImageView *imageview)
{
	QuiverImageViewPrivate *priv = quiver_image_view_get_instance_private(imageview);
	return !(0 == priv->transition_timeout_id && 0 == priv->idle_transition_create_id);
}

gdouble quiver_image_view_get_magnification(QuiverImageView *imageview)
{
	QuiverImageViewPrivate *priv = quiver_image_view_get_instance_private(imageview);
	gdouble magnification;
	gint display_width,display_height;


	switch (priv->view_mode)
	{
		case QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW:
		case QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW_STRETCH:
		case QUIVER_IMAGE_VIEW_MODE_FILL_SCREEN:
			magnification = 1.;
			if (NULL != priv->pixbuf)
			{
				quiver_image_view_get_pixbuf_display_size(imageview,&display_width,&display_height);
				magnification = display_width/(gdouble)priv->pixbuf_width;
			}
			break;
		case QUIVER_IMAGE_VIEW_MODE_ZOOM:
			if (0 != priv->magnification_timeout_id)
			{
				magnification = priv->magnification_final;
			}
			else
			{
				magnification = priv->magnification;
			}
			break;
		case QUIVER_IMAGE_VIEW_MODE_ACTUAL_SIZE:
		default:
			magnification = 1.;
	}
	return magnification;
}

gboolean quiver_image_view_can_magnify(QuiverImageView *imageview, gboolean in)
{
	QuiverImageViewPrivate *priv = quiver_image_view_get_instance_private(imageview);
	gdouble mag = quiver_image_view_get_magnification(imageview);
	gboolean can_magnify = FALSE;
	
	if (in)
	{
		if (QUIVER_IMAGE_VIEW_MAG_MAX > mag)
		{
			can_magnify = TRUE;
		}
	}
	else
	{
		gdouble w = priv->pixbuf_width * mag;
		gdouble h = priv->pixbuf_height * mag;

		if ( (w > priv->pixbuf_width && h > priv->pixbuf_height)
			 || (QUIVER_IMAGE_VIEW_MIN_IMAGE_SIZE < w &&
			QUIVER_IMAGE_VIEW_MIN_IMAGE_SIZE < h) )
		{
			can_magnify = TRUE;
		}
	}
		
	return can_magnify;
}

void quiver_image_view_set_magnification(QuiverImageView *imageview,gdouble new_mag)
{
	QuiverImageViewPrivate *priv = quiver_image_view_get_instance_private(imageview);
	// restrict the zoom amount
	if (QUIVER_IMAGE_VIEW_MAG_MAX < new_mag)
	{
		new_mag = QUIVER_IMAGE_VIEW_MAG_MAX;
	}
	
	if (1 > new_mag)
	{
		gdouble new_w = priv->pixbuf_width * new_mag;
		gdouble new_h = priv->pixbuf_height * new_mag;
		if (QUIVER_IMAGE_VIEW_MIN_IMAGE_SIZE > priv->pixbuf_width &&
			QUIVER_IMAGE_VIEW_MIN_IMAGE_SIZE > priv->pixbuf_height)
		{
			new_mag = 1.;
		}
		else if (QUIVER_IMAGE_VIEW_MIN_IMAGE_SIZE > new_w &&
			QUIVER_IMAGE_VIEW_MIN_IMAGE_SIZE > new_h)
		{
			gdouble mag_w = (gdouble)QUIVER_IMAGE_VIEW_MIN_IMAGE_SIZE / priv->pixbuf_width;
			gdouble mag_h = (gdouble)QUIVER_IMAGE_VIEW_MIN_IMAGE_SIZE / priv->pixbuf_height;
			new_mag = mag_w;
			if (mag_h< mag_w)
			{
				new_mag = mag_h;
			} 
		}
	}
	
	if (QUIVER_IMAGE_VIEW_MAGNIFICATION_MODE_SMOOTH == priv->magnification_mode)
	{
		priv->magnification_final = new_mag;
		quiver_image_view_add_magnification_timeout(imageview);
	}
	else
	{
		quiver_image_view_set_magnification_full(imageview,new_mag);
	}
}
static void quiver_image_view_set_magnification_full(QuiverImageView *imageview,gdouble new_mag)
{
	QuiverImageViewPrivate *priv = quiver_image_view_get_instance_private(imageview);
	GtkWidget *widget;
	gint old_width,old_height, new_width,new_height;
	gint x,y;
	gdouble old_hadjust,old_vadjust,new_hadjust,new_vadjust;
	gdouble old_mag;

	quiver_image_view_get_pixbuf_display_size(imageview,&old_width,&old_height);

	old_mag = priv->magnification;

	if (old_mag < new_mag)
	{
		quiver_image_view_send_reload_event(imageview);
	}
	
	
	priv->magnification = new_mag;

	if (0 == priv->magnification_timeout_id
	    && old_mag != new_mag)
	{
		g_signal_emit(imageview,imageview_signals[SIGNAL_MAGNIFICATION_CHANGED],0);
	}

	widget = GTK_WIDGET(imageview);

    GdkSurface *surface = gtk_native_get_surface(gtk_widget_get_native(widget));
    GdkDevice *device = NULL;
    GdkDisplay *display = gtk_widget_get_display(widget);
    if (display) {
        GdkSeat *seat = gdk_display_get_default_seat(display);
        if (seat) {
            device = gdk_seat_get_pointer(seat);
        }
    }
    if (surface && device) gdk_surface_get_device_position(surface, device, (double*)&x, (double*)&y, NULL);

	old_hadjust = gtk_adjustment_get_value(priv->hadjustment);
	old_vadjust = gtk_adjustment_get_value(priv->vadjustment);


	priv->scroll_draw   = FALSE;

	quiver_image_view_update_size(imageview);

	gint old_hpage_size = gtk_adjustment_get_page_size(priv->hadjustment);
	gint old_vpage_size = gtk_adjustment_get_page_size(priv->vadjustment);

	if (old_width < gtk_widget_get_width(widget))
	{
		old_hpage_size = old_width;
		x = (x * old_width)/gtk_widget_get_width(widget);
	}
	if (old_height < gtk_widget_get_height(widget))
	{
		y = (y * old_height)/gtk_widget_get_height(widget);
		old_vpage_size = old_height;
	}

	quiver_image_view_get_pixbuf_display_size(imageview,&new_width,&new_height);

	if (new_width > gtk_widget_get_width(widget))
	{
		if (0 < x && x <= gtk_widget_get_width(widget) && 0 < y && y <= gtk_widget_get_height(widget))
		{
			new_hadjust = (old_hadjust + x) * (new_mag/old_mag) - x;
		}
		else
		{
			new_hadjust = (old_hadjust + old_hpage_size/2.) * (new_mag/old_mag) - gtk_adjustment_get_page_size(priv->hadjustment)/2.;
		}
		if (new_hadjust > gtk_adjustment_get_upper(priv->hadjustment) - gtk_adjustment_get_page_size(priv->hadjustment))
			new_hadjust = MAX (0, gtk_adjustment_get_upper(priv->hadjustment) - gtk_adjustment_get_page_size(priv->hadjustment));
		if (0 > new_hadjust)
			new_hadjust = 0;

		gtk_adjustment_set_value(priv->hadjustment,new_hadjust);
	}
	else
	{
		gtk_adjustment_set_value(priv->hadjustment,0);
	}

	if (new_height > gtk_widget_get_height(widget))
	{
		if (0 < x && x <= gtk_widget_get_width(widget) && 0 < y && y <= gtk_widget_get_height(widget))
		{
			new_vadjust = (old_vadjust + y) * (new_mag/old_mag) -y;
		}
		else
		{
			new_vadjust = (old_vadjust + old_vpage_size/2.) * (new_mag/old_mag) - gtk_adjustment_get_page_size(priv->vadjustment)/2.;
		}
		if (new_vadjust > gtk_adjustment_get_upper(priv->vadjustment) - gtk_adjustment_get_page_size(priv->vadjustment))
			new_vadjust = MAX (0, gtk_adjustment_get_upper(priv->vadjustment) - gtk_adjustment_get_page_size(priv->vadjustment));
		if (0 > new_vadjust)
			new_vadjust = 0;

		gtk_adjustment_set_value(priv->vadjustment,new_vadjust);
	}
	else
	{
		gtk_adjustment_set_value(priv->vadjustment,0);
	}
	quiver_image_view_transition_stop(imageview);

	quiver_image_view_create_scaled_pixbuf(imageview,GDK_INTERP_NEAREST);
	quiver_image_view_add_scale_hq_timeout(imageview);
	priv->scroll_draw   = TRUE;

	if (gtk_widget_get_mapped(widget))
	{
        gtk_widget_queue_draw(widget);
	}
}

void quiver_image_view_set_magnification_mode(QuiverImageView *imageview,QuiverImageViewMagnificationMode mode)
{
	QuiverImageViewPrivate *priv = quiver_image_view_get_instance_private(imageview);
	priv->magnification_mode = mode;
}


void quiver_image_view_rotate(QuiverImageView *imageview, gboolean clockwise)
{
	QuiverImageViewPrivate *priv = quiver_image_view_get_instance_private(imageview);
	GdkPixbuf * pixbuf_rotated = NULL;
	
	if (NULL == priv->pixbuf)
		return;

	if (clockwise)
	{
		pixbuf_rotated = gdk_pixbuf_rotate_simple(priv->pixbuf,GDK_PIXBUF_ROTATE_CLOCKWISE);
	}
	else
	{
		pixbuf_rotated = gdk_pixbuf_rotate_simple(priv->pixbuf,GDK_PIXBUF_ROTATE_COUNTERCLOCKWISE);
	}
	if (NULL != pixbuf_rotated)
	{
		quiver_image_view_set_pixbuf_at_size(imageview,pixbuf_rotated,priv->pixbuf_height,priv->pixbuf_width);
		g_object_unref(pixbuf_rotated);
	}
}

void quiver_image_view_flip(QuiverImageView *imageview, gboolean horizontal)
{
	QuiverImageViewPrivate *priv = quiver_image_view_get_instance_private(imageview);
	GdkPixbuf * pixbuf_flipped = NULL;

	if (NULL == priv->pixbuf)
		return;

	if (horizontal)
	{
		pixbuf_flipped = gdk_pixbuf_flip(priv->pixbuf,TRUE);
	}
	else
	{
		pixbuf_flipped = gdk_pixbuf_flip(priv->pixbuf,FALSE);
	}
	
	if (NULL != pixbuf_flipped)
	{
		quiver_image_view_set_pixbuf_at_size(imageview,pixbuf_flipped,priv->pixbuf_width,priv->pixbuf_height);
		g_object_unref(pixbuf_flipped);
	}
}

void quiver_image_view_connect_pixbuf_loader_signals(QuiverImageView *imageview,GdkPixbufLoader *loader)
{
	g_signal_connect(G_OBJECT(loader),"size-prepared",G_CALLBACK(pixbuf_loader_size_prepared),imageview);
	g_signal_connect(G_OBJECT(loader),"area-prepared",G_CALLBACK(pixbuf_loader_area_prepared),imageview);
	g_signal_connect(G_OBJECT(loader),"area-updated",G_CALLBACK(pixbuf_loader_area_updated),imageview);
	g_signal_connect(G_OBJECT(loader),"closed",G_CALLBACK(pixbuf_loader_closed),imageview);
}

void quiver_image_view_connect_pixbuf_size_prepared_signal(QuiverImageView *imageview,GdkPixbufLoader *loader)
{
	g_signal_connect(G_OBJECT(loader),"size-prepared",G_CALLBACK(pixbuf_loader_size_prepared),imageview);
}

GtkAdjustment * quiver_image_view_get_hadjustment(QuiverImageView *imageview)
{
	QuiverImageViewPrivate *priv = quiver_image_view_get_instance_private(imageview);
	return priv->hadjustment;
}
GtkAdjustment * quiver_image_view_get_vadjustment(QuiverImageView *imageview)
{
	QuiverImageViewPrivate *priv = quiver_image_view_get_instance_private(imageview);
	return priv->vadjustment;
}

void quiver_image_view_activate(QuiverImageView *imageview)
{
	g_signal_emit(imageview,imageview_signals[SIGNAL_ACTIVATED],0);
}

/* end public functions */
static void quiver_image_view_prepare_for_new_pixbuf(QuiverImageView *imageview, gint new_width, gint new_height)
{
	QuiverImageViewPrivate *priv = quiver_image_view_get_instance_private(imageview);
	quiver_image_view_transition_stop(imageview);
	
	if (0 != priv->magnification_timeout_id)
	{
		g_source_remove(priv->magnification_timeout_id);
		priv->magnification_timeout_id = 0;
	}
	
	if (0 != priv->timeout_scale_hq_id)
	{
		g_source_remove(priv->timeout_scale_hq_id);
		priv->timeout_scale_hq_id = 0;
	}
	
	if (0 != priv->scroll_timeout_id)
	{
		g_source_remove(priv->scroll_timeout_id);
		priv->scroll_timeout_id = 0;
	}
	
	if (0 != priv->transition_timeout_id)
	{
		g_source_remove(priv->transition_timeout_id);
		priv->transition_timeout_id = 0;
	}

	if (0 != priv->animation_timeout_id)
	{	
		g_source_remove(priv->animation_timeout_id);
		priv->animation_timeout_id = 0;
	}
	
	if (NULL != priv->pixbuf_animation_iter)
	{
		g_object_unref(priv->pixbuf_animation_iter);
		priv->pixbuf_animation_iter = NULL;
	}

	if (NULL != priv->pixbuf_animation)
	{
		g_object_unref(priv->pixbuf_animation);
		priv->pixbuf_animation = NULL;
	}

	if (NULL != priv->pixbuf_scaled)
	{
		g_object_unref(priv->pixbuf_scaled);
		priv->pixbuf_scaled = NULL;
	}

	if (NULL != priv->pixbuf)
	{
		if (!priv->transitions_enabled)
		{
			quiver_image_view_invalidate_old_image_area(imageview,new_width,new_height);
		}
		g_object_unref(priv->pixbuf);
		priv->pixbuf = NULL;
	}
	
	priv->reload_event_sent = FALSE;
	
}

static void pixbuf_loader_size_prepared(GdkPixbufLoader *loader,gint width, gint height,gpointer userdata)
{
	QuiverImageView *imageview = (QuiverImageView*)userdata;
	QuiverImageViewPrivate *priv = quiver_image_view_get_instance_private(imageview);
	GtkWidget *widget;

	widget = GTK_WIDGET(imageview);

	priv->pixbuf_width_next = width;
	priv->pixbuf_height_next = height;
	if (!gtk_widget_get_mapped(widget))
	{
		return;
	}
}
static void pixbuf_loader_area_prepared(GdkPixbufLoader *loader,gpointer userdata)
{
	QuiverImageView *imageview = (QuiverImageView*)userdata;
	QuiverImageViewPrivate *priv = quiver_image_view_get_instance_private(imageview);
	GdkPixbuf *pixbuf;

	GdkPixbufAnimation* pixbuf_animation;

	pixbuf_animation = gdk_pixbuf_loader_get_animation (loader);
	g_object_ref(pixbuf_animation);

	pixbuf = gdk_pixbuf_animation_get_static_image (pixbuf_animation);
	g_object_ref(pixbuf);

	quiver_image_view_prepare_for_new_pixbuf(imageview,	
		priv->pixbuf_width_next, priv->pixbuf_height_next);

	priv->pixbuf_width = priv->pixbuf_width_next;
	priv->pixbuf_height = priv->pixbuf_height_next;
	
	priv->pixbuf_animation = pixbuf_animation;
	
	priv->pixbuf = pixbuf;
	
	quiver_image_view_transition_stop(imageview);

	quiver_image_view_create_scaled_pixbuf(imageview,GDK_INTERP_NEAREST);

	priv->scroll_draw = FALSE;

	quiver_image_view_update_size(imageview);
	
	quiver_image_view_set_default_adjustment_values(imageview);
	
	priv->scroll_draw = TRUE;
	
	priv->area_updated = TRUE;

}
static void pixbuf_loader_area_updated (GdkPixbufLoader *loader,gint x, gint y, gint width,gint height,gpointer userdata)
{
	QuiverImageView *imageview = (QuiverImageView*)userdata;
	QuiverImageViewPrivate *priv = quiver_image_view_get_instance_private(imageview);
	cairo_rectangle_int_t rect;
	GdkPixbufAnimation* pixbuf_animation;
	gint dw,dh,aw,ah;

	pixbuf_animation = priv->pixbuf_animation;

	aw = gdk_pixbuf_get_width(priv->pixbuf);
	ah = gdk_pixbuf_get_height(priv->pixbuf);
	
	quiver_image_view_get_pixbuf_display_size(imageview, &dw, &dh);

	if (dw == aw && dh == ah)
	{
		rect.x = x;
		rect.y = y;
		rect.width = width;
		rect.height = height;


		if (0 == priv->animation_timeout_id)
		{
			if (!gdk_pixbuf_animation_is_static_image(pixbuf_animation))
			{
				quiver_image_view_start_animation(imageview);
			}
		}

		quiver_image_view_invalidate_image_area(imageview,&rect);
		priv->area_updated = TRUE;
	}
}
static void pixbuf_loader_closed(GdkPixbufLoader *loader,gpointer userdata)
{	

	QuiverImageView *imageview = (QuiverImageView*)userdata;
	QuiverImageViewPrivate *priv = quiver_image_view_get_instance_private(imageview);
	GdkPixbufAnimation* pixbuf_animation;

	pixbuf_animation = priv->pixbuf_animation;
	
	if (NULL != pixbuf_animation)
	{
		if (!priv->area_updated)
		{
	
			if (gdk_pixbuf_animation_is_static_image(pixbuf_animation))
			{
				quiver_image_view_transition_stop(imageview);

				quiver_image_view_create_scaled_pixbuf(imageview,GDK_INTERP_NEAREST);
				quiver_image_view_invalidate_image_area(imageview,NULL);
				quiver_image_view_add_scale_hq_timeout(imageview);
	
			}
			else
			{
				quiver_image_view_invalidate_image_area(imageview,NULL);
				if (0 == priv->animation_timeout_id)
					quiver_image_view_start_animation(imageview);
			}
		}
		else
		{
			priv->area_updated = FALSE;
		}
	}

}

static void draw_pixbuf(QuiverImageView *imageview, cairo_t *cr) {
	QuiverImageViewPrivate *priv = quiver_image_view_get_instance_private(imageview);
    if (priv->pixbuf_scaled) {
        gdk_cairo_set_source_pixbuf(cr, priv->pixbuf_scaled, 0, 0);
        cairo_paint(cr);
    } else if (priv->pixbuf) {
        gdk_cairo_set_source_pixbuf(cr, priv->pixbuf, 0, 0);
        cairo_paint(cr);
    }
}

static void quiver_image_view_send_reload_event(QuiverImageView *imageview) {
    g_signal_emit(G_OBJECT(imageview), imageview_signals[SIGNAL_RELOAD], 0);
}

static void quiver_image_view_prepare_transition_pixbufs(QuiverImageView *imageview) {
}

static void quiver_image_view_create_next_transition_pixbuf(QuiverImageView *imageview) {
}

static void quiver_image_view_create_scaled_pixbuf(QuiverImageView *imageview, GdkInterpType interptype) {
	QuiverImageViewPrivate *priv = quiver_image_view_get_instance_private(imageview);
    if (!priv->pixbuf) return;

    gint display_width, display_height;
    quiver_image_view_get_pixbuf_display_size(imageview, &display_width, &display_height);

    if (priv->pixbuf_scaled) {
        g_object_unref(priv->pixbuf_scaled);
        priv->pixbuf_scaled = NULL;
    }

    if (display_width > 0 && display_height > 0) {
        priv->pixbuf_scaled = gdk_pixbuf_scale_simple(
            priv->pixbuf,
            display_width,
            display_height,
            interptype
        );
    }
}
