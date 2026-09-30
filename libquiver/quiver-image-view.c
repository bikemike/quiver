#include <config.h>
#include <gtk/gtk.h>

#include <math.h>
#include <string.h>
#include <sys/time.h>

#include "quiver-pixbuf-utils.h"
#include "quiver-image-view.h"
#include "quiver-marshallers.h"

//#include "gtkintl.h"


#define QUIVER_IMAGE_VIEW_GET_PRIVATE(obj) (quiver_image_view_get_instance_private (QUIVER_IMAGE_VIEW (obj)))

/* set up some defaults */
/* Max zoom (magnification) applied to an image: 16x (1600%).  Matches the
 * video zoom ceiling; adjustable constant.  Was previously 50x which was
 * effectively unbounded. */
#define QUIVER_IMAGE_VIEW_MAG_MAX              16.
#define QUIVER_IMAGE_VIEW_MIN_IMAGE_SIZE       32
#define QUIVER_IMAGE_VIEW_SCALE_HQ_TIMEOUT     200

#define TRANSITION_FPS           40.
#define TRANSITION_MIN_TIMEOUT   5.
#define TRANSITION_TIME          .5   //seconds

#define SMOOTH_SCROLL_TIMEOUT                    35 // 35 ms ~= 28fps

#define QUIVER_PARAM_READWRITE G_PARAM_READWRITE|G_PARAM_STATIC_NAME|G_PARAM_STATIC_NICK|G_PARAM_STATIC_BLURB

struct _QuiverImageViewPrivate
{
	GdkTexture *texture;
#if HAVE_GDK_PIXBUF
	GdkPixbuf *pixbuf;
	GdkPixbuf *pixbuf_scaled;
	GdkPixbufAnimation *pixbuf_animation;
	GdkPixbufAnimationIter *pixbuf_animation_iter;
#endif
	
	gint pixbuf_width;
	gint pixbuf_height;

	// used to keep track of the actual size
	// of the image being loaded
	gint pixbuf_width_next;
	gint pixbuf_height_next;

	/* backend-neutral animation: frame textures with per-frame delays (ms).
	 * Each texture in animation_frames carries one owned reference. */
	GdkTexture **animation_frames;
	gint *animation_delays;
	gsize animation_n_frames;
	gsize animation_frame_index;

	QuiverImageViewMode view_mode;
	QuiverImageViewMode view_mode_last;

	gboolean transitions_enabled;
	gboolean in_transition;
	gdouble transition_progress;
	gint64 transition_start_time;
	guint transition_tick_id;
	GdkTexture *transition_texture_old;
	gint transition_old_w;
	gint transition_old_h;
	gint transition_n_frames;
#if HAVE_GDK_PIXBUF
	GdkPixbuf *transition_pixbuf_old;
	GdkPixbuf *transition_pixbuf_new;
	// list of intermediate pixbufs for the transition
	GList *transition_pixbufs_intermediate;
#endif
	guint transition_timeout_id;
	guint idle_transition_create_id;
	
	QuiverImageViewMagnificationMode magnification_mode;
	
	guint magnification_timeout_id;
	
	gdouble magnification_final;
	gdouble magnification; // magnification level as a percent (1 = 100%)

	guint timeout_scale_hq_id;
	
	GtkAdjustment *hadjustment;
	GtkAdjustment *vadjustment;
	guint hscroll_policy : 1;
	guint vscroll_policy : 1;

	guint scroll_timeout_id;
	gdouble last_hadjustment;
	gdouble last_vadjustment;

	/* set once dispose starts: no timeout may be armed from then on, because a
	 * source left behind would fire into an object that is on its way out */
	gboolean disposed;

	/* has the area been updated as the image is being loaded?*/
	gboolean area_updated;
	guint animation_timeout_id;


	QuiverImageViewMouseMode mouse_move_mode;
	gdouble mouse_x1, mouse_y1;
	gdouble mouse_x2, mouse_y2;
	gboolean mouse_move_capture;

	gboolean scroll_draw;

	gboolean rubberband_mode_start;
	gboolean rubberband_mode;
	GdkRectangle rubberband_rect;
	GdkRectangle rubberband_rect_old;

	gboolean smooth_scroll;
	
	guint tick_id_smooth_scroll_slowdown;
	guint timeout_id_smooth_scroll_slowdown;
	gint64 smooth_scroll_last_time;
	struct timeval last_motion_time;
	GList* velocity_time_list;
	
	gboolean reload_event_sent;

	gboolean needs_recenter;
	gboolean zoom_anchor_center;

	/* The framing "keep zoom and pan" is keeping, held while there is no
	 * picture to read it off: the viewer blanks the view while the next item
	 * loads, and the scroll position it leaves behind is the top left corner. */
	gboolean has_kept_center;
	gdouble kept_center_x;
	gdouble kept_center_y;
	/* set by a delivery that had no viewport to apply the kept centre in, and
	 * cleared by the first size change that has one */
	gboolean needs_kept_center;

};
G_DEFINE_TYPE_WITH_CODE(QuiverImageView,quiver_image_view,GTK_TYPE_WIDGET, G_ADD_PRIVATE(QuiverImageView) G_IMPLEMENT_INTERFACE(GTK_TYPE_SCROLLABLE, NULL));

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

static void quiver_image_view_snapshot (GtkWidget *widget, GtkSnapshot *snapshot);
static void quiver_image_view_size_allocate  (GtkWidget     *widget,
                                             int width,
                                             int height,
                                             int baseline);

static void quiver_image_view_measure (GtkWidget *widget,
					GtkOrientation orientation,
					int for_size,
					int *minimum,
					int *natural,
					int *minimum_baseline,
					int *natural_baseline);

static void      quiver_image_view_handle_size_change (QuiverImageView *imageview);

static void quiver_image_view_gesture_pressed (GtkGestureClick *gesture,
					       int n_press,
					       double x,
					       double y,
					       QuiverImageView *imageview);
static void quiver_image_view_gesture_drag_begin (GtkGestureDrag *gesture,
						  double x,
						  double y,
						  QuiverImageView *imageview);
static void quiver_image_view_gesture_drag_update (GtkGestureDrag *gesture,
						   double x,
						   double y,
						   QuiverImageView *imageview);
static void quiver_image_view_gesture_drag_end (GtkGestureDrag *gesture,
						double x,
						double y,
						QuiverImageView *imageview);
static void quiver_image_view_setup_controllers (QuiverImageView *imageview);

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

static void     quiver_image_view_dispose(GObject *object);
static void     quiver_image_view_finalize(GObject *object);

/* start utility function prototypes*/
static void quiver_image_view_send_reload_event(QuiverImageView *imageview);
static gboolean quiver_image_view_mode_is_zoomed(QuiverImageViewMode mode);
static guint quiver_image_view_get_width(QuiverImageView *imageview);
static guint quiver_image_view_get_height(QuiverImageView *imageview);
static void
quiver_image_view_set_adjustment_upper (GtkAdjustment *adj,
				 gdouble        upper,
				 gboolean       always_emit_changed);


static void quiver_image_view_add_scale_hq_timeout(QuiverImageView *imageview);
static gboolean quiver_image_view_timeout_scale_hq(gpointer data);
static void quiver_image_view_stop_smooth_scroll_slowdown(QuiverImageView *imageview);
static void quiver_image_view_start_smooth_scroll_slowdown(QuiverImageView *imageview);
static gboolean quiver_image_view_tick_smooth_scroll_slowdown(GtkWidget *widget, GdkFrameClock *frame_clock, gpointer data);
static gboolean quiver_image_view_timeout_smooth_scroll_slowdown(gpointer data);
static void quiver_image_view_create_scaled_pixbuf(QuiverImageView *imageview,GdkInterpType interptype);

static gboolean quiver_image_view_timeout_scroll(gpointer data);
static void quiver_image_view_add_scroll_timeout(QuiverImageView *imageview);
static void quiver_image_view_scroll(QuiverImageView *imageview);

#if HAVE_GDK_PIXBUF
static void quiver_image_view_start_animation(QuiverImageView *imageview);
static void quiver_image_view_add_animation_timeout(QuiverImageView *imageview);
static gboolean quiver_image_view_timeout_animation(gpointer data);
#endif

static void quiver_image_view_animation_frames_stop(QuiverImageView *imageview);
static void quiver_image_view_animation_frames_start(QuiverImageView *imageview);
static gboolean quiver_image_view_animation_frames_timeout(gpointer data);

static void quiver_image_view_transition_start(QuiverImageView *imageview);
static void quiver_image_view_transition_stop(QuiverImageView *imageview);

static void      quiver_image_view_set_magnification_full(QuiverImageView *imageview,gdouble new_mag);
static gdouble   quiver_image_view_clamp_magnification(QuiverImageView *imageview,gdouble new_mag);
static void      quiver_image_view_add_magnification_timeout(QuiverImageView *imageview);
static gboolean quiver_image_view_timeout_magnification(gpointer data);
//static void quiver_image_view_magnification_start(QuiverImageView *imageview);

static void quiver_image_view_get_pixbuf_display_size(QuiverImageView *imageview, gint *width, gint *height);
static void quiver_image_view_get_pixbuf_display_size_alt(QuiverImageView *imageview,gint in_width, gint in_height, gint *out_width, gint *out_height);

static void quiver_image_view_set_default_adjustment_values(QuiverImageView *imageview);

static void quiver_image_view_invalidate_old_image_area(QuiverImageView *imageview,gint new_width, gint new_height);
#if HAVE_GDK_PIXBUF
static void quiver_image_view_invalidate_image_area(QuiverImageView *imageview,GdkRectangle *rect);
#endif

static void quiver_image_view_set_view_mode_full(QuiverImageView *imageview,QuiverImageViewMode mode,gboolean invalidate);

static void quiver_image_view_update_size(QuiverImageView *imageview);
static void quiver_image_view_remember_view_center(QuiverImageView *imageview);

static void quiver_image_view_prepare_for_new_pixbuf(QuiverImageView *imageview, gint new_width, gint new_height);

#if HAVE_GDK_PIXBUF
/* start pixbuf loader callbacks */
static void pixbuf_loader_size_prepared(GdkPixbufLoader *loader,gint width, gint height,gpointer userdata);
static void pixbuf_loader_area_prepared(GdkPixbufLoader *loader,gpointer userdata);
static void pixbuf_loader_area_updated (GdkPixbufLoader *loader,gint x, gint y, gint width,gint height,gpointer userdata);
static void pixbuf_loader_closed(GdkPixbufLoader *loader,gpointer userdata);
/* end pixbuf loader callbacks */
#endif

/* end utility function prototypes*/

/* end private function prototypes */

/* start private globals */

//static guint imageview_signals[SIGNAL_COUNT] = {0};

/* end private globals */


/* start private functions */
/* The zoomed modes let the user pick the magnification themselves; they only
 * differ in what happens to it when another image is loaded. */
static gboolean
quiver_image_view_mode_is_zoomed(QuiverImageViewMode mode)
{
	return (QUIVER_IMAGE_VIEW_MODE_ZOOM == mode
		|| QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP == mode);
}

static void 
quiver_image_view_class_init (QuiverImageViewClass *klass)
{
	GtkWidgetClass *widget_class;
	GObjectClass *obj_class;

	widget_class = GTK_WIDGET_CLASS (klass);
	obj_class = G_OBJECT_CLASS (klass);

	widget_class->snapshot = quiver_image_view_snapshot;
	widget_class->measure = quiver_image_view_measure;
	widget_class->size_allocate = quiver_image_view_size_allocate;

	//klass->set_scroll_adjustments      = quiver_image_view_set_scroll_adjustments;

	obj_class->dispose                 = quiver_image_view_dispose;
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

	/*

	g_object_class_install_property (obj_class,
		PROP_N_ITEMS,
		g_param_spec_boxed ("n-items-closure",
		P_("Number of Items Closure"),
		P_("The closure to get the number of items in the imageview"),
		G_TYPE_CLOSURE,
		QUIVER_PARAM_READWRITE));
	g_object_class_install_property (obj_class,
		PROP_THUMBNAIL_PIXBUF,
		g_param_spec_boxed ("thumbnail-pixbuf-closure",
		P_("The nth items pixbuf Closure"),
		P_("The closure to get the thumbnail pixbuf for the nth item in the imageview"),
		G_TYPE_CLOSURE,
		QUIVER_PARAM_READWRITE));
	g_object_class_install_property (obj_class,
		PROP_ICON_PIXBUF,
		g_param_spec_boxed ("icon-pixbuf-closure",
		P_("The nth items icon pixbuf Closure"),
		P_("The closure to get the icon pixbuf for the nth item in the imageview"),
		G_TYPE_CLOSURE,
		QUIVER_PARAM_READWRITE));
	g_object_class_install_property (obj_class,
		PROP_TEXT,
		g_param_spec_boxed ("text-closure",
		P_("The nth items text Closure"),
		P_("The closure to get the text for the nth item in the imageview"),
		G_TYPE_CLOSURE,
		QUIVER_PARAM_READWRITE));
		*/
}

static void 
quiver_image_view_init(QuiverImageView *imageview)
{
	//printf("in  the init!\n");
	imageview->priv = QUIVER_IMAGE_VIEW_GET_PRIVATE(imageview);

	imageview->priv->texture       = NULL;
#if HAVE_GDK_PIXBUF
	imageview->priv->pixbuf        = NULL;
	imageview->priv->pixbuf_scaled = NULL;
	imageview->priv->pixbuf_animation = NULL;
	imageview->priv->pixbuf_animation_iter = NULL;
#endif
	
	imageview->priv->pixbuf_width  = 0;
	imageview->priv->pixbuf_height = 0;

	imageview->priv->pixbuf_width_next  = 0;
	imageview->priv->pixbuf_height_next = 0;
	
	imageview->priv->view_mode = QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW;
	imageview->priv->view_mode_last = QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW;
	
	imageview->priv->transitions_enabled = FALSE;
	imageview->priv->in_transition = FALSE;
	imageview->priv->transition_progress = 0.0;
	imageview->priv->transition_start_time = 0;
	imageview->priv->transition_tick_id = 0;
	imageview->priv->transition_texture_old = NULL;
	imageview->priv->transition_old_w = 0;
	imageview->priv->transition_old_h = 0;
	imageview->priv->transition_n_frames = (gint)(TRANSITION_TIME * TRANSITION_FPS);
#if HAVE_GDK_PIXBUF
	imageview->priv->transition_pixbuf_old = NULL;
	imageview->priv->transition_pixbuf_new = NULL;
	imageview->priv->transition_pixbufs_intermediate = NULL;
#endif
	imageview->priv->transition_timeout_id = 0;
	imageview->priv->idle_transition_create_id = 0;


	imageview->priv->magnification = 1; // magnification level as a percent (1 = 100%)
	imageview->priv->magnification_timeout_id = 0; // 
	imageview->priv->magnification_final = 1;
	imageview->priv->zoom_anchor_center = FALSE;


	imageview->priv->timeout_scale_hq_id = 0;
/*
	imageview->priv->closure_n_items = NULL;
	imageview->priv->closure_icon_pixbuf = NULL;
	imageview->priv->closure_thumbnail_pixbuf = NULL;
	imageview->priv->closure_text = NULL;
*/
	imageview->priv->hadjustment  = NULL;
	imageview->priv->vadjustment  = NULL;

	imageview->priv->scroll_timeout_id = 0;
	imageview->priv->last_hadjustment = 0.0;
	imageview->priv->last_vadjustment = 0.0;

	imageview->priv->area_updated = FALSE;
	imageview->priv->animation_timeout_id = FALSE;

	imageview->priv->scroll_draw   = TRUE;
	imageview->priv->smooth_scroll = TRUE;
	
	imageview->priv->reload_event_sent = FALSE;
	
	imageview->priv->mouse_x1 = 0;
	imageview->priv->mouse_y1 = 0;
	imageview->priv->mouse_x2 = 0;
	imageview->priv->mouse_y2 = 0;

	imageview->priv->mouse_move_capture = FALSE;
	imageview->priv->mouse_move_mode = QUIVER_IMAGE_VIEW_MOUSE_MODE_DRAG;

	imageview->priv->rubberband_mode_start = FALSE;
	imageview->priv->rubberband_mode = FALSE;

	imageview->priv->tick_id_smooth_scroll_slowdown = 0;
	imageview->priv->timeout_id_smooth_scroll_slowdown = 0;
	imageview->priv->smooth_scroll_last_time = 0;
	imageview->priv->velocity_time_list = NULL;

	gtk_widget_set_focusable(GTK_WIDGET(imageview), TRUE);

	gtk_widget_set_size_request(GTK_WIDGET(imageview),QUIVER_IMAGE_VIEW_MIN_IMAGE_SIZE,QUIVER_IMAGE_VIEW_MIN_IMAGE_SIZE);

	quiver_image_view_setup_controllers(imageview);

	
}

