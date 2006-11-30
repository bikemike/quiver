#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include "quiver-icon-view.h"
#include "quiver-marshallers.h"
#include <math.h>
#include <sys/time.h>
#include "quiver-pixbuf-utils.h"

//#include "gtkintl.h"


#define QUIVER_ICON_VIEW_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), QUIVER_TYPE_ICON_VIEW, QuiverIconViewPrivate))

/* set up some defaults */
#define QUIVER_ICON_VIEW_ICON_WIDTH              128
#define QUIVER_ICON_VIEW_ICON_HEIGHT             128
#define QUIVER_ICON_VIEW_CELL_PADDING            8 
#define QUIVER_ICON_VIEW_ICON_SHADOW_SIZE        5
#define QUIVER_ICON_VIEW_ICON_BORDER_SIZE        1

#define QUIVER_PARAM_READWRITE G_PARAM_READWRITE|G_PARAM_STATIC_NAME|G_PARAM_STATIC_NICK|G_PARAM_STATIC_BLURB

G_DEFINE_TYPE(QuiverIconView,quiver_icon_view,GTK_TYPE_WIDGET);

/* start private data structures */
typedef struct _CellItem CellItem;
struct _CellItem
{
	gboolean selected;
	
};

/* signals */
enum {
	SIGNAL_CELL_ACTIVATED,
	SIGNAL_CURSOR_CHANGED,
	SIGNAL_SELECTION_CHANGED,
	SIGNAL_COUNT
};
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


struct _QuiverIconViewPrivate
{
	GtkAdjustment *hadjustment;
	GtkAdjustment *vadjustment;

	gdouble last_hadjustment;
	gdouble last_vadjustment;

	guint icon_width;
	guint icon_height;
	guint icon_border_size;
	guint cell_padding;

	GdkPixmap *drop_shadow[5][8];

	gint rubberband_x1, rubberband_y1;
	gint rubberband_x2, rubberband_y2;

	gboolean scroll_draw;

	gboolean rubberband_mode_start;
	gboolean rubberband_mode;
	GdkRectangle rubberband_rect;
	GdkRectangle rubberband_rect_old;

	gint rubberband_scroll_x;
	gint rubberband_scroll_y;

	guint timeout_id_rubberband_scroll;

	gint cursor_cell;
	gint prelight_cell;
	gint cursor_cell_first;
	
	gboolean mouse_button_is_down;

	gboolean idle_load_running;
	gboolean smooth_scroll;
	gint smooth_scroll_cell;
	
	guint timeout_id_smooth_scroll;

	guint n_columns;
	guint n_rows;
	
	/* callback functions and data */
	QuiverIconViewGetNItemsFunc callback_get_n_items;
	gpointer callback_get_n_items_data;
	GtkDestroyNotify callback_get_n_items_data_destroy;

	QuiverIconViewGetIconPixbufFunc callback_get_icon_pixbuf;
	gpointer callback_get_icon_pixbuf_data;
	GtkDestroyNotify callback_get_icon_pixbuf_data_destroy;

	QuiverIconViewGetThumbnailPixbufFunc callback_get_thumbnail_pixbuf;
	gpointer callback_get_thumbnail_pixbuf_data;
	GtkDestroyNotify callback_get_thumbnail_pixbuf_data_destroy;

	QuiverIconViewGetTextFunc callback_get_text;
	gpointer callback_get_text_data;
	GtkDestroyNotify callback_get_text_data_destroy;

	QuiverIconViewGetOverlayPixbufFunc callback_get_overlay_pixbuf;
	gpointer callback_get_overlay_pixbuf_data;
	GtkDestroyNotify callback_get_overlay_pixbuf_data_destroy;

	CellItem *cell_items;
	guint n_cell_items;
};
/* end private data structures */


/* start private function prototypes */

static void      quiver_icon_view_realize        (GtkWidget *widget);

static void      quiver_icon_view_size_allocate  (GtkWidget     *widget,
                                             GtkAllocation *allocation);

static void      quiver_icon_view_size_request (GtkWidget *widget, GtkRequisition *requisition);

static void      quiver_icon_view_send_configure (QuiverIconView *iconview);

static gboolean  quiver_icon_view_configure_event (GtkWidget *widget,
                    GdkEventConfigure *event);
static gboolean  quiver_icon_view_expose_event (GtkWidget *iconview,
                    GdkEventExpose *event);
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

static void      quiver_icon_view_set_scroll_adjustments (QuiverIconView *iconview,
                    GtkAdjustment *hadjustment,
                    GtkAdjustment *vadjustment);


static void      remove_timeout_smooth_scroll(QuiverIconView *iconview);

static void      quiver_icon_view_adjustment_value_changed (GtkAdjustment *adjustment,
                    QuiverIconView *iconview);

static void      quiver_icon_view_scroll_to_cell_smooth(QuiverIconView *iconview, guint cell);

static gboolean  quiver_icon_view_timeout_smooth_scroll(gpointer data);

/*
static void      quiver_icon_view_set_property (GObject *object,
                    guint               prop_id,
                    const GValue       *value,
                    GParamSpec         *pspec);
static void      quiver_icon_view_get_property (GObject    *object,
                    guint       prop_id,
                    GValue     *value,
                    GParamSpec *pspec);

*/

static void     quiver_icon_view_finalize(GObject *object);

/* start utility function prototypes*/
static guint quiver_icon_view_get_width(QuiverIconView *iconview);
static guint quiver_icon_view_get_height(QuiverIconView *iconview);
static void
quiver_icon_view_set_adjustment_upper (GtkAdjustment *adj,
				 gdouble        upper,
				 gboolean       always_emit_changed);
static gint quiver_icon_view_get_cell_for_xy(QuiverIconView *iconview,gint x, gint y);
static void quiver_icon_view_set_cursor_cell_full(QuiverIconView *iconview,gint new_cursor_cell,GdkModifierType state,gboolean is_mouse);

static void quiver_icon_view_scroll_to_cell_force_top(QuiverIconView *iconview,guint cell,gboolean force_top);
static void quiver_icon_view_scroll_to_cell(QuiverIconView *iconview,guint cell);
static void quiver_icon_view_set_select_all(QuiverIconView *iconview, gboolean selected);
static void quiver_icon_view_shift_select_cells(QuiverIconView *iconview,guint new_cursor_cell);
static void quiver_icon_view_update_rubber_band(QuiverIconView *iconview);
static void quiver_icon_view_update_rubber_band_selection(QuiverIconView *iconview);
static void quiver_icon_view_update_icon_size(QuiverIconView *iconview);
static guint quiver_icon_view_get_n_items(QuiverIconView* iconview);
static GdkPixbuf* quiver_icon_view_get_thumbnail_pixbuf(QuiverIconView* iconview,guint cell);
static GdkPixbuf* quiver_icon_view_get_icon_pixbuf(QuiverIconView* iconview,guint cell);
static void quiver_icon_view_get_bound_size(guint bound_width,guint bound_height,guint *width,guint *height,gboolean fill_if_smaller);

