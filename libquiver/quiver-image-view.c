#include <config.h>
#include <gtk/gtk.h>
#include <math.h>
#include <string.h>
#include <sys/time.h>
#include "quiver-pixbuf-utils.h"
#include "quiver-image-view.h"
#include "quiver-marshallers.h"

#define QUIVER_IMAGE_VIEW_GET_PRIVATE(obj) (quiver_image_view_get_instance_private (QUIVER_IMAGE_VIEW (obj)))

#define QUIVER_IMAGE_VIEW_MAG_MAX              50.
#define QUIVER_IMAGE_VIEW_MIN_IMAGE_SIZE       32
#define QUIVER_IMAGE_VIEW_SCALE_HQ_TIMEOUT     200

#define QUIVER_PARAM_READWRITE G_PARAM_READWRITE|G_PARAM_STATIC_STRINGS

enum {
	SIGNAL_ACTIVATED,
	SIGNAL_RELOAD,
	SIGNAL_MAGNIFICATION_CHANGED,
	SIGNAL_VIEW_MODE_CHANGED,
	SIGNAL_COUNT
};

static guint imageview_signals[SIGNAL_COUNT] = {0};

enum {
   PROP_0,
   PROP_HADJUSTMENT,
   PROP_VADJUSTMENT,
   PROP_HSCROLL_POLICY,
   PROP_VSCROLL_POLICY,
};

struct _QuiverImageViewPrivate
{
	GdkPixbuf *pixbuf;
	GdkPixbuf *pixbuf_scaled;
	GdkPixbufAnimation *pixbuf_animation;
	GdkPixbufAnimationIter *pixbuf_animation_iter;
	
	gint pixbuf_width;
	gint pixbuf_height;

	gint pixbuf_width_next;
	gint pixbuf_height_next;

	QuiverImageViewMode view_mode;
	QuiverImageViewMode view_mode_last;

	gboolean transitions_enabled;
	gint transition_n_frames;
	GdkPixbuf *transition_pixbuf_old;
	GdkPixbuf *transition_pixbuf_new;
	GList *transition_pixbufs_intermediate;
	guint transition_timeout_id;
	guint idle_transition_create_id;
	
	QuiverImageViewMagnificationMode magnification_mode;
	guint magnification_timeout_id;
	gdouble magnification_final;
	gdouble magnification;

	guint timeout_scale_hq_id;
	
	GtkAdjustment *hadjustment;
	GtkAdjustment *vadjustment;
	GtkScrollablePolicy hscroll_policy;
	GtkScrollablePolicy vscroll_policy;

	guint scroll_timeout_id;
	gdouble last_hadjustment;
	gdouble last_vadjustment;

	gboolean area_updated;
	guint animation_timeout_id;

	QuiverImageViewMouseMode mouse_move_mode;
	gint mouse_x1, mouse_y1;
	gint mouse_x2, mouse_y2;
	gboolean mouse_move_capture;

	gboolean scroll_draw;

	gboolean rubberband_mode_start;
	gboolean rubberband_mode;
	cairo_rectangle_int_t rubberband_rect;
	cairo_rectangle_int_t rubberband_rect_old;

	gboolean smooth_scroll;
	
	guint timeout_id_smooth_scroll_slowdown;
	struct timeval last_motion_time;
	GList* velocity_time_list;
	
	gboolean reload_event_sent;
};

G_DEFINE_TYPE_WITH_CODE(QuiverImageView, quiver_image_view, GTK_TYPE_WIDGET,
                        G_ADD_PRIVATE(QuiverImageView)
                        G_IMPLEMENT_INTERFACE(GTK_TYPE_SCROLLABLE, NULL));

static void quiver_image_view_snapshot (GtkWidget *widget, GtkSnapshot *snapshot);
static void quiver_image_view_measure (GtkWidget *widget, GtkOrientation orientation, int for_size, int *minimum, int *natural, int *minimum_baseline, int *natural_baseline);
static void quiver_image_view_size_allocate (GtkWidget *widget, int width, int height, int baseline);
static void quiver_image_view_finalize(GObject *object);
static void quiver_image_view_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void quiver_image_view_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static void quiver_image_view_adjustment_value_changed (GtkAdjustment *adjustment, QuiverImageView *imageview);
static void quiver_image_view_update_size(QuiverImageView *imageview);
static void quiver_image_view_get_pixbuf_display_size(QuiverImageView *imageview, gint *width, gint *height);
static void quiver_image_view_create_scaled_pixbuf(QuiverImageView *imageview, GdkInterpType interptype);
static void quiver_image_view_prepare_for_new_pixbuf(QuiverImageView *imageview, gint new_width, gint new_height);
static void quiver_image_view_send_reload_event(QuiverImageView *imageview);

