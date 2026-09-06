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


#define QUIVER_ICON_VIEW_GET_PRIVATE(obj) (quiver_icon_view_get_instance_private (QUIVER_ICON_VIEW (obj)))

/* set up some defaults */
#define QUIVER_ICON_VIEW_ICON_WIDTH              128
#define QUIVER_ICON_VIEW_ICON_HEIGHT             128
#define QUIVER_ICON_VIEW_CELL_PADDING            8 
#define QUIVER_ICON_VIEW_ICON_SHADOW_SIZE        5
#define QUIVER_ICON_VIEW_ICON_BORDER_SIZE        1

#define SMOOTH_SCROLL_TIMEOUT                    35 // 35 ms ~= 28fps

#define QUIVER_PARAM_READWRITE G_PARAM_READWRITE|G_PARAM_STATIC_NAME|G_PARAM_STATIC_NICK|G_PARAM_STATIC_BLURB

typedef struct _CellItem CellItem;
struct _CellItem
{
	gboolean selected;
	
};
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
	gboolean drag_performed;
	GdkRectangle rubberband_rect;
	GdkRectangle rubberband_rect_old;

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

	QuiverIconViewGetThumbnailTextureFunc callback_get_thumbnail_texture;
	gpointer callback_get_thumbnail_texture_data;
	GDestroyNotify callback_get_thumbnail_texture_data_destroy;

	QuiverIconViewGetTextFunc callback_get_text;
	gpointer callback_get_text_data;
	GDestroyNotify callback_get_text_data_destroy;

	QuiverIconViewGetOverlayPixbufFunc callback_get_overlay_pixbuf;
	gpointer callback_get_overlay_pixbuf_data;
	GDestroyNotify callback_get_overlay_pixbuf_data_destroy;

	CellItem *cell_items;
	gulong n_cell_items;
};
G_DEFINE_TYPE_WITH_CODE(QuiverIconView,quiver_icon_view,GTK_TYPE_WIDGET,G_ADD_PRIVATE(QuiverIconView) G_IMPLEMENT_INTERFACE(GTK_TYPE_SCROLLABLE,NULL));

#if (GLIB_MAJOR_VERSION < 2) || (GLIB_MAJOR_VERSION == 2 && GLIB_MINOR_VERSION < 10)
#define g_object_ref_sink(o) G_STMT_START{	\
	  g_object_ref (o);				\
	  gtk_object_sink ((GtkObject*)o);		\
}G_STMT_END
#endif
/* start private data structures */

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
};

typedef struct _VelocityTimeStruct 
{
	gint hvelocity;
	gint vvelocity;
	gdouble time;
} VelocityTimeStruct;

/* end private data structures */


#define MAX_VELOCITY 12000

/* start private function prototypes */

static void      quiver_icon_view_snapshot        (GtkWidget *widget, GtkSnapshot *snapshot);
static void      quiver_icon_view_size_allocate  (GtkWidget     *widget,
                                             int width,
                                             int height,
                                             int baseline);
static void      quiver_icon_view_measure (GtkWidget *widget,
					GtkOrientation orientation,
					int for_size,
					int *minimum,
					int *natural,
					int *minimum_baseline,
					int *natural_baseline);

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
static void quiver_icon_view_update_rubber_band(QuiverIconView *iconview, gint x, gint y);
static void quiver_icon_view_update_rubber_band_selection(QuiverIconView *iconview);
static void quiver_icon_view_update_icon_size(QuiverIconView *iconview);
static gulong quiver_icon_view_get_n_items(QuiverIconView* iconview);
static GdkPixbuf* quiver_icon_view_get_thumbnail_pixbuf(QuiverIconView* iconview,gulong cell, gint* actual_width, gint *actual_height);
static GdkTexture* quiver_icon_view_get_thumbnail_texture(QuiverIconView* iconview,gulong cell, gint* actual_width, gint *actual_height);
static GdkPixbuf* quiver_icon_view_get_icon_pixbuf(QuiverIconView* iconview,gulong cell);

static void quiver_icon_view_click_cell(QuiverIconView *iconview,gulong cell);
static void quiver_icon_view_snapshot_icons (GtkWidget *widget, GtkSnapshot *snapshot);
/* end utility function prototypes*/

/* start controller callback prototypes */
static void quiver_icon_view_gesture_pressed (GtkGestureClick *gesture,
					       int n_press,
					       double x,
					       double y,
					       QuiverIconView *iconview);
static void quiver_icon_view_gesture_released (GtkGestureClick *gesture,
					       int n_press,
					       double x,
					       double y,
					       QuiverIconView *iconview);
static void quiver_icon_view_gesture_drag_begin (GtkGestureDrag *gesture,
						  double x,
						  double y,
						  QuiverIconView *iconview);
static void quiver_icon_view_gesture_drag_update (GtkGestureDrag *gesture,
						   double x,
						   double y,
						   QuiverIconView *iconview);
static void quiver_icon_view_gesture_drag_end (GtkGestureDrag *gesture,
						double x,
						double y,
						QuiverIconView *iconview);
static gboolean quiver_icon_view_scroll_controller_cb (GtkEventControllerScroll *controller,
							double dx,
							double dy,
							QuiverIconView *iconview);
static gboolean quiver_icon_view_key_controller_cb (GtkEventControllerKey *controller,
						guint keyval,
						guint keycode,
						GdkModifierType state,
						QuiverIconView *iconview);
static void quiver_icon_view_motion_controller_cb (GtkEventControllerMotion *controller,
						   double x,
						   double y,
						   QuiverIconView *iconview);
static void quiver_icon_view_leave_controller_cb (GtkEventControllerMotion *controller,
						  QuiverIconView *iconview);
static void quiver_icon_view_setup_controllers (QuiverIconView *iconview);
/* end controller callback prototypes */

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

	widget_class->snapshot = quiver_icon_view_snapshot;
	widget_class->measure = quiver_icon_view_measure;
	widget_class->size_allocate = quiver_icon_view_size_allocate;

	obj_class->set_property            = quiver_icon_view_set_property;
	obj_class->get_property            = quiver_icon_view_get_property;

	/* Override properties */
	g_object_class_override_property (obj_class, PROP_HADJUSTMENT, "hadjustment");
	g_object_class_override_property (obj_class, PROP_VADJUSTMENT, "vadjustment");
	g_object_class_override_property (obj_class, PROP_HSCROLL_POLICY, "hscroll-policy");
	g_object_class_override_property (obj_class, PROP_VSCROLL_POLICY, "vscroll-policy");



	obj_class->finalize                = quiver_icon_view_finalize;

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

	iconview->priv->callback_get_thumbnail_texture = NULL;
	iconview->priv->callback_get_thumbnail_texture_data = NULL;
	iconview->priv->callback_get_thumbnail_texture_data_destroy = NULL;

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
	iconview->priv->last_x        = -1;
	iconview->priv->last_y        = -1;
	iconview->priv->drag_mode_start = FALSE;
	iconview->priv->drag_mode_enabled = FALSE;
	iconview->priv->drag_performed = FALSE;
	
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


	gtk_widget_set_focusable(GTK_WIDGET(iconview),TRUE);
	
	iconview->priv->n_cell_items = 0;
	iconview->priv->cell_items = (CellItem*)g_malloc0( sizeof(CellItem)*(iconview->priv->n_cell_items+1) );

	quiver_icon_view_setup_controllers(iconview);
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

	g_list_free_full(iconview->priv->velocity_time_list, g_free);
	if (iconview->priv->callback_get_thumbnail_texture_data_destroy)
	{
		(*iconview->priv->callback_get_thumbnail_texture_data_destroy)(iconview->priv->callback_get_thumbnail_texture_data);
	}

	g_free (iconview->priv->cell_items);

	parent = g_type_class_peek_parent(klass);
	if (parent)
	{
		parent->finalize(object);
	}
}

