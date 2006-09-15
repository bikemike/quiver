#include <gtk/gtk.h>

#include <math.h>
#include <string.h>
#include <sys/time.h>

#include "quiver-pixbuf-utils.h"
#include "quiver-image-view.h"
#include "quiver-marshallers.h"

//#include "gtkintl.h"


#define QUIVER_IMAGE_VIEW_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), QUIVER_TYPE_IMAGE_VIEW, QuiverImageViewPrivate))

/* set up some defaults */
#define QUIVER_IMAGE_VIEW_MAG_MAX              50.
#define QUIVER_IMAGE_VIEW_MIN_IMAGE_SIZE       32
#define QUIVER_ICON_VIEW_SCALE_HQ_TIMEOUT      100

#define QUIVER_PARAM_READWRITE G_PARAM_READWRITE|G_PARAM_STATIC_NAME|G_PARAM_STATIC_NICK|G_PARAM_STATIC_BLURB

G_DEFINE_TYPE(QuiverImageView,quiver_image_view,GTK_TYPE_WIDGET);

/* start private data structures */


/* signals */
enum {
	SIGNAL_ACTIVATED,
	SIGNAL_RELOAD,
	SIGNAL_COUNT
};

static guint imageview_signals[SIGNAL_COUNT] = {0};

/* properties */
/* properties */
enum {
   PROP_0,
/*
   PROP_N_ITEMS,
   PROP_ICON_PIXBUF,
   PROP_THUMBNAIL_PIXBUF,
   PROP_TEXT
*/
};


struct _QuiverImageViewPrivate
{
	GdkPixbuf *pixbuf;
	GdkPixbuf *pixbuf_scaled;
	GdkPixbufAnimation *pixbuf_animation;
	GdkPixbufAnimationIter *pixbuf_animation_iter;
	
	gint pixbuf_width;
	gint pixbuf_height;

	// used to keep track of the actual size
	// of the image being loaded
	gint pixbuf_width_next;
	gint pixbuf_height_next;

	QuiverImageViewMode view_mode;
	QuiverImageViewMode view_mode_last;

	QuiverImageViewTransitionType transition_type;
	gdouble transition_percent; // 0 -> 1
	GdkPixbuf *transition_pixbuf_old;
	GdkPixbuf *transition_pixbuf_new;
	guint transition_timeout_id;
	
	QuiverImageViewMagnificationMode magnification_mode;
	
	guint magnification_timeout_id;
	
	gdouble magnification_final;
	gdouble magnification; // magnification level as a percent (1 = 100%)

	guint timeout_scale_hq_id;
	
	GtkAdjustment *hadjustment;
	GtkAdjustment *vadjustment;

	guint scroll_timeout_id;
	gdouble last_hadjustment;
	gdouble last_vadjustment;

	/* has the area been updated as the image is being loaded?*/
	gboolean area_updated;
	guint animation_timeout_id;


	QuiverImageViewMouseMode mouse_move_mode;
	gint mouse_x1, mouse_y1;
	gint mouse_x2, mouse_y2;
	gboolean mouse_move_capture;

	gboolean scroll_draw;

	gboolean rubberband_mode_start;
	gboolean rubberband_mode;
	GdkRectangle rubberband_rect;
	GdkRectangle rubberband_rect_old;

	gboolean smooth_scroll;
	gint smooth_scroll_cell;
	
	gboolean reload_event_sent;

};
/* end private data structures */


/* start private function prototypes */

static void quiver_image_view_realize        (GtkWidget *widget);
static void quiver_image_view_size_allocate  (GtkWidget     *widget,
                                             GtkAllocation *allocation);

static void      quiver_image_view_size_request (GtkWidget *widget, GtkRequisition *requisition);

static void      quiver_image_view_send_configure (QuiverImageView *imageview);

static gboolean  quiver_image_view_configure_event (GtkWidget *widget,
                    GdkEventConfigure *event);
static gboolean  quiver_image_view_expose_event (GtkWidget *imageview,
                    GdkEventExpose *event);
static gboolean  quiver_image_view_button_press_event  (GtkWidget *widget,
                    GdkEventButton *event);

static gboolean  quiver_image_view_button_release_event (GtkWidget *widget,
                    GdkEventButton *event);
static gboolean  quiver_image_view_motion_notify_event (GtkWidget *widget,
                    GdkEventMotion *event);
static gboolean  quiver_image_view_scroll_event ( GtkWidget *widget,
                    GdkEventScroll *event);

static void      quiver_image_view_set_scroll_adjustments (QuiverImageView *imageview,
                    GtkAdjustment *hadjustment,
                    GtkAdjustment *vadjustment);

static void      quiver_image_view_adjustment_value_changed (GtkAdjustment *adjustment,
                    QuiverImageView *imageview);

static void     quiver_image_view_finalize(GObject *object);

/* start utility function prototypes*/
static void quiver_image_view_send_reload_event(QuiverImageView *imageview);
static guint quiver_image_view_get_width(QuiverImageView *imageview);
static guint quiver_image_view_get_height(QuiverImageView *imageview);
static void
quiver_image_view_set_adjustment_upper (GtkAdjustment *adj,
				 gdouble        upper,
				 gboolean       always_emit_changed);



static void quiver_image_view_get_bound_size(guint bound_width,guint bound_height,guint *width,guint *height,gboolean fill_if_smaller);
static void quiver_image_view_add_scale_hq_timeout(QuiverImageView *imageview);
static gboolean quiver_image_view_timeout_scale_hq(gpointer data);
//static void quiver_image_view_create_transition_pixbuf(QuiverImageView *imageview);
static void quiver_image_view_create_scaled_pixbuf(QuiverImageView *imageview,GdkInterpType interptype);

static gboolean quiver_image_view_timeout_scroll(gpointer data);
static void quiver_image_view_add_scroll_timeout(QuiverImageView *imageview);
static void quiver_image_view_scroll(QuiverImageView *imageview);

static void quiver_image_view_start_animation(QuiverImageView *imageview);
static void quiver_image_view_add_animation_timeout(QuiverImageView *imageview);
static gboolean quiver_image_view_timeout_animation(gpointer data);

//static void quiver_image_view_add_transition_timeout(QuiverImageView *imageview);
//static gboolean quiver_image_view_timeout_transition(gpointer data);
//static void quiver_image_view_transition_start(QuiverImageView *imageview);

static void quiver_image_view_set_magnification_full(QuiverImageView *imageview,gdouble new_mag);
static void quiver_image_view_add_magnification_timeout(QuiverImageView *imageview);
static gboolean quiver_image_view_timeout_magnification(gpointer data);
//static void quiver_image_view_magnification_start(QuiverImageView *imageview);

static void quiver_image_view_get_pixbuf_display_size(QuiverImageView *imageview, gint *width, gint *height);
static void quiver_image_view_get_pixbuf_display_size_alt(QuiverImageView *imageview,gint in_width, gint in_height, gint *out_width, gint *out_height);

static void quiver_image_view_invalidate_old_image_area(QuiverImageView *imageview,gint new_width, gint new_height);
static void quiver_image_view_invalidate_image_area(QuiverImageView *imageview,GdkRectangle *rect);

static void quiver_image_view_reset_view_mode(QuiverImageView *imageview,gboolean invalidate);
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

/* end private function prototypes */

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
	widget_class->size_request         = quiver_image_view_size_request;
	widget_class->expose_event         = quiver_image_view_expose_event;
	widget_class->configure_event      = quiver_image_view_configure_event;
	widget_class->button_press_event   = quiver_image_view_button_press_event;
	widget_class->button_release_event = quiver_image_view_button_release_event;
	widget_class->motion_notify_event  = quiver_image_view_motion_notify_event;
	widget_class->scroll_event         = quiver_image_view_scroll_event;
	//widget_class->key_press_event      = quiver_image_view_key_press_event;
	klass->set_scroll_adjustments      = quiver_image_view_set_scroll_adjustments;

	obj_class->finalize                = quiver_image_view_finalize;
	/*
	obj_class->set_property            = quiver_image_view_set_property;
	obj_class->get_property            = quiver_image_view_get_property;
	*/

	widget_class->set_scroll_adjustments_signal =
       g_signal_new (/*FIXME:- commented out*//*I_*/("set_scroll_adjustments"),
	     G_OBJECT_CLASS_TYPE (obj_class),
	     G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
	     G_STRUCT_OFFSET (QuiverImageViewClass, set_scroll_adjustments),
	     NULL, NULL,
	     _quiver_marshal_VOID__OBJECT_OBJECT,
	     G_TYPE_NONE, 2,
	     GTK_TYPE_ADJUSTMENT,
	     GTK_TYPE_ADJUSTMENT);

	g_type_class_add_private (obj_class, sizeof (QuiverImageViewPrivate));

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

	imageview->priv->pixbuf        = NULL;
	imageview->priv->pixbuf_scaled = NULL;
	imageview->priv->pixbuf_animation = NULL;
	imageview->priv->pixbuf_animation_iter = NULL;
	
	imageview->priv->pixbuf_width  = 0;
	imageview->priv->pixbuf_height = 0;

	imageview->priv->pixbuf_width_next  = 0;
	imageview->priv->pixbuf_height_next = 0;
	
	imageview->priv->view_mode = QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW;
	imageview->priv->view_mode_last = QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW;
	
	imageview->priv->transition_type = QUIVER_IMAGE_VIEW_TRANSITION_TYPE_NONE;
	imageview->priv->transition_percent = 0.;
	imageview->priv->transition_pixbuf_old = NULL;
	imageview->priv->transition_pixbuf_new = NULL;
	imageview->priv->transition_timeout_id = 0;


	imageview->priv->magnification = 1; // magnification level as a percent (1 = 100%)
	imageview->priv->magnification_timeout_id = 0; // 
	imageview->priv->magnification_final = 1;
	
	imageview->priv->timeout_scale_hq_id = 0;