void quiver_image_view_set_hadjustment (QuiverImageView *imageview, GtkAdjustment *hadj);
void quiver_image_view_set_vadjustment (QuiverImageView *imageview, GtkAdjustment *vadj);

static void 
quiver_image_view_class_init (QuiverImageViewClass *klass)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
	GObjectClass *obj_class = G_OBJECT_CLASS (klass);

	widget_class->snapshot = quiver_image_view_snapshot;
	widget_class->measure = quiver_image_view_measure;
	widget_class->size_allocate = quiver_image_view_size_allocate;

	obj_class->finalize = quiver_image_view_finalize;
	obj_class->set_property = quiver_image_view_set_property;
	obj_class->get_property = quiver_image_view_get_property;

	g_object_class_install_property (obj_class, PROP_HADJUSTMENT,
		g_param_spec_object ("hadjustment", "Horizontal adjustment", "Horizontal adjustment", GTK_TYPE_ADJUSTMENT, QUIVER_PARAM_READWRITE));
	g_object_class_install_property (obj_class, PROP_VADJUSTMENT,
		g_param_spec_object ("vadjustment", "Vertical adjustment", "Vertical adjustment", GTK_TYPE_ADJUSTMENT, QUIVER_PARAM_READWRITE));
	g_object_class_install_property (obj_class, PROP_HSCROLL_POLICY,
		g_param_spec_enum ("hscroll-policy", "Horizontal Scroll Policy", "Horizontal Scroll Policy", GTK_TYPE_SCROLLABLE_POLICY, GTK_SCROLL_MINIMUM, QUIVER_PARAM_READWRITE));
	g_object_class_install_property (obj_class, PROP_VSCROLL_POLICY,
		g_param_spec_enum ("vscroll-policy", "Vertical Scroll Policy", "Vertical Scroll Policy", GTK_TYPE_SCROLLABLE_POLICY, GTK_SCROLL_MINIMUM, QUIVER_PARAM_READWRITE));

	imageview_signals[SIGNAL_ACTIVATED] = g_signal_new ("activated", G_TYPE_FROM_CLASS (obj_class), G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (QuiverImageViewClass, activated), NULL, NULL, NULL, G_TYPE_NONE, 0);
	imageview_signals[SIGNAL_RELOAD] = g_signal_new ("reload", G_TYPE_FROM_CLASS (obj_class), G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (QuiverImageViewClass, reload), NULL, NULL, NULL, G_TYPE_NONE, 0);
	imageview_signals[SIGNAL_MAGNIFICATION_CHANGED] = g_signal_new ("magnification-changed", G_TYPE_FROM_CLASS (obj_class), G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (QuiverImageViewClass, magnification_changed), NULL, NULL, NULL, G_TYPE_NONE, 0);
	imageview_signals[SIGNAL_VIEW_MODE_CHANGED] = g_signal_new ("view-mode-changed", G_TYPE_FROM_CLASS (obj_class), G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (QuiverImageViewClass, view_mode_changed), NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void 
quiver_image_view_init(QuiverImageView *imageview)
{
	imageview->priv = quiver_image_view_get_instance_private(imageview);
	imageview->priv->view_mode = QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW;
	imageview->priv->view_mode_last = QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW;
	imageview->priv->magnification = 1;
	imageview->priv->magnification_final = 1;
	imageview->priv->scroll_draw = TRUE;
	gtk_widget_set_focusable(GTK_WIDGET(imageview), TRUE);
}

static void
quiver_image_view_finalize(GObject *object)
{
	QuiverImageView *imageview = QUIVER_IMAGE_VIEW(object);
	quiver_image_view_prepare_for_new_pixbuf(imageview, 0, 0);
	G_OBJECT_CLASS (quiver_image_view_parent_class)->finalize (object);
}

static void
quiver_image_view_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	QuiverImageView *imageview = QUIVER_IMAGE_VIEW (object);
	switch (prop_id) {
		case PROP_HADJUSTMENT: quiver_image_view_set_hadjustment(imageview, (GtkAdjustment*)g_value_get_object (value)); break;
		case PROP_VADJUSTMENT: quiver_image_view_set_vadjustment(imageview, (GtkAdjustment*)g_value_get_object (value)); break;
		case PROP_HSCROLL_POLICY: imageview->priv->hscroll_policy = (GtkScrollablePolicy)g_value_get_enum(value); gtk_widget_queue_resize(GTK_WIDGET(imageview)); break;
		case PROP_VSCROLL_POLICY: imageview->priv->vscroll_policy = (GtkScrollablePolicy)g_value_get_enum(value); gtk_widget_queue_resize(GTK_WIDGET(imageview)); break;
		default: G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec); break;
	}
}