static void
quiver_icon_view_snapshot (GtkWidget *widget, GtkSnapshot *snapshot)
{
	int alloc_w = gtk_widget_get_width(widget);
	int alloc_h = gtk_widget_get_height(widget);
	if (alloc_w <= 0 || alloc_h <= 0)
		return;

	graphene_rect_t clip_rect;
	graphene_rect_init(&clip_rect, 0, 0, (float)alloc_w, (float)alloc_h);
	gtk_snapshot_push_clip(snapshot, &clip_rect);

G_GNUC_BEGIN_IGNORE_DEPRECATIONS
	GtkStyleContext *context = gtk_widget_get_style_context(widget);
	gtk_snapshot_render_background(snapshot, context, 0, 0, alloc_w, alloc_h);
G_GNUC_END_IGNORE_DEPRECATIONS

	quiver_icon_view_snapshot_icons (widget, snapshot);

	gtk_snapshot_pop(snapshot);
}

static void
quiver_icon_view_size_allocate (GtkWidget     *widget,
				int width,
				int height,
				int baseline)
{
	(void)baseline;
	(void)width;
	(void)height;
	g_return_if_fail (QUIVER_IS_ICON_VIEW (widget));

	QuiverIconView *iconview = QUIVER_ICON_VIEW(widget);

	quiver_icon_view_update_icon_size(iconview);
	quiver_icon_view_scroll_to_cell_force_top(iconview,iconview->priv->cursor_cell,TRUE);
	gtk_widget_queue_draw(widget);
}