/*
	imageview->priv->closure_n_items = NULL;
	imageview->priv->closure_icon_pixbuf = NULL;
	imageview->priv->closure_thumbnail_pixbuf = NULL;
	imageview->priv->closure_text = NULL;
*/
	quiver_image_view_set_scroll_adjustments(imageview,NULL,NULL);
	//imageview->priv->hadjustment  = NULL;
	//imageview->priv->vadjustment  = NULL;

	imageview->priv->scroll_timeout_id = 0;
	imageview->priv->last_hadjustment = 0.0;
	imageview->priv->last_vadjustment = 0.0;

	imageview->priv->area_updated = FALSE;
	imageview->priv->animation_timeout_id = FALSE;

	imageview->priv->scroll_draw   = TRUE;
	imageview->priv->smooth_scroll = FALSE;
	imageview->priv->smooth_scroll_cell = -1;
	
	imageview->priv->reload_event_sent = FALSE;
	
	imageview->priv->mouse_x1 = 0;
	imageview->priv->mouse_y1 = 0;
	imageview->priv->mouse_x2 = 0;
	imageview->priv->mouse_y2 = 0;

	imageview->priv->mouse_move_capture = FALSE;
	imageview->priv->mouse_move_mode = QUIVER_IMAGE_VIEW_MOUSE_MODE_DRAG;

	imageview->priv->rubberband_mode_start = FALSE;
	imageview->priv->rubberband_mode = FALSE;

	GTK_WIDGET_SET_FLAGS(imageview,GTK_CAN_FOCUS);
	//GTK_WIDGET_UNSET_FLAGS(imageview,GTK_DOUBLE_BUFFERED);

	gtk_widget_set_size_request(GTK_WIDGET(imageview),QUIVER_IMAGE_VIEW_MIN_IMAGE_SIZE,QUIVER_IMAGE_VIEW_MIN_IMAGE_SIZE);

	
}

/*
static void
quiver_image_view_destroy(GtkObject *object)
{
	QuiverImageView *imageview = QUIVER_IMAGE_VIEW(object);

}
*/


static void
quiver_image_view_realize (GtkWidget *widget)
{
	QuiverImageView *imageview;
	GdkWindowAttr attributes;
	gint attributes_mask;

	g_return_if_fail (QUIVER_IS_IMAGE_VIEW (widget));

	imageview = QUIVER_IMAGE_VIEW (widget);
	GTK_WIDGET_SET_FLAGS (widget, GTK_REALIZED);

	attributes.window_type = GDK_WINDOW_CHILD;
	attributes.x = widget->allocation.x;
	attributes.y = widget->allocation.y;
	attributes.width = widget->allocation.width;
	attributes.height = widget->allocation.height;
	attributes.wclass = GDK_INPUT_OUTPUT;
	attributes.visual = gtk_widget_get_visual (widget);
	attributes.colormap = gtk_widget_get_colormap (widget);
	//attributes.event_mask = GDK_VISIBILITY_NOTIFY_MASK;
	attributes.event_mask = gtk_widget_get_events (widget) | 
					   GDK_EXPOSURE_MASK |
					   GDK_KEY_PRESS_MASK | GDK_KEY_RELEASE_MASK |
					   GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
					   GDK_POINTER_MOTION_MASK | GDK_POINTER_MOTION_HINT_MASK;

	attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL | GDK_WA_COLORMAP;

	widget->window = gdk_window_new (gtk_widget_get_parent_window (widget), &attributes, attributes_mask);
	gdk_window_set_user_data (widget->window, imageview);

	widget->style = gtk_style_attach (widget->style, widget->window);
	gtk_style_set_background (widget->style, widget->window, GTK_STATE_NORMAL);

	quiver_image_view_send_configure (QUIVER_IMAGE_VIEW (widget));
}

/*
static void
quiver_image_view_unrealize(GtkWidget *widget)
{

}
*/


static void
quiver_image_view_finalize(GObject *object)
{
	GObjectClass *parent,*obj_class;
	QuiverImageViewClass *klass; 
	QuiverImageView *imageview;

	imageview = QUIVER_IMAGE_VIEW(object);
	klass = QUIVER_IMAGE_VIEW_GET_CLASS(imageview);
	obj_class = G_OBJECT_CLASS (klass);

	// remove timeout callbacks and unref data
	quiver_image_view_prepare_for_new_pixbuf(imageview,0,0);

	parent = g_type_class_peek_parent(klass);
	if (parent)
	{
		parent->finalize(object);
	}
}


static void
quiver_image_view_size_allocate (GtkWidget     *widget,
				GtkAllocation *allocation)
{
	g_return_if_fail (QUIVER_IS_IMAGE_VIEW (widget));
	g_return_if_fail (allocation != NULL);

	/*QuiverImageView *imageview = QUIVER_IMAGE_VIEW(widget);*/

	widget->allocation = *allocation;

	if (GTK_WIDGET_MAPPED (widget))
	{
		gdk_window_move_resize (widget->window,
			allocation->x, allocation->y,
			allocation->width, allocation->height);
			
		quiver_image_view_send_configure (QUIVER_IMAGE_VIEW (widget));
	}

}

static void
quiver_image_view_size_request (GtkWidget *widget, GtkRequisition *requisition)
{
	QuiverImageView *imageview;
		
	imageview = QUIVER_IMAGE_VIEW(widget);
	requisition->width = 20;
	requisition->height = 20;
	
	
}

static void
quiver_image_view_send_configure (QuiverImageView *imageview)
{
  GtkWidget *widget;
  GdkEvent *event = gdk_event_new (GDK_CONFIGURE);

  widget = GTK_WIDGET (imageview);

  event->configure.window = g_object_ref (widget->window);
  event->configure.send_event = TRUE;
  event->configure.x = widget->allocation.x;
  event->configure.y = widget->allocation.y;
  event->configure.width = widget->allocation.width;
  event->configure.height = widget->allocation.height;
  

  gtk_widget_event (widget, event);

  
  gdk_event_free (event);
}

static gboolean
quiver_image_view_configure_event( GtkWidget *widget, GdkEventConfigure *event )
{
	QuiverImageView *imageview;

	GdkModifierType mask;
	int x,y;

	imageview = QUIVER_IMAGE_VIEW(widget);
	gdk_window_get_pointer(widget->window,&x,&y,&mask);
	
	GdkInterpType interptype = GDK_INTERP_NEAREST;
	quiver_image_view_update_size(imageview);
	quiver_image_view_send_reload_event(imageview);

	quiver_image_view_create_scaled_pixbuf(imageview,interptype);
	quiver_image_view_add_scale_hq_timeout(imageview);

	return TRUE;
}