static void
quiver_image_view_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	QuiverImageView *imageview = QUIVER_IMAGE_VIEW (object);
	switch (prop_id) {
		case PROP_HADJUSTMENT: g_value_set_object(value, imageview->priv->hadjustment); break;
		case PROP_VADJUSTMENT: g_value_set_object(value, imageview->priv->vadjustment); break;
		case PROP_HSCROLL_POLICY: g_value_set_enum(value, imageview->priv->hscroll_policy); break;
		case PROP_VSCROLL_POLICY: g_value_set_enum(value, imageview->priv->vscroll_policy); break;
		default: G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec); break;
	}
}

static void
quiver_image_view_measure (GtkWidget *widget, GtkOrientation orientation, int for_size, int *minimum, int *natural, int *minimum_baseline, int *natural_baseline)
{
	*minimum = *natural = QUIVER_IMAGE_VIEW_MIN_IMAGE_SIZE;
}

static void
quiver_image_view_size_allocate (GtkWidget *widget, int width, int height, int baseline)
{
	QuiverImageView *imageview = QUIVER_IMAGE_VIEW(widget);
	quiver_image_view_update_size(imageview);
	quiver_image_view_send_reload_event(imageview);
	quiver_image_view_create_scaled_pixbuf(imageview, GDK_INTERP_NEAREST);
}

static void
quiver_image_view_snapshot (GtkWidget *widget, GtkSnapshot *snapshot)
{
	QuiverImageView *imageview = QUIVER_IMAGE_VIEW(widget);
	int width = gtk_widget_get_width(widget);
	int height = gtk_widget_get_height(widget);

	GdkRGBA black = {0, 0, 0, 1};
	gtk_snapshot_append_color(snapshot, &black, &GRAPHENE_RECT_INIT(0, 0, width, height));

	GdkPixbuf *pixbuf = imageview->priv->pixbuf;
	if (imageview->priv->pixbuf_scaled) pixbuf = imageview->priv->pixbuf_scaled;

	if (pixbuf) {
		int pw = gdk_pixbuf_get_width(pixbuf);
		int ph = gdk_pixbuf_get_height(pixbuf);
		float x = MAX(0, (width - pw) / 2.0f);
		float y = MAX(0, (height - ph) / 2.0f);

		GdkTexture *texture = gdk_texture_new_for_pixbuf(pixbuf);
		gtk_snapshot_append_texture(snapshot, texture, &GRAPHENE_RECT_INIT(x, y, pw, ph));
		g_object_unref(texture);
	}
}

void quiver_image_view_set_hadjustment (QuiverImageView *imageview, GtkAdjustment *hadj)
{
	if (imageview->priv->hadjustment == hadj) return;
	if (imageview->priv->hadjustment) {
		g_signal_handlers_disconnect_by_func (imageview->priv->hadjustment, (void*)quiver_image_view_adjustment_value_changed, imageview);
		g_object_unref (imageview->priv->hadjustment);
	}
	imageview->priv->hadjustment = hadj ? (GtkAdjustment*)g_object_ref(hadj) : NULL;
	if (imageview->priv->hadjustment) {
		g_signal_connect (imageview->priv->hadjustment, "value-changed", G_CALLBACK (quiver_image_view_adjustment_value_changed), imageview);
		quiver_image_view_update_size(imageview);
	}
	g_object_notify(G_OBJECT(imageview), "hadjustment");
}

void quiver_image_view_set_vadjustment (QuiverImageView *imageview, GtkAdjustment *vadj)
{
	if (imageview->priv->vadjustment == vadj) return;
	if (imageview->priv->vadjustment) {
		g_signal_handlers_disconnect_by_func (imageview->priv->vadjustment, (void*)quiver_image_view_adjustment_value_changed, imageview);
		g_object_unref (imageview->priv->vadjustment);
	}
	imageview->priv->vadjustment = vadj ? (GtkAdjustment*)g_object_ref(vadj) : NULL;
	if (imageview->priv->vadjustment) {
		g_signal_connect (imageview->priv->vadjustment, "value-changed", G_CALLBACK (quiver_image_view_adjustment_value_changed), imageview);
		quiver_image_view_update_size(imageview);
	}
	g_object_notify(G_OBJECT(imageview), "vadjustment");
}

static void
quiver_image_view_adjustment_value_changed (GtkAdjustment *adjustment, QuiverImageView *imageview)
{
	gtk_widget_queue_draw(GTK_WIDGET(imageview));
}

static void
quiver_image_view_update_size(QuiverImageView *imageview)
{
	GtkWidget *widget = GTK_WIDGET(imageview);
	int width, height;
	quiver_image_view_get_pixbuf_display_size(imageview, &width, &height);

	if (imageview->priv->hadjustment) {
		gtk_adjustment_configure(imageview->priv->hadjustment,
			gtk_adjustment_get_value(imageview->priv->hadjustment),
			0, MAX(gtk_widget_get_width(widget), width),
			gtk_widget_get_width(widget) * 0.1,
			gtk_widget_get_width(widget) * 0.9,
			gtk_widget_get_width(widget));
	}
	if (imageview->priv->vadjustment) {
		gtk_adjustment_configure(imageview->priv->vadjustment,
			gtk_adjustment_get_value(imageview->priv->vadjustment),
			0, MAX(gtk_widget_get_height(widget), height),
			gtk_widget_get_height(widget) * 0.1,
			gtk_widget_get_height(widget) * 0.9,
			gtk_widget_get_height(widget));
	}
}