static void
quiver_icon_view_measure (GtkWidget *widget,
				GtkOrientation orientation,
				int for_size,
				int *minimum,
				int *natural,
				int *minimum_baseline,
				int *natural_baseline)
{
	(void)for_size;
	(void)minimum_baseline;
	(void)natural_baseline;
	QuiverIconView *iconview = QUIVER_ICON_VIEW(widget);

	guint cell_w = quiver_icon_view_get_cell_width(iconview);
	guint cell_h = quiver_icon_view_get_cell_height(iconview);

	if (orientation == GTK_ORIENTATION_HORIZONTAL)
	{
		guint w = cell_w;
		if (0 != iconview->priv->n_columns)
			w *= iconview->priv->n_columns;
		*minimum = *natural = w;
	}
	else
	{
		guint h = cell_h;
		if (0 != iconview->priv->n_rows)
			h *= iconview->priv->n_rows;
		*minimum = *natural = h;
	}
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
	
	c = gtk_widget_get_width(widget) / cell_width;
	
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
quiver_icon_view_snapshot_icons (GtkWidget *widget, GtkSnapshot *snapshot)
{
	QuiverIconView *iconview = QUIVER_ICON_VIEW(widget);
	
	guint cell_width = quiver_icon_view_get_cell_width(iconview);
	guint cell_height = quiver_icon_view_get_cell_height(iconview);
	if (cell_width == 0 || cell_height == 0)
		return;

	guint vadjust = iconview->priv->vadjustment ? (guint)gtk_adjustment_get_value(iconview->priv->vadjustment) : 0;
	guint hadjust = iconview->priv->hadjustment ? (guint)gtk_adjustment_get_value(iconview->priv->hadjustment) : 0;

	guint padding = iconview->priv->cell_padding;

	guint num_cols, num_rows;
	quiver_icon_view_get_col_row_count(iconview, &num_cols, &num_rows);
	if (num_cols == 0)
		return;

	guint num_adj_cols = hadjust / cell_width;
	guint adj_offset_x = hadjust - num_adj_cols * cell_width;

	guint num_adj_rows = vadjust / cell_height;
	guint adj_offset_y = vadjust - num_adj_rows * cell_height;
	guint num_adj_cells_y = num_adj_rows * num_cols;

	int alloc_w = gtk_widget_get_width(widget);
	int alloc_h = gtk_widget_get_height(widget);

	guint row_start = adj_offset_y / cell_height;
	guint col_start = adj_offset_x / cell_width;
	guint row_end = (adj_offset_y + alloc_h - 1) / cell_height;
	guint col_end = (adj_offset_x + alloc_w - 1) / cell_width;

	if (col_end >= num_cols)
	{
		col_end = (0 != num_cols) ? num_cols - 1 : 0;
	}

	guint bound_width = (cell_width > padding) ? cell_width - padding : cell_width;
	guint bound_height = (cell_height > padding) ? cell_height - padding : cell_height;

	gulong n_cells = quiver_icon_view_get_n_items(iconview);

	GdkRGBA sel_color = { 0.21f, 0.52f, 0.89f, 1.0f };
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
	GtkStyleContext* context = gtk_widget_get_style_context(widget);
	gtk_style_context_lookup_color(context, "theme_selected_bg_color", &sel_color);
G_GNUC_END_IGNORE_DEPRECATIONS

	for (guint j = row_start; j <= row_end; j++)
	{
		for (guint i = col_start; i <= col_end; i++)
		{
			gulong current_cell = j * num_cols + i + num_adj_cells_y + num_adj_cols;

			gint x_cell_offset = (gint)(i * cell_width) - (gint)adj_offset_x;
			gint y_cell_offset = (gint)(j * cell_height) - (gint)adj_offset_y;

			if (current_cell >= n_cells)
			{
				continue;
			}

			if (iconview->priv->cell_items[current_cell].selected)
			{
				graphene_rect_t sel_bounds;
				graphene_rect_init(&sel_bounds,
					(float)(x_cell_offset + (gint)padding / 2),
					(float)(y_cell_offset + (gint)padding / 2),
					(float)bound_width, (float)bound_height);

				GdkRGBA sel_bg = sel_color;
				sel_bg.alpha = gtk_widget_has_focus(widget) ? 0.40f : 0.25f;
				gtk_snapshot_append_color(snapshot, &sel_bg, &sel_bounds);

				GskRoundedRect sel_outline;
				gsk_rounded_rect_init_from_rect(&sel_outline, &sel_bounds, 0.0f);
				float b_widths[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
				GdkRGBA b_colors[4] = { sel_color, sel_color, sel_color, sel_color };
				gtk_snapshot_append_border(snapshot, &sel_outline, b_widths, b_colors);
			}

			if (gtk_widget_has_focus(widget) && current_cell == iconview->priv->cursor_cell)
			{
				graphene_rect_t focus_rect;
				graphene_rect_init(&focus_rect,
					(float)(x_cell_offset - 1 + (gint)padding / 2),
					(float)(y_cell_offset - 1 + (gint)padding / 2),
					(float)(bound_width + 2), (float)(bound_height + 2));

				GskRoundedRect focus_outline;
				gsk_rounded_rect_init_from_rect(&focus_outline, &focus_rect, 0.0f);
				float f_widths[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
				GdkRGBA f_colors[4] = { sel_color, sel_color, sel_color, sel_color };
				gtk_snapshot_append_border(snapshot, &focus_outline, f_widths, f_colors);
			}

			gboolean stock = FALSE;
			gint aw = 0, ah = 0;
			GdkTexture *texture = quiver_icon_view_get_thumbnail_texture(iconview, current_cell, &aw, &ah);
			if (NULL == texture)
			{
				GdkPixbuf *pb = quiver_icon_view_get_thumbnail_pixbuf(iconview, current_cell, &aw, &ah);
				if (NULL != pb)
				{
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
					texture = gdk_texture_new_for_pixbuf(pb);
G_GNUC_END_IGNORE_DEPRECATIONS
					g_object_unref(pb);
				}
			}

			if (NULL == texture)
			{
				GdkPixbuf *pb = quiver_icon_view_get_icon_pixbuf(iconview, current_cell);
				if (NULL != pb)
				{
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
					texture = gdk_texture_new_for_pixbuf(pb);
G_GNUC_END_IGNORE_DEPRECATIONS
					g_object_unref(pb);
				}
				stock = TRUE;
			}

			if (NULL != texture)
			{
				guint pixbuf_width = gdk_texture_get_width(texture);
				guint pixbuf_height = gdk_texture_get_height(texture);
				guint nw = (aw > 0) ? (guint)aw : pixbuf_width;
				guint nh = (ah > 0) ? (guint)ah : pixbuf_height;

				quiver_rect_get_bound_size(iconview->priv->icon_width, iconview->priv->icon_height, &nw, &nh, FALSE);

				int x_icon_offset = ((int)cell_width - (int)nw) / 2;
				int y_icon_offset = ((int)cell_height - (int)nh) / 2;

				graphene_rect_t thumb_bounds;
				graphene_rect_init(&thumb_bounds,
					(float)(x_cell_offset + x_icon_offset),
					(float)(y_cell_offset + y_icon_offset),
					(float)nw, (float)nh);

				if (!stock)
				{
					GskRoundedRect shadow_outline;
					gsk_rounded_rect_init_from_rect(&shadow_outline, &thumb_bounds, 0.0f);
					GdkRGBA shadow_color = { 0.0f, 0.0f, 0.0f, 0.40f };
					gtk_snapshot_append_outset_shadow(snapshot, &shadow_outline, &shadow_color, 1.0f, 2.0f, 0.0f, 3.0f);
				}

				gtk_snapshot_append_texture(snapshot, texture, &thumb_bounds);

				if (current_cell == iconview->priv->prelight_cell)
				{
					GdkRGBA hover_color = { 1.0f, 1.0f, 1.0f, 0.15f };
					gtk_snapshot_append_color(snapshot, &hover_color, &thumb_bounds);
				}

				if (!stock)
				{
					guint border = iconview->priv->icon_border_size;
					if (border > 0)
					{
						GskRoundedRect border_outline;
						gsk_rounded_rect_init_from_rect(&border_outline, &thumb_bounds, 0.0f);
						float b_w[4] = { (float)border, (float)border, (float)border, (float)border };
						GdkRGBA white_color = { 1.0f, 1.0f, 1.0f, 0.95f };
						GdkRGBA b_c[4] = { white_color, white_color, white_color, white_color };
						gtk_snapshot_append_border(snapshot, &border_outline, b_w, b_c);
					}
				}

				g_object_unref(texture);
			}

			/* draw overlay icons */
			for (guint k = 0; k < QUIVER_ICON_OVERLAY_COUNT; k++)
			{
				if (iconview->priv->callback_get_overlay_pixbuf)
				{
					GdkPixbuf *overlay = (*iconview->priv->callback_get_overlay_pixbuf)(iconview,
							current_cell, (QuiverIconOverlayType)k,
							iconview->priv->callback_get_overlay_pixbuf_data);
							
					if (overlay)
					{
						guint overlay_w = gdk_pixbuf_get_width(overlay);
						guint overlay_h = gdk_pixbuf_get_height(overlay);

G_GNUC_BEGIN_IGNORE_DEPRECATIONS
						GdkTexture *overlay_tex = gdk_texture_new_for_pixbuf(overlay);
G_GNUC_END_IGNORE_DEPRECATIONS
						g_object_unref(overlay);

						if (overlay_tex)
						{
							graphene_rect_t overlay_bounds;
							graphene_rect_init(&overlay_bounds,
								(float)(x_cell_offset + (gint)padding / 2 + 2 + 16 * k),
								(float)(y_cell_offset + (gint)padding / 2 + 2),
								(float)overlay_w, (float)overlay_h);
							gtk_snapshot_append_texture(snapshot, overlay_tex, &overlay_bounds);
							g_object_unref(overlay_tex);
						}
					}
				}
			}

			/* draw cell text (e.g. folder names) */
			if (iconview->priv->callback_get_text)
			{
				gchar* text = (*iconview->priv->callback_get_text)(iconview,
						current_cell,
						iconview->priv->callback_get_text_data);

				if (NULL != text && '\0' != text[0])
				{
					PangoLayout *layout = gtk_widget_create_pango_layout(widget, text);
					PangoFontDescription* font_desc = pango_font_description_from_string("sans 11");
					pango_layout_set_font_description(layout, font_desc);
					pango_font_description_free(font_desc);

					pango_layout_set_text(layout, text, -1);
					pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
					pango_layout_set_width(layout,
						((gint)cell_width - (gint)padding) * PANGO_SCALE);

					gint text_w = 0, text_h = 0;
					pango_layout_get_pixel_size(layout, &text_w, &text_h);

					gint x_text = x_cell_offset + ((gint)cell_width - text_w) / 2;
					gint y_text = y_cell_offset + (gint)cell_height - text_h - ((gint)padding / 2) - 2;

					// Text drop shadow
					GdkRGBA shadow_col = { 0.0f, 0.0f, 0.0f, 0.55f };
					graphene_point_t shadow_pt;
					graphene_point_init(&shadow_pt, (float)(x_text + 1), (float)(y_text + 1));
					gtk_snapshot_save(snapshot);
					gtk_snapshot_translate(snapshot, &shadow_pt);
					gtk_snapshot_append_layout(snapshot, layout, &shadow_col);
					gtk_snapshot_restore(snapshot);

					// Text foreground
					GdkRGBA fg;
					gtk_widget_get_color(widget, &fg);
					graphene_point_t text_pt;
					graphene_point_init(&text_pt, (float)x_text, (float)y_text);
					gtk_snapshot_save(snapshot);
					gtk_snapshot_translate(snapshot, &text_pt);
					gtk_snapshot_append_layout(snapshot, layout, &fg);
					gtk_snapshot_restore(snapshot);

					g_object_unref(layout);
				}
				g_free(text);
			}
		}
	}

	if (iconview->priv->drag_mode_enabled && 
	    QUIVER_ICON_VIEW_DRAG_BEHAVIOR_RUBBER_BAND == iconview->priv->drag_behavior)
	{
		graphene_rect_t rub_bounds;
		graphene_rect_init(&rub_bounds,
			(float)(iconview->priv->rubberband_rect.x - (gint)hadjust),
			(float)(iconview->priv->rubberband_rect.y - (gint)vadjust),
			(float)iconview->priv->rubberband_rect.width,
			(float)iconview->priv->rubberband_rect.height);

		GdkRGBA rub_bg = sel_color;
		rub_bg.alpha = 0.25f;
		gtk_snapshot_append_color(snapshot, &rub_bg, &rub_bounds);

		GskRoundedRect rub_outline;
		gsk_rounded_rect_init_from_rect(&rub_outline, &rub_bounds, 0.0f);
		float rb_widths[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		GdkRGBA rb_colors[4] = { sel_color, sel_color, sel_color, sel_color };
		gtk_snapshot_append_border(snapshot, &rub_outline, rb_widths, rb_colors);
	}
}


static gboolean
rubberband_scroll_timeout (gpointer data)
{
	QuiverIconView *iconview;
	gdouble xvalue,yvalue;
	
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
	
	quiver_icon_view_update_rubber_band (iconview, iconview->priv->last_x, iconview->priv->last_y);
	  
	return TRUE;
}


gboolean quiver_icon_view_scroll_event_cb ( GtkEventControllerScroll *controller,
           double dx,
           double dy,
           QuiverIconView *iconview)
{
	(void)dx;
	(void)controller;

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
		gdouble page_size = gtk_adjustment_get_page_size (iconview->priv->vadjustment);
		gdouble scroll_unit = pow (page_size, 2.0 / 3.0);
		hadjust += (gint)(dy * scroll_unit);

		if (hadjust < gtk_adjustment_get_lower(iconview->priv->hadjustment))
			hadjust = gtk_adjustment_get_lower(iconview->priv->hadjustment);
		else if (hadjust > gtk_adjustment_get_upper(iconview->priv->hadjustment) - gtk_adjustment_get_page_size(iconview->priv->hadjustment))
			hadjust = gtk_adjustment_get_upper(iconview->priv->hadjustment) - gtk_adjustment_get_page_size(iconview->priv->hadjustment);

	}
	else
	{
		gdouble page_size = gtk_adjustment_get_page_size (iconview->priv->vadjustment);
		gdouble scroll_unit = pow (page_size, 2.0 / 3.0);
		vadjust += (gint)(dy * scroll_unit);

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


static void
quiver_icon_view_set_hadjustment (QuiverIconView *iconview, GtkAdjustment *adj)
{
	gboolean need_adjust = FALSE;

	g_return_if_fail (QUIVER_IS_ICON_VIEW (iconview));

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
	
		if (gtk_widget_get_realized (GTK_WIDGET(iconview)))
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
	gboolean need_adjust = FALSE;

	g_return_if_fail (QUIVER_IS_ICON_VIEW (iconview));

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
	
		if (gtk_widget_get_realized (GTK_WIDGET(iconview)))
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
			if (cell_x < (gulong)hadjust)
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
			if (cell_y < (gulong)vadjust)
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

	notdone = quiver_icon_view_smooth_scroll_step(iconview);
	
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
		g_list_free_full(iconview->priv->velocity_time_list, g_free);
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
	
	return !(hdone && vdone);
}

/* start callbacks */
static void
quiver_icon_view_adjustment_value_changed (GtkAdjustment *adjustment,
           QuiverIconView *iconview)
{ (void)adjustment; 
	GtkWidget *widget = GTK_WIDGET(iconview);

	gdouble hadj,vadj;
	hadj = (int)gtk_adjustment_get_value(iconview->priv->hadjustment);
	vadj = (int)gtk_adjustment_get_value(iconview->priv->vadjustment);

	if (iconview->priv->scroll_draw)
	{
		iconview->priv->last_vadjustment = vadj;
		iconview->priv->last_hadjustment = hadj;
	}
	gtk_widget_queue_draw(widget);
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
    g_signal_emit_by_name (adj, "changed");
  if (value_changed)
    g_signal_emit_by_name (adj, "value-changed");
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
	
	if (current_col >= cols)
		return G_MAXULONG;

	remainder = (x+hadjust) - current_col * cell_width;
	if (padding/2 > remainder || padding/2 > cell_width-remainder)
		return G_MAXULONG;
	
	current_row =  (y +vadjust) / cell_height;

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
		cell_x = (cell / rows) * cell_width;
		cell_y = (cell % rows) * cell_height;
	}
	else
	{
		cell_x = (cell % cols) * cell_width;
		cell_y = (cell / cols) * cell_height;

	}
	
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
	else if (cell_x < (gulong)hadjust || force_top)
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
	else if (cell_y < (gulong)vadjust || force_top)
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
			if (!iconview->priv->cell_items[i].selected)
			{
				iconview->priv->cell_items[i].selected = TRUE;
				quiver_icon_view_invalidate_cell(iconview,i);
				selection_changed = TRUE;
			}
			
		}
		else
		{
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
quiver_icon_view_update_rubber_band(QuiverIconView *iconview, gint x, gint y)
{
	GtkWidget *widget = GTK_WIDGET (iconview);

	iconview->priv->last_x = x;
	iconview->priv->last_y = y;

	iconview->priv->rubberband_rect_old = iconview->priv->rubberband_rect;

	gint vadjust = iconview->priv->vadjustment ? (gint)gtk_adjustment_get_value(iconview->priv->vadjustment) : 0;
	gint hadjust = iconview->priv->hadjustment ? (gint)gtk_adjustment_get_value(iconview->priv->hadjustment) : 0;

	iconview->priv->rubberband_x2 = x + hadjust;
	iconview->priv->rubberband_y2 = y + vadjust;

	iconview->priv->rubberband_rect.x = MIN (iconview->priv->rubberband_x1, iconview->priv->rubberband_x2);
	iconview->priv->rubberband_rect.y = MIN (iconview->priv->rubberband_y1, iconview->priv->rubberband_y2);
	iconview->priv->rubberband_rect.width = ABS (iconview->priv->rubberband_x1 - iconview->priv->rubberband_x2) + 1;
	iconview->priv->rubberband_rect.height = ABS (iconview->priv->rubberband_y1 - iconview->priv->rubberband_y2) + 1;

	quiver_icon_view_update_rubber_band_selection(iconview);

	gtk_widget_queue_draw(widget);
}

static void
quiver_icon_view_update_rubber_band_selection(QuiverIconView *iconview)
{
	gboolean selection_changed = FALSE;

	guint cell_width = quiver_icon_view_get_cell_width(iconview);
	guint cell_height = quiver_icon_view_get_cell_height(iconview);
	if (cell_width == 0 || cell_height == 0)
		return;

	guint padding = iconview->priv->cell_padding;

	guint num_cols, num_rows;
	quiver_icon_view_get_col_row_count(iconview, &num_cols, &num_rows);
	if (num_cols == 0 || num_rows == 0)
		return;

	gulong n_cells = quiver_icon_view_get_n_items(iconview);
	if (n_cells == 0 || iconview->priv->cell_items == NULL)
		return;

	GdkRectangle *new_rect = &iconview->priv->rubberband_rect;
	GdkRectangle *old_rect = &iconview->priv->rubberband_rect_old;

	/* Calculate bounding box of old and new selection rectangles to restrict the search space */
	gint min_x = MIN(new_rect->x, old_rect->x);
	gint min_y = MIN(new_rect->y, old_rect->y);
	gint max_x = MAX(new_rect->x + new_rect->width, old_rect->x + old_rect->width);
	gint max_y = MAX(new_rect->y + new_rect->height, old_rect->y + old_rect->height);

	guint start_col = (min_x > 0) ? (guint)(min_x / cell_width) : 0;
	guint end_col = (max_x > 0) ? (guint)(max_x / cell_width) : 0;
	guint start_row = (min_y > 0) ? (guint)(min_y / cell_height) : 0;
	guint end_row = (max_y > 0) ? (guint)(max_y / cell_height) : 0;

	if (end_col >= num_cols)
		end_col = num_cols - 1;
	if (end_row >= num_rows)
		end_row = num_rows - 1;

	GdkRectangle tmp_rect;
	for (guint j = start_row; j <= end_row; j++)
	{
		for (guint i = start_col; i <= end_col; i++)
		{
			gulong current_cell = j * num_cols + i;
			if (current_cell >= n_cells)
				break;

			tmp_rect.x = i * cell_width + padding / 2;
			tmp_rect.y = j * cell_height + padding / 2;
			tmp_rect.width = cell_width - padding;
			tmp_rect.height = cell_height - padding;

			if (gdk_rectangle_intersect(new_rect, &tmp_rect, NULL))
			{
				if (!iconview->priv->cell_items[current_cell].selected)
				{
					iconview->priv->cell_items[current_cell].selected = TRUE;
					quiver_icon_view_invalidate_cell(iconview, current_cell);
					selection_changed = TRUE;
				}
			}
			else if (gdk_rectangle_intersect(old_rect, &tmp_rect, NULL))
			{
				if (iconview->priv->cell_items[current_cell].selected)
				{
					iconview->priv->cell_items[current_cell].selected = FALSE;
					quiver_icon_view_invalidate_cell(iconview, current_cell);
					selection_changed = TRUE;
				}
			}
		}
	}

	if (selection_changed)
	{
		g_signal_emit(iconview, iconview_signals[SIGNAL_SELECTION_CHANGED], 0);
	}
}


static void
quiver_icon_view_update_icon_size(QuiverIconView *iconview)
{
	GtkWidget *widget = GTK_WIDGET(iconview);

	guint width = quiver_icon_view_get_width(iconview);
	guint height = quiver_icon_view_get_height(iconview);

	guint cell_width = quiver_icon_view_get_cell_width(iconview);
	guint cell_height = quiver_icon_view_get_cell_height(iconview);

	GtkAdjustment *hadjustment, *vadjustment;

	hadjustment = iconview->priv->hadjustment;
	vadjustment = iconview->priv->vadjustment;

	gtk_adjustment_set_page_size(hadjustment, gtk_widget_get_width(widget));
	gtk_adjustment_set_page_increment(hadjustment, gtk_widget_get_width(widget));
	gtk_adjustment_set_step_increment(hadjustment,  cell_width);
	gtk_adjustment_set_lower(hadjustment, 0);
	gtk_adjustment_set_upper(hadjustment, MAX(gtk_widget_get_width(widget), width));

	if (gtk_adjustment_get_value(hadjustment) > gtk_adjustment_get_upper(hadjustment) - gtk_adjustment_get_page_size(hadjustment))
		gtk_adjustment_set_value (hadjustment, MAX(0, gtk_adjustment_get_upper(hadjustment) - gtk_adjustment_get_page_size(hadjustment)));

	gtk_adjustment_set_page_size(vadjustment, gtk_widget_get_height(widget));
	gtk_adjustment_set_page_increment(vadjustment, gtk_widget_get_height(widget));
	gtk_adjustment_set_step_increment(vadjustment, cell_height);
	gtk_adjustment_set_lower(vadjustment, 0);
	gtk_adjustment_set_upper(vadjustment, MAX(gtk_widget_get_height(widget), height));

	g_signal_emit_by_name (hadjustment, "changed");
	g_signal_emit_by_name (vadjustment, "changed");


	iconview->priv->scroll_draw = FALSE;

	quiver_icon_view_scroll_to_cell_force_top(iconview,iconview->priv->cursor_cell,TRUE);

	gtk_widget_queue_draw(widget);

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


static GdkTexture* quiver_icon_view_get_thumbnail_texture(QuiverIconView* iconview, gulong cell, gint* actual_width, gint *actual_height)
{
	GdkTexture *texture = NULL;
	if (iconview->priv->callback_get_thumbnail_texture)
		texture = (*iconview->priv->callback_get_thumbnail_texture)(iconview, cell, actual_width, actual_height, iconview->priv->callback_get_thumbnail_texture_data);
	return texture;
}


/* end utility functions*/

/* start controller callbacks */

static void
quiver_icon_view_gesture_pressed (GtkGestureClick *gesture,
				   int n_press,
				   double x,
				   double y,
				   QuiverIconView *iconview)
{
	(void)gesture;
	GtkWidget *widget = GTK_WIDGET(iconview);

	gint ix = (gint)x;
	gint iy = (gint)y;

	iconview->priv->start_x = ix;
	iconview->priv->start_y = iy;

	iconview->priv->last_x = ix;
	iconview->priv->last_y = iy;
	
	iconview->priv->drag_performed = FALSE;
	
	gettimeofday(&iconview->priv->last_motion_time,NULL);
	
	g_list_free_full(iconview->priv->velocity_time_list, g_free);
	iconview->priv->velocity_time_list = NULL;

	gint vadjust = (gint)gtk_adjustment_get_value(iconview->priv->vadjustment);
	gint hadjust = (gint)gtk_adjustment_get_value(iconview->priv->hadjustment);

	if (!gtk_widget_has_focus (widget))
	{
		gtk_widget_grab_focus (widget);
	}

	if (1 == n_press)
	{
		/* First press */
		/* Get modifier state from current event */
		GdkModifierType state = 0;
		GdkEvent *ev = gtk_event_controller_get_current_event(GTK_EVENT_CONTROLLER(gesture));
		if (ev != NULL)
			state = gdk_event_get_modifier_state(ev);

		iconview->priv->mouse_button_is_down = TRUE;
	
		iconview->priv->rubberband_x1 = iconview->priv->rubberband_x2 = ix + hadjust;
		iconview->priv->rubberband_y1 = iconview->priv->rubberband_y2 = iy + vadjust;
	
		gulong cell = quiver_icon_view_get_cell_for_xy (iconview,ix,iy);

		if (QUIVER_ICON_VIEW_DRAG_BEHAVIOR_SCROLL == iconview->priv->drag_behavior)
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
			if (0 == (state & GDK_CONTROL_MASK))
			{
				quiver_icon_view_set_select_all(iconview,FALSE);
			}
			iconview->priv->drag_mode_start = TRUE;
		}
	}
	else if (2 == n_press)
	{
		/* Double click */
		gulong cell = quiver_icon_view_get_cell_for_xy (iconview,ix,iy);
		if (cell != G_MAXULONG)
		{
			iconview->priv->mouse_button_is_down = FALSE;
			quiver_icon_view_activate_cell(iconview,cell);
		}
	}
}

static void
quiver_icon_view_gesture_released (GtkGestureClick *gesture,
				   int n_press,
				   double x,
				   double y,
				   QuiverIconView *iconview)
{
	(void)gesture;
	(void)n_press;

	gint ix = (gint)x;
	gint iy = (gint)y;

	/* Get modifier state from current event */
	GdkModifierType state = 0;
	GdkEvent *ev = gtk_event_controller_get_current_event(GTK_EVENT_CONTROLLER(gesture));
	if (ev != NULL)
		state = gdk_event_get_modifier_state(ev);

	gulong cell = quiver_icon_view_get_cell_for_xy (iconview,ix,iy);

	gboolean was_drag = iconview->priv->drag_mode_enabled || iconview->priv->drag_performed;
	iconview->priv->drag_performed = FALSE;

	// drag mode enabled means mouse has
	// moved while button is down
	if (was_drag)
	{
		if (QUIVER_ICON_VIEW_DRAG_BEHAVIOR_RUBBER_BAND == iconview->priv->drag_behavior)
		{
			if (iconview->priv->timeout_id_rubberband_scroll != 0)
			{
				g_source_remove (iconview->priv->timeout_id_rubberband_scroll);
				iconview->priv->timeout_id_rubberband_scroll = 0;
			}
			/* Preserve rubber-band multi-selection. If releasing over a selected cell,
			 * update cursor_cell without clearing the other selected items. */
			if (cell != G_MAXULONG && iconview->priv->cell_items != NULL &&
			    cell < iconview->priv->n_cell_items && iconview->priv->cell_items[cell].selected)
			{
				gulong old_cursor = iconview->priv->cursor_cell;
				iconview->priv->cursor_cell = cell;
				if (old_cursor != G_MAXULONG)
					quiver_icon_view_invalidate_cell(iconview, old_cursor);
				quiver_icon_view_invalidate_cell(iconview, cell);
				g_signal_emit(iconview, iconview_signals[SIGNAL_CURSOR_CHANGED], 0, cell);
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
		quiver_icon_view_set_cursor_cell_full(iconview,cell,state,TRUE);
	}
	if (iconview->priv->timeout_id_rubberband_scroll != 0)
	{
		g_source_remove (iconview->priv->timeout_id_rubberband_scroll);
		iconview->priv->timeout_id_rubberband_scroll = 0;
	}
	iconview->priv->mouse_button_is_down = FALSE;
	iconview->priv->drag_mode_start = FALSE;
	iconview->priv->drag_mode_enabled = FALSE;
	iconview->priv->rubberband_rect.width = 0;
	iconview->priv->rubberband_rect.height = 0;
	gtk_widget_queue_draw(GTK_WIDGET(iconview));
}

static void
quiver_icon_view_gesture_drag_begin (GtkGestureDrag *gesture,
				      double x,
				      double y,
				      QuiverIconView *iconview)
{
	(void)gesture;
	gint ix = (gint)x;
	gint iy = (gint)y;
	iconview->priv->start_x = ix;
	iconview->priv->start_y = iy;
	iconview->priv->last_x = ix;
	iconview->priv->last_y = iy;
	iconview->priv->drag_performed = FALSE;

	gint vadjust = iconview->priv->vadjustment ? (gint)gtk_adjustment_get_value(iconview->priv->vadjustment) : 0;
	gint hadjust = iconview->priv->hadjustment ? (gint)gtk_adjustment_get_value(iconview->priv->hadjustment) : 0;

	iconview->priv->rubberband_x1 = ix + hadjust;
	iconview->priv->rubberband_y1 = iy + vadjust;
	iconview->priv->rubberband_x2 = ix + hadjust;
	iconview->priv->rubberband_y2 = iy + vadjust;

	iconview->priv->rubberband_rect.x = iconview->priv->rubberband_x1;
	iconview->priv->rubberband_rect.y = iconview->priv->rubberband_y1;
	iconview->priv->rubberband_rect.width = 1;
	iconview->priv->rubberband_rect.height = 1;
	iconview->priv->rubberband_rect_old = iconview->priv->rubberband_rect;
}

static void
quiver_icon_view_gesture_drag_update (GtkGestureDrag *gesture,
				       double x,
				       double y,
				       QuiverIconView *iconview)
{
	(void)gesture;

	gint ix = (gint)x;
	gint iy = (gint)y;

	/* Calculate absolute position from drag start */
	double start_x, start_y;
	gtk_gesture_drag_get_start_point(gesture, &start_x, &start_y);
	gint abs_x = (gint)start_x + ix;
	gint abs_y = (gint)start_y + iy;

	if (iconview->priv->drag_mode_start && 
		(10 < abs(abs_x - iconview->priv->start_x) || 10 < abs(abs_y - iconview->priv->start_y)))
	{
		iconview->priv->drag_mode_start = FALSE;
		iconview->priv->drag_mode_enabled = TRUE;
		iconview->priv->drag_performed = TRUE;
	}

	gulong new_cell = quiver_icon_view_get_cell_for_xy (iconview,abs_x,abs_y);

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
		}
	}

	if (iconview->priv->drag_mode_enabled && 
		QUIVER_ICON_VIEW_DRAG_BEHAVIOR_RUBBER_BAND == iconview->priv->drag_behavior)
	{
		quiver_icon_view_update_rubber_band(iconview, abs_x, abs_y);
		iconview->priv->rubberband_scroll_x = 0;
		iconview->priv->rubberband_scroll_y = 0;

		GtkWidget *widget = GTK_WIDGET(iconview);
		
		/* check if the window needs to scroll */
		if (abs_x < 0 || abs_x > gtk_widget_get_width(widget))
		{
			if (abs_x < 0)
				iconview->priv->rubberband_scroll_x = abs_x;
			else
				iconview->priv->rubberband_scroll_x = abs_x - gtk_widget_get_width(widget);
	
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
		if (abs_y < 0 || abs_y > gtk_widget_get_height(widget))
		{
			if (abs_y < 0)
				iconview->priv->rubberband_scroll_y = abs_y;
			else
				iconview->priv->rubberband_scroll_y = abs_y - gtk_widget_get_height(widget);
	
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
		
		VelocityTimeStruct* vt = g_malloc(sizeof(VelocityTimeStruct));
		
		vt->time = new_time - old_time;
		vt->hvelocity = (gint)((abs_x - iconview->priv->last_x) / vt->time);
		vt->hvelocity = MIN((gint)MAX_VELOCITY,vt->hvelocity);
		vt->hvelocity = MAX(-(gint)MAX_VELOCITY,vt->hvelocity);
		
		vt->vvelocity =  (gint)((abs_y - iconview->priv->last_y) / vt->time);
		vt->vvelocity = MIN((gint)MAX_VELOCITY,vt->vvelocity);
		vt->vvelocity = MAX(-(gint)MAX_VELOCITY,vt->vvelocity);		

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
		
		iconview->priv->rubberband_x2 = abs_x + hadjust;
		iconview->priv->rubberband_y2 = abs_y + vadjust;
	
		hadjust += iconview->priv->rubberband_x1 - iconview->priv->rubberband_x2;
		hadjust = MAX(0,MIN(gtk_adjustment_get_upper(iconview->priv->hadjustment) - gtk_adjustment_get_page_size(iconview->priv->hadjustment),hadjust));
		vadjust += iconview->priv->rubberband_y1 - iconview->priv->rubberband_y2;
		vadjust = MAX(0,MIN(gtk_adjustment_get_upper(iconview->priv->vadjustment) - gtk_adjustment_get_page_size(iconview->priv->vadjustment),vadjust));
		gtk_adjustment_set_value(iconview->priv->hadjustment,hadjust);
		gtk_adjustment_set_value(iconview->priv->vadjustment,vadjust);

		iconview->priv->rubberband_x1 = abs_x + hadjust;
		iconview->priv->rubberband_y1 = abs_y + vadjust;
	}
	
	iconview->priv->last_x = abs_x;
	iconview->priv->last_y = abs_y;
}

static void
quiver_icon_view_gesture_drag_end (GtkGestureDrag *gesture,
				    double x,
				    double y,
				    QuiverIconView *iconview)
{
	(void)gesture;

	if (iconview->priv->timeout_id_rubberband_scroll != 0)
	{
		g_source_remove (iconview->priv->timeout_id_rubberband_scroll);
		iconview->priv->timeout_id_rubberband_scroll = 0;
	}

	if (iconview->priv->drag_mode_enabled)
	{
		iconview->priv->drag_performed = TRUE;

		double start_x, start_y;
		gtk_gesture_drag_get_start_point(gesture, &start_x, &start_y);
		gint abs_x = (gint)start_x + (gint)x;
		gint abs_y = (gint)start_y + (gint)y;

		if (QUIVER_ICON_VIEW_DRAG_BEHAVIOR_RUBBER_BAND == iconview->priv->drag_behavior)
		{
			quiver_icon_view_update_rubber_band(iconview, abs_x, abs_y);
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

	/* Reset drag state */
	iconview->priv->drag_mode_start = FALSE;
	iconview->priv->drag_mode_enabled = FALSE;
	iconview->priv->rubberband_rect.width = 0;
	iconview->priv->rubberband_rect.height = 0;
	gtk_widget_queue_draw(GTK_WIDGET(iconview));
}

static gboolean
quiver_icon_view_scroll_controller_cb (GtkEventControllerScroll *controller,
					double dx,
					double dy,
					QuiverIconView *iconview)
{
	(void)dx;
	(void)controller;

	return quiver_icon_view_scroll_event_cb(NULL, dx, dy, iconview);
}

static gboolean
quiver_icon_view_key_controller_cb (GtkEventControllerKey *controller,
				    guint keyval,
				    guint keycode,
				    GdkModifierType state,
				    QuiverIconView *iconview)
{
	(void)controller;
	(void)keycode;

	GtkWidget *widget = GTK_WIDGET(iconview);
	gulong n_cells  = quiver_icon_view_get_n_items(iconview);

	guint cols,rows;
	quiver_icon_view_get_col_row_count(iconview,&cols,&rows);
	gboolean rval = TRUE;

	guint cell_height = quiver_icon_view_get_cell_height(iconview);
	guint rows_per_page = gtk_widget_get_height(widget)/cell_height;
	if (rows_per_page == 0)
	{
		rows_per_page = 1;
	}
	guint n_cells_per_page = cols * rows_per_page;

	gulong new_cursor_cell = iconview->priv->cursor_cell;
	
	switch(keyval)
	{
		case GDK_KEY_Return:
		case GDK_KEY_KP_Enter:
			quiver_icon_view_activate_cell(iconview,iconview->priv->cursor_cell);
			new_cursor_cell = iconview->priv->cursor_cell;
			break;
		case GDK_KEY_a:
		case GDK_KEY_A:
			if (state & GDK_CONTROL_MASK)
			{
				quiver_icon_view_set_select_all(iconview,TRUE);
			}
			break;
		case GDK_KEY_space:
			if (state & GDK_CONTROL_MASK)
			{
				if (G_MAXULONG != iconview->priv->cursor_cell &&
					iconview->priv->cursor_cell < n_cells)
				{
					iconview->priv->cell_items[iconview->priv->cursor_cell].selected = !iconview->priv->cell_items[iconview->priv->cursor_cell].selected;
					g_signal_emit(iconview,iconview_signals[SIGNAL_SELECTION_CHANGED],0);
					quiver_icon_view_invalidate_cell(iconview,iconview->priv->cursor_cell);
				}
			}
			break;

		case GDK_KEY_KP_Left:
		case GDK_KEY_Left:
			new_cursor_cell = iconview->priv->cursor_cell-1;
			break;
		case GDK_KEY_KP_Right:
		case GDK_KEY_Right:
			new_cursor_cell = iconview->priv->cursor_cell+1;
			break;
		case GDK_KEY_KP_Up:
		case GDK_KEY_Up:
			new_cursor_cell = iconview->priv->cursor_cell - cols;
			break;
		case GDK_KEY_KP_Down:
		case GDK_KEY_Down:
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

	if (new_cursor_cell < n_cells)
	{
		if (new_cursor_cell != iconview->priv->cursor_cell)
		{
			quiver_icon_view_set_cursor_cell_full(iconview,new_cursor_cell,state,FALSE);
		}
	}
	else
	{
		if (G_MAXULONG != iconview->priv->cursor_cell &&
			iconview->priv->cursor_cell < n_cells)
		{
			quiver_icon_view_set_cursor_cell_full(iconview,iconview->priv->cursor_cell,state,FALSE);
		}
	}
	
	return rval;
}

static void
quiver_icon_view_motion_controller_cb (GtkEventControllerMotion *controller,
				       double x,
				       double y,
				       QuiverIconView *iconview)
{
	(void)controller;

	gint ix = (gint)x;
	gint iy = (gint)y;

	iconview->priv->last_x = ix;
	iconview->priv->last_y = iy;

	gulong new_cell = quiver_icon_view_get_cell_for_xy (iconview,ix,iy);

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
		}
	}
	else if (iconview->priv->prelight_cell != G_MAXULONG)
	{
		quiver_icon_view_invalidate_cell(iconview,iconview->priv->prelight_cell);
	}
}

static void
quiver_icon_view_leave_controller_cb (GtkEventControllerMotion *controller,
				      QuiverIconView *iconview)
{
	(void)controller;

	if (G_MAXULONG != iconview->priv->prelight_cell)
	{
		quiver_icon_view_invalidate_cell(iconview,iconview->priv->prelight_cell);
		iconview->priv->prelight_cell = G_MAXULONG;
	}

	iconview->priv->last_x = -1;
	iconview->priv->last_y = -1;
}

static void
quiver_icon_view_setup_controllers (QuiverIconView *iconview)
{
	GtkWidget *widget = GTK_WIDGET(iconview);

	GtkGesture *click = gtk_gesture_click_new();
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_PRIMARY);
	g_signal_connect(click, "pressed",
		G_CALLBACK(quiver_icon_view_gesture_pressed), iconview);
	g_signal_connect(click, "released",
		G_CALLBACK(quiver_icon_view_gesture_released), iconview);
	gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(click));

	GtkGesture *drag = gtk_gesture_drag_new();
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(drag), GDK_BUTTON_PRIMARY);
	g_signal_connect(drag, "drag-begin",
		G_CALLBACK(quiver_icon_view_gesture_drag_begin), iconview);
	g_signal_connect(drag, "drag-update",
		G_CALLBACK(quiver_icon_view_gesture_drag_update), iconview);
	g_signal_connect(drag, "drag-end",
		G_CALLBACK(quiver_icon_view_gesture_drag_end), iconview);
	gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(drag));

	GtkEventController *scroll = gtk_event_controller_scroll_new(
		GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
	gtk_event_controller_set_propagation_phase(scroll, GTK_PHASE_TARGET);
	g_signal_connect(scroll, "scroll",
		G_CALLBACK(quiver_icon_view_scroll_controller_cb), iconview);
	gtk_widget_add_controller(widget, scroll);

	GtkEventController *key = gtk_event_controller_key_new();
	g_signal_connect(key, "key-pressed",
		G_CALLBACK(quiver_icon_view_key_controller_cb), iconview);
	gtk_widget_add_controller(widget, key);

	GtkEventController *motion = gtk_event_controller_motion_new();
	g_signal_connect(motion, "motion",
		G_CALLBACK(quiver_icon_view_motion_controller_cb), iconview);
	g_signal_connect(motion, "leave",
		G_CALLBACK(quiver_icon_view_leave_controller_cb), iconview);
	gtk_widget_add_controller(widget, motion);
}

/* end controller callbacks */

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
	g_return_if_fail (QUIVER_IS_ICON_VIEW (iconview));
	g_return_if_fail (x != NULL && y != NULL);

	*x = -1;
	*y = -1;

	gint wx = iconview->priv->last_x;
	gint wy = iconview->priv->last_y;

	/* If available, try to compute current pointer position from the native window surface */
	GtkWidget *widget = GTK_WIDGET(iconview);
	GtkNative *native = gtk_widget_get_native(widget);
	if (native != NULL)
	{
		GdkSurface *surface = gtk_native_get_surface(native);
		if (surface != NULL)
		{
			GdkDisplay *display = gdk_surface_get_display(surface);
			GdkSeat *seat = gdk_display_get_default_seat(display);
			if (seat != NULL)
			{
				GdkDevice *device = gdk_seat_get_pointer(seat);
				if (device != NULL)
				{
					double px, py;
					GdkModifierType state;
					if (gdk_surface_get_device_position(surface, device, &px, &py, &state))
					{
						graphene_point_t p = GRAPHENE_POINT_INIT((float)px, (float)py);
						graphene_point_t out_p;
						if (gtk_widget_compute_point(GTK_WIDGET(native), widget, &p, &out_p))
						{
							wx = (gint)out_p.x;
							wy = (gint)out_p.y;
							iconview->priv->last_x = wx;
							iconview->priv->last_y = wy;
						}
					}
				}
			}
		}
	}

	if (wx < 0 || wy < 0)
	{
		return;
	}

	/* Only return valid coordinates if the pointer is actually inside this cell */
	if (quiver_icon_view_get_cell_for_xy(iconview, wx, wy) != (gulong)cell)
	{
		return;
	}

	guint cell_width = quiver_icon_view_get_cell_width(iconview);
	guint cell_height = quiver_icon_view_get_cell_height(iconview);
	if (cell_width == 0 || cell_height == 0)
	{
		return;
	}

	guint hadjust = iconview->priv->hadjustment ? (guint)gtk_adjustment_get_value(iconview->priv->hadjustment) : 0;
	guint vadjust = iconview->priv->vadjustment ? (guint)gtk_adjustment_get_value(iconview->priv->vadjustment) : 0;

	guint cols, rows;
	quiver_icon_view_get_col_row_count(iconview, &cols, &rows);
	if (cols == 0)
	{
		return;
	}

	gint cell_col = cell % cols;
	gint cell_row = cell / cols; 
	gint cell_x = (gint)(cell_width * cell_col) - (gint)hadjust;
	gint cell_y = (gint)(cell_height * cell_row) - (gint)vadjust;

	gint x_icon_offset = ((gint)cell_width - (gint)iconview->priv->icon_width) / 2;
	gint y_icon_offset = ((gint)cell_height - (gint)iconview->priv->icon_height) / 2;

	gint cx = wx - cell_x - x_icon_offset;
	gint cy = wy - cell_y - y_icon_offset;
	if (cx < 0)
		cx = 0;
	if (cx >= (gint)iconview->priv->icon_width)
		cx = (gint)iconview->priv->icon_width - 1;
	if (cy < 0)
		cy = 0;
	if (cy >= (gint)iconview->priv->icon_height)
		cy = (gint)iconview->priv->icon_height - 1;

	*x = cx;
	*y = cy;
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

	gulong cell_start, cell_end;
	gulong n_cells = quiver_icon_view_get_n_items(iconview);

	if (num_rows <= 1 && num_cols > 1)
	{
		guint cell_width = quiver_icon_view_get_cell_width(iconview);

		if (0 == cell_width)
		{
			*first = 0;
			*last = 0;
			return;
		}

		guint hadj = (guint)gtk_adjustment_get_value(iconview->priv->hadjustment);
		guint col_first = hadj / cell_width;
		guint col_last = (hadj + gtk_widget_get_width(widget)) / cell_width;

		cell_start = col_first * num_rows;
		cell_end = (col_last + 1) * num_rows;
	}
	else
	{
		guint cell_height = quiver_icon_view_get_cell_height(iconview);

		if (0 == cell_height)
		{
			*first = 0;
			*last = 0;
			return;
		}

		guint vadj = (guint)gtk_adjustment_get_value(iconview->priv->vadjustment);
		guint row_first = vadj / cell_height;
		guint row_last = (vadj + gtk_widget_get_height(widget)) / cell_height;

		cell_start = row_first * num_cols;
		cell_end = row_last * num_cols + num_cols;
	}

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
	
	gtk_widget_queue_draw(widget);
}

void 
quiver_icon_view_invalidate_cell(QuiverIconView *iconview,
		gulong cell)
{

	GtkWidget *widget = GTK_WIDGET (iconview);
	
	gulong n_cells = quiver_icon_view_get_n_items(iconview);
	if (cell >= n_cells)
		return;
	
	(void)widget;
	gtk_widget_queue_draw(GTK_WIDGET(iconview));
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
quiver_icon_view_set_thumbnail_texture_func (QuiverIconView *iconview,
         QuiverIconViewGetThumbnailTextureFunc func,gpointer data,GDestroyNotify destroy)
{
	g_return_if_fail (QUIVER_IS_ICON_VIEW (iconview));

	if (iconview->priv->callback_get_thumbnail_texture_data_destroy)
		(*iconview->priv->callback_get_thumbnail_texture_data_destroy)(iconview->priv->callback_get_thumbnail_texture_data);

	iconview->priv->callback_get_thumbnail_texture = func;
	iconview->priv->callback_get_thumbnail_texture_data = data;
	iconview->priv->callback_get_thumbnail_texture_data_destroy = destroy;

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