static void quiver_image_view_send_reload_event(QuiverImageView *imageview)
{
	// send reload signal if current display size is 
	// greater than the actual pixbuf size and the actual
	// pixbuf size is less than the "set_pixbuf_at_size" size
	if (NULL != imageview->priv->pixbuf && !imageview->priv->reload_event_sent)
	{
		gint aw, ah;
		gint width,height;
	
		quiver_image_view_get_pixbuf_display_size(imageview,&width,&height);

		aw = gdk_pixbuf_get_width(imageview->priv->pixbuf);
		ah = gdk_pixbuf_get_height(imageview->priv->pixbuf);

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
/*
static void quiver_image_view_create_transition_pixbuf(QuiverImageView *imageview)
{

//	if (QUIVER_IMAGE_VIEW_TRANSITION_TYPE_NONE != imageview->priv->transition_type
//		&& 0 != imageview->priv->transition_timeout_id)
//	{
		
		if (NULL == imageview->priv->pixbuf_scaled)
		{
			quiver_image_view_create_scaled_pixbuf(imageview,GDK_INTERP_BILINEAR);

			if (NULL == imageview->priv->pixbuf_scaled && NULL != imageview->priv->pixbuf)
			{
				imageview->priv->pixbuf_scaled = gdk_pixbuf_copy(imageview->priv->pixbuf);
			}
			
			imageview->priv->transition_pixbuf_new = imageview->priv->pixbuf_scaled;
			
			// we need to create the new pixbuf size
			gint old_w,old_h;
			gint new_w,new_h;
			old_w = 0;
			old_h = 0;
			if (NULL != imageview->priv->transition_pixbuf_old)
			{
				old_w = gdk_pixbuf_get_width(imageview->priv->transition_pixbuf_old);
				old_h = gdk_pixbuf_get_height(imageview->priv->transition_pixbuf_old);
			}

			new_w = gdk_pixbuf_get_width(imageview->priv->transition_pixbuf_new);
			new_h = gdk_pixbuf_get_height(imageview->priv->transition_pixbuf_new);
			
			gint w,h;
			w = MAX(old_w,new_w);
			h = MAX(old_h,new_h);
			
			imageview->priv->pixbuf_scaled = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, w, h);
		}

		gint alpha = 255 * imageview->priv->transition_percent;

		gint old_w,old_h;
		gint new_w,new_h;
		
		old_w = 0;
		old_h = 0;
		if (NULL != imageview->priv->transition_pixbuf_old)
		{
			old_w = gdk_pixbuf_get_width(imageview->priv->transition_pixbuf_old);
			old_h = gdk_pixbuf_get_height(imageview->priv->transition_pixbuf_old);
		}

		new_w = gdk_pixbuf_get_width(imageview->priv->transition_pixbuf_new);
		new_h = gdk_pixbuf_get_height(imageview->priv->transition_pixbuf_new);
		
		gint w,h;
		w = MAX(old_w,new_w);
		h = MAX(old_h,new_h);
		
		// FIXME: fill the pixbuf with the background color of the window
		gdk_pixbuf_fill(imageview->priv->pixbuf_scaled,(guint32)0x00000000);
		
		// composite the old image
		if (NULL != imageview->priv->transition_pixbuf_old)
		{
			gdk_pixbuf_composite
				(imageview->priv->transition_pixbuf_old,
				 imageview->priv->pixbuf_scaled,
				 (w - old_w)/2,
				 (h - old_h)/2,
				 old_w,
				 old_h,
				 (w - old_w)/2,
				 (h - old_h)/2,
				 1,
				 1,
				 GDK_INTERP_NEAREST,
				 255 - alpha);
		}
		// composite the new
		if (NULL != imageview->priv->transition_pixbuf_new)
		{
			gdk_pixbuf_composite
				(imageview->priv->transition_pixbuf_new,
				 imageview->priv->pixbuf_scaled,
				 (w - new_w)/2,
				 (h - new_h)/2,
				 new_w,
				 new_h,
				 (w - new_w)/2,
				 (h - new_h)/2,
				 1,
				 1,
				 GDK_INTERP_NEAREST,
				 alpha);
			
		}		

//	}

}
*/

static void quiver_image_view_create_scaled_pixbuf(QuiverImageView *imageview,GdkInterpType interptype)
{
	GtkWidget *widget;
	GdkPixbuf *pixbuf;
	gint actual_width,actual_height;
	gint width,height;
	gint new_width,new_height;
	gboolean stretch;

	stretch = FALSE;

	widget = GTK_WIDGET(imageview);

	pixbuf = imageview->priv->pixbuf;

	if ( !GTK_WIDGET_MAPPED (widget) )
	{
		return;
	}

	if (NULL == pixbuf)
		return;

	actual_width = gdk_pixbuf_get_width(pixbuf);
	actual_height = gdk_pixbuf_get_height(pixbuf);

	width = imageview->priv->pixbuf_width;
	height = imageview->priv->pixbuf_height;

	quiver_image_view_get_pixbuf_display_size(imageview, &new_width,&new_height);

	if (NULL != imageview->priv->pixbuf_scaled)
	{
		g_object_unref(imageview->priv->pixbuf_scaled);
		imageview->priv->pixbuf_scaled = NULL;
	}



	switch (imageview->priv->view_mode)
	{
		case QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW_STRETCH:
			// drop into normal fit window mode
		case QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW:
			if (new_width != actual_width || new_height != actual_height)
			{
				imageview->priv->pixbuf_scaled = gdk_pixbuf_scale_simple (pixbuf, new_width, new_height, interptype);
			}
			break;

		case QUIVER_IMAGE_VIEW_MODE_ACTUAL_SIZE:
			// no need for scaled image because we are showing the actual size;
			if (new_width != actual_width || new_height != actual_height)
			{
				// except in this case. in this case the actual size is
				// not the same as the size we want to display the image at
				// this is for quickview to blow up a thumbnail to the size of
				// the actual image 
			}
			else
			{
				break;
			}

		case QUIVER_IMAGE_VIEW_MODE_ZOOM:
		{

			if (new_width == actual_width && new_height == actual_height)
				break;
			gdouble magnification = imageview->priv->magnification;

			gdouble hadjust  = (gint)gtk_adjustment_get_value(imageview->priv->hadjustment);
			gdouble vadjust  = (gint)gtk_adjustment_get_value(imageview->priv->vadjustment);

			gint wnd_width,wnd_height;
			wnd_width = widget->allocation.width;
			wnd_height = widget->allocation.height;

			gint src_clip_x,src_clip_y;
			gint src_clip_width, src_clip_height;
			gint new_x,new_y;

			new_x = new_y = 0;
			src_clip_x = 0;
			src_clip_y = 0;
			src_clip_width = actual_width;
			src_clip_height = actual_height;

			gint tmp_w,tmp_h;
			tmp_w = new_width;
			tmp_h = new_height;

			// image will be bigger than the window size, so we 
			// should get a suitable subset of the image, resize 
			// it and crop out the unnecessary bits
			
			if (new_width > wnd_width)
			{
				// get the area from the original image that will be used as
				// the sub_pixbuf for resizing (we dont want to resize the
				// whole image - just the region of interest.
				
				// tmp_mag - in case the actual size of the pixbuf is 
				// not the same as the set size
				gdouble tmp_mag = magnification * width / actual_width;

				src_clip_x     = (gint)(hadjust / tmp_mag);
				src_clip_x = MAX(0,src_clip_x-2); // make the src area a tad bigger

				src_clip_width = (gint)(wnd_width / tmp_mag + .5); // add .5 for rounding
				src_clip_width +=4; // make the src area a tad bigger
				src_clip_width = MIN(actual_width-src_clip_x,src_clip_width);

				new_width = wnd_width;

				tmp_w = (gint)((src_clip_width) * tmp_mag+.5);

				new_x     = (gint)(hadjust - src_clip_x*tmp_mag +.5);
				new_x = MAX(0,MIN(new_x, tmp_w - new_width));
			}
		
			if (new_height > wnd_height)
			{
				gdouble tmp_mag = magnification * width / actual_width;
				
				src_clip_y      = (gint)(vadjust / tmp_mag);
				src_clip_y = MAX(0,src_clip_y-2); // make the src area a tad bigger

				src_clip_height = (gint)(wnd_height / tmp_mag + .5);// add .5 for rounding
				src_clip_height +=4; // make the src area a tad bigger
				src_clip_height = MIN(actual_height-src_clip_y,src_clip_height);

				new_height = wnd_height;
				//add one to make sure we get more than we need


				tmp_h = (gint)((src_clip_height)*tmp_mag +.5);

				new_y     =  (gint)(vadjust - src_clip_y*tmp_mag);
				new_y = MAX(0,MIN(new_y, tmp_h - new_height));
			}
			//printf("original image size: %d %d\n",actual_width,actual_height);

			//printf("sx,sy,sw,sh %d %d  - %d %d\n",src_clip_x,src_clip_y,src_clip_width,src_clip_height);
			GdkPixbuf *sub_pixbuf = gdk_pixbuf_new_subpixbuf(imageview->priv->pixbuf,src_clip_x,src_clip_y,src_clip_width,src_clip_height);
			//printf("new img size %d %d \n",tmp_w,tmp_h);
			GdkPixbuf *new_pixbuf = gdk_pixbuf_scale_simple(sub_pixbuf,tmp_w, tmp_h,interptype);

			g_object_unref(sub_pixbuf);

			//printf("nx,ny,nw,nh %d %d - %d %d\n",new_x,new_y,new_width,new_height);
			sub_pixbuf = gdk_pixbuf_new_subpixbuf(new_pixbuf,new_x,new_y,new_width,new_height);
			//printf("done\n");

			g_object_unref(new_pixbuf);

			imageview->priv->pixbuf_scaled = sub_pixbuf;

			break;

		}
		default:
			break;
	}
}
 
static void draw_pixbuf(QuiverImageView *imageview,GdkRegion *region,gboolean stretch)
{
	GtkWidget *widget;
	GdkPixbuf *pixbuf;
	GdkPixbuf *pixbuf_scaled;
	gint width,height;
	//guint x_offset,y_offset;

	widget = GTK_WIDGET(imageview);

	gint hadj,vadj;
	hadj = (gint)gtk_adjustment_get_value(imageview->priv->hadjustment);
	vadj = (gint)gtk_adjustment_get_value(imageview->priv->vadjustment);

	pixbuf = imageview->priv->pixbuf;
	pixbuf_scaled = imageview->priv->pixbuf_scaled;

	if (pixbuf_scaled)
	{
		pixbuf = pixbuf_scaled;
		hadj = 0;
		vadj = 0;
	}
	
	g_object_ref(pixbuf);
		
	width = gdk_pixbuf_get_width(pixbuf);
	height = gdk_pixbuf_get_height(pixbuf);



	
	GdkRectangle pixbuf_rect;
	pixbuf_rect.x = MAX(0,((widget->allocation.width - width)/2));
	pixbuf_rect.y = MAX(0,((widget->allocation.height - height)/2));
	pixbuf_rect.width = MIN(width,widget->allocation.width);
	pixbuf_rect.height = MIN(height,widget->allocation.height);


	

	int i;
	int n_rectangles =0;
	GdkRectangle *rects = NULL;

	gdk_region_get_rectangles(region,&rects,&n_rectangles);

	for (i = 0; i < n_rectangles; i++)
	{
		GdkRectangle intersection;
		if (gdk_rectangle_intersect(&pixbuf_rect,&rects[i],&intersection))
		{
			gdk_draw_pixbuf(widget->window,widget->style->black_gc,pixbuf,
				hadj + intersection.x - pixbuf_rect.x,
				vadj + intersection.y - pixbuf_rect.y,
				intersection.x,
				intersection.y,
				intersection.width,
				intersection.height,
				GDK_RGB_DITHER_NONE,0,0);
		}

	}
	g_free(rects);
	g_object_unref(pixbuf);

	/*
	gdk_draw_rectangle(widget->window,widget->style->black_gc,FALSE,
		pixbuf_rect.x,
		pixbuf_rect.y,
		pixbuf_rect.width-1,
		pixbuf_rect.height-1);
	
	*/

}

static gboolean
quiver_image_view_expose_event (GtkWidget *widget, GdkEventExpose *event)
{

	QuiverImageView *imageview;
	imageview = QUIVER_IMAGE_VIEW(widget);

	if (NULL != imageview->priv->pixbuf)
	{
		switch (imageview->priv->view_mode)
		{
			case QUIVER_IMAGE_VIEW_MODE_ACTUAL_SIZE:
			case QUIVER_IMAGE_VIEW_MODE_ZOOM:
			case QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW:
				draw_pixbuf(imageview,event->region,FALSE);
				break;
			case QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW_STRETCH:
				draw_pixbuf(imageview,event->region,TRUE);
				break;
			default:
				break;
		}
	}

	return TRUE;
}


static gboolean 
quiver_image_view_button_press_event (GtkWidget *widget, 
                     GdkEventButton *event)
{
	QuiverImageView *imageview = QUIVER_IMAGE_VIEW(widget);

	//printf("button pressed\n");

	gint x =0, y=0;
	x = (gint)event->x;
	y = (gint)event->y;

	imageview->priv->mouse_x1 = imageview->priv->mouse_x2 = x;
	imageview->priv->mouse_y1 = imageview->priv->mouse_y2 = y;


	if (!GTK_WIDGET_HAS_FOCUS (widget))
	{
		gtk_widget_grab_focus (widget);
	}

	if (1 == event->button)
	{
		if (GDK_BUTTON_PRESS == event->type)
		{
			imageview->priv->mouse_move_capture = TRUE;
			return TRUE;
		}
		else if (GDK_2BUTTON_PRESS == event->type)
		{
			//activate
			quiver_image_view_activate(imageview);
		}
		else if (GDK_3BUTTON_PRESS == event->type)
		{
		}
	}


	return FALSE;
	
}

static gboolean 
quiver_image_view_button_release_event (GtkWidget *widget, 
         GdkEventButton *event)
{
	//printf("button released\n");

	QuiverImageView *imageview = QUIVER_IMAGE_VIEW(widget);

	gint x =0, y=0;
	x = (gint)event->x;
	y = (gint)event->y;

	imageview->priv->mouse_x1 = imageview->priv->mouse_x2 = x;
	imageview->priv->mouse_y1 = imageview->priv->mouse_y2 = y;
	
	if (1 == event->button)
	{
		imageview->priv->mouse_move_capture = FALSE;
	}

	if (imageview->priv->rubberband_mode)
	{

		GdkRegion *invalid_region;
		GdkRegion *tmp_region;

		tmp_region = gdk_region_rectangle(&imageview->priv->rubberband_rect);
		gdk_region_shrink(tmp_region,1,1);

		invalid_region = gdk_region_rectangle(&imageview->priv->rubberband_rect);

		gdk_region_subtract(invalid_region,tmp_region);
		gdk_region_shrink(invalid_region,-1,-1);

		gdk_window_invalidate_region(widget->window,invalid_region,FALSE);

		gdk_region_destroy(invalid_region);
		gdk_region_destroy(tmp_region);
	}

	imageview->priv->rubberband_mode_start = FALSE;
	imageview->priv->rubberband_mode = FALSE;
	return TRUE;
}

static gboolean
quiver_image_view_motion_notify_event (GtkWidget *widget,
            GdkEventMotion *event)
{
	QuiverImageView *imageview = QUIVER_IMAGE_VIEW(widget);

	GdkModifierType state;
	gint x =0, y=0;

	if (event->is_hint)
	{
		gdk_window_get_pointer (event->window, &x, &y, &state);
	}		
	else
	{
		x = (gint)event->x;
		y = (gint)event->y;
		state = (GdkModifierType)event->state;
		
	}

	if (imageview->priv->mouse_move_capture)
	{
		if (QUIVER_IMAGE_VIEW_MOUSE_MODE_DRAG == imageview->priv->mouse_move_mode)
		{
			//printf("motion\n");
			
			gdouble hadjust = gtk_adjustment_get_value(imageview->priv->hadjustment);
			gdouble vadjust = gtk_adjustment_get_value(imageview->priv->vadjustment);
			hadjust += imageview->priv->mouse_x1 - x;
			hadjust = MAX(0,MIN(imageview->priv->hadjustment->upper - imageview->priv->hadjustment->page_size,hadjust));
			vadjust += imageview->priv->mouse_y1 - y;
			vadjust = MAX(0,MIN(imageview->priv->vadjustment->upper - imageview->priv->vadjustment->page_size,vadjust));
			gtk_adjustment_set_value(imageview->priv->hadjustment,hadjust);
			gtk_adjustment_set_value(imageview->priv->vadjustment,vadjust);
			imageview->priv->mouse_x1 = x;
			imageview->priv->mouse_y1 = y;
		}
		//imageview->priv->mouse_move_capture = FALSE;
	}

	/*
	 * FIXME
	 */
	if (imageview->priv->rubberband_mode_start)
	{
		imageview->priv->rubberband_mode_start = FALSE;
		imageview->priv->rubberband_mode = TRUE;
	}

	if (imageview->priv->rubberband_mode)
	{
		GdkRegion *invalid_region;
		GdkRegion *old_region;

		imageview->priv->rubberband_rect_old.x = MIN (imageview->priv->mouse_x1, imageview->priv->mouse_x2);
		imageview->priv->rubberband_rect_old.y = MIN (imageview->priv->mouse_y1, imageview->priv->mouse_y2);
		imageview->priv->rubberband_rect_old.width = ABS (imageview->priv->mouse_x1 - imageview->priv->mouse_x2)+1;
		imageview->priv->rubberband_rect_old.height = ABS (imageview->priv->mouse_y1 - imageview->priv->mouse_y2)+1;
		
		imageview->priv->mouse_x2 = x;
		imageview->priv->mouse_y2 = y;

		imageview->priv->rubberband_rect.x = MIN (imageview->priv->mouse_x1, imageview->priv->mouse_x2);
		imageview->priv->rubberband_rect.y = MIN (imageview->priv->mouse_y1, imageview->priv->mouse_y2);
		imageview->priv->rubberband_rect.width = ABS (imageview->priv->mouse_x1 - imageview->priv->mouse_x2)+1;
		imageview->priv->rubberband_rect.height = ABS (imageview->priv->mouse_y1 - imageview->priv->mouse_y2)+1;

		invalid_region = gdk_region_rectangle(&imageview->priv->rubberband_rect);
		old_region = gdk_region_rectangle(&imageview->priv->rubberband_rect_old);
		gdk_region_xor(invalid_region,old_region);
		gdk_region_shrink(invalid_region,-1,-1);

		// the way that gdk draws rectangles means we need to subtract
		// one from the width and height
		imageview->priv->rubberband_rect.width -= 1;
		imageview->priv->rubberband_rect.height -= 1;
		//rubberband_rect_old.width -= 1;
		//rubberband_rect_old.height -= 1;

		//redraw_needed = TRUE;
		gdk_window_invalidate_region(widget->window,invalid_region,FALSE);
		gdk_region_destroy(invalid_region);
		gdk_region_destroy(old_region);
	}

	return FALSE;

}

gboolean quiver_image_view_scroll_event ( GtkWidget *widget,
           GdkEventScroll *event)
{
	
	QuiverImageView *imageview;
	imageview = QUIVER_IMAGE_VIEW(widget);


	int adjustment = 5;
	if (event->state & GDK_SHIFT_MASK)
	{
		adjustment = 1;
	}
	//printf("scroll event\n");

	gboolean rvalue = FALSE;

	if (event->state & GDK_CONTROL_MASK)
	{
		/* magnification in on the image */
		if (GDK_SCROLL_UP == event->direction)
		{
			if (QUIVER_IMAGE_VIEW_MODE_ZOOM != imageview->priv->view_mode)
				quiver_image_view_set_view_mode_full(imageview,QUIVER_IMAGE_VIEW_MODE_ZOOM,FALSE);
			quiver_image_view_set_magnification(imageview,
				quiver_image_view_get_magnification(imageview)/1.3);
			//vadjust -= imageview->priv->vadjustment->step_increment;
		}
		else if (GDK_SCROLL_DOWN == event->direction)
		{
			if (QUIVER_IMAGE_VIEW_MODE_ZOOM != imageview->priv->view_mode)
				quiver_image_view_set_view_mode_full(imageview,QUIVER_IMAGE_VIEW_MODE_ZOOM,FALSE);
			quiver_image_view_set_magnification(imageview,
				quiver_image_view_get_magnification(imageview)*1.3);
			//vadjust += imageview->priv->vadjustment->step_increment;
		}

	
		rvalue = TRUE;
	}

	return rvalue;
}


static GtkAdjustment *
new_default_adjustment (void)
{
  return GTK_ADJUSTMENT (gtk_adjustment_new (0.0, 0.0, 0.0, 0.0, 0.0, 0.0));
}


static void           
quiver_image_view_set_scroll_adjustments (QuiverImageView     *imageview,
			    GtkAdjustment *hadj,
			    GtkAdjustment *vadj)
{
	gboolean need_adjust = FALSE;

	g_return_if_fail (QUIVER_IS_IMAGE_VIEW (imageview));

	if (hadj)
		g_return_if_fail (GTK_IS_ADJUSTMENT (hadj));
	else
		hadj = new_default_adjustment ();
	if (vadj)
		g_return_if_fail (GTK_IS_ADJUSTMENT (vadj));
	else
		vadj = new_default_adjustment ();

	if (imageview->priv->hadjustment && (imageview->priv->hadjustment != hadj))
	{
		g_signal_handlers_disconnect_by_func (imageview->priv->hadjustment,
			quiver_image_view_adjustment_value_changed,
			imageview);
		g_object_unref (imageview->priv->hadjustment);
	}

	if (imageview->priv->vadjustment && (imageview->priv->vadjustment != vadj))
	{
		g_signal_handlers_disconnect_by_func (imageview->priv->vadjustment,
			quiver_image_view_adjustment_value_changed,
			imageview);
		g_object_unref (imageview->priv->vadjustment);
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
	if (need_adjust && vadj && hadj)
		quiver_image_view_adjustment_value_changed (NULL, imageview);
}

void quiver_image_view_add_scale_hq_timeout(QuiverImageView *imageview)
{
	if (0 != imageview->priv->timeout_scale_hq_id)
	{
		g_source_remove(imageview->priv->timeout_scale_hq_id);
	}
	imageview->priv->timeout_scale_hq_id = g_timeout_add(QUIVER_ICON_VIEW_SCALE_HQ_TIMEOUT,quiver_image_view_timeout_scale_hq,imageview);
}

static gboolean 
quiver_image_view_timeout_scale_hq(gpointer data)
{
	gboolean retval;
	QuiverImageView *imageview;
	GtkWidget *widget;

	GdkModifierType mask;
	gint x,y;

	gdk_threads_enter();

	imageview = (QuiverImageView*)data;
	widget = GTK_WIDGET(imageview);
	retval = FALSE;

	gdk_window_get_pointer(widget->window,&x,&y,&mask);


	if (GDK_BUTTON1_MASK & mask || GDK_BUTTON2_MASK & mask) 
	{
		retval = TRUE;
	}
	else
	{
		// run the hq scale function
		// now invalidate the window
		//
		GdkRectangle rect;
		rect.x = 0;
		rect.y = 0;
		rect.width = widget->allocation.width;
		rect.height = widget->allocation.height;
		//printf("%%%%%%%%%%%% timeout scale HQ!\n");
		quiver_image_view_create_scaled_pixbuf(imageview,GDK_INTERP_BILINEAR);
		if (GTK_WIDGET_MAPPED (widget))
		{
			// FIXME: probalby dont need to invalidate whole image area
			gdk_window_invalidate_rect(widget->window,&rect,FALSE);
		}		
			

	}
	gdk_threads_leave();
	return retval;
}


static void 
quiver_image_view_scroll(QuiverImageView *imageview)
{
	GtkWidget *widget = GTK_WIDGET(imageview);
	gdouble hadj,vadj;
	hadj = floor(gtk_adjustment_get_value(imageview->priv->hadjustment));
	vadj = floor(gtk_adjustment_get_value(imageview->priv->vadjustment));
	
	if (GTK_WIDGET_MAPPED (imageview))
	{
		if (imageview->priv->scroll_draw)
		{
			//printf("########### scrolldraw scale\n");
			quiver_image_view_create_scaled_pixbuf(imageview,GDK_INTERP_NEAREST);

			gint hdiff = floor(imageview->priv->last_hadjustment - hadj);
			gint vdiff = floor(imageview->priv->last_vadjustment - vadj);

			gdk_window_scroll(widget->window,hdiff,vdiff);
			gdk_window_process_updates (widget->window, FALSE);

			quiver_image_view_add_scale_hq_timeout(imageview);
		}
		imageview->priv->last_vadjustment = vadj;
		imageview->priv->last_hadjustment = hadj;
	}
	imageview->priv->scroll_timeout_id = 0;
}

static gboolean 
quiver_image_view_timeout_scroll(gpointer data)
{
	QuiverImageView *imageview;

	imageview = (QuiverImageView*)data;

	gdk_threads_enter();

	quiver_image_view_scroll(imageview);

	gdk_threads_leave();

	return FALSE;
}

static void quiver_image_view_add_scroll_timeout(QuiverImageView *imageview)
{
	if (0 != imageview->priv->scroll_timeout_id)
	{
		g_source_remove(imageview->priv->scroll_timeout_id);
	}
	imageview->priv->scroll_timeout_id = g_timeout_add(2,quiver_image_view_timeout_scroll,imageview);

}


/* start callbacks */
static void
quiver_image_view_adjustment_value_changed (GtkAdjustment *adjustment,
           QuiverImageView *imageview)
{
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
  
  gdouble min = MAX (0., upper - adj->page_size);

  if (upper != adj->upper)
    {
      adj->upper = upper;
      changed = TRUE;
    }
      
  if (adj->value > min)
    {
      adj->value = min;
      value_changed = TRUE;
    }
  
  if (changed || always_emit_changed)
    gtk_adjustment_changed (adj);
  if (value_changed)
    gtk_adjustment_value_changed (adj);
}

static void
quiver_image_view_update_size(QuiverImageView *imageview)
{
	GtkWidget *widget = GTK_WIDGET(imageview);
	gint width,height;

	quiver_image_view_get_pixbuf_display_size(imageview,&width,&height);

	//printf("updating size!\n");

	GtkAdjustment *hadjustment, *vadjustment;

	hadjustment = imageview->priv->hadjustment;
	vadjustment = imageview->priv->vadjustment;

	hadjustment->page_size = widget->allocation.width;
	hadjustment->page_increment = widget->allocation.width * 0.9;
	hadjustment->step_increment = widget->allocation.width * 0.1;
	hadjustment->lower = 0;
	hadjustment->upper = MAX (widget->allocation.width, width);

	if (hadjustment->value > hadjustment->upper - hadjustment->page_size)
		gtk_adjustment_set_value (hadjustment, MAX (0, hadjustment->upper - hadjustment->page_size));

	vadjustment->page_size = widget->allocation.height;
	vadjustment->page_increment = widget->allocation.height * 0.9;
	vadjustment->step_increment = widget->allocation.height * 0.1;
	vadjustment->lower = 0;
	vadjustment->upper = MAX (widget->allocation.height, height);

	if (vadjustment->value > vadjustment->upper - vadjustment->page_size)
		gtk_adjustment_set_value (vadjustment, MAX (0, vadjustment->upper - vadjustment->page_size));

	gtk_adjustment_changed (hadjustment);
	gtk_adjustment_changed (vadjustment);
	
}

static void quiver_image_view_get_bound_size(guint bound_width,guint bound_height,guint *width,guint *height,gboolean fill_if_smaller)
{
	guint w = *width;
	guint h = *height;

	if (!fill_if_smaller && 
			(w < bound_width && h < bound_height) )
	{
		return;
	}

	if (w != bound_width || h != bound_height)
	{
		gdouble ratio = (double)w/h;
		guint new_height;

		new_height = (guint)(bound_width/ratio);
		if (new_height < bound_height)
		{
			*width = bound_width;
			*height = new_height;
		}
		else
		{
			*width = (guint)(bound_height *ratio);
			*height = bound_height;
		}
	}
}	

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

	
	gdk_threads_enter();
	
	//printf("timeout!\n");
	if (gdk_pixbuf_animation_iter_advance(imageview->priv->pixbuf_animation_iter,NULL))
	{

		GdkPixbuf *pixbuf = gdk_pixbuf_animation_iter_get_pixbuf(imageview->priv->pixbuf_animation_iter);
		if (NULL != imageview->priv->pixbuf)
			g_object_unref(imageview->priv->pixbuf);

		imageview->priv->pixbuf = gdk_pixbuf_copy(pixbuf);
		if (GTK_WIDGET_MAPPED (widget))
		{
			quiver_image_view_create_scaled_pixbuf(imageview,GDK_INTERP_BILINEAR);
			quiver_image_view_invalidate_image_area(imageview,NULL);
		}
	}
	quiver_image_view_add_animation_timeout(imageview);
	
	gdk_threads_leave();

	return FALSE;
}
/*
static void quiver_image_view_transition_start(QuiverImageView *imageview)
{
	imageview->priv->transition_percent = 0.;
	quiver_image_view_add_transition_timeout(imageview);
}
static void quiver_image_view_add_transition_timeout(QuiverImageView *imageview)
{
	if (0 != imageview->priv->transition_timeout_id)
	{
		g_source_remove(imageview->priv->transition_timeout_id);
	}
	imageview->priv->transition_timeout_id = g_timeout_add(5,quiver_image_view_timeout_transition,imageview);
}

static gboolean quiver_image_view_timeout_transition(gpointer data)
{
	gboolean rval = FALSE;
	QuiverImageView *imageview = (QuiverImageView*)data;
	
	gdk_threads_enter();
	
	imageview->priv->transition_percent += .15;
	
	if (1 < imageview->priv->transition_percent)
	{
		
		//g_object_unref(imageview->priv->transition_pixbuf_old);
		//imageview->priv->transition_pixbuf_old = NULL;
		//imageview->priv->pixbuf_scaled = imageview->priv->transition_pixbuf_new;
		//imageview->priv->transition_pixbuf_new = NULL;
		//imageview->priv->transition_percent = 1.;
		
		//rval = FALSE;
	}
	else
	{
		//quiver_image_view_create_transition_pixbuf(imageview);
		quiver_image_view_add_transition_timeout(imageview);
	}
	quiver_image_view_invalidate_image_area(imageview,NULL);
	gdk_threads_leave();
	return rval;	
}
*/

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
	
	gdk_threads_enter();
	
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
		quiver_image_view_set_magnification_full(imageview,imageview->priv->magnification_final);
		imageview->priv->magnification_timeout_id = 0;
		rval = FALSE;
	}
	else
	{
		quiver_image_view_set_magnification_full(imageview,imageview->priv->magnification + mag_diff/2);
		imageview->priv->magnification_timeout_id = 0;
		quiver_image_view_add_magnification_timeout(imageview);
		rval = FALSE;
	}
	gdk_threads_leave();
	
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

void quiver_image_view_get_pixbuf_display_size_for_mode(QuiverImageView *imageview, QuiverImageViewMode mode, gint *width, gint *height)
{
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
			{
				gdouble magnification = imageview->priv->magnification;
				*out_width  = (gint)(*out_width * magnification);
				*out_height  = (gint)(*out_height * magnification);
			}
			break;
		case QUIVER_IMAGE_VIEW_MODE_ACTUAL_SIZE:
			break;

		case QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW:
			quiver_image_view_get_bound_size(widget->allocation.width,widget->allocation.height,(guint*)out_width,(guint*)out_height,FALSE);
			break;
		case QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW_STRETCH:
			quiver_image_view_get_bound_size(widget->allocation.width,widget->allocation.height,(guint*)out_width,(guint*)out_height,TRUE);
			break;
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
	GtkWidget *widget;
	GdkRectangle old_rect;
	GdkRectangle new_rect;
	gint old_width,old_height;

	GdkRegion *old_region,*new_region;

	GdkPixbuf *old_pixbuf;
	
	widget = GTK_WIDGET(imageview);
	old_pixbuf = imageview->priv->pixbuf;

	if ( !GTK_WIDGET_MAPPED (widget) )
	{
		return;
	}

	quiver_image_view_get_pixbuf_display_size(imageview,&old_width,&old_height);
	
	QuiverImageViewMode mode = imageview->priv->view_mode;
	
	if (QUIVER_IMAGE_VIEW_MODE_ZOOM == imageview->priv->view_mode)
	{
		mode = imageview->priv->view_mode_last;
	}
	quiver_image_view_get_pixbuf_display_size_for_mode_alt(imageview, mode, new_width, new_height, &new_width, &new_height);

	old_rect.x = MAX(0,(widget->allocation.width - old_width)/2.);
	old_rect.y = MAX(0,(widget->allocation.height - old_height)/2.);
	old_rect.width = MIN(old_width,widget->allocation.width);
	old_rect.height = MIN(old_height,widget->allocation.height);

	new_rect.x = MAX(0,(widget->allocation.width - new_width)/2.);
	new_rect.y = MAX(0,(widget->allocation.height - new_height)/2.);
	new_rect.width = MIN(new_width,widget->allocation.width);
	new_rect.height = MIN(new_height,widget->allocation.height);
	//printf(" %d %d %d %d\n",old_rect.x,old_rect.y,old_rect.width,old_rect.height);
	//printf(" %d %d %d %d\n",new_rect.x,new_rect.y,new_rect.width,new_rect.height);

	old_region = gdk_region_rectangle(&old_rect);
	new_region = gdk_region_rectangle(&new_rect);
	gdk_region_shrink(old_region,-1,-1);
	gdk_region_shrink(new_region,1,1);
	gdk_region_subtract(old_region,new_region);
	gdk_window_invalidate_region(widget->window,old_region,FALSE);
	gdk_region_destroy(old_region);
	gdk_region_destroy(new_region);

}


/* by default , this function will intersect the two rects and only invalidate
 * the area that is in both 
 */
static void quiver_image_view_invalidate_image_area(QuiverImageView *imageview,GdkRectangle *sub_rect)
{
	GtkWidget *widget;
	GdkRectangle invalid_rect;
	GdkRectangle pixbuf_rect;
	GdkRectangle sub_rect_tmp;
	gint width,height;

	GdkPixbuf *pixbuf;

	widget = GTK_WIDGET(imageview);
	pixbuf = imageview->priv->pixbuf;

	if ( !GTK_WIDGET_MAPPED (widget) )
	{
		return;
	}
	
	quiver_image_view_get_pixbuf_display_size(imageview,&width,&height);

	pixbuf_rect.x = MAX(0,((widget->allocation.width - width)/2));
	pixbuf_rect.y = MAX(0,((widget->allocation.height - height)/2));
	pixbuf_rect.width = MIN(width,widget->allocation.width);
	pixbuf_rect.height = MIN(height,widget->allocation.height);

	if (NULL != sub_rect)
	{
		sub_rect_tmp = *sub_rect;
		sub_rect_tmp.x += pixbuf_rect.x;
		sub_rect_tmp.y += pixbuf_rect.y;
		gdk_rectangle_intersect(&sub_rect_tmp,&pixbuf_rect,&invalid_rect);
		//printf("invalid rect: %d %d %d %d\n",invalid_rect.x,invalid_rect.y,invalid_rect.width,invalid_rect.height);
	}
	else
	{
		invalid_rect = pixbuf_rect;
	}
	gdk_window_invalidate_rect(widget->window,&invalid_rect,FALSE);
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
	imageview->priv->smooth_scroll = smooth_scroll;
}

GdkPixbuf* quiver_image_view_get_pixbuf(QuiverImageView *imageview)
{
	return imageview->priv->pixbuf;
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
	quiver_image_view_prepare_for_new_pixbuf(imageview,width,height);
	
	if (NULL != pixbuf)
	{
		g_object_ref(pixbuf);

		imageview->priv->pixbuf = pixbuf;
	}
	imageview->priv->pixbuf_width = width;
	imageview->priv->pixbuf_height = height;
	
	if (reset_view_mode)
	{
		quiver_image_view_reset_view_mode(imageview,FALSE);
		
		imageview->priv->scroll_draw = FALSE;
		quiver_image_view_update_size(imageview);
		
		gtk_adjustment_set_value(imageview->priv->hadjustment,0);
		gtk_adjustment_set_value(imageview->priv->vadjustment,0);
		
		imageview->priv->scroll_draw = TRUE;
	}
	
	quiver_image_view_create_scaled_pixbuf(imageview,GDK_INTERP_NEAREST);
	quiver_image_view_invalidate_image_area(imageview,NULL);
	quiver_image_view_add_scale_hq_timeout(imageview);
}

static void quiver_image_view_reset_view_mode(QuiverImageView *imageview,gboolean invalidate)
{
	if (QUIVER_IMAGE_VIEW_MODE_ZOOM == imageview->priv->view_mode)
	{
		quiver_image_view_set_view_mode_full(imageview,imageview->priv->view_mode_last,invalidate);
	}
}

QuiverImageViewMode quiver_image_view_get_view_mode(QuiverImageView *imageview)
{
	return imageview->priv->view_mode;
}

void quiver_image_view_set_view_mode(QuiverImageView *imageview,QuiverImageViewMode mode)
{
	quiver_image_view_set_view_mode_full(imageview,mode,TRUE);
}

static void quiver_image_view_set_view_mode_full(QuiverImageView *imageview,QuiverImageViewMode mode,gboolean invalidate)
{
	
	GtkWidget *widget;
	GdkRectangle rect;
	
	if (QUIVER_IMAGE_VIEW_MODE_ZOOM != imageview->priv->view_mode 
		&& QUIVER_IMAGE_VIEW_MODE_ZOOM == mode)
	{
		imageview->priv->magnification = quiver_image_view_get_magnification(imageview);
		imageview->priv->view_mode_last = imageview->priv->view_mode;
	}

	widget = GTK_WIDGET(imageview);

	gint ow,oh,nw,nh;
	
	quiver_image_view_get_pixbuf_display_size(imageview,&ow, &oh);

	// when switching to zoom view mode, we need to set 
	// the current magnification the magnification to be 
	// that of the current view mode
	imageview->priv->magnification = quiver_image_view_get_magnification(imageview);

	imageview->priv->view_mode = mode;

	// for the case that we are switching away from zoom mode, we must 
	// set the magnification to the correct magnification for the current mode.
	// if we are switching to zoom mode, this essentially does nothing
	imageview->priv->magnification = quiver_image_view_get_magnification(imageview);

	if (!invalidate)
	{
		return;
	}

	quiver_image_view_get_pixbuf_display_size(imageview,&nw, &nh);

	if (ow != nw || oh != nh)
	{
		rect.x = 0;
		rect.y = 0;
		rect.width = widget->allocation.width;
		rect.height = widget->allocation.height;

		quiver_image_view_create_scaled_pixbuf(imageview,GDK_INTERP_BILINEAR);
		if ( GTK_WIDGET_MAPPED (widget) )
		{
			//FIXME: probably dont need to invalidate whole image area
			gdk_window_invalidate_rect(widget->window,&rect,FALSE);
		}


		quiver_image_view_update_size(imageview);

	}
}

void quiver_image_view_set_transition_type(QuiverImageView *imageview,QuiverImageViewTransitionType type)
{
	printf("quiver_image_view_set_transition_type not implemented yet!\n");
	imageview->priv->transition_type = type;
}

gdouble quiver_image_view_get_magnification(QuiverImageView *imageview)
{
	GtkWidget *widget;
	gdouble magnification;

	gint display_width,display_height;


	widget = GTK_WIDGET(imageview);

	switch (imageview->priv->view_mode)
	{
		case QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW:
		case QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW_STRETCH:
			magnification = 1.;
			if (NULL != imageview->priv->pixbuf)
			{
				quiver_image_view_get_pixbuf_display_size(imageview,&display_width,&display_height);
				magnification = display_width/(gdouble)imageview->priv->pixbuf_width;
			}
			break;
		case QUIVER_IMAGE_VIEW_MODE_ZOOM:
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

void quiver_image_view_set_magnification(QuiverImageView *imageview,gdouble new_mag)
{
	// restrict the zoom amount
	if (QUIVER_IMAGE_VIEW_MAG_MAX < new_mag)
	{
		new_mag = QUIVER_IMAGE_VIEW_MAG_MAX;
	}
	
	if (1 > new_mag)
	{
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
			if (mag_h< mag_w)
			{
				new_mag = mag_h;
			} 
		}
	}
	
	if (QUIVER_IMAGE_VIEW_MAGNIFICATION_MODE_SMOOTH == imageview->priv->magnification_mode)
	{
		imageview->priv->magnification_final = new_mag;
		quiver_image_view_add_magnification_timeout(imageview);
	}
	else
	{
		quiver_image_view_set_magnification_full(imageview,new_mag);
	}
}
static void quiver_image_view_set_magnification_full(QuiverImageView *imageview,gdouble new_mag)
{
	/*FIXME: we should CLAMP the input to a calculated size*/
	GtkWidget *widget;
	GdkRectangle rect;
	gint old_width,old_height, new_width,new_height;
	GdkModifierType mask;
	gint x,y;
	gdouble old_hadjust,old_vadjust,new_hadjust,new_vadjust;
	gdouble old_mag;

	quiver_image_view_get_pixbuf_display_size(imageview,&old_width,&old_height);

	old_mag = imageview->priv->magnification;

	if (old_mag < new_mag)
	{
		quiver_image_view_send_reload_event(imageview);
	}
	
	imageview->priv->magnification = new_mag;
	widget = GTK_WIDGET(imageview);

	gdk_window_get_pointer(widget->window,&x,&y,&mask);
	//printf("pointer position: %d x %d\n",x,y);


	old_hadjust = gtk_adjustment_get_value(imageview->priv->hadjustment);
	old_vadjust = gtk_adjustment_get_value(imageview->priv->vadjustment);


	// update size must be done befor the code that comes after it
	imageview->priv->scroll_draw   = FALSE;

	//printf("!!!!!UPDATE SIZE 1\n");
	quiver_image_view_update_size(imageview);
	//printf("!!!!!UPDATE SIZE 2\n");

	gint old_hpage_size = imageview->priv->hadjustment->page_size;
	gint old_vpage_size = imageview->priv->vadjustment->page_size;

	if (old_width < widget->allocation.width)
	{
		old_hpage_size = old_width;
		x = (x * old_width)/widget->allocation.width;
	}
	if (old_height < widget->allocation.height)
	{
		y = (y * old_height)/widget->allocation.height;
		old_vpage_size = old_height;
	}

	// we need to do several things:
	// update the scrollbar adjustments, update the scaled pixbuf, and invalidate
	quiver_image_view_get_pixbuf_display_size(imageview,&new_width,&new_height);

	if (new_width > widget->allocation.width)
	{
		// we will set the adjustment based on the pointer position.
		// if the pointer is not within the widget area, use middle!
		if (0 < x && x <= widget->allocation.width && 0 < y && y <= widget->allocation.height)
		{
			//set x as the next center point
			//new_hadjust = (old_hadjust + x) * (new_mag/old_mag) - imageview->priv->hadjustment->page_size/2.;
			new_hadjust = (old_hadjust + x) * (new_mag/old_mag) - x;
		}
		else
		{
			// set the center as the next centerpoint
			new_hadjust = (old_hadjust + old_hpage_size/2.) * (new_mag/old_mag) - imageview->priv->hadjustment->page_size/2.;
		}
		if (new_hadjust > imageview->priv->hadjustment->upper - imageview->priv->hadjustment->page_size)
			new_hadjust = MAX (0, imageview->priv->hadjustment->upper - imageview->priv->hadjustment->page_size);
		if (0 > new_hadjust)
			new_hadjust = 0;

		//printf("old new h: %d %d\n",(gint)old_hadjust,(gint)new_hadjust);
		gtk_adjustment_set_value(imageview->priv->hadjustment,new_hadjust);
	}
	else
	{
		gtk_adjustment_set_value(imageview->priv->hadjustment,0);
	}

	if (new_height > widget->allocation.height)
	{
		// we will set the adjustment based on the pointer position.
		// if the pointer is not within the widget area, use middle!
		if (0 < x && x <= widget->allocation.width && 0 < y && y <= widget->allocation.height)
		{
			//set x as the next center point
			//new_vadjust = (old_vadjust + y) * (new_mag/old_mag) - imageview->priv->vadjustment->page_size/2.;
			new_vadjust = (old_vadjust + y) * (new_mag/old_mag) -y;
		}
		else
		{
			// set the center as the next centerpoint
			//printf("old new v: %d %d\n",(gint)old_vadjust,(gint)new_vadjust);
			//printf("old page: %d\n",(gint)old_vpage_size);
			new_vadjust = (old_vadjust + old_vpage_size/2.) * (new_mag/old_mag) - imageview->priv->vadjustment->page_size/2.;
			//printf("old new v: %d %d\n",(gint)old_vadjust,(gint)new_vadjust);
		}
		if (new_vadjust > imageview->priv->vadjustment->upper - imageview->priv->vadjustment->page_size)
			new_vadjust = MAX (0, imageview->priv->vadjustment->upper - imageview->priv->vadjustment->page_size);
		if (0 > new_vadjust)
			new_vadjust = 0;

		//printf("old new v: %d %d\n",(gint)old_vadjust,(gint)new_vadjust);
		gtk_adjustment_set_value(imageview->priv->vadjustment,new_vadjust);
	}
	else
	{
		gtk_adjustment_set_value(imageview->priv->vadjustment,0);
	}
	quiver_image_view_create_scaled_pixbuf(imageview,GDK_INTERP_NEAREST);
	quiver_image_view_add_scale_hq_timeout(imageview);
	imageview->priv->scroll_draw   = TRUE;

	rect.x = 0;
	rect.y = 0;
	rect.width = widget->allocation.width;
	rect.height = widget->allocation.height;
	
	if (GTK_WIDGET_MAPPED(widget))
	{
		// FIXME: probably dont need to invalidate whole window
		gdk_window_invalidate_rect(widget->window,&rect,FALSE);
	}
}

void quiver_image_view_set_magnification_mode(QuiverImageView *imageview,QuiverImageViewMagnificationMode mode)
{
	imageview->priv->magnification_mode = mode;	
}


void quiver_image_view_rotate(QuiverImageView *imageview, gboolean clockwise)
{
	GdkPixbuf * pixbuf_rotated = NULL;
	
	if (NULL == imageview->priv->pixbuf)
		return;

	if (clockwise)
	{
		pixbuf_rotated = gdk_pixbuf_rotate_simple(imageview->priv->pixbuf,GDK_PIXBUF_ROTATE_CLOCKWISE);
	}
	else
	{
		pixbuf_rotated = gdk_pixbuf_rotate_simple(imageview->priv->pixbuf,GDK_PIXBUF_ROTATE_COUNTERCLOCKWISE);
	}
	if (NULL != pixbuf_rotated)
	{
		quiver_image_view_set_pixbuf_at_size(imageview,pixbuf_rotated,imageview->priv->pixbuf_height,imageview->priv->pixbuf_width);
		g_object_unref(pixbuf_rotated);
	}
}

void quiver_image_view_flip(QuiverImageView *imageview, gboolean horizontal)
{
	GdkPixbuf * pixbuf_flipped = NULL;

	if (NULL == imageview->priv->pixbuf)
		return;

	if (horizontal)
	{
		pixbuf_flipped = gdk_pixbuf_flip(imageview->priv->pixbuf,TRUE);
	}
	else
	{
		pixbuf_flipped = gdk_pixbuf_flip(imageview->priv->pixbuf,FALSE);
	}
	
	if (NULL != pixbuf_flipped)
	{
		quiver_image_view_set_pixbuf_at_size(imageview,pixbuf_flipped,imageview->priv->pixbuf_width,imageview->priv->pixbuf_height);
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
	
	if (NULL != imageview->priv->pixbuf_animation_iter)
	{
		g_object_unref(imageview->priv->pixbuf_animation_iter);
		imageview->priv->pixbuf_animation_iter = NULL;
	}

	//quiver_image_view_set_pixbuf(imageview,pixbuf);
	if (NULL != imageview->priv->pixbuf_animation)
	{
		//printf("ref count %d anim\n",G_OBJECT(imageview->priv->pixbuf_animation)->ref_count );
		g_object_unref(imageview->priv->pixbuf_animation);
		imageview->priv->pixbuf_animation = NULL;
	}
	
	
	if (NULL != imageview->priv->pixbuf_scaled)
	{
		//printf("ref count %d scaled\n",G_OBJECT(imageview->priv->pixbuf_scaled)->ref_count );
		g_object_unref(imageview->priv->pixbuf_scaled);
		imageview->priv->pixbuf_scaled = NULL;
	}

	if (NULL != imageview->priv->pixbuf)
	{
		//printf("ref count %d pixbuf\n",G_OBJECT(imageview->priv->pixbuf)->ref_count );
		quiver_image_view_invalidate_old_image_area(imageview,new_width,new_height);
		g_object_unref(imageview->priv->pixbuf);
		imageview->priv->pixbuf = NULL;
	}
	
	imageview->priv->reload_event_sent = FALSE;


	
	/*
	if (NULL == imageview->priv->pixbuf_scaled)
	{
		if (NULL != imageview->priv->pixbuf)
		{
			imageview->priv->transition_pixbuf_old = imageview->priv->pixbuf;
			g_object_ref(imageview->priv->transition_pixbuf_old);
		}
	}
	else
	{
		imageview->priv->transition_pixbuf_old = imageview->priv->pixbuf_scaled;
		g_object_ref(imageview->priv->transition_pixbuf_old);
	}
	*/	
	
}

static void pixbuf_loader_size_prepared(GdkPixbufLoader *loader,gint width, gint height,gpointer userdata)
{
	GtkWidget *widget;
	QuiverImageView *imageview;
	GdkPixbufFormat *format;
	gboolean resize;
	gchar **mime_types;
	gchar **mime_type;
	
	guint new_width,new_height;

	resize = TRUE;
	imageview = (QuiverImageView*)userdata;
	
	quiver_image_view_reset_view_mode(imageview,FALSE);

	widget = GTK_WIDGET(imageview);

	if (!GTK_WIDGET_MAPPED(widget))
	{
		return;
	}

	format = gdk_pixbuf_loader_get_format(loader);
	
	mime_types = gdk_pixbuf_format_get_mime_types(format);
	mime_type = mime_types;

	while (NULL != *mime_type)
	{
		//printf("mime type: %s\n",*mime_type);
		if (0 == strcmp(*mime_type,"image/gif"))
		{
			resize = FALSE;
			break;
		}
		mime_type++;
	}

	g_strfreev(mime_types);

	imageview->priv->pixbuf_width_next = width;
	imageview->priv->pixbuf_height_next = height;

	if (!resize)
		return;
	
	switch (imageview->priv->view_mode)
	{
		case QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW:
		case QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW_STRETCH:
			new_width = width;
			new_height = height;
			quiver_image_view_get_bound_size(widget->allocation.width,widget->allocation.height,&new_width,&new_height,FALSE);
			gdk_pixbuf_loader_set_size(loader,new_width,new_height);
			break;
		case QUIVER_IMAGE_VIEW_MODE_ZOOM:
		case QUIVER_IMAGE_VIEW_MODE_ACTUAL_SIZE:
		default:
			break;
	}

	//printf("size prepared\n");
}
static void pixbuf_loader_area_prepared(GdkPixbufLoader *loader,gpointer userdata)
{
	QuiverImageView *imageview;
	GdkPixbuf *pixbuf;

	imageview = (QuiverImageView*)userdata;
	//pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);
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
	
	quiver_image_view_create_scaled_pixbuf(imageview,GDK_INTERP_NEAREST);

	imageview->priv->scroll_draw = FALSE;

	quiver_image_view_update_size(imageview);
	
	gtk_adjustment_set_value(imageview->priv->hadjustment,0);
	gtk_adjustment_set_value(imageview->priv->vadjustment,0);
	
	imageview->priv->scroll_draw = TRUE;
	
	imageview->priv->area_updated = TRUE;

}
static void pixbuf_loader_area_updated (GdkPixbufLoader *loader,gint x, gint y, gint width,gint height,gpointer userdata)
{
	GtkWidget *widget;
	QuiverImageView *imageview;
	GdkRectangle rect;
	GdkPixbufAnimation* pixbuf_animation;
	gint dw,dh,aw,ah;

	imageview = (QuiverImageView*)userdata;
	widget = GTK_WIDGET(imageview);
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
			quiver_image_view_timeout_animation((gpointer)imageview);
			*/
			//quiver_image_view_add_animation_timeout(imageview);
		}

		//printf("x,y,w,h: %d %d %d %d\n",x,y,width,height);
		printf("%d=%d %d=%d\n",dw, width, dh,height);

		quiver_image_view_invalidate_image_area(imageview,&rect);
		imageview->priv->area_updated = TRUE;
		//printf("area updated\n");
	}
	else
	{
		// we are displaying the image at a different size than the loader is returning
		// printf("%d %d %d %d\n",aw, ah, dw,dh);
	}


	//quiver_image_view_create_scaled_pixbuf(imageview,GDK_INTERP_BILINEAR);
	//gdk_window_invalidate_rect(widget->window,&rect,FALSE);

}
static void pixbuf_loader_closed(GdkPixbufLoader *loader,gpointer userdata)
{	

	GtkWidget *widget;
	QuiverImageView *imageview;
	GdkPixbufAnimation* pixbuf_animation;

	imageview = (QuiverImageView*)userdata;
	widget = GTK_WIDGET(imageview);

	pixbuf_animation = imageview->priv->pixbuf_animation;
	
	if (NULL != pixbuf_animation)
	{
		
		if (!imageview->priv->area_updated)
		{
			printf("area not updated!\n");
	
			if (gdk_pixbuf_animation_is_static_image(pixbuf_animation))
			{
				//printf("->not an animation!\n");
				quiver_image_view_create_scaled_pixbuf(imageview,GDK_INTERP_BILINEAR);
				quiver_image_view_invalidate_image_area(imageview,NULL);
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
	
		if (gdk_pixbuf_animation_is_static_image(imageview->priv->pixbuf_animation))
		{
			/* FIXME : This is a workaround for a bug in the loader 
			 * for static images, it returns a ref count of 1, but for
			 * animations it returns 2, so we dont need to unref if it is static 
			 * just set it to null 
			 */
		}
	}
//	printf("closed\n");

}