/*
static void
quiver_image_view_destroy(GtkObject *object)
{
	QuiverImageView *imageview = QUIVER_IMAGE_VIEW(object);

}
*/


/*
static void
quiver_image_view_unrealize(GtkWidget *widget)
{

}
*/


static void
quiver_image_view_set_property (GObject *object,
                    guint               prop_id,
                    const GValue       *value,
                    GParamSpec         *pspec)
{
	QuiverImageView  *imageview;

	imageview = QUIVER_IMAGE_VIEW (object);

	switch (prop_id)
	{
		case PROP_HADJUSTMENT:
			quiver_image_view_set_hadjustment(imageview, g_value_get_object (value));
			break;
		case PROP_VADJUSTMENT:
			quiver_image_view_set_vadjustment(imageview, g_value_get_object (value));
			break;
		case PROP_HSCROLL_POLICY:
			imageview->priv->hscroll_policy = g_value_get_enum(value);
			gtk_widget_queue_resize(GTK_WIDGET(imageview));
			break;
		case PROP_VSCROLL_POLICY:
			imageview->priv->vscroll_policy = g_value_get_enum(value);
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
	QuiverImageView  *imageview;

	imageview = QUIVER_IMAGE_VIEW (object);

	switch (prop_id)
	{
		case PROP_HADJUSTMENT:
			g_value_set_object(value, imageview->priv->hadjustment);
			break;
		case PROP_VADJUSTMENT:
			g_value_set_object(value, imageview->priv->vadjustment);
			break;
		case PROP_HSCROLL_POLICY:
			g_value_set_enum(value, imageview->priv->hscroll_policy);
			break;
		case PROP_VSCROLL_POLICY:
			g_value_set_enum(value, imageview->priv->vscroll_policy);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}



static void
quiver_image_view_dispose(GObject *object)
{
	QuiverImageView *imageview = QUIVER_IMAGE_VIEW(object);

	imageview->priv->disposed = TRUE;
	quiver_image_view_transition_stop(imageview);
	quiver_image_view_animation_frames_stop(imageview);

	/* The adjustments come off first: they are what arms the scroll timeout,
	 * and taking them down moves them, which would put a source back that the
	 * removals below have already cleaned out. */
	if (imageview->priv->hadjustment)
	{
		g_signal_handlers_disconnect_by_func (imageview->priv->hadjustment,
			quiver_image_view_adjustment_value_changed,
			imageview);
		g_clear_object (&imageview->priv->hadjustment);
	}

	if (imageview->priv->vadjustment)
	{
		g_signal_handlers_disconnect_by_func (imageview->priv->vadjustment,
			quiver_image_view_adjustment_value_changed,
			imageview);
		g_clear_object (&imageview->priv->vadjustment);
	}

	if (0 != imageview->priv->magnification_timeout_id)
	{
		g_source_remove(imageview->priv->magnification_timeout_id);
		imageview->priv->magnification_timeout_id = 0;
	}

	if (0 != imageview->priv->timeout_scale_hq_id)
	{
		g_source_remove(imageview->priv->timeout_scale_hq_id);
		imageview->priv->timeout_scale_hq_id = 0;
	}

	if (0 != imageview->priv->scroll_timeout_id)
	{
		g_source_remove(imageview->priv->scroll_timeout_id);
		imageview->priv->scroll_timeout_id = 0;
	}

	if (0 != imageview->priv->transition_timeout_id)
	{
		g_source_remove(imageview->priv->transition_timeout_id);
		imageview->priv->transition_timeout_id = 0;
	}

	if (0 != imageview->priv->animation_timeout_id)
	{
		g_source_remove(imageview->priv->animation_timeout_id);
		imageview->priv->animation_timeout_id = 0;
	}

	quiver_image_view_animation_frames_stop(imageview);

	quiver_image_view_stop_smooth_scroll_slowdown(imageview);

#if HAVE_GDK_PIXBUF
	if (imageview->priv->pixbuf_animation_iter)
	{
		g_clear_object (&imageview->priv->pixbuf_animation_iter);
	}

	if (imageview->priv->pixbuf_animation)
	{
		g_clear_object (&imageview->priv->pixbuf_animation);
	}

	if (imageview->priv->pixbuf_scaled)
	{
		g_clear_object (&imageview->priv->pixbuf_scaled);
	}
#endif

	if (imageview->priv->texture)
	{
		g_clear_object (&imageview->priv->texture);
	}

#if HAVE_GDK_PIXBUF
	if (imageview->priv->pixbuf)
	{
		g_clear_object (&imageview->priv->pixbuf);
	}
#endif

	if (imageview->priv->transition_texture_old)
	{
		g_clear_object (&imageview->priv->transition_texture_old);
	}

#if HAVE_GDK_PIXBUF
	if (imageview->priv->transition_pixbuf_old)
	{
		g_clear_object (&imageview->priv->transition_pixbuf_old);
	}

	if (imageview->priv->transition_pixbuf_new)
	{
		g_clear_object (&imageview->priv->transition_pixbuf_new);
	}
#endif

	if (imageview->priv->velocity_time_list)
	{
		g_list_free_full(imageview->priv->velocity_time_list, g_free);
		imageview->priv->velocity_time_list = NULL;
	}

#if HAVE_GDK_PIXBUF
	if (imageview->priv->transition_pixbufs_intermediate)
	{
		g_list_free_full(imageview->priv->transition_pixbufs_intermediate, g_object_unref);
		imageview->priv->transition_pixbufs_intermediate = NULL;
	}
#endif

	G_OBJECT_CLASS (quiver_image_view_parent_class)->dispose (object);
}

static void
quiver_image_view_finalize(GObject *object)
{
	QuiverImageView *imageview = QUIVER_IMAGE_VIEW(object);

	if (imageview->priv->velocity_time_list)
	{
		g_list_free_full(imageview->priv->velocity_time_list, g_free);
		imageview->priv->velocity_time_list = NULL;
	}

#if HAVE_GDK_PIXBUF
	if (imageview->priv->transition_pixbufs_intermediate)
	{
		g_list_free_full(imageview->priv->transition_pixbufs_intermediate, g_object_unref);
		imageview->priv->transition_pixbufs_intermediate = NULL;
	}
#endif

	G_OBJECT_CLASS (quiver_image_view_parent_class)->finalize (object);
}


static void
quiver_image_view_size_allocate (GtkWidget     *widget,
				int width,
				int height,
				int baseline)
{
	(void)baseline;
	(void)width;
	(void)height;
	g_return_if_fail (QUIVER_IS_IMAGE_VIEW (widget));

	QuiverImageView *imageview = QUIVER_IMAGE_VIEW(widget);

	quiver_image_view_handle_size_change(imageview);
}

static void quiver_image_view_measure (GtkWidget *widget,
					GtkOrientation orientation,
					int for_size,
					int *minimum,
					int *natural,
					int *minimum_baseline,
					int *natural_baseline)
{
	(void)widget;
	(void)orientation;
	(void)for_size;
	(void)minimum_baseline;
	(void)natural_baseline;
	*minimum = *natural = QUIVER_IMAGE_VIEW_MIN_IMAGE_SIZE;
}

/*
static void
quiver_image_view_size_request (GtkWidget *widget, GtkRequisition *requisition)
{
	QuiverImageView *imageview;
 (void)imageview;
		
	imageview = QUIVER_IMAGE_VIEW(widget);
	requisition->width = 20;
	requisition->height = 20;
	
	
}
static void
quiver_image_view_get_preferred_width (GtkWidget *widget, gint* min_width, gint* natural_width)
{
	GtkRequisition requisition = {0};
	quiver_image_view_size_request(widget, &requisition);
	*min_width = *natural_width = requisition.width;
}
static void
quiver_image_view_get_preferred_height (GtkWidget *widget, gint* min_height, gint* natural_height)
{
	GtkRequisition requisition = {0};
	quiver_image_view_size_request(widget, &requisition);
	*min_height = *natural_height = requisition.height;
}
*/

static void
quiver_image_view_handle_size_change (QuiverImageView *imageview)
{
	gdouble old_mag;

	quiver_image_view_update_size(imageview);

	if (imageview->priv->has_kept_center
	    && imageview->priv->needs_kept_center
	    && gtk_adjustment_get_page_size(imageview->priv->hadjustment) > 1.)
	{
		/* the position the mode is keeping, applied now that there is a viewport
		 * to apply it in - a view that was handed a picture while it was off
		 * screen could not hold it */
		quiver_image_view_set_view_center(imageview,
			imageview->priv->kept_center_x, imageview->priv->kept_center_y);
		imageview->priv->needs_kept_center = FALSE;
		imageview->priv->needs_recenter = FALSE;
	}
	else if (imageview->priv->needs_recenter
	    && gtk_adjustment_get_page_size(imageview->priv->hadjustment) > 1.)
	{
		quiver_image_view_set_default_adjustment_values(imageview);
		imageview->priv->needs_recenter = FALSE;
	}

	quiver_image_view_send_reload_event(imageview);

	quiver_image_view_transition_stop(imageview);
	quiver_image_view_create_scaled_pixbuf(imageview,GDK_INTERP_NEAREST);
	quiver_image_view_add_scale_hq_timeout(imageview);

	// set the magnification
	if (0 == imageview->priv->magnification_timeout_id)
	{
		old_mag = imageview->priv->magnification;
		imageview->priv->magnification = quiver_image_view_get_magnification(imageview);

		if (old_mag != imageview->priv->magnification)
		{
			// emit a magnification changed signal
			g_signal_emit(imageview,imageview_signals[SIGNAL_MAGNIFICATION_CHANGED],0);
		}
	}
}

static void quiver_image_view_send_reload_event(QuiverImageView *imageview)
{
	// send reload signal if current display size is 
	// greater than the actual texture size and the actual
	// texture size is less than the "set_texture_at_size" size
	if (NULL != imageview->priv->texture && !imageview->priv->reload_event_sent)
	{
		gint aw, ah;
		gint width,height;
	
		quiver_image_view_get_pixbuf_display_size(imageview,&width,&height);

		aw = gdk_texture_get_width(imageview->priv->texture);
		ah = gdk_texture_get_height(imageview->priv->texture);

		if (imageview->priv->pixbuf_width > aw || imageview->priv->pixbuf_height > ah)
		{
			if (width > aw || height > ah)
			{
				g_signal_emit(imageview,imageview_signals[SIGNAL_RELOAD],0);
				imageview->priv->reload_event_sent = TRUE;
			}
		}
	}
}






static void quiver_image_view_create_scaled_pixbuf(QuiverImageView *imageview, GdkInterpType interptype)
{
	(void)interptype;
	GtkWidget *widget = GTK_WIDGET(imageview);
	if (gtk_widget_get_mapped(widget))
	{
		gtk_widget_queue_draw(widget);
	}
}

static void draw_texture_with_size(QuiverImageView *imageview, GtkSnapshot *snapshot, GdkTexture *texture, gint img_w, gint img_h)
{
	if (!texture) return;

	GtkWidget *widget = GTK_WIDGET(imageview);
	gint alloc_w = gtk_widget_get_width(widget);
	gint alloc_h = gtk_widget_get_height(widget);
	if (alloc_w <= 0 || alloc_h <= 0) return;

	gint disp_w = 0, disp_h = 0;
	quiver_image_view_get_pixbuf_display_size_for_mode_alt(
		imageview, imageview->priv->view_mode, img_w, img_h, &disp_w, &disp_h);
	if (disp_w <= 0 || disp_h <= 0)
	{
		disp_w = gdk_texture_get_width(texture);
		disp_h = gdk_texture_get_height(texture);
	}

	gdouble hadjust = imageview->priv->hadjustment ? gtk_adjustment_get_value(imageview->priv->hadjustment) : 0.0;
	gdouble vadjust = imageview->priv->vadjustment ? gtk_adjustment_get_value(imageview->priv->vadjustment) : 0.0;

	gdouble x = (disp_w < alloc_w) ? (alloc_w - disp_w) / 2.0 : -hadjust;
	gdouble y = (disp_h < alloc_h) ? (alloc_h - disp_h) / 2.0 : -vadjust;

	graphene_rect_t bounds;
	graphene_rect_init(&bounds, (float)x, (float)y, (float)disp_w, (float)disp_h);

	gtk_snapshot_append_texture(snapshot, texture, &bounds);
}

static void
quiver_image_view_snapshot(GtkWidget* widget, GtkSnapshot* snapshot)
{
	QuiverImageView *imageview = QUIVER_IMAGE_VIEW(widget);

#if HAVE_GDK_PIXBUF
	if (NULL == imageview->priv->texture && NULL != imageview->priv->pixbuf)
	{
		imageview->priv->texture = quiver_pixbuf_to_texture(imageview->priv->pixbuf);
	}
#endif

	if (!imageview->priv->texture && !imageview->priv->transition_texture_old)
		return;

	gint alloc_w = gtk_widget_get_width(widget);
	gint alloc_h = gtk_widget_get_height(widget);
	if (alloc_w <= 0 || alloc_h <= 0)
		return;

	graphene_rect_t clip_rect;
	graphene_rect_init(&clip_rect, 0, 0, (float)alloc_w, (float)alloc_h);
	gtk_snapshot_push_clip(snapshot, &clip_rect);

	if (imageview->priv->in_transition && imageview->priv->transition_texture_old != NULL && imageview->priv->texture != NULL)
	{
		gtk_snapshot_push_cross_fade(snapshot, imageview->priv->transition_progress);
		draw_texture_with_size(imageview, snapshot, imageview->priv->transition_texture_old,
		                       imageview->priv->transition_old_w, imageview->priv->transition_old_h);
		gtk_snapshot_pop(snapshot);
		draw_texture_with_size(imageview, snapshot, imageview->priv->texture,
		                       imageview->priv->pixbuf_width, imageview->priv->pixbuf_height);
		gtk_snapshot_pop(snapshot);
	}
	else if (imageview->priv->texture != NULL)
	{
		draw_texture_with_size(imageview, snapshot, imageview->priv->texture,
		                       imageview->priv->pixbuf_width, imageview->priv->pixbuf_height);
	}

	gtk_snapshot_pop(snapshot);
}


static void
quiver_image_view_gesture_pressed (GtkGestureClick *gesture,
				   int n_press,
				   double x,
				   double y,
				   QuiverImageView *imageview)
{
	(void)gesture;
	GtkWidget *widget = GTK_WIDGET(imageview);

	imageview->priv->mouse_x1 = imageview->priv->mouse_x2 = x;
	imageview->priv->mouse_y1 = imageview->priv->mouse_y2 = y;

	quiver_image_view_stop_smooth_scroll_slowdown(imageview);

	if (!gtk_widget_has_focus (widget))
	{
		gtk_widget_grab_focus (widget);
	}

	if (2 == n_press)
	{
		quiver_image_view_activate(imageview);
	}
}

static void
quiver_image_view_gesture_drag_begin (GtkGestureDrag *gesture,
				      double x,
				      double y,
				      QuiverImageView *imageview)
{
	(void)gesture;
	imageview->priv->mouse_x1 = x;
	imageview->priv->mouse_y1 = y;
	imageview->priv->mouse_move_capture = TRUE;
	quiver_image_view_stop_smooth_scroll_slowdown(imageview);
	gettimeofday(&imageview->priv->last_motion_time, NULL);
}

static void
quiver_image_view_gesture_drag_update (GtkGestureDrag *gesture,
				       double x,
				       double y,
				       QuiverImageView *imageview)
{
	(void)gesture;
	if (QUIVER_IMAGE_VIEW_MOUSE_MODE_DRAG != imageview->priv->mouse_move_mode)
		return;

	/* GTK4's drag-update passes the pointer as an OFFSET from the drag start
	 * point (the docs name them offset_x/offset_y), unlike drag-begin which
	 * passes widget-relative absolute coordinates.  Re-base on the start point
	 * so the pan math below compares like-for-like. */
	gdouble abs_x = x, abs_y = y;
	{
		gdouble start_x, start_y;
		gtk_gesture_drag_get_start_point(gesture, &start_x, &start_y);
		abs_x += start_x;
		abs_y += start_y;
	}

	struct timeval new_motion_time = {0};
	gettimeofday(&new_motion_time,NULL);
	gdouble old_time = (gdouble)imageview->priv->last_motion_time.tv_sec + ((gdouble)imageview->priv->last_motion_time.tv_usec)/1000000;
	gdouble new_time = (gdouble)new_motion_time.tv_sec + ((gdouble)new_motion_time.tv_usec)/1000000;
	gdouble dt = new_time - old_time;

#define MAX_VELOCITY 12000
	if (dt > 0.0005)
	{
		VelocityTimeStruct* vt = g_malloc(sizeof(VelocityTimeStruct));

		gdouble xdist = abs_x - imageview->priv->mouse_x1;
		gdouble ydist = abs_y - imageview->priv->mouse_y1;

		vt->time     = dt;
		vt->angle    = atan2(ydist, xdist);
		gdouble dist = sqrt ( (double)( ydist*ydist + xdist*xdist));
		vt->velocity =  dist / vt->time ;
		vt->velocity = MIN (MAX_VELOCITY, vt->velocity);

		if ( 3 <= g_list_length(imageview->priv->velocity_time_list) )
		{
			GList* last = g_list_last(imageview->priv->velocity_time_list);
			g_free(last->data);
			imageview->priv->velocity_time_list =
				g_list_delete_link(imageview->priv->velocity_time_list,last);
		}
		imageview->priv->velocity_time_list =
			g_list_prepend(imageview->priv->velocity_time_list, vt);

		imageview->priv->last_motion_time = new_motion_time;
	}

	gdouble hadjust = gtk_adjustment_get_value(imageview->priv->hadjustment);
	gdouble vadjust = gtk_adjustment_get_value(imageview->priv->vadjustment);
	hadjust += imageview->priv->mouse_x1 - abs_x;
	hadjust = MAX(0,MIN(gtk_adjustment_get_upper(imageview->priv->hadjustment) - gtk_adjustment_get_page_size(imageview->priv->hadjustment),hadjust));
	vadjust += imageview->priv->mouse_y1 - abs_y;
	vadjust = MAX(0,MIN(gtk_adjustment_get_upper(imageview->priv->vadjustment) - gtk_adjustment_get_page_size(imageview->priv->vadjustment),vadjust));
	gtk_adjustment_set_value(imageview->priv->hadjustment,hadjust);
	gtk_adjustment_set_value(imageview->priv->vadjustment,vadjust);
	imageview->priv->mouse_x1 = abs_x;
	imageview->priv->mouse_y1 = abs_y;
}

static void
quiver_image_view_gesture_drag_end (GtkGestureDrag *gesture,
				    double x,
				    double y,
				    QuiverImageView *imageview)
{
	(void)gesture;
	(void)x;
	(void)y;
	imageview->priv->mouse_move_capture = FALSE;

	if (QUIVER_IMAGE_VIEW_MOUSE_MODE_DRAG == imageview->priv->mouse_move_mode)
	{
		struct timeval new_motion_time = {0};
		gettimeofday(&new_motion_time,NULL);

		gdouble old_time = (gdouble)imageview->priv->last_motion_time.tv_sec + ((gdouble)imageview->priv->last_motion_time.tv_usec)/1000000;
		gdouble new_time = (gdouble)new_motion_time.tv_sec + ((gdouble)new_motion_time.tv_usec)/1000000;

		if (imageview->priv->smooth_scroll &&
			imageview->priv->velocity_time_list != NULL &&
			(0.1 > new_time - old_time) )
		{
			quiver_image_view_start_smooth_scroll_slowdown(imageview);
		}
		else
		{
			quiver_image_view_stop_smooth_scroll_slowdown(imageview);
		}
	}
}

static void
quiver_image_view_stop_smooth_scroll_slowdown(QuiverImageView *imageview)
{
	if (imageview->priv->tick_id_smooth_scroll_slowdown != 0)
	{
		gtk_widget_remove_tick_callback(GTK_WIDGET(imageview), imageview->priv->tick_id_smooth_scroll_slowdown);
		imageview->priv->tick_id_smooth_scroll_slowdown = 0;
	}
	if (imageview->priv->timeout_id_smooth_scroll_slowdown != 0)
	{
		g_source_remove(imageview->priv->timeout_id_smooth_scroll_slowdown);
		imageview->priv->timeout_id_smooth_scroll_slowdown = 0;
	}
	if (imageview->priv->velocity_time_list != NULL)
	{
		g_list_free_full(imageview->priv->velocity_time_list, g_free);
		imageview->priv->velocity_time_list = NULL;
	}
	imageview->priv->smooth_scroll_last_time = 0;
}

static gboolean
quiver_image_view_smooth_scroll_step(QuiverImageView *imageview, gint64 frame_time_us)
{
	gdouble dt = 0.016;
	if (imageview->priv->smooth_scroll_last_time > 0 && frame_time_us > imageview->priv->smooth_scroll_last_time)
	{
		dt = (gdouble)(frame_time_us - imageview->priv->smooth_scroll_last_time) / 1000000.0;
		if (dt > 0.05) dt = 0.05;
	}
	imageview->priv->smooth_scroll_last_time = frame_time_us;

	GList* list_itr = g_list_first(imageview->priv->velocity_time_list);
	gdouble hvelocity_avg = 0;
	gdouble vvelocity_avg = 0;
	gdouble total_time = 0; 
	if (NULL != list_itr)
	{
		do
		{
			VelocityTimeStruct* vt = (VelocityTimeStruct*)list_itr->data;
			hvelocity_avg += vt->velocity * cos(vt->angle) * vt->time;
			vvelocity_avg += vt->velocity * sin(vt->angle) * vt->time;
			total_time += vt->time;
			list_itr = g_list_next(list_itr);
		} while (NULL != list_itr);
	}

	if (total_time <= 0.0)
	{
		return FALSE;
	}

	hvelocity_avg = hvelocity_avg / total_time;
	vvelocity_avg = vvelocity_avg / total_time;

	if (1 != g_list_length(imageview->priv->velocity_time_list))
	{
		g_list_free_full(imageview->priv->velocity_time_list, g_free);
		imageview->priv->velocity_time_list = NULL;
		
		VelocityTimeStruct* vt = g_malloc(sizeof(VelocityTimeStruct));
		vt->time     = total_time;
		vt->angle    = atan2(vvelocity_avg, hvelocity_avg);
		vt->velocity = sqrt((double)(vvelocity_avg * vvelocity_avg + hvelocity_avg * hvelocity_avg));
		
		imageview->priv->velocity_time_list = 
			g_list_append(imageview->priv->velocity_time_list, vt);
	}

	GList* first = g_list_first(imageview->priv->velocity_time_list);
	if (NULL == first)
	{
		return FALSE;
	}

	VelocityTimeStruct* vt = (VelocityTimeStruct*)first->data;
	// Frame-rate independent exponential decay: k = ln(1.04) / 0.035 ≈ 1.1206 s^-1
	gdouble decay = exp(-1.1206 * dt);
	vt->velocity *= decay;

	hvelocity_avg = vt->velocity * cos(vt->angle);
	vvelocity_avg = vt->velocity * sin(vt->angle);

	gdouble hdistance = dt * hvelocity_avg;
	gdouble vdistance = dt * vvelocity_avg;

	gboolean hdone = FALSE;
	gboolean vdone = FALSE;

	if (vt->velocity < 10.0 || (fabs(hdistance) < 0.05 && fabs(vdistance) < 0.05))
	{
		hdone = TRUE;
		vdone = TRUE;
	}
	else
	{
		if (imageview->priv->hadjustment != NULL && fabs(hvelocity_avg) > 1.0)
		{
			gdouble old_hadjust = gtk_adjustment_get_value(imageview->priv->hadjustment);
			gdouble max_h = gtk_adjustment_get_upper(imageview->priv->hadjustment) -
			                gtk_adjustment_get_page_size(imageview->priv->hadjustment);
			if (max_h < 0.0) max_h = 0.0;

			gdouble hadjust = old_hadjust - hdistance;
			if (hadjust <= 0.0 || hadjust >= max_h)
			{
				hdone = TRUE;
			}
			hadjust = CLAMP(hadjust, 0.0, max_h);

			if (fabs(old_hadjust - hadjust) < 0.001)
			{
				hdone = TRUE;
			}
			else
			{
				gtk_adjustment_set_value(imageview->priv->hadjustment, hadjust);
			}
		}
		else
		{
			hdone = TRUE;
		}

		if (imageview->priv->vadjustment != NULL && fabs(vvelocity_avg) > 1.0)
		{
			gdouble old_vadjust = gtk_adjustment_get_value(imageview->priv->vadjustment);
			gdouble max_v = gtk_adjustment_get_upper(imageview->priv->vadjustment) -
			                gtk_adjustment_get_page_size(imageview->priv->vadjustment);
			if (max_v < 0.0) max_v = 0.0;

			gdouble vadjust = old_vadjust - vdistance;
			if (vadjust <= 0.0 || vadjust >= max_v)
			{
				vdone = TRUE;
			}
			vadjust = CLAMP(vadjust, 0.0, max_v);

			if (fabs(old_vadjust - vadjust) < 0.001)
			{
				vdone = TRUE;
			}
			else
			{
				gtk_adjustment_set_value(imageview->priv->vadjustment, vadjust);
			}
		}
		else
		{
			vdone = TRUE;
		}
	}

	return !(hdone && vdone);
}

static gboolean
quiver_image_view_tick_smooth_scroll_slowdown(GtkWidget *widget, GdkFrameClock *frame_clock, gpointer data)
{
	(void)widget;
	QuiverImageView *imageview = (QuiverImageView*)data;
	gint64 frame_time = gdk_frame_clock_get_frame_time(frame_clock);
	gboolean keep_going = quiver_image_view_smooth_scroll_step(imageview, frame_time);
	if (!keep_going)
	{
		imageview->priv->tick_id_smooth_scroll_slowdown = 0;
		quiver_image_view_stop_smooth_scroll_slowdown(imageview);
		return G_SOURCE_REMOVE;
	}
	return G_SOURCE_CONTINUE;
}

static gboolean
quiver_image_view_timeout_smooth_scroll_slowdown(gpointer data)
{
	QuiverImageView *imageview = (QuiverImageView*)data;
	gint64 now = g_get_monotonic_time();
	gboolean keep_going = quiver_image_view_smooth_scroll_step(imageview, now);
	if (!keep_going)
	{
		imageview->priv->timeout_id_smooth_scroll_slowdown = 0;
		quiver_image_view_stop_smooth_scroll_slowdown(imageview);
		return G_SOURCE_REMOVE;
	}
	return G_SOURCE_CONTINUE;
}

static void
quiver_image_view_start_smooth_scroll_slowdown(QuiverImageView *imageview)
{
	if (imageview->priv->tick_id_smooth_scroll_slowdown != 0)
	{
		gtk_widget_remove_tick_callback(GTK_WIDGET(imageview), imageview->priv->tick_id_smooth_scroll_slowdown);
		imageview->priv->tick_id_smooth_scroll_slowdown = 0;
	}
	if (imageview->priv->timeout_id_smooth_scroll_slowdown != 0)
	{
		g_source_remove(imageview->priv->timeout_id_smooth_scroll_slowdown);
		imageview->priv->timeout_id_smooth_scroll_slowdown = 0;
	}
	imageview->priv->smooth_scroll_last_time = g_get_monotonic_time();

	if (gtk_widget_get_mapped(GTK_WIDGET(imageview)))
	{
		imageview->priv->tick_id_smooth_scroll_slowdown =
			gtk_widget_add_tick_callback(GTK_WIDGET(imageview),
			                             quiver_image_view_tick_smooth_scroll_slowdown,
			                             imageview,
			                             NULL);
	}
	else
	{
		imageview->priv->timeout_id_smooth_scroll_slowdown =
			g_timeout_add(16,
			              quiver_image_view_timeout_smooth_scroll_slowdown,
			              imageview);
	}
}



static void
quiver_image_view_setup_controllers (QuiverImageView *imageview)
{
	GtkWidget *widget = GTK_WIDGET(imageview);

	GtkGesture *click = gtk_gesture_click_new();
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_PRIMARY);
	g_signal_connect(click, "pressed",
		G_CALLBACK(quiver_image_view_gesture_pressed), imageview);
	gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(click));

	GtkGesture *drag = gtk_gesture_drag_new();
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(drag), GDK_BUTTON_PRIMARY);
	g_signal_connect(drag, "drag-begin",
		G_CALLBACK(quiver_image_view_gesture_drag_begin), imageview);
	g_signal_connect(drag, "drag-update",
		G_CALLBACK(quiver_image_view_gesture_drag_update), imageview);
	g_signal_connect(drag, "drag-end",
		G_CALLBACK(quiver_image_view_gesture_drag_end), imageview);
	gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(drag));
}


