#include <config.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include "quiver-icon-view.h"
#include "quiver-marshallers.h"
#include <math.h>
#include <stdlib.h>
#include <sys/time.h>
#include "quiver-pixbuf-utils.h"
#include <glib.h>
//#include "gtkintl.h"


#define QUIVER_ICON_VIEW_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), QUIVER_TYPE_ICON_VIEW, QuiverIconViewPrivate))

/* set up some defaults */
#define QUIVER_ICON_VIEW_ICON_WIDTH              128
#define QUIVER_ICON_VIEW_ICON_HEIGHT             128
#define QUIVER_ICON_VIEW_CELL_PADDING            8 
#define QUIVER_ICON_VIEW_ICON_SHADOW_SIZE        5
#define QUIVER_ICON_VIEW_ICON_BORDER_SIZE        1

#define SMOOTH_SCROLL_TIMEOUT                    35 // 35 ms ~= 28fps

#define QUIVER_PARAM_READWRITE G_PARAM_READWRITE|G_PARAM_STATIC_NAME|G_PARAM_STATIC_NICK|G_PARAM_STATIC_BLURB

G_DEFINE_TYPE_WITH_CODE(QuiverIconView,quiver_icon_view,GTK_TYPE_WIDGET,G_IMPLEMENT_INTERFACE(GTK_TYPE_SCROLLABLE,NULL));

#if (GLIB_MAJOR_VERSION < 2) || (GLIB_MAJOR_VERSION == 2 && GLIB_MINOR_VERSION < 10)
#define g_object_ref_sink(o) G_STMT_START{	\
	  g_object_ref (o);				\
	  gtk_object_sink ((GtkObject*)o);		\
}G_STMT_END
#endif
/* start private data structures */
typedef struct _CellItem CellItem;
struct _CellItem
{
	gboolean selected;
	
};

/* signals */
enum {
	SIGNAL_CELL_CLICKED,
	SIGNAL_CELL_ACTIVATED,
	SIGNAL_CURSOR_CHANGED,
	SIGNAL_SELECTION_CHANGED,
	SIGNAL_COUNT
};

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
	gint hvelocity;
	gint vvelocity;
	gdouble time;
} VelocityTimeStruct;

struct _QuiverIconViewPrivate
{
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

	gboolean idle_load_running;
	QuiverIconViewScrollType scroll_type;
	gulong smooth_scroll_cell;
	gdouble smooth_scroll_hadjust;
	gdouble smooth_scroll_vadjust;
	
	QuiverIconViewDragBehavior drag_behavior;
	
	guint timeout_id_smooth_scroll;
	guint timeout_id_smooth_scroll_slowdown;

	guint n_columns;
	guint n_rows;
	
	/* callback functions and data */
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
};
/* end private data structures */


/* start private function prototypes */

static void      quiver_icon_view_realize        (GtkWidget *widget);

static void      quiver_icon_view_size_allocate  (GtkWidget     *widget,
                                             GtkAllocation *allocation);

static void      quiver_icon_view_size_request (GtkWidget *widget, GtkRequisition *requisition);
static void      quiver_icon_view_get_preferred_width (GtkWidget *widget, gint* min_width, gint* natural_width);
static void      quiver_icon_view_get_preferred_height (GtkWidget *widget, gint* min_height, gint* natural_height);

static void      quiver_icon_view_send_configure (QuiverIconView *iconview);

static gboolean  quiver_icon_view_configure_event (GtkWidget *widget,
                    GdkEventConfigure *event);
static gboolean  quiver_icon_view_draw(GtkWidget* widget, cairo_t* cr);
static gboolean  quiver_icon_view_button_press_event  (GtkWidget *widget,
                    GdkEventButton *event);

static gboolean  quiver_icon_view_button_release_event (GtkWidget *widget,
                    GdkEventButton *event);
static gboolean  quiver_icon_view_motion_notify_event (GtkWidget *widget,
                    GdkEventMotion *event);
static gboolean  quiver_icon_view_scroll_event ( GtkWidget *widget,
                    GdkEventScroll *event);
static gboolean  quiver_icon_view_key_press_event  (GtkWidget *widget,
                    GdkEventKey *event);
static gboolean  quiver_icon_view_leave_notify_event (GtkWidget *widget,
                    GdkEventCrossing *event);

static void      quiver_icon_view_set_hadjustment (QuiverIconView *iconview,
                    GtkAdjustment *hadjustment);
static void      quiver_icon_view_set_vadjustment (QuiverIconView *iconview,
                    GtkAdjustment *vadjustment);


static void      remove_timeout_smooth_scroll(QuiverIconView *iconview);

static void      quiver_icon_view_adjustment_value_changed (GtkAdjustment *adjustment,
                    QuiverIconView *iconview);

static void      quiver_icon_view_scroll_to_cell_smooth(QuiverIconView *iconview, gulong cell);
static void      quiver_icon_view_scroll_to_adjustment_smooth(QuiverIconView *iconview, gint hadjust, gint vadjust);

static gboolean  quiver_icon_view_smooth_scroll_step(QuiverIconView* iconview);
static gboolean  quiver_icon_view_timeout_smooth_scroll(gpointer data);
static gboolean  quiver_icon_view_timeout_smooth_scroll_slowdown(gpointer data);

static void      quiver_icon_view_style_set(GtkWidget *widget, GtkStyle *prev_style, gpointer data);

static void      quiver_icon_view_set_property (GObject *object,
                    guint               prop_id,
                    const GValue       *value,
                    GParamSpec         *pspec);
static void      quiver_icon_view_get_property (GObject    *object,
                    guint       prop_id,
                    GValue     *value,
                    GParamSpec *pspec);


static void     quiver_icon_view_finalize(GObject *object);

/* start utility function prototypes*/
static guint quiver_icon_view_get_width(QuiverIconView *iconview);
static guint quiver_icon_view_get_height(QuiverIconView *iconview);
static void
quiver_icon_view_set_adjustment_upper (GtkAdjustment *adj,
				 gdouble        upper,
				 gboolean       always_emit_changed);
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
/* end utility function prototypes*/

/* end private function prototypes */

/* start private globals */

static guint iconview_signals[SIGNAL_COUNT] = {0};

/* end private globals */