static void quiver_image_view_get_pixbuf_display_size(QuiverImageView *imageview, gint *width, gint *height) {
    *width = imageview->priv->pixbuf_width;
    *height = imageview->priv->pixbuf_height;
}

static void quiver_image_view_create_scaled_pixbuf(QuiverImageView *imageview, GdkInterpType interptype) {}
static void quiver_image_view_prepare_for_new_pixbuf(QuiverImageView *imageview, gint new_width, gint new_height) {}
static void quiver_image_view_send_reload_event(QuiverImageView *imageview) {}

GtkWidget *quiver_image_view_new() { return g_object_new(QUIVER_TYPE_IMAGE_VIEW, NULL); }
void quiver_image_view_set_pixbuf(QuiverImageView *iv, GdkPixbuf *pb) {
    if (iv->priv->pixbuf) g_object_unref(iv->priv->pixbuf);
    iv->priv->pixbuf = pb ? (GdkPixbuf*)g_object_ref(pb) : NULL;
    if (pb) {
        iv->priv->pixbuf_width = gdk_pixbuf_get_width(pb);
        iv->priv->pixbuf_height = gdk_pixbuf_get_height(pb);
    }
    quiver_image_view_update_size(iv);
    gtk_widget_queue_draw(GTK_WIDGET(iv));
}
void quiver_image_view_set_pixbuf_at_size(QuiverImageView *iv, GdkPixbuf *pb, int w, int h) {
    quiver_image_view_set_pixbuf(iv, pb);
    iv->priv->pixbuf_width = w;
    iv->priv->pixbuf_height = h;
    quiver_image_view_update_size(iv);
}

void quiver_image_view_set_smooth_scroll(QuiverImageView *iv, gboolean s) {}
GdkPixbuf* quiver_image_view_get_pixbuf(QuiverImageView *iv) { return iv->priv->pixbuf; }
void quiver_image_view_set_pixbuf_at_size_ex(QuiverImageView *iv, GdkPixbuf *pb, int w, int h, gboolean r) { quiver_image_view_set_pixbuf_at_size(iv, pb, w, h); }
QuiverImageViewMode quiver_image_view_get_view_mode(QuiverImageView *iv) { return iv->priv->view_mode; }
QuiverImageViewMode quiver_image_view_get_view_mode_unmagnified(QuiverImageView *iv) { return iv->priv->view_mode; }
void quiver_image_view_set_view_mode(QuiverImageView *iv, QuiverImageViewMode m) { iv->priv->view_mode = m; }
void quiver_image_view_reset_view_mode(QuiverImageView *iv, gboolean i) {}
void quiver_image_view_set_enable_transitions(QuiverImageView *iv, gboolean e) {}
gboolean quiver_image_view_is_in_transition(QuiverImageView *iv) { return FALSE; }
void quiver_image_view_set_magnification(QuiverImageView *iv, gdouble m) { iv->priv->magnification = m; }
void quiver_image_view_set_magnification_mode(QuiverImageView *iv, QuiverImageViewMagnificationMode m) {}
gdouble quiver_image_view_get_magnification(QuiverImageView *iv) { return iv->priv->magnification; }
gboolean quiver_image_view_can_magnify(QuiverImageView *iv, gboolean in) { return TRUE; }
void quiver_image_view_get_pixbuf_display_size_for_mode(QuiverImageView *iv, QuiverImageViewMode m, gint *w, gint *h) {}
void quiver_image_view_get_pixbuf_display_size_for_mode_alt(QuiverImageView *iv, QuiverImageViewMode m, gint iw, gint ih, gint *ow, gint *oh) {}
void quiver_image_view_rotate(QuiverImageView *iv, gboolean c) {}
void quiver_image_view_flip(QuiverImageView *iv, gboolean h) {}
void quiver_image_view_connect_pixbuf_loader_signals(QuiverImageView *iv, GdkPixbufLoader *l) {}
void quiver_image_view_connect_pixbuf_size_prepared_signal(QuiverImageView *iv, GdkPixbufLoader *l) {}
GtkAdjustment * quiver_image_view_get_hadjustment(QuiverImageView *iv) { return iv->priv->hadjustment; }
GtkAdjustment * quiver_image_view_get_vadjustment(QuiverImageView *iv) { return iv->priv->vadjustment; }
void quiver_image_view_activate(QuiverImageView *iv) {}