static GtkAdjustment *
new_default_adjustment (void)
{
  return GTK_ADJUSTMENT (gtk_adjustment_new (0.0, 0.0, 0.0, 0.0, 0.0, 0.0));
}

void      quiver_image_view_set_hadjustment (QuiverImageView *imageview,
                    GtkAdjustment *hadj)
{
	gboolean need_adjust = FALSE;

	g_return_if_fail (QUIVER_IS_IMAGE_VIEW (imageview));

	if (hadj)
		g_return_if_fail (GTK_IS_ADJUSTMENT (hadj));
	else
		hadj = new_default_adjustment ();

	if (imageview->priv->hadjustment && (imageview->priv->hadjustment != hadj))
	{
		g_signal_handlers_disconnect_by_func (imageview->priv->hadjustment,
			quiver_image_view_adjustment_value_changed,
			imageview);
		g_object_unref (imageview->priv->hadjustment);
	}

	if (imageview->priv->hadjustment != hadj)
	{
		imageview->priv->hadjustment = hadj;
		g_object_ref_sink (imageview->priv->hadjustment);
		guint width = quiver_image_view_get_width(imageview);
		quiver_image_view_set_adjustment_upper (imageview->priv->hadjustment, width, FALSE);

		g_signal_connect (imageview->priv->hadjustment, "value_changed",
		G_CALLBACK (quiver_image_view_adjustment_value_changed),
			imageview);
		need_adjust = TRUE;
	}

	/* vadj or hadj can be NULL while constructing; don't emit a signal
	then */
	if (need_adjust && hadj)
		quiver_image_view_adjustment_value_changed (NULL, imageview);
}