/* start private functions */
static void 
quiver_icon_view_class_init (QuiverIconViewClass *klass)
{
	GtkWidgetClass *widget_class;
	GObjectClass *obj_class;

	widget_class = GTK_WIDGET_CLASS (klass);
	obj_class = G_OBJECT_CLASS (klass);

	widget_class->realize              = quiver_icon_view_realize;
	widget_class->size_allocate        = quiver_icon_view_size_allocate;
	widget_class->get_preferred_width  = quiver_icon_view_get_preferred_width;
	widget_class->get_preferred_height = quiver_icon_view_get_preferred_height;
	widget_class->draw                 = quiver_icon_view_draw;
	widget_class->configure_event      = quiver_icon_view_configure_event;
	widget_class->button_press_event   = quiver_icon_view_button_press_event;
	widget_class->button_release_event = quiver_icon_view_button_release_event;
	widget_class->motion_notify_event  = quiver_icon_view_motion_notify_event;
	widget_class->scroll_event         = quiver_icon_view_scroll_event;
	widget_class->key_press_event      = quiver_icon_view_key_press_event;
	widget_class->leave_notify_event   = quiver_icon_view_leave_notify_event;

	obj_class->set_property            = quiver_icon_view_set_property;
	obj_class->get_property            = quiver_icon_view_get_property;

	/* Override properties */
	g_object_class_override_property (obj_class, PROP_HADJUSTMENT, "hadjustment");
	g_object_class_override_property (obj_class, PROP_VADJUSTMENT, "vadjustment");
	g_object_class_override_property (obj_class, PROP_HSCROLL_POLICY, "hscroll-policy");
	g_object_class_override_property (obj_class, PROP_VSCROLL_POLICY, "vscroll-policy");



	obj_class->finalize                = quiver_icon_view_finalize;

	g_type_class_add_private (obj_class, sizeof (QuiverIconViewPrivate));

	iconview_signals[SIGNAL_CELL_CLICKED] = g_signal_new (/*FIXME I_*/("cell_clicked"),
		G_TYPE_FROM_CLASS (obj_class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (QuiverIconViewClass, cell_clicked),
		NULL, NULL,
		g_cclosure_marshal_VOID__UINT,
		G_TYPE_NONE, 1,
		G_TYPE_UINT);

	iconview_signals[SIGNAL_CELL_ACTIVATED] = g_signal_new (/*FIXME I_*/("cell_activated"),
		G_TYPE_FROM_CLASS (obj_class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (QuiverIconViewClass, cell_activated),
		NULL, NULL,
		g_cclosure_marshal_VOID__UINT,
		G_TYPE_NONE, 1,
		G_TYPE_UINT);

	iconview_signals[SIGNAL_CURSOR_CHANGED] = g_signal_new (/*FIXME I_*/("cursor_changed"),
		G_TYPE_FROM_CLASS (obj_class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (QuiverIconViewClass, cursor_changed),
		NULL, NULL,
		g_cclosure_marshal_VOID__UINT,
		G_TYPE_NONE, 1,
		G_TYPE_UINT);

	iconview_signals[SIGNAL_SELECTION_CHANGED] = g_signal_new (/*FIXME I_*/("selection_changed"),
		G_TYPE_FROM_CLASS (obj_class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (QuiverIconViewClass, selection_changed),
		NULL, NULL,
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE,0);

#define GTK_PARAM_READABLE G_PARAM_READABLE|G_PARAM_STATIC_NAME|G_PARAM_STATIC_NICK|G_PARAM_STATIC_BLURB

	/* Style properties */
	// FIXME -  P_ for last two strings
	gtk_widget_class_install_style_property (widget_class,
		g_param_spec_boxed ("selection-box-color",
		("Selection Box Color"),
		("Color of the selection box"),
		GDK_TYPE_COLOR,
		GTK_PARAM_READABLE));

	gtk_widget_class_install_style_property (widget_class,
		g_param_spec_uchar ("selection-box-alpha",
		("Selection Box Alpha"),
		("Opacity of the selection box"),
		0, 0xff,
		0x40,
		GTK_PARAM_READABLE));
		
	/*

	g_object_class_install_property (obj_class,
		PROP_N_ITEMS,
		g_param_spec_boxed ("n-items-closure",
		P_("Number of Items Closure"),
		P_("The closure to get the number of items in the iconview"),
		G_TYPE_CLOSURE,
		QUIVER_PARAM_READWRITE));
	g_object_class_install_property (obj_class,
		PROP_THUMBNAIL_PIXBUF,
		g_param_spec_boxed ("thumbnail-pixbuf-closure",
		P_("The nth items pixbuf Closure"),
		P_("The closure to get the thumbnail pixbuf for the nth item in the iconview"),
		G_TYPE_CLOSURE,
		QUIVER_PARAM_READWRITE));
	g_object_class_install_property (obj_class,
		PROP_ICON_PIXBUF,
		g_param_spec_boxed ("icon-pixbuf-closure",
		P_("The nth items icon pixbuf Closure"),
		P_("The closure to get the icon pixbuf for the nth item in the iconview"),
		G_TYPE_CLOSURE,
		QUIVER_PARAM_READWRITE));
	g_object_class_install_property (obj_class,
		PROP_TEXT,
		g_param_spec_boxed ("text-closure",
		P_("The nth items text Closure"),
		P_("The closure to get the text for the nth item in the iconview"),
		G_TYPE_CLOSURE,
		QUIVER_PARAM_READWRITE));
		*/
}

static GtkAdjustment *
new_default_adjustment (void)
{
  return GTK_ADJUSTMENT (gtk_adjustment_new (0.0, 0.0, 0.0, 0.0, 0.0, 0.0));
}

static void 
quiver_icon_view_init(QuiverIconView *iconview)
{
	iconview->priv = QUIVER_ICON_VIEW_GET_PRIVATE(iconview);

	iconview->priv->callback_get_n_items = NULL;
	iconview->priv->callback_get_n_items_data = NULL;
	iconview->priv->callback_get_n_items_data_destroy = NULL;

	iconview->priv->callback_get_icon_pixbuf = NULL;
	iconview->priv->callback_get_icon_pixbuf_data = NULL;
	iconview->priv->callback_get_icon_pixbuf_data_destroy = NULL;

	iconview->priv->callback_get_thumbnail_pixbuf = NULL;
	iconview->priv->callback_get_thumbnail_pixbuf_data = NULL;
	iconview->priv->callback_get_thumbnail_pixbuf_data_destroy = NULL;

	iconview->priv->callback_get_text = NULL;
	iconview->priv->callback_get_text_data = NULL;
	iconview->priv->callback_get_text_data_destroy = NULL;
	
	iconview->priv->callback_get_overlay_pixbuf = NULL;
	iconview->priv->callback_get_overlay_pixbuf_data = NULL;
	iconview->priv->callback_get_overlay_pixbuf_data_destroy = NULL;

	iconview->priv->hadjustment = NULL;
	iconview->priv->vadjustment = NULL;

	iconview->priv->last_hadjustment = 0.0;
	iconview->priv->last_vadjustment = 0.0;

	iconview->priv->icon_width   = QUIVER_ICON_VIEW_ICON_WIDTH;
	iconview->priv->icon_height  = QUIVER_ICON_VIEW_ICON_HEIGHT;
	iconview->priv->icon_border_size  = QUIVER_ICON_VIEW_ICON_BORDER_SIZE;
	iconview->priv->cell_padding = QUIVER_ICON_VIEW_CELL_PADDING;

	iconview->priv->scroll_draw   = TRUE;
	iconview->priv->scroll_type = QUIVER_ICON_VIEW_SCROLL_NORMAL;
	iconview->priv->smooth_scroll_cell = G_MAXULONG;
	iconview->priv->smooth_scroll_hadjust = 0.;
	iconview->priv->smooth_scroll_vadjust = 0.;
	
	iconview->priv->drag_behavior = QUIVER_ICON_VIEW_DRAG_BEHAVIOR_RUBBER_BAND;
	
	iconview->priv->rubberband_x1 = 0;
	iconview->priv->rubberband_y1 = 0;
	iconview->priv->rubberband_x2 = 0;
	iconview->priv->rubberband_y2 = 0;
	iconview->priv->start_x        = 0;
	iconview->priv->start_y        = 0;
	iconview->priv->last_x        = 0;
	iconview->priv->last_y        = 0;
	iconview->priv->drag_mode_start = FALSE;
	iconview->priv->drag_mode_enabled = FALSE;
	
	iconview->priv->timeout_id_rubberband_scroll = 0;
	
	iconview->priv->mouse_button_is_down = FALSE;

	iconview->priv->velocity_time_list = NULL;

	iconview->priv->cursor_cell = G_MAXULONG;
	iconview->priv->prelight_cell = G_MAXULONG;

	iconview->priv->timeout_id_smooth_scroll          = 0;
	iconview->priv->timeout_id_smooth_scroll_slowdown = 0;

	/* setting these to 0 lets the widget decide how many
	 * columns and rows to have
	 */
	iconview->priv->n_columns = 0;
	iconview->priv->n_rows = 0;

	/* the following item is for multiselect */
	iconview->priv->cursor_cell_first = G_MAXULONG;


	gtk_widget_set_can_focus(GTK_WIDGET(iconview),TRUE);
	//GTK_WIDGET_UNSET_FLAGS(iconview,GTK_DOUBLE_BUFFERED);
	
	//iconview->priv->cell_items = g_malloc0( (sizeof *iconview->priv->cell_items));
	iconview->priv->cell_items = (CellItem*)g_malloc0( sizeof(CellItem)*(iconview->priv->n_cell_items+1) );
	iconview->priv->n_cell_items = 0;

	//iconview->priv->cell_items[0].selected = TRUE;

	g_signal_connect (GTK_WIDGET(iconview), "style-set",
			G_CALLBACK (quiver_icon_view_style_set),
			NULL);
}

static void
quiver_icon_view_finalize(GObject *object)
{
	GObjectClass *parent;
	QuiverIconViewClass *klass; 
	QuiverIconView *iconview;

	iconview = QUIVER_ICON_VIEW(object);
	klass = QUIVER_ICON_VIEW_GET_CLASS(iconview);

	// remove timeout callbacks
	remove_timeout_smooth_scroll(iconview);

	if (iconview->priv->timeout_id_rubberband_scroll != 0)
	{
		g_source_remove (iconview->priv->timeout_id_rubberband_scroll);
		iconview->priv->timeout_id_rubberband_scroll = 0;
	}

	if ( 0 != iconview->priv->timeout_id_smooth_scroll_slowdown)
	{
		g_source_remove(iconview->priv->timeout_id_smooth_scroll_slowdown);
		iconview->priv->timeout_id_smooth_scroll_slowdown = 0;
	}

	g_list_foreach(iconview->priv->velocity_time_list, (GFunc)g_free, NULL);
	g_list_free(iconview->priv->velocity_time_list);
	iconview->priv->velocity_time_list = NULL;
	
	g_free (iconview->priv->cell_items);

	parent = g_type_class_peek_parent(klass);
	if (parent)
	{
		parent->finalize(object);
	}
}

static void
quiver_icon_view_realize (GtkWidget *widget)
{
	QuiverIconView *iconview;
	GdkWindowAttr attributes;
	gint attributes_mask;

	g_return_if_fail (QUIVER_IS_ICON_VIEW (widget));

	iconview = QUIVER_ICON_VIEW (widget);
	gtk_widget_set_realized(widget, TRUE);

	attributes.window_type = GDK_WINDOW_CHILD;
	attributes.x = gtk_widget_get_allocated_width(widget);
	attributes.y = gtk_widget_get_allocated_height(widget);
	attributes.width = gtk_widget_get_allocated_width(widget);
	attributes.height = gtk_widget_get_allocated_height(widget);
	attributes.wclass = GDK_INPUT_OUTPUT;
	attributes.visual = gtk_widget_get_visual (widget);
	// FIXME: colormap?
	//attributes.colormap = gtk_widget_get_colormap (widget);
	//attributes.event_mask = GDK_VISIBILITY_NOTIFY_MASK;
	attributes.event_mask = gtk_widget_get_events (widget) | 
					   GDK_EXPOSURE_MASK |
					   GDK_KEY_PRESS_MASK | GDK_KEY_RELEASE_MASK |
					   GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
					   GDK_POINTER_MOTION_MASK | GDK_POINTER_MOTION_HINT_MASK |
					   GDK_LEAVE_NOTIFY_MASK |
					   GDK_SCROLL_MASK
					   ;

	attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL; // FIXME: | GDK_WA_COLORMAP;

	gtk_widget_set_window(widget,gdk_window_new (gtk_widget_get_parent_window (widget), &attributes, attributes_mask));

	gdk_window_set_user_data(gtk_widget_get_window(widget), iconview);

	GtkStyleContext* context = gtk_widget_get_style_context(widget);
	gtk_style_context_set_background(context, gtk_widget_get_window(widget));

	quiver_icon_view_send_configure (QUIVER_ICON_VIEW (widget));
}

/*
static void
quiver_icon_view_unrealize(GtkWidget *widget)
{

}
*/

static void
quiver_icon_view_size_allocate (GtkWidget     *widget,
				GtkAllocation *allocation)
{
	g_return_if_fail (QUIVER_IS_ICON_VIEW (widget));
	g_return_if_fail (allocation != NULL);

	GtkAllocation orig = {0};
	QuiverIconView *iconview = QUIVER_ICON_VIEW(widget);

	gtk_widget_get_allocation(widget, &orig);

	// check if there was a change
	if (allocation->x == orig.x && allocation->y == orig.y &&
		allocation->width == orig.width && allocation->height == orig.height)
		return; // no change
	
	gtk_widget_set_allocation(widget, allocation);

	if (gtk_widget_get_realized (widget))
	{
		gdk_window_move_resize( gtk_widget_get_window((widget)),
			allocation->x, allocation->y,
			allocation->width, allocation->height);
	
		quiver_icon_view_send_configure (QUIVER_ICON_VIEW (widget));

		quiver_icon_view_update_icon_size(iconview);
		quiver_icon_view_scroll_to_cell_force_top(iconview,iconview->priv->cursor_cell,TRUE);

	}


}

static void
quiver_icon_view_size_request (GtkWidget *widget, GtkRequisition *requisition)
{
	QuiverIconView *iconview;
		
	iconview = QUIVER_ICON_VIEW(widget);
	requisition->width = quiver_icon_view_get_cell_width(iconview);
	requisition->height = quiver_icon_view_get_cell_height(iconview);

	if (0 != iconview->priv->n_columns)
	{
		requisition->width *= iconview->priv->n_columns;
	}

	if (0 != iconview->priv->n_rows)
	{
		requisition->height *= iconview->priv->n_rows;
	}
}

static void
quiver_icon_view_get_preferred_width (GtkWidget *widget, gint* min_width, gint* natural_width)
{
	GtkRequisition requisition = {0};
	quiver_icon_view_size_request(widget, &requisition);
	*min_width = *natural_width = requisition.width;
}
static void
quiver_icon_view_get_preferred_height (GtkWidget *widget, gint* min_height, gint* natural_height)
{
	GtkRequisition requisition = {0};
	quiver_icon_view_size_request(widget, &requisition);
	*min_height = *natural_height = requisition.height;
}

static void
quiver_icon_view_send_configure (QuiverIconView *iconview)
{
  GtkWidget *widget;
  GdkEvent *event = gdk_event_new (GDK_CONFIGURE);

  widget = GTK_WIDGET (iconview);

  event->configure.window = g_object_ref(gtk_widget_get_window(widget));
  event->configure.send_event = TRUE;
  event->configure.x = gtk_widget_get_allocated_width(widget);
  event->configure.y = gtk_widget_get_allocated_height(widget);
  event->configure.width = gtk_widget_get_allocated_width(widget);
  event->configure.height = gtk_widget_get_allocated_height(widget);
  
  gtk_widget_event (widget, event);
  gdk_event_free (event);
}

static gboolean
quiver_icon_view_configure_event( GtkWidget *widget, GdkEventConfigure *event )
{
	/* icon_view_update_size(widget); */
	return TRUE;
}

guint
quiver_icon_view_get_cell_width(QuiverIconView *iconview)
{
	return iconview->priv->icon_width + iconview->priv->cell_padding +
		QUIVER_ICON_VIEW_ICON_SHADOW_SIZE*2 + iconview->priv->icon_border_size*2 ;
}

guint
quiver_icon_view_get_cell_height(QuiverIconView *iconview)
{
	return iconview->priv->icon_height + iconview->priv->cell_padding +
		QUIVER_ICON_VIEW_ICON_SHADOW_SIZE*2 + iconview->priv->icon_border_size *2;
}

static void
quiver_icon_view_get_col_row_count(QuiverIconView *iconview,guint *cols, guint *rows)
{
	guint c,r;
	GtkWidget *widget;
	widget = GTK_WIDGET(iconview);

	gulong n_cells  = quiver_icon_view_get_n_items(iconview);
	guint cell_width = quiver_icon_view_get_cell_width(iconview);

	r = 0;
	
	c = gtk_widget_get_allocated_width(widget) / cell_width;
	
	if (0 == c)
		c = 1;

	if (n_cells)
		r = (n_cells-1) / c + 1;

	if (0 != iconview->priv->n_columns)
	{
		c = iconview->priv->n_columns;
		r = (n_cells-1) / c + 1;
	}
	else if (0 != iconview->priv->n_rows)
	{
		r = iconview->priv->n_rows;
		c = (n_cells-1) / r + 1;
	}

	if (NULL != cols)
	{
		*cols = c;
	}

	if (NULL != rows)
	{
		*rows = r;
	}
}

static void
quiver_icon_view_draw_icons (GtkWidget *widget, cairo_t* cr, cairo_rectangle_int_t r) // r  = invalid rect area
{
	QuiverIconView *iconview;
	iconview = QUIVER_ICON_VIEW(widget);
	
	guint cell_width = quiver_icon_view_get_cell_width(iconview);
	guint cell_height = quiver_icon_view_get_cell_height(iconview);


	guint vadjust = (guint)gtk_adjustment_get_value(iconview->priv->vadjustment);
	guint hadjust = (guint)gtk_adjustment_get_value(iconview->priv->hadjustment);

	/* initial cell offset calculations */

	guint padding = iconview->priv->cell_padding;

	gulong i = 0;
	gulong j = 0;

	guint num_cols,num_rows;
	quiver_icon_view_get_col_row_count(iconview,&num_cols,&num_rows);

	guint num_adj_cols = hadjust / cell_width;
	guint adj_offset_x = hadjust - num_adj_cols * cell_width;
	//guint num_adj_cells_x = num_adj_cols * num_rows;

	guint num_adj_rows = vadjust / cell_height;
	guint adj_offset_y = vadjust - num_adj_rows * cell_height;
	guint num_adj_cells_y = num_adj_rows * num_cols;

	guint row_start = (r.y + adj_offset_y) / cell_height;
	guint col_start = (r.x + adj_offset_x) / cell_width;
	// must subtract 1 from the calculated end column/row.
	guint row_end = (r.y + adj_offset_y + r.height -1)/ cell_height;
	guint col_end = (r.x + adj_offset_x + r.width -1) / cell_width;

	if (col_end >= num_cols)
	{
		col_end = 0;
		if (0 != num_cols)
		{
			col_end = num_cols - 1;
		}
	}

	GtkStateFlags state;
	guint bound_width = cell_width - padding;
	guint bound_height = cell_height - padding;

	cairo_rectangle(cr, r.x, r.y, r.width, r.height);
	cairo_save(cr);
	cairo_clip(cr);

	gulong n_cells  = quiver_icon_view_get_n_items(iconview);
	for (j=row_start;j <= row_end; j++)
	{
		for (i = col_start;i<= col_end; i++)
		{
			gulong current_cell = j * num_cols + i + num_adj_cells_y + num_adj_cols;


			guint x_cell_offset = i * cell_width;
			guint y_cell_offset = j * cell_height;

			x_cell_offset -= adj_offset_x;
			y_cell_offset -= adj_offset_y;

			if ( current_cell >= n_cells)
			{
				// passed the last cell
				continue;
			}

			state = gtk_widget_get_state_flags(widget);
			if ( iconview->priv->cell_items[current_cell].selected )
			{
				if (gtk_widget_has_focus(widget))
				{
					state = GTK_STATE_FLAG_SELECTED;
				}
				else
				{
					state = GTK_STATE_FLAG_ACTIVE;
				}

				GtkStyleContext* context = gtk_widget_get_style_context(widget);
				gtk_style_context_save(context);
				gtk_style_context_set_state(context, state);

				cairo_set_source_rgba (cr, 1., 1., .7, .1);
				gtk_render_background(context, cr, x_cell_offset+padding/2, y_cell_offset+padding/2, bound_width, bound_height);
				gtk_render_frame(context,cr, x_cell_offset+padding/2, y_cell_offset+padding/2, bound_width, bound_height);
				cairo_stroke(cr);
				gtk_style_context_restore(context);
			}
			else if (current_cell == iconview->priv->prelight_cell)
			{
				//state = GTK_STATE_PRELIGHT;
				//gdk_gc_copy(gc_clip,widget->style->bg_gc[state]);
				//gdk_gc_set_clip_region(gc_clip, in_region);
				//gdk_draw_rectangle gtk_widget_get_window((widget), gc_clip,TRUE,x_cell_offset+padding/2, y_cell_offset+padding/2, bound_width, bound_height);
				//cairo_set_line_width(cr, 1);
				//cairo_rectangle(cr, x_cell_offset+padding/2, y_cell_offset+padding/2, bound_width, bound_height);
				//cairo_stroke(cr);
			}

			if (gtk_widget_is_focus(widget) && current_cell == iconview->priv->cursor_cell)
			{
				gtk_render_focus(
					gtk_widget_get_style_context(widget),
					cr,
					x_cell_offset-1 + padding/2,
					y_cell_offset-1 + padding/2,
					bound_width+2,
					bound_height +2);//x_cell_offset, y_cell_offset, bound_width, bound_height);


			}

			gboolean stock = FALSE;

			gint aw = 0, ah = 0;
			GdkPixbuf *pixbuf = quiver_icon_view_get_thumbnail_pixbuf(iconview,current_cell, &aw, &ah);

			if (NULL == pixbuf)
			{
				pixbuf = quiver_icon_view_get_icon_pixbuf(iconview,current_cell);
				stock = TRUE;
			}

			if (NULL != pixbuf)
			{
				guint pixbuf_width,pixbuf_height;

				pixbuf_width = gdk_pixbuf_get_width(pixbuf);
				pixbuf_height = gdk_pixbuf_get_height(pixbuf);

				guint nw = aw;
				guint nh = ah;

				quiver_rect_get_bound_size(iconview->priv->icon_width,iconview->priv->icon_height,&nw,&nh,FALSE);

				if (!stock && (pixbuf_width != nw || pixbuf_height != nh ))
				{
					pixbuf_width = nw;
					pixbuf_height = nh;
					GdkPixbuf* newpixbuf = gdk_pixbuf_scale_simple (
										pixbuf,
										nw,
										nh,
										//GDK_INTERP_BILINEAR);
										GDK_INTERP_NEAREST);
					//printf("resized\n");
					g_object_unref(pixbuf);
					pixbuf = newpixbuf;
					//printf("adding: %d - %s\n",current_cell,f.GetFilePath().c_str());
					//pixbuf_cache.AddPixbuf(f.GetFilePath(),pixbuf);

				}

				int x_icon_offset = (cell_width - pixbuf_width) /2;
				int y_icon_offset = (cell_height - pixbuf_height) /2;

				if (current_cell == iconview->priv->prelight_cell)
				{
					GdkPixbuf *pixbuf2 = gdk_pixbuf_new(GDK_COLORSPACE_RGB,gdk_pixbuf_get_has_alpha(pixbuf),
							gdk_pixbuf_get_bits_per_sample(pixbuf),
							pixbuf_width,pixbuf_height);

					pixbuf_brighten (pixbuf, pixbuf2, 25);
					
					g_object_unref(pixbuf);
					pixbuf = pixbuf2;
				}
				
				cairo_save(cr);

				cairo_translate(cr, x_cell_offset + x_icon_offset, y_cell_offset + y_icon_offset);

				gdk_cairo_set_source_pixbuf(cr, pixbuf, 0, 0);
				cairo_pattern_t* src = cairo_get_source(cr);
				cairo_pattern_set_extend(src, CAIRO_EXTEND_REPEAT);

				cairo_rectangle(cr,0,0, pixbuf_width, pixbuf_height);

				cairo_fill(cr);

				cairo_restore(cr);

				g_object_unref(pixbuf);

				if (!stock)
				{
					guint border = iconview->priv->icon_border_size;
#ifndef QUIVER_MAEMO // FIXME: drop shadow algorithm is too slow for maemo
					GtkStateFlags state_flags = gtk_widget_get_state_flags(widget);
					quiver_icon_view_draw_drop_shadow(iconview, cr, state_flags, 
							x_cell_offset + x_icon_offset - border, 
							y_cell_offset + y_icon_offset - border, 
							pixbuf_width + border*2, 
							pixbuf_height + border *2);
#endif
					// draw a border around the thumbnail
					if (0 < border)
					{
						gint border_offset = ceill(border/2.);
						cairo_set_source_rgb (cr, 1, 1, 1);
						cairo_set_line_width(cr, border);
						cairo_rectangle(cr, 
							x_cell_offset + x_icon_offset - border_offset+.5,
							y_cell_offset + y_icon_offset - border_offset+.5,
							pixbuf_width + border,
							pixbuf_height + border);
						cairo_stroke(cr);
					}
				}

			}
			
			/* draw the overlay icons */
			guint k;
			for (k = 0; k < QUIVER_ICON_OVERLAY_COUNT; k++)
			{
				if (iconview->priv->callback_get_overlay_pixbuf)
				{
					GdkPixbuf *overlay = (*iconview->priv->callback_get_overlay_pixbuf)(iconview,
							current_cell, k,
							iconview->priv->callback_get_overlay_pixbuf_data);
							
					if (overlay)
					{
						if (current_cell != iconview->priv->cursor_cell)
						{
							GdkPixbuf*overlay2 = gdk_pixbuf_copy(overlay);
							if (current_cell != iconview->priv->prelight_cell)
								pixbuf_set_grayscale(overlay2);
							pixbuf_set_alpha(overlay2,64);
							g_object_unref(overlay);
							overlay = overlay2;
						}

						guint pixbuf_width,pixbuf_height;

						pixbuf_width = gdk_pixbuf_get_width(overlay);
						pixbuf_height = gdk_pixbuf_get_height(overlay);

						cairo_save(cr);

						cairo_rectangle(cr,x_cell_offset + padding/2 + 2 + 16*k ,y_cell_offset + padding/2 +2, pixbuf_width, pixbuf_height);
						cairo_clip(cr);

						gdk_cairo_set_source_pixbuf(cr, overlay, x_cell_offset + padding/2 + 2 + 16*k ,y_cell_offset + padding/2 +2);
						cairo_pattern_t* src = cairo_get_source(cr);
						cairo_pattern_set_extend(src, CAIRO_EXTEND_REPEAT);
						cairo_paint(cr);

						cairo_restore(cr);

						g_object_unref(overlay);
					}
				}
			}
					

			/* draw a cell number*/
			/*
			char cell_string[10];
			sprintf(cell_string,"%d",current_cell);
			//printf("%s, ",cell_string);

			PangoLayout *layout;
			PangoRectangle logical_rect;

			layout = pango_cairo_create_layout (cr);

			pango_layout_set_text (layout, cell_string, -1);
			pango_layout_get_pixel_extents (layout, NULL, &logical_rect);

			pango_cairo_update_layout(cr, layout);
			cairo_move_to(cr, x_cell_offset + 10, y_cell_offset + 10);
			pango_cairo_show_layout(cr, layout);

			g_object_unref(layout);
			*/


		}
	}
	
	
	if (iconview->priv->drag_mode_enabled && 
	    QUIVER_ICON_VIEW_DRAG_BEHAVIOR_RUBBER_BAND == iconview->priv->drag_behavior)
	{
		cairo_rectangle_int_t rub_rect = iconview->priv->rubberband_rect;
		rub_rect.x -= hadjust;
		rub_rect.y -= vadjust;
		
		cairo_rectangle(cr, rub_rect.x, rub_rect.y, rub_rect.width, rub_rect.height);
		cairo_save(cr);
		cairo_clip(cr);

		GtkStyleContext* context = gtk_widget_get_style_context(widget);

		gtk_style_context_save(context);
		gtk_style_context_add_class(context, GTK_STYLE_CLASS_RUBBERBAND);
		
		gtk_render_background(context, cr, rub_rect.x, rub_rect.y, rub_rect.width, rub_rect.height);
		gtk_render_frame(context, cr, rub_rect.x, rub_rect.y, rub_rect.width, rub_rect.height);

		gtk_style_context_restore(context);

		cairo_restore(cr);
	}

	cairo_restore(cr);

}


static gboolean quiver_icon_view_draw(GtkWidget* widget, cairo_t* cr)
{
	QuiverIconView *iconview;
	iconview = QUIVER_ICON_VIEW(widget);

	cairo_rectangle_list_t* rectangles = cairo_copy_clip_rectangle_list(cr);

	int rect_index = 0;
	for (rect_index = 0; rect_index < rectangles->num_rectangles; ++rect_index)
	{
		cairo_rectangle_int_t rect;

		rect.x =  rectangles->rectangles[rect_index].x;
		rect.y = rectangles->rectangles[rect_index].y;
		rect.width = rectangles->rectangles[rect_index].width;
		rect.height = rectangles->rectangles[rect_index].height;

		//gtk_render_background (gtk_widget_get_style_context(widget), cr, rect.x, rect.y, rect.width, rect.height);

		quiver_icon_view_draw_icons (iconview, cr, rect);

	}

	cairo_rectangle_list_destroy(rectangles);

	return TRUE;
}


static gboolean 
quiver_icon_view_button_press_event (GtkWidget *widget, 
                     GdkEventButton *event)
{
	QuiverIconView *iconview = QUIVER_ICON_VIEW(widget);

	gint x =0, y=0;
	x = (gint)event->x;
	y = (gint)event->y;

	iconview->priv->start_x = x;
	iconview->priv->start_y = y;

	iconview->priv->last_x = x;
	iconview->priv->last_y = y;
	
	gettimeofday(&iconview->priv->last_motion_time,NULL);
	
	g_list_foreach(iconview->priv->velocity_time_list, (GFunc)g_free, NULL);
	g_list_free(iconview->priv->velocity_time_list);
	iconview->priv->velocity_time_list = NULL;

	gint vadjust = (gint)gtk_adjustment_get_value(iconview->priv->vadjustment);
	gint hadjust = (gint)gtk_adjustment_get_value(iconview->priv->hadjustment);

	if (!gtk_widget_has_focus (widget))
	{
		gtk_widget_grab_focus (widget);
	}

	if (1 == event->button)
	{
		iconview->priv->mouse_button_is_down = TRUE;
	
		iconview->priv->rubberband_x1 = iconview->priv->rubberband_x2 = x + hadjust;
		iconview->priv->rubberband_y1 = iconview->priv->rubberband_y2 = y + vadjust;
	
		//int cell_x,cell_y;
		int cell = quiver_icon_view_get_cell_for_xy (iconview,x,y);
	

		if (cell != G_MAXULONG && GDK_2BUTTON_PRESS == event->type)
		{
			iconview->priv->mouse_button_is_down = FALSE;
			quiver_icon_view_activate_cell(iconview,cell);
		}			    
		else if (QUIVER_ICON_VIEW_DRAG_BEHAVIOR_SCROLL == iconview->priv->drag_behavior)
		{
			iconview->priv->drag_mode_start = TRUE;
			if ( 0 != iconview->priv->timeout_id_smooth_scroll_slowdown)
			{
				g_source_remove(iconview->priv->timeout_id_smooth_scroll_slowdown);
				iconview->priv->timeout_id_smooth_scroll_slowdown = 0;
			}
			gettimeofday(&iconview->priv->last_motion_time, NULL);
		}
		else if (cell == G_MAXULONG &&
		    QUIVER_ICON_VIEW_DRAG_BEHAVIOR_RUBBER_BAND == iconview->priv->drag_behavior)
		{
			if (0 == (event->state & GDK_CONTROL_MASK))
			{
				quiver_icon_view_set_select_all(iconview,FALSE);
			}
			iconview->priv->drag_mode_start = TRUE;
		}
	
	}

	return FALSE;
}

static gboolean 
quiver_icon_view_button_release_event (GtkWidget *widget, 
         GdkEventButton *event)
{
	QuiverIconView *iconview = QUIVER_ICON_VIEW(widget);

	gint x =0, y=0;
	x = (gint)event->x;
	y = (gint)event->y;
	
	gint vadjust = (gint)gtk_adjustment_get_value(iconview->priv->vadjustment);
	gint hadjust = (gint)gtk_adjustment_get_value(iconview->priv->hadjustment);

	iconview->priv->rubberband_x1 = iconview->priv->rubberband_x2 = x + hadjust;
	iconview->priv->rubberband_y1 = iconview->priv->rubberband_y2 = y + vadjust;

	if (1 == event->button)
	{
		gulong cell = quiver_icon_view_get_cell_for_xy (iconview,x,y);
	
		// drag mode enabled means mouse has
		// moved while button is down
		if (iconview->priv->drag_mode_enabled)
		{
			if (QUIVER_ICON_VIEW_DRAG_BEHAVIOR_RUBBER_BAND == iconview->priv->drag_behavior)
			{
#ifdef QUIVER_MAEMO
				cairo_region_t *invalid_region;
				cairo_region_t *tmp_region;
		
				tmp_region = gdk_region_rectangle(&iconview->priv->rubberband_rect);
				gdk_region_shrink(tmp_region,1,1);
		
				invalid_region = gdk_region_rectangle(&iconview->priv->rubberband_rect);
		
				gdk_region_subtract(invalid_region,tmp_region);
				gdk_region_shrink(invalid_region,-1,-1);
		
				gtk_widget_get_window(gdk_window_invalidate_region(widget),invalid_region,FALSE);
				gdk_region_destroy(invalid_region);
				gdk_region_destroy(tmp_region);
#else
				cairo_rectangle_int_t invalid_rect = iconview->priv->rubberband_rect;
				invalid_rect.x -= 1;
				invalid_rect.y -= 1;
				invalid_rect.width += 2;
				invalid_rect.height += 2;
				cairo_region_t* invalid_region = cairo_region_create_rectangle(&invalid_rect);

				cairo_region_translate(invalid_region,-hadjust,-vadjust);
	
				gdk_window_invalidate_region(gtk_widget_get_window(widget),invalid_region,FALSE);
				cairo_region_destroy(invalid_region);
#endif			
				if (iconview->priv->timeout_id_rubberband_scroll != 0)
				{
					g_source_remove (iconview->priv->timeout_id_rubberband_scroll);
					iconview->priv->timeout_id_rubberband_scroll = 0;
				}
			}
			else if (QUIVER_ICON_VIEW_DRAG_BEHAVIOR_SCROLL == iconview->priv->drag_behavior)
			{
				struct timeval new_motion_time = {0};
				gettimeofday(&new_motion_time,NULL);

				gdouble old_time = (gdouble)iconview->priv->last_motion_time.tv_sec + ((gdouble)iconview->priv->last_motion_time.tv_usec)/1000000;
				gdouble new_time = (gdouble)new_motion_time.tv_sec + ((gdouble)new_motion_time.tv_usec)/1000000;
				
				if ( 3 == g_list_length(iconview->priv->velocity_time_list) &&
					(0.1 > new_time - old_time) )
				{
					remove_timeout_smooth_scroll(iconview);

					iconview->priv->timeout_id_smooth_scroll_slowdown = 
						g_timeout_add(SMOOTH_SCROLL_TIMEOUT,quiver_icon_view_timeout_smooth_scroll_slowdown,iconview);
				}
			}
		}
		else if (G_MAXULONG != cell && iconview->priv->mouse_button_is_down)
		{
			if (cell == iconview->priv->cursor_cell)
			{
				quiver_icon_view_click_cell(iconview, cell);
			}
			quiver_icon_view_set_cursor_cell_full(iconview,cell,(GdkModifierType)event->state,TRUE);
		}
		iconview->priv->mouse_button_is_down = FALSE;
		iconview->priv->drag_mode_start = FALSE;
		iconview->priv->drag_mode_enabled = FALSE;

	}

	return FALSE;

}

static gboolean
rubberband_scroll_timeout (gpointer data)
{
	QuiverIconView *iconview;
	gdouble xvalue,yvalue;
	
	gdk_threads_enter ();
	iconview = data;
	
	xvalue = MIN(gtk_adjustment_get_value(iconview->priv->hadjustment) +
		iconview->priv->rubberband_scroll_x,
		gtk_adjustment_get_upper(iconview->priv->hadjustment) -
		gtk_adjustment_get_page_size(iconview->priv->hadjustment));

	yvalue = MIN(gtk_adjustment_get_value(iconview->priv->vadjustment) +
		iconview->priv->rubberband_scroll_y,
		gtk_adjustment_get_upper(iconview->priv->vadjustment) -
		gtk_adjustment_get_page_size(iconview->priv->vadjustment));
	
	gtk_adjustment_set_value (iconview->priv->hadjustment,xvalue);
	gtk_adjustment_set_value (iconview->priv->vadjustment, yvalue);
	
	quiver_icon_view_update_rubber_band (iconview);
	  
	gdk_threads_leave ();
	return TRUE;
}


static gboolean
quiver_icon_view_motion_notify_event (GtkWidget *widget,
            GdkEventMotion *event)
{
	QuiverIconView *iconview = QUIVER_ICON_VIEW(widget);

	GdkModifierType state;
	gint x =0, y=0;

	if (event->is_hint)
	{
		gdk_window_get_pointer(event->window, &x, &y, &state);
	}		
	else
	{
		x = (gint)event->x;
		y = (gint)event->y;
		state = (GdkModifierType)event->state;
		
	}

	if (iconview->priv->drag_mode_start && 
		(10 < abs(x -iconview->priv->start_x) || 10 < abs(y -iconview->priv->start_y)))
	{
		iconview->priv->drag_mode_start = FALSE;
		iconview->priv->drag_mode_enabled = TRUE;
	}

	gulong new_cell = quiver_icon_view_get_cell_for_xy (iconview,x,y);

	if (iconview->priv->prelight_cell != new_cell)
	{
		if (G_MAXULONG != iconview->priv->prelight_cell)
		{
			quiver_icon_view_invalidate_cell(iconview,iconview->priv->prelight_cell);
		}
		iconview->priv->prelight_cell = new_cell;
		if (G_MAXULONG != iconview->priv->prelight_cell)
		{
			quiver_icon_view_invalidate_cell(iconview,iconview->priv->prelight_cell);
			if (iconview->priv->callback_get_text)
			{
				//gchar *text = (*iconview->priv->callback_get_text)(iconview,iconview->priv->prelight_cell,iconview->priv->callback_get_text_data);
				//printf("Filename: %s\n",text);
				//g_free(text);
			}
		}
	}


	//printf("new_cell : %d \n",new_cell);

	if (iconview->priv->drag_mode_enabled && 
		QUIVER_ICON_VIEW_DRAG_BEHAVIOR_RUBBER_BAND == iconview->priv->drag_behavior)
	{
		quiver_icon_view_update_rubber_band(iconview);
		iconview->priv->rubberband_scroll_x = 0;
		iconview->priv->rubberband_scroll_y = 0;
		
		/* check if the window needs to scroll */
		if (x < 0 || x > gtk_widget_get_allocated_width(widget))
		{
			if (x < 0)
				iconview->priv->rubberband_scroll_x = x;
			else
				iconview->priv->rubberband_scroll_x = x - gtk_widget_get_allocated_width(widget);
	
			if (iconview->priv->timeout_id_rubberband_scroll == 0)
				iconview->priv->timeout_id_rubberband_scroll = g_timeout_add (30, rubberband_scroll_timeout, 
								iconview);
		}
		else
		{ 
			if (iconview->priv->timeout_id_rubberband_scroll != 0)
			{
				g_source_remove (iconview->priv->timeout_id_rubberband_scroll);
				iconview->priv->timeout_id_rubberband_scroll = 0;
			}
		}

		/* check if the window needs to scroll */
		if (y < 0 || y > gtk_widget_get_allocated_height(widget))
		{
			if (y < 0)
				iconview->priv->rubberband_scroll_y = y;
			else
				iconview->priv->rubberband_scroll_y = y - gtk_widget_get_allocated_height(widget);
	
			if (iconview->priv->timeout_id_rubberband_scroll == 0)
				iconview->priv->timeout_id_rubberband_scroll = g_timeout_add (30, rubberband_scroll_timeout, 
								iconview);
		}
		else
		{ 
			if (iconview->priv->timeout_id_rubberband_scroll != 0)
			{
				g_source_remove (iconview->priv->timeout_id_rubberband_scroll);
				iconview->priv->timeout_id_rubberband_scroll = 0;
			}
		}

	}
	else if (iconview->priv->drag_mode_enabled && 
		QUIVER_ICON_VIEW_DRAG_BEHAVIOR_SCROLL == iconview->priv->drag_behavior)
	{
		
		struct timeval new_motion_time = {0};
		gettimeofday(&new_motion_time,NULL);
		gdouble old_time = (gdouble)iconview->priv->last_motion_time.tv_sec + ((gdouble)iconview->priv->last_motion_time.tv_usec)/1000000;
		gdouble new_time = (gdouble)new_motion_time.tv_sec + ((gdouble)new_motion_time.tv_usec)/1000000;
		
		// velocity pixels per second
		// max velocity should be around +/-12000 pps
#define MAX_VELOCITY 12000
		VelocityTimeStruct* vt = g_malloc(sizeof(VelocityTimeStruct));
		
		vt->time = new_time - old_time;
		vt->hvelocity = (gint)((x - iconview->priv->last_x) / vt->time);
		vt->hvelocity = MIN(MAX_VELOCITY,vt->hvelocity);
		vt->hvelocity = MAX(-MAX_VELOCITY,vt->hvelocity);
		
		vt->vvelocity =  (gint)((y - iconview->priv->last_y) / vt->time);
		vt->vvelocity = MIN(MAX_VELOCITY,vt->vvelocity);
		vt->vvelocity = MAX(-MAX_VELOCITY,vt->vvelocity);		

		if ( 3 == g_list_length(iconview->priv->velocity_time_list) )
		{
			GList* last = g_list_last(iconview->priv->velocity_time_list);
			iconview->priv->velocity_time_list = 
				g_list_remove_link(iconview->priv->velocity_time_list,last);	
		}
		iconview->priv->velocity_time_list = 
			g_list_prepend(iconview->priv->velocity_time_list, vt);
		
		
		iconview->priv->last_motion_time = new_motion_time;
		
		gdouble hadjust = gtk_adjustment_get_value(iconview->priv->hadjustment);
		gdouble vadjust = gtk_adjustment_get_value(iconview->priv->vadjustment);
		
		iconview->priv->rubberband_x2 = x + hadjust;
		iconview->priv->rubberband_y2 = y + vadjust;
	
		hadjust += iconview->priv->rubberband_x1 - iconview->priv->rubberband_x2;
		hadjust = MAX(0,MIN(gtk_adjustment_get_upper(iconview->priv->hadjustment) - gtk_adjustment_get_page_size(iconview->priv->hadjustment),hadjust));
		vadjust += iconview->priv->rubberband_y1 - iconview->priv->rubberband_y2;
		vadjust = MAX(0,MIN(gtk_adjustment_get_upper(iconview->priv->vadjustment) - gtk_adjustment_get_page_size(iconview->priv->vadjustment),vadjust));
		gtk_adjustment_set_value(iconview->priv->hadjustment,hadjust);
		gtk_adjustment_set_value(iconview->priv->vadjustment,vadjust);

		iconview->priv->rubberband_x1 = x + hadjust;
		iconview->priv->rubberband_y1 = y + vadjust;
	}
	
	iconview->priv->last_x = x;
	iconview->priv->last_y = y;
		
	return FALSE;

}

gboolean quiver_icon_view_scroll_event ( GtkWidget *widget,
           GdkEventScroll *event)
{
	
	QuiverIconView *iconview;
	iconview = QUIVER_ICON_VIEW(widget);

	gint hadjust = (gint)gtk_adjustment_get_value(iconview->priv->hadjustment);
	gint vadjust = (gint)gtk_adjustment_get_value(iconview->priv->vadjustment);

	if (0 != iconview->priv->timeout_id_smooth_scroll &&
		iconview->priv->smooth_scroll_cell == G_MAXULONG)
	{
		hadjust = iconview->priv->smooth_scroll_hadjust;
		vadjust = iconview->priv->smooth_scroll_vadjust;
	}

	remove_timeout_smooth_scroll(iconview);

	if (1 == iconview->priv->n_rows)
	{
		if (GDK_SCROLL_UP == event->direction)
		{
			hadjust -= gtk_adjustment_get_step_increment(iconview->priv->hadjustment);
		}
		else if (GDK_SCROLL_DOWN == event->direction)
		{
			hadjust += gtk_adjustment_get_step_increment(iconview->priv->hadjustment);
		}

		if (hadjust < gtk_adjustment_get_lower(iconview->priv->hadjustment))
			hadjust = gtk_adjustment_get_lower(iconview->priv->hadjustment);
		else if (hadjust > gtk_adjustment_get_upper(iconview->priv->hadjustment) - gtk_adjustment_get_page_size(iconview->priv->hadjustment))
			hadjust = gtk_adjustment_get_upper(iconview->priv->hadjustment) - gtk_adjustment_get_page_size(iconview->priv->hadjustment);

	}
	else
	{
		if (GDK_SCROLL_UP == event->direction)
		{
			vadjust -= gtk_adjustment_get_step_increment(iconview->priv->vadjustment);
		}
		else if (GDK_SCROLL_DOWN == event->direction)
		{
			vadjust += gtk_adjustment_get_step_increment(iconview->priv->vadjustment);
		}

		if (vadjust < gtk_adjustment_get_lower(iconview->priv->vadjustment))
			vadjust = gtk_adjustment_get_lower(iconview->priv->vadjustment);
		else if (vadjust > gtk_adjustment_get_upper(iconview->priv->vadjustment) - gtk_adjustment_get_page_size(iconview->priv->vadjustment))
			vadjust = gtk_adjustment_get_upper(iconview->priv->vadjustment) - gtk_adjustment_get_page_size(iconview->priv->vadjustment);

	}

	if (QUIVER_ICON_VIEW_SCROLL_SMOOTH == iconview->priv->scroll_type ||
		QUIVER_ICON_VIEW_SCROLL_SMOOTH_CENTER == iconview->priv->scroll_type)
	{
		quiver_icon_view_scroll_to_adjustment_smooth(iconview, hadjust, vadjust);
	}
	else
	{
		gtk_adjustment_set_value(iconview->priv->hadjustment,hadjust);
		gtk_adjustment_set_value(iconview->priv->vadjustment,vadjust);
	}

	return TRUE;
}

static gboolean
quiver_icon_view_key_press_event  (GtkWidget *widget,
                   GdkEventKey *event)
{
	QuiverIconView *iconview;
	iconview = QUIVER_ICON_VIEW(widget);

	gulong n_cells  = quiver_icon_view_get_n_items(iconview);

	guint cols,rows;
	quiver_icon_view_get_col_row_count(iconview,&cols,&rows);
	gboolean rval = TRUE;


	//guint cell_width = quiver_icon_view_get_cell_width(iconview);
	guint cell_height = quiver_icon_view_get_cell_height(iconview);

	/* FIXME: we should not use the size from the widget but 
	 * rather have our own width value and use it */
	guint rows_per_page = gtk_widget_get_allocated_height(widget)/cell_height;
	if (rows_per_page == 0)
	{
		rows_per_page = 1;
	}
	guint n_cells_per_page = cols * rows_per_page;
	//printf("cols: %d ,rows %d, n cells %d\n",cols,rows_per_page,n_cells_per_page);

	gulong new_cursor_cell = iconview->priv->cursor_cell;
	
	switch(event->keyval)
	{
		case GDK_KEY_Return:
		case GDK_KEY_KP_Enter:
			quiver_icon_view_activate_cell(iconview,iconview->priv->cursor_cell);
			new_cursor_cell = iconview->priv->cursor_cell;
			break;
		case GDK_KEY_a:
		case GDK_KEY_A:
			if (event->state & GDK_CONTROL_MASK)
			{
				quiver_icon_view_set_select_all(iconview,TRUE);
			}
			break;
		case GDK_KEY_space:
			if (event->state & GDK_CONTROL_MASK)
			{
				iconview->priv->cell_items[iconview->priv->cursor_cell].selected = !iconview->priv->cell_items[iconview->priv->cursor_cell].selected;
				g_signal_emit(iconview,iconview_signals[SIGNAL_SELECTION_CHANGED],0);
				quiver_icon_view_invalidate_cell(iconview,iconview->priv->cursor_cell);
			}
			break;

		case GDK_KEY_KP_Left:
		case GDK_KEY_Left:
			//printf("left %d\n",cursor_cell);
			new_cursor_cell = iconview->priv->cursor_cell-1;
			break;
		case GDK_KEY_KP_Right:
		case GDK_KEY_Right:
			//printf("right %d\n",cursor_cell);
			new_cursor_cell = iconview->priv->cursor_cell+1;
			break;
		case GDK_KEY_KP_Up:
		case GDK_KEY_Up:
			//printf("up %d, %d\n",cursor_cell, cursor_cell-cols);
			new_cursor_cell = iconview->priv->cursor_cell - cols;
			break;
		case GDK_KEY_KP_Down:
		case GDK_KEY_Down:
			//printf("down %d\n",cursor_cell);
			new_cursor_cell = iconview->priv->cursor_cell + cols;
			break;
		case GDK_KEY_Home:
			new_cursor_cell = 0;
			break;
		case GDK_KEY_End:
			new_cursor_cell = n_cells -1;
			break;

		case GDK_KEY_Page_Up:
			{
				new_cursor_cell -= n_cells_per_page;
			}
			if (new_cursor_cell <= 0)
			{
				new_cursor_cell = 0;
			}
			break;
		case GDK_KEY_Page_Down:
			{
				new_cursor_cell += n_cells_per_page;
			}
			if (n_cells <= new_cursor_cell)
			{
				new_cursor_cell = n_cells -1;
			}
			break;

		default:
			rval = FALSE;
			break;

	}

	/*
	if (n_cells <= new_cursor_cell)
	{
		new_cursor_cell = n_cells -1;
	}

	if (new_cursor_cell <= 0)
	{
		new_cursor_cell = 0;
	}
	*/

	if (0 <= new_cursor_cell && new_cursor_cell < n_cells)
	{
		if (new_cursor_cell != iconview->priv->cursor_cell)
		{
			quiver_icon_view_set_cursor_cell_full(iconview,new_cursor_cell,(GdkModifierType)event->state,FALSE);
		}
	}
	else
	{
		new_cursor_cell = iconview->priv->cursor_cell;
		quiver_icon_view_set_cursor_cell_full(iconview,new_cursor_cell,(GdkModifierType)event->state,FALSE);
	}
	
	//iconview->priv->redraw_needed = TRUE;
	
  	if (!rval)
  		rval = GTK_WIDGET_CLASS (quiver_icon_view_parent_class)->key_press_event (widget, event);
	
	return rval;
}

static gboolean
quiver_icon_view_leave_notify_event (GtkWidget *widget,
       GdkEventCrossing *event)
{
	QuiverIconView *iconview;
	iconview = QUIVER_ICON_VIEW(widget);
	if (G_MAXULONG != iconview->priv->prelight_cell)
	{
		quiver_icon_view_invalidate_cell(iconview,iconview->priv->prelight_cell);
		iconview->priv->prelight_cell = G_MAXULONG;
	}
	return FALSE;
}

static void
quiver_icon_view_set_hadjustment (QuiverIconView *iconview, GtkAdjustment *adj)
{
	GtkWidget *widget;
	gboolean need_adjust = FALSE;

	g_return_if_fail (QUIVER_IS_ICON_VIEW (iconview));

	widget = GTK_WIDGET(iconview);

	if (adj)
		g_return_if_fail (GTK_IS_ADJUSTMENT (adj));

	if (iconview->priv->hadjustment && (iconview->priv->hadjustment != adj))
	{
		g_signal_handlers_disconnect_by_func (iconview->priv->hadjustment,
			quiver_icon_view_adjustment_value_changed,
			iconview);
		g_object_unref (iconview->priv->hadjustment);
		iconview->priv->hadjustment = NULL;
	}

	if (iconview->priv->hadjustment != adj)
	{
		iconview->priv->hadjustment = adj;
		g_object_ref_sink (iconview->priv->hadjustment);
	
		if (gtk_widget_get_realized (widget))
		{
			guint width = quiver_icon_view_get_width(iconview);
			quiver_icon_view_set_adjustment_upper (iconview->priv->hadjustment, width, FALSE);
			need_adjust = TRUE;
		}
		g_signal_connect (iconview->priv->hadjustment, "value_changed",
		G_CALLBACK (quiver_icon_view_adjustment_value_changed),
			iconview);
	}

	/* vadj or hadj can be NULL while constructing; don't emit a signal
	then */
	if (need_adjust && iconview->priv->vadjustment && iconview->priv->hadjustment)
		quiver_icon_view_adjustment_value_changed (NULL, iconview);

	g_object_notify(G_OBJECT(iconview), "hadjustment");
}

static void
quiver_icon_view_set_vadjustment (QuiverIconView *iconview, GtkAdjustment *adj)
{
	GtkWidget *widget;
	gboolean need_adjust = FALSE;

	g_return_if_fail (QUIVER_IS_ICON_VIEW (iconview));

	widget = GTK_WIDGET(iconview);

	if (adj)
		g_return_if_fail (GTK_IS_ADJUSTMENT (adj));

	if (iconview->priv->vadjustment && (iconview->priv->vadjustment != adj))
	{
		g_signal_handlers_disconnect_by_func (iconview->priv->vadjustment,
			quiver_icon_view_adjustment_value_changed,
			iconview);
		g_object_unref (iconview->priv->vadjustment);
		iconview->priv->vadjustment = NULL;
	}

	if (iconview->priv->vadjustment != adj)
	{
		iconview->priv->vadjustment = adj;
		g_object_ref_sink (iconview->priv->vadjustment);
	
		if (gtk_widget_get_realized (widget))
		{
			guint width = quiver_icon_view_get_width(iconview);
			quiver_icon_view_set_adjustment_upper (iconview->priv->vadjustment, width, FALSE);
			need_adjust = TRUE;
		}
		g_signal_connect (iconview->priv->vadjustment, "value_changed",
		G_CALLBACK (quiver_icon_view_adjustment_value_changed),
			iconview);
	}

	/* vadj or vadj can be NULL while constructing; don't emit a signal
	then */
	if (need_adjust && iconview->priv->vadjustment && iconview->priv->hadjustment)
		quiver_icon_view_adjustment_value_changed (NULL, iconview);

	g_object_notify(G_OBJECT(iconview), "vadjustment");
}


static void remove_timeout_smooth_scroll(QuiverIconView *iconview)
{
	if (0 != iconview->priv->timeout_id_smooth_scroll)
	{
		g_source_remove (iconview->priv->timeout_id_smooth_scroll);
		iconview->priv->timeout_id_smooth_scroll = 0;
		iconview->priv->smooth_scroll_cell = G_MAXULONG;
		iconview->priv->smooth_scroll_hadjust = 0.;
		iconview->priv->smooth_scroll_vadjust = 0.;
	}
}
		
static void
quiver_icon_view_scroll_to_adjustment_smooth(QuiverIconView *iconview, gint hadjust, gint vadjust)
{
	if ( 0 != iconview->priv->timeout_id_smooth_scroll_slowdown)
	{
		g_source_remove(iconview->priv->timeout_id_smooth_scroll_slowdown);
		iconview->priv->timeout_id_smooth_scroll_slowdown = 0;
	}

	iconview->priv->smooth_scroll_cell = G_MAXULONG;
	iconview->priv->smooth_scroll_hadjust = hadjust;
	iconview->priv->smooth_scroll_vadjust = vadjust;

	if (0 == iconview->priv->timeout_id_smooth_scroll)
	{
		iconview->priv->timeout_id_smooth_scroll = g_timeout_add(SMOOTH_SCROLL_TIMEOUT,quiver_icon_view_timeout_smooth_scroll,iconview);
	}

	quiver_icon_view_smooth_scroll_step(iconview);
}

static void quiver_icon_view_scroll_to_cell_smooth(QuiverIconView *iconview, gulong cell)
{
	if ( 0 != iconview->priv->timeout_id_smooth_scroll_slowdown)
	{
		g_source_remove(iconview->priv->timeout_id_smooth_scroll_slowdown);
		iconview->priv->timeout_id_smooth_scroll_slowdown = 0;
	}


	iconview->priv->smooth_scroll_cell = cell;

	if (0 == iconview->priv->timeout_id_smooth_scroll)
	{
		iconview->priv->timeout_id_smooth_scroll = g_timeout_add(SMOOTH_SCROLL_TIMEOUT,quiver_icon_view_timeout_smooth_scroll,iconview);
	}

	quiver_icon_view_smooth_scroll_step(iconview);
}

static gboolean
quiver_icon_view_smooth_scroll_step(QuiverIconView* iconview)
{
	gboolean hdone = FALSE; 
	gboolean vdone = FALSE; 
	
	gulong cell = iconview->priv->smooth_scroll_cell;

	gint hadjust = (guint)gtk_adjustment_get_value(iconview->priv->hadjustment);
	gint vadjust = (guint)gtk_adjustment_get_value(iconview->priv->vadjustment);
	gint new_hadjust = hadjust;
	gint new_vadjust = vadjust;

	if (G_MAXULONG == cell)
	{
		// scroll to hadjust/vadjust
		new_hadjust = iconview->priv->smooth_scroll_hadjust;
		new_vadjust = iconview->priv->smooth_scroll_vadjust;
	}
	else
	{
		guint cols,rows;
		quiver_icon_view_get_col_row_count(iconview,&cols,&rows);

		guint cell_width = quiver_icon_view_get_cell_width(iconview);
		guint cell_height = quiver_icon_view_get_cell_height(iconview);

		gulong cell_x = (cell % cols) * cell_width;
		gulong cell_y = (cell / cols) * cell_height;

		if (QUIVER_ICON_VIEW_SCROLL_SMOOTH_CENTER == iconview->priv->scroll_type)
		{
			new_hadjust = cell_x - gtk_adjustment_get_page_size(iconview->priv->hadjustment)/2 + cell_width/2;
			new_hadjust = MAX (0,new_hadjust);
			new_hadjust = MIN(new_hadjust, gtk_adjustment_get_upper(iconview->priv->hadjustment) - gtk_adjustment_get_page_size(iconview->priv->hadjustment));
			

			new_vadjust = cell_y - gtk_adjustment_get_page_size(iconview->priv->vadjustment)/2 + cell_height/2;
			new_vadjust = MAX (0,new_vadjust);
			new_vadjust = MIN(new_vadjust, gtk_adjustment_get_upper(iconview->priv->vadjustment) - gtk_adjustment_get_page_size(iconview->priv->vadjustment));

		}
		else if (QUIVER_ICON_VIEW_SCROLL_SMOOTH == iconview->priv->scroll_type)
		{
			/* horizontal adjustment */
			if (cell_x < hadjust)
			{
				new_hadjust = cell_x;
			}
			else if (cell_x > hadjust + gtk_adjustment_get_page_size(iconview->priv->hadjustment) - cell_width)
			{
				new_hadjust = cell_x + cell_width - gtk_adjustment_get_page_size(iconview->priv->hadjustment);
			}

			new_hadjust = MAX (0,new_hadjust);
			new_hadjust = MIN(new_hadjust, gtk_adjustment_get_upper(iconview->priv->hadjustment) - gtk_adjustment_get_page_size(iconview->priv->hadjustment));

			/* vertical adjustment */
			if (cell_y < vadjust)
			{
				new_vadjust = cell_y;
			}
			else if (cell_y > vadjust + gtk_adjustment_get_page_size(iconview->priv->vadjustment) - cell_height)
			{
				new_vadjust = cell_y + cell_height - gtk_adjustment_get_page_size(iconview->priv->vadjustment);
			}

			new_vadjust = MAX (0,new_vadjust);
			new_vadjust = MIN(new_vadjust, gtk_adjustment_get_upper(iconview->priv->vadjustment) - gtk_adjustment_get_page_size(iconview->priv->vadjustment));
		}
	}

	gint mid_hadjust = (new_hadjust + hadjust) / 2;
	if (mid_hadjust != new_hadjust)
	{
		if (1 == ABS(new_hadjust - mid_hadjust))
		{
			mid_hadjust = new_hadjust;
			hdone = TRUE;
		}
		gtk_adjustment_set_value(iconview->priv->hadjustment,mid_hadjust);
	}
	else
	{
		hdone = TRUE;
	}

	gint mid_vadjust = (new_vadjust + vadjust) / 2;
	if (vadjust != new_vadjust)
	{
		if (1 == ABS(new_vadjust - vadjust))
		{
			mid_vadjust = new_vadjust;
			vdone = TRUE;
		}
		gtk_adjustment_set_value(iconview->priv->vadjustment,mid_vadjust);
	}
	else
	{
		vdone = TRUE;
	}

	if (hdone && vdone)
	{
		remove_timeout_smooth_scroll(iconview);
		return FALSE;
	}
	return TRUE;
}

static gboolean 
quiver_icon_view_timeout_smooth_scroll(gpointer data)
{
	QuiverIconView *iconview = (QuiverIconView*)data;
	gboolean notdone = FALSE;

	gdk_threads_enter();
	notdone = quiver_icon_view_smooth_scroll_step(iconview);
	gdk_threads_leave();
	
	return notdone;
}

static gboolean
quiver_icon_view_timeout_smooth_scroll_slowdown(gpointer data)
{
	QuiverIconView *iconview = (QuiverIconView*)data;

	gdouble divider = 1.02; 	
	gdouble timeout_secs = SMOOTH_SCROLL_TIMEOUT / 1000.;

	gboolean hdone = FALSE;
	gboolean vdone = FALSE;

	gdk_threads_enter();
	
	GList* list_itr = g_list_first(iconview->priv->velocity_time_list);
	gint hvelocity_avg = 0;
	gint vvelocity_avg = 0;
	gdouble total_time = 0; 
	if (NULL != list_itr)
	{
		do
		{
			VelocityTimeStruct* vt = list_itr->data;
			hvelocity_avg += (gint)(vt->hvelocity * vt->time);
			vvelocity_avg += (gint)(vt->vvelocity * vt->time);
			total_time += vt->time;
			list_itr = g_list_next(list_itr);
		} while (NULL !=list_itr);
	}
	
	hvelocity_avg = (gint)(hvelocity_avg/total_time);
	vvelocity_avg = (gint)(vvelocity_avg/total_time);


	if (1 != g_list_length(iconview->priv->velocity_time_list) )
	{
		g_list_foreach(iconview->priv->velocity_time_list, (GFunc)g_free, NULL);
		g_list_free(iconview->priv->velocity_time_list);
		iconview->priv->velocity_time_list = NULL;
		
		VelocityTimeStruct* vt = g_malloc(sizeof(VelocityTimeStruct));
		vt->hvelocity = hvelocity_avg;
		vt->vvelocity = vvelocity_avg;
		vt->time = total_time;
		
		iconview->priv->velocity_time_list = 
			g_list_append(iconview->priv->velocity_time_list,vt);
	}
	GList* first = g_list_first(iconview->priv->velocity_time_list);
	
	VelocityTimeStruct* vt = (VelocityTimeStruct*)first->data;
	vt->hvelocity = (gint)(hvelocity_avg/divider);
	vt->vvelocity = (gint)(vvelocity_avg/divider);
	
	gint hdistance = (gint)(timeout_secs * hvelocity_avg);
	
	if (0 == hdistance || 0 == hvelocity_avg)
	{
		hdone = TRUE;
	}
	else
	{
	
		gint old_hadjust = (guint)gtk_adjustment_get_value(iconview->priv->hadjustment);
		gint hadjust = old_hadjust;
		hadjust = MAX (0,hadjust - hdistance);
		hadjust = MIN(hadjust, gtk_adjustment_get_upper(iconview->priv->hadjustment) - gtk_adjustment_get_page_size(iconview->priv->hadjustment));
		if (old_hadjust == hadjust)
		{
			hdone = TRUE;
		}
		else
		{
			gtk_adjustment_set_value(iconview->priv->hadjustment,hadjust);
		}
	}
	
	gint vdistance = (gint)(timeout_secs * vvelocity_avg);

	if (0 == vdistance || 0 == vvelocity_avg)
	{
		vdone = TRUE;
	}
	else
	{
	
		gint old_vadjust = (guint)gtk_adjustment_get_value(iconview->priv->vadjustment);
		gint vadjust = old_vadjust;
		vadjust = MAX (0,vadjust - vdistance);
		vadjust = MIN(vadjust, gtk_adjustment_get_upper(iconview->priv->vadjustment) - gtk_adjustment_get_page_size(iconview->priv->vadjustment));
		if (old_vadjust == vadjust)
		{
			vdone = TRUE;
		}
		else
		{
			gtk_adjustment_set_value(iconview->priv->vadjustment,vadjust);
		}
	}
	
	
	if (hdone && vdone)
	{
		iconview->priv->timeout_id_smooth_scroll_slowdown = 0;
	}
	
	gdk_threads_leave();
	
	return !(hdone && vdone);
}

static void
quiver_icon_view_style_set(GtkWidget *widget, GtkStyle *prev_style, gpointer data)
{
	QuiverIconView* iconview = QUIVER_ICON_VIEW(widget);
}

/* start callbacks */
static void
quiver_icon_view_adjustment_value_changed (GtkAdjustment *adjustment,
           QuiverIconView *iconview)
{
	GtkWidget *widget = GTK_WIDGET(iconview);

	gdouble hadj,vadj;
	hadj = (int)gtk_adjustment_get_value(iconview->priv->hadjustment);
	vadj = (int)gtk_adjustment_get_value(iconview->priv->vadjustment);

	if (gtk_widget_get_realized (iconview))
	{
		if (iconview->priv->scroll_draw)
		{
			gint hdiff = (gint)(iconview->priv->last_hadjustment - hadj);
			gint vdiff = (gint)(iconview->priv->last_vadjustment - vadj);

			gdk_window_scroll(gtk_widget_get_window(widget),hdiff,vdiff);
			gdk_window_process_updates(gtk_widget_get_window(widget), FALSE);
		}
		iconview->priv->last_vadjustment = vadj;
		iconview->priv->last_hadjustment = hadj;
	}
}

static void
quiver_icon_view_set_property (GObject *object,
                    guint               prop_id,
                    const GValue       *value,
                    GParamSpec         *pspec)
{
	QuiverIconView  *iconview;

	iconview = QUIVER_ICON_VIEW (object);

	switch (prop_id)
	{
		case PROP_HADJUSTMENT:
			quiver_icon_view_set_hadjustment(iconview, g_value_get_object (value));
			break;
		case PROP_VADJUSTMENT:
			quiver_icon_view_set_vadjustment(iconview, g_value_get_object (value));
			break;
		case PROP_HSCROLL_POLICY:
			iconview->priv->hscroll_policy = g_value_get_enum(value);
			gtk_widget_queue_resize(GTK_WIDGET(iconview));
			break;
		case PROP_VSCROLL_POLICY:
			iconview->priv->vscroll_policy = g_value_get_enum(value);
			gtk_widget_queue_resize(GTK_WIDGET(iconview));
			break;
		/*
		case PROP_N_ITEMS:
			quiver_icon_view_set_n_items_closure (iconview, g_value_get_boxed (value));
			break;
		case PROP_ICON_PIXBUF:
			quiver_icon_view_set_icon_pixbuf_closure (iconview, g_value_get_boxed (value));
			break;
		case PROP_THUMBNAIL_PIXBUF:
			quiver_icon_view_set_thumbnail_pixbuf_closure (iconview, g_value_get_boxed (value));
			break;
		case PROP_TEXT:
			quiver_icon_view_set_text_closure (iconview, g_value_get_boxed (value));
			break;
		*/
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}

}

static void      
quiver_icon_view_get_property (GObject    *object,
                    guint       prop_id,
                    GValue     *value,
                    GParamSpec *pspec)
{
	QuiverIconView  *iconview;

	iconview = QUIVER_ICON_VIEW (object);

	switch (prop_id)
	{
		case PROP_HADJUSTMENT:
			g_value_set_object(value, iconview->priv->hadjustment);
			break;
		case PROP_VADJUSTMENT:
			g_value_set_object(value, iconview->priv->vadjustment);
			break;
		case PROP_HSCROLL_POLICY:
			g_value_set_enum(value, iconview->priv->hscroll_policy);
			break;
		case PROP_VSCROLL_POLICY:
			g_value_set_enum(value, iconview->priv->vscroll_policy);
			break;
/*
		case PROP_N_ITEMS:
			g_value_set_boxed (value, iconview->priv->closure_n_items);
			break;
		case PROP_ICON_PIXBUF:
			g_value_set_boxed (value, iconview->priv->closure_icon_pixbuf);
			break;
		case PROP_THUMBNAIL_PIXBUF:
			g_value_set_boxed (value, iconview->priv->closure_thumbnail_pixbuf);
			break;
		case PROP_TEXT:
			g_value_set_boxed (value, iconview->priv->closure_text);
			break;
*/
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}


/* end callbacks */

/* start utility functions*/
static guint
quiver_icon_view_get_width(QuiverIconView *iconview)
{
	guint cols,rows;
	guint cell_width = quiver_icon_view_get_cell_width(iconview);
	quiver_icon_view_get_col_row_count(iconview, &cols, &rows);

	return cols * cell_width;
}

static guint
quiver_icon_view_get_height(QuiverIconView *iconview)
{
	guint cols,rows;
	guint cell_height = quiver_icon_view_get_cell_height(iconview);
	quiver_icon_view_get_col_row_count(iconview, &cols, &rows);

	return rows * cell_height;
}

static void
quiver_icon_view_set_adjustment_upper (GtkAdjustment *adj,
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
    gtk_adjustment_changed (adj);
  if (value_changed)
    gtk_adjustment_value_changed (adj);
}

gulong
quiver_icon_view_get_cell_for_xy(QuiverIconView *iconview,gint x, gint y)
{
	guint cell_width = quiver_icon_view_get_cell_width(iconview);
	guint cell_height = quiver_icon_view_get_cell_height(iconview);

	guint padding = iconview->priv->cell_padding;
	guint hadjust = (int)gtk_adjustment_get_value(iconview->priv->hadjustment);
	guint vadjust = (int)gtk_adjustment_get_value(iconview->priv->vadjustment);

	guint cols, rows;
	quiver_icon_view_get_col_row_count(iconview,&cols,&rows);

	guint current_row,current_col;
	guint remainder;
	current_col = (x+hadjust) / cell_width;
	// if the column is greater than the total number of columns
	// return -1
	
	if (current_col >= cols)
		return G_MAXULONG;

	remainder = (x+hadjust) - current_col * cell_width;
	// check if x is in padding area, if so return -1
	if (padding/2 > remainder || padding/2 > cell_width-remainder)
		return G_MAXULONG;
	
	current_row =  (y +vadjust) / cell_height;

	//GdkPixbuf *pixbuf = GetStockIcon(f);



	
	remainder = (y + vadjust) - current_row * cell_height;
	if (padding/2 > remainder || padding/2 > cell_height-remainder)
		return G_MAXULONG;


	gulong cell = current_row * cols + current_col;
	gulong n_cells  = quiver_icon_view_get_n_items(iconview);

	if (n_cells < cell+1)
		return G_MAXULONG;

	return cell;
}


static void
quiver_icon_view_set_cursor_cell_full(QuiverIconView *iconview,gulong new_cursor_cell,GdkModifierType state,gboolean is_mouse)
{
	gulong old_cell = iconview->priv->cursor_cell;
	
	if (state & GDK_CONTROL_MASK)
	{
		// normal mode, deselect all, select new cell
		quiver_icon_view_invalidate_cell(iconview,iconview->priv->cursor_cell);
		iconview->priv->cursor_cell = new_cursor_cell;
		if (is_mouse)
		{
			// for the mouse, we want to toggle the item on the control click
			iconview->priv->cell_items[iconview->priv->cursor_cell].selected = !iconview->priv->cell_items[iconview->priv->cursor_cell].selected;
			g_signal_emit(iconview,iconview_signals[SIGNAL_SELECTION_CHANGED],0);
		}
		quiver_icon_view_invalidate_cell(iconview,iconview->priv->cursor_cell);
		iconview->priv->cursor_cell_first = G_MAXULONG;
	}
	else if (state & GDK_SHIFT_MASK)
	{
		if (G_MAXULONG == iconview->priv->cursor_cell_first)
		{
			iconview->priv->cursor_cell_first = iconview->priv->cursor_cell;
		}
		quiver_icon_view_shift_select_cells(iconview,new_cursor_cell);
	}
	else
	{
		// normal mode, deselect all, select new cell
		iconview->priv->cursor_cell_first = G_MAXULONG;

		quiver_icon_view_invalidate_cell(iconview,iconview->priv->cursor_cell);

		// check the selection
		gulong i;
		gulong n_cells  = quiver_icon_view_get_n_items(iconview);
		gboolean change_selection;

		change_selection = FALSE;
		for (i = 0; i < n_cells;i++)
		{
			if (iconview->priv->cell_items[i].selected && i != new_cursor_cell)
			{
				change_selection = TRUE;
				iconview->priv->cell_items[i].selected = FALSE;
				quiver_icon_view_invalidate_cell(iconview,i);
			}
		}

		iconview->priv->cursor_cell = new_cursor_cell;
		
		if (!iconview->priv->cell_items[new_cursor_cell].selected)
		{
			change_selection = TRUE;
		}

		if (change_selection)
		{
			//quiver_icon_view_set_select_all(iconview,FALSE);
			iconview->priv->cell_items[iconview->priv->cursor_cell].selected = TRUE;
			quiver_icon_view_invalidate_cell(iconview,iconview->priv->cursor_cell);
			g_signal_emit(iconview,iconview_signals[SIGNAL_SELECTION_CHANGED],0);
		}
	}

	quiver_icon_view_scroll_to_cell(iconview,new_cursor_cell);
	if (old_cell != iconview->priv->cursor_cell)
	{
		g_signal_emit(iconview,iconview_signals[SIGNAL_CURSOR_CHANGED],0,new_cursor_cell);
	}
	//redraw_needed = TRUE;
}

static void 
quiver_icon_view_scroll_to_cell_force_top(QuiverIconView *iconview,gulong cell,gboolean force_top)
{
	guint cols,rows;
	quiver_icon_view_get_col_row_count(iconview,&cols,&rows);

	guint cell_width = quiver_icon_view_get_cell_width(iconview);
	guint cell_height = quiver_icon_view_get_cell_height(iconview);

	gint hadjust = (guint)gtk_adjustment_get_value(iconview->priv->hadjustment);
	gint vadjust = (guint)gtk_adjustment_get_value(iconview->priv->vadjustment);

	guint cell_x;
	guint cell_y;
		
	if (0 != iconview->priv->n_rows)
	{
		// FIXME: this is a bug for anything but row=1
		// also we should probably switch the drawing order so it draws like this:
		// 1 3 5 7 9
		// 2 4 6 8
		// instead of:
		// 1 2 3 4 5
		// 6 7 8 9
		cell_x = (cell / rows) * cell_width;
		cell_y = (cell % rows) * cell_height;
	}
	else
	{
		cell_x = (cell % cols) * cell_width;
		cell_y = (cell / cols) * cell_height;

	}

	/*
	if ( force_top || cell_y < vadjust)
	{
		if (cell_y < gtk_adjustment_get_upper(iconview->priv->vadjustment) - gtk_adjustment_get_page_size(iconview->priv->vadjustment))
			gtk_adjustment_set_value(iconview->priv->vadjustment,cell_y);
		else
			gtk_adjustment_get_upper(gtk_adjustment_set_value(iconview->priv->vadjustment,iconview->priv->vadjustment) - gtk_adjustment_get_page_size(iconview->priv->vadjustment));
	}
	else if (vadjust + gtk_adjustment_get_page_size(iconview->priv->vadjustment) < cell_y+cell_height)
	{
		gtk_adjustment_set_value(iconview->priv->vadjustment,cell_y+cell_height gtk_adjustment_get_page_size(-iconview->priv->vadjustment));
	}
	*/
	
	if (QUIVER_ICON_VIEW_SCROLL_NORMAL != iconview->priv->scroll_type && !force_top)
	{
		quiver_icon_view_scroll_to_cell_smooth(iconview,cell);

		return;
	}

	/* horizontal adjustment */
	gint new_hadjust;
	if (QUIVER_ICON_VIEW_SCROLL_SMOOTH_CENTER == iconview->priv->scroll_type)
	{
		new_hadjust = cell_x - gtk_adjustment_get_page_size(iconview->priv->hadjustment)/2 + cell_width/2;
	}
	else if (cell_x < hadjust || force_top)
	{
		new_hadjust = cell_x;
	}
	else if (cell_x > hadjust + gtk_adjustment_get_page_size(iconview->priv->hadjustment) - cell_width)
	{
		new_hadjust = cell_x + cell_width - gtk_adjustment_get_page_size(iconview->priv->hadjustment);
	}
	else
	{
		new_hadjust = hadjust;
	}

	new_hadjust = MAX (0,new_hadjust);
	new_hadjust = MIN(new_hadjust, gtk_adjustment_get_upper(iconview->priv->hadjustment) - gtk_adjustment_get_page_size(iconview->priv->hadjustment));

	if (new_hadjust != hadjust)
	{
		gtk_adjustment_set_value(iconview->priv->hadjustment, new_hadjust);
	}

	/* vertical adjustment */
	gint new_vadjust;
	if (QUIVER_ICON_VIEW_SCROLL_SMOOTH_CENTER == iconview->priv->scroll_type)
	{
		new_vadjust = cell_y - gtk_adjustment_get_page_size(iconview->priv->vadjustment)/2 + cell_height/2;
	}
	else if (cell_y < vadjust || force_top)
	{
		new_vadjust = cell_y;
	}
	else if (cell_y > vadjust + gtk_adjustment_get_page_size(iconview->priv->vadjustment) - cell_height)
	{
		new_vadjust = cell_y + cell_height - gtk_adjustment_get_page_size(iconview->priv->vadjustment);
	}
	else
	{
		new_vadjust = vadjust;
	}

	new_vadjust = MAX (0,new_vadjust);
	new_vadjust = MIN(new_vadjust, gtk_adjustment_get_upper(iconview->priv->vadjustment) - gtk_adjustment_get_page_size(iconview->priv->vadjustment));

	if (new_vadjust != vadjust)
	{
		gtk_adjustment_set_value(iconview->priv->vadjustment, new_vadjust);
	}
}

static void
quiver_icon_view_scroll_to_cell(QuiverIconView *iconview,gulong cell)
{
	quiver_icon_view_scroll_to_cell_force_top(iconview,cell,FALSE);
}
static void
quiver_icon_view_set_select_all(QuiverIconView *iconview, gboolean selected)
{
	gulong i;
	gulong n_cells  = quiver_icon_view_get_n_items(iconview);
	gboolean changed;

	changed = FALSE;
	for (i = 0; i < n_cells;i++)
	{
		if (iconview->priv->cell_items[i].selected != selected)
		{
			iconview->priv->cell_items[i].selected = selected;
			quiver_icon_view_invalidate_cell(iconview,i);
			changed = TRUE;
		}
	}
	if (changed)
	{
		g_signal_emit(iconview,iconview_signals[SIGNAL_SELECTION_CHANGED],0);
	}
}

static void
quiver_icon_view_shift_select_cells(QuiverIconView *iconview,gulong new_cursor_cell)
{
	gulong i;
	gboolean selection_changed;
	selection_changed = FALSE;

	gulong first_cell = iconview->priv->cursor_cell_first;

	if (G_MAXULONG == new_cursor_cell || G_MAXULONG == first_cell)
	{
		return;
	}
	
	gulong n_cells  = quiver_icon_view_get_n_items(iconview);
	
	if (0 == n_cells)
	{
		return;
	}


	gulong select_range_start = MIN(first_cell,new_cursor_cell);
	gulong select_range_end = MAX(first_cell,new_cursor_cell);

	gulong start = MIN(first_cell,MIN(iconview->priv->cursor_cell,new_cursor_cell));
	gulong end = MAX(first_cell,MAX(iconview->priv->cursor_cell,new_cursor_cell));


	quiver_icon_view_invalidate_cell(iconview,iconview->priv->cursor_cell);
	quiver_icon_view_invalidate_cell(iconview,new_cursor_cell);

	iconview->priv->cursor_cell = new_cursor_cell;
	
	for (i = start; i <= end; i++)
	{
		if (select_range_start <= i && i <= select_range_end)
		{
			// select this range
			if (!iconview->priv->cell_items[i].selected)
			{
				iconview->priv->cell_items[i].selected = TRUE;
				quiver_icon_view_invalidate_cell(iconview,i);
				selection_changed = TRUE;
			}
			
		}
		else
		{
			// unselect the old range
			if (iconview->priv->cell_items[i].selected)
			{
				iconview->priv->cell_items[i].selected = FALSE;
				quiver_icon_view_invalidate_cell(iconview,i);
				selection_changed = TRUE;
			}
		}
	}
	if (selection_changed)
	{
		g_signal_emit(iconview,iconview_signals[SIGNAL_SELECTION_CHANGED],0);
	}
}
static void 
quiver_icon_view_update_rubber_band(QuiverIconView *iconview)
{
	gint x =0, y=0;
	cairo_region_t *invalid_region;
	cairo_region_t *old_region;
	GdkModifierType state;
	GtkWidget *widget;
	
	widget = GTK_WIDGET (iconview);
	gdk_window_get_pointer(gtk_widget_get_window(widget), &x, &y, &state);
		
	iconview->priv->rubberband_rect_old.x = MIN (iconview->priv->rubberband_x1, iconview->priv->rubberband_x2);
	iconview->priv->rubberband_rect_old.y = MIN (iconview->priv->rubberband_y1, iconview->priv->rubberband_y2);
	iconview->priv->rubberband_rect_old.width = ABS (iconview->priv->rubberband_x1 - iconview->priv->rubberband_x2)+1;
	iconview->priv->rubberband_rect_old.height = ABS (iconview->priv->rubberband_y1 - iconview->priv->rubberband_y2)+1;
	
	gint vadjust = (gint)gtk_adjustment_get_value(iconview->priv->vadjustment);
	gint hadjust = (gint)gtk_adjustment_get_value(iconview->priv->hadjustment);
	
	iconview->priv->rubberband_x2 = x + hadjust;
	iconview->priv->rubberband_y2 = y + vadjust;
	
	iconview->priv->rubberband_rect.x = MIN (iconview->priv->rubberband_x1, iconview->priv->rubberband_x2);
	iconview->priv->rubberband_rect.y = MIN (iconview->priv->rubberband_y1, iconview->priv->rubberband_y2);
	iconview->priv->rubberband_rect.width = ABS (iconview->priv->rubberband_x1 - iconview->priv->rubberband_x2)+1;
	iconview->priv->rubberband_rect.height = ABS (iconview->priv->rubberband_y1 - iconview->priv->rubberband_y2)+1;
	
	invalid_region = cairo_region_create_rectangle(&iconview->priv->rubberband_rect);
	old_region = cairo_region_create_rectangle(&iconview->priv->rubberband_rect_old);
	cairo_region_xor(invalid_region,old_region);

	int num_rects = cairo_region_num_rectangles(invalid_region);
	cairo_region_t* invalid_region_expanded = cairo_region_create();
	int i = 0;
	for (i = 0; i < num_rects; ++i)
	{
		cairo_rectangle_int_t tmprect = {0};
		cairo_region_get_rectangle(invalid_region, i, &tmprect);
		tmprect.x -= 1;
		tmprect.y -= 1;
		tmprect.width += 2;
		tmprect.height += 2;
		cairo_region_union_rectangle(invalid_region_expanded, &tmprect);
	}
	
	quiver_icon_view_update_rubber_band_selection(iconview);
	
	//redraw_needed = TRUE;
	cairo_region_translate(invalid_region_expanded,-hadjust,-vadjust);
	gdk_window_invalidate_region(gtk_widget_get_window(widget),invalid_region_expanded,FALSE);
	cairo_region_destroy(invalid_region_expanded);
	cairo_region_destroy(invalid_region);
	cairo_region_destroy(old_region);
}

static void
quiver_icon_view_update_rubber_band_selection(QuiverIconView *iconview)
{
	gboolean selection_changed;

	selection_changed = FALSE;

	guint cell_width = quiver_icon_view_get_cell_width(iconview);
	guint cell_height = quiver_icon_view_get_cell_height(iconview);
	guint padding = iconview->priv->cell_padding;

	gulong i = 0;
	gulong j = 0;

	guint num_cols,num_rows;
	quiver_icon_view_get_col_row_count(iconview,&num_cols,&num_rows);

	cairo_region_t *old_region;
	cairo_region_t *new_region;
	new_region = cairo_region_create_rectangle(&iconview->priv->rubberband_rect);
	old_region = cairo_region_create_rectangle(&iconview->priv->rubberband_rect_old);

	cairo_rectangle_int_t tmp_rect;

	// FIXME : this is not optimized. currently it iterates over all cells /
	gulong n_cells  = quiver_icon_view_get_n_items(iconview);
	for (j=0;j <= num_rows; j++)
	{
		for (i = 0;i< num_cols; i ++)
		{
			int current_cell = j * num_cols + i;
			if (current_cell >= n_cells)
				break;

			tmp_rect.x = i * cell_width + padding/2;
			tmp_rect.y = j * cell_height + padding/2;
			tmp_rect.width = cell_width - padding;
			tmp_rect.height = cell_height - padding;

			if ( CAIRO_REGION_OVERLAP_OUT != cairo_region_contains_rectangle(new_region,&tmp_rect) )
			{
				// select!
				if (!iconview->priv->cell_items[current_cell].selected)
				{
					iconview->priv->cell_items[current_cell].selected = TRUE;
					quiver_icon_view_invalidate_cell(iconview,current_cell);
					selection_changed = TRUE;
				}
			}
			else if ( CAIRO_REGION_OVERLAP_OUT != cairo_region_contains_rectangle(old_region,&tmp_rect) )
			{
				// unselect!
				if (iconview->priv->cell_items[current_cell].selected)
				{
					iconview->priv->cell_items[current_cell].selected = FALSE;
					quiver_icon_view_invalidate_cell(iconview,current_cell);
					selection_changed = TRUE;
				}
			}
		}
	}

	if (selection_changed)
	{
		g_signal_emit(iconview,iconview_signals[SIGNAL_SELECTION_CHANGED],0);
	}

	cairo_region_destroy(new_region);
	cairo_region_destroy(old_region);
}


static void
quiver_icon_view_update_icon_size(QuiverIconView *iconview)
{
	GtkWidget *widget = GTK_WIDGET(iconview);

	if (!gtk_widget_get_realized (widget))
	{
		return;
	}

	guint width = quiver_icon_view_get_width(iconview);
	guint height = quiver_icon_view_get_height(iconview);

	guint cell_width = quiver_icon_view_get_cell_width(iconview);
	guint cell_height = quiver_icon_view_get_cell_height(iconview);

	GtkAdjustment *hadjustment, *vadjustment;

	hadjustment = iconview->priv->hadjustment;
	vadjustment = iconview->priv->vadjustment;

	gtk_adjustment_set_page_size(hadjustment, gtk_widget_get_allocated_width(widget));
	gtk_adjustment_set_page_increment(hadjustment, gtk_widget_get_allocated_width(widget));
	gtk_adjustment_set_step_increment(hadjustment,  cell_width);
	gtk_adjustment_set_lower(hadjustment, 0);
	gtk_adjustment_set_upper(hadjustment, MAX(gtk_widget_get_allocated_width(widget), width));

	if (gtk_adjustment_get_value(hadjustment) > gtk_adjustment_get_upper(hadjustment) - gtk_adjustment_get_page_size(hadjustment))
		gtk_adjustment_set_value (hadjustment, MAX(0, gtk_adjustment_get_upper(hadjustment) - gtk_adjustment_get_page_size(hadjustment)));

	gtk_adjustment_set_page_size(vadjustment, gtk_widget_get_allocated_height(widget));
	gtk_adjustment_set_page_increment(vadjustment, gtk_widget_get_allocated_height(widget));
	gtk_adjustment_set_step_increment(vadjustment, cell_height);
	gtk_adjustment_set_lower(vadjustment, 0);
	gtk_adjustment_set_upper(vadjustment, MAX(gtk_widget_get_allocated_height(widget), height));

	/* FIXME: We should have an option for this if the user doesnt want resize to keep
	 * the current focus cell in view
	 * */
	/*
	if gtk_adjustment_get_value((vadjustment) > gtk_adjustment_get_upper(vadjustment) - gtk_adjustment_get_page_size(vadjustment))
		gtk_adjustment_set_value (vadjustment, MAX (0, gtk_adjustment_get_upper(vadjustment) - gtk_adjustment_get_page_size(vadjustment)));
		*/

	gtk_adjustment_changed (hadjustment);
	gtk_adjustment_changed (vadjustment);


	iconview->priv->scroll_draw = FALSE;

	quiver_icon_view_scroll_to_cell_force_top(iconview,iconview->priv->cursor_cell,TRUE);

	//printf("invalidating whole area\n");

	cairo_rectangle_int_t rect;
	rect.x = 0;
	rect.y = 0;
	rect.width = gtk_widget_get_allocated_width(widget);
	rect.height = gtk_widget_get_allocated_height(widget);
	if (gtk_widget_get_realized(widget))
	{
		gdk_window_invalidate_rect(gtk_widget_get_window(widget),&rect,FALSE);
		gdk_window_process_updates(gtk_widget_get_window(widget),FALSE);
	}

	iconview->priv->scroll_draw = TRUE;

}

static gulong
quiver_icon_view_get_n_items(QuiverIconView* iconview)
{
	gulong n_items = 0;
	if (iconview->priv->callback_get_n_items)
	{
		n_items = (*iconview->priv->callback_get_n_items)(iconview,iconview->priv->callback_get_n_items_data);
	}
	if (iconview->priv->n_cell_items != n_items)
	{
		iconview->priv->n_cell_items = n_items;
		g_free (iconview->priv->cell_items);
		iconview->priv->cell_items = (CellItem*)g_malloc0( sizeof(CellItem)*(n_items+1) );
		quiver_icon_view_update_icon_size(iconview);
		
		g_signal_emit(iconview,iconview_signals[SIGNAL_SELECTION_CHANGED],0);
	}
	return n_items;
}

static GdkPixbuf* quiver_icon_view_get_thumbnail_pixbuf(QuiverIconView* iconview,gulong cell, gint* actual_width, gint *actual_height)
{
	GdkPixbuf *pixbuf = NULL;
	if (iconview->priv->callback_get_thumbnail_pixbuf)
		pixbuf = (*iconview->priv->callback_get_thumbnail_pixbuf)(iconview,cell, actual_width, actual_height, iconview->priv->callback_get_thumbnail_pixbuf_data);
	return pixbuf;
}

static GdkPixbuf* quiver_icon_view_get_icon_pixbuf(QuiverIconView* iconview,gulong cell)
{
	GdkPixbuf *pixbuf = NULL;
	if (iconview->priv->callback_get_icon_pixbuf)
		pixbuf = (*iconview->priv->callback_get_icon_pixbuf)(iconview,cell,iconview->priv->callback_get_icon_pixbuf_data);
	return pixbuf;
}


static void quiver_icon_view_draw_drop_shadow(QuiverIconView *iconview, cairo_t* cr, GtkStateFlags state_flags, int rect_x,int rect_y, int rect_w, int rect_h)
{
	int shadow_width = QUIVER_ICON_VIEW_ICON_SHADOW_SIZE - 1; // one pixel blank

	cairo_save(cr);

	// set region to be with in this
	cairo_rectangle (cr, rect_x - shadow_width, rect_y - shadow_width, rect_w + shadow_width*2, rect_h + shadow_width*2);
	// but not within this (specified in reverse winding)
	cairo_rectangle (cr, rect_x + rect_w, rect_y, -rect_w, rect_h);
	cairo_clip (cr);
	

	cairo_set_source_rgba (cr, 0, 0, 0, .1);
	cairo_set_line_join(cr,CAIRO_LINE_JOIN_ROUND);
	int k = 0;
	for (k = shadow_width*2;k >=1; k-=2)
	{
		cairo_set_line_width(cr,k);
		//cairo_rectangle (cr, top, left, width, height);
		cairo_rectangle (cr, rect_x, rect_y, rect_w, rect_h);
		cairo_stroke(cr);
	}

	cairo_restore(cr);
}


/* end utility functions*/

/* end private functions */

/* start public functions */
GtkWidget *
quiver_icon_view_new()
{
	return g_object_new(QUIVER_TYPE_ICON_VIEW, 
		"hadjustment", new_default_adjustment(),
		"vadjustment", new_default_adjustment(),
		NULL);
}

void quiver_icon_view_set_n_columns(QuiverIconView *iconview,guint n_columns)
{
	iconview->priv->n_columns = n_columns;
	iconview->priv->n_rows = 0;
}

void quiver_icon_view_set_n_rows(QuiverIconView *iconview,guint n_rows)
{
	iconview->priv->n_rows = n_rows;
	iconview->priv->n_columns = 0;
}

void quiver_icon_view_set_scroll_type(QuiverIconView *iconview, QuiverIconViewScrollType scroll_type)
{
	iconview->priv->scroll_type = scroll_type;
}

void 
quiver_icon_view_set_drag_behavior(QuiverIconView *iconview,QuiverIconViewDragBehavior behavior)
{
	iconview->priv->drag_behavior = behavior;
}

void 
quiver_icon_view_set_icon_size(QuiverIconView *iconview, guint width,guint height)
{
	iconview->priv->icon_width  = width;
	iconview->priv->icon_height = height;

	guint request_width,request_height;

	request_width = quiver_icon_view_get_cell_width(iconview);
	request_height = quiver_icon_view_get_cell_height(iconview);

	if (0 != iconview->priv->n_columns)
	{
		request_width *= iconview->priv->n_columns;
	}

	if (0 != iconview->priv->n_rows)
	{
		request_height *= iconview->priv->n_rows;
	}
	gtk_widget_set_size_request(GTK_WIDGET(iconview),request_width,request_height);
	/* adjust the size of the scrollbars accordingly */
	quiver_icon_view_update_icon_size(iconview);
}

void
quiver_icon_view_set_cell_padding(QuiverIconView *iconview,guint padding)
{
	iconview->priv->cell_padding = padding;
}

void
quiver_icon_view_get_icon_size(QuiverIconView *iconview, guint* width,guint* height)
{
	*width  = iconview->priv->icon_width;
	*height = iconview->priv->icon_height;
}

guint
quiver_icon_view_get_cell_padding(QuiverIconView *iconview)
{
	return iconview->priv->cell_padding;
}

void quiver_icon_view_activate_cell(QuiverIconView *iconview,gulong cell)
{
	g_return_if_fail (QUIVER_IS_ICON_VIEW (iconview));
	
	g_signal_emit(iconview,iconview_signals[SIGNAL_CELL_ACTIVATED],0,cell);
}

static
void quiver_icon_view_click_cell(QuiverIconView *iconview,gulong cell)
{
	g_return_if_fail (QUIVER_IS_ICON_VIEW (iconview));
	
	g_signal_emit(iconview,iconview_signals[SIGNAL_CELL_CLICKED],0,cell);
}

gulong quiver_icon_view_get_cursor_cell(QuiverIconView *iconview)
{
	g_return_val_if_fail (QUIVER_IS_ICON_VIEW (iconview), G_MAXULONG);
	return iconview->priv->cursor_cell;
}

void quiver_icon_view_set_cursor_cell(QuiverIconView *iconview,gulong new_cursor_cell)
{
	g_return_if_fail (QUIVER_IS_ICON_VIEW (iconview));

	quiver_icon_view_set_cursor_cell_full(iconview,new_cursor_cell,(GdkModifierType)0,FALSE);
}

gulong quiver_icon_view_get_prelight_cell(QuiverIconView* iconview)
{
	g_return_val_if_fail (QUIVER_IS_ICON_VIEW (iconview), G_MAXULONG);
	return iconview->priv->prelight_cell;
}

void quiver_icon_view_get_cell_mouse_position(QuiverIconView* iconview, guint cell, gint *x, gint *y)
{
	gint wx, wy;

	gtk_widget_get_pointer(GTK_WIDGET(iconview), &wx, &wy);

	guint cell_width = quiver_icon_view_get_cell_width(iconview);
	guint cell_height = quiver_icon_view_get_cell_height(iconview);

	guint padding = iconview->priv->cell_padding;
	guint hadjust = (int)gtk_adjustment_get_value(iconview->priv->hadjustment);
	guint vadjust = (int)gtk_adjustment_get_value(iconview->priv->vadjustment);

	guint cols, rows;
	quiver_icon_view_get_col_row_count(iconview,&cols,&rows);
	gint cell_col = cell % cols;
	gint cell_row = cell / cols; 
	gint cell_x = cell_width * cell_col - hadjust;
	gint cell_y = cell_height * cell_row - vadjust;

	*x = wx - cell_x - padding;
	*y = wy - cell_y - padding;
}

void quiver_icon_view_set_selection(QuiverIconView *iconview,const GList *selection)
{
	gboolean selection_changed;
	const GList *selection_iter;

	g_return_if_fail (QUIVER_IS_ICON_VIEW (iconview));

	selection_changed = FALSE;
	selection_iter = selection;




	quiver_icon_view_set_select_all(iconview,FALSE);

	while (NULL != selection_iter)
	{
		gulong item = (gulong)selection_iter->data;
		if (item < iconview->priv->n_cell_items)
		{
			iconview->priv->cell_items[item].selected = TRUE;
			quiver_icon_view_invalidate_cell(iconview,item);
			selection_changed = TRUE;
		}
	}
	if (selection_changed)
	{
		g_signal_emit(iconview,iconview_signals[SIGNAL_SELECTION_CHANGED],0);
	}
}

GList* quiver_icon_view_get_selection(QuiverIconView *iconview)
{
	GList *selection;
	gulong i;

	selection = NULL;

	for (i = 0 ; i < iconview->priv->n_cell_items; i++)
	{
		if (iconview->priv->cell_items[i].selected)
		{
			selection = g_list_append(selection,(gpointer)i);
		}
	}
	return selection;
}

void quiver_icon_view_get_visible_range(QuiverIconView *iconview,gulong *first, gulong *last)
{
	GtkWidget *widget = GTK_WIDGET(iconview);

	guint num_cols,num_rows;
	quiver_icon_view_get_col_row_count(iconview,&num_cols,&num_rows);

	/*guint cell_width = quiver_icon_view_get_cell_width(iconview);*/
	guint cell_height = quiver_icon_view_get_cell_height(iconview);

	guint adj = (guint)gtk_adjustment_get_value(iconview->priv->vadjustment);

	guint row_first = adj / cell_height;
	guint row_last = (adj + gtk_widget_get_allocated_height(widget)) / cell_height;

	/* so , the current view contains icons from col 0, row_first
	 * to col num_cols -1, row_last
	 * so that is 
	 */

	gulong cell_start = row_first * num_cols;
	gulong cell_end = row_last * num_cols + num_cols;

	gulong n_cells = quiver_icon_view_get_n_items(iconview); 

	if ( n_cells < cell_end )
	{
		cell_end = n_cells ;
	}
	*first = cell_start;
	*last = cell_end;
}

void
quiver_icon_view_invalidate_window(QuiverIconView *iconview)
{
	GtkWidget* widget;
	widget = GTK_WIDGET(iconview);
	
	gdk_window_invalidate_rect(gtk_widget_get_window(widget), NULL, FALSE);
}

void 
quiver_icon_view_invalidate_cell(QuiverIconView *iconview,
		gulong cell)
{

	GtkWidget *widget = GTK_WIDGET (iconview);
	
	if (!gtk_widget_get_realized(widget))
	{
		return;
	}
	
	gulong n_cells = quiver_icon_view_get_n_items(iconview);
	if (0 > cell || cell >= n_cells)
		return;
	
	guint cell_width = quiver_icon_view_get_cell_width(iconview);
	guint cell_height = quiver_icon_view_get_cell_height(iconview);

	guint num_cols,num_rows;
	quiver_icon_view_get_col_row_count(iconview,&num_cols,&num_rows);

	/*guint width = quiver_icon_view_get_width(iconview);*/
	//guint height = quiver_icon_view_get_height(iconview);

	guint hadjust = (guint)gtk_adjustment_get_value(iconview->priv->hadjustment);
	guint vadjust = (guint)gtk_adjustment_get_value(iconview->priv->vadjustment);

	int current_row,current_col;
	current_row = cell  / num_cols;
	current_col = cell % num_cols;

	cairo_rectangle_int_t cell_rect;
	cell_rect.x = current_col * cell_width - hadjust;
	cell_rect.y = current_row * cell_height - vadjust;
	cell_rect.width = cell_width;
	cell_rect.height = cell_height;

	gdk_window_invalidate_rect(gtk_widget_get_window(widget),&cell_rect,FALSE);
	//redraw_needed = TRUE;
}

void
quiver_icon_view_set_n_items_func (QuiverIconView *iconview, 
         QuiverIconViewGetNItemsFunc func,gpointer data,GDestroyNotify destroy)
{
	g_return_if_fail (QUIVER_IS_ICON_VIEW (iconview));

	if (iconview->priv->callback_get_n_items_data_destroy)
		(*iconview->priv->callback_get_n_items_data_destroy)(iconview->priv->callback_get_n_items_data);

	iconview->priv->callback_get_n_items = func;
	iconview->priv->callback_get_n_items_data = data;
	iconview->priv->callback_get_n_items_data_destroy = destroy;

}

void
quiver_icon_view_set_icon_pixbuf_func (QuiverIconView *iconview,
         QuiverIconViewGetIconPixbufFunc func,gpointer data,GDestroyNotify destroy)
{
	g_return_if_fail (QUIVER_IS_ICON_VIEW (iconview));

	if (iconview->priv->callback_get_icon_pixbuf_data_destroy)
		(*iconview->priv->callback_get_icon_pixbuf_data_destroy)(iconview->priv->callback_get_icon_pixbuf_data);

	iconview->priv->callback_get_icon_pixbuf = func;
	iconview->priv->callback_get_icon_pixbuf_data = data;
	iconview->priv->callback_get_icon_pixbuf_data_destroy = destroy;

}

void
quiver_icon_view_set_thumbnail_pixbuf_func (QuiverIconView *iconview,
         QuiverIconViewGetThumbnailPixbufFunc func,gpointer data,GDestroyNotify destroy)
{
	g_return_if_fail (QUIVER_IS_ICON_VIEW (iconview));

	if (iconview->priv->callback_get_thumbnail_pixbuf_data_destroy)
		(*iconview->priv->callback_get_thumbnail_pixbuf_data_destroy)(iconview->priv->callback_get_thumbnail_pixbuf_data);

	iconview->priv->callback_get_thumbnail_pixbuf = func;
	iconview->priv->callback_get_thumbnail_pixbuf_data = data;
	iconview->priv->callback_get_thumbnail_pixbuf_data_destroy = destroy;

}

void
quiver_icon_view_set_text_func (QuiverIconView *iconview,
         QuiverIconViewGetTextFunc func,gpointer data,GDestroyNotify destroy)
{
	g_return_if_fail (QUIVER_IS_ICON_VIEW (iconview));

	if (iconview->priv->callback_get_text_data_destroy)
		(*iconview->priv->callback_get_text_data_destroy)(iconview->priv->callback_get_text_data);

	iconview->priv->callback_get_text = func;
	iconview->priv->callback_get_text_data = data;
	iconview->priv->callback_get_text_data_destroy = destroy;

}

void quiver_icon_view_set_overlay_pixbuf_func (QuiverIconView *iconview,
         QuiverIconViewGetOverlayPixbufFunc func,gpointer data,GDestroyNotify destroy)
{
	g_return_if_fail (QUIVER_IS_ICON_VIEW (iconview));

	if (iconview->priv->callback_get_overlay_pixbuf_data_destroy)
		(*iconview->priv->callback_get_overlay_pixbuf_data_destroy)(iconview->priv->callback_get_overlay_pixbuf_data);

	iconview->priv->callback_get_overlay_pixbuf = func;
	iconview->priv->callback_get_overlay_pixbuf_data = data;
	iconview->priv->callback_get_overlay_pixbuf_data_destroy = destroy;

}
/* end public functions */