static void quiver_icon_view_draw_drop_shadow(QuiverIconView *iconview,GdkDrawable *drawable,GtkStateType state, int rect_x,int rect_y, int rect_w, int rect_h);
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
	widget_class->size_request         = quiver_icon_view_size_request;
	widget_class->expose_event         = quiver_icon_view_expose_event;
	widget_class->configure_event      = quiver_icon_view_configure_event;
	widget_class->button_press_event   = quiver_icon_view_button_press_event;
	widget_class->button_release_event = quiver_icon_view_button_release_event;
	widget_class->motion_notify_event  = quiver_icon_view_motion_notify_event;
	widget_class->scroll_event         = quiver_icon_view_scroll_event;
	widget_class->key_press_event      = quiver_icon_view_key_press_event;
	widget_class->leave_notify_event   = quiver_icon_view_leave_notify_event;
	klass->set_scroll_adjustments      = quiver_icon_view_set_scroll_adjustments;

	/*
	obj_class->set_property            = quiver_icon_view_set_property;
	obj_class->get_property            = quiver_icon_view_get_property;
	*/
	obj_class->finalize                = quiver_icon_view_finalize;

	widget_class->set_scroll_adjustments_signal =
       g_signal_new (/*FIXME:- commented out*//*I_*/("set_scroll_adjustments"),
	     G_OBJECT_CLASS_TYPE (obj_class),
	     G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
	     G_STRUCT_OFFSET (QuiverIconViewClass, set_scroll_adjustments),
	     NULL, NULL,
	     _quiver_marshal_VOID__OBJECT_OBJECT,
	     G_TYPE_NONE, 2,
	     GTK_TYPE_ADJUSTMENT,
	     GTK_TYPE_ADJUSTMENT);

	g_type_class_add_private (obj_class, sizeof (QuiverIconViewPrivate));


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

	
	quiver_icon_view_set_scroll_adjustments(iconview,new_default_adjustment(),new_default_adjustment());
	//iconview->priv->hadjustment  = NULL;
	//iconview->priv->vadjustment  = NULL;

	iconview->priv->last_hadjustment = 0.0;
	iconview->priv->last_vadjustment = 0.0;

	iconview->priv->icon_width   = QUIVER_ICON_VIEW_ICON_WIDTH;
	iconview->priv->icon_height  = QUIVER_ICON_VIEW_ICON_HEIGHT;
	iconview->priv->icon_border_size  = QUIVER_ICON_VIEW_ICON_BORDER_SIZE;
	iconview->priv->cell_padding = QUIVER_ICON_VIEW_CELL_PADDING;

	iconview->priv->scroll_draw   = TRUE;
	iconview->priv->smooth_scroll = FALSE;
	iconview->priv->smooth_scroll_cell = -1;
	
	iconview->priv->rubberband_x1 = 0;
	iconview->priv->rubberband_y1 = 0;
	iconview->priv->rubberband_x2 = 0;
	iconview->priv->rubberband_y2 = 0;
	iconview->priv->rubberband_mode_start = FALSE;
	iconview->priv->rubberband_mode = FALSE;
	
	iconview->priv->timeout_id_rubberband_scroll = 0;
	
	iconview->priv->mouse_button_is_down = FALSE;

	iconview->priv->cursor_cell = -1;
	iconview->priv->prelight_cell = -1;

	/* setting these to 0 lets the widget decide how many
	 * columns and rows to have
	 */
	iconview->priv->n_columns = 0;
	iconview->priv->n_rows = 0;

	/* the following item is for multiselect */
	iconview->priv->cursor_cell_first = -1;


	GTK_WIDGET_SET_FLAGS(iconview,GTK_CAN_FOCUS);
	//GTK_WIDGET_UNSET_FLAGS(iconview,GTK_DOUBLE_BUFFERED);
	
	gint i,j;
	for (i = 0;i<5;i++)
		for (j = 0;j<8;j++)
			iconview->priv->drop_shadow[i][j] = NULL;

	//iconview->priv->cell_items = g_malloc0( (sizeof *iconview->priv->cell_items));
	iconview->priv->cell_items = (CellItem*)g_malloc0( sizeof(CellItem)*(iconview->priv->n_cell_items+1) );
	iconview->priv->n_cell_items = 0;

	//iconview->priv->cell_items[0].selected = TRUE;
	
}