void quiver_image_view_set_vadjustment (QuiverImageView *imageview,
                    GtkAdjustment *vadj)
{
	gboolean need_adjust = FALSE;

	g_return_if_fail (QUIVER_IS_IMAGE_VIEW (imageview));

	if (vadj)
		g_return_if_fail (GTK_IS_ADJUSTMENT (vadj));
	else
		vadj = new_default_adjustment ();

	if (imageview->priv->vadjustment && (imageview->priv->vadjustment != vadj))
	{
		g_signal_handlers_disconnect_by_func (imageview->priv->vadjustment,
			quiver_image_view_adjustment_value_changed,
			imageview);
		g_object_unref (imageview->priv->vadjustment);
	}

	if (imageview->priv->vadjustment != vadj)
	{
		imageview->priv->vadjustment = vadj;
		g_object_ref_sink (imageview->priv->vadjustment);
		guint height = quiver_image_view_get_height(imageview);
		quiver_image_view_set_adjustment_upper (imageview->priv->vadjustment, height, FALSE);

		g_signal_connect (imageview->priv->vadjustment, "value_changed",
		G_CALLBACK (quiver_image_view_adjustment_value_changed),
			imageview);
		need_adjust = TRUE;
	}


	/* vadj or hadj can be NULL while constructing; don't emit a signal
	then */
	if (need_adjust && vadj)
		quiver_image_view_adjustment_value_changed (NULL, imageview);
}

void quiver_image_view_add_scale_hq_timeout(QuiverImageView *imageview)
{
	if (imageview->priv->disposed)
		return;

	if (0 != imageview->priv->timeout_scale_hq_id)
	{
		g_source_remove(imageview->priv->timeout_scale_hq_id);
	}
	imageview->priv->timeout_scale_hq_id = g_timeout_add(QUIVER_IMAGE_VIEW_SCALE_HQ_TIMEOUT,quiver_image_view_timeout_scale_hq,imageview);
}

static gboolean 
quiver_image_view_timeout_scale_hq(gpointer data)
{
	gboolean retval;
	QuiverImageView *imageview;
	GtkWidget *widget;

	imageview = (QuiverImageView*)data;
	widget = GTK_WIDGET(imageview);
	retval = FALSE;

	/* see the note in quiver_image_view_timeout_scroll: scaling queues a draw
	 * that can re-arm this timeout, so the id has to go before the work */
	imageview->priv->timeout_scale_hq_id = 0;

	// run the hq scale function
	quiver_image_view_create_scaled_pixbuf(imageview,GDK_INTERP_BILINEAR);
	if (gtk_widget_get_mapped (widget))
	{
		gtk_widget_queue_draw(widget);
	}

	return retval;
}


static void 
quiver_image_view_scroll(QuiverImageView *imageview)
{
	GtkWidget *widget = GTK_WIDGET(imageview);
	gdouble hadj,vadj;
	hadj = floor(gtk_adjustment_get_value(imageview->priv->hadjustment));
	vadj = floor(gtk_adjustment_get_value(imageview->priv->vadjustment));
	
	if (gtk_widget_get_mapped (GTK_WIDGET(imageview)))
	{
		if (imageview->priv->scroll_draw)
		{
			//printf("########### scrolldraw scale\n");
			quiver_image_view_transition_stop(imageview);

		quiver_image_view_create_scaled_pixbuf(imageview,GDK_INTERP_NEAREST);

		gtk_widget_queue_draw(widget);

		quiver_image_view_add_scale_hq_timeout(imageview);
		}
		imageview->priv->last_vadjustment = vadj;
		imageview->priv->last_hadjustment = hadj;
	}
}

static gboolean 
quiver_image_view_timeout_scroll(gpointer data)
{
	QuiverImageView *imageview;

	imageview = (QuiverImageView*)data;

	/* released before the scroll, not after: queueing a draw can adjust the
	 * view and arm the next timeout, and clearing afterwards would drop its id
	 * and leave the source untracked */
	imageview->priv->scroll_timeout_id = 0;

	quiver_image_view_scroll(imageview);

	return FALSE;
}

static void quiver_image_view_add_scroll_timeout(QuiverImageView *imageview)
{
	if (imageview->priv->disposed)
		return;

	if (0 == imageview->priv->scroll_timeout_id)
	{
		imageview->priv->scroll_timeout_id = g_timeout_add(2,quiver_image_view_timeout_scroll,imageview);
	}

}


/* start callbacks */
static void
quiver_image_view_adjustment_value_changed (GtkAdjustment *adjustment,
           QuiverImageView *imageview)
{
	(void)adjustment;
	/* Panning is the user choosing what is framed, so this is where that
	 * position is worth keeping: the view is off screen often enough (the
	 * browser has it while the next item is chosen) that the position has to
	 * outlive the viewport it was chosen in. */
	quiver_image_view_remember_view_center(imageview);

	if (imageview->priv->scroll_draw)
	{
		quiver_image_view_add_scroll_timeout(imageview);
	}
	else
	{
		quiver_image_view_scroll(imageview);
	}
}

/*

static void
quiver_image_view_set_property (GObject *object,
                    guint               prop_id,
                    const GValue       *value,
                    GParamSpec         *pspec)
{
	QuiverImageView  *imageview;

	imageview = QUIVER_IMAGE_VIEW (object);

	switch (prop_id)
	{
		case PROP_N_ITEMS:
			quiver_image_view_set_n_items_closure (imageview, g_value_get_boxed (value));
			break;
		case PROP_ICON_PIXBUF:
			quiver_image_view_set_icon_pixbuf_closure (imageview, g_value_get_boxed (value));
			break;
		case PROP_THUMBNAIL_PIXBUF:
			quiver_image_view_set_thumbnail_pixbuf_closure (imageview, g_value_get_boxed (value));
			break;
		case PROP_TEXT:
			quiver_image_view_set_text_closure (imageview, g_value_get_boxed (value));
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
	QuiverImageView  *imageview;

	imageview = QUIVER_IMAGE_VIEW (object);

	switch (prop_id)
	{
		case PROP_N_ITEMS:
			g_value_set_boxed (value, imageview->priv->closure_n_items);
			break;
		case PROP_ICON_PIXBUF:
			g_value_set_boxed (value, imageview->priv->closure_icon_pixbuf);
			break;
		case PROP_THUMBNAIL_PIXBUF:
			g_value_set_boxed (value, imageview->priv->closure_thumbnail_pixbuf);
			break;
		case PROP_TEXT:
			g_value_set_boxed (value, imageview->priv->closure_text);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

*/

/* end callbacks */

/* start utility functions*/
static guint
quiver_image_view_get_width(QuiverImageView *imageview)
{ (void)imageview; 
	return 1;
}

static guint
quiver_image_view_get_height(QuiverImageView *imageview)
{ (void)imageview; 
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
  
  if (changed || always_emit_changed)
    g_signal_emit_by_name (adj, "changed");
  if (value_changed)
    g_signal_emit_by_name (adj, "value-changed");
}

static void
quiver_image_view_update_size(QuiverImageView *imageview)
{
	GtkWidget *widget = GTK_WIDGET(imageview);
	gint width,height;

	quiver_image_view_get_pixbuf_display_size(imageview,&width,&height);

	GtkAdjustment *hadjustment, *vadjustment;

	hadjustment = imageview->priv->hadjustment;
	vadjustment = imageview->priv->vadjustment;

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

	g_signal_emit_by_name (hadjustment, "changed");
	g_signal_emit_by_name (vadjustment, "changed");
	
}

#if HAVE_GDK_PIXBUF
static void quiver_image_view_start_animation(QuiverImageView *imageview)
{

	imageview->priv->pixbuf_animation_iter = gdk_pixbuf_animation_get_iter(imageview->priv->pixbuf_animation,NULL);
	//g_get_current_time());
	GdkPixbuf *pixbuf = gdk_pixbuf_animation_iter_get_pixbuf(imageview->priv->pixbuf_animation_iter);
	if (NULL != imageview->priv->pixbuf)
	{
		g_object_unref(imageview->priv->pixbuf);
	}
	imageview->priv->pixbuf = gdk_pixbuf_copy(pixbuf);

	quiver_image_view_transition_stop(imageview);

	quiver_image_view_create_scaled_pixbuf(imageview,GDK_INTERP_BILINEAR);
	
	quiver_image_view_invalidate_image_area(imageview,NULL);
	
	quiver_image_view_add_animation_timeout(imageview);
	
}

static void quiver_image_view_add_animation_timeout(QuiverImageView *imageview)
{
	gint delay;
	delay = gdk_pixbuf_animation_iter_get_delay_time(imageview->priv->pixbuf_animation_iter);
	if (-1 != delay)
	{
		imageview->priv->animation_timeout_id = 
			g_timeout_add(delay,quiver_image_view_timeout_animation,imageview);
	}
	else
	{
		imageview->priv->animation_timeout_id = 0;
		if (NULL != imageview->priv->pixbuf_animation_iter)
		{
			g_object_unref(imageview->priv->pixbuf_animation_iter);
			imageview->priv->pixbuf_animation_iter = NULL;
		}
	}


}

static gboolean quiver_image_view_timeout_animation(gpointer data)
{
	GtkWidget *widget;
	QuiverImageView* imageview;
	imageview = (QuiverImageView*)data;
	
	widget = GTK_WIDGET(imageview);

	
	//printf("timeout!\n");
	if (gdk_pixbuf_animation_iter_advance(imageview->priv->pixbuf_animation_iter,NULL))
	{

		GdkPixbuf *pixbuf = gdk_pixbuf_animation_iter_get_pixbuf(imageview->priv->pixbuf_animation_iter);
		if (NULL != imageview->priv->pixbuf)
			g_object_unref(imageview->priv->pixbuf);

		imageview->priv->pixbuf = gdk_pixbuf_copy(pixbuf);
		if (NULL != imageview->priv->texture)
		{
			g_object_unref(imageview->priv->texture);
			imageview->priv->texture = NULL;
		}
		imageview->priv->texture = quiver_pixbuf_to_texture(imageview->priv->pixbuf);
		if (gtk_widget_get_mapped (widget))
		{
			gtk_widget_queue_draw(widget);
		}
	}
	quiver_image_view_add_animation_timeout(imageview);
	
	return FALSE;
}
#endif

static void quiver_image_view_animation_frames_stop(QuiverImageView *imageview)
{
	if (0 != imageview->priv->animation_timeout_id)
	{
		g_source_remove(imageview->priv->animation_timeout_id);
		imageview->priv->animation_timeout_id = 0;
	}
	if (NULL != imageview->priv->animation_frames)
	{
		for (gsize i = 0; i < imageview->priv->animation_n_frames; i++)
		{
			if (NULL != imageview->priv->animation_frames[i])
				g_object_unref(imageview->priv->animation_frames[i]);
		}
		g_free(imageview->priv->animation_frames);
		imageview->priv->animation_frames = NULL;
	}
	g_free(imageview->priv->animation_delays);
	imageview->priv->animation_delays = NULL;
	imageview->priv->animation_n_frames = 0;
	imageview->priv->animation_frame_index = 0;
}

static gboolean quiver_image_view_animation_frames_timeout(gpointer data)
{
	QuiverImageView *imageview = QUIVER_IMAGE_VIEW(data);
	imageview->priv->animation_timeout_id = 0;

	if (NULL == imageview->priv->animation_frames || imageview->priv->animation_n_frames < 2)
		return G_SOURCE_REMOVE;

	imageview->priv->animation_frame_index++;
	if (imageview->priv->animation_frame_index >= imageview->priv->animation_n_frames)
		imageview->priv->animation_frame_index = 0;

	GdkTexture *next = imageview->priv->animation_frames[imageview->priv->animation_frame_index];
	if (NULL != next && next != imageview->priv->texture)
	{
		if (NULL != imageview->priv->texture)
			g_object_unref(imageview->priv->texture);
		imageview->priv->texture = g_object_ref(next);
		if (gtk_widget_get_mapped(GTK_WIDGET(imageview)))
			gtk_widget_queue_draw(GTK_WIDGET(imageview));
	}

	gint delay = imageview->priv->animation_delays[imageview->priv->animation_frame_index];
	imageview->priv->animation_timeout_id =
		g_timeout_add(delay > 0 ? delay : 1, quiver_image_view_animation_frames_timeout, imageview);

	return G_SOURCE_REMOVE;
}

static void quiver_image_view_animation_frames_start(QuiverImageView *imageview)
{
	if (imageview->priv->animation_n_frames < 2)
		return;
	imageview->priv->animation_frame_index = 0;
	gint delay = imageview->priv->animation_delays[0];
	imageview->priv->animation_timeout_id =
		g_timeout_add(delay > 0 ? delay : 1, quiver_image_view_animation_frames_timeout, imageview);
}

static gboolean
quiver_image_view_transition_tick(GtkWidget *widget, GdkFrameClock *frame_clock, gpointer user_data)
{
	(void)user_data;
	QuiverImageView *imageview = QUIVER_IMAGE_VIEW(widget);

	if (!imageview->priv->in_transition)
	{
		imageview->priv->transition_tick_id = 0;
		return G_SOURCE_REMOVE;
	}

	gint64 frame_time = gdk_frame_clock_get_frame_time(frame_clock);
	if (imageview->priv->transition_start_time == 0)
	{
		imageview->priv->transition_start_time = frame_time;
	}

	gdouble elapsed = (frame_time - imageview->priv->transition_start_time) / 1000000.0;
	gdouble t = elapsed / TRANSITION_TIME;

	if (t >= 1.0)
	{
		imageview->priv->transition_progress = 1.0;
		quiver_image_view_transition_stop(imageview);
		gtk_widget_queue_draw(widget);
		return G_SOURCE_REMOVE;
	}

	// Smooth cubic curve: t * t * (3.0 - 2.0 * t)
	imageview->priv->transition_progress = t * t * (3.0 - 2.0 * t);
	gtk_widget_queue_draw(widget);
	return G_SOURCE_CONTINUE;
}

static void quiver_image_view_transition_stop(QuiverImageView *imageview)
{
	if (imageview->priv->transition_tick_id != 0)
	{
		gtk_widget_remove_tick_callback(GTK_WIDGET(imageview), imageview->priv->transition_tick_id);
		imageview->priv->transition_tick_id = 0;
	}

	if (imageview->priv->transition_texture_old != NULL)
	{
		g_object_unref(imageview->priv->transition_texture_old);
		imageview->priv->transition_texture_old = NULL;
	}

	imageview->priv->in_transition = FALSE;
	imageview->priv->transition_progress = 0.0;
	imageview->priv->transition_start_time = 0;
}

static void quiver_image_view_transition_start(QuiverImageView *imageview)
{
	if (imageview->priv->transition_texture_old == NULL || imageview->priv->texture == NULL)
	{
		quiver_image_view_transition_stop(imageview);
		return;
	}

	imageview->priv->in_transition = TRUE;
	imageview->priv->transition_progress = 0.0;
	imageview->priv->transition_start_time = 0;

	if (imageview->priv->transition_tick_id == 0)
	{
		imageview->priv->transition_tick_id = gtk_widget_add_tick_callback(
			GTK_WIDGET(imageview), quiver_image_view_transition_tick, NULL, NULL);
	}
}


static void quiver_image_view_add_magnification_timeout(QuiverImageView *imageview)
{
	if (0 == imageview->priv->magnification_timeout_id)
	{
		imageview->priv->magnification_timeout_id = g_timeout_add(30,quiver_image_view_timeout_magnification,imageview);
	}
}
static gboolean quiver_image_view_timeout_magnification(gpointer data)
{
	gboolean rval = TRUE;
	QuiverImageView *imageview = (QuiverImageView*)data;
	
	gdouble mag_diff = imageview->priv->magnification_final - imageview->priv->magnification;
	gdouble percent_diff = imageview->priv->magnification_final / imageview->priv->magnification;

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
		imageview->priv->magnification_timeout_id = 0;
		quiver_image_view_set_magnification_full(imageview,imageview->priv->magnification_final);
		imageview->priv->zoom_anchor_center = FALSE;
		/* The animation ticked the upper/page size of both adjustments along
		 * the way, but "changed" is only emitted by the full setter, so tell
		 * the listeners that the final magnification is now in effect. */
		if (imageview->priv->hadjustment != NULL)
			g_signal_emit_by_name(imageview->priv->hadjustment, "changed");
		if (imageview->priv->vadjustment != NULL)
			g_signal_emit_by_name(imageview->priv->vadjustment, "changed");
		rval = FALSE;
	}
	else
	{
		quiver_image_view_set_magnification_full(imageview,imageview->priv->magnification + mag_diff/2);
		if (0 != imageview->priv->magnification_timeout_id)
		{
			imageview->priv->magnification_timeout_id = 0;
			quiver_image_view_add_magnification_timeout(imageview);
		}
		rval = FALSE;
	}
	
	return rval;
}
/*
static void quiver_image_view_magnification_start(QuiverImageView *imageview)
{
	//imageview->priv->magnification_final = _percent = 0.;
	quiver_image_view_add_magnification_timeout(imageview);
}
*/

/* end utility functions*/
static void quiver_image_view_get_pixbuf_display_size(QuiverImageView *imageview, gint *width, gint *height)
{
	*width = imageview->priv->pixbuf_width;
	*height = imageview->priv->pixbuf_height;
	quiver_image_view_get_pixbuf_display_size_alt(imageview,*width, *height,width,height);
}

static void quiver_image_view_get_pixbuf_display_size_alt(QuiverImageView *imageview,gint in_width, gint in_height, gint *out_width, gint *out_height)
{
	quiver_image_view_get_pixbuf_display_size_for_mode_alt(imageview,imageview->priv->view_mode,in_width,in_height,out_width,out_height);
}

static void quiver_image_view_set_default_adjustment_values(QuiverImageView *imageview)
{
	gdouble hval, vval;
	hval = (gtk_adjustment_get_upper(imageview->priv->hadjustment) - gtk_adjustment_get_lower(imageview->priv->hadjustment))/2 - gtk_adjustment_get_page_size(imageview->priv->hadjustment)/2;
	vval = (gtk_adjustment_get_upper(imageview->priv->vadjustment) - gtk_adjustment_get_lower(imageview->priv->vadjustment))/2 - gtk_adjustment_get_page_size(imageview->priv->vadjustment)/2;
	gtk_adjustment_set_value(imageview->priv->hadjustment,hval);
	gtk_adjustment_set_value(imageview->priv->vadjustment,vval);
}

/* The zoomed modes keep the magnification and the visible area when another
 * image is loaded.  The scroll adjustments are in widget pixels of the scaled
 * image, so the center is converted to and from a fraction of the picture: the
 * middle of what is on screen, as 0..1 of the width and of the height.
 *
 * A fraction, not a position in pixels, because the number has to describe the
 * same point of a *different* picture.  A pixel offset is only meaningful for
 * the picture it was measured on: carried over to one of another size it lands
 * somewhere else entirely, and where the two pictures differ in aspect ratio it
 * usually lands off the edge, where the scroll range clamps it - so zooming to
 * the bottom right of one picture and switching to a wider, shorter one came up
 * centred horizontally.  A fraction frames the same relative part of any
 * picture, whatever its size or aspect ratio.
 *
 * The fraction is of the *picture*, not of the texture it happens to be drawn
 * from.  The magnification is relative to the picture too - the scroll range is
 * built from it (see quiver_image_view_update_size) - while the texture is
 * whatever resolution was decoded, which at a deep zoom is far smaller than the
 * picture it stands for.  Converting through the texture's own size therefore
 * described a smaller picture than the one on screen: the wanted offset came out
 * off the scroll range, was clamped to the edge, and the clamped result became
 * the "kept" center - so a zoom to the top right of one picture arrived at the
 * middle of the next, and again on the sharper redelivery. */
/* Where the user is looking, as a fraction of the picture, held for as long as
 * there is a picture to hold it for.  The adjustments are where the view reads
 * its position from while it is on screen; this is the same position when it is
 * not, so that "keep zoom and pan" can keep it across a gap in which the view
 * has no viewport at all. */
static void
quiver_image_view_remember_view_center(QuiverImageView *imageview)
{
	gdouble center_x = 0., center_y = 0.;
	if (!quiver_image_view_mode_is_zoomed(imageview->priv->view_mode))
		return;
	if (!quiver_image_view_get_view_center(imageview, &center_x, &center_y))
		return;
	imageview->priv->has_kept_center = TRUE;
	imageview->priv->kept_center_x = center_x;
	imageview->priv->kept_center_y = center_y;
}

gboolean
quiver_image_view_get_view_center(QuiverImageView *imageview, gdouble *center_x, gdouble *center_y)
{
	GdkTexture *texture = imageview->priv->texture;
	gdouble magnification = quiver_image_view_get_magnification(imageview);
	gint texture_width;
	gint texture_height;

	if (!quiver_image_view_mode_is_zoomed(imageview->priv->view_mode))
		return FALSE;
	if (NULL == texture || NULL == imageview->priv->hadjustment || NULL == imageview->priv->vadjustment)
		return FALSE;
	/* the picture, which is the unit the scroll range and the magnification are
	 * in, rather than the texture that is drawn for it */
	texture_width = imageview->priv->pixbuf_width;
	texture_height = imageview->priv->pixbuf_height;
	if (texture_width <= 0)
		texture_width = gdk_texture_get_width(texture);
	if (texture_height <= 0)
		texture_height = gdk_texture_get_height(texture);
	if (texture_width <= 0 || texture_height <= 0)
		return FALSE;
	if (magnification <= 0.)
		return FALSE;
	/* Without a viewport there is no centre to report: the scroll position is
	 * then whatever the adjustments were last told, and dividing that by the
	 * picture size answers 0,0 - the top left corner - for a view that is not
	 * even on screen.  A caller holding that as the position to keep would
	 * remember the corner, so this says "there is no position" instead. */
	if (gtk_adjustment_get_page_size(imageview->priv->hadjustment) <= 1.
		|| gtk_adjustment_get_page_size(imageview->priv->vadjustment) <= 1.)
		return FALSE;

	*center_x = (gtk_adjustment_get_value(imageview->priv->hadjustment)
		+ gtk_adjustment_get_page_size(imageview->priv->hadjustment) / 2.) / magnification / texture_width;
	*center_y = (gtk_adjustment_get_value(imageview->priv->vadjustment)
		+ gtk_adjustment_get_page_size(imageview->priv->vadjustment) / 2.) / magnification / texture_height;

	return TRUE;
}

void
quiver_image_view_set_view_center(QuiverImageView *imageview, gdouble center_x, gdouble center_y)
{
	GdkTexture *texture = imageview->priv->texture;
	GtkAdjustment *hadjustment = imageview->priv->hadjustment;
	GtkAdjustment *vadjustment = imageview->priv->vadjustment;
	gdouble magnification = quiver_image_view_get_magnification(imageview);
	gdouble min_x, max_x, min_y, max_y;
	gdouble x, y;
	gint picture_width;
	gint picture_height;

	if (NULL == texture || NULL == hadjustment || NULL == vadjustment)
		return;
	if (magnification <= 0.)
		return;
	if (gdk_texture_get_width(texture) <= 0 || gdk_texture_get_height(texture) <= 0)
		return;

	/* center_x/center_y are fractions of this picture's width and height, the
	 * same unit quiver_image_view_get_view_center() reports.  The picture is the
	 * one the magnification and the scroll range are in, so it is what the
	 * fraction is of: the texture may be a much smaller decode of it. */
	picture_width = imageview->priv->pixbuf_width;
	picture_height = imageview->priv->pixbuf_height;
	if (picture_width <= 0)
		picture_width = gdk_texture_get_width(texture);
	if (picture_height <= 0)
		picture_height = gdk_texture_get_height(texture);

	/* The scroll range is the range of centres: the middle of the scroll is the
	 * middle of the picture and the ends of it are the picture's edges.  A
	 * picture smaller than the viewport has no range to scroll and so has no
	 * centre to keep either - it is in the middle, which is where an empty
	 * range puts it. */
	min_x = gtk_adjustment_get_lower(hadjustment);
	max_x = MAX (min_x, gtk_adjustment_get_upper(hadjustment) - gtk_adjustment_get_page_size(hadjustment));
	min_y = gtk_adjustment_get_lower(vadjustment);
	max_y = MAX (min_y, gtk_adjustment_get_upper(vadjustment) - gtk_adjustment_get_page_size(vadjustment));

	x = CLAMP (center_x * picture_width * magnification
		- gtk_adjustment_get_page_size(hadjustment) / 2., min_x, max_x);
	y = CLAMP (center_y * picture_height * magnification
		- gtk_adjustment_get_page_size(vadjustment) / 2., min_y, max_y);

	/* A centre, a picture size or a magnification that is not a number puts an
	 * infinity or a NaN into the scroll value, which GTK refuses outright and
	 * which leaves the axis unable to scroll at all - and an axis that cannot
	 * scroll cannot be centred.  Nothing here says where to be in that case, so
	 * it goes to the middle, like a view with nothing to go on. */
	if (!isfinite (x))
		x = (min_x + max_x) / 2.;
	if (!isfinite (y))
		y = (min_y + max_y) / 2.;

	gtk_adjustment_set_value(hadjustment, x);
	gtk_adjustment_set_value(vadjustment, y);

	quiver_image_view_remember_view_center(imageview);
}

void quiver_image_view_get_picture_size(QuiverImageView *imageview, gint *width, gint *height)
{
	if (imageview == NULL || !QUIVER_IS_IMAGE_VIEW(imageview))
	{
		if (width != NULL)
			*width = 0;
		if (height != NULL)
			*height = 0;
		return;
	}
	if (width != NULL)
		*width = imageview->priv->pixbuf_width;
	if (height != NULL)
		*height = imageview->priv->pixbuf_height;
}

void quiver_image_view_get_pixbuf_display_size_for_mode(QuiverImageView *imageview, QuiverImageViewMode mode, gint *width, gint *height)
{ (void)mode; 
	*width = imageview->priv->pixbuf_width;
	*height = imageview->priv->pixbuf_height;
	quiver_image_view_get_pixbuf_display_size_for_mode_alt(imageview,imageview->priv->view_mode,*width, *height,width,height);
}