static void
quiver_icon_view_finalize(GObject *object)
{
	GObjectClass *parent,*obj_class;
	QuiverIconViewClass *klass; 
	QuiverIconView *iconview;

	iconview = QUIVER_ICON_VIEW(object);
	klass = QUIVER_ICON_VIEW_GET_CLASS(iconview);
	obj_class = G_OBJECT_CLASS (klass);

	// remove timeout callbacks
	remove_timeout_smooth_scroll(iconview);

	if (iconview->priv->timeout_id_rubberband_scroll != 0)
	{
		g_source_remove (iconview->priv->timeout_id_rubberband_scroll);
		iconview->priv->timeout_id_rubberband_scroll = 0;
	}

	gint i,j;
	for (i = 0;i<5;i++)
	{
		for (j = 0;j<8;j++)
		{
			if (NULL != iconview->priv->drop_shadow[i][j])
			{
				g_object_unref(iconview->priv->drop_shadow[i][j]);
				iconview->priv->drop_shadow[i][j] = NULL;
			}
		}
	}

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
					   GDK_POINTER_MOTION_MASK | GDK_POINTER_MOTION_HINT_MASK |
					   GDK_LEAVE_NOTIFY_MASK;

	attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL | GDK_WA_COLORMAP;

	widget->window = gdk_window_new (gtk_widget_get_parent_window (widget), &attributes, attributes_mask);
	gdk_window_set_user_data (widget->window, iconview);

	widget->style = gtk_style_attach (widget->style, widget->window);
	gtk_style_set_background (widget->style, widget->window, GTK_STATE_NORMAL);

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

	QuiverIconView *iconview = QUIVER_ICON_VIEW(widget);

	widget->allocation = *allocation;

	if (GTK_WIDGET_REALIZED (widget))
	{
		gdk_window_move_resize (widget->window,
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
quiver_icon_view_send_configure (QuiverIconView *iconview)
{
  GtkWidget *widget;
  GdkEvent *event = gdk_event_new (GDK_CONFIGURE);

  widget = GTK_WIDGET (iconview);

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

	guint n_cells  = quiver_icon_view_get_n_items(iconview);
	guint cell_width = quiver_icon_view_get_cell_width(iconview);

	r = 0;
	
	c = widget->allocation.width / cell_width;
	
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
draw_pixmap (GtkWidget *widget, GdkRegion *in_region)
{
	QuiverIconView *iconview;
	iconview = QUIVER_ICON_VIEW(widget);

	guint cell_width = quiver_icon_view_get_cell_width(iconview);
	guint cell_height = quiver_icon_view_get_cell_height(iconview);

	GdkRectangle tmp_rect;
	
	GdkRectangle r;
	gdk_region_get_clipbox(in_region,&r);
	//printf("clipbox: %d %d %d %d\n",r.x , r.y , r.width, r.height);

	guint vadjust = (guint)gtk_adjustment_get_value(iconview->priv->vadjustment);
	guint hadjust = (guint)gtk_adjustment_get_value(iconview->priv->hadjustment);

	/* initial cell offset calculations */

	guint padding = iconview->priv->cell_padding;

	guint i = 0;
	guint j = 0;

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

	GdkGC *dotted_line_gc = gdk_gc_new(widget->window);
	GdkColor color;
	gdk_color_parse("#ddd",&color);
	gdk_gc_set_rgb_fg_color(dotted_line_gc,&color);
	gdk_gc_set_line_attributes(dotted_line_gc,1,GDK_LINE_ON_OFF_DASH,GDK_CAP_BUTT,GDK_JOIN_MITER);

	GdkGC *border_gc = gdk_gc_new(widget->window);
	
	//GdkColor color;
	gdk_color_parse("white",&color);
	gdk_gc_set_rgb_fg_color(border_gc,&color);

	gdk_gc_set_line_attributes(border_gc,iconview->priv->icon_border_size,GDK_LINE_SOLID,GDK_CAP_BUTT,GDK_JOIN_MITER);//GDK_CAP_BUTT,GDK_JOIN_MITER);


	// invalidate the free space region to the right of the item
	gdk_draw_rectangle (widget->window, widget->style->bg_gc[GTK_WIDGET_STATE(widget)],TRUE, num_cols*cell_width,r.y,widget->allocation.width - num_cols*cell_width,r.height);

	GtkStateType state;
	guint bound_width = cell_width - padding;
	guint bound_height = cell_height - padding;

	guint n_cells  = quiver_icon_view_get_n_items(iconview);
	for (j=row_start;j <= row_end; j++)
	{
		for (i = col_start;i<= col_end; i++)
		{
			gint current_cell = j * num_cols + i + num_adj_cells_y + num_adj_cols;


			guint x_cell_offset = i * cell_width;
			guint y_cell_offset = j * cell_height;

			x_cell_offset -= adj_offset_x;
			y_cell_offset -= adj_offset_y;

			tmp_rect.x = i * cell_width - adj_offset_x;
			tmp_rect.y = j * cell_height - adj_offset_y;
			tmp_rect.width = cell_width;
			tmp_rect.height = cell_height;

			if ( GDK_OVERLAP_RECTANGLE_OUT == gdk_region_rect_in(in_region,&tmp_rect) )
			{
				//printf("we do not need to redraw %d\n",current_cell);
				continue;
			}
			else
			{
				//printf("we need to redraw %d\n",current_cell);
			}

			// clear the cell
			gdk_draw_rectangle (widget->window, widget->style->bg_gc[GTK_WIDGET_STATE(widget)],TRUE, tmp_rect.x,tmp_rect.y,tmp_rect.width,tmp_rect.height);

			if ( current_cell >= n_cells)
			{
				// we are passed the last cell
				// so we can skip drawing the cells and just draw white
				continue;
			}

			state = (GtkStateType)GTK_WIDGET_STATE(widget);
			if ( iconview->priv->cell_items[current_cell].selected )
			{
				if (GTK_WIDGET_HAS_FOCUS(widget))
				{
					state = GTK_STATE_SELECTED;
				}
				else
				{
					state = GTK_STATE_ACTIVE;
				}
				gdk_draw_rectangle (widget->window, widget->style->bg_gc[state],TRUE,x_cell_offset+padding/2, y_cell_offset+padding/2, bound_width, bound_height);
			}
			else if (current_cell == iconview->priv->prelight_cell)
			{
				//state = GTK_STATE_PRELIGHT;
				//gdk_draw_rectangle (widget->window, widget->style->bg_gc[state],TRUE,x_cell_offset+padding/2, y_cell_offset+padding/2, bound_width, bound_height);
			}

		

			gboolean stock = FALSE;
			GdkPixbuf *pixbuf = quiver_icon_view_get_thumbnail_pixbuf(iconview,current_cell);
			if (NULL == pixbuf)
			{
				pixbuf = quiver_icon_view_get_icon_pixbuf(iconview,current_cell);
				stock = TRUE;
			}

			guint pixbuf_width,pixbuf_height;

			pixbuf_width = gdk_pixbuf_get_width(pixbuf);
			pixbuf_height = gdk_pixbuf_get_height(pixbuf);

			guint nw = pixbuf_width;
			guint nh = pixbuf_height;

			quiver_icon_view_get_bound_size(iconview->priv->icon_width,iconview->priv->icon_height,&nw,&nh,TRUE);

			if (!stock && (pixbuf_width != nw || pixbuf_height != nh ))
			{
				pixbuf_width = nw;
				pixbuf_height = nh;
				//printf("new size %d %d\n",nw,nh);
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

				gdk_draw_pixbuf	(widget->window,
	                     widget->style->fg_gc[GTK_WIDGET_STATE (widget)],
	                     pixbuf2,0,0,x_cell_offset + x_icon_offset,y_cell_offset+y_icon_offset,-1,-1,
	                     GDK_RGB_DITHER_NONE,0,0);
				g_object_unref(pixbuf2);
			}
			else
			{
				/*
				if (!GTK_WIDGET_HAS_FOCUS(widget))
				{
					GdkPixbuf *pixbuf_gray = gdk_pixbuf_copy(pixbuf);
					pixbuf_set_grayscale(pixbuf_gray);
					g_object_unref(pixbuf);
					pixbuf = pixbuf_gray;

				}
				*/

				gdk_draw_pixbuf	(widget->window,
	                     widget->style->fg_gc[GTK_WIDGET_STATE (widget)],
	                     pixbuf,0,0,x_cell_offset + x_icon_offset,y_cell_offset+y_icon_offset,-1,-1,
	                     GDK_RGB_DITHER_NONE,0,0);
			}

			g_object_unref(pixbuf);

			if (!stock)
			{
				/*
				if (40 <= iconview->priv->icon_width)
				{
				*/
				guint border = iconview->priv->icon_border_size;
				quiver_icon_view_draw_drop_shadow(iconview,widget->window,	state, 
						x_cell_offset + x_icon_offset - border, 
						y_cell_offset + y_icon_offset - border, 
						pixbuf_width + border*2, 
						pixbuf_height + border *2);
					/*
				}
				*/
				// draw a border around the thumbnail
				if (0 < border)
				{
					gint border_offset = ceill(border/2.);
					//printf("border offset: %d\n",border_offset);
					gdk_draw_rectangle (widget->window, border_gc,FALSE,
						x_cell_offset + x_icon_offset - border_offset,
						y_cell_offset + y_icon_offset - border_offset,
						pixbuf_width + border,
						pixbuf_height + border);
				}
				/*
						x_cell_offset + x_icon_offset - (ceill(border/2.)),
						y_cell_offset + y_icon_offset - (ceill(border/2.)),
						pixbuf_width + border,
						pixbuf_height + border);
						*/
				//gdk_draw_rectangle (widget->window, widget->style->white_gc,FALSE,x_cell_offset + x_icon_offset -1, y_cell_offset + y_icon_offset -1, pixbuf_width+1, pixbuf_height+1);
			}

			// draw a border around the cell
			// gdk_draw_rectangle (widget->window, dotted_line_gc,FALSE, x_cell_offset+padding/2, y_cell_offset+padding/2, bound_width-1, bound_height-1);
			
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

						gdk_draw_pixbuf	(widget->window,
								 widget->style->fg_gc[GTK_WIDGET_STATE (widget)],
								 overlay,0,0,x_cell_offset + padding/2 + 2 + 16*k ,y_cell_offset + padding/2 +2,-1,-1,
								 GDK_RGB_DITHER_NONE,0,0);
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

			layout = gtk_widget_create_pango_layout (widget, NULL);

			pango_layout_set_text (layout, cell_string, -1);
			pango_layout_get_pixel_extents (layout, NULL, &logical_rect);

			gdk_draw_layout(widget->window, widget->style->fg_gc[state], x_cell_offset + 10, y_cell_offset + 10,layout);
			g_object_unref(layout);
			*/


			if (GTK_WIDGET_HAS_FOCUS(widget) && current_cell == iconview->priv->cursor_cell)
			{
				// gtk stuff
				//state = GTK_STATE_SELECTED;
				gtk_paint_focus (widget->style,
						 widget->window,
						 GTK_STATE_NORMAL,
						 NULL, widget, NULL,
						 x_cell_offset-1 + padding/2,
						 y_cell_offset-1 + padding/2,
						 bound_width+2,
						 bound_height +2);//x_cell_offset, y_cell_offset, bound_width, bound_height);



			}
		}
	}
	g_object_unref(dotted_line_gc);
	g_object_unref(border_gc);
	
	
	if (iconview->priv->rubberband_mode)
	{
		cairo_t *cr = gdk_cairo_create(widget->window);

		GdkColor *fill_color_gdk;
		guchar fill_color_alpha;

		gtk_widget_style_get (widget,
			"selection-box-color", &fill_color_gdk,
			"selection-box-alpha", &fill_color_alpha,
			 NULL);
		
		if (!fill_color_gdk)
		fill_color_gdk = gdk_color_copy (&widget->style->base[GTK_STATE_SELECTED]);

		cairo_set_source_rgba (cr,
			fill_color_gdk->red / 65535.,
			fill_color_gdk->green / 65535.,
			fill_color_gdk->blue / 65535.,
			fill_color_alpha / 255.);

		GdkRectangle rub_rect = iconview->priv->rubberband_rect;
		rub_rect.x -= hadjust;
		rub_rect.y -= vadjust;
		
		GdkRegion *rubber_region = gdk_region_rectangle(&rub_rect);

		gdk_region_intersect(rubber_region,in_region);
		
		int i;
		int n_rectangles =0;
		GdkRectangle *rects = NULL;

		gdk_region_get_rectangles(rubber_region,&rects,&n_rectangles);
		for (i = 0; i < n_rectangles; i++)
		{
			//printf("rect %d: %d %d %d %d \n",i,rects[i].x, rects[i].y, rects[i].width, rects[i].height);
			gdk_cairo_rectangle (cr,
				&rects[i]);	
		}
		g_free(rects);

	
		cairo_clip(cr);
		cairo_paint(cr);
		
		
		cairo_set_source_rgb (cr,
			fill_color_gdk->red / 65535.,
			fill_color_gdk->green / 65535.,
			fill_color_gdk->blue / 65535.);



		cairo_destroy(cr);
		
		GdkGC *color_gc = gdk_gc_new(widget->window);
		gdk_gc_set_rgb_fg_color(color_gc,fill_color_gdk);

		gdk_draw_rectangle (widget->window,
				color_gc,
				FALSE,
				rub_rect.x,
				rub_rect.y,
				rub_rect.width,
				rub_rect.height
				);
		g_object_unref(color_gc);
		
		gdk_color_free (fill_color_gdk);
	}


}


static gboolean
quiver_icon_view_expose_event (GtkWidget *widget, GdkEventExpose *event)
{

	QuiverIconView *iconview;
	iconview = QUIVER_ICON_VIEW(widget);

	draw_pixmap (widget,event->region);

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

	gint vadjust = (gint)gtk_adjustment_get_value(iconview->priv->vadjustment);
	gint hadjust = (gint)gtk_adjustment_get_value(iconview->priv->hadjustment);

	iconview->priv->mouse_button_is_down = TRUE;

	iconview->priv->rubberband_x1 = iconview->priv->rubberband_x2 = x + hadjust;
	iconview->priv->rubberband_y1 = iconview->priv->rubberband_y2 = y + vadjust;

	//int cell_x,cell_y;
	int cell = quiver_icon_view_get_cell_for_xy (iconview,x,y);

	if (!GTK_WIDGET_HAS_FOCUS (widget))
	{
		gtk_widget_grab_focus (widget);
	}


	if (cell == -1)
	{
		if (0 == (event->state & GDK_CONTROL_MASK))
		{
			quiver_icon_view_set_select_all(iconview,FALSE);
		}
		iconview->priv->rubberband_mode_start = TRUE;
	}
	else if (1 == event->button && GDK_2BUTTON_PRESS == event->type)
	{
		iconview->priv->mouse_button_is_down = FALSE;
		quiver_icon_view_activate_cell(iconview,cell);
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

	//int cell_x,cell_y;
	int cell = quiver_icon_view_get_cell_for_xy (iconview,x,y);

	if (iconview->priv->rubberband_mode)
	{
		/* this commented out code is for non-solid rect only
		GdkRegion *invalid_region;
		GdkRegion *tmp_region;

		tmp_region = gdk_region_rectangle(&iconview->priv->rubberband_rect);
		gdk_region_shrink(tmp_region,1,1);

		invalid_region = gdk_region_rectangle(&iconview->priv->rubberband_rect);

		gdk_region_subtract(invalid_region,tmp_region);
		gdk_region_shrink(invalid_region,-1,-1);

		gdk_window_invalidate_region(widget->window,invalid_region,FALSE);
		gdk_region_destroy(invalid_region);
		gdk_region_destroy(tmp_region);
		*/		
		GdkRegion *invalid_region;
		invalid_region = gdk_region_rectangle(&iconview->priv->rubberband_rect);
		gdk_region_shrink(invalid_region,-1,-1);
		gdk_region_offset(invalid_region,-hadjust,-vadjust);
		
		gdk_window_invalidate_region(widget->window,invalid_region,FALSE);
		
		if (iconview->priv->timeout_id_rubberband_scroll != 0)
		{
			g_source_remove (iconview->priv->timeout_id_rubberband_scroll);
			iconview->priv->timeout_id_rubberband_scroll = 0;
		}
		
	}
	else if (-1 != cell && iconview->priv->mouse_button_is_down)
	{
		quiver_icon_view_set_cursor_cell_full(iconview,cell,(GdkModifierType)event->state,TRUE);
	}
	iconview->priv->mouse_button_is_down = FALSE;
	iconview->priv->rubberband_mode_start = FALSE;
	iconview->priv->rubberband_mode = FALSE;
	return TRUE;
}

static gboolean
rubberband_scroll_timeout (gpointer data)
{
	QuiverIconView *iconview;
	gdouble xvalue,yvalue;
	
	gdk_threads_enter ();
	  
	iconview = data;
	
	xvalue = MIN (iconview->priv->hadjustment->value +
		iconview->priv->rubberband_scroll_x,
		iconview->priv->hadjustment->upper -
		iconview->priv->hadjustment->page_size);

	yvalue = MIN (iconview->priv->vadjustment->value +
		iconview->priv->rubberband_scroll_y,
		iconview->priv->vadjustment->upper -
		iconview->priv->vadjustment->page_size);
	
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
		gdk_window_get_pointer (event->window, &x, &y, &state);
	}		
	else
	{
		x = (gint)event->x;
		y = (gint)event->y;
		state = (GdkModifierType)event->state;
		
	}

	if (iconview->priv->rubberband_mode_start)
	{
		iconview->priv->rubberband_mode_start = FALSE;
		iconview->priv->rubberband_mode = TRUE;
	}

	gint new_cell = quiver_icon_view_get_cell_for_xy (iconview,x,y);

	if (iconview->priv->prelight_cell != new_cell)
	{
		if (-1 != iconview->priv->prelight_cell)
		{
			quiver_icon_view_invalidate_cell(iconview,iconview->priv->prelight_cell);
		}
		iconview->priv->prelight_cell = new_cell;
		if (-1 != iconview->priv->prelight_cell)
		{
			quiver_icon_view_invalidate_cell(iconview,iconview->priv->prelight_cell);
			if (iconview->priv->callback_get_text)
			{
				gchar *text = (*iconview->priv->callback_get_text)(iconview,iconview->priv->prelight_cell,iconview->priv->callback_get_text_data);
				printf("Filename: %s\n",text);
				g_free(text);
			}
		}
	}


	//printf("new_cell : %d \n",new_cell);

	if (iconview->priv->rubberband_mode)
	{
		quiver_icon_view_update_rubber_band(iconview);
		iconview->priv->rubberband_scroll_x = 0;
		iconview->priv->rubberband_scroll_y = 0;
		
		/* check if the window needs to scroll */
		if (x < 0 || x > widget->allocation.width)
		{
			if (x < 0)
				iconview->priv->rubberband_scroll_x = x;
			else
				iconview->priv->rubberband_scroll_x = x - widget->allocation.width;
	
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
		if (y < 0 || y > widget->allocation.height)
		{
			if (y < 0)
				iconview->priv->rubberband_scroll_y = y;
			else
				iconview->priv->rubberband_scroll_y = y - widget->allocation.height;
	
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
		return TRUE;

	}
	return FALSE;

}

gboolean quiver_icon_view_scroll_event ( GtkWidget *widget,
           GdkEventScroll *event)
{
	
	QuiverIconView *iconview;
	iconview = QUIVER_ICON_VIEW(widget);

	gint adjust;
		
	remove_timeout_smooth_scroll(iconview);
		
	if (1 == iconview->priv->n_rows)
	{
		adjust = (gint)gtk_adjustment_get_value(iconview->priv->hadjustment);

		if (GDK_SCROLL_UP == event->direction)
		{
			adjust -= iconview->priv->hadjustment->step_increment;
		}
		else if (GDK_SCROLL_DOWN == event->direction)
		{
			adjust += iconview->priv->hadjustment->step_increment;
		}

		if (adjust < iconview->priv->hadjustment->lower)
			adjust = iconview->priv->hadjustment->lower;
		else if (adjust > iconview->priv->hadjustment->upper - iconview->priv->hadjustment->page_size)
			adjust = iconview->priv->hadjustment->upper - iconview->priv->hadjustment->page_size;

		gtk_adjustment_set_value(iconview->priv->hadjustment,adjust);
	}
	else
	{
		adjust = (gint)gtk_adjustment_get_value(iconview->priv->vadjustment);

		if (GDK_SCROLL_UP == event->direction)
		{
			adjust -= iconview->priv->vadjustment->step_increment;
		}
		else if (GDK_SCROLL_DOWN == event->direction)
		{
			adjust += iconview->priv->vadjustment->step_increment;
		}

		if (adjust < iconview->priv->vadjustment->lower)
			adjust = iconview->priv->vadjustment->lower;
		else if (adjust > iconview->priv->vadjustment->upper - iconview->priv->vadjustment->page_size)
			adjust = iconview->priv->vadjustment->upper - iconview->priv->vadjustment->page_size;

		gtk_adjustment_set_value(iconview->priv->vadjustment,adjust);
	}

	return TRUE;
}

static gboolean
quiver_icon_view_key_press_event  (GtkWidget *widget,
                   GdkEventKey *event)
{
	QuiverIconView *iconview;
	iconview = QUIVER_ICON_VIEW(widget);

	guint n_cells  = quiver_icon_view_get_n_items(iconview);

	//printf("key pressed\n");
	guint cols,rows;
	quiver_icon_view_get_col_row_count(iconview,&cols,&rows);
	gboolean rval = TRUE;


	//guint cell_width = quiver_icon_view_get_cell_width(iconview);
	guint cell_height = quiver_icon_view_get_cell_height(iconview);

	/* FIXME: we should not use the size from the widget but 
	 * rather have our own width value and use it */
	guint rows_per_page = widget->allocation.height/cell_height;
	if (rows_per_page == 0)
	{
		rows_per_page = 1;
	}
	guint n_cells_per_page = cols * rows_per_page;
	//printf("cols: %d ,rows %d, n cells %d\n",cols,rows_per_page,n_cells_per_page);

	gint new_cursor_cell = iconview->priv->cursor_cell;
	
	switch(event->keyval)
	{
		case GDK_Return:
		case GDK_KP_Enter:
			quiver_icon_view_activate_cell(iconview,iconview->priv->cursor_cell);
			break;
		case GDK_a:
		case GDK_A:
			if (event->state & GDK_CONTROL_MASK)
			{
				quiver_icon_view_set_select_all(iconview,TRUE);
			}
			break;
		case GDK_space:
			if (event->state & GDK_CONTROL_MASK)
			{
				iconview->priv->cell_items[iconview->priv->cursor_cell].selected = !iconview->priv->cell_items[iconview->priv->cursor_cell].selected;
				g_signal_emit(iconview,iconview_signals[SIGNAL_SELECTION_CHANGED],0);
				quiver_icon_view_invalidate_cell(iconview,iconview->priv->cursor_cell);
			}
			break;

		case GDK_KP_Left:
		case GDK_Left:
			//printf("left %d\n",cursor_cell);
			new_cursor_cell = iconview->priv->cursor_cell-1;
			break;
		case GDK_KP_Right:
		case GDK_Right:
			//printf("right %d\n",cursor_cell);
			new_cursor_cell = iconview->priv->cursor_cell+1;
			break;
		case GDK_KP_Up:
		case GDK_Up:
			//printf("up %d, %d\n",cursor_cell, cursor_cell-cols);
			new_cursor_cell = iconview->priv->cursor_cell - cols;
			break;
		case GDK_KP_Down:
		case GDK_Down:
			//printf("down %d\n",cursor_cell);
			new_cursor_cell = iconview->priv->cursor_cell + cols;
			break;
		case GDK_Home:
			new_cursor_cell = 0;
			break;
		case GDK_End:
			new_cursor_cell = n_cells -1;
			break;

		case GDK_Page_Up:
			{
				new_cursor_cell -= n_cells_per_page;
			}
			if (new_cursor_cell <= 0)
			{
				new_cursor_cell = 0;
			}
			break;
		case GDK_Page_Down:
			{
				new_cursor_cell += n_cells_per_page;
			}
			if ((gint)n_cells <= new_cursor_cell)
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

	if (0 <= new_cursor_cell && new_cursor_cell < (gint)n_cells)
	{
		if (new_cursor_cell != (gint)iconview->priv->cursor_cell)
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
	return rval;
}

static gboolean
quiver_icon_view_leave_notify_event (GtkWidget *widget,
       GdkEventCrossing *event)
{
	QuiverIconView *iconview;
	iconview = QUIVER_ICON_VIEW(widget);
	if (-1 != iconview->priv->prelight_cell)
	{
		quiver_icon_view_invalidate_cell(iconview,iconview->priv->prelight_cell);
		iconview->priv->prelight_cell = -1;
	}
	return FALSE;
}


static void           
quiver_icon_view_set_scroll_adjustments (QuiverIconView     *iconview,
			    GtkAdjustment *hadj,
			    GtkAdjustment *vadj)
{
	GtkWidget *widget;
	gboolean need_adjust = FALSE;

	g_return_if_fail (QUIVER_IS_ICON_VIEW (iconview));

	widget = GTK_WIDGET(iconview);

	if (hadj)
		g_return_if_fail (GTK_IS_ADJUSTMENT (hadj));
	if (vadj)
		g_return_if_fail (GTK_IS_ADJUSTMENT (vadj));

	if (iconview->priv->hadjustment && (iconview->priv->hadjustment != hadj))
	{
		g_signal_handlers_disconnect_by_func (iconview->priv->hadjustment,
			quiver_icon_view_adjustment_value_changed,
			iconview);
		g_object_unref (iconview->priv->hadjustment);
		iconview->priv->hadjustment = NULL;
	}

	if (iconview->priv->vadjustment && (iconview->priv->vadjustment != vadj))
	{
		g_signal_handlers_disconnect_by_func (iconview->priv->vadjustment,
			quiver_icon_view_adjustment_value_changed,
			iconview);
		g_object_unref (iconview->priv->vadjustment);
		iconview->priv->vadjustment = NULL;
	}

	if (iconview->priv->hadjustment != hadj)
	{
		iconview->priv->hadjustment = hadj;
		g_object_ref_sink (iconview->priv->hadjustment);
	
		if (GTK_WIDGET_REALIZED (widget))
		{
			guint width = quiver_icon_view_get_width(iconview);
			quiver_icon_view_set_adjustment_upper (iconview->priv->hadjustment, width, FALSE);
			need_adjust = TRUE;
		}
		g_signal_connect (iconview->priv->hadjustment, "value_changed",
		G_CALLBACK (quiver_icon_view_adjustment_value_changed),
			iconview);
	}

	if (iconview->priv->vadjustment != vadj)
	{
		iconview->priv->vadjustment = vadj;
		g_object_ref_sink (iconview->priv->vadjustment);
		if (GTK_WIDGET_REALIZED (widget))
		{
	
			guint height = quiver_icon_view_get_height(iconview);
			quiver_icon_view_set_adjustment_upper (iconview->priv->vadjustment, height, FALSE);
			need_adjust = TRUE;
		}

		g_signal_connect (iconview->priv->vadjustment, "value_changed",
			G_CALLBACK (quiver_icon_view_adjustment_value_changed),
			iconview);
	}


	/* vadj or hadj can be NULL while constructing; don't emit a signal
	then */
	if (need_adjust && vadj && hadj)
		quiver_icon_view_adjustment_value_changed (NULL, iconview);
}


static void remove_timeout_smooth_scroll(QuiverIconView *iconview)
{
	if (0 != iconview->priv->timeout_id_smooth_scroll)
	{
		g_source_remove (iconview->priv->timeout_id_smooth_scroll);
		iconview->priv->timeout_id_smooth_scroll = 0;
	}
}
		
static void quiver_icon_view_scroll_to_cell_smooth(QuiverIconView *iconview, guint cell)
{
	remove_timeout_smooth_scroll(iconview);

	iconview->priv->smooth_scroll_cell = cell;

	iconview->priv->timeout_id_smooth_scroll = g_timeout_add(30,quiver_icon_view_timeout_smooth_scroll,iconview);
}

static gboolean 
quiver_icon_view_timeout_smooth_scroll(gpointer data)
{
	gboolean hdone = FALSE; 
	gboolean vdone = FALSE; 

	QuiverIconView *iconview = (QuiverIconView*)data;

	gdk_threads_enter();
	
	gint cell = iconview->priv->smooth_scroll_cell;

	if (-1 == cell)
	{
		gdk_threads_leave();
		return FALSE;
	}

	guint cols,rows;
	quiver_icon_view_get_col_row_count(iconview,&cols,&rows);

	guint cell_width = quiver_icon_view_get_cell_width(iconview);
	guint cell_height = quiver_icon_view_get_cell_height(iconview);

	gint hadjust = (guint)gtk_adjustment_get_value(iconview->priv->hadjustment);
	gint vadjust = (guint)gtk_adjustment_get_value(iconview->priv->vadjustment);

	guint cell_x = (cell % cols) * cell_width;
	guint cell_y = (cell / cols) * cell_height;

	gint new_hadjust = cell_x - iconview->priv->hadjustment->page_size/2 + cell_width/2;
	new_hadjust = MAX (0,new_hadjust);
	new_hadjust = MIN (new_hadjust,iconview->priv->hadjustment->upper - iconview->priv->hadjustment->page_size);
	
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

	gint new_vadjust = cell_y - iconview->priv->vadjustment->page_size/2 + cell_height/2;
	new_vadjust = MAX (0,new_vadjust);
	new_vadjust = MIN (new_vadjust,iconview->priv->vadjustment->upper - iconview->priv->vadjustment->page_size);

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
		gdk_threads_leave();
		return FALSE;
	}
	
	gdk_threads_leave();
	
	return TRUE;
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

	if (GTK_WIDGET_REALIZED (iconview))
	{
		if (iconview->priv->scroll_draw)
		{
			gint hdiff = (gint)(iconview->priv->last_hadjustment - hadj);
			gint vdiff = (gint)(iconview->priv->last_vadjustment - vadj);

			gdk_window_scroll(widget->window,hdiff,vdiff);
			gdk_window_process_updates (widget->window, FALSE);
		}
		iconview->priv->last_vadjustment = vadj;
		iconview->priv->last_hadjustment = hadj;
	}
}

/*

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
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

*/

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

static gint
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
		return -1;

	remainder = (x+hadjust) - current_col * cell_width;
	// check if x is in padding area, if so return -1
	if (padding/2 > remainder || padding/2 > cell_width-remainder)
		return -1;
	
	current_row =  (y +vadjust) / cell_height;

	//GdkPixbuf *pixbuf = GetStockIcon(f);



	
	remainder = (y + vadjust) - current_row * cell_height;
	if (padding/2 > remainder || padding/2 > cell_height-remainder)
		return -1;


	guint cell = current_row * cols + current_col;
	guint n_cells  = quiver_icon_view_get_n_items(iconview);
	if (n_cells < cell+1)
		return -1;

	/*
	 * FIXME

	QuiverFile f = image_list[cell];
	
	*/
	GdkPixbuf *pixbuf = quiver_icon_view_get_thumbnail_pixbuf(iconview,cell);
	if (NULL != pixbuf)
	{
		guint pixbuf_width,pixbuf_height;
		pixbuf_width = gdk_pixbuf_get_width(pixbuf);
		pixbuf_height = gdk_pixbuf_get_height(pixbuf);

		quiver_icon_view_get_bound_size(iconview->priv->icon_width,iconview->priv->icon_height,&pixbuf_width,&pixbuf_height,TRUE);

		GdkRectangle rect;
		rect.x = current_col * cell_width + ((cell_width - pixbuf_width)/2);
		rect.y = current_row * cell_width + ((cell_height - pixbuf_height)/2);
		rect.width = pixbuf_width;
		rect.height = pixbuf_height;
		x += hadjust;
		y += vadjust;

		g_object_unref(pixbuf);

		if (x < rect.x || x > rect.x + rect.width || y < rect.y || y > rect.y + rect.height)
			return -1;
		
	}

	return cell;
}


static void
quiver_icon_view_set_cursor_cell_full(QuiverIconView *iconview,gint new_cursor_cell,GdkModifierType state,gboolean is_mouse)
{
	gint old_cell = iconview->priv->cursor_cell;
	
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
		iconview->priv->cursor_cell_first = -1;
	}
	else if (state & GDK_SHIFT_MASK)
	{
		if (-1 == iconview->priv->cursor_cell_first)
		{
			iconview->priv->cursor_cell_first = iconview->priv->cursor_cell;
		}
		quiver_icon_view_shift_select_cells(iconview,new_cursor_cell);
	}
	else
	{
		// normal mode, deselect all, select new cell
		iconview->priv->cursor_cell_first = -1;

		quiver_icon_view_invalidate_cell(iconview,iconview->priv->cursor_cell);

		// check the selection
		guint i;
		guint n_cells  = quiver_icon_view_get_n_items(iconview);
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
quiver_icon_view_scroll_to_cell_force_top(QuiverIconView *iconview,guint cell,gboolean force_top)
{
	guint cols,rows;
	quiver_icon_view_get_col_row_count(iconview,&cols,&rows);

	guint cell_width = quiver_icon_view_get_cell_width(iconview);
	guint cell_height = quiver_icon_view_get_cell_height(iconview);

	gint hadjust = (guint)gtk_adjustment_get_value(iconview->priv->hadjustment);
	gint vadjust = (guint)gtk_adjustment_get_value(iconview->priv->vadjustment);

	guint cell_x = (cell % cols) * cell_width;
	guint cell_y = (cell / cols) * cell_height;

	/*
	if ( force_top || cell_y < vadjust)
	{
		if (cell_y < iconview->priv->vadjustment->upper - iconview->priv->vadjustment->page_size)
			gtk_adjustment_set_value(iconview->priv->vadjustment,cell_y);
		else
			gtk_adjustment_set_value(iconview->priv->vadjustment,iconview->priv->vadjustment->upper - iconview->priv->vadjustment->page_size);
	}
	else if (vadjust + iconview->priv->vadjustment->page_size < cell_y+cell_height)
	{
		gtk_adjustment_set_value(iconview->priv->vadjustment,cell_y+cell_height -iconview->priv->vadjustment->page_size);
	}
	*/
	
	if (iconview->priv->smooth_scroll && !force_top)
	{
		quiver_icon_view_scroll_to_cell_smooth(iconview,cell);

		return;
	}

	/* horizontal adjustment */
	gint new_hadjust;
	if (iconview->priv->smooth_scroll)
	{
		new_hadjust = cell_x - iconview->priv->hadjustment->page_size/2 + cell_width/2;
	}
	else if (cell_x < hadjust || force_top)
	{
		new_hadjust = cell_x;
	}
	else if (cell_x > hadjust + iconview->priv->hadjustment->page_size - cell_width)
	{
		new_hadjust = cell_x + cell_width - iconview->priv->hadjustment->page_size;
	}
	else
	{
		new_hadjust = hadjust;
	}

	new_hadjust = MAX (0,new_hadjust);
	new_hadjust = MIN (new_hadjust,iconview->priv->hadjustment->upper - iconview->priv->hadjustment->page_size);

	if (new_hadjust != hadjust)
	{
		gtk_adjustment_set_value(iconview->priv->hadjustment, new_hadjust);
	}

	/* vertical adjustment */
	gint new_vadjust;
	if (iconview->priv->smooth_scroll)
	{
		new_vadjust = cell_y - iconview->priv->vadjustment->page_size/2 + cell_height/2;
	}
	else if (cell_y < vadjust || force_top)
	{
		new_vadjust = cell_y;
	}
	else if (cell_y > vadjust + iconview->priv->vadjustment->page_size - cell_height)
	{
		new_vadjust = cell_y + cell_height - iconview->priv->vadjustment->page_size;
	}
	else
	{
		new_vadjust = vadjust;
	}

	new_vadjust = MAX (0,new_vadjust);
	new_vadjust = MIN (new_vadjust,iconview->priv->vadjustment->upper - iconview->priv->vadjustment->page_size);

	if (new_vadjust != vadjust)
	{
		gtk_adjustment_set_value(iconview->priv->vadjustment, new_vadjust);
	}
}

static void
quiver_icon_view_scroll_to_cell(QuiverIconView *iconview,guint cell)
{
	quiver_icon_view_scroll_to_cell_force_top(iconview,cell,FALSE);
}
static void
quiver_icon_view_set_select_all(QuiverIconView *iconview, gboolean selected)
{
	guint i;
	guint n_cells  = quiver_icon_view_get_n_items(iconview);
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
quiver_icon_view_shift_select_cells(QuiverIconView *iconview,guint new_cursor_cell)
{
	guint i;
	gboolean selection_changed;
	selection_changed = FALSE;

	guint first_cell = (guint)iconview->priv->cursor_cell_first;



	guint select_range_start = MIN(first_cell,new_cursor_cell);
	guint select_range_end = MAX(first_cell,new_cursor_cell);

	guint start = MIN(first_cell,MIN(iconview->priv->cursor_cell,new_cursor_cell));
	guint end = MAX(first_cell,MAX(iconview->priv->cursor_cell,new_cursor_cell));


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
	GdkRegion *invalid_region;
	GdkRegion *old_region;
	GdkModifierType state;
	GtkWidget *widget;
	
	widget = GTK_WIDGET (iconview);
	gdk_window_get_pointer (widget->window, &x, &y, &state);
		
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
	
	invalid_region = gdk_region_rectangle(&iconview->priv->rubberband_rect);
	old_region = gdk_region_rectangle(&iconview->priv->rubberband_rect_old);
	gdk_region_xor(invalid_region,old_region);
	gdk_region_shrink(invalid_region,-1,-1);
	
	quiver_icon_view_update_rubber_band_selection(iconview);
	
	// the way that gdk draws rectangles means we need to subtract
	// one from the width and height
	iconview->priv->rubberband_rect.width -= 1;
	iconview->priv->rubberband_rect.height -= 1;
	//rubberband_rect_old.width -= 1;
	//rubberband_rect_old.height -= 1;
	
	//redraw_needed = TRUE;
	gdk_region_offset(invalid_region,-hadjust,-vadjust);
	gdk_window_invalidate_region(widget->window,invalid_region,FALSE);
	gdk_region_destroy(invalid_region);
	gdk_region_destroy(old_region);
		
		
	
}

static void
quiver_icon_view_update_rubber_band_selection(QuiverIconView *iconview)
{
	gboolean selection_changed;

	selection_changed = FALSE;

	guint cell_width = quiver_icon_view_get_cell_width(iconview);
	guint cell_height = quiver_icon_view_get_cell_height(iconview);
	//guint padding = QUIVER_ICON_VIEW_CELL_PADDING;

	guint i = 0;
	guint j = 0;

	guint num_cols,num_rows;
	quiver_icon_view_get_col_row_count(iconview,&num_cols,&num_rows);

	GdkRegion *old_region;
	GdkRegion *new_region;
	new_region = gdk_region_rectangle(&iconview->priv->rubberband_rect);
	old_region = gdk_region_rectangle(&iconview->priv->rubberband_rect_old);

	GdkRectangle tmp_rect;

	/* FIXME : this is not optimized. currently it iterates over all cells */
	guint n_cells  = quiver_icon_view_get_n_items(iconview);
	for (j=0;j <= num_rows; j++)
	{
		for (i = 0;i< num_cols; i ++)
		{
			int current_cell = j * num_cols + i;
			if (current_cell >= n_cells)
				break;

			tmp_rect.x = i * cell_width;
			tmp_rect.y = j * cell_height;
			tmp_rect.width = cell_width;
			tmp_rect.height = cell_height;

			if ( GDK_OVERLAP_RECTANGLE_OUT != gdk_region_rect_in(new_region,&tmp_rect) )
			{
				// select!
				if (!iconview->priv->cell_items[current_cell].selected)
				{
					iconview->priv->cell_items[current_cell].selected = TRUE;
					quiver_icon_view_invalidate_cell(iconview,current_cell);
					selection_changed = TRUE;
				}
			}
			else if ( GDK_OVERLAP_RECTANGLE_OUT != gdk_region_rect_in(old_region,&tmp_rect) )
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

	gdk_region_destroy(new_region);
	gdk_region_destroy(old_region);
}


static void
quiver_icon_view_update_icon_size(QuiverIconView *iconview)
{
	GtkWidget *widget = GTK_WIDGET(iconview);

	if (!GTK_WIDGET_REALIZED (widget))
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

	hadjustment->page_size = widget->allocation.width;
	hadjustment->page_increment = widget->allocation.width ;
	hadjustment->step_increment = cell_width;
	hadjustment->lower = 0;
	hadjustment->upper = MAX (widget->allocation.width, width);

	if (hadjustment->value > hadjustment->upper - hadjustment->page_size)
		gtk_adjustment_set_value (hadjustment, MAX (0, hadjustment->upper - hadjustment->page_size));

	vadjustment->page_size = widget->allocation.height;
	vadjustment->page_increment = widget->allocation.height;
	vadjustment->step_increment = cell_height;
	vadjustment->lower = 0;
	vadjustment->upper = MAX (widget->allocation.height, height);

	/* FIXME: We should have an option for this if the user doesnt want resize to keep
	 * the current focus cell in view
	 * */
	/*
	if (vadjustment->value > vadjustment->upper - vadjustment->page_size)
		gtk_adjustment_set_value (vadjustment, MAX (0, vadjustment->upper - vadjustment->page_size));
		*/

	gtk_adjustment_changed (hadjustment);
	gtk_adjustment_changed (vadjustment);


	iconview->priv->scroll_draw = FALSE;

	quiver_icon_view_scroll_to_cell_force_top(iconview,iconview->priv->cursor_cell,TRUE);

	//printf("invalidating whole area\n");

	GdkRectangle rect;
	rect.x = 0;
	rect.y = 0;
	rect.width = widget->allocation.width;
	rect.height = widget->allocation.height;
	if (GTK_WIDGET_REALIZED(widget))
	{
		gdk_window_invalidate_rect(widget->window,&rect,FALSE);
		gdk_window_process_updates(widget->window,FALSE);
	}

	iconview->priv->scroll_draw = TRUE;

}

static guint
quiver_icon_view_get_n_items(QuiverIconView* iconview)
{
	guint n_items = 0;
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
	}
	return n_items;
}

static GdkPixbuf* quiver_icon_view_get_thumbnail_pixbuf(QuiverIconView* iconview,guint cell)
{
	GdkPixbuf *pixbuf = NULL;
	if (iconview->priv->callback_get_thumbnail_pixbuf)
		pixbuf = (*iconview->priv->callback_get_thumbnail_pixbuf)(iconview,cell,iconview->priv->callback_get_thumbnail_pixbuf_data);
	return pixbuf;
}

static GdkPixbuf* quiver_icon_view_get_icon_pixbuf(QuiverIconView* iconview,guint cell)
{
	GdkPixbuf *pixbuf = NULL;
	if (iconview->priv->callback_get_icon_pixbuf)
		pixbuf = (*iconview->priv->callback_get_icon_pixbuf)(iconview,cell,iconview->priv->callback_get_icon_pixbuf_data);
	return pixbuf;
}

static void quiver_icon_view_get_bound_size(guint bound_width,guint bound_height,guint *width,guint *height,gboolean fill_if_smaller)
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

		new_height = bound_width/ratio;
		if (new_height < bound_height)
		{
			*width = bound_width;
			*height = new_height;
		}
		else
		{
			*width = bound_height *ratio;
			*height = bound_height;
		}
	}
}	

static void quiver_icon_view_draw_drop_shadow(QuiverIconView *iconview,GdkDrawable *drawable,GtkStateType state, int rect_x,int rect_y, int rect_w, int rect_h)
{

	GtkWidget *widget = GTK_WIDGET(iconview);
	int shadow_width = QUIVER_ICON_VIEW_ICON_SHADOW_SIZE - 1; // one pixel blank
	int tl_offset = 1;
	//rect_w+=1;
	//rect_h+=1;
	GdkGC *gc = widget->style->bg_gc[state];


	if (NULL == iconview->priv->drop_shadow[state][0])
	{
		GdkPixmap *drop_shadow_src = gdk_pixmap_new(drawable,shadow_width*3,shadow_width*3,-1);

		gdk_draw_rectangle (drop_shadow_src,
			gc,
			TRUE,
			0, 0,
			shadow_width*3,
			shadow_width*3);			

		cairo_t *cr;
		cr = gdk_cairo_create(drop_shadow_src);

		cairo_set_source_rgba (cr, 0, 0, 0, .1);
		cairo_set_line_join(cr,CAIRO_LINE_JOIN_ROUND);
		int k = 0;
		int top=shadow_width,left=shadow_width,width=shadow_width,height=shadow_width;
		for (k = shadow_width*2;k >=1; k-=2)
		{
			cairo_set_line_width(cr,k);
			cairo_rectangle (cr, top, left, width, height);
			cairo_stroke(cr);
		}
		cairo_destroy(cr);



		//top left offset
		/* 0 - top left, 1 top, 2, top right, 3 left, 4 right, 5 bottom left, 6 bottom, 7 bottom right */

		iconview->priv->drop_shadow[state][0] = gdk_pixmap_new(drawable,shadow_width-tl_offset,shadow_width-tl_offset,-1);
		iconview->priv->drop_shadow[state][1] = gdk_pixmap_new(drawable,1,shadow_width-tl_offset,-1);
		iconview->priv->drop_shadow[state][2] = gdk_pixmap_new(drawable,shadow_width,shadow_width-tl_offset,-1);
		iconview->priv->drop_shadow[state][3] = gdk_pixmap_new(drawable,shadow_width-tl_offset,1,-1);
		iconview->priv->drop_shadow[state][4] = gdk_pixmap_new(drawable,shadow_width,1,-1);
		iconview->priv->drop_shadow[state][5] = gdk_pixmap_new(drawable,shadow_width-tl_offset,shadow_width,-1);
		iconview->priv->drop_shadow[state][6] = gdk_pixmap_new(drawable,1,shadow_width,-1);
		iconview->priv->drop_shadow[state][7] = gdk_pixmap_new(drawable,shadow_width,shadow_width,-1);

		// tl
		gdk_draw_drawable (iconview->priv->drop_shadow[state][0],gc,drop_shadow_src, left - shadow_width, top - shadow_width, 0, 0, shadow_width-tl_offset, shadow_width-tl_offset);
		// t
		gdk_draw_drawable(iconview->priv->drop_shadow[state][1],gc,drop_shadow_src, left, top - shadow_width, 0, 0, 1, shadow_width-tl_offset);
		// tr
		gdk_draw_drawable(iconview->priv->drop_shadow[state][2],gc,drop_shadow_src, left + width, top - shadow_width, 0, 0, shadow_width, shadow_width-tl_offset);
		// l
		gdk_draw_drawable(iconview->priv->drop_shadow[state][3],gc,drop_shadow_src, left-shadow_width, top, 0, 0, shadow_width-tl_offset,1);
		// r
		gdk_draw_drawable(iconview->priv->drop_shadow[state][4],gc,drop_shadow_src, left+width, top, 0, 0, shadow_width,1);
		// bl
		gdk_draw_drawable (iconview->priv->drop_shadow[state][5],gc,drop_shadow_src, left - shadow_width, top + height, 0, 0, shadow_width-tl_offset, shadow_width);
		// b
		gdk_draw_drawable(iconview->priv->drop_shadow[state][6],gc,drop_shadow_src, left, top + height, 0, 0, 1, shadow_width);
		// br
		gdk_draw_drawable(iconview->priv->drop_shadow[state][7],gc,drop_shadow_src, left + width, top + height, 0, 0, shadow_width, shadow_width);
		g_object_unref(drop_shadow_src);
	}

	// top left
	gdk_draw_drawable(drawable,gc,iconview->priv->drop_shadow[state][0],0,0,rect_x-shadow_width+tl_offset,rect_y-shadow_width+tl_offset,-1,-1);
	// top right
	gdk_draw_drawable(drawable,gc,iconview->priv->drop_shadow[state][2],0,0,rect_x+rect_w,rect_y-shadow_width+tl_offset,-1,-1);
	// bottom left
	gdk_draw_drawable(drawable,gc,iconview->priv->drop_shadow[state][5],0,0,rect_x-shadow_width+tl_offset,rect_y+rect_h,-1,-1);
	// bottom right
	gdk_draw_drawable(drawable,gc,iconview->priv->drop_shadow[state][7],0,0,rect_x+rect_w,rect_y+rect_h,-1,-1);

	int i;
	for (i = 0; i < rect_w;i++)
	{
		// top
		gdk_draw_drawable(drawable,gc,iconview->priv->drop_shadow[state][1],0,0,rect_x+i,rect_y-shadow_width+tl_offset,-1,-1);

		//bottom
		gdk_draw_drawable(drawable,gc,iconview->priv->drop_shadow[state][6],0,0,rect_x+i,rect_y+rect_h,-1,-1);
	}


	for (i = 0; i < rect_h;i++)
	{
		//left 
		gdk_draw_drawable(drawable,gc,iconview->priv->drop_shadow[state][3],0,0,rect_x-shadow_width+tl_offset,rect_y+i,-1,-1);
		//right
		gdk_draw_drawable(drawable,gc,iconview->priv->drop_shadow[state][4],0,0,rect_x+rect_w,rect_y+i,-1,-1);
	}


}


/* end utility functions*/

/* end private functions */

/* start public functions */
GtkWidget *
quiver_icon_view_new()
{
	return g_object_new(QUIVER_TYPE_ICON_VIEW,NULL);
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

void quiver_icon_view_set_smooth_scroll(QuiverIconView *iconview,gboolean smooth_scroll)
{
	iconview->priv->smooth_scroll = smooth_scroll;
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

void quiver_icon_view_activate_cell(QuiverIconView *iconview,guint cell)
{
	g_return_if_fail (QUIVER_IS_ICON_VIEW (iconview));
	
	g_signal_emit(iconview,iconview_signals[SIGNAL_CELL_ACTIVATED],0,cell);
}

gint quiver_icon_view_get_cursor_cell(QuiverIconView *iconview)
{
	g_return_val_if_fail (QUIVER_IS_ICON_VIEW (iconview), 0);
	return iconview->priv->cursor_cell;
}

void quiver_icon_view_set_cursor_cell(QuiverIconView *iconview,gint new_cursor_cell)
{
	g_return_if_fail (QUIVER_IS_ICON_VIEW (iconview));

	quiver_icon_view_set_cursor_cell_full(iconview,new_cursor_cell,(GdkModifierType)0,FALSE);
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
		guint item = (guint)selection_iter->data;
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
	guint i;

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

void quiver_icon_view_get_visible_range(QuiverIconView *iconview,guint *first, guint *last)
{
	GtkWidget *widget = GTK_WIDGET(iconview);

	guint num_cols,num_rows;
	quiver_icon_view_get_col_row_count(iconview,&num_cols,&num_rows);

	/*guint cell_width = quiver_icon_view_get_cell_width(iconview);*/
	guint cell_height = quiver_icon_view_get_cell_height(iconview);

	guint adj = (guint)gtk_adjustment_get_value(iconview->priv->vadjustment);

	guint row_first = adj / cell_height;
	guint row_last = (adj + widget->allocation.height) / cell_height;

	/* so , the current view contains icons from col 0, row_first
	 * to col num_cols -1, row_last
	 * so that is 
	 */

	guint cell_start = row_first * num_cols;
	guint cell_end = row_last * num_cols + num_cols;

	guint n_cells = quiver_icon_view_get_n_items(iconview); 

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
	GdkRectangle rect = {0};
	rect.width = widget->allocation.width;
	rect.height = widget->allocation.height;
	
	gdk_window_invalidate_rect(widget->window, &rect, FALSE);
}

void 
quiver_icon_view_invalidate_cell(QuiverIconView *iconview,
		gint cell)
{

	GtkWidget *widget = GTK_WIDGET (iconview);
	
	if (!GTK_WIDGET_REALIZED(widget))
	{
		return;
	}
	
	gint n_cells = quiver_icon_view_get_n_items(iconview);
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

	GdkRectangle cell_rect;
	cell_rect.x = current_col * cell_width - hadjust;
	cell_rect.y = current_row * cell_height - vadjust;
	cell_rect.width = cell_width;
	cell_rect.height = cell_height;

	gdk_window_invalidate_rect(widget->window,&cell_rect,FALSE);
	//redraw_needed = TRUE;
}

void
quiver_icon_view_set_n_items_func (QuiverIconView *iconview, 
         QuiverIconViewGetNItemsFunc func,gpointer data,GtkDestroyNotify destroy)
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
         QuiverIconViewGetIconPixbufFunc func,gpointer data,GtkDestroyNotify destroy)
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
         QuiverIconViewGetThumbnailPixbufFunc func,gpointer data,GtkDestroyNotify destroy)
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
         QuiverIconViewGetTextFunc func,gpointer data,GtkDestroyNotify destroy)
{
	g_return_if_fail (QUIVER_IS_ICON_VIEW (iconview));

	if (iconview->priv->callback_get_text_data_destroy)
		(*iconview->priv->callback_get_text_data_destroy)(iconview->priv->callback_get_text_data);

	iconview->priv->callback_get_text = func;
	iconview->priv->callback_get_text_data = data;
	iconview->priv->callback_get_text_data_destroy = destroy;

}

void quiver_icon_view_set_overlay_pixbuf_func (QuiverIconView *iconview,
         QuiverIconViewGetOverlayPixbufFunc func,gpointer data,GtkDestroyNotify destroy)
{
	g_return_if_fail (QUIVER_IS_ICON_VIEW (iconview));

	if (iconview->priv->callback_get_overlay_pixbuf_data_destroy)
		(*iconview->priv->callback_get_overlay_pixbuf_data_destroy)(iconview->priv->callback_get_overlay_pixbuf_data);

	iconview->priv->callback_get_overlay_pixbuf = func;
	iconview->priv->callback_get_overlay_pixbuf_data = data;
	iconview->priv->callback_get_overlay_pixbuf_data_destroy = destroy;

}
/* end public functions */