void quiver_image_view_get_pixbuf_display_size_for_mode_alt(QuiverImageView *imageview, QuiverImageViewMode mode, gint in_width, gint in_height, gint *out_width, gint *out_height)
{
	GtkWidget *widget;
	widget = GTK_WIDGET(imageview);
	
	*out_width = in_width;
	*out_height = in_height;

	switch (mode)
	{
		case QUIVER_IMAGE_VIEW_MODE_ZOOM:
		case QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP:
			{
				gdouble magnification = imageview->priv->magnification;
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

/*
 * this function will invalidate the area of the current pixbuf but not the area 
 * that the new_pixbuf is in
 */


static void quiver_image_view_invalidate_old_image_area(QuiverImageView *imageview, gint new_width, gint new_height)
{
	(void)new_width;
	(void)new_height;
	GtkWidget *widget = GTK_WIDGET(imageview);

	if (gtk_widget_in_destruction(widget) || !gtk_widget_get_mapped(widget))
	{
		return;
	}

	gtk_widget_queue_draw(widget);
}


/* by default , this function will intersect the two rects and only invalidate
 * the area that is in both 
 */
#if HAVE_GDK_PIXBUF
static void quiver_image_view_invalidate_image_area(QuiverImageView *imageview, GdkRectangle *sub_rect)
{
	(void)sub_rect;
	GtkWidget *widget = GTK_WIDGET(imageview);

	if (gtk_widget_in_destruction(widget) || !gtk_widget_get_mapped(widget))
	{
		return;
	}

	gtk_widget_queue_draw(widget);
}
#endif


/* end private functions */

/* start public functions */
GtkWidget *
quiver_image_view_new()
{
	return g_object_new(QUIVER_TYPE_IMAGE_VIEW,NULL);
}

void quiver_image_view_set_smooth_scroll(QuiverImageView *imageview,gboolean smooth_scroll)
{
	imageview->priv->smooth_scroll = smooth_scroll;
	if (!smooth_scroll)
	{
		quiver_image_view_stop_smooth_scroll_slowdown(imageview);
	}
}

gboolean quiver_image_view_get_smooth_scroll(QuiverImageView *imageview)
{
	return imageview->priv->smooth_scroll;
}

GdkTexture* quiver_image_view_get_texture(QuiverImageView *imageview)
{
	return imageview->priv->texture;
}

#if HAVE_GDK_PIXBUF
GdkPixbuf* quiver_image_view_get_pixbuf(QuiverImageView *imageview)
{
	if (NULL != imageview->priv->pixbuf)
		return imageview->priv->pixbuf;

	if (NULL != imageview->priv->texture)
	{
		imageview->priv->pixbuf = gdk_pixbuf_get_from_texture(imageview->priv->texture);
		return imageview->priv->pixbuf;
	}
	return NULL;
}
#endif

void quiver_image_view_set_texture(QuiverImageView *imageview, GdkTexture *texture)
{
	gint width = 0, height = 0;
	if (NULL != texture)
	{
		width = gdk_texture_get_width(texture);
		height = gdk_texture_get_height(texture);
	}
	quiver_image_view_set_texture_at_size(imageview, texture, width, height);
}

void quiver_image_view_set_texture_at_size(QuiverImageView *imageview, GdkTexture *texture, int width, int height)
{
	quiver_image_view_set_texture_at_size_ex(imageview, texture, width, height, TRUE);
}

void quiver_image_view_set_texture_at_size_ex(QuiverImageView *imageview, GdkTexture *texture, int width, int height, gboolean reset_view_mode)
{
	GtkWidget* widget = GTK_WIDGET(imageview);
	gdouble old_mag = quiver_image_view_get_magnification(imageview);

	/* Where the user is looking, in source-image pixels.  This has to be read
	 * while the OLD texture is still installed: once the new one replaces it
	 * the same scroll offset maps to a different point of the image. */
	gdouble center_x = 0.;
	gdouble center_y = 0.;
	gboolean keep_center = FALSE;

	GdkTexture* old_texture = NULL;
	gint old_w = imageview->priv->pixbuf_width;
	gint old_h = imageview->priv->pixbuf_height;

	/* "Keep zoom and pan" is not reset by anything that hands the view another
	 * picture - a new item, a rotation, a reload - because the framing on screen
	 * is what the mode is there to keep and it is the user's position in their
	 * own picture, not a leftover.  So every delivery in that mode is a delivery
	 * that keeps, whether or not the caller asked for a reset. */
	if (reset_view_mode
		&& QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP == imageview->priv->view_mode)
		reset_view_mode = FALSE;

	if (!reset_view_mode)
	{
		keep_center = quiver_image_view_get_view_center(imageview,&center_x,&center_y);
		if (keep_center)
		{
			imageview->priv->has_kept_center = TRUE;
			imageview->priv->kept_center_x = center_x;
			imageview->priv->kept_center_y = center_y;
		}
		else if (imageview->priv->has_kept_center)
		{
			/* The view was blanked while it was off screen - the viewer empties it
			 * before the next item is loaded - so there is no picture to read the
			 * centre off, but the framing being kept is still the framing this item
			 * should come up in.  Otherwise the scroll position the blank left
			 * behind wins, and that is the top left corner. */
			center_x = imageview->priv->kept_center_x;
			center_y = imageview->priv->kept_center_y;
			keep_center = TRUE;
		}
	}
	else
	{
		/* a reset is not a framing being kept */
		imageview->priv->has_kept_center = FALSE;
		imageview->priv->needs_kept_center = FALSE;
	}

	if (imageview->priv->transitions_enabled && reset_view_mode && NULL != imageview->priv->texture)
	{
		old_texture = g_object_ref(imageview->priv->texture);
	}

	quiver_image_view_transition_stop(imageview);
	quiver_image_view_animation_frames_stop(imageview);

	/* Whatever happens to the framing, the old picture's pending work is the
	 * old picture's: a magnification still animating towards its size, a
	 * higher quality scale pass, an animated pixbuf, the transition.  They all
	 * finish against the picture that has just replaced the one they were
	 * started for, so they are torn down here for every delivery. */
	quiver_image_view_prepare_for_new_pixbuf(imageview, width, height);

	if (NULL != old_texture)
	{
		imageview->priv->transition_texture_old = old_texture;
		imageview->priv->transition_old_w = old_w;
		imageview->priv->transition_old_h = old_h;
	}

	if (NULL != texture)
	{
		imageview->priv->texture = g_object_ref(texture);
		if (width <= 0)
			width = gdk_texture_get_width(texture);
		if (height <= 0)
			height = gdk_texture_get_height(texture);
	}
	imageview->priv->pixbuf_width = width;
	imageview->priv->pixbuf_height = height;

	if (reset_view_mode)
	{
		quiver_image_view_reset_view_mode(imageview, FALSE);

		imageview->priv->scroll_draw = FALSE;
		quiver_image_view_update_size(imageview);

		quiver_image_view_set_default_adjustment_values(imageview);

		imageview->priv->scroll_draw = TRUE;
		imageview->priv->needs_recenter = TRUE;
		/* the magnification is not zeroed here: the reset above has just put the
		 * new picture at the size the mode shows it, and zeroing it would leave
		 * a mode that keeps a magnification (1:1) with no magnification at all */
		old_mag = 0;
	}
	else
	{
		quiver_image_view_update_size(imageview);

		if (keep_center)
		{
			quiver_image_view_set_view_center(imageview,center_x,center_y);
			/* A view with no viewport has no page to centre a position in, so the
			 * position is applied as soon as it has one - otherwise it lands as
			 * the empty view's scroll position, which is the top left corner. */
			imageview->priv->needs_kept_center = TRUE;
		}
		else
		{
			/* Nothing to keep and nothing to go back to: the scroll position is
			 * whatever the last picture - or a blank view - left behind, which
			 * is the top left corner, so put the picture in the middle. */
			quiver_image_view_set_default_adjustment_values(imageview);
		}
		/* the centre this picture was given is the centre, so a later size
		 * change must not re-centre it and throw the pan away */
		imageview->priv->needs_recenter = FALSE;
	}

	imageview->priv->magnification = quiver_image_view_get_magnification(imageview);
	if (old_mag != imageview->priv->magnification)
	{
		g_signal_emit(imageview, imageview_signals[SIGNAL_MAGNIFICATION_CHANGED], 0);
	}

	if (1 == gtk_widget_get_width(widget) || 1 == gtk_widget_get_height(widget))
		return;

	if (imageview->priv->transitions_enabled && reset_view_mode && NULL != imageview->priv->transition_texture_old)
	{
		quiver_image_view_transition_start(imageview);
	}
	else
	{
		gtk_widget_queue_draw(widget);
	}
}

#if HAVE_GDK_PIXBUF
void quiver_image_view_set_pixbuf(QuiverImageView *imageview, GdkPixbuf *pixbuf)
{
	gint width = 0, height = 0;
	if (NULL != pixbuf)
	{
		width = gdk_pixbuf_get_width(pixbuf);
		height = gdk_pixbuf_get_height(pixbuf);
	}
	quiver_image_view_set_pixbuf_at_size(imageview, pixbuf, width, height);
}

void quiver_image_view_set_pixbuf_at_size(QuiverImageView *imageview, GdkPixbuf *pixbuf, int width, int height)
{
	quiver_image_view_set_pixbuf_at_size_ex(imageview, pixbuf, width, height, TRUE);
}

void quiver_image_view_set_pixbuf_at_size_ex(QuiverImageView *imageview, GdkPixbuf *pixbuf, int width, int height, gboolean reset_view_mode)
{
	GdkTexture *texture = NULL;
	if (NULL != pixbuf)
	{
		texture = quiver_pixbuf_to_texture(pixbuf);
	}

	quiver_image_view_set_texture_at_size_ex(imageview, texture, width, height, reset_view_mode);

	if (NULL != imageview->priv->pixbuf)
	{
		g_object_unref(imageview->priv->pixbuf);
		imageview->priv->pixbuf = NULL;
	}
	if (NULL != pixbuf)
	{
		imageview->priv->pixbuf = g_object_ref(pixbuf);
	}

	if (NULL != texture)
	{
		g_object_unref(texture);
	}
}
#endif

void quiver_image_view_set_animation_frames(QuiverImageView *imageview,
                                            GdkTexture **frames, gint *delays_ms,
                                            gsize n_frames, int width, int height,
                                            gboolean reset_view_mode)
{
	GtkWidget* widget = GTK_WIDGET(imageview);

	/* Route through the still-image setter so view mode, sizing, adjustments
	 * and transitions behave exactly as they do for a still image; that also
	 * stops any animation that is currently playing. */
	quiver_image_view_set_texture_at_size_ex(imageview,
		(frames != NULL && n_frames > 0) ? frames[0] : NULL, width, height, reset_view_mode);

	if (NULL == frames || n_frames < 2)
		return;

	imageview->priv->animation_n_frames = n_frames;
	imageview->priv->animation_frames = g_new0(GdkTexture*, n_frames);
	imageview->priv->animation_delays = g_new0(gint, n_frames);
	for (gsize i = 0; i < n_frames; i++)
	{
		imageview->priv->animation_frames[i] = (NULL != frames[i]) ? g_object_ref(frames[i]) : NULL;
		imageview->priv->animation_delays[i] = (NULL != delays_ms) ? delays_ms[i] : 100;
	}
	imageview->priv->animation_frame_index = 0;

	if (1 == gtk_widget_get_width(widget) || 1 == gtk_widget_get_height(widget))
		return;

	quiver_image_view_animation_frames_start(imageview);
}

void quiver_image_view_reset_view_mode(QuiverImageView *imageview,gboolean invalidate)
{
	/* Zooming drops the view into 1:1, and that is a framing the pointer asked
	 * for rather than a mode anybody chose out of the menu, so it does not
	 * outlive the picture it was made on: a new picture, or a new list, comes up
	 * in the mode the view was in before the zoom.  That is the mode the HUD,
	 * the preference and "keep zoom and pan" have been naming all along - they
	 * all read the unmagnified mode - so restoring it here is what puts the
	 * three back in step with what is on screen.
	 *
	 * "Keep zoom and pan" is the exception, and the user opted into it: it is
	 * there to carry the framing over, so it stays put and the framing is kept
	 * rather than put back where the mode would have shown it whole.
	 */
	if (QUIVER_IMAGE_VIEW_MODE_ZOOM == imageview->priv->view_mode)
	{
		quiver_image_view_set_view_mode_full(imageview,
			imageview->priv->view_mode_last, invalidate);
		return;
	}

	/* Otherwise a reset is "put the picture back where the mode in force shows
	 * it whole", not "pick some other mode": the mode is the viewer's, and it is
	 * what decides how the *next* picture is shown. */
	const QuiverImageViewMode mode = imageview->priv->view_mode;
	gdouble mag = 0.;
	gint natural_w = 0;
	gint natural_h = 0;

	switch (mode)
	{
		case QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW:
		case QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW_STRETCH:
		case QUIVER_IMAGE_VIEW_MODE_FILL_SCREEN:
			if (imageview->priv->pixbuf_width <= 0 || imageview->priv->pixbuf_height <= 0)
				return;
			/* the size this mode shows the picture at, which is the picture
			 * scaled to the window: the same computation the mode itself makes,
			 * so the reset lands on the framing the mode would have shown */
			natural_w = imageview->priv->pixbuf_width;
			natural_h = imageview->priv->pixbuf_height;
			quiver_image_view_get_pixbuf_display_size_for_mode_alt(imageview, mode,
				natural_w, natural_h, &natural_w, &natural_h);
			if (natural_w <= 0)
				return;
			mag = (gdouble)natural_w / (gdouble)imageview->priv->pixbuf_width;
			break;
		default:
			/* actual size has no zoom to undo, and "keep zoom and pan" keeps its
			 * framing instead of having it reset */
			return;
	}

	quiver_image_view_set_framing(imageview, mag, 0.5, 0.5);
	if (invalidate)
		gtk_widget_queue_draw(GTK_WIDGET(imageview));
}

QuiverImageViewMode quiver_image_view_get_view_mode(QuiverImageView *imageview)
{
	return imageview->priv->view_mode;
}

QuiverImageViewMode quiver_image_view_get_view_mode_unmagnified(QuiverImageView *imageview)
{
	if (quiver_image_view_mode_is_zoomed(imageview->priv->view_mode))
	{
		return imageview->priv->view_mode_last;
	}

	return imageview->priv->view_mode;
}

void quiver_image_view_set_view_mode(QuiverImageView *imageview,QuiverImageViewMode mode)
{
	quiver_image_view_set_view_mode_full(imageview,mode,TRUE);
}


static void quiver_image_view_set_view_mode_full(QuiverImageView *imageview,QuiverImageViewMode mode,gboolean invalidate)
{
	GtkWidget *widget;
	QuiverImageViewMode old_mode;
	
	gdouble old_mag = imageview->priv->magnification;
	
	if (!quiver_image_view_mode_is_zoomed(imageview->priv->view_mode)
		&& quiver_image_view_mode_is_zoomed(mode))
	{
		imageview->priv->magnification = quiver_image_view_get_magnification(imageview);
		imageview->priv->view_mode_last = imageview->priv->view_mode;
	}
	
	if (quiver_image_view_mode_is_zoomed(imageview->priv->view_mode)
		&& !quiver_image_view_mode_is_zoomed(mode))
	{	
		if (0 != imageview->priv->magnification_timeout_id)
		{
			g_source_remove(imageview->priv->magnification_timeout_id);
			imageview->priv->magnification_timeout_id = 0;		
		}
	}

	widget = GTK_WIDGET(imageview);

	gint ow,oh,nw,nh;
	
	quiver_image_view_get_pixbuf_display_size(imageview,&ow, &oh);

	// when switching to zoom view mode, we need to set 
	// the current magnification the magnification to be 
	// that of the current view mode
	imageview->priv->magnification = quiver_image_view_get_magnification(imageview);
	old_mode = imageview->priv->view_mode;
	imageview->priv->view_mode = mode;
	if (old_mode != mode)
	{
		g_signal_emit(imageview,imageview_signals[SIGNAL_VIEW_MODE_CHANGED],0);
	}


	// for the case that we are switching away from zoom mode, we must 
	// set the magnification to the correct magnification for the current mode.
	// if we are switching to zoom mode, this essentially does nothing
	imageview->priv->magnification = quiver_image_view_get_magnification(imageview);

	if (old_mag != imageview->priv->magnification)
	{
		// emit a magnification changed signal
		g_signal_emit(imageview,imageview_signals[SIGNAL_MAGNIFICATION_CHANGED],0);
	}


	if (!invalidate)
	{
		return;
	}

	quiver_image_view_get_pixbuf_display_size(imageview,&nw, &nh);

	if (ow != nw || oh != nh)
	{

		imageview->priv->scroll_draw   = FALSE;
		quiver_image_view_update_size(imageview);
		quiver_image_view_set_default_adjustment_values(imageview);
		imageview->priv->scroll_draw   = TRUE;
		imageview->priv->needs_recenter = TRUE;

		quiver_image_view_transition_stop(imageview);

		quiver_image_view_create_scaled_pixbuf(imageview,GDK_INTERP_BILINEAR);
		if ( gtk_widget_get_mapped (widget) )
		{
			//FIXME: probably dont need to invalidate whole image area
			gtk_widget_queue_draw(widget);
		}
	}
}

gboolean quiver_image_view_get_enable_transitions(QuiverImageView *imageview)
{
	return imageview->priv->transitions_enabled;
}

void quiver_image_view_set_enable_transitions(QuiverImageView *imageview,gboolean enable)
{
	imageview->priv->transitions_enabled = enable;
}

gboolean quiver_image_view_is_in_transition(QuiverImageView *imageview)
{
	return imageview->priv->in_transition;
}

gdouble quiver_image_view_get_magnification(QuiverImageView *imageview)
{
	gdouble magnification;

	gint display_width,display_height;

	switch (imageview->priv->view_mode)
	{
		case QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW:
		case QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW_STRETCH:
		case QUIVER_IMAGE_VIEW_MODE_FILL_SCREEN:
			magnification = 1.;
			if (NULL != imageview->priv->texture
#if HAVE_GDK_PIXBUF
			    || NULL != imageview->priv->pixbuf
#endif
			)
			{
				quiver_image_view_get_pixbuf_display_size(imageview,&display_width,&display_height);
				/* A fit of a picture whose size is not known yet is no
				 * magnification at all, and 0/0 is not one to report: the view
				 * stays at actual size until there is a size to fit. */
				/* A fit of a picture whose size is not known yet is no
				 * magnification at all, and 0/0 is not one to report: the view
				 * stays at actual size until there is a size to fit. */
				magnification = imageview->priv->pixbuf_width > 0
					? display_width / (gdouble)imageview->priv->pixbuf_width
					: 1.;
			}
			break;
		case QUIVER_IMAGE_VIEW_MODE_ZOOM:
		case QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP:
			if (0 != imageview->priv->magnification_timeout_id)
			{
				magnification = imageview->priv->magnification_final;
			}
			else
			{
				magnification = imageview->priv->magnification;
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
		if (QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW == imageview->priv->view_mode)
		{
			return FALSE;
		}
		/* Can zoom out while above the fit-to-window floor (mirrors the
		 * clamp in quiver_image_view_clamp_magnification). */
		gdouble wnd_w = (gdouble)gtk_widget_get_width(GTK_WIDGET(imageview));
		gdouble wnd_h = (gdouble)gtk_widget_get_height(GTK_WIDGET(imageview));
		gdouble min_mag = 0.;
		if (wnd_w > 0. && wnd_h > 0. && imageview->priv->pixbuf_width > 0 && imageview->priv->pixbuf_height > 0)
		{
			gdouble fit_scale = MIN(wnd_w / imageview->priv->pixbuf_width,
				wnd_h / imageview->priv->pixbuf_height);
			min_mag = MIN(fit_scale, 1.0);
		}
		can_magnify = (mag > min_mag + 0.005);
	}
		
	return can_magnify;
}

/* Clamp a magnification level to the allowed zoom range: at most
 * QUIVER_IMAGE_VIEW_MAG_MAX (when zooming in) and at least the "fit to
 * window" magnification (when zooming out), so the image never shrinks
 * smaller than it fits in the window -- mirroring the video zoom-out clamp.
 * QUIVER_IMAGE_VIEW_MIN_IMAGE_SIZE remains as an absolute floor.  Shared by
 * every code path that applies a zoom so the limit is always enforced (the
 * old per-call clamps left set_magnification_full, and thus wheel/menu
 * zooming, clamped only on one path). */
static gdouble
quiver_image_view_clamp_magnification(QuiverImageView *imageview, gdouble new_mag)
{
	if (QUIVER_IMAGE_VIEW_MAG_MAX < new_mag)
	{
		new_mag = QUIVER_IMAGE_VIEW_MAG_MAX;
	}

	if (1 > new_mag)
	{
		/* Mirror the video zoom-out clamp: the low floor is the "fit to
		 * window" magnification, so the user can zoom out to fit but never
		 * below actual size (1.0), which keeps the image from shrinking
		 * smaller than it fits in the window.  MIN_IMAGE_SIZE below remains
		 * as an absolute floor. */
		gdouble wnd_w = (gdouble)gtk_widget_get_width(GTK_WIDGET(imageview));
		gdouble wnd_h = (gdouble)gtk_widget_get_height(GTK_WIDGET(imageview));
		if (wnd_w > 0. && wnd_h > 0.)
		{
			gdouble fit_scale = MIN(wnd_w / imageview->priv->pixbuf_width,
				wnd_h / imageview->priv->pixbuf_height);
			gdouble min_mag = MIN(fit_scale, 1.0);
			if (min_mag > 0.)
				new_mag = MAX(new_mag, min_mag);
		}

		gdouble new_w = imageview->priv->pixbuf_width * new_mag;
		gdouble new_h = imageview->priv->pixbuf_height * new_mag;
		if (QUIVER_IMAGE_VIEW_MIN_IMAGE_SIZE > imageview->priv->pixbuf_width &&
			QUIVER_IMAGE_VIEW_MIN_IMAGE_SIZE > imageview->priv->pixbuf_height)
		{
			new_mag = 1.;
		}
		else if (QUIVER_IMAGE_VIEW_MIN_IMAGE_SIZE > new_w &&
			QUIVER_IMAGE_VIEW_MIN_IMAGE_SIZE > new_h)
		{
			gdouble mag_w = (gdouble)QUIVER_IMAGE_VIEW_MIN_IMAGE_SIZE / imageview->priv->pixbuf_width;
			gdouble mag_h = (gdouble)QUIVER_IMAGE_VIEW_MIN_IMAGE_SIZE / imageview->priv->pixbuf_height;
			new_mag = mag_w;
			if (mag_h < mag_w)
			{
				new_mag = mag_h;
			}
		}
	}

	return new_mag;
}

void quiver_image_view_set_magnification(QuiverImageView *imageview,gdouble new_mag)
{
	/* A magnification the widget cannot draw is not a zoom it can apply: a
	 * non-finite one - a 0/0 with no picture size known yet - would put NaN into
	 * the display size and from there into the scroll ranges, which GTK rejects
	 * and which the view then cannot scroll at all. */
	/* A magnification the widget cannot draw is not a zoom it can apply: a
	 * non-finite one - a 0/0 with no picture size known yet - would put NaN into
	 * the display size and from there into the scroll ranges, which GTK rejects
	 * and which the view then cannot scroll at all. */
	if (!isfinite(new_mag) || new_mag <= 0.)
		return;
	/* restrict the zoom amount */
	new_mag = quiver_image_view_clamp_magnification(imageview, new_mag);
	
	if (QUIVER_IMAGE_VIEW_MAGNIFICATION_MODE_SMOOTH == imageview->priv->magnification_mode)
	{
		imageview->priv->magnification_final = new_mag;
		quiver_image_view_add_magnification_timeout(imageview);
	}
	else
	{
		quiver_image_view_set_magnification_full(imageview,new_mag);
		imageview->priv->zoom_anchor_center = FALSE;
	}
}

/* Put the view at a magnification and a centre (both as fractions of the
 * picture, from quiver_image_view_get_view_center()) in one go, without the
 * smooth zoom.
 *
 * A zoom that eases towards its target passes through magnifications the
 * picture cannot be panned in yet - a magnification below the fit has no
 * scroll range at all, so the centre falls back to the middle of the picture
 * and stays there when the zoom gets big enough to pan again.  Whatever centre
 * was asked for is therefore gone by the time the animation lands, which is
 * how a framing that was set to a corner arrives in the middle.  Setting both at
 * once, immediately, has no such intermediate state to lose it in. */
void quiver_image_view_set_framing(QuiverImageView *imageview, gdouble magnification,
	gdouble center_x, gdouble center_y)
{
	g_return_if_fail(QUIVER_IS_IMAGE_VIEW(imageview));

	const QuiverImageViewMagnificationMode mode = imageview->priv->magnification_mode;
	quiver_image_view_transition_stop(imageview);
	/* an animation already on its way would land after this and undo it */
	if (0 != imageview->priv->magnification_timeout_id)
	{
		g_source_remove(imageview->priv->magnification_timeout_id);
		imageview->priv->magnification_timeout_id = 0;
	}
	imageview->priv->magnification_mode = QUIVER_IMAGE_VIEW_MAGNIFICATION_MODE_DEFAULT;
	quiver_image_view_set_zoom_anchor_center(imageview, TRUE);
	quiver_image_view_set_magnification(imageview, magnification);
	quiver_image_view_set_view_center(imageview, center_x, center_y);
	quiver_image_view_set_zoom_anchor_center(imageview, FALSE);
	imageview->priv->magnification_mode = mode;
}

void quiver_image_view_set_zoom_anchor_center(QuiverImageView *imageview, gboolean anchor_center)
{
	g_return_if_fail(QUIVER_IS_IMAGE_VIEW(imageview));
	imageview->priv->zoom_anchor_center = anchor_center;
}

gboolean quiver_image_view_get_zoom_anchor_center(QuiverImageView *imageview)
{
	g_return_val_if_fail(QUIVER_IS_IMAGE_VIEW(imageview), FALSE);
	return imageview->priv->zoom_anchor_center;
}

/* Apply a multiplicative zoom factor immediately (no smooth animation).
 * Used for continuous wheel/touchpad zooming so the image tracks the input
 * without easing on after the scroll stops.  Is clamped to the allowed range;
 * returns the factor actually applied (may differ if a limit was hit). */
gdouble quiver_image_view_zoom_by(QuiverImageView *imageview,gdouble factor)
{
	imageview->priv->zoom_anchor_center = FALSE;
	gdouble new_mag = quiver_image_view_clamp_magnification(imageview,
		imageview->priv->magnification * factor);
	quiver_image_view_set_magnification_full(imageview,new_mag);
	return factor;
}

static void quiver_image_view_set_magnification_full(QuiverImageView *imageview,gdouble new_mag)
{
	new_mag = quiver_image_view_clamp_magnification(imageview, new_mag);

	GtkWidget *widget;
	gint old_width,old_height, new_width,new_height;
	gint x = 0, y = 0;
	gdouble old_hadjust,old_vadjust,new_hadjust,new_vadjust;
	gdouble old_mag;

	quiver_image_view_get_pixbuf_display_size(imageview,&old_width,&old_height);

	old_mag = imageview->priv->magnification;

	/* The anchor is kept below by scaling the offset by the ratio of the old
	 * magnification to the new one, and a magnification of zero has no ratio to
	 * give: there is no previous zoom for the pointer's position to be a fraction
	 * of, and dividing by it puts an infinity - and then a NaN - into the scroll
	 * value, which GTK refuses and which the view then cannot scroll at all.
	 * With nothing to scale the position by, the zoom is about the middle, which
	 * is what a view with no zoom behind it can do. */
	if (!isfinite(old_mag) || old_mag <= 0.)
		old_mag = new_mag;

	if (old_mag < new_mag)
	{
		quiver_image_view_send_reload_event(imageview);
	}
	
	
	imageview->priv->magnification = new_mag;

	if (0 == imageview->priv->magnification_timeout_id
	    && old_mag != new_mag)
	{
		// emit a magnification changed signal
		g_signal_emit(imageview,imageview_signals[SIGNAL_MAGNIFICATION_CHANGED],0);
	}

	widget = GTK_WIDGET(imageview);

	/* Zoom anchor: use the pointer's widget-local position so zooming stays
	 * centered on the cursor.  When zoom_anchor_center is set (e.g. clicking
	 * HUD zoom buttons or zooming via actions), zoom about the center instead. */
	if (imageview->priv->zoom_anchor_center)
	{
		x = -1;
		y = -1;
	}
	else
	{
		gint wwd = gtk_widget_get_width(widget);
		gint wht = gtk_widget_get_height(widget);
		GtkWidget *toplevel = GTK_WIDGET(gtk_widget_get_root(widget));
		if (toplevel != NULL && gtk_widget_get_realized(toplevel))
		{
			GtkNative *native = gtk_widget_get_native(toplevel);
			if (native != NULL)
			{
				GdkSurface *surface = gtk_native_get_surface(native);
				if (surface != NULL)
				{
					GdkDisplay *display = gdk_surface_get_display(surface);
					if (display != NULL)
					{
						GdkSeat *seat = gdk_display_get_default_seat(display);
						if (seat != NULL)
						{
							GdkDevice *device = gdk_seat_get_pointer(seat);
							if (device != NULL)
							{
								double px = 0, py = 0;
								GdkModifierType mask = 0;
								double sx = 0, sy = 0;
								if (gdk_surface_get_device_position(surface, device, &px, &py, &mask))
								{
									gtk_native_get_surface_transform(native, &sx, &sy);
									graphene_point_t pt_native = GRAPHENE_POINT_INIT(
										(float)(px - sx), (float)(py - sy));
									graphene_point_t pt_widget;
									if (gtk_widget_compute_point(GTK_WIDGET(native), widget,
											&pt_native, &pt_widget))
									{
										x = (gint)pt_widget.x;
										y = (gint)pt_widget.y;
									}
								}
							}
						}
					}
				}
			}
		}

		/* keep the anchor within the widget; an out-of-area position (e.g.
		 * pointer over a sibling) must not drive centering off the viewport */
		if (x < 0 || x > wwd || y < 0 || y > wht)
		{
			x = -1;
			y = -1;
		}
		else
		{
			x = MIN(x, wwd);
			y = MIN(y, wht);
		}
	}

	old_hadjust = gtk_adjustment_get_value(imageview->priv->hadjustment);
	old_vadjust = gtk_adjustment_get_value(imageview->priv->vadjustment);


	// update size must be done befor the code that comes after it
	imageview->priv->scroll_draw   = FALSE;

	quiver_image_view_update_size(imageview);

	gint old_hpage_size = gtk_adjustment_get_page_size(imageview->priv->hadjustment);
	gint old_vpage_size = gtk_adjustment_get_page_size(imageview->priv->vadjustment);

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

	// we need to do several things:
	// update the scrollbar adjustments, update the scaled pixbuf, and invalidate
	quiver_image_view_get_pixbuf_display_size(imageview,&new_width,&new_height);

	if (new_width > gtk_widget_get_width(widget))
	{
		// we will set the adjustment based on the pointer position.
		// if the pointer is not within the widget area, use middle!
		if (0 < x && x <= gtk_widget_get_width(widget) && 0 < y && y <= gtk_widget_get_height(widget))
		{
			//set x as the next center point
			//new_hadjust = (old_hadjust + x) * (new_mag/old_mag) - gtk_adjustment_get_page_size(imageview->priv->hadjustment)/2.;
			new_hadjust = (old_hadjust + x) * (new_mag/old_mag) - x;
		}
		else
		{
			// set the center as the next centerpoint
			new_hadjust = (old_hadjust + old_hpage_size/2.) * (new_mag/old_mag) - gtk_adjustment_get_page_size(imageview->priv->hadjustment)/2.;
		}
		if (new_hadjust > gtk_adjustment_get_upper(imageview->priv->hadjustment) - gtk_adjustment_get_page_size(imageview->priv->hadjustment))
			new_hadjust = MAX (0, gtk_adjustment_get_upper(imageview->priv->hadjustment) - gtk_adjustment_get_page_size(imageview->priv->hadjustment));
		if (0 > new_hadjust)
			new_hadjust = 0;

		//printf("old new h: %d %d\n",(gint)old_hadjust,(gint)new_hadjust);
		gtk_adjustment_set_value(imageview->priv->hadjustment,new_hadjust);
	}
	else
	{
		gtk_adjustment_set_value(imageview->priv->hadjustment,0);
	}

	if (new_height > gtk_widget_get_height(widget))
	{
		// we will set the adjustment based on the pointer position.
		// if the pointer is not within the widget area, use middle!
		if (0 < x && x <= gtk_widget_get_width(widget) && 0 < y && y <= gtk_widget_get_height(widget))
		{
			//set x as the next center point
			//new_vadjust = (old_vadjust + y) * (new_mag/old_mag) - gtk_adjustment_get_page_size(imageview->priv->vadjustment)/2.;
			new_vadjust = (old_vadjust + y) * (new_mag/old_mag) -y;
		}
		else
		{
			// set the center as the next centerpoint
			//printf("old new v: %d %d\n",(gint)old_vadjust,(gint)new_vadjust);
			//printf("old page: %d\n",(gint)old_vpage_size);
			new_vadjust = (old_vadjust + old_vpage_size/2.) * (new_mag/old_mag) - gtk_adjustment_get_page_size(imageview->priv->vadjustment)/2.;
			//printf("old new v: %d %d\n",(gint)old_vadjust,(gint)new_vadjust);
		}
		if (new_vadjust > gtk_adjustment_get_upper(imageview->priv->vadjustment) - gtk_adjustment_get_page_size(imageview->priv->vadjustment))
			new_vadjust = MAX (0, gtk_adjustment_get_upper(imageview->priv->vadjustment) - gtk_adjustment_get_page_size(imageview->priv->vadjustment));
		if (0 > new_vadjust)
			new_vadjust = 0;

		gtk_adjustment_set_value(imageview->priv->vadjustment,new_vadjust);
	}
	else
	{
		gtk_adjustment_set_value(imageview->priv->vadjustment,0);
	}
	quiver_image_view_transition_stop(imageview);

	quiver_image_view_create_scaled_pixbuf(imageview,GDK_INTERP_NEAREST);
	quiver_image_view_add_scale_hq_timeout(imageview);
	imageview->priv->scroll_draw   = TRUE;

	if (gtk_widget_get_mapped(widget))
	{
		gtk_widget_queue_draw(widget);
	}
}

void quiver_image_view_set_magnification_mode(QuiverImageView *imageview,QuiverImageViewMagnificationMode mode)
{
	imageview->priv->magnification_mode = mode;	
}


void quiver_image_view_rotate(QuiverImageView *imageview, gboolean clockwise)
{
	GdkTexture *current_texture = quiver_image_view_get_texture(imageview);
	if (NULL != current_texture)
	{
		int src_w = gdk_texture_get_width(current_texture);
		int src_h = gdk_texture_get_height(current_texture);
		int dst_w = src_h;
		int dst_h = src_w;
		gsize src_stride = (gsize)src_w * 4;
		gsize dst_stride = (gsize)dst_w * 4;
		guint32 *src_buf = (guint32*)g_malloc(src_stride * src_h);
		guint32 *dst_buf = (guint32*)g_malloc(dst_stride * dst_h);
		GdkTextureDownloader *dl = gdk_texture_downloader_new(current_texture);
		gdk_texture_downloader_set_format(dl, GDK_MEMORY_R8G8B8A8);
		gdk_texture_downloader_download_into(dl, (guchar*)src_buf, src_stride);
		gdk_texture_downloader_free(dl);

		for (int y = 0; y < dst_h; ++y)
		{
			for (int x = 0; x < dst_w; ++x)
			{
				int sx, sy;
				if (clockwise)
				{
					sx = y;
					sy = src_h - 1 - x;
				}
				else
				{
					sx = src_w - 1 - y;
					sy = x;
				}
				if (sx >= 0 && sx < src_w && sy >= 0 && sy < src_h)
					dst_buf[y * dst_w + x] = src_buf[sy * src_w + sx];
			}
		}
		g_free(src_buf);
		GBytes *bytes = g_bytes_new_take(dst_buf, dst_stride * dst_h);
		GdkTexture *rotated = gdk_memory_texture_new(dst_w, dst_h, GDK_MEMORY_R8G8B8A8, bytes, dst_stride);
		g_bytes_unref(bytes);
		quiver_image_view_set_texture_at_size(imageview, rotated, imageview->priv->pixbuf_height, imageview->priv->pixbuf_width);
		g_object_unref(rotated);
		return;
	}

#if HAVE_GDK_PIXBUF
	GdkPixbuf * pixbuf_rotated = NULL;
	GdkPixbuf * current_pixbuf = quiver_image_view_get_pixbuf(imageview);
	
	if (NULL == current_pixbuf)
		return;

	if (clockwise)
	{
		pixbuf_rotated = gdk_pixbuf_rotate_simple(current_pixbuf, GDK_PIXBUF_ROTATE_CLOCKWISE);
	}
	else
	{
		pixbuf_rotated = gdk_pixbuf_rotate_simple(current_pixbuf, GDK_PIXBUF_ROTATE_COUNTERCLOCKWISE);
	}
	if (NULL != pixbuf_rotated)
	{
		quiver_image_view_set_pixbuf_at_size(imageview, pixbuf_rotated, imageview->priv->pixbuf_height, imageview->priv->pixbuf_width);
		g_object_unref(pixbuf_rotated);
	}
#endif
}

void quiver_image_view_flip(QuiverImageView *imageview, gboolean horizontal)
{
	GdkTexture *current_texture = quiver_image_view_get_texture(imageview);
	if (NULL != current_texture)
	{
		int src_w = gdk_texture_get_width(current_texture);
		int src_h = gdk_texture_get_height(current_texture);
		gsize stride = (gsize)src_w * 4;
		guint32 *src_buf = (guint32*)g_malloc(stride * src_h);
		guint32 *dst_buf = (guint32*)g_malloc(stride * src_h);
		GdkTextureDownloader *dl = gdk_texture_downloader_new(current_texture);
		gdk_texture_downloader_set_format(dl, GDK_MEMORY_R8G8B8A8);
		gdk_texture_downloader_download_into(dl, (guchar*)src_buf, stride);
		gdk_texture_downloader_free(dl);

		for (int y = 0; y < src_h; ++y)
		{
			for (int x = 0; x < src_w; ++x)
			{
				int sx = horizontal ? (src_w - 1 - x) : x;
				int sy = horizontal ? y : (src_h - 1 - y);
				dst_buf[y * src_w + x] = src_buf[sy * src_w + sx];
			}
		}
		g_free(src_buf);
		GBytes *bytes = g_bytes_new_take(dst_buf, stride * src_h);
		GdkTexture *flipped = gdk_memory_texture_new(src_w, src_h, GDK_MEMORY_R8G8B8A8, bytes, stride);
		g_bytes_unref(bytes);
		quiver_image_view_set_texture_at_size(imageview, flipped, imageview->priv->pixbuf_width, imageview->priv->pixbuf_height);
		g_object_unref(flipped);
		return;
	}

#if HAVE_GDK_PIXBUF
	GdkPixbuf * pixbuf_flipped = NULL;
	GdkPixbuf * current_pixbuf = quiver_image_view_get_pixbuf(imageview);

	if (NULL == current_pixbuf)
		return;

	if (horizontal)
	{
		pixbuf_flipped = gdk_pixbuf_flip(current_pixbuf, TRUE);
	}
	else
	{
		pixbuf_flipped = gdk_pixbuf_flip(current_pixbuf, FALSE);
	}
	
	if (NULL != pixbuf_flipped)
	{
		quiver_image_view_set_pixbuf_at_size(imageview, pixbuf_flipped, imageview->priv->pixbuf_width, imageview->priv->pixbuf_height);
		g_object_unref(pixbuf_flipped);
	}
#endif
}

#if HAVE_GDK_PIXBUF
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
#endif

GtkAdjustment * quiver_image_view_get_hadjustment(QuiverImageView *imageview)
{
	return imageview->priv->hadjustment;
}
GtkAdjustment * quiver_image_view_get_vadjustment(QuiverImageView *imageview)
{
	return imageview->priv->vadjustment;	
}

void quiver_image_view_activate(QuiverImageView *imageview)
{
	g_signal_emit(imageview,imageview_signals[SIGNAL_ACTIVATED],0);
}

/* end public functions */
static void quiver_image_view_prepare_for_new_pixbuf(QuiverImageView *imageview, gint new_width, gint new_height)
{
	quiver_image_view_transition_stop(imageview);
	
	if (0 != imageview->priv->magnification_timeout_id)
	{
		/* A magnification on its way somewhere is cancelled here, but the size it
		 * was on its way to is what the user asked for and so is what the next
		 * picture is shown at - not wherever the animation had got to when the
		 * picture changed underneath it. */
		imageview->priv->magnification = imageview->priv->magnification_final;
		g_source_remove(imageview->priv->magnification_timeout_id);
		imageview->priv->magnification_timeout_id = 0;		
	}
	
	if (0 != imageview->priv->timeout_scale_hq_id)
	{
		g_source_remove(imageview->priv->timeout_scale_hq_id);
		imageview->priv->timeout_scale_hq_id = 0;
	}
	
	if (0 != imageview->priv->scroll_timeout_id)
	{
		g_source_remove(imageview->priv->scroll_timeout_id);
		imageview->priv->scroll_timeout_id = 0;
	}
	
	if (0 != imageview->priv->transition_timeout_id)
	{
		g_source_remove(imageview->priv->transition_timeout_id);
		imageview->priv->transition_timeout_id = 0;
	}

	if (0 != imageview->priv->animation_timeout_id)
	{	
		g_source_remove(imageview->priv->animation_timeout_id);
		imageview->priv->animation_timeout_id = 0;
	}

	quiver_image_view_animation_frames_stop(imageview);
	
#if HAVE_GDK_PIXBUF
	if (NULL != imageview->priv->pixbuf_animation_iter)
	{
		g_object_unref(imageview->priv->pixbuf_animation_iter);
		imageview->priv->pixbuf_animation_iter = NULL;
	}

	if (NULL != imageview->priv->pixbuf_animation)
	{
		g_object_unref(imageview->priv->pixbuf_animation);
		imageview->priv->pixbuf_animation = NULL;
	}

	if (NULL != imageview->priv->pixbuf_scaled)
	{
		g_object_unref(imageview->priv->pixbuf_scaled);
		imageview->priv->pixbuf_scaled = NULL;
	}
#endif

	if (NULL != imageview->priv->texture)
	{
		if (!imageview->priv->transitions_enabled)
		{
			quiver_image_view_invalidate_old_image_area(imageview,new_width,new_height);
		}
		g_object_unref(imageview->priv->texture);
		imageview->priv->texture = NULL;
	}

#if HAVE_GDK_PIXBUF
	if (NULL != imageview->priv->pixbuf)
	{
		if (!imageview->priv->transitions_enabled)
		{
			quiver_image_view_invalidate_old_image_area(imageview,new_width,new_height);
		}
		g_object_unref(imageview->priv->pixbuf);
		imageview->priv->pixbuf = NULL;
	}
#endif
	
	imageview->priv->reload_event_sent = FALSE;
	imageview->priv->needs_recenter = FALSE;
	
}

#if HAVE_GDK_PIXBUF
static void pixbuf_loader_size_prepared(GdkPixbufLoader *loader,gint width, gint height,gpointer userdata)
{ (void)loader; 
	GtkWidget *widget;
	QuiverImageView *imageview;

	imageview = (QuiverImageView*)userdata;
	
	// FIXME: don't always want to reset the view mode
	// and do we need any of these signals anymore?
	//quiver_image_view_reset_view_mode(imageview,FALSE);

	widget = GTK_WIDGET(imageview);

	imageview->priv->pixbuf_width_next = width;
	imageview->priv->pixbuf_height_next = height;
	if (!gtk_widget_get_mapped(widget))
	{
		return;
	}
}
static void pixbuf_loader_area_prepared(GdkPixbufLoader *loader,gpointer userdata)
{
	QuiverImageView *imageview;
	GdkPixbuf *pixbuf;

	imageview = (QuiverImageView*)userdata;
	GdkPixbufAnimation* pixbuf_animation;

	pixbuf_animation = gdk_pixbuf_loader_get_animation (loader);
	g_object_ref(pixbuf_animation);

	pixbuf = gdk_pixbuf_animation_get_static_image (pixbuf_animation);
	g_object_ref(pixbuf);

	quiver_image_view_prepare_for_new_pixbuf(imageview,	
		imageview->priv->pixbuf_width_next, imageview->priv->pixbuf_height_next);

	imageview->priv->pixbuf_width = imageview->priv->pixbuf_width_next;
	imageview->priv->pixbuf_height = imageview->priv->pixbuf_height_next;
	
	imageview->priv->pixbuf_animation = pixbuf_animation;
	
	imageview->priv->pixbuf = pixbuf;
	if (NULL != imageview->priv->texture)
	{
		g_object_unref(imageview->priv->texture);
		imageview->priv->texture = NULL;
	}
	if (NULL != pixbuf)
	{
		imageview->priv->texture = quiver_pixbuf_to_texture(pixbuf);
	}
	
	quiver_image_view_transition_stop(imageview);

	quiver_image_view_create_scaled_pixbuf(imageview,GDK_INTERP_NEAREST);

	imageview->priv->scroll_draw = FALSE;

	quiver_image_view_update_size(imageview);
	
	quiver_image_view_set_default_adjustment_values(imageview);
	
	imageview->priv->scroll_draw = TRUE;
	
	imageview->priv->needs_recenter = TRUE;

	// FIXME: this makes the image not get invalidated on the close signal
	imageview->priv->area_updated = TRUE;

}
static void pixbuf_loader_area_updated (GdkPixbufLoader *loader,gint x, gint y, gint width,gint height,gpointer userdata)
{ (void)loader; 
	QuiverImageView *imageview;
	GdkRectangle rect;
	GdkPixbufAnimation* pixbuf_animation;
	gint dw,dh,aw,ah;

	imageview = (QuiverImageView*)userdata;
	pixbuf_animation = imageview->priv->pixbuf_animation;

	aw = gdk_pixbuf_get_width(imageview->priv->pixbuf);
	ah = gdk_pixbuf_get_height(imageview->priv->pixbuf);
	
	quiver_image_view_get_pixbuf_display_size(imageview, &dw, &dh);

	if (dw == aw && dh == ah)
	{
		rect.x = x;
		rect.y = y;
		rect.width = width;
		rect.height = height;


		if (0 == imageview->priv->animation_timeout_id)
		{
			if (!gdk_pixbuf_animation_is_static_image(pixbuf_animation))
			{
				quiver_image_view_start_animation(imageview);
				//printf("animation started!\n");
			}
		}
		else
		{
			/*
			g_source_remove(imageview->priv->animation_timeout_id);
			imageview->priv->animation_timeout_id = 0;
			quiver_image_view_timeout_animation(imageview);
			quiver_image_view_add_animation_timeout(imageview);
			*/
		}

		//printf("x,y,w,h: %d %d %d %d\n",x,y,width,height);
		//printf("%d=%d %d=%d\n",dw, width, dh,height);

		quiver_image_view_invalidate_image_area(imageview,&rect);
		imageview->priv->area_updated = TRUE;
	}
	else
	{
		// we are displaying the image at a different size than the loader is returning
		// printf("%d %d %d %d\n",aw, ah, dw,dh);
	}


	//quiver_image_view_create_scaled_pixbuf(imageview,GDK_INTERP_BILINEAR);
	//gdk_window_invalidate_rect(gtk_widget_get_window(widget),&rect,FALSE);

}
static void pixbuf_loader_closed(GdkPixbufLoader *loader,gpointer userdata)
{ (void)loader; 	
	QuiverImageView *imageview;
	GdkPixbufAnimation* pixbuf_animation;

	imageview = (QuiverImageView*)userdata;
	pixbuf_animation = imageview->priv->pixbuf_animation;
	
	if (NULL != pixbuf_animation)
	{
		//printf("animation not null!\n");
		
		if (!imageview->priv->area_updated)
		{
	
			if (gdk_pixbuf_animation_is_static_image(pixbuf_animation))
			{
				quiver_image_view_transition_stop(imageview);

				quiver_image_view_create_scaled_pixbuf(imageview,GDK_INTERP_NEAREST);
				quiver_image_view_invalidate_image_area(imageview,NULL);
				quiver_image_view_add_scale_hq_timeout(imageview);
				//quiver_image_view_create_scaled_pixbuf(imageview,GDK_INTERP_BILINEAR);
				//quiver_image_view_invalidate_image_area(imageview,NULL);
				//quiver_image_view_create_transition_pixbuf(imageview);
				//quiver_image_view_transition_start(imageview);
	
			}
			else
			{
				//printf("->is an animation!\n");
				quiver_image_view_invalidate_image_area(imageview,NULL);
				if (0 == imageview->priv->animation_timeout_id)
					quiver_image_view_start_animation(imageview);
			}
		}
		else
		{
			imageview->priv->area_updated = FALSE;
		}
	}
//	printf("closed\n");

}
#endif
