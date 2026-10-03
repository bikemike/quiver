#include <config.h>

#include <gdk/gdk.h>
// GDK4: gdkx.h removed; X11-specific code guarded out.

#include <libquiver/quiver-icon-view.h>
#include <libquiver/quiver-image-view.h>
#include <libquiver/quiver-pixbuf-utils.h>
#include <libquiver/quiver-navigation-control.h>

#include <gst/gst.h>
#include <gst/video/video.h> // For GstVideoSink
#include <gtk/gtk.h> // For GtkSink

#include "Viewer.h"
#include "ThreadUtil.h"
#include "Timer.h"


#include "QuiverUtils.h"
#include "ShortcutManager.h"
#include "QuiverVideoOps.h"
#include "QuiverRotatedPaintable.h"
#include "ImageLoader.h"
#include "ImageList.h"

#include "QuiverFile.h"

#include "QuiverPrefs.h"
#include "IPreferencesEventHandler.h"

#include "QuiverStockIcons.h"
#include "QuiverFileOps.h"
#include "QuiverClipboard.h"

#include "IImageListEventHandler.h"

#include "Statusbar.h"

#include "IPixbufLoaderObserver.h"
#include "IconViewThumbLoader.h"
#include <memory>
#include <atomic>
#include <vector>
#include <thread>
#include <cmath>
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gst/video/gstvideoaffinetransformationmeta.h>
#include <gst/gl/gstglsyncmeta.h>
#include <gst/gl/gstglcontext.h>
#include <gst/gl/gstglbufferpool.h>

#include <gdk/gdkkeysyms.h>
#include <exiv2/exiv2.hpp>
#include <memory>
#include <math.h>
#include <iostream>

using namespace std;

#define ORIENTATION_ROTATE_CW	0
#define ORIENTATION_ROTATE_CCW 	1
#define ORIENTATION_FLIP_H    	2
#define ORIENTATION_FLIP_V    	3

#define SLIDESHOW_WAIT_DURATION 100 // milliseconds

static int orientation_matrix[4][9] = 
{ 
	{0,6,7,8,5,2,3,4,1}, //cw rotation
	{0,8,5,6,7,4,1,2,3}, //ccw rotation
	{0,2,1,4,3,6,5,8,7}, //flip h
	{0,4,3,2,1,8,7,6,5}, //flip v
};

// combines two orientations
static int combine_matrix[9][9] =
{
	{1,1,2,3,4,5,6,7,8,},
	{1,1,2,3,4,5,6,7,8,},
	{2,2,1,4,3,8,7,6,5,},
	{3,3,4,1,2,7,8,5,6,},
	{4,4,3,2,1,6,5,8,7,},
	{5,5,6,7,8,1,2,3,4,},
	{6,6,5,8,7,4,3,2,1,},
	{7,7,8,5,6,3,4,1,2,},
	{8,8,7,6,5,2,1,4,3,},
};

#if HAVE_GDK_PIXBUF
static GdkPixbuf* icon_pixbuf_callback(QuiverIconView *iconview, gulong cell, gpointer user_data);
static GdkPixbuf* thumbnail_pixbuf_callback(QuiverIconView *iconview, gulong cell, gint* actual_width, gint* actual_height, gpointer user_data);
#endif
static GdkTexture* icon_texture_callback(QuiverIconView *iconview, gulong cell, gpointer user_data);
static GdkTexture* thumbnail_texture_callback(QuiverIconView *iconview, gulong cell, gint* actual_width, gint* actual_height, gpointer user_data);
static GdkTexture* filmstrip_texture_callback(QuiverIconView* iconview, gulong cell,
	gint thumb_natural_w, gint thumb_natural_h,
	gint thumb_drawn_w, gint thumb_drawn_h,
	QuiverIconViewFilmstripSide side, gpointer user_data);
static gulong n_cells_callback(QuiverIconView *iconview, gpointer user_data);
static void image_view_adjustment_changed (GtkAdjustment *adjustment, gpointer user_data);
static void image_view_adjustment_value_changed (GtkAdjustment *adjustment, gpointer user_data);

/* Navigation control geometry.  The control is pinned to the bottom-right
 * corner with the same inset from the right edge as from the bottom one; the
 * fallbacks are only used before the widgets have been measured. */
#define NAV_CONTROL_EDGE_MARGIN    16
#define NAV_CONTROL_FALLBACK_WIDTH 154 /* max control size + pill padding/border */
#define PILL_ROW_FALLBACK_WIDTH    540

static void viewer_radio_action_handler_cb(GSimpleAction *action, GVariant *parameter, gpointer user_data);
static void viewer_action_handler_cb(GSimpleAction *action, GVariant *parameter, gpointer data);

static gboolean viewer_scrollwheel_event(GtkEventControllerScroll *controller, gdouble dx, gdouble dy, gpointer data);
static void viewer_motion_notify(GtkEventControllerMotion *controller, gdouble x, gdouble y, gpointer data);
static void viewer_imageview_activated(QuiverImageView *imageview,gpointer data);
static void viewer_imageview_reload(QuiverImageView *imageview,gpointer data);
static void viewer_imageview_magnification_changed(QuiverImageView *imageview,gpointer data);
static void viewer_imageview_view_mode_changed(QuiverImageView *imageview,gpointer data);
static gboolean viewer_imageview_key_press_event(GtkEventControllerKey *controller, guint keyval, guint keycode, GdkModifierType state, gpointer userdata);

static void viewer_iconview_cell_activated(QuiverIconView *iconview,gulong cell,gpointer data);
static void viewer_iconview_cursor_changed(QuiverIconView *iconview,gulong cell,gpointer data);
static void viewer_icon_view_map_cb(GtkWidget *widget, gpointer user_data);
static void viewer_icon_view_unmap_cb(GtkWidget *widget, gpointer user_data);

static void viewer_volume_value_changed (GtkRange *range, gdouble value, gpointer user_data);

static gboolean viewer_scale_change_value_cb(GtkRange *range, GtkScrollType scroll, gdouble value, gpointer user_data);





static void set_widget_bg_color(GtkWidget *widget, const GdkRGBA *color) {
    QuiverUtils::SetWidgetBgColor(widget, color);
}


// popup menu callbacks
static void viewer_show_context_menu(GtkWidget *widget, gdouble x_root, gdouble y_root, guint32 time, gpointer userdata);
static void viewer_button_press_cb(GtkGestureClick *gesture, gint n_press, gdouble x, gdouble y, gpointer user_data);
static void viewer_button_release_cb(GtkGestureClick *gesture, gint n_press, gdouble x, gdouble y, gpointer user_data);
static void viewer_video_options_create_popup_cb(GtkMenuButton *button, gpointer user_data);
static void viewer_snapshot_button_clicked_cb(GtkButton *button, gpointer user_data);
static void viewer_slideshow_resume_clicked_cb(gpointer user_data);
static void viewer_video_rw_cb(gpointer user_data);
static void viewer_video_ff_cb(gpointer user_data);
static void viewer_frame_step_back_cb(gpointer user_data);
static void viewer_frame_step_fwd_cb(gpointer user_data);
static void viewer_shortcuts_changed_cb(gpointer user_data);

/* GTK4 Gesture and Overlay Callbacks */
static void viewer_gesture_zoom_begin_cb(GtkGesture *gesture, GdkEventSequence *sequence, gpointer user_data);
static void viewer_gesture_zoom_scale_changed_cb(GtkGestureZoom *gesture, gdouble scale, gpointer user_data);
static void viewer_gesture_zoom_end_cb(GtkGesture *gesture, GdkEventSequence *sequence, gpointer user_data);

static void viewer_two_finger_pan_begin_cb(GtkGestureDrag *gesture, gdouble start_x, gdouble start_y, gpointer user_data);
static void viewer_two_finger_pan_update_cb(GtkGestureDrag *gesture, gdouble offset_x, gdouble offset_y, gpointer user_data);
static void viewer_two_finger_pan_end_cb(GtkGestureDrag *gesture, gdouble offset_x, gdouble offset_y, gpointer user_data);

static void viewer_swipe_cb(GtkGestureSwipe *gesture, gdouble velocity_x, gdouble velocity_y, gpointer user_data);

static void viewer_overlay_prev_cb(Viewer::ViewerImpl *p);
static void viewer_overlay_next_cb(Viewer::ViewerImpl *p);
static void viewer_overlay_slideshow_cb(Viewer::ViewerImpl *p);
static void viewer_overlay_zoom_out_cb(Viewer::ViewerImpl *p);
static void viewer_overlay_zoom_fit_cb(Viewer::ViewerImpl *p);
static void viewer_overlay_zoom_in_cb(Viewer::ViewerImpl *p);
static void viewer_overlay_rotate_ccw_cb(Viewer::ViewerImpl *p);
static void viewer_overlay_rotate_cw_cb(Viewer::ViewerImpl *p);
static void viewer_image_submenu_create_popup_cb(GtkMenuButton *button, gpointer user_data);
static void viewer_view_mode_create_popup_cb(GtkWidget *popover, gpointer user_data);
static void viewer_view_mode_arrow_cb(Viewer::ViewerImpl *p);
static void viewer_overlay_fullscreen_cb(Viewer::ViewerImpl *p);

static const char* get_fullscreen_icon_name(bool bFullscreen)
{
	GdkDisplay *disp = gdk_display_get_default();
	GtkIconTheme *theme = disp ? gtk_icon_theme_get_for_display(disp) : NULL;
	if (bFullscreen)
	{
		if (theme && gtk_icon_theme_has_icon(theme, "unfullscreen-square-symbolic"))
			return "unfullscreen-square-symbolic";
		if (theme && gtk_icon_theme_has_icon(theme, "unfullscreen-square"))
			return "unfullscreen-square";
		return "view-restore-symbolic";
	}
	else
	{
		if (theme && gtk_icon_theme_has_icon(theme, "fullscreen-square-symbolic"))
			return "fullscreen-square-symbolic";
		if (theme && gtk_icon_theme_has_icon(theme, "fullscreen-square"))
			return "fullscreen-square";
		return "view-fullscreen-symbolic";
	}
}

static GdkContentProvider* signal_drag_source_prepare(GtkDragSource *source, gdouble x, gdouble y, gpointer user_data);
static void signal_drag_begin (GtkDragSource *source, GdkDrag *drag, gpointer user_data);
static void signal_drag_end(GtkDragSource *source, GdkDrag *drag, gpointer user_data);

/* Attach the shared input controllers (click/scroll/motion/gestures) to a widget
 * so the viewer area responds over the image, the video fixed and the GL
 * sink widget alike. */
static void attach_viewer_input_controllers(GtkWidget *widget, gpointer user_data)
{
	GtkGesture *click = gtk_gesture_click_new();
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), 0);
	g_signal_connect(click, "pressed", G_CALLBACK(viewer_button_press_cb), user_data);
	g_signal_connect(click, "released", G_CALLBACK(viewer_button_release_cb), user_data);
	gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(click));

	/* One shared scroll controller on every viewer widget (image view, video
	 * fixed, GL sink).  It is created smooth; the handler toggles
	 * GTK_EVENT_CONTROLLER_SCROLL_DISCRETE on/off depending on whether the
	 * user is zooming (Ctrl/Shift -> smooth continuous zoom) or navigating
	 * (no modifier -> discrete, one image per wheel notch). */
	GtkEventController *scroll = gtk_event_controller_scroll_new(
		(GtkEventControllerScrollFlags)GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES);
	g_signal_connect(scroll, "scroll", G_CALLBACK(viewer_scrollwheel_event), user_data);
	gtk_widget_add_controller(widget, scroll);

	GtkEventController *motion = gtk_event_controller_motion_new();
	g_signal_connect(motion, "motion", G_CALLBACK(viewer_motion_notify), user_data);
	gtk_widget_add_controller(widget, motion);

	/* GTK4 Gesture Controllers: pinch-to-zoom, two-finger pan, swipe/flick */
	GtkGesture *zoom = gtk_gesture_zoom_new();
	g_signal_connect(zoom, "begin", G_CALLBACK(viewer_gesture_zoom_begin_cb), user_data);
	g_signal_connect(zoom, "scale-changed", G_CALLBACK(viewer_gesture_zoom_scale_changed_cb), user_data);
	g_signal_connect(zoom, "end", G_CALLBACK(viewer_gesture_zoom_end_cb), user_data);
	gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(zoom));

	GtkGesture *two_finger_pan = GTK_GESTURE(g_object_new(GTK_TYPE_GESTURE_DRAG, "n-points", 2, NULL));
	g_signal_connect(two_finger_pan, "drag-begin", G_CALLBACK(viewer_two_finger_pan_begin_cb), user_data);
	g_signal_connect(two_finger_pan, "drag-update", G_CALLBACK(viewer_two_finger_pan_update_cb), user_data);
	g_signal_connect(two_finger_pan, "drag-end", G_CALLBACK(viewer_two_finger_pan_end_cb), user_data);
	gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(two_finger_pan));

	gtk_gesture_group(zoom, two_finger_pan);

	GtkGesture *swipe = gtk_gesture_swipe_new();
	gtk_gesture_single_set_touch_only(GTK_GESTURE_SINGLE(swipe), TRUE);
	g_signal_connect(swipe, "swipe", G_CALLBACK(viewer_swipe_cb), user_data);
	gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(swipe));
}


static gboolean timeout_play_position (gpointer data);
static gboolean timeout_event_motion_notify (gpointer user_data);
static void set_control_visible(GtkWidget* w, bool visible);
static void viewer_set_controls_visible(Viewer::ViewerImpl *p, bool visible);
static void viewer_controls_show(Viewer::ViewerImpl *p);
static void viewer_set_controls_opacity(Viewer::ViewerImpl *p, double opacity);
static void viewer_set_idle_cursor(Viewer::ViewerImpl *p, bool hidden);

static gchar* gst_time_format(gint64 time);

#if VIDEO_ZOOM_SMOOTH_ANIMATION
static gboolean video_zoom_timeout(gpointer data);
#endif
static void video_zoom_get_pointer(Viewer::ViewerImpl *p, gdouble *px, gdouble *py);
static void video_zoom_get_pointer_in(Viewer::ViewerImpl *p, GtkWidget *area, gdouble *px, gdouble *py);
static void video_zoom_raise_media_windows(Viewer::ViewerImpl *p);
static void video_zoom_sink_map_cb(GtkWidget *widget, gpointer user_data);
static void video_paintable_invalidated_cb(GdkPaintable *paintable, gpointer user_data);
static void frame_video_preview_from_frames(QuiverImageView *imageview, GdkTexture **frames,
	gsize n_frames, gint *delays_ms, gint width, gint height, gboolean reset_view_mode,
	Viewer::ViewerImpl *impl);
static void frame_video_preview_from_video(QuiverImageView *imageview, GdkTexture *texture,
	gint width, gint height, gboolean reset_view_mode, Viewer::ViewerImpl *impl);
static void viewer_image_adjustment_changed_cb(GtkAdjustment *adjustment, gpointer user_data);
static GstPadProbeReturn video_sink_glitch_probe_cb(GstPad *pad, GstPadProbeInfo *info, gpointer user_data);

#define OVERLAY_AUTO_HIDE_TIMEOUT_MS   800 /* ms before HUD / filmstrip auto-hide */


#define ACTION_VIEWER_SLIDESHOW        "SlideShow"
#define ACTION_VIEWER_CUT              "ViewerCut"
#define ACTION_VIEWER_COPY             "ViewerCopy"
#define ACTION_VIEWER_TRASH            "ViewerTrash"
#define ACTION_VIEWER_TRASH_FORCE      "ViewerTrashForce"
#define ACTION_VIEWER_RESTORE          "ViewerRestore"
#define ACTION_VIEWER_PREVIOUS         "ImagePrevious"
#define ACTION_VIEWER_NEXT             "ImageNext"
#define ACTION_VIEWER_FIRST            "ImageFirst"
#define ACTION_VIEWER_LAST             "ImageLast"
#define ACTION_VIEWER_ZOOM             "Zoom"
#define ACTION_VIEWER_ZOOM_FIT         "ZoomFit"
#define ACTION_VIEWER_ZOOM_FIT_STRETCH "ZoomFitStretch"
#define ACTION_VIEWER_ZOOM_FILL_SCREEN "ZoomFillScreen"
#define ACTION_VIEWER_ZOOM_100         "Zoom100"
#define ACTION_VIEWER_ZOOM_IN          "ZoomIn"
#define ACTION_VIEWER_ZOOM_OUT         "ZoomOut"
#define ACTION_VIEWER_ZOOM_KEEP        "ZoomKeep"
#define ACTION_VIEWER_ROTATE_CW        "RotateCW"
#define ACTION_VIEWER_ROTATE_CCW       "RotateCCW"
#define ACTION_VIEWER_FLIP_H           "FlipH"
#define ACTION_VIEWER_FLIP_V           "FlipV"

/* Multiply a smooth (touchpad) scroll delta by this to get a "notch"
 * equivalent for proportional Ctrl+wheel zooming.  WHEEL deltas are already
 * whole notches (1.0).  1.0 = treat 1 surface unit as one notch; lower makes
 * touchpad zoom slower.  Tune on-device. */
#define QUIVER_SCROLL_SURFACE_PER_NOTCH  0.25
#define ACTION_VIEWER_VIEW_FILM_STRIP  "ViewFilmStrip"
#define ACTION_VIEWER_NEXT_2            ACTION_VIEWER_NEXT"_2"
#define ACTION_VIEWER_PREVIOUS_2        ACTION_VIEWER_PREVIOUS"_2"
#define ACTION_VIEWER_ROTATE_CW_2       ACTION_VIEWER_ROTATE_CW"_2"
#define ACTION_VIEWER_ROTATE_CCW_2      ACTION_VIEWER_ROTATE_CCW"_2"
#define ACTION_VIEWER_ROTATE_FOR_BEST_FIT "MaximizeForDisplay"
#define ACTION_VIEWER_FLIP_H_2          ACTION_VIEWER_FLIP_H"_2"
#define ACTION_VIEWER_FLIP_V_2          ACTION_VIEWER_FLIP_V"_2"

#define ACTION_VIEWER_VIDEO_PLAY         "VideoPlay"
#define ACTION_VIEWER_VIDEO_SKIP_FORWARD "VideoSkipForward"
#define ACTION_VIEWER_VIDEO_SKIP_BACK    "VideoSkipBack"
#define ACTION_VIEWER_VIDEO_PLAY_2       ACTION_VIEWER_VIDEO_PLAY"_2"
#define ACTION_VIEWER_VIDEO_SEEK_FWD_5   "VideoSeekFwd5"
#define ACTION_VIEWER_VIDEO_SEEK_BACK_5  "VideoSeekBack5"
#define ACTION_VIEWER_VIDEO_FRAME_FWD    "VideoFrameFwd"
#define ACTION_VIEWER_VIDEO_FRAME_BACK   "VideoFrameBack"
#define ACTION_VIEWER_VIDEO_SNAPSHOT     "VideoSnapshot"
#define ACTION_VIEWER_VIDEO_MUTE         "VideoMute"








// has next
static const gchar* pszActionsNext[] =
{
	ACTION_VIEWER_NEXT,
	ACTION_VIEWER_NEXT_2,
	ACTION_VIEWER_LAST,
};
// has prev
static const gchar* pszActionsPrev[] =
{
	ACTION_VIEWER_PREVIOUS,
	ACTION_VIEWER_PREVIOUS_2,
	ACTION_VIEWER_FIRST,
};


// actions that should only be sensitive when an image is loaded
static const gchar* pszActionsImage[] =
{
	ACTION_VIEWER_ROTATE_CW,
	ACTION_VIEWER_ROTATE_CCW,
	ACTION_VIEWER_FLIP_H,
	ACTION_VIEWER_FLIP_V,
	ACTION_VIEWER_ZOOM_IN,
	ACTION_VIEWER_ZOOM_OUT,
	ACTION_VIEWER_COPY,
	ACTION_VIEWER_TRASH,
};

static const gchar* pszActionsVideo[] =
{
	ACTION_VIEWER_VIDEO_PLAY,
	ACTION_VIEWER_VIDEO_PLAY_2,
	ACTION_VIEWER_VIDEO_SKIP_FORWARD,
	ACTION_VIEWER_VIDEO_SKIP_BACK,
	ACTION_VIEWER_VIDEO_SEEK_FWD_5,
	ACTION_VIEWER_VIDEO_SEEK_BACK_5,
	ACTION_VIEWER_VIDEO_FRAME_FWD,
	ACTION_VIEWER_VIDEO_FRAME_BACK,
	ACTION_VIEWER_VIDEO_SNAPSHOT,
	ACTION_VIEWER_VIDEO_MUTE,
	"VideoSpeed025",
	"VideoSpeed05",
	"VideoSpeed10",
	"VideoSpeed15",
	"VideoSpeed20",
	"VideoSpeed40",
	"VideoSpeed80",
	"VideoSpeed160",
};



/* Shared, ref-counted handle to the viewer's image-view widget.  Both the
 * loader observer and every pixbuf idle posted from the loader thread hold a
 * reference.  When the widget is destroyed (component teardown) its "destroy"
 * handler NULLs the pointers, so a pixbuf idle that was already queued when
 * the widget died can no longer write into freed widgets. */
struct ViewPixbufTarget {
	QuiverImageView *pImageView;
	GtkWidget *pErrorLabel;
	/* the viewer this target writes into, for the zoom trace only: it is what
	 * turns a delivery line into "which file" rather than "which picture", and
	 * without it two stills in a row are indistinguishable in the log */
	Viewer::ViewerImpl *pOwner;
	int iRefs;
};

static void view_pixbuf_target_destroyed(GtkWidget *widget, gpointer data)
{
	(void)widget;
	ViewPixbufTarget *t = (ViewPixbufTarget*)data;
	t->pImageView = NULL;
	t->pErrorLabel = NULL;
}

static ViewPixbufTarget* view_pixbuf_target_new(QuiverImageView *pImageView, GtkWidget *pErrorLabel,
	Viewer::ViewerImpl *pOwner)
{
	ViewPixbufTarget *t = new ViewPixbufTarget{pImageView, pErrorLabel, pOwner, 1};
	g_signal_connect(G_OBJECT(pImageView), "destroy", G_CALLBACK(view_pixbuf_target_destroyed), t);
	return t;
}

/* The zoomed view modes both let the user pick the magnification; they only
 * differ in whether it survives loading the next image. */
static gboolean viewer_view_mode_is_zoomed(QuiverImageViewMode mode)
{
	return (QUIVER_IMAGE_VIEW_MODE_ZOOM == mode
		|| QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP == mode);
}

/* "keep zoom and pan": the mode that carries the zoom and the pan over to the
 * next item, whichever kind of item that is. */
static gboolean viewer_view_mode_is_keep(QuiverImageViewMode mode)
{
	return (QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP == mode);
}

/* "Keep zoom and pan" has to survive every path that delivers a new image to
 * the view (the decoded image, the quick preview and the full-size reload), so
 * the reset is suppressed here rather than at each call site. */
gboolean Viewer::ShouldResetViewForNewImage(GtkWidget *pImageView, bool bResetViewMode)
{
	/* Every route that hands a new image to the view goes through here, the
	 * loader's as well as the cached thumbnail one: "keep zoom and pan" has
	 * to survive the image changing, whichever way the new pixels arrive. */
	if (NULL != pImageView && QUIVER_IS_IMAGE_VIEW(pImageView))
	{
		if (QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP == quiver_image_view_get_view_mode(QUIVER_IMAGE_VIEW(pImageView)))
		{
			return FALSE;
		}
	}

	return bResetViewMode ? TRUE : FALSE;
}


static void view_pixbuf_target_ref(ViewPixbufTarget *t)
{
	g_atomic_int_inc(&t->iRefs);
}

static void view_pixbuf_target_unref(ViewPixbufTarget *t)
{
	if (g_atomic_int_dec_and_test(&t->iRefs))
	{
		if (t->pImageView && G_IS_OBJECT(t->pImageView))
		{
			g_signal_handlers_disconnect_by_func(t->pImageView, (gpointer)view_pixbuf_target_destroyed, t);
		}
		delete t;
	}
}

#if HAVE_GDK_PIXBUF
struct AsyncPixbufData {
	ViewPixbufTarget *pTarget;
	GdkPixbuf *pixbuf;
	gint width, height;
	gboolean bReset;
	bool bAtSize;
};
#endif

struct AsyncTextureData {
	ViewPixbufTarget *pTarget;
	GdkTexture *texture;
	gint width, height;
	gboolean bReset;
	bool bAtSize;
};

/* Animation frame set handed across a thread boundary; owns its frames until
 * the idle callback replaces them and releases them. */
struct AsyncAnimationFramesData {
	ViewPixbufTarget *pTarget;
	GdkTexture **frames;
	gint *delays;
	gsize count;
	gint width, height;
	gboolean bReset;
};

/* toggle the "failed to load" indicator for the viewer image view.
 * SetPixbuf(NULL) is only ever dispatched by the image loader for a failed
 * load (or for a previously-failed image), so NULL == show the indicator. */
static void show_image_load_error(GtkWidget *pErrorLabel, bool bShow)
{
	if (NULL == pErrorLabel)
		return;
	if (bShow != gtk_widget_get_visible(pErrorLabel))
		gtk_widget_set_visible(pErrorLabel, bShow);
}

#if HAVE_GDK_PIXBUF
static gboolean idle_set_pixbuf_v(gpointer data) {
	AsyncPixbufData *p = (AsyncPixbufData*)data;
	/* the image view may already be gone (queued before widget teardown) */
	if (p->pTarget->pImageView != NULL)
	{
		/* an aborted/preview-load path sends a real pixbuf; only the failure
		 * path sends NULL */
		show_image_load_error(p->pTarget->pErrorLabel, p->pixbuf == NULL);
		if (p->bAtSize) {
			quiver_image_view_set_pixbuf_at_size_ex(p->pTarget->pImageView, p->pixbuf, p->width, p->height, p->bReset);
		} else {
			quiver_image_view_set_pixbuf(p->pTarget->pImageView, p->pixbuf);
		}
	}
	if (p->pixbuf) g_object_unref(p->pixbuf);
	view_pixbuf_target_unref(p->pTarget);
	delete p;
	return FALSE;
}
#endif

static gboolean idle_set_texture_v(gpointer data) {
	AsyncTextureData *p = (AsyncTextureData*)data;
	if (p->pTarget->pImageView != NULL)
	{
		/* the delivery that is about to happen, on the GUI thread and with the
		 * picture the loader finished: the file is named here because that is
		 * the point at which the next image is really swapped in */
		show_image_load_error(p->pTarget->pErrorLabel, p->texture == NULL);
		if (p->bAtSize) {
			/* a video's own preview arrives here, and the video's zoom and pan
			 * are what it has to be shown at */
			frame_video_preview_from_video(p->pTarget->pImageView, p->texture,
				p->width, p->height, p->bReset, p->pTarget->pOwner);
		} else {
			quiver_image_view_set_texture(p->pTarget->pImageView, p->texture);
		}
	}
	if (p->texture) g_object_unref(p->texture);
	view_pixbuf_target_unref(p->pTarget);
	delete p;
	return FALSE;
}

static gboolean idle_set_animation_frames_v(gpointer data) {
	AsyncAnimationFramesData *p = (AsyncAnimationFramesData*)data;
	if (p->pTarget->pImageView != NULL)
	{
		show_image_load_error(p->pTarget->pErrorLabel, FALSE);
		/* a video's preview arrives as frames, and is framed like any other
		 * delivery: the flag, the picture, then the video's zoom and pan */
		frame_video_preview_from_frames(p->pTarget->pImageView, p->frames, p->count,
			p->delays, p->width, p->height, p->bReset, p->pTarget->pOwner);
	}
	quiver_animation_frames_free(p->frames, p->delays, p->count);
	view_pixbuf_target_unref(p->pTarget);
	delete p;
	return FALSE;
}

class ViewerImageViewPixbufLoaderObserver : public IPixbufLoaderObserver
{
public:
	ViewerImageViewPixbufLoaderObserver(QuiverImageView *imageview, GtkWidget *pErrorLabel,
		Viewer::ViewerImpl *pOwner)
		: m_pTarget(view_pixbuf_target_new(imageview, pErrorLabel, pOwner)) {};
	virtual ~ViewerImageViewPixbufLoaderObserver(){
		if (m_pTarget && m_pTarget->pImageView && G_IS_OBJECT(m_pTarget->pImageView))
		{
			g_signal_handlers_disconnect_by_func(m_pTarget->pImageView, (gpointer)view_pixbuf_target_destroyed, m_pTarget);
			m_pTarget->pImageView = NULL;
			m_pTarget->pErrorLabel = NULL;
		}
		view_pixbuf_target_unref(m_pTarget);
	};

#if HAVE_GDK_PIXBUF
	virtual void ConnectSignals(GdkPixbufLoader *loader){
		quiver_image_view_connect_pixbuf_loader_signals(m_pTarget->pImageView,loader);
		};
	virtual void ConnectSignalSizePrepared(GdkPixbufLoader * loader){
		quiver_image_view_connect_pixbuf_size_prepared_signal(m_pTarget->pImageView,loader);
		};

	// custom calls
	virtual void SetPixbuf(GdkPixbuf * pixbuf){
		if (ThreadUtil::IsGUIThread()) {
			show_image_load_error(m_pTarget->pErrorLabel, pixbuf == NULL);
			quiver_image_view_set_pixbuf(m_pTarget->pImageView,pixbuf);
		} else {
			if (pixbuf) g_object_ref(pixbuf);
			AsyncPixbufData *data = new AsyncPixbufData{m_pTarget, pixbuf, 0, 0, FALSE, false};
			view_pixbuf_target_ref(m_pTarget);
			g_idle_add_full(G_PRIORITY_HIGH, idle_set_pixbuf_v, data, NULL);
		}
	};
	virtual void SetPixbufAtSize(GdkPixbuf *pixbuf, gint width, gint height, bool bResetViewMode = true ){
		gboolean bReset = Viewer::ShouldResetViewForNewImage(GTK_WIDGET(m_pTarget->pImageView), bResetViewMode);
		if (ThreadUtil::IsGUIThread()) {
			show_image_load_error(m_pTarget->pErrorLabel, false);
			quiver_image_view_set_pixbuf_at_size_ex(m_pTarget->pImageView,pixbuf,width,height,bReset);
		} else {
			if (pixbuf) g_object_ref(pixbuf);
			AsyncPixbufData *data = new AsyncPixbufData{m_pTarget, pixbuf, width, height, bReset, true};
			view_pixbuf_target_ref(m_pTarget);
			g_idle_add_full(G_PRIORITY_HIGH, idle_set_pixbuf_v, data, NULL);
		}
	};
#endif
	virtual void SetTexture(GdkTexture * texture){
		if (ThreadUtil::IsGUIThread()) {
			show_image_load_error(m_pTarget->pErrorLabel, texture == NULL);
			quiver_image_view_set_texture(m_pTarget->pImageView, texture);
		} else {
			if (texture) g_object_ref(texture);
			AsyncTextureData *data = new AsyncTextureData{m_pTarget, texture, 0, 0, FALSE, false};
			view_pixbuf_target_ref(m_pTarget);
			g_idle_add_full(G_PRIORITY_HIGH, idle_set_texture_v, data, NULL);
		}
	};
	virtual void SetTextureAtSize(GdkTexture *texture, gint width, gint height, bool bResetViewMode = true ){
		gboolean bReset = Viewer::ShouldResetViewForNewImage(GTK_WIDGET(m_pTarget->pImageView), bResetViewMode);
		if (ThreadUtil::IsGUIThread()) {
			show_image_load_error(m_pTarget->pErrorLabel, false);
			/* a video's own preview arrives here, and the video's zoom and pan
			 * are what it has to be shown at */
			frame_video_preview_from_video(m_pTarget->pImageView, texture,
				width, height, bReset, m_pTarget->pOwner);
		} else {
			if (texture) g_object_ref(texture);
			AsyncTextureData *data = new AsyncTextureData{m_pTarget, texture, width, height, bReset, true};
			view_pixbuf_target_ref(m_pTarget);
			g_idle_add_full(G_PRIORITY_HIGH, idle_set_texture_v, data, NULL);
		}
	};
	virtual void SetAnimationFrames(GdkTexture **frames, gint *delays_ms, gsize n_frames,
	                               gint width, gint height, bool bResetViewMode = true ){
		gboolean bReset = Viewer::ShouldResetViewForNewImage(GTK_WIDGET(m_pTarget->pImageView), bResetViewMode);
		if (ThreadUtil::IsGUIThread()) {
			show_image_load_error(m_pTarget->pErrorLabel, false);
			/* a video's preview arrives as frames, and is framed like any other
			 * delivery: the flag, the picture, then the video's zoom and pan */
			frame_video_preview_from_frames(m_pTarget->pImageView, frames, n_frames,
				delays_ms, width, height, bReset, m_pTarget->pOwner);
			quiver_animation_frames_free(frames, delays_ms, n_frames);
		} else {
			AsyncAnimationFramesData *data = new AsyncAnimationFramesData{
				m_pTarget, frames, delays_ms, n_frames, width, height, bReset};
			view_pixbuf_target_ref(m_pTarget);
			g_idle_add_full(G_PRIORITY_HIGH, idle_set_animation_frames_v, data, NULL);
		}
	};
	
	virtual void SignalBytesRead(long bytes_read,long total){ (void)total;  (void)bytes_read; };
private:
	ViewPixbufTarget *m_pTarget;
};

typedef boost::shared_ptr<IPixbufLoaderObserver> IPixbufLoaderObserverPtr;


class Viewer::ViewerImpl
{
public:

	typedef enum _ScrollbarType
	{
		HORIZONTAL,
		VERTICAL,
		BOTH,
	}ScrollbarType;
		
// constructor / destructor 
	ViewerImpl(Viewer *pViewer);
	~ViewerImpl();

// methods
	void SetImageList(IImageListViewPtr imgList);
	void Rename();
	void UpdateUI();
	
	void CacheNext(bool bDirectionForward);
	void SetImageIndex(int index, bool bDirectionForward, bool bCacheNext = true);

	int  GetCurrentOrientation(bool bCombinedWithMaximizedOrientation = false);
	int  GetMaximizedOrientation(QuiverFile f, bool bCombinedWithFileOrientation = false);

	void CacheImageAtSize(QuiverFile f, int w, int h);
	void LoadImageAtSize(QuiverFile f, int w, int h);
	void LoadImage(QuiverFile f);
	void GrowDecodeSizeForZoom(QuiverFile f, gint &width, gint &height);

	void SetCurrentOrientation(int iOrientation, bool bUpdateExif = true);
	void AddFilmstrip();
	
	void UpdateScrollbars();

	void QueueIconViewUpdate(int timeout = 100 /* ms */);

	void SlideShowStop(bool bEmitStopEvent = true, bool bUpdateUI = true);

	bool IsPlaying() const
	{
		return m_bIsPlaying;
	}

	/* The timeline row in the unified HUD is visible for videos only once playback has started and controls are visible. */
	void UpdateTimelineVisibility()
	{
		if (m_pTimelineRow)
		{
			bool visible = IsVideo() && m_bVideoPlaybackStarted && m_bControlsVisible;
			set_control_visible(m_pTimelineRow, visible);
		}
	}

	void SetIsPlaying(bool isPlaying)
	{
		bool bWasPlaying = m_bIsPlaying;
		m_bIsPlaying = isPlaying;

		/* surface playback transitions so the app can keep the screen awake
		 * while a video is actually playing.  Skip during teardown: emitting
		 * would call m_pViewer->shared_from_this(), which throws bad_weak_ptr
		 * while the Viewer itself is being destroyed. */
		if (!m_bShuttingDown && isPlaying && !bWasPlaying)
			m_pViewer->EmitVideoPlaybackStartedEvent();
		else if (!m_bShuttingDown && !isPlaying && bWasPlaying)
			m_pViewer->EmitVideoPlaybackStoppedEvent();

		if (0 != m_iTimeoutPlayProgress)
		{
			g_source_remove(m_iTimeoutPlayProgress);
			m_iTimeoutPlayProgress = 0;
		}

		if (IsPlaying())
		{
			m_bVideoPlaybackStarted = true;
			m_bTimelineVisible = true;
			UpdateTimelineVisibility();
			if (m_pPlayImage && GTK_IS_IMAGE(m_pPlayImage))
				gtk_image_set_from_icon_name(GTK_IMAGE(m_pPlayImage), "media-playback-pause-symbolic");

			/* hide center play button during playback */
			if (m_pCenterPlayBtn)
				gtk_widget_set_visible(m_pCenterPlayBtn, FALSE);

			/* show HUD via opacity fade-in */
			viewer_controls_show(this);
			StartControlsFade(true);

			m_iTimeoutPlayProgress = g_timeout_add(200,timeout_play_position,this);
		}
		else
		{
			CancelControlsFade();
			if (m_pPlayImage && GTK_IS_IMAGE(m_pPlayImage))
				gtk_image_set_from_icon_name(GTK_IMAGE(m_pPlayImage), "media-playback-start-symbolic");
			/* show center play button when paused/stopped on video */
			UpdateCenterPlayButtonVisibility();
			/* A pause (click or keyboard) must not leave the pointer hidden.
			 * But the slideshow machine stops the video on every advance
			 * (SetImageIndex -> StopVideo), and that must not flash the
			 * pointer back on while the show is running and the user is idle. */
			if (!(m_bSlideShowRunning && !m_bSlideShowPaused))
			{
				viewer_set_idle_cursor(this, false);
			}
			RefreshAutoHideTimer();
		}

		UpdateFilmstripForPlayback();
	}

	/* every interaction (click, pan, seek, play/pause) must reset the
	 * auto-hide timer; otherwise a timer armed before the click can fire right
	 * after it and make the whole media bar (timeline included) disappear */
	void RefreshAutoHideTimer()
	{
		if (0 != m_iTimeoutMouseMotionNotify)
		{
			g_source_remove(m_iTimeoutMouseMotionNotify);
			m_iTimeoutMouseMotionNotify = 0;
		}
		if (m_ImageListPtr && m_ImageListPtr->GetSize() > 0)
		{
			m_iTimeoutMouseMotionNotify = g_timeout_add(OVERLAY_AUTO_HIDE_TIMEOUT_MS, timeout_event_motion_notify, this);
		}
	}

	// methods for playing videos
	void PlayPauseVideo();
	void SkipForward();
	void SkipBack();
	void SeekRelative(gint64 seconds);
	void UpdateTimeline();
	void StopVideo(bool reloadImage = true, bool keepPreviewFraming = false);
	void SetPlaybackSpeed(double speed);
	void Snapshot();
	bool IsMuted() const { return m_bMuted; }
	void ToggleMute();
	void SetMuted(bool bMute);
	void UpdateVolumeUI();
	// returns true if current item is a video
	bool IsVideo() const;
	// switch the stack to the video page and unhide the video widgets
	void ShowVideoPage();

	// video zoom (in-pipeline crop, optionally HW-accelerated upscale)
	void SetVideoZoom(gdouble zoom);
	/* the visible part of the frame, as a fraction of it (0..1) */
	void SetVideoPanPixels(gdouble px, gdouble py);
	gdouble GetVideoPanOffsetX() const;
	gdouble GetVideoPanOffsetY() const;
	void CaptureVideoPanFromPreview();
	/* Take the zoom and the pan from the quick preview that is on screen, and
	 * report whether there was a framing to take (a preview showing the whole
	 * frame has none, and leaves the video's own zoom alone) */
	bool AdoptVideoPreviewFraming(bool bProvisional = false);
	/* The picture the view is drawing, which is what a magnification is
	 * measured against: not the texture decoded for it, which can be a small
	 * grab scaled up to fill the frame, but the size the picture was declared at
	 * - for a video's quick preview, the frame's own size. */
	bool GetPreviewPictureSize(gint* width, gint* height) const;
	/* Put the quick preview at the zoom and the pan the video itself is going
	 * to play at, which is the only framing that can be right for a video: the
	 * picture the image view is holding is a preview that was fitted into the
	 * widget, not the framing the user chose, so nothing it kept from the
	 * previous item is worth keeping here. */
	/* Bring the quick preview to the video's zoom and pan.  bNewPicture says the
	 * picture has just landed, which is the only time the view's own framing can
	 * be the one to keep - a re-frame after a zoom change is the other way
	 * round, the video is the authority and the view is told. */
	void FrameVideoPreviewFromVideo(gboolean bNewPicture);
	bool IsVideoPreviewFraming() const;
	/* true while a picture is being handed to the view: the adjustments move
	 * then, and what they say is the delivery's own doing rather than a place
	 * the user put the video, so it is not read back as one */
	bool m_bFramingVideoPreview = false;
	/* the item whose quick preview is on screen, framed to that video's zoom and
	 * pan.  Empty is "no picture is a pan source": the view is showing the
	 * previous item, a half-laid-out delivery, or nothing at all, and where it
	 * happens to be scrolled to then says nothing about where the video that is
	 * loading is to play from.  Naming the item rather than holding a flag is
	 * what makes this self-checking - the moment the list moves on, the name on
	 * screen is the wrong one and the view stops being read. */
	std::string m_sPreviewPanItem;
	bool IsPreviewPanValid() const;
	bool IsVideoPreviewPanSource() const;
	/* one line of everything that decides where the frame is drawn */
	/* A preview picture landed while the view still had no viewport, so whether
	 * the framing on screen is one worth keeping could not be told apart from
	 * one that is merely fitted.  Nothing is imposed until the view has been
	 * laid out, and the first layout decides: imposing the video's own zoom in
	 * the meantime is what dropped a zoom the user had just made on a still.
	 * The magnification it was decided at is kept with it, because the decision
	 * is only about the zoom: if the framing on screen has moved on since - the
	 * user zoomed or panned the preview - then it is theirs and re-deciding would
	 * take it away. */
	bool m_bPreviewFramingPending = false;
	double m_dPreviewFramingMag = 0.;
	void UpdateVideoPreviewViewAreaFromImageView();
	bool IsVideoPreviewShowing() const;
	void ResetVideoPan();
	QuiverImageViewMode GetViewMode() const;
	QuiverImageViewMode GetChosenViewMode() const;
	void SetVideoViewMode(QuiverImageViewMode mode);
	void ApplyVideoZoom();
	/* before the first frame the pipeline has no size to scale, so the zoom
	 * is applied to the quick preview instead and handed to the video when
	 * playback starts; borrowing/returning the image view keeps the image's
	 * own view mode and zoom untouched */
	void ApplyVideoPreviewZoom(gdouble zoom);
	void BorrowImageViewForPreviewZoom();
	void ReturnImageViewFromPreviewZoom();
	void RotateVideo(bool clockwise);
	int GetVideoUserRotation() const { return m_iVideoUserRotation; }

	// video pan inertia
	void StartVideoPanSlowdown();
	void StopVideoPanSlowdown();
	bool VideoPanSlowdownStep(gint64 frame_time_us);
	void RecordVideoPanSample(gdouble dx, gdouble dy, gdouble dt);
	bool CanVideoPan() const;
	static gboolean video_pan_slowdown_tick_cb(GtkWidget *widget, GdkFrameClock *frame_clock, gpointer data);
	static gboolean video_pan_slowdown_timeout_cb(gpointer data);


// member variables

	GtkWidget *m_pIconView;
	GtkWidget *m_pImageView;

	GtkWidget * m_pGrid;
	GtkWidget * m_pOverlay;
	GtkWidget * m_pStack;

	/* shown on top of the image view when the current image failed to load
	 * (otherwise the previous image's content would keep being displayed) */
	GtkWidget * m_pImageErrorLabel;
	GtkAdjustment * m_pAdjustmentH;
	GtkAdjustment * m_pAdjustmentV;

	GtkWidget * m_pScrollbarH;
	GtkWidget * m_pScrollbarV;

	GtkWidget *m_pHBox;
	GtkWidget *m_pVBox;
	
	gdouble m_dAdjustmentValueLastH;
	gdouble m_dAdjustmentValueLastV;
	
	GtkWidget *m_pNavControlPill;
	GtkWidget *m_pNavigationControl;

	GtkWidget* m_pMediaControls;
	GtkWidget* m_pTransportRow;   /* [rewind][play][ff] floating at the center of the video */
	GtkWidget* m_pPlayImage;
	GtkWidget* m_pPlayButton;
	GtkWidget* m_pTimeline;
	GtkWidget* m_pTimeElapsedLabel;
	GtkWidget* m_pTimeDurationLabel;
	GtkWidget* m_pPlayProgress;      /* GtkScale (seek slider) */
	gulong     m_iPlayProgressChangeHandler; /* signal handler ID for change-value */
	GtkWidget* m_pControlsBox;
	GtkWidget* m_pRewindBtn;
	GtkWidget* m_pFfBtn;
	GtkWidget* m_pSnapBtn;
	GtkWidget* m_pTimelineRow;
	GtkWidget* m_pVolumeButton;
	GtkWidget* m_pVolumePopover;
	GtkWidget* m_pVolumeScale;
	GtkWidget* m_pVolumeMuteBtn;
	double m_dVolume;
	bool m_bMuted;
	GtkWidget* m_pFullscreenBtn;
	GtkWidget* m_pVideoOptionsBtn;
	GtkWidget* m_pVideoOptionsPopover; // currently-open video-options popover (created on click)
	guint       m_iVideoZoomIdle;      // pending ApplyVideoZoom idle (0 = none)

	/* the timeline (time label, progress bar, volume button) stays hidden for
	 * a newly shown video until play is pressed; after that it is sticky so
	 * pausing or seeking never hides it - only a change of the current item
	 * resets it to hidden */
	bool m_bTimelineVisible;


	QuiverFile m_QuiverFileCurrent;

	int m_iCurrentOrientation;
	
	StatusbarPtr m_StatusbarPtr;

	IPixbufLoaderObserverPtr m_PixbufLoaderObserverPtr;
	ImageLoader m_ImageLoader;
	IImageListViewPtr m_ImageListPtr;

	Viewer *m_pViewer;
	
	guint m_iIdleSetIndex;
	guint m_iTimeoutScrollbars;
	guint m_iTimeoutUpdateListID;
	guint m_iTimeoutSlideshowID;
	guint m_iTimeoutClickID;
	guint m_iTimeoutMouseMotionNotify;
	guint m_iTimeoutPlayProgress;

	/* "Slideshow paused. [Resume]" pill, shown while a slideshow is paused
	 * (on an image or a video) so the user can both see the paused state and
	 * resume without hunting for a hotkey. */
	GtkWidget *m_pSlideShowPausedPill;

	int   m_iSlideShowDuration;
	int   m_iSlideShowWaitCount;
	bool  m_bSlideShowLoop;
	bool  m_bVideoLoop;

	bool m_bMaximizeViewableArea;
	bool m_bMaximizeViewabe;

	bool m_bIsPlaying;
	/* Set at the top of ~ViewerImpl: suppresses event emission (which would
	 * call m_pViewer->shared_from_this(), fatal during the Viewer's own
	 * destruction) once teardown has begun. */
	bool m_bShuttingDown;
	GtkWidget* m_pPlayAnimWidget;
	GtkWidget* m_pPlayAnimImage;
	guint      m_iPlayAnimTickId;
	gint64     m_iPlayAnimStartTime;
	void       TriggerPlayPauseAnimation(bool isPlaying);
	void       CancelPlayPauseAnimation();

	ImageCache m_ThumbnailCache;
	ImageCache m_FilmstripCache;

	double m_dPlaybackSpeed;
	GtkWidget* m_pSpeedButton;
	GtkWidget* m_pSpeedLabel;

	GtkDragSource* m_pDragSource = nullptr;
	GtkDropTarget* m_pDropTarget = nullptr;
	GtkWidget* m_pContextMenuPopover = nullptr;
	GtkWidget* m_pContextMenuTrashBtn = nullptr;
	GtkWidget* m_pContextMenuRestoreBtn = nullptr;
	GtkWidget* m_pContextMenuInfoLabel = nullptr;

	// gstreamer elements for playing videos
	GstElement* m_pPipeline = nullptr;
	GdkPaintable* m_pVideoPaintable = nullptr; // paintable exposed by the sink
	QuiverRotatedPaintable* m_pVideoRotatedPaintable = nullptr; // paintable with rotation applied
	int         m_iVideoUserRotation = 0;      // 0, 90, 180, 270
	GtkWidget*  m_pVideoSinkWidget = nullptr; // GtkPicture wrapping the paintable
	GtkWidget*  m_pVideoFixed = nullptr;      // GtkFixed canvas (fills the viewer area, clips the video sink widget)
	// how the digital zoom crop+scale chain is implemented, chosen at build time
	// from the GPU acceleration actually available on the platform
	typedef enum
	{
		VIDEO_ZOOM_SOFTWARE = 0, // videocrop + videoscale (CPU)
		VIDEO_ZOOM_MEDIA_SDK,    // Intel Media SDK: videocrop + vapostproc
		VIDEO_ZOOM_VAAPI,        // Intel gstreamer-vaapi: native crop-* props
		VIDEO_ZOOM_NVIDIA,       // NVIDIA: nvvidconv coordinate-based crop props
		VIDEO_ZOOM_GL
	} VideoZoomType;
	VideoZoomType m_VideoZoomType = VIDEO_ZOOM_GL;
	GstElement* m_pVideoZoomInput = nullptr;  // input capsfilter: permissive caps so playbin template check passes with HW decoders
	GstElement* m_pVideoCrop = nullptr;       // in-pipeline crop element for digital zoom (software path only)
	GstElement* m_pVideoZoomConvert = nullptr; // normalizes formats for the scaler
	GstElement* m_pVideoZoomScaler = nullptr; // scaler (HW-accelerated if available, else videoscale)
	GstElement* m_pVideoZoomCaps = nullptr;   // capsfilter forcing the scaled output frame size
	GstElement* m_pVideoZoomInputCaps = nullptr; // widens the chain's sink template to the decoder's memory type
	gdouble     m_dVideoZoom;       // current video zoom factor (1.0 = actual size, like the image view)
	gdouble     m_dVideoZoomFinal;  // target video zoom factor for the smooth animation
	gdouble     m_dVideoZoomMin;    // lowest zoom allowed: the fit level seen so far (1.0 when actual size)
	/* The video's copy of the viewer's view mode, kept in step with the image
	 * view's by GetViewMode()/SetVideoViewMode(): the video's own zoom and pan
	 * live beside the image view's, but the mode itself is the viewer's, so
	 * "keep zoom and pan" means the same thing for a photo and a video. */
	QuiverImageViewMode m_eVideoViewMode = QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW_STRETCH;
	/* TRUE while a zoom chosen on the not-yet-playing video is held for the
	 * first ApplyVideoZoom, which then bounds it by the real fit level */
	gboolean    m_bVideoZoomFromPreview = false;
	/* TRUE while the visible region the quick preview was left at is held as
	 * the pan for the first ApplyVideoZoom, so playback starts there */
	gboolean    m_bVideoPanFromPreview = false;
	/* the image view's magnification and center, taken before the preview zoom
	 * borrowed it (the view mode is the viewer's own, so it is not part of
	 * this: it is the live one that is put back, not a snapshot) */
	gboolean    m_bPreviewZoomBorrowed = false;
	gdouble     m_dPreviewZoomSavedMag = 1.0;
	gdouble     m_dPreviewZoomSavedCenterX = 0.0;
	gdouble     m_dPreviewZoomSavedCenterY = 0.0;
	gboolean    m_bPreviewZoomSavedCenter = false;
	guint       m_iVideoZoomTimeoutID; // timer driving the smooth video zoom animation
	guint       m_iGstBusWatchID = 0;  // watch ID for GStreamer bus messages
	/* Where the *centre* of the visible part of the frame sits, as a fraction
	 * (0..1) of the frame itself on each axis: 0.5 is the middle, and one
	 * value per axis describes the same point whatever the frame's aspect
	 * ratio is.
	 *
	 * The centre, and not the offset of the region's left edge, is what carries
	 * over between items.  An offset as a fraction would have to be re-normalized
	 * against the new frame's own scroll range, and since that range depends on
	 * the frame's aspect ratio and on the zoom, the same stored value lands on
	 * a different point of the picture: switching from a 16:9 to a 4:3 file
	 * would slide the view off the point the user had centred.  The centre is
	 * scale- and aspect-free, so it frames the same part of any frame.  The
	 * pixel pan below is derived from it on every layout. */
	gdouble     m_fVideoPanFX;
	gdouble     m_fVideoPanFY;
	/* the size of the visible part of the frame in display px, from the last
	 * layout: it turns a pixel pan into the centre that is kept, and back */
	gdouble     m_dVideoViewW;
	gdouble     m_dVideoViewH;
	/* the pixel range the visible part can be offset over, from the last
	 * layout: the part of the frame that is scrolled off, i.e. frame minus the
	 * visible part */
	gdouble     m_dVideoPanRangeX;
	gdouble     m_dVideoPanRangeY;
	gdouble     m_dVideoPanX;       // viewport (visible part of the frame) left edge, in display px
	gdouble     m_dVideoPanY;       // viewport top edge, in display px
	/* the frame the last apply sized the picture for, and the zoom factor it
	 * used: anchoring the zoom keeps the display point under the pointer where
	 * it is, but a change of *frame* is not a zoom - it keeps the centre */
	gdouble     m_dVideoLastFrameW;   // frame width the last applied zoom was for
	gdouble     m_dVideoLastFrameH;   // frame height the last applied zoom was for
	gdouble     m_dVideoLastFrameZoom; // zoom factor the last apply used
	gdouble     m_dVideoLastWidgetW; // last applied video widget width (for zoom anchoring)
	gdouble     m_dVideoLastWidgetH; // last applied video widget height
	gdouble     m_dVideoLastZc;      // last applied pipeline crop factor
	gboolean    m_bVideoZoomCropActive; // zoomcaps is forcing the scaled output size
	gboolean    m_bVideoZoomInputCropActive; // zoominputcaps is forcing system-memory input for the crop
	gboolean    m_bVideoPanning;    // left-button pan drag in progress
	guint       m_uiVideoPanSlowdownTickID = 0;
	guint       m_uiVideoPanSlowdownTimeoutID = 0;
	gint64      m_iVideoPanSlowdownLastTime = 0;
	gdouble     m_dVideoPanVelX = 0.;
	gdouble     m_dVideoPanVelY = 0.;
	gint64      m_iVideoPanLastMotionTime = 0;
	gdouble     m_dVideoPanLastMotionX = 0.;
	gdouble     m_dVideoPanLastMotionY = 0.;
	struct VideoPanSample {
		gdouble dx = 0.;
		gdouble dy = 0.;
		gdouble dt = 0.;
	};
	static constexpr int VIDEO_PAN_MAX_SAMPLES = 3;
	VideoPanSample m_aVideoPanSamples[VIDEO_PAN_MAX_SAMPLES]{};
	int         m_iVideoPanSampleCount = 0;
	bool        m_bVideoPanSlowdownActive = false;
	bool        m_bVideoPanSlowdownInterrupted = false;
	bool        m_bKineticScrolling = true;
	gboolean    m_bVideoNeedsFirstFrame; // TRUE after switching videos: defer opacity restore until new frame is decoded
	gboolean    m_bVideoFlushPending; // TRUE between the flushing seek and the next ASYNC_DONE
	bool        m_bVideoPagePending; // TRUE: keep the image/preview page visible until the video's first frame is ready
	bool        m_bVideoPlaybackStarted; // TRUE once playback has been triggered for the current video
	gdouble     m_dVideoPanStartRootX; // pan drag start point, root (screen) coords
	gdouble     m_dVideoPanStartRootY;
	gdouble     m_dVideoPanStartPX; // crop window left/top at drag start
	gdouble     m_dVideoPanStartPY;
	gint        m_iVideoWidth;      // current video frame width (from caps probe)
	gint        m_iVideoHeight;     // current video frame height
	gint        m_iVideoFpsNum;
	gint        m_iVideoFpsDen;
	gint        m_iVideoParN;
	gint        m_iVideoParD;
	// viewport rectangle of the *main* view, normalized against the frame, so
	// the miniature can mark which part of it is on screen
	gdouble     m_dVideoPreviewViewX;
	gdouble     m_dVideoPreviewViewY;
	gdouble     m_dVideoPreviewViewW;
	gdouble     m_dVideoPreviewViewH;
	gdouble     m_dVideoPreviewDispW;   // displayed size of the visible viewport, for drag scaling
	gdouble     m_dVideoPreviewDispH;
	gboolean    m_bVideoPreviewHasFrame; // the miniature sink has uploaded a real frame
	gboolean    m_bVideoPreviewRedrawQueued; // a miniature redraw is already pending on the main thread
	guint       m_iVideoPreviewRedrawIdle = 0; // the pending redraw above, so teardown can drop it
	gint        m_iVideoSinkW;      // last set_size_request width  (skip if unchanged)
	gint        m_iVideoSinkH;      // last set_size_request height
	gint        m_iVideoSinkX;      // last layout_move x
	gint        m_iVideoSinkY;      // last layout_move y (from caps probe)
	gfloat      m_fVideoZoomSx = -999.0f; // cached gltransformation properties
	gfloat      m_fVideoZoomSy = -999.0f;
	gfloat      m_fVideoZoomTx = -999.0f;
	gfloat      m_fVideoZoomTy = -999.0f;

	struct VideoGlitchTracker
	{
		const char          *m_pszName = "probe";
		std::atomic<guint64> m_uFrameIndex{0};
		std::atomic<guint64> m_uLastGlitchFrame{0};
		std::atomic<gint64>  m_iLastGlitchPts{-1};
		std::atomic<guint>   m_uGlitchCount{0};
		std::atomic<double>  m_dLastR64{1.0};
		std::atomic<double>  m_dLastR16{1.0};
		std::atomic<double>  m_dLastMAE{0.0};
		std::atomic<double>  m_dLastFlicker{0.0};
		std::atomic<bool>    m_bLastWasGlitch{false};

		double               m_dRollingMAE = 5.0;
		GstVideoInfo         m_VideoInfo{};
		bool                 m_bHaveVideoInfo = false;

		struct FrameItem
		{
			guint64 frame_idx{0};
			GstClockTime pts{GST_CLOCK_TIME_NONE};
			std::vector<uint8_t> grid;
			float matrix[16]{};
			bool has_matrix{false};
		};
		std::vector<FrameItem> m_vHistory;
		GstClockTime         m_uLastPts{GST_CLOCK_TIME_NONE};

		std::vector<uint8_t> m_vPrevRGB;
		guint64              m_uPrevRGBFrameIdx{0};
		int                  m_iPrevRGBW{0};
		int                  m_iPrevRGBH{0};

		std::vector<uint8_t> m_vPrevRGB2;
		guint64              m_uPrevRGB2FrameIdx{0};
		int                  m_iPrevRGB2W{0};
		int                  m_iPrevRGB2H{0};

		std::thread          m_SaveThread;

		~VideoGlitchTracker()
		{
			if (m_SaveThread.joinable())
				m_SaveThread.join();
		}

		void Reset()
		{
			if (m_SaveThread.joinable())
				m_SaveThread.join();
			m_uFrameIndex.store(0, std::memory_order_relaxed);
			m_uLastGlitchFrame.store(0, std::memory_order_relaxed);
			m_iLastGlitchPts.store(-1, std::memory_order_relaxed);
			m_uGlitchCount.store(0, std::memory_order_relaxed);
			m_dLastR64.store(1.0, std::memory_order_relaxed);
			m_dLastR16.store(1.0, std::memory_order_relaxed);
			m_dLastMAE.store(0.0, std::memory_order_relaxed);
			m_dLastFlicker.store(0.0, std::memory_order_relaxed);
			m_bLastWasGlitch.store(false, std::memory_order_relaxed);
			m_dRollingMAE = 5.0;
			m_vHistory.clear();
			m_uLastPts = GST_CLOCK_TIME_NONE;
			m_vPrevRGB.clear();
			m_uPrevRGBFrameIdx = 0;
			m_iPrevRGBW = 0;
			m_iPrevRGBH = 0;
			m_vPrevRGB2.clear();
			m_uPrevRGB2FrameIdx = 0;
			m_iPrevRGB2W = 0;
			m_iPrevRGB2H = 0;
			m_bHaveVideoInfo = false;
		}
	};

	VideoGlitchTracker   m_DecoderGlitchTracker;
	VideoGlitchTracker   m_SinkGlitchTracker;
	std::atomic<gint64>  m_iLastZoomApplyTimeUs{0};
	std::atomic<guint64> m_uZoomApplyCount{0};

	/* filmstrip overlay mode */
	bool        m_bFilmstripOverlay;   // true when filmstrip floats over the image
	bool        m_bHideFilmstripFS;    // true when filmstrip should hide in fullscreen
	bool        m_bFilmstripHiddenByFS; // true while filmstrip is hidden due to fullscreen
	GtkWidget*  m_pFilmstripEdge;      // invisible event box along the edge for hover-reveal
	GtkWidget*  m_pFilmstripOverlayContainer; // the GtkOverlay or alignment that holds the filmstrip in overlay mode
	guint       m_iTimeoutFilmstripHide; // auto-hide timer ID (0 = not running)
	guint       m_iTimeoutFilmstripFade; // fade animation timer ID (0 = not running)
	bool        m_bFadingIn;            // true = fading in, false = fading out
	double      m_dFadeOpacity;         // current opacity during fade

	/* media controls fade */
	guint       m_iTimeoutControlsFade;
	bool        m_bControlsFadingIn;
	double      m_dControlsFadeOpacity;

	/* navigation control fade: it only exists while the image is larger than
	 * the viewport, so it animates in/out instead of popping */
	guint       m_iTimeoutNavControlFade = 0; // fade animation timer ID (0 = not running)
	bool        m_bNavControlFadingIn    = false; // true = fading in, false = fading out
	double      m_dNavControlFadeOpacity = 0.0;  // current opacity during fade
	bool        m_bNavControlShown       = false; // last applied show state (transition edge)

	gboolean    m_bSeekDragging;

	/* last pointer position in surface coords (to ignore synthetic motion
	 * events caused by the controls re-laying-out under a parked pointer) */
	double      m_dPointerRootX;
	double      m_dPointerRootY;
	bool        m_bPointerPosValid;

	/* 1x1 transparent cursor, lazily created, used to hide the pointer when
	 * it sits idly over a playing video (in step with the controls' auto-hide) */
	GdkCursor*  m_pBlankCursor;

	typedef enum _SlideShowState
	{
		SLIDESHOW_STATE_ADVANCE,
		SLIDESHOW_STATE_CACHE,
		SLIDESHOW_STATE_PLAY_VIDEO,
		SLIDESHOW_STATE_PLAYING_VIDEO,
		SLIDESHOW_STATE_PAUSED
	} SlideShowState;

	SlideShowState m_SlideShowState;
	SlideShowState m_SlideShowPrePauseState;
	bool  m_bSlideShowRunning;
	bool  m_bSlideShowPaused;
	/* True only while the slideshow's own state machine is calling
	 * SetImageIndex (timeout_advance_slideshow).  Lets SetImageIndex tell a
	 * machine-driven advance apart from user navigation. */
	bool  m_bSlideShowMachineAdvancing;

	// Viewer Control Overlays (floating HUD controls)
	GtkWidget* m_pViewerOverlayBar;
	bool       m_bPointerOverOverlayBar;
	bool       m_bControlsVisible;
	bool       m_bVideoZoomAnchorCenter;
	GtkWidget* m_pViewerPrevBtn;
	GtkWidget* m_pViewerNextBtn;
	GtkWidget* m_pViewerSlideshowBtn;
	GtkWidget* m_pViewerVideoSlideshowBtn;
	GtkWidget* m_pImageBlank1;
	GtkWidget* m_pImageBlank2;
	GtkWidget* m_pVideoBlank1;
	GtkWidget* m_pVideoBlank2;
	GtkWidget* m_pViewerZoomOutBtn;
	GtkWidget* m_pViewerZoomFitBtn;
	/* the arrow half of the split fit/zoom-fit button */
	GtkWidget* m_pViewModeSplitBox;
	GtkWidget* m_pViewModeSplitSep;
	GtkWidget* m_pViewModeMenuBtn;
	GtkWidget* m_pViewModeMenuPopover;
	GtkWidget* m_pViewerZoomInBtn;
	/* thin separators that group the HUD's button clusters */
	GtkWidget* m_pSepNavigation;
	GtkWidget* m_pSepRotate;
	GtkWidget* m_pSepZoom;
	GtkWidget* m_pViewerRotateCcwBtn;
	GtkWidget* m_pViewerRotateCwBtn;
	GtkWidget* m_pViewerFlipHBtn;
	GtkWidget* m_pViewerFlipVBtn;
	GtkWidget* m_pImageSubmenuBtn;
	GtkWidget* m_pImageSubmenuPopover;
	GtkWidget* m_pViewerFullscreenBtn;

	// Gesture tracking
	double m_dGestureLastScale;
	double m_dTwoFingerPanStartHAdj;
	double m_dTwoFingerPanStartVAdj;
	double m_dTwoFingerPanStartVidX;
	double m_dTwoFingerPanStartVidY;
	double m_dTwoFingerPanLastOffsetX = 0.0;
	double m_dTwoFingerPanLastOffsetY = 0.0;
	gint64 m_iTwoFingerPanLastTime = 0;

	void UpdateSlideshowButton();
	void UpdateHUDTooltips();
	void UpdateHUDPosition();
	void UpdateNavigationControl();
	bool UpdateNavigationControlTexture();
	void UpdateNavControlPosition();
	void UpdateNavControlVisibility();
	void StartNavControlFade(bool fadeIn);
	void CancelNavControlFade();
	void ApplyNavControlMinSize(bool bApply);
	bool IsNavControlNeeded() const;
	bool IsNavPreviewEnabled() const;
	void ConfigureVideoPreviewScale(gint dispW, gint dispH);
	void ResetVideoPreviewViewState();
	void UpdateVideoPreviewViewArea();
	void ReleaseVideoPreview();
	GstElement* BuildVideoZoomBin();
	void RebuildVideoZoomBin();
	bool IsSlideShowRunning() const { return m_bSlideShowRunning; }
	bool IsSlideShowPaused() const { return m_bSlideShowPaused; }
	void SlideShowPause();
	void SlideShowResume();
	void SlideShowTogglePause();

	/* Manual-navigation handling: when the user moves the current item
	 * mid-show (arrow keys, scroll wheel, filmstrip, etc.), pause the show
	 * so the item they selected is not skipped past by the machine's stale
	 * timer.  A manual resume (click, hotkey, or the paused pill's Resume
	 * button) restarts the show on the current item. */
	void HandleSlideShowManualNavigation();
	/* "Slideshow paused. [Resume]" pill helpers. */
	void ShowSlideShowPausedPill();
	void HideSlideShowPausedPill();

	bool        m_bVideoPreviewClick = false;
	gdouble     m_dVideoPreviewClickX = 0.0;
	gdouble     m_dVideoPreviewClickY = 0.0;
	GtkWidget*  m_pCenterPlayBtn = nullptr;
	void        UpdateCenterPlayButtonVisibility();

	bool IsFilmstripOverlay() const { return m_bFilmstripOverlay; }
	bool IsHideFilmstripFS() const { return m_bHideFilmstripFS; }
	bool IsPointerOverFilmstrip() const;
	bool IsFilmstripShowing() const;
	bool IsPointerOverMediaControls() const;
	bool IsPointerOverControls() const;
	bool IsPointOverControlsOrFilmstrip(GtkWidget *event_widget, double x, double y) const;
	void ShowFilmstripOverlay();
	void HideFilmstripOverlay();
	void UpdateFilmstripForPlayback();
	void ScheduleFilmstripHide();
	void CancelFilmstripHide();
	void CancelFilmstripFade();
	void StartFilmstripFade(bool fadeIn);
	void CancelControlsFade();
	void StartControlsFade(bool fadeIn);

/* nested classes */
	//class ViewerEventHandler;
	class ImageListEventHandler : public IImageListEventHandler
	{
	public:
		ImageListEventHandler(Viewer::ViewerImpl* parent){this->parent = parent;};
		virtual void HandleContentsChanged(ImageListEventPtr event);
		virtual void HandleCurrentIndexChanged(ImageListEventPtr event) ;
		virtual void HandleItemAdded(ImageListEventPtr event);
		virtual void HandleItemRemoved(ImageListEventPtr event);
		virtual void HandleItemChanged(ImageListEventPtr event);
	private:
		Viewer::ViewerImpl *parent;
	};

	class PreferencesEventHandler : public IPreferencesEventHandler
	{
	public:
		PreferencesEventHandler(ViewerImpl* parent) {this->parent = parent;};
		virtual void HandlePreferenceChanged(PreferencesEventPtr event);
	private:
		ViewerImpl* parent;
	};
	
	class ViewerThumbLoader : public IconViewThumbLoader
	{
	public:
		ViewerThumbLoader(ViewerImpl* pViewerImpl, guint iNumThreads, std::shared_ptr<bool> spAlive)  :
			IconViewThumbLoader(iNumThreads, false),
			m_pViewerImpl(pViewerImpl),
			m_spAlive(spAlive)
		{
			m_bMapped.store(false, std::memory_order_relaxed);
			m_uiThumbWidth.store(96, std::memory_order_relaxed);
			m_uiThumbHeight.store(96, std::memory_order_relaxed);
			Start();
		}
		
		~ViewerThumbLoader(){}

		void SetIconDimensions(guint uiWidth, guint uiHeight)
		{
			m_uiThumbWidth.store(uiWidth, std::memory_order_relaxed);
			m_uiThumbHeight.store(uiHeight, std::memory_order_relaxed);
		}

		void SetMapped(bool bMapped)
		{
			m_bMapped.store(bMapped, std::memory_order_relaxed);
		}
		
	protected:
		
		virtual void LoadThumbnail(const ThumbLoaderItem &item, guint uiWidth, guint uiHeight);
		virtual QuiverFile GetQuiverFile(gulong index);
		virtual void GetVisibleRange(gulong* pulStart, gulong* pulEnd);
		virtual void GetIconSize(guint* puiWidth, guint* puiHeight);
		virtual gulong GetNumItems();
		virtual void SetIsRunning(bool bIsRunning);
		virtual void SetCacheSize(guint uiCacheSize);

		/* Ask the icon view to repaint one cell on the main thread. */
		void InvalidateCell(gulong index);
	
		
	private:
		ViewerImpl* m_pViewerImpl; 
		std::shared_ptr<bool> m_spAlive;
		std::atomic<bool> m_bMapped;
		std::atomic<guint> m_uiThumbWidth;
		std::atomic<guint> m_uiThumbHeight;
	};

	IPreferencesEventHandlerPtr  m_PreferencesEventHandlerPtr;
	IImageListEventHandlerPtr    m_ImageListEventHandlerPtr;
	std::shared_ptr<bool>        m_spAlive;
	ViewerThumbLoader            m_ThumbnailLoader;

};

void Viewer::Rename()
{
	m_ViewerImplPtr->Rename();
}

void Viewer::ViewerImpl::Rename()
{
	if (0 == m_ImageListPtr->GetSize())
		return;

	/* The prompt and the rename itself live in QuiverUtils so the browser
	 * and the viewer cannot drift apart. */
	char* new_uri = QuiverUtils::PromptAndRenameFile(m_ImageListPtr->GetCurrent());
	if (NULL == new_uri)
		return;

	/* Point the list at the new file without leaving the current position. */
	ImageList* real = dynamic_cast<ImageList*>(m_ImageListPtr.get());
	if (NULL != real)
	{
		real->Reload();
		real->SetCurrentFile(new_uri);
		/* Re-seat the view on the renamed item: SetCurrentFile only fires a
		 * change event when the rename also shifted the item's index, so
		 * drive the filmstrip/icon view directly to stay on the item under
		 * its new name. */
		guint idx = real->GetCurrentIndex();
		SetImageIndex(idx, true);
		quiver_icon_view_set_cursor_cell(QUIVER_ICON_VIEW(m_pIconView), idx);
	}
	g_free(new_uri);
}

void Viewer::ViewerImpl::SetImageList(IImageListViewPtr imgList)
{
	m_ImageListPtr->RemoveEventHandler(m_ImageListEventHandlerPtr);
	
	m_ImageListPtr = imgList;
	
	m_ImageListPtr->AddEventHandler(m_ImageListEventHandlerPtr);
	
	UpdateUI();
}
// has image


void Viewer::ViewerImpl::UpdateUI()
{
	if (m_pImageView == NULL || !G_IS_OBJECT(m_pImageView) || !QUIVER_IS_IMAGE_VIEW(m_pImageView))
	{
		return;
	}
	if (m_ImageListPtr->GetSize())
	{
		if ( 0 == m_ImageListPtr->GetCurrentIndex() && m_ImageListPtr->GetCurrentIndex() == m_ImageListPtr->GetSize() - 1 )
		{
			// disable both
			QuiverUtils::SetActionsSensitive(pszActionsNext, G_N_ELEMENTS(pszActionsNext), FALSE);
			QuiverUtils::SetActionsSensitive(pszActionsPrev, G_N_ELEMENTS(pszActionsPrev), FALSE);
		}
		else if (m_ImageListPtr->GetCurrentIndex() == m_ImageListPtr->GetSize() - 1 )
		{
			// disable next
			QuiverUtils::SetActionsSensitive(pszActionsNext, G_N_ELEMENTS(pszActionsNext), FALSE);

			// enable previous
			QuiverUtils::SetActionsSensitive(pszActionsPrev, G_N_ELEMENTS(pszActionsPrev), TRUE);
		}
		else if ( 0 == m_ImageListPtr->GetCurrentIndex() ) 
		{
			// disable previous
			QuiverUtils::SetActionsSensitive(pszActionsPrev, G_N_ELEMENTS(pszActionsPrev), FALSE);

			// enable next
			QuiverUtils::SetActionsSensitive(pszActionsNext, G_N_ELEMENTS(pszActionsNext), TRUE);

		}
		else
		{
			// enable both
			QuiverUtils::SetActionsSensitive(pszActionsPrev, G_N_ELEMENTS(pszActionsPrev), TRUE);
			QuiverUtils::SetActionsSensitive(pszActionsNext, G_N_ELEMENTS(pszActionsNext), TRUE);
		}
		QuiverUtils::SetActionsSensitive(pszActionsImage, G_N_ELEMENTS(pszActionsImage), TRUE);
	}
	else
	{
		// disable both
		QuiverUtils::SetActionsSensitive(pszActionsPrev, G_N_ELEMENTS(pszActionsPrev), FALSE);
		QuiverUtils::SetActionsSensitive(pszActionsNext, G_N_ELEMENTS(pszActionsNext), FALSE);
		QuiverUtils::SetActionsSensitive(pszActionsImage, G_N_ELEMENTS(pszActionsImage), FALSE);
		
	}
	QuiverUtils::SetActionsSensitive(pszActionsVideo, G_N_ELEMENTS(pszActionsVideo), IsVideo());

	/* The play/pause keys (space/k/P) must also work while a slideshow is
	 * running on an image, so the show can be paused/resumed from the
	 * keyboard without first navigating back to a video. */
	{
		const char *pszPlayPauseActions[] = {
			ACTION_VIEWER_VIDEO_PLAY,
			ACTION_VIEWER_VIDEO_PLAY_2,
		};
		QuiverUtils::SetActionsSensitive(pszPlayPauseActions, G_N_ELEMENTS(pszPlayPauseActions),
			IsVideo() || m_bSlideShowRunning);
	}
	
	{
		/* For videos the zoom factor is applied in the pipeline (from the fit
		 * level up to 8x of the actual size); for stills it comes from the
		 * image view's magnification. */
		gboolean bCanZoomIn = FALSE;
		gboolean bCanZoomOut = FALSE;
		if (m_ImageListPtr && m_ImageListPtr->GetSize() > 0)
		{
			if (IsVideo())
			{
				bCanZoomIn = (m_dVideoZoomFinal < 16.0);
				bCanZoomOut = (GetViewMode() != QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW
				               && GetViewMode() != QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW_STRETCH
				               && (m_dVideoZoomFinal > m_dVideoZoomMin + 0.005));
			}
			else
			{
				bCanZoomIn = quiver_image_view_can_magnify(QUIVER_IMAGE_VIEW(m_pImageView), TRUE);
				bCanZoomOut = quiver_image_view_can_magnify(QUIVER_IMAGE_VIEW(m_pImageView), FALSE);
			}
		}

		gboolean bCanZoomFit = FALSE;
		if (m_ImageListPtr && m_ImageListPtr->GetSize() > 0)
		{
			if (IsVideo())
			{
				bCanZoomFit = (GetViewMode() != QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW
				               && GetViewMode() != QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW_STRETCH)
				              || m_dVideoZoomFinal > m_dVideoZoomMin + 0.005;
			}
			else
			{
				bCanZoomFit = (quiver_image_view_get_view_mode(QUIVER_IMAGE_VIEW(m_pImageView)) != QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW_STRETCH);
			}
		}

		GAction* action;
		action = QuiverUtils::GetAction(ACTION_VIEWER_ZOOM_IN);
		if (NULL != action)
			g_simple_action_set_enabled(G_SIMPLE_ACTION(action), bCanZoomIn);
		action = QuiverUtils::GetAction(ACTION_VIEWER_ZOOM_OUT);
		if (NULL != action)
			g_simple_action_set_enabled(G_SIMPLE_ACTION(action), bCanZoomOut);
		/* The view-mode popover reuses this action, so it must stay enabled
		 * while an image is loaded: the whole point of opening the menu is to
		 * pick a mode, and the mode it is in is very often the one the fit
		 * button would pick.  bCanZoomFit only drives the button, which is
		 * legitimately insensitive when the view already fits. */
		gboolean bHasItems = (m_ImageListPtr && m_ImageListPtr->GetSize() > 0);
		action = QuiverUtils::GetAction(ACTION_VIEWER_ZOOM_FIT);
		if (NULL != action)
			g_simple_action_set_enabled(G_SIMPLE_ACTION(action), bHasItems);
		gboolean bCanPrev = FALSE;
		gboolean bCanNext = FALSE;
		if (bHasItems)
		{
			bCanPrev = (m_ImageListPtr->GetCurrentIndex() > 0);
			bCanNext = (m_ImageListPtr->GetCurrentIndex() < m_ImageListPtr->GetSize() - 1);
		}
		gboolean bCanSlideshow = m_bSlideShowRunning || (m_ImageListPtr && m_ImageListPtr->GetSize() >= 2);
		gboolean bCanRotate = bHasItems && !IsVideo();

		bool bIsVid = IsVideo();
		UpdateTimelineVisibility();
		UpdateCenterPlayButtonVisibility();
		UpdateSlideshowButton();

		if (m_pImageBlank1)
			gtk_widget_set_visible(m_pImageBlank1, !bIsVid);
		if (m_pViewerRotateCcwBtn)
			gtk_widget_set_visible(m_pViewerRotateCcwBtn, !bIsVid);
		if (m_pViewerRotateCwBtn)
			gtk_widget_set_visible(m_pViewerRotateCwBtn, !bIsVid);
		if (m_pImageBlank2)
			gtk_widget_set_visible(m_pImageBlank2, !bIsVid);
		if (m_pImageSubmenuBtn)
			gtk_widget_set_visible(m_pImageSubmenuBtn, !bIsVid);
		if (m_pViewModeSplitBox)
			/* the split fit/mode control is part of the HUD in both image and
			 * video mode: it lives in its own slot, so hiding it in one mode
			 * and showing it in the other would move every button after it */
			gtk_widget_set_visible(m_pViewModeSplitBox, TRUE);

		if (m_pPlayButton)
			gtk_widget_set_visible(m_pPlayButton, bIsVid);
		if (m_pRewindBtn)
			gtk_widget_set_visible(m_pRewindBtn, bIsVid);
		if (m_pFfBtn)
			gtk_widget_set_visible(m_pFfBtn, bIsVid);
		if (m_pVolumeButton)
			gtk_widget_set_visible(m_pVolumeButton, bIsVid);
		if (m_pVideoOptionsBtn)
			gtk_widget_set_visible(m_pVideoOptionsBtn, bIsVid);

		if (m_pViewerPrevBtn)
			gtk_widget_set_sensitive(m_pViewerPrevBtn, bCanPrev);
		if (m_pViewerNextBtn)
			gtk_widget_set_sensitive(m_pViewerNextBtn, bCanNext);
		if (m_pViewerSlideshowBtn)
			gtk_widget_set_sensitive(m_pViewerSlideshowBtn, bCanSlideshow);
		if (m_pViewerVideoSlideshowBtn)
			gtk_widget_set_sensitive(m_pViewerVideoSlideshowBtn, bCanSlideshow);
		if (m_pViewerZoomInBtn)
			gtk_widget_set_sensitive(m_pViewerZoomInBtn, bCanZoomIn);
		if (m_pViewerZoomOutBtn)
			gtk_widget_set_sensitive(m_pViewerZoomOutBtn, bCanZoomOut);
		if (m_pViewerZoomFitBtn)
			gtk_widget_set_sensitive(m_pViewerZoomFitBtn, bCanZoomFit);
		if (m_pViewModeMenuBtn)
			/* always available while an image is loaded: picking a mode is
			 * precisely what the user does when the current one is already
			 * the default fit, so this must not follow bCanZoomFit */
			gtk_widget_set_sensitive(m_pViewModeMenuBtn, bHasItems);
		if (m_pViewerRotateCcwBtn)
			gtk_widget_set_sensitive(m_pViewerRotateCcwBtn, bCanRotate);
		if (m_pViewerRotateCwBtn)
			gtk_widget_set_sensitive(m_pViewerRotateCwBtn, bCanRotate);
		if (m_pImageSubmenuBtn)
			gtk_widget_set_sensitive(m_pImageSubmenuBtn, bHasItems);
		if (m_pPlayButton)
			gtk_widget_set_sensitive(m_pPlayButton, bIsVid);
		if (m_pFfBtn)
			gtk_widget_set_sensitive(m_pFfBtn, bIsVid);
		if (m_pRewindBtn)
			gtk_widget_set_sensitive(m_pRewindBtn, bIsVid);
		if (m_pSnapBtn)
			gtk_widget_set_sensitive(m_pSnapBtn, bIsVid);
		if (m_pVolumeButton)
			gtk_widget_set_sensitive(m_pVolumeButton, bIsVid);
		if (m_pVideoOptionsBtn)
			gtk_widget_set_sensitive(m_pVideoOptionsBtn, bIsVid);

		if (m_pViewerFullscreenBtn && m_pOverlay)
		{
			GtkWidget *root = GTK_WIDGET(gtk_widget_get_root(m_pOverlay));
			bool bFS = root && GTK_IS_WINDOW(root) && gtk_window_is_fullscreen(GTK_WINDOW(root));
			gtk_button_set_icon_name(GTK_BUTTON(m_pViewerFullscreenBtn),
				get_fullscreen_icon_name(bFS));
			std::string fs_tip = ShortcutManager::GetInstance().GetTooltipForAction("FullScreen", bFS ? "Exit Fullscreen" : "Fullscreen");
			if (bFS)
				fs_tip += " / Middle Click / Esc";
			else
				fs_tip += " / Middle Click";
			gtk_widget_set_tooltip_text(m_pViewerFullscreenBtn, fs_tip.c_str());
		}
	}

	PreferencesPtr prefsPtr = Preferences::GetInstance();

	if (m_bSlideShowRunning)
	{
		bool bMaximize = prefsPtr->GetBoolean(QUIVER_PREFS_SLIDESHOW, QUIVER_PREFS_SLIDESHOW_ROTATE_FOR_BEST_FIT, false);
		QuiverUtils::ToggleActionSetActive(ACTION_VIEWER_ROTATE_FOR_BEST_FIT, bMaximize ? TRUE : FALSE);

		bool bHideFilmStrip = prefsPtr->GetBoolean(QUIVER_PREFS_SLIDESHOW, QUIVER_PREFS_SLIDESHOW_FILMSTRIP_HIDE, true);

		// hide film strip if necessary
		QuiverUtils::ToggleActionSetActive(ACTION_VIEWER_VIEW_FILM_STRIP, bHideFilmStrip ? FALSE : TRUE);


	}
	else
	{
		bool bMaximize = prefsPtr->GetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_ROTATE_FOR_BEST_FIT, false);
		QuiverUtils::ToggleActionSetActive(ACTION_VIEWER_ROTATE_FOR_BEST_FIT, bMaximize ? TRUE : FALSE);

		bool bShowFilmStrip = prefsPtr->GetBoolean(QUIVER_PREFS_VIEWER,QUIVER_PREFS_VIEWER_FILMSTRIP_SHOW);
		bool bFS = false;
		if (m_pOverlay)
		{
			GtkWidget *root = GTK_WIDGET(gtk_widget_get_root(m_pOverlay));
			bFS = root && GTK_IS_WINDOW(root) && gtk_window_is_fullscreen(GTK_WINDOW(root));
		}
		if (bFS && m_bHideFilmstripFS)
		{
			bShowFilmStrip = false;
		}
		QuiverUtils::ToggleActionSetActive(ACTION_VIEWER_VIEW_FILM_STRIP, bShowFilmStrip ? TRUE : FALSE);
	}
}

void Viewer::ViewerImpl::UpdateScrollbars()
{
	gint width, height;
	/* borrowed: the image view goes with the widget tree, and the scrollbar
	 * update is a timeout that can outlive it */
	if (m_pImageView == NULL || !QUIVER_IS_IMAGE_VIEW(m_pImageView))
		return;
	QuiverImageViewMode view_mode = quiver_image_view_get_view_mode(QUIVER_IMAGE_VIEW(m_pImageView));
	
	quiver_image_view_get_pixbuf_display_size_for_mode(
		QUIVER_IMAGE_VIEW(m_pImageView),
		view_mode,
		&width,
		&height);
		
	GtkWidget *pScrollbarH = m_pScrollbarH;
	GtkWidget *pScrollbarV = m_pScrollbarV;

	if (NULL == m_pScrollbarV)
		return;
	if (NULL == m_pScrollbarH)
		return;

	gint sb_width = gtk_widget_get_width(pScrollbarV);
	gint sb_height = gtk_widget_get_height(pScrollbarH);
	
	gint area_w = gtk_widget_get_width(m_pGrid);
	gint area_h = gtk_widget_get_height(m_pGrid);

	PreferencesPtr prefsPtr = Preferences::GetInstance();
	bool bHideScrollbars = 	prefsPtr->GetBoolean(QUIVER_PREFS_VIEWER,QUIVER_PREFS_VIEWER_SCROLLBARS_HIDE);
	
	if (bHideScrollbars || QUIVER_IMAGE_VIEW_MODE_FILL_SCREEN == view_mode || IsVideo())
	{
		//FIXME: just don't show scrollbars because they jump around in an
		// infinite loop for certain situations in this mode
		// hide h hide v
		gtk_widget_set_visible(pScrollbarV, FALSE); 	
		gtk_widget_set_visible(pScrollbarH, FALSE);

	}
	else if ( (area_w < width && area_h < height) ||
		(area_w < width && area_h - sb_height < height) ||
		(area_w - sb_width < width && area_h < height) )
	{
		// show h show v
		gtk_widget_set_visible(pScrollbarV, TRUE); 	
		gtk_widget_set_visible(pScrollbarH, TRUE);
	}
	else if (area_w < width)
	{
		// show h hide v
		gtk_widget_set_visible(pScrollbarV, FALSE);
		gtk_widget_set_visible(pScrollbarH, TRUE);		
	}
	else if (area_h < height)
	{
		// hide h show v
		gtk_widget_set_visible(pScrollbarH, FALSE);
		gtk_widget_set_visible(pScrollbarV, TRUE);	
	}
	else
	{
		// hide h hide v
		gtk_widget_set_visible(pScrollbarV, FALSE); 	
		gtk_widget_set_visible(pScrollbarH, FALSE);
	}

	m_iTimeoutScrollbars = 0;


	GtkAdjustment *h = m_pAdjustmentH;
	GtkAdjustment *v = m_pAdjustmentV;

	if (NULL == h || NULL == v)
		return;
	

	if (gtk_adjustment_get_page_size(h) >= gtk_adjustment_get_upper(h) &&
		gtk_adjustment_get_page_size(v) >= gtk_adjustment_get_upper(v))
	{
		// enable drag n drop via GtkDragSource controller
		if (!m_pDragSource) {
			m_pDragSource = gtk_drag_source_new();
			g_signal_connect(m_pDragSource, "prepare", G_CALLBACK(signal_drag_source_prepare), this);
			g_signal_connect(m_pDragSource, "drag-begin", G_CALLBACK(signal_drag_begin), this);
			g_signal_connect(m_pDragSource, "drag-end", G_CALLBACK(signal_drag_end), this);
			gtk_widget_add_controller(m_pImageView, GTK_EVENT_CONTROLLER(m_pDragSource));
		}

	}
	else
	{
		// disable drag n drop
		if (m_pDragSource) {
			gtk_widget_remove_controller(m_pImageView, GTK_EVENT_CONTROLLER(m_pDragSource));
			m_pDragSource = NULL;
		}
	}

}

void Viewer::ViewerImpl::CacheImageAtSize(QuiverFile f, int w, int h)
{
	if (m_bMaximizeViewableArea)
	{
		ImageLoader::LoadParams params = {};
		params.orientation = GetMaximizedOrientation(f,true);
		params.max_width = w;
		params.max_height = h;
		params.reload = false;
		params.fullsize = false;
		params.no_thumb_preview = false;
		params.state = ImageLoader::CACHE;
		m_ImageLoader.LoadImage(f,params);

	}
	else
	{
		m_ImageLoader.CacheImageAtSize(f,w,h);
	}
}

void Viewer::ViewerImpl::LoadImage(QuiverFile f)
{
	gint width=0, height=0;

	QuiverImageViewMode mode = quiver_image_view_get_view_mode_unmagnified(QUIVER_IMAGE_VIEW(m_pImageView));
	
	if (mode != QUIVER_IMAGE_VIEW_MODE_ACTUAL_SIZE && gtk_widget_get_realized(m_pImageView))
	{
		width = gtk_widget_get_width(m_pImageView);
		height = gtk_widget_get_height(m_pImageView);
	}

	if (mode == QUIVER_IMAGE_VIEW_MODE_FILL_SCREEN)
	{
		int in_width = f.GetWidth();
		int in_height = f.GetHeight();

		if (4 < GetMaximizedOrientation(f,true) )
		{
			swap(in_width,in_height);
		}

		quiver_image_view_get_pixbuf_display_size_for_mode_alt(
				QUIVER_IMAGE_VIEW(m_pImageView), 
				QUIVER_IMAGE_VIEW_MODE_FILL_SCREEN, 
				in_width, in_height, 
				&width, &height);
	}

	SetCurrentOrientation(f.GetOrientation(), false);

	GrowDecodeSizeForZoom(f, width, height);

	LoadImageAtSize(f, width, height);
}

/* Zoomed modes scale the image up on screen, so past 1:1 the widget-sized
 * decode would be soft.  Ask for as many pixels as the magnified image needs;
 * the loader never returns more than the file actually has, so this only ever
 * sharpens the result. */
void Viewer::ViewerImpl::GrowDecodeSizeForZoom(QuiverFile f, gint &width, gint &height)
{
	if (!viewer_view_mode_is_zoomed(quiver_image_view_get_view_mode(QUIVER_IMAGE_VIEW(m_pImageView))))
		return;

	if (f.GetWidth() <= 0 || f.GetHeight() <= 0)
		return;

	int in_width = f.GetWidth();
	int in_height = f.GetHeight();

	if (4 < GetMaximizedOrientation(f,true) )
	{
		swap(in_width,in_height);
	}

	gint zoom_width = 0, zoom_height = 0;
	quiver_image_view_get_pixbuf_display_size_for_mode_alt(
			QUIVER_IMAGE_VIEW(m_pImageView),
			QUIVER_IMAGE_VIEW_MODE_ZOOM,
			in_width, in_height,
			&zoom_width, &zoom_height);

	if (zoom_width > width)
		width = zoom_width;
	if (zoom_height > height)
		height = zoom_height;
}

void Viewer::ViewerImpl::LoadImageAtSize(QuiverFile f, int w, int h)
{
	if (m_bMaximizeViewableArea)
	{
		ImageLoader::LoadParams params = {};
		params.orientation = GetCurrentOrientation(true);
		params.max_width = w;
		params.max_height = h;
		params.reload = false;
		params.fullsize = false;
		params.no_thumb_preview = false;
		params.state = ImageLoader::LOAD;
		m_ImageLoader.LoadImage(f,params);
	}
	else
	{
		m_ImageLoader.LoadImageAtSize(f,w,h);
	}
}

void Viewer::ViewerImpl::CacheNext(bool bDirectionForward)
{
	gint width=0, height=0;

	QuiverImageViewMode mode = quiver_image_view_get_view_mode_unmagnified(QUIVER_IMAGE_VIEW(m_pImageView));
	
	if (mode == QUIVER_IMAGE_VIEW_MODE_FILL_SCREEN)
	{
		QuiverFile f;
		bool bGetSize = false;

		if (bDirectionForward)
		{
			if (m_ImageListPtr->HasNext())
			{
				f = m_ImageListPtr->GetNext();
				bGetSize = true;
			}
		}
		else
		{
			if (m_ImageListPtr->HasPrevious())
			{
				f = m_ImageListPtr->GetPrevious();
				bGetSize = true;
			}
		}

		if (bGetSize)
		{
			int in_width = f.GetWidth();
			int in_height = f.GetHeight();

			if (4 < GetMaximizedOrientation(f,true) )
			{
				swap(in_width,in_height);
			}

			quiver_image_view_get_pixbuf_display_size_for_mode_alt(
					QUIVER_IMAGE_VIEW(m_pImageView), 
					QUIVER_IMAGE_VIEW_MODE_FILL_SCREEN, 
					in_width, in_height, 
					&width, &height);
		}
	}
	else if (mode != QUIVER_IMAGE_VIEW_MODE_ACTUAL_SIZE && gtk_widget_get_realized(m_pImageView))
	{
		width = gtk_widget_get_width(m_pImageView);
		height = gtk_widget_get_height(m_pImageView);
	}

	/* the cached image replaces the current one when the user navigates, so it
	 * needs the same decode size the zoomed view will ask for */
	if (m_ImageListPtr != NULL)
	{
		QuiverFile zoom_file;
		bool bZoomFile = false;
		if (bDirectionForward && m_ImageListPtr->HasNext())
		{
			zoom_file = m_ImageListPtr->GetNext();
			bZoomFile = true;
		}
		else if (!bDirectionForward && m_ImageListPtr->HasPrevious())
		{
			zoom_file = m_ImageListPtr->GetPrevious();
			bZoomFile = true;
		}

		if (bZoomFile)
		{
			GrowDecodeSizeForZoom(zoom_file, width, height);
		}
	}

	if (bDirectionForward)
	{
		// cache the next image if there is one
		if (m_ImageListPtr->HasNext())
		{
			QuiverFile f = m_ImageListPtr->GetNext();
			CacheImageAtSize(f, width, height);
		}
		else if (0 != m_iTimeoutSlideshowID && m_bSlideShowLoop)
		{
			QuiverFile f = m_ImageListPtr->Get(0);
			CacheImageAtSize(f, width, height);
		}
	}
	else
	{
		// cache the prev image if there is one
		if (m_ImageListPtr->HasPrevious())
		{
			QuiverFile f = m_ImageListPtr->GetPrevious();
			CacheImageAtSize(f, width, height);
		}
	}
}

void Viewer::ViewerImpl::SetImageIndex(int index, bool bDirectionForward, bool bCacheNext)
{
	m_ImageListPtr->BlockHandler(m_ImageListEventHandlerPtr);
	
	if (0 != m_ImageListPtr->GetSize() && m_ImageListPtr->SetCurrentIndex(index))
	{
		StopVideo(false);

		m_pViewer->EmitCursorChangedEvent();

		/* in overlay mode, briefly show the filmstrip on navigation */
		if (m_bFilmstripOverlay)
		{
			if (!IsPointerOverFilmstrip())
			{
				ShowFilmstripOverlay();
				ScheduleFilmstripHide();
			}
		}

		g_signal_handlers_block_by_func(m_pIconView,(gpointer)viewer_iconview_cursor_changed,this);

		// a new current item: show only the play button; the timeline appears
		// once play is pressed
		m_bTimelineVisible = false;
		UpdateTimelineVisibility();

		bool keep_visible = m_bPointerOverOverlayBar || IsPointerOverControls();
		if (keep_visible)
		{
			viewer_controls_show(this);
			viewer_set_controls_opacity(this, 1.0);
			RefreshAutoHideTimer();
		}
		else
		{
			viewer_set_controls_visible(this, false);
		}
		UpdateCenterPlayButtonVisibility();

		QuiverFile f = m_ImageListPtr->GetCurrent();
		UpdateNavigationControl();

		GdkTexture *cached_thumb = m_ThumbnailCache.GetTexture(f.GetURI());
		if (NULL != cached_thumb)
		{
			int w = f.GetWidth();
			int h = f.GetHeight();
			if (w <= 0 || h <= 0)
			{
				w = gdk_texture_get_width(cached_thumb);
				h = gdk_texture_get_height(cached_thumb);
			}
			if (4 < f.GetOrientation())
			{
				swap(w, h);
			}
			/* the cached thumbnail is delivered straight to the view, so it
			 * has to go through the same guard as the loader's: a "keep zoom
			 * and pan" image must not be reset by a cache hit */
			quiver_image_view_set_texture_at_size_ex(QUIVER_IMAGE_VIEW(m_pImageView), cached_thumb, w, h,
				Viewer::ShouldResetViewForNewImage(GTK_WIDGET(m_pImageView), TRUE));
			g_object_unref(cached_thumb);
		}
		
		LoadImage(f);
		
		quiver_icon_view_set_cursor_cell( QUIVER_ICON_VIEW(m_pIconView),
		      m_ImageListPtr->GetCurrentIndex() );	

		if (bCacheNext)
		{
			CacheNext(bDirectionForward);
		}
			
		g_signal_handlers_unblock_by_func(m_pIconView,(gpointer)viewer_iconview_cursor_changed,this);

		/* If the current item changed because the user navigated (arrow keys,
		 * scroll wheel, filmstrip, First/Last/Next/Previous actions) while a
		 * slideshow is running, re-seat the show on the new item.  Videos get
		 * a short grace countdown before auto-playing so rapid scrolling
		 * never triggers playback.  Skip this for the state machine's own
		 * advance (m_bSlideShowMachineAdvancing). */
		if (m_bSlideShowRunning && !m_bSlideShowPaused && !m_bSlideShowMachineAdvancing)
		{
			HandleSlideShowManualNavigation();
		}
	}
	
	m_ImageListPtr->UnblockHandler(m_ImageListEventHandlerPtr);
	
	if (m_ImageListPtr->GetSize())
	{
		m_QuiverFileCurrent = m_ImageListPtr->GetCurrent();
	}
	else
	{
		QuiverFile f;
		m_QuiverFileCurrent = f;
		show_image_load_error(m_pImageErrorLabel, false);
		quiver_image_view_set_texture(QUIVER_IMAGE_VIEW(m_pImageView),NULL);
	}
	
	// update the toolbar / menu buttons - (un)set sensitive 
	UpdateUI();
}

int  Viewer::ViewerImpl::GetMaximizedOrientation(QuiverFile f, bool bCombinedWithFileOrientation /* = false*/)
{
	int orientation = 1;
	if (m_bMaximizeViewableArea)
	{
		int aw = f.GetWidth();
		int ah = f.GetHeight();
		if (4 < f.GetOrientation())
		{
			swap(aw,ah);
		}

		gint width=0, height=0;
		width = gtk_widget_get_width(m_pImageView);
		height = gtk_widget_get_height(m_pImageView);

		double rimg = aw/(double)ah;
		double rscreen = width/(double)height;

		if ( (rimg < 1 && rscreen < 1) ||
			(rimg >= 1 && rscreen >= 1) )
		{
			orientation = 1;
		}
		else
		{
			// FIXME : make an option to rotate left or right?
			// 6 = rotate to the right
			orientation = 6;
		}

	}

	if (bCombinedWithFileOrientation)
	{
		orientation = combine_matrix[orientation][f.GetOrientation()];
	}

	return orientation;
}

int  Viewer::ViewerImpl::GetCurrentOrientation(bool bCombinedWithMaximizedOrientation /* = false */)
{
	int orientation = m_iCurrentOrientation;

	if (bCombinedWithMaximizedOrientation && m_bMaximizeViewableArea)
	{
		QuiverFile f = m_ImageListPtr->GetCurrent();
		orientation = combine_matrix[orientation][GetMaximizedOrientation(f)];
	}

	return orientation;
}

void Viewer::ViewerImpl::SetCurrentOrientation(int iOrientation, bool bUpdateExif /*= true*/)
{
	m_iCurrentOrientation = iOrientation;
	
if (bUpdateExif)
	{
		QuiverFile f = m_ImageListPtr->GetCurrent();
		f.SetOrientation(m_iCurrentOrientation);
	}
	m_ImageLoader.SetLoadOrientation(GetCurrentOrientation(true));
}

static void
on_filmstrip_overlay_leave(GtkEventControllerMotion *controller, gpointer user_data)
{
	(void)controller;
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl *)user_data;
	p->ScheduleFilmstripHide();
}

static void
on_filmstrip_overlay_enter(GtkEventControllerMotion *controller, gpointer user_data)
{
	(void)controller;
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl *)user_data;
	p->CancelFilmstripHide();
	p->ShowFilmstripOverlay();
	/* Coming onto the strip is pointer activity: the view's own motion handler
	 * only runs for the picture, so a pointer that was hidden over the picture
	 * would stay hidden over the strip the pointer has just moved onto. */
	viewer_set_idle_cursor(p, false);
	p->RefreshAutoHideTimer();
}

/* Moving along the strip counts for as much as moving along the picture: the
 * strip is worked with the pointer, so the pointer has to be there to be seen
 * doing it. */
static void
on_filmstrip_overlay_motion(GtkEventControllerMotion *controller, gdouble x, gdouble y, gpointer user_data)
{
	(void)controller; (void)x; (void)y;
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl *)user_data;
	viewer_set_idle_cursor(p, false);
	p->RefreshAutoHideTimer();
}

static void
on_filmstrip_edge_enter(GtkEventControllerMotion *controller, gpointer user_data)
{
	(void)controller;
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl *)user_data;
	if (p->IsPointerOverMediaControls()) return;
	p->CancelFilmstripHide();
	p->ShowFilmstripOverlay();
}

static void
on_filmstrip_edge_leave(GtkEventControllerMotion *controller, gpointer user_data)
{
	(void)controller;
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl *)user_data;
	p->ScheduleFilmstripHide();
}

static gboolean filmstrip_hide_timeout_cb(gpointer user_data)
{
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl *)user_data;
	p->m_iTimeoutFilmstripHide = 0;
	p->HideFilmstripOverlay();
	return G_SOURCE_REMOVE;
}

#define FADE_STEP 0.12   /* opacity change per tick */
#define FADE_INTERVAL 16 /* ms per tick (~60fps) */

static gboolean filmstrip_fade_cb(gpointer user_data)
{
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl *)user_data;

	if (p->m_bFadingIn)
	{
		p->m_dFadeOpacity = MIN(p->m_dFadeOpacity + FADE_STEP, 1.0);
		gtk_widget_set_opacity(p->m_pIconView, p->m_dFadeOpacity);
		if (p->m_dFadeOpacity >= 1.0)
		{
			p->m_iTimeoutFilmstripFade = 0;
			return G_SOURCE_REMOVE;
		}
	}
	else
	{
		p->m_dFadeOpacity = MAX(p->m_dFadeOpacity - FADE_STEP, 0.0);
		gtk_widget_set_opacity(p->m_pIconView, p->m_dFadeOpacity);
		if (p->m_dFadeOpacity <= 0.0)
		{
			gtk_widget_set_visible(p->m_pIconView, FALSE);
			p->m_iTimeoutFilmstripFade = 0;
			return G_SOURCE_REMOVE;
		}
	}
	return G_SOURCE_CONTINUE;
}

void Viewer::ViewerImpl::CancelFilmstripFade()
{
	if (0 != m_iTimeoutFilmstripFade)
	{
		g_source_remove(m_iTimeoutFilmstripFade);
		m_iTimeoutFilmstripFade = 0;
	}
}

void Viewer::ViewerImpl::StartFilmstripFade(bool fadeIn)
{
	CancelFilmstripFade();
	m_bFadingIn = fadeIn;
	m_iTimeoutFilmstripFade = g_timeout_add(FADE_INTERVAL, filmstrip_fade_cb, this);
}

/* ── media controls visibility ────────────────────────────────────
 * Controls are shown/hidden natively (gtk_widget_set_visible): a hidden
 * control is not rendered and, vitally, is not a pointer pick target, so it
 * can never swallow clicks meant for whatever is underneath (the old scheme
 * faked "hidden" with an opacity of 0, which left the widget targetable and
 * required an extra gtk_widget_set_can_target whack-a-mole for every control.
 * When visible, the control is fully opaque. */

static void set_control_visible(GtkWidget* w, bool visible)
{
	if (!w) return;
	if (visible)
	{
		gtk_widget_set_opacity(w, 1.0);
		gtk_widget_set_visible(w, TRUE);
	}
	else
	{
		gtk_widget_set_visible(w, FALSE);
	}
}

/* The unified overlay bar and timeline row fade/show/hide together. */
static void viewer_set_controls_opacity(Viewer::ViewerImpl* p, double opacity)
{
	if (p->m_pViewerOverlayBar)
		gtk_widget_set_opacity(p->m_pViewerOverlayBar, opacity);
	if (p->m_pTimelineRow && p->IsVideo() && p->m_bVideoPlaybackStarted)
		gtk_widget_set_opacity(p->m_pTimelineRow, opacity);
}

static void viewer_set_controls_visible(Viewer::ViewerImpl* p, bool visible)
{
	p->m_bControlsVisible = visible;
	if (p->m_pViewerOverlayBar)
		set_control_visible(p->m_pViewerOverlayBar, visible);
	if (p->m_pTimelineRow)
		set_control_visible(p->m_pTimelineRow, visible && p->IsVideo() && p->m_bVideoPlaybackStarted);
}

/* True when the controls should be (re-)shown: they are gone entirely, or a
 * fade-out is mid-flight and user activity must not let them vanish. */
static bool viewer_controls_need_reshow(Viewer::ViewerImpl* p)
{
	if (NULL == p->m_pViewerOverlayBar)
		return false;
	if (!gtk_widget_get_visible(p->m_pViewerOverlayBar))
		return true;
	return 0 != p->m_iTimeoutControlsFade && !p->m_bControlsFadingIn;
}

/* Make the overlay bar (and timeline) pickable/rendered again WITHOUT touching
 * opacity: used before a fade-in, which animates the (still low) opacity up
 * to 1.0 to restore a smooth transition instead of a hard pop-in. */
static void viewer_controls_show(Viewer::ViewerImpl* p)
{
	p->m_bControlsVisible = true;
	if (p->m_pViewerOverlayBar)
		gtk_widget_set_visible(p->m_pViewerOverlayBar, TRUE);
	if (p->m_pTimelineRow && p->IsVideo() && p->m_bVideoPlaybackStarted)
		gtk_widget_set_visible(p->m_pTimelineRow, TRUE);
}

/* 1x1 fully-transparent cursor used to hide the pointer while watching a
 * video.  GTK4 has no gdk_blank_cursor, so one is assembled from a blank
 * texture (same trick the classic quiver code did, now in the GTK4 idiom). */
static GdkCursor* viewer_blank_cursor(Viewer::ViewerImpl* p)
{
	if (NULL == p->m_pBlankCursor)
	{
		static const guint8 transparent_px[4] = { 0, 0, 0, 0 };
		GBytes *bytes = g_bytes_new_static(transparent_px, sizeof(transparent_px));
		GdkTexture *tex = gdk_memory_texture_new(1, 1, GDK_MEMORY_R8G8B8A8, bytes, 4);
		g_bytes_unref(bytes);
		p->m_pBlankCursor = gdk_cursor_new_from_texture(tex, 0, 0, NULL);
		g_object_unref(tex);
	}
	return p->m_pBlankCursor;
}

/* Show/hide the pointer over the whole window in step with the controls'
 * auto-hide fade-out while a video is playing (the legacy Quiver.cpp
 * version hid the cursor too, but only in fullscreen and the GTK2-era
 * gdk_window_set_cursor call was commented out under a FIXME). */
static void viewer_set_idle_cursor(Viewer::ViewerImpl* p, bool hidden)
{
	/* The cursor goes on the window, and the window is found through the
	 * overlay.  A viewer built without an overlay - no HUD - has no window of
	 * its own to put a cursor on, and the window it would find is the one the
	 * browser is in. */
	if (p == NULL || p->m_pOverlay == NULL || !GTK_IS_WIDGET(p->m_pOverlay))
		return;
	GtkWidget *root = GTK_WIDGET(gtk_widget_get_root(p->m_pOverlay));
	if (NULL == root)
		return;
	gtk_widget_set_cursor(root, hidden ? viewer_blank_cursor(p) : NULL);
}

#define CONTROLS_FADE_STEP  0.12
#define CONTROLS_FADE_INTERVAL 16

static gboolean controls_fade_cb(gpointer user_data)
{
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl *)user_data;

	if (p->m_bControlsFadingIn)
	{
		p->m_dControlsFadeOpacity = MIN(p->m_dControlsFadeOpacity + CONTROLS_FADE_STEP, 1.0);
		viewer_set_controls_opacity(p, p->m_dControlsFadeOpacity);
		if (p->m_dControlsFadeOpacity >= 1.0)
		{
			p->m_iTimeoutControlsFade = 0;
			return G_SOURCE_REMOVE;
		}
	}
	else
	{
		p->m_dControlsFadeOpacity = MAX(p->m_dControlsFadeOpacity - CONTROLS_FADE_STEP, 0.0);
		viewer_set_controls_opacity(p, p->m_dControlsFadeOpacity);
		if (p->m_dControlsFadeOpacity <= 0.0)
		{
			viewer_set_controls_visible(p, false);
			p->m_iTimeoutControlsFade = 0;
			return G_SOURCE_REMOVE;
		}
	}
	return G_SOURCE_CONTINUE;
}

void Viewer::ViewerImpl::CancelControlsFade()
{
	if (0 != m_iTimeoutControlsFade)
	{
		g_source_remove(m_iTimeoutControlsFade);
		m_iTimeoutControlsFade = 0;
	}
}

void Viewer::ViewerImpl::StartControlsFade(bool fadeIn)
{
	CancelControlsFade();
	m_bControlsFadingIn = fadeIn;
	m_iTimeoutControlsFade = g_timeout_add(CONTROLS_FADE_INTERVAL, controls_fade_cb, this);
}

/* ── Play / Pause animation (fading in and out in both transparency and size) ── */

static gboolean play_anim_tick_cb(GtkWidget *widget, GdkFrameClock *frame_clock, gpointer user_data)
{
	(void)widget;
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl *)user_data;
	if (NULL == p || NULL == p->m_pPlayAnimWidget)
		return G_SOURCE_REMOVE;

	gint64 now = gdk_frame_clock_get_frame_time(frame_clock);
	if (0 == p->m_iPlayAnimStartTime)
	{
		p->m_iPlayAnimStartTime = now;
	}

	const gint64 duration_us = 450000; // 450 ms animation
	gint64 elapsed = now - p->m_iPlayAnimStartTime;
	double t = (double)elapsed / (double)duration_us;

	if (t >= 1.0)
	{
		gtk_widget_set_opacity(p->m_pPlayAnimWidget, 0.0);
		gtk_widget_set_visible(p->m_pPlayAnimWidget, FALSE);
		p->m_iPlayAnimTickId = 0;
		p->m_iPlayAnimStartTime = 0;
		if (p->m_pPlayButton)
		{
			gtk_widget_set_opacity(p->m_pPlayButton, 1.0);
		}
		return G_SOURCE_REMOVE;
	}

	if (t < 0.0)
		t = 0.0;

	double opacity = 0.0;
	double scale = 1.0;

	/* Phase 1 (0.0 -> 0.35, ~160ms): fade-in and scale up from 0.60 to 1.08 (subtle pop) */
	/* Phase 2 (0.35 -> 1.0, ~290ms): fade-out and expand from 1.08 to 1.45 (dissolving burst) */
	if (t <= 0.35)
	{
		double sub = t / 0.35;
		double ease = sin(sub * (M_PI / 2.0));
		opacity = ease;
		scale = 0.60 + (1.08 - 0.60) * ease;
	}
	else
	{
		double sub = (t - 0.35) / 0.65;
		double ease = sin(sub * (M_PI / 2.0));
		opacity = 1.0 - ease;
		scale = 1.08 + (1.45 - 1.08) * ease;
	}

	const int base_btn_size = 92;
	const int base_icon_size = 48;
	int btn_size = (int)(base_btn_size * scale + 0.5);
	int icon_size = (int)(base_icon_size * scale + 0.5);

	gtk_widget_set_size_request(p->m_pPlayAnimWidget, btn_size, btn_size);
	if (p->m_pPlayAnimImage)
	{
		gtk_image_set_pixel_size(GTK_IMAGE(p->m_pPlayAnimImage), icon_size);
	}
	gtk_widget_set_opacity(p->m_pPlayAnimWidget, opacity);

	return G_SOURCE_CONTINUE;
}

void Viewer::ViewerImpl::CancelPlayPauseAnimation()
{
	if (0 != m_iPlayAnimTickId && NULL != m_pPlayAnimWidget)
	{
		gtk_widget_remove_tick_callback(m_pPlayAnimWidget, m_iPlayAnimTickId);
		m_iPlayAnimTickId = 0;
	}
	m_iPlayAnimStartTime = 0;
	if (NULL != m_pPlayAnimWidget)
	{
		gtk_widget_set_opacity(m_pPlayAnimWidget, 0.0);
		gtk_widget_set_visible(m_pPlayAnimWidget, FALSE);
	}
	if (NULL != m_pPlayButton)
	{
		gtk_widget_set_opacity(m_pPlayButton, 1.0);
	}
}

void Viewer::ViewerImpl::TriggerPlayPauseAnimation(bool isPlaying)
{
	if (NULL == m_pPlayAnimWidget || NULL == m_pPlayAnimImage)
		return;

	const char *icon_name = isPlaying ? "media-playback-start" : "media-playback-pause";
	gtk_image_set_from_icon_name(GTK_IMAGE(m_pPlayAnimImage), icon_name);

	const int base_btn_size = 92;
	const int base_icon_size = 48;
	int init_btn = (int)(base_btn_size * 0.60);
	int init_icon = (int)(base_icon_size * 0.60);
	gtk_widget_set_size_request(m_pPlayAnimWidget, init_btn, init_btn);
	gtk_image_set_pixel_size(GTK_IMAGE(m_pPlayAnimImage), init_icon);
	gtk_widget_set_opacity(m_pPlayAnimWidget, 0.0);
	gtk_widget_set_visible(m_pPlayAnimWidget, TRUE);

	m_iPlayAnimStartTime = 0;
	if (0 == m_iPlayAnimTickId)
	{
		m_iPlayAnimTickId = gtk_widget_add_tick_callback(m_pPlayAnimWidget, play_anim_tick_cb, this, NULL);
	}
}

/* ── filmstrip overlay (continued) ──────────────────────────────── */

void Viewer::ViewerImpl::ShowFilmstripOverlay()
{
	if (!m_bFilmstripOverlay) return;
	if (!QuiverUtils::ToggleActionGetActive(ACTION_VIEWER_VIEW_FILM_STRIP)) return;
	if (m_bFilmstripHiddenByFS) return;
	/* A playing video is no reason to refuse: reaching the next item without
	 * stopping the one being watched is what the strip is for, and keeping it out
	 * of the way of the video controls is what the pointer-over-controls test and
	 * the hit test below are for - not a refusal to put it on screen. */
	/* The icon view is created hidden and never shown again after a
	 * fade-out completes; a fade-in only animates opacity.  Reveal the
	 * widget itself or the strip can never be displayed. */
	if (!gtk_widget_get_visible(m_pIconView))
	{
		gtk_widget_set_visible(m_pIconView, TRUE);
	}
	CancelFilmstripHide();
	CancelFilmstripFade();
	m_dFadeOpacity = gtk_widget_get_opacity(m_pIconView);
	if (m_dFadeOpacity < 1.0)
		StartFilmstripFade(true);
}

void Viewer::ViewerImpl::HideFilmstripOverlay()
{
	if (!m_bFilmstripOverlay) return;
	if (IsPointerOverFilmstrip()) return;
	CancelFilmstripFade();
	m_dFadeOpacity = gtk_widget_get_opacity(m_pIconView);
	if (m_dFadeOpacity > 0.0)
		StartFilmstripFade(false);
	else
		gtk_widget_set_visible(m_pIconView, FALSE);
}

void Viewer::ViewerImpl::UpdateFilmstripForPlayback()
{
	if (!m_bFilmstripOverlay || !m_pFilmstripEdge) return;

	if (IsVideo() && IsPlaying())
	{
		/* Hide the strip itself so it does not sit over the video controls, and
		 * hide it even if the pointer is over it: the user pressed play while
		 * the strip was up, which means the video is what they are looking at
		 * now.  The hover edge stays: the strip is what the pointer is for, and
		 * the edge is how it is summoned. */
		CancelFilmstripFade();
		m_dFadeOpacity = gtk_widget_get_opacity(m_pIconView);
		if (m_dFadeOpacity > 0.0)
			StartFilmstripFade(false);
		else
			gtk_widget_set_visible(m_pIconView, FALSE);
	}
	else
	{
		/* Restore the hover edge.  It is the one thing here that was ever lost
		 * for good: a widget hidden once and never shown again takes the
		 * filmstrip with it for the rest of the session, so playing a video
		 * once left the strip unreachable by the pointer from then on, with
		 * nothing but this branch to bring it back. */
		gtk_widget_set_visible(m_pFilmstripEdge, TRUE);
	}
}

bool Viewer::ViewerImpl::IsFilmstripShowing() const
{
	/* The strip is there when the icon view is both visible and not fully faded
	 * out: HideFilmstripOverlay() leaves it visible while the fade runs, so the
	 * visible flag alone would keep saying "on screen" for a strip nobody can
	 * see, and that strip must not be a reason to hold the pointer back. */
	if (m_pIconView == NULL || !GTK_IS_WIDGET(m_pIconView))
		return false;
	if (!gtk_widget_get_visible(m_pIconView))
		return false;
	return gtk_widget_get_opacity(m_pIconView) > 0.0;
}

bool Viewer::ViewerImpl::IsPointerOverFilmstrip() const
{
	if (!m_bFilmstripOverlay || !m_pFilmstripOverlayContainer) return false;

	GtkNative *native = GTK_NATIVE(gtk_widget_get_native(m_pFilmstripOverlayContainer));
	if (!native) return false;
	GdkSurface *surface = gtk_native_get_surface(native);
	if (!surface) return false;

	GdkDisplay *display = gtk_widget_get_display(m_pFilmstripOverlayContainer);
	if (!display) return false;
	GdkSeat *seat = gdk_display_get_default_seat(display);
	if (!seat) return false;
	GdkDevice *device = gdk_seat_get_pointer(seat);
	if (!device) return false;
	double surface_x = 0., surface_y = 0.;
	if (!gdk_surface_get_device_position(surface, device, &surface_x, &surface_y, NULL))
		return false;

	graphene_point_t src = GRAPHENE_POINT_INIT((float)surface_x, (float)surface_y);
	graphene_point_t dest;
	if (!gtk_widget_compute_point(GTK_WIDGET(native), m_pFilmstripOverlayContainer, &src, &dest))
		return false;
	return dest.x >= 0 && dest.x < gtk_widget_get_width(m_pFilmstripOverlayContainer)
		&& dest.y >= 0 && dest.y < gtk_widget_get_height(m_pFilmstripOverlayContainer);
}

bool Viewer::ViewerImpl::IsPointerOverControls() const
{
	if (!IsVideo() && m_bPointerOverOverlayBar)
		return true;

	auto check_widget = [](GtkWidget *ctrl) -> bool {
		if (!ctrl || !gtk_widget_get_visible(ctrl)) return false;

		GtkNative *native = GTK_NATIVE(gtk_widget_get_native(ctrl));
		if (!native) return false;
		GdkSurface *surface = gtk_native_get_surface(native);
		if (!surface) return false;

		GdkDisplay *display = gtk_widget_get_display(ctrl);
		if (!display) return false;
		GdkSeat *seat = gdk_display_get_default_seat(display);
		if (!seat) return false;
		GdkDevice *device = gdk_seat_get_pointer(seat);
		if (!device) return false;
		double surface_x = 0., surface_y = 0.;
		if (!gdk_surface_get_device_position(surface, device, &surface_x, &surface_y, NULL))
			return false;

		graphene_point_t src = GRAPHENE_POINT_INIT((float)surface_x, (float)surface_y);
		graphene_point_t dest;
		if (!gtk_widget_compute_point(GTK_WIDGET(native), ctrl, &src, &dest))
			return false;
		float local_x = dest.x;
		float local_y = dest.y;

		/* X: anywhere across the full width of the controls bar */
		bool in_x = local_x >= 0
			&& local_x < gtk_widget_get_width(ctrl);

		/* Y: at or below the controls top, extending to its bottom */
		bool in_y = local_y >= 0
			&& local_y <= gtk_widget_get_height(ctrl);

		return in_x && in_y;
	};

	if (check_widget(m_pViewerOverlayBar))
		return true;
	if (IsVideo() && m_bVideoPlaybackStarted && check_widget(m_pTimelineRow))
		return true;
	return false;
}

bool Viewer::ViewerImpl::IsPointerOverMediaControls() const
{
	return IsPointerOverControls();
}

bool Viewer::ViewerImpl::IsPointOverControlsOrFilmstrip(GtkWidget *event_widget, double x, double y) const
{
	if (m_bPointerOverOverlayBar)
		return true;

	auto check_widget = [&](GtkWidget *ctrl) -> bool {
		if (!ctrl || !gtk_widget_get_visible(ctrl)) return false;
		if (gtk_widget_get_opacity(ctrl) < 0.05) return false;

		int w = gtk_widget_get_width(ctrl);
		int h = gtk_widget_get_height(ctrl);
		if (w <= 0 || h <= 0)
		{
			gtk_widget_measure(ctrl, GTK_ORIENTATION_HORIZONTAL, -1, NULL, &w, NULL, NULL);
			gtk_widget_measure(ctrl, GTK_ORIENTATION_VERTICAL, -1, NULL, &h, NULL, NULL);
		}
		if (w <= 0 || h <= 0) return false;

		if (event_widget != NULL && gtk_widget_get_root(event_widget) != NULL
			&& gtk_widget_get_root(ctrl) != NULL
			&& gtk_widget_get_root(event_widget) == gtk_widget_get_root(ctrl))
		{
			graphene_point_t src = GRAPHENE_POINT_INIT((float)x, (float)y);
			graphene_point_t dest;
			if (gtk_widget_compute_point(event_widget, ctrl, &src, &dest))
			{
				if (dest.x >= 0 && dest.x < w && dest.y >= 0 && dest.y < h)
				{
					return true;
				}
			}
		}
		return false;
	};

	if (check_widget(m_pViewerOverlayBar))
		return true;
	if (IsVideo() && check_widget(m_pTimelineRow))
		return true;
	if (check_widget(m_pNavControlPill))
		return true;
	if (check_widget(m_pCenterPlayBtn))
		return true;
	if (check_widget(m_pSlideShowPausedPill))
		return true;

	if (m_bFilmstripOverlay)
	{
		if (check_widget(m_pFilmstripOverlayContainer))
			return true;
		if (check_widget(m_pFilmstripEdge))
			return true;
	}
	if (check_widget(m_pIconView))
		return true;

	if (IsPointerOverControls() || IsPointerOverFilmstrip())
		return true;

	return false;
}

void Viewer::ViewerImpl::ScheduleFilmstripHide()
{
	if (!m_bFilmstripOverlay) return;
	if (IsPointerOverFilmstrip()) return;
	CancelFilmstripHide();
	m_iTimeoutFilmstripHide = g_timeout_add(OVERLAY_AUTO_HIDE_TIMEOUT_MS, filmstrip_hide_timeout_cb, this);
}

void Viewer::ViewerImpl::CancelFilmstripHide()
{
	if (0 != m_iTimeoutFilmstripHide)
	{
		g_source_remove(m_iTimeoutFilmstripHide);
		m_iTimeoutFilmstripHide = 0;
	}
}

void Viewer::ViewerImpl::AddFilmstrip()
{
	PreferencesPtr prefsPtr = Preferences::GetInstance();
	int iFilmstripPos = prefsPtr->GetInteger(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_FILMSTRIP_POSITION, FSTRIP_POS_RIGHT);
	bool bOverlay = prefsPtr->GetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_FILMSTRIP_OVERLAY, true);

	/* remove filmstrip from its current parent (if any) */
	GtkWidget *current_parent = gtk_widget_get_parent(m_pIconView);
	if (current_parent != NULL) {
		g_object_ref(m_pIconView);
		gtk_widget_unparent(m_pIconView);
	}

	/* clean up any previous overlay edge widget */
	if (NULL != m_pFilmstripEdge)
	{
		GtkWidget *edge = m_pFilmstripEdge;
		m_pFilmstripEdge = NULL;
		if (gtk_widget_get_parent(edge) != NULL)
		{
			gtk_widget_unparent(edge);
		}
	}

	/* clean up any previous overlay container */
	if (NULL != m_pFilmstripOverlayContainer)
	{
		GtkWidget *container = m_pFilmstripOverlayContainer;
		m_pFilmstripOverlayContainer = NULL;
		if (gtk_widget_get_parent(container) != NULL)
		{
			gtk_widget_unparent(container);
		}
	}

	if (bOverlay)
	{
		/* ---- overlay mode: float the filmstrip on top of the image ---- */
		m_bFilmstripOverlay = true;

		/* single row or single column inside the overlay */
		if (iFilmstripPos == FSTRIP_POS_TOP || iFilmstripPos == FSTRIP_POS_BOTTOM)
			quiver_icon_view_set_n_rows(QUIVER_ICON_VIEW(m_pIconView), 1);
		else
			quiver_icon_view_set_n_columns(QUIVER_ICON_VIEW(m_pIconView), 1);

		/* wrap in an event box to position it within the overlay */
		m_pFilmstripOverlayContainer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
		gtk_widget_set_vexpand(m_pFilmstripOverlayContainer, FALSE);

		switch (iFilmstripPos)
		{
			case FSTRIP_POS_LEFT:
				gtk_widget_set_halign(m_pFilmstripOverlayContainer, GTK_ALIGN_START);
				gtk_widget_set_valign(m_pFilmstripOverlayContainer, GTK_ALIGN_FILL);
				break;
			case FSTRIP_POS_RIGHT:
				gtk_widget_set_halign(m_pFilmstripOverlayContainer, GTK_ALIGN_END);
				gtk_widget_set_valign(m_pFilmstripOverlayContainer, GTK_ALIGN_FILL);
				break;
			case FSTRIP_POS_TOP:
				gtk_widget_set_halign(m_pFilmstripOverlayContainer, GTK_ALIGN_FILL);
				gtk_widget_set_valign(m_pFilmstripOverlayContainer, GTK_ALIGN_START);
				break;
			case FSTRIP_POS_BOTTOM:
				gtk_widget_set_halign(m_pFilmstripOverlayContainer, GTK_ALIGN_FILL);
				gtk_widget_set_valign(m_pFilmstripOverlayContainer, GTK_ALIGN_END);
				break;
		}

		gtk_box_append(GTK_BOX(m_pFilmstripOverlayContainer), m_pIconView);

		/* for top/bottom, the container and icon view fill the full width;
		 * for left/right they stretch across the full overlay height */
		if (iFilmstripPos == FSTRIP_POS_TOP || iFilmstripPos == FSTRIP_POS_BOTTOM)
		{
			gtk_widget_set_hexpand(m_pFilmstripOverlayContainer, TRUE);
			gtk_widget_set_vexpand(m_pFilmstripOverlayContainer, FALSE);
			gtk_widget_set_hexpand(m_pIconView, TRUE);
			gtk_widget_set_vexpand(m_pIconView, FALSE);
		}
		else
		{
			gtk_widget_set_hexpand(m_pFilmstripOverlayContainer, FALSE);
			gtk_widget_set_vexpand(m_pFilmstripOverlayContainer, TRUE);
			gtk_widget_set_hexpand(m_pIconView, FALSE);
			gtk_widget_set_vexpand(m_pIconView, TRUE);
		}

		gtk_overlay_add_overlay(GTK_OVERLAY(m_pOverlay), m_pFilmstripOverlayContainer);

		/* add a transparent CSS class (clear any per-widget bg first) */
		set_widget_bg_color(m_pIconView, NULL);
		gtk_widget_add_css_class(m_pIconView, "filmstrip-overlay");

		/* create a hover edge along the relevant screen edge */
		m_pFilmstripEdge = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
		{
			GtkEventController *motion = gtk_event_controller_motion_new();
			g_signal_connect(motion, "enter", G_CALLBACK(on_filmstrip_edge_enter), this);
			g_signal_connect(motion, "leave", G_CALLBACK(on_filmstrip_edge_leave), this);
			gtk_widget_add_controller(m_pFilmstripEdge, motion);
		}

		/* the filmstrip container tracks leave/enter so the strip stays
		 * visible while the mouse is anywhere over the filmstrip, and motion so
		 * the pointer is not taken away underneath it */
		{
			GtkEventController *motion = gtk_event_controller_motion_new();
			g_signal_connect(motion, "enter", G_CALLBACK(on_filmstrip_overlay_enter), this);
			g_signal_connect(motion, "leave", G_CALLBACK(on_filmstrip_overlay_leave), this);
			g_signal_connect(motion, "motion", G_CALLBACK(on_filmstrip_overlay_motion), this);
			gtk_widget_add_controller(m_pFilmstripOverlayContainer, motion);
		}

		/* size the edge: thin strip along the relevant edge */
		switch (iFilmstripPos)
		{
			case FSTRIP_POS_LEFT:
				gtk_widget_set_size_request(m_pFilmstripEdge, 20, -1);
				gtk_widget_set_halign(m_pFilmstripEdge, GTK_ALIGN_START);
				gtk_widget_set_valign(m_pFilmstripEdge, GTK_ALIGN_FILL);
				break;
			case FSTRIP_POS_RIGHT:
				gtk_widget_set_size_request(m_pFilmstripEdge, 20, -1);
				gtk_widget_set_halign(m_pFilmstripEdge, GTK_ALIGN_END);
				gtk_widget_set_valign(m_pFilmstripEdge, GTK_ALIGN_FILL);
				break;
			case FSTRIP_POS_TOP:
				gtk_widget_set_size_request(m_pFilmstripEdge, -1, 20);
				gtk_widget_set_halign(m_pFilmstripEdge, GTK_ALIGN_FILL);
				gtk_widget_set_valign(m_pFilmstripEdge, GTK_ALIGN_START);
				break;
			case FSTRIP_POS_BOTTOM:
				gtk_widget_set_size_request(m_pFilmstripEdge, -1, 20);
				gtk_widget_set_halign(m_pFilmstripEdge, GTK_ALIGN_FILL);
				gtk_widget_set_valign(m_pFilmstripEdge, GTK_ALIGN_END);
				break;
		}

		gtk_overlay_add_overlay(GTK_OVERLAY(m_pOverlay), m_pFilmstripEdge);

		/* start hidden; the hover edge reveals it */
		gtk_widget_set_visible(m_pIconView, FALSE);
	}
	else
	{
		/* ---- docked mode: pack into the box as before ---- */
		m_bFilmstripOverlay = false;

		GtkBox* box = NULL;
		if (m_pIconView != NULL && !gtk_widget_get_visible(m_pIconView))
			gtk_widget_set_visible(m_pIconView, TRUE);

		switch (iFilmstripPos)
		{
			case FSTRIP_POS_TOP:
			case FSTRIP_POS_BOTTOM:
				box = GTK_BOX(m_pVBox);
				quiver_icon_view_set_n_rows(QUIVER_ICON_VIEW(m_pIconView),1);
				gtk_widget_set_hexpand(m_pIconView, TRUE);
				gtk_widget_set_vexpand(m_pIconView, FALSE);
				gtk_box_append (box, m_pIconView);
				break;
			case FSTRIP_POS_LEFT:
			case FSTRIP_POS_RIGHT:
				box = GTK_BOX(m_pHBox);
				quiver_icon_view_set_n_columns(QUIVER_ICON_VIEW(m_pIconView),1);
				gtk_widget_set_hexpand(m_pIconView, FALSE);
				gtk_widget_set_vexpand(m_pIconView, TRUE);
				gtk_box_append (box, m_pIconView);
				break;
		}

		switch (iFilmstripPos)
		{
			case FSTRIP_POS_TOP:
			case FSTRIP_POS_LEFT:
				/* place the filmstrip first in the box (at the start) */
				gtk_box_reorder_child_after (box, m_pIconView, NULL);
				break;
			case FSTRIP_POS_BOTTOM:
			case FSTRIP_POS_RIGHT:
				/* gtk_box_append already put it last; leave it at the end so
				 * it docks below / to the right of the image area. */
				break;
		}

		/* remove the overlay CSS class */
		gtk_widget_remove_css_class(m_pIconView, "filmstrip-overlay");

		CancelFilmstripHide();
		CancelFilmstripFade();

		/* icon view may have been hidden or faded out in overlay mode;
		 * ensure it is fully shown and opaque for docked mode */
		gtk_widget_set_visible(m_pIconView, TRUE);
		gtk_widget_set_opacity(m_pIconView, 1.0);
	}

	/* hand our reference over to the new parent container */
	if (NULL != current_parent)
	{
		g_object_unref(m_pIconView);
	}

	UpdateHUDPosition();
}

static gboolean 
timeout_event_motion_notify (gpointer user_data)
{
	Viewer::ViewerImpl *pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)user_data;

	gboolean bKeepVisible = FALSE;

	// keep visible while the volume popup is open (it floats above the bar)
	if (!bKeepVisible && pViewerImpl->m_pVolumeButton != NULL)
	{
		GtkPopover* volPop = gtk_menu_button_get_popover(GTK_MENU_BUTTON(pViewerImpl->m_pVolumeButton));
		if (volPop != NULL && gtk_widget_get_visible(GTK_WIDGET(volPop)))
			bKeepVisible = TRUE;
	}

	// keep visible while the video options popover is open
	if (!bKeepVisible && pViewerImpl->m_pVideoOptionsBtn != NULL)
	{
		GtkPopover* pop = gtk_menu_button_get_popover(GTK_MENU_BUTTON(pViewerImpl->m_pVideoOptionsBtn));
		if (pop != NULL && gtk_widget_get_visible(GTK_WIDGET(pop)))
			bKeepVisible = TRUE;
	}

	// ...and while the image submenu popover is open
	if (!bKeepVisible && pViewerImpl->m_pImageSubmenuBtn != NULL)
	{
		GtkPopover* imgPop = gtk_menu_button_get_popover(GTK_MENU_BUTTON(pViewerImpl->m_pImageSubmenuBtn));
		if (imgPop != NULL && gtk_widget_get_visible(GTK_WIDGET(imgPop)))
			bKeepVisible = TRUE;
	}

	// ...and while the view mode popover of the split button is open
	if (!bKeepVisible && pViewerImpl->m_pViewModeMenuPopover != NULL)
	{
		if (gtk_widget_get_visible(GTK_WIDGET(pViewerImpl->m_pViewModeMenuPopover)))
			bKeepVisible = TRUE;
	}

	// ...and while a GTK grab is active (e.g. dragging the timeline scale)
	if (!bKeepVisible && FALSE)
		bKeepVisible = TRUE;

	// ...and while pointer is over the viewer overlay bar
	if (!bKeepVisible && (pViewerImpl->m_bPointerOverOverlayBar
		|| pViewerImpl->IsPointerOverControls()))
		bKeepVisible = TRUE;

	// ...and while the pointer is over a filmstrip that is on screen.  The
	// pointer is the only way to work the strip, so taking it away while it is
	// under the pointer hides the very thing being pointed at - and unlike the
	// controls above, which come back on the next move, the strip is what the
	// move was over.
	if (!bKeepVisible && pViewerImpl->IsFilmstripShowing()
		&& pViewerImpl->IsPointerOverFilmstrip())
		bKeepVisible = TRUE;

	GtkWidget *root = GTK_WIDGET(gtk_widget_get_root(pViewerImpl->m_pOverlay));
	if (!bKeepVisible && root && GTK_IS_WINDOW(root) && !gtk_window_is_active(GTK_WINDOW(root)))
		bKeepVisible = TRUE;

	if (pViewerImpl->m_ImageListPtr && pViewerImpl->m_ImageListPtr->GetSize() > 0)
	{
		if (bKeepVisible)
		{
			pViewerImpl->m_iTimeoutMouseMotionNotify = g_timeout_add(OVERLAY_AUTO_HIDE_TIMEOUT_MS, timeout_event_motion_notify, pViewerImpl);
		}
		else
		{
			pViewerImpl->StartControlsFade(false);
			pViewerImpl->m_iTimeoutMouseMotionNotify = 0;
			/* the controls faded away because the pointer went idle: hide the
			 * pointer if playing video or in fullscreen/slideshow */
			GtkWidget *root = GTK_WIDGET(gtk_widget_get_root(pViewerImpl->m_pOverlay));
			bool bFS = root && GTK_IS_WINDOW(root) && gtk_window_is_fullscreen(GTK_WINDOW(root));
			if (pViewerImpl->IsPlaying() || pViewerImpl->m_bSlideShowRunning || bFS)
			{
				viewer_set_idle_cursor(pViewerImpl, true);
			}
		}
	}
	else
	{
		pViewerImpl->m_iTimeoutMouseMotionNotify = 0;
	}

	return FALSE;
}


/* Show/populate the seek-time popover positioned above the slider handle.
 * value is in [0,1] (fraction of the clip).  bKeyboard is true for arrow-key
 * seeks, which auto-hide after a short delay. */
static gboolean
viewer_scale_change_value_cb(GtkRange *range, GtkScrollType scroll, gdouble value, gpointer user_data)
{
	(void)range;
	(void)scroll;

	Viewer::ViewerImpl *p = (Viewer::ViewerImpl *)user_data;

	if (!p->m_pPipeline)
		return FALSE;

	GstFormat format = GST_FORMAT_TIME;
	gint64 clip_duration = 0;
	if (!gst_element_query_duration(GST_ELEMENT(p->m_pPipeline), format, &clip_duration) || clip_duration <= 0)
		return FALSE;

	gint64 target = (gint64)(clip_duration * CLAMP(value, 0.0, 1.0));

	gchar* str_pos = gst_time_format(target);
	gchar* str_len = gst_time_format(clip_duration);
	gchar* markup;

	markup = g_strdup_printf("<b>%s</b>", str_pos);
	gtk_label_set_markup(GTK_LABEL(p->m_pTimeElapsedLabel), markup);
	g_free(markup);

	markup = g_strdup_printf("<b>%s</b>", str_len);
	gtk_label_set_markup(GTK_LABEL(p->m_pTimeDurationLabel), markup);
	g_free(markup);

	g_free(str_len);
	g_free(str_pos);

	gdouble speed = (p->m_dPlaybackSpeed > 0.0) ? p->m_dPlaybackSpeed : 1.0;
	gst_element_seek(GST_ELEMENT(p->m_pPipeline), speed, format,
		GstSeekFlags(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE),
		GST_SEEK_TYPE_SET, target,
		GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);
	return FALSE;
}

static void
viewer_scale_button_press_cb(GtkGestureClick *gesture, gint n_press, gdouble x, gdouble y, gpointer user_data)
{
	(void)gesture; (void)n_press; (void)x; (void)y;
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl *)user_data;
	p->m_bSeekDragging = TRUE;
	p->CancelControlsFade();
	viewer_set_controls_visible(p, true);
	viewer_set_controls_opacity(p, 1.0);
}

static void
viewer_scale_button_release_cb(GtkGestureClick *gesture, gint n_press, gdouble x, gdouble y, gpointer user_data)
{
	(void)gesture; (void)n_press; (void)x; (void)y;
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl *)user_data;
	p->m_bSeekDragging = FALSE;
	p->RefreshAutoHideTimer();
}

/* Only treat a motion event as real "activity" when the pointer has actually
 * moved in surface coordinates.  GTK synthesizes motion events when a widget
 * re-lays-out under a parked pointer (e.g. the elapsed-time label growing and
 * nudging the transport row every tick); re-arming the auto-hide timer on
 * those kept the controls visible forever while the pointer sat still. */
static bool viewer_pointer_moved(Viewer::ViewerImpl *p)
{
	if (!p->m_pMediaControls) return true;
	GdkDisplay *disp = gtk_widget_get_display(p->m_pMediaControls);
	if (!disp) return true;
	GdkSeat *seat = gdk_display_get_default_seat(disp);
	if (!seat) return true;
	GdkDevice *dev = gdk_seat_get_pointer(seat);
	if (!dev) return true;
	double sx = 0., sy = 0.;
	GdkSurface *surf = gdk_device_get_surface_at_position(dev, &sx, &sy);
	if (NULL == surf)
	{
		/* Pointer not over any surface (e.g. outside the window). */
		p->m_bPointerPosValid = FALSE;
		return TRUE;
	}
	bool moved = !p->m_bPointerPosValid
		|| fabs(sx - p->m_dPointerRootX) >= 1.0
		|| fabs(sy - p->m_dPointerRootY) >= 1.0;
	p->m_dPointerRootX = sx;
	p->m_dPointerRootY = sy;
	p->m_bPointerPosValid = TRUE;
	return moved;
}

static void viewer_overlay_bar_enter_cb(GtkEventControllerMotion *controller, gdouble x, gdouble y, gpointer user_data)
{
	(void)controller; (void)x; (void)y;
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl *)user_data;
	p->m_bPointerOverOverlayBar = true;
	p->CancelControlsFade();
	viewer_controls_show(p);
	viewer_set_controls_opacity(p, 1.0);
	viewer_set_idle_cursor(p, false);
}

static void viewer_overlay_bar_leave_cb(GtkEventControllerMotion *controller, gpointer user_data)
{
	(void)controller;
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl *)user_data;
	p->m_bPointerOverOverlayBar = false;
	p->RefreshAutoHideTimer();
}

/* Connected on control buttons and the timeline scale via motion
 * controllers so the bar stays visible while hovering. */
static void controls_show_on_event_cb(GtkEventControllerMotion *controller, gdouble x, gdouble y, gpointer user_data)
{
	(void)controller; (void)x; (void)y;
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl *)user_data;
	if (!viewer_pointer_moved(p))
	{
		return;
	}
	gboolean was_hidden = viewer_controls_need_reshow(p);
	if (was_hidden)
	{
		viewer_controls_show(p);
		p->StartControlsFade(true);
		if (p->IsVideo())
		{
			p->UpdateTimelineVisibility();
		}
	}
	/* hovering a control button counts as pointer activity too */
	viewer_set_idle_cursor(p, false);
	p->RefreshAutoHideTimer();
}

/* The zoom-fit split control is a single button to the eye: while the pointer is
 * anywhere in it the whole box gets a highlight (including the separator, which
 * has no hover of its own) and the half under the pointer adds a second, stronger
 * one from the :hover rule on its child button. */
static void viewer_split_button_enter_cb(GtkEventControllerMotion *controller, gdouble x, gdouble y, gpointer user_data)
{
	(void)controller; (void)x; (void)y;
	GtkWidget *box = GTK_WIDGET(user_data);
	if (GTK_IS_WIDGET(box))
		gtk_widget_add_css_class(box, "view-mode-split-hovered");
}

static void viewer_split_button_leave_cb(GtkEventControllerMotion *controller, gpointer user_data)
{
	(void)controller;
	GtkWidget *box = GTK_WIDGET(user_data);
	if (GTK_IS_WIDGET(box))
		gtk_widget_remove_css_class(box, "view-mode-split-hovered");
}

static void
viewer_motion_notify(GtkEventControllerMotion *controller, gdouble x, gdouble y, gpointer user_data)
{
	GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(controller));
	(void)x; (void)y;
	Viewer::ViewerImpl *pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)user_data;
	if ((widget == pViewerImpl->m_pVideoFixed || widget == pViewerImpl->m_pVideoSinkWidget)
		&& pViewerImpl->m_bVideoPanning)
	{
		/* drag-to-pan: move the viewport so the video follows the pointer. */
		if (ABS(x - pViewerImpl->m_dVideoPanStartRootX) >= 5.
			|| ABS(y - pViewerImpl->m_dVideoPanStartRootY) >= 5.)
		{
			pViewerImpl->m_bVideoPanSlowdownInterrupted = false;
		}
		gdouble srcPerPx = (pViewerImpl->m_dVideoZoom > 0.) ? (1. / pViewerImpl->m_dVideoZoom) : 1.;
		pViewerImpl->SetVideoPanPixels(
			pViewerImpl->m_dVideoPanStartPX - (x - pViewerImpl->m_dVideoPanStartRootX) * srcPerPx,
			pViewerImpl->m_dVideoPanStartPY - (y - pViewerImpl->m_dVideoPanStartRootY) * srcPerPx);

		gint64 now = g_get_monotonic_time();
		if (pViewerImpl->m_iVideoPanLastMotionTime > 0)
		{
			gdouble dt = (gdouble)(now - pViewerImpl->m_iVideoPanLastMotionTime) / 1000000.0;
			if (dt >= 0.005)
			{
				gdouble dx = x - pViewerImpl->m_dVideoPanLastMotionX;
				gdouble dy = y - pViewerImpl->m_dVideoPanLastMotionY;

				const gdouble max_vel = 12000.0;
				gdouble dist = std::hypot(dx, dy);
				if (dist > max_vel * dt && dist > 0.)
				{
					dx = (dx / dist) * (max_vel * dt);
					dy = (dy / dist) * (max_vel * dt);
				}

				pViewerImpl->RecordVideoPanSample(dx, dy, dt);
				pViewerImpl->m_dVideoPanLastMotionX = x;
				pViewerImpl->m_dVideoPanLastMotionY = y;
				pViewerImpl->m_iVideoPanLastMotionTime = now;
			}
		}
		else
		{
			pViewerImpl->m_dVideoPanLastMotionX = x;
			pViewerImpl->m_dVideoPanLastMotionY = y;
			pViewerImpl->m_iVideoPanLastMotionTime = now;
		}

		pViewerImpl->ApplyVideoZoom();
		pViewerImpl->RefreshAutoHideTimer();
		/* dragging needs the pointer visible */
		viewer_set_idle_cursor(pViewerImpl, false);
		return;
	}

	if (widget == pViewerImpl->m_pImageView || widget == pViewerImpl->m_pVideoFixed
		|| widget == pViewerImpl->m_pVideoSinkWidget)
	{
		if (!viewer_pointer_moved(pViewerImpl))
		{
			return;
		}

		/* motion anywhere over the viewer brings the pointer back */
		viewer_set_idle_cursor(pViewerImpl, false);

		if (0 != pViewerImpl->m_iTimeoutMouseMotionNotify)
		{
			g_source_remove(pViewerImpl->m_iTimeoutMouseMotionNotify);
			pViewerImpl->m_iTimeoutMouseMotionNotify = 0;
		}

		if (0 != pViewerImpl->m_ImageListPtr->GetSize())
		{
			if (viewer_controls_need_reshow(pViewerImpl))
			{
				viewer_controls_show(pViewerImpl);
				pViewerImpl->StartControlsFade(true);
				if (pViewerImpl->IsVideo())
				{
					pViewerImpl->UpdateTimelineVisibility();
				}
			}
		}

		pViewerImpl->RefreshAutoHideTimer();
	}
}

/* ── GTK4 Gesture Controllers: pinch-to-zoom, two-finger pan, swipe/flick ── */

static void viewer_gesture_zoom_begin_cb(GtkGesture *gesture, GdkEventSequence *sequence, gpointer user_data)
{
	(void)gesture; (void)sequence;
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl *)user_data;
	p->m_dGestureLastScale = 1.0;
	p->RefreshAutoHideTimer();
}

static void viewer_gesture_zoom_scale_changed_cb(GtkGestureZoom *gesture, gdouble scale, gpointer user_data)
{
	(void)gesture;
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl *)user_data;
	if (scale <= 0.0)
		return;

	double factor = (p->m_dGestureLastScale > 0.0) ? (scale / p->m_dGestureLastScale) : 1.0;
	p->m_dGestureLastScale = scale;

	factor = CLAMP(factor, 0.5, 2.0);

	if (p->IsVideo())
	{
		p->SetVideoZoom(p->m_dVideoZoomFinal * factor);
	}
	else
	{
		QuiverImageViewMode zoom_mode = quiver_image_view_get_view_mode(QUIVER_IMAGE_VIEW(p->m_pImageView));
		if (!viewer_view_mode_is_zoomed(zoom_mode))
		{
			quiver_image_view_set_view_mode(QUIVER_IMAGE_VIEW(p->m_pImageView), QUIVER_IMAGE_VIEW_MODE_ZOOM);
		}
		quiver_image_view_zoom_by(QUIVER_IMAGE_VIEW(p->m_pImageView), factor);
	}

	p->RefreshAutoHideTimer();
}

static void viewer_gesture_zoom_end_cb(GtkGesture *gesture, GdkEventSequence *sequence, gpointer user_data)
{
	(void)gesture; (void)sequence;
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl *)user_data;
	p->m_dGestureLastScale = 1.0;
	p->RefreshAutoHideTimer();
}

static void viewer_two_finger_pan_begin_cb(GtkGestureDrag *gesture, gdouble start_x, gdouble start_y, gpointer user_data)
{
	(void)gesture; (void)start_x; (void)start_y;
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl *)user_data;
	if (p->IsVideo())
	{
		p->StopVideoPanSlowdown();
		p->m_dTwoFingerPanStartVidX = p->m_dVideoPanX;
		p->m_dTwoFingerPanStartVidY = p->m_dVideoPanY;
		p->m_dTwoFingerPanLastOffsetX = 0.;
		p->m_dTwoFingerPanLastOffsetY = 0.;
		p->m_iTwoFingerPanLastTime = g_get_monotonic_time();
		p->m_iVideoPanSampleCount = 0;
	}
	else
	{
		GtkAdjustment *hadj = quiver_image_view_get_hadjustment(QUIVER_IMAGE_VIEW(p->m_pImageView));
		GtkAdjustment *vadj = quiver_image_view_get_vadjustment(QUIVER_IMAGE_VIEW(p->m_pImageView));
		p->m_dTwoFingerPanStartHAdj = hadj ? gtk_adjustment_get_value(hadj) : 0.0;
		p->m_dTwoFingerPanStartVAdj = vadj ? gtk_adjustment_get_value(vadj) : 0.0;
	}
	p->RefreshAutoHideTimer();
}

static void viewer_two_finger_pan_update_cb(GtkGestureDrag *gesture, gdouble offset_x, gdouble offset_y, gpointer user_data)
{
	(void)gesture;
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl *)user_data;
	if (p->IsVideo())
	{
		gdouble srcPerPx = (p->m_dVideoZoom > 0.) ? (1. / p->m_dVideoZoom) : 1.;
		p->SetVideoPanPixels(
			p->m_dTwoFingerPanStartVidX - offset_x * srcPerPx,
			p->m_dTwoFingerPanStartVidY - offset_y * srcPerPx);
		p->ApplyVideoZoom();

		gint64 now = g_get_monotonic_time();
		if (p->m_iTwoFingerPanLastTime > 0)
		{
			gdouble dt = (gdouble)(now - p->m_iTwoFingerPanLastTime) / 1000000.0;
			if (dt >= 0.005)
			{
				gdouble dx = offset_x - p->m_dTwoFingerPanLastOffsetX;
				gdouble dy = offset_y - p->m_dTwoFingerPanLastOffsetY;

				const gdouble max_vel = 12000.0;
				gdouble dist = std::hypot(dx, dy);
				if (dist > max_vel * dt && dist > 0.)
				{
					dx = (dx / dist) * (max_vel * dt);
					dy = (dy / dist) * (max_vel * dt);
				}

				p->RecordVideoPanSample(dx, dy, dt);
				p->m_dTwoFingerPanLastOffsetX = offset_x;
				p->m_dTwoFingerPanLastOffsetY = offset_y;
				p->m_iTwoFingerPanLastTime = now;
			}
		}
		else
		{
			p->m_dTwoFingerPanLastOffsetX = offset_x;
			p->m_dTwoFingerPanLastOffsetY = offset_y;
			p->m_iTwoFingerPanLastTime = now;
		}
	}
	else
	{
		if (viewer_view_mode_is_zoomed(quiver_image_view_get_view_mode(QUIVER_IMAGE_VIEW(p->m_pImageView))))
		{
			GtkAdjustment *hadj = quiver_image_view_get_hadjustment(QUIVER_IMAGE_VIEW(p->m_pImageView));
			GtkAdjustment *vadj = quiver_image_view_get_vadjustment(QUIVER_IMAGE_VIEW(p->m_pImageView));
			if (hadj)
			{
				double new_val = p->m_dTwoFingerPanStartHAdj - offset_x;
				double upper = gtk_adjustment_get_upper(hadj) - gtk_adjustment_get_page_size(hadj);
				new_val = CLAMP(new_val, gtk_adjustment_get_lower(hadj), MAX(gtk_adjustment_get_lower(hadj), upper));
				gtk_adjustment_set_value(hadj, new_val);
			}
			if (vadj)
			{
				double new_val = p->m_dTwoFingerPanStartVAdj - offset_y;
				double upper = gtk_adjustment_get_upper(vadj) - gtk_adjustment_get_page_size(vadj);
				new_val = CLAMP(new_val, gtk_adjustment_get_lower(vadj), MAX(gtk_adjustment_get_lower(vadj), upper));
				gtk_adjustment_set_value(vadj, new_val);
			}
		}
	}
	p->RefreshAutoHideTimer();
}

static void viewer_two_finger_pan_end_cb(GtkGestureDrag *gesture, gdouble offset_x, gdouble offset_y, gpointer user_data)
{
	(void)gesture; (void)offset_x; (void)offset_y;
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl *)user_data;
	if (p->IsVideo())
	{
		gint64 now = g_get_monotonic_time();
		if (p->m_iTwoFingerPanLastTime > 0)
		{
			gdouble dt = (gdouble)(now - p->m_iTwoFingerPanLastTime) / 1000000.0;
			if (dt >= 0.005)
			{
				gdouble dx = offset_x - p->m_dTwoFingerPanLastOffsetX;
				gdouble dy = offset_y - p->m_dTwoFingerPanLastOffsetY;
				const gdouble max_vel = 12000.0;
				gdouble dist = std::hypot(dx, dy);
				if (dist > max_vel * dt && dist > 0.)
				{
					dx = (dx / dist) * (max_vel * dt);
					dy = (dy / dist) * (max_vel * dt);
				}
				p->RecordVideoPanSample(dx, dy, dt);
				p->m_dTwoFingerPanLastOffsetX = offset_x;
				p->m_dTwoFingerPanLastOffsetY = offset_y;
				p->m_iTwoFingerPanLastTime = now;
			}
		}

		gdouble time_since_motion = (p->m_iTwoFingerPanLastTime > 0)
			? (gdouble)(now - p->m_iTwoFingerPanLastTime) / 1000000.0
			: 1.0;

		if (p->m_bKineticScrolling &&
			time_since_motion < 0.15 &&
			p->m_iVideoPanSampleCount > 0)
		{
			p->StartVideoPanSlowdown();
		}
		else
		{
			p->StopVideoPanSlowdown();
		}
	}
	p->RefreshAutoHideTimer();
}

static void viewer_swipe_cb(GtkGestureSwipe *gesture, gdouble velocity_x, gdouble velocity_y, gpointer user_data)
{
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl *)user_data;
	if (NULL == p || 0 == p->m_ImageListPtr->GetSize())
		return;

	/* Ignore mouse, touchpad, or other non-touchscreen events */
	GdkDevice *dev = gtk_gesture_get_device(GTK_GESTURE(gesture));
	if (dev == NULL)
	{
		GdkEvent *ev = gtk_event_controller_get_current_event(GTK_EVENT_CONTROLLER(gesture));
		if (ev != NULL)
			dev = gdk_event_get_device(ev);
	}
	if (dev != NULL && gdk_device_get_source(dev) != GDK_SOURCE_TOUCHSCREEN)
	{
		return;
	}

	/* Disable swiping when zoomed in: dragging must only pan the image */
	if (p->m_pImageView && QUIVER_IS_IMAGE_VIEW(p->m_pImageView))
	{
		QuiverImageView *iv = QUIVER_IMAGE_VIEW(p->m_pImageView);
		QuiverImageViewMode mode = quiver_image_view_get_view_mode(iv);
		if (viewer_view_mode_is_zoomed(mode) || mode == QUIVER_IMAGE_VIEW_MODE_ACTUAL_SIZE)
		{
			return;
		}
	}
	if (p->IsVideo() && p->CanVideoPan())
	{
		return;
	}

	const double kSwipeThreshold = 120.0;

	if (fabs(velocity_x) >= fabs(velocity_y))
	{
		if (velocity_x < -kSwipeThreshold)
		{
			GAction *act = QuiverUtils::GetAction(ACTION_VIEWER_NEXT);
			if (act) g_action_activate(act, NULL);
		}
		else if (velocity_x > kSwipeThreshold)
		{
			GAction *act = QuiverUtils::GetAction(ACTION_VIEWER_PREVIOUS);
			if (act) g_action_activate(act, NULL);
		}
	}
	else
	{
		if (velocity_y < -kSwipeThreshold)
		{
			// Flick up: toggle fullscreen
			GAction *fs = QuiverUtils::GetAction("FullScreen");
			if (fs) g_action_activate(fs, NULL);
		}
		else if (velocity_y > kSwipeThreshold)
		{
			// Flick down: exit fullscreen if fullscreen
			GtkRoot *root = gtk_widget_get_root(p->m_pHBox);
			bool bFS = root && GTK_IS_WINDOW(root) && gtk_window_is_fullscreen(GTK_WINDOW(root));
			if (bFS)
			{
				GAction *fs = QuiverUtils::GetAction("FullScreen");
				if (fs) g_action_activate(fs, NULL);
			}
		}
	}
	p->RefreshAutoHideTimer();
}

/* ── Overlay button callbacks ── */

static void viewer_overlay_prev_cb(Viewer::ViewerImpl *p)
{
	(void)p;
	GAction *act = QuiverUtils::GetAction(ACTION_VIEWER_PREVIOUS);
	if (act) g_action_activate(act, NULL);
}

static void viewer_overlay_next_cb(Viewer::ViewerImpl *p)
{
	(void)p;
	GAction *act = QuiverUtils::GetAction(ACTION_VIEWER_NEXT);
	if (act) g_action_activate(act, NULL);
}

static void viewer_overlay_slideshow_cb(Viewer::ViewerImpl *p)
{
	if (!p || !p->m_pViewer) return;
	gboolean want_active = !p->m_bSlideShowRunning;
	QuiverUtils::ToggleActionSetState(ACTION_VIEWER_SLIDESHOW, !want_active);
	GAction *act = QuiverUtils::GetAction(ACTION_VIEWER_SLIDESHOW);
	if (act)
	{
		g_action_activate(act, NULL);
		return;
	}
	if (!p->m_pViewer) return;
	if (p->m_bSlideShowRunning)
	{
		p->m_pViewer->SlideShowStop();
	}
	else
	{
		p->m_pViewer->SlideShowStart();
	}
}

static void viewer_overlay_zoom_out_cb(Viewer::ViewerImpl *p)
{
	if (p->IsVideo())
	{
		p->m_bVideoZoomAnchorCenter = true;
	}
	else
	{
		quiver_image_view_set_zoom_anchor_center(QUIVER_IMAGE_VIEW(p->m_pImageView), TRUE);
	}
	GAction *act = QuiverUtils::GetAction(ACTION_VIEWER_ZOOM_OUT);
	if (act) g_action_activate(act, NULL);
}

static void viewer_overlay_zoom_fit_cb(Viewer::ViewerImpl *p)
{
	(void)p;
	/* The HUD zoom-fit button applies the default fit mode, which is
	 * "fit window, stretched". */
	GAction *act = QuiverUtils::GetAction(ACTION_VIEWER_ZOOM_FIT_STRETCH);
	if (act) g_action_activate(act, NULL);
}

static void viewer_overlay_zoom_in_cb(Viewer::ViewerImpl *p)
{
	if (p->IsVideo())
	{
		p->m_bVideoZoomAnchorCenter = true;
	}
	else
	{
		quiver_image_view_set_zoom_anchor_center(QUIVER_IMAGE_VIEW(p->m_pImageView), TRUE);
	}
	GAction *act = QuiverUtils::GetAction(ACTION_VIEWER_ZOOM_IN);
	if (act) g_action_activate(act, NULL);
}

static void viewer_overlay_rotate_ccw_cb(Viewer::ViewerImpl *p)
{
	(void)p;
	GAction *act = QuiverUtils::GetAction(ACTION_VIEWER_ROTATE_CCW);
	if (act) g_action_activate(act, NULL);
}

static void viewer_overlay_rotate_cw_cb(Viewer::ViewerImpl *p)
{
	(void)p;
	GAction *act = QuiverUtils::GetAction(ACTION_VIEWER_ROTATE_CW);
	if (act) g_action_activate(act, NULL);
}


static void viewer_overlay_fullscreen_cb(Viewer::ViewerImpl *p)
{
	(void)p;
	GAction *fs = QuiverUtils::GetAction("FullScreen");
	if (fs) g_action_activate(fs, NULL);
}

static void viewer_radio_action_handler_cb(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	viewer_action_handler_cb(action, parameter, user_data);
}

static Viewer::ViewerImpl *s_pLastRegisteredViewerImpl = NULL;

static void viewer_action_handler_cb(GSimpleAction *action, GVariant *parameter, gpointer data)
{ (void)parameter; 
	Viewer::ViewerImpl *pViewerImpl = (Viewer::ViewerImpl*)data;
	if (pViewerImpl == NULL || pViewerImpl != s_pLastRegisteredViewerImpl)
	{
		return;
	}
	
	QuiverImageView *imageview = QUIVER_IMAGE_VIEW(pViewerImpl->m_pImageView);

	PreferencesPtr prefsPtr = Preferences::GetInstance();
	
	//printf("Viewer Action: %s\n",g_action_get_name(G_ACTION(action)));
	
	const gchar * szAction = g_action_get_name(G_ACTION(action));
	
	if (0 == strcmp(szAction, ACTION_VIEWER_ZOOM_FIT)
		|| 0 == strcmp(szAction, ACTION_VIEWER_ZOOM_FIT_STRETCH)
		|| 0 == strcmp(szAction, ACTION_VIEWER_ZOOM_100)
		|| 0 == strcmp(szAction, ACTION_VIEWER_ZOOM_FILL_SCREEN)
		|| 0 == strcmp(szAction, ACTION_VIEWER_ZOOM)
		|| 0 == strcmp(szAction, ACTION_VIEWER_ZOOM_KEEP))
	{
		QuiverImageViewMode zoom_mode = (QuiverImageViewMode)QuiverUtils::GetRadioActionCurrent(szAction);
		if (pViewerImpl->IsVideo())
		{
			if (pViewerImpl->m_iVideoZoomTimeoutID != 0)
			{
				g_source_remove(pViewerImpl->m_iVideoZoomTimeoutID);
				pViewerImpl->m_iVideoZoomTimeoutID = 0;
			}
			/* the mode is the viewer's, so the video is shown in the mode the
			 * stills are: "keep zoom and pan" keeps the video's zoom and pan
			 * too, exactly as it keeps an image's */
			pViewerImpl->SetVideoViewMode(zoom_mode);
			/* re-center the visible viewport when changing modes during
			 * playback, just like the first ApplyVideoZoom after a reset */
			pViewerImpl->m_dVideoLastWidgetW = 0.;
			pViewerImpl->m_dVideoLastFrameZoom = 0.;
			pViewerImpl->ApplyVideoZoom();
			pViewerImpl->UpdateUI();
			if (!pViewerImpl->IsPlaying())
			{
				gint64 pos = 0;
				if (gst_element_query_position(GST_ELEMENT(pViewerImpl->m_pPipeline), GST_FORMAT_TIME, &pos))
				{
					gdouble speed = (pViewerImpl->m_dPlaybackSpeed > 0.0) ? pViewerImpl->m_dPlaybackSpeed : 1.0;
					gst_element_seek(GST_ELEMENT(pViewerImpl->m_pPipeline),
						speed,
						GST_FORMAT_TIME,
						GstSeekFlags(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT),
						GST_SEEK_TYPE_SET, pos,
						GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);
				}
			}
		}
		else
		{
			/* the same setter for a still, so the mode picked over an image is
			 * the mode the next video is shown in */
			pViewerImpl->SetVideoViewMode(zoom_mode);
		}
	}
	else if (0 == strcmp(szAction, ACTION_VIEWER_ZOOM_IN)
	)
	{
		if (pViewerImpl->IsVideo())
		{
			pViewerImpl->SetVideoZoom(pViewerImpl->m_dVideoZoomFinal * 1.25);
			return;
		}

		if (!viewer_view_mode_is_zoomed(quiver_image_view_get_view_mode(imageview)))
			quiver_image_view_set_view_mode(imageview,QUIVER_IMAGE_VIEW_MODE_ZOOM);

		quiver_image_view_set_magnification(imageview,
						quiver_image_view_get_magnification(imageview)*1.25);
	}
	else if (0 == strcmp(szAction, ACTION_VIEWER_ZOOM_OUT)
	)
	{
		if (pViewerImpl->IsVideo())
		{
			if (pViewerImpl->m_dVideoZoomFinal > pViewerImpl->m_dVideoZoomMin + 0.005)
			{
				pViewerImpl->SetVideoZoom(pViewerImpl->m_dVideoZoomFinal / 1.25);
			}
			return;
		}

		if (!viewer_view_mode_is_zoomed(quiver_image_view_get_view_mode(imageview)))
			quiver_image_view_set_view_mode(imageview,QUIVER_IMAGE_VIEW_MODE_ZOOM);

		quiver_image_view_set_magnification(imageview,
			quiver_image_view_get_magnification(imageview)/1.25);
	}
	else if (0 == strcmp(szAction,ACTION_VIEWER_ROTATE_CW) || 0 == strcmp(szAction,ACTION_VIEWER_ROTATE_CW_2))
	{
		if (pViewerImpl->IsVideo())
		{
			pViewerImpl->RotateVideo(true);
		}
		else
		{
			quiver_image_view_rotate(imageview,TRUE);
			if (pViewerImpl->m_pNavigationControl)
				quiver_navigation_control_rotate(QUIVER_NAVIGATION_CONTROL(pViewerImpl->m_pNavigationControl), TRUE);
			pViewerImpl->SetCurrentOrientation( orientation_matrix[ORIENTATION_ROTATE_CW][pViewerImpl->GetCurrentOrientation()] );
			if (pViewerImpl->m_ImageListPtr && pViewerImpl->m_ImageListPtr->GetSize() > 0)
			{
				QuiverFile f = pViewerImpl->m_ImageListPtr->GetCurrent();
				if (f.GetURI())
					QuiverFileOps::UndoStackRecordRotate(f.GetURI(), +1);
			}
		}
	}
	else if (0 == strcmp(szAction,ACTION_VIEWER_ROTATE_CCW) || 0 == strcmp(szAction,ACTION_VIEWER_ROTATE_CCW_2))
	{
		if (pViewerImpl->IsVideo())
		{
			pViewerImpl->RotateVideo(false);
		}
		else
		{
			quiver_image_view_rotate(imageview,FALSE);
			if (pViewerImpl->m_pNavigationControl)
				quiver_navigation_control_rotate(QUIVER_NAVIGATION_CONTROL(pViewerImpl->m_pNavigationControl), FALSE);
			pViewerImpl->SetCurrentOrientation( orientation_matrix[ORIENTATION_ROTATE_CCW][pViewerImpl->GetCurrentOrientation()] );
			if (pViewerImpl->m_ImageListPtr && pViewerImpl->m_ImageListPtr->GetSize() > 0)
			{
				QuiverFile f = pViewerImpl->m_ImageListPtr->GetCurrent();
				if (f.GetURI())
					QuiverFileOps::UndoStackRecordRotate(f.GetURI(), -1);
			}
		}
	}
	else if (0 == strcmp(szAction,ACTION_VIEWER_FLIP_H) || 0 == strcmp(szAction,ACTION_VIEWER_FLIP_H_2))
	{
		quiver_image_view_flip(imageview,TRUE);
		if (pViewerImpl->m_pNavigationControl)
			quiver_navigation_control_flip(QUIVER_NAVIGATION_CONTROL(pViewerImpl->m_pNavigationControl), TRUE);
		pViewerImpl->SetCurrentOrientation( orientation_matrix[ORIENTATION_FLIP_H][pViewerImpl->GetCurrentOrientation()] );
	}
	else if (0 == strcmp(szAction,ACTION_VIEWER_FLIP_V) || 0 == strcmp(szAction,ACTION_VIEWER_FLIP_V_2))
	{
		quiver_image_view_flip(imageview,FALSE);
		if (pViewerImpl->m_pNavigationControl)
			quiver_navigation_control_flip(QUIVER_NAVIGATION_CONTROL(pViewerImpl->m_pNavigationControl), FALSE);
		pViewerImpl->SetCurrentOrientation( orientation_matrix[ORIENTATION_FLIP_V][pViewerImpl->GetCurrentOrientation()] );
	}
	else if (0 == strcmp(szAction,ACTION_VIEWER_VIDEO_PLAY) || 0 == strcmp(szAction,ACTION_VIEWER_VIDEO_PLAY_2))
	{
		if (pViewerImpl->IsVideo())
		{
			/* On a video the play/pause keys toggle video playback directly,
			 * even mid-slideshow (this is what makes playing during the
			 * manual-nav grace countdown work).  If the slideshow itself is
			 * paused, resume the whole show — SlideShowResume() restarts the
			 * stopped video and the show's machine together. */
			if (pViewerImpl->m_pViewer && pViewerImpl->m_pViewer->IsSlideShowPaused())
			{
				pViewerImpl->m_pViewer->SlideShowResume();
			}
			else
			{
				pViewerImpl->PlayPauseVideo();
			}
		}
		else if (pViewerImpl->m_pViewer && pViewerImpl->m_pViewer->IsSlideShowRunning())
		{
			/* On an image during a slideshow the keys pause/resume the show. */
			pViewerImpl->m_pViewer->SlideShowTogglePause();
			pViewerImpl->TriggerPlayPauseAnimation(!pViewerImpl->m_bSlideShowPaused);
		}
		else
		{
			pViewerImpl->PlayPauseVideo();
		}
	}
	else if (0 == strcmp(szAction,ACTION_VIEWER_VIDEO_SKIP_FORWARD))
	{
		pViewerImpl->SkipForward();
	}
	else if (0 == strcmp(szAction,ACTION_VIEWER_VIDEO_SKIP_BACK))
	{
		pViewerImpl->SkipBack();
	}
	else if (0 == strcmp(szAction,ACTION_VIEWER_VIDEO_SEEK_FWD_5))
	{
		pViewerImpl->SeekRelative(5);
	}
	else if (0 == strcmp(szAction,ACTION_VIEWER_VIDEO_SEEK_BACK_5))
	{
		pViewerImpl->SeekRelative(-5);
	}
	else if (0 == strcmp(szAction,ACTION_VIEWER_VIDEO_FRAME_FWD))
	{
		viewer_frame_step_fwd_cb(pViewerImpl);
	}
	else if (0 == strcmp(szAction,ACTION_VIEWER_VIDEO_FRAME_BACK))
	{
		viewer_frame_step_back_cb(pViewerImpl);
	}
	else if (0 == strcmp(szAction,ACTION_VIEWER_VIDEO_SNAPSHOT))
	{
		pViewerImpl->Snapshot();
	}
	else if (0 == strcmp(szAction, ACTION_VIEWER_VIDEO_MUTE))
	{
		pViewerImpl->ToggleMute();
	}
	else if (g_str_has_prefix(szAction, "VideoSpeed"))
	{
		int idx = QuiverUtils::GetRadioActionCurrent(szAction);
		double speeds[] = { 0.25, 0.5, 1.0, 1.5, 2.0, 4.0, 8.0, 16.0 };
		if (idx >= 0 && idx < 8)
		{
			pViewerImpl->SetPlaybackSpeed(speeds[idx]);
		}
	}
	else if (0 == strcmp(szAction, ACTION_VIEWER_FIRST))
	{
		pViewerImpl->m_ImageListPtr->First();
	}
	else if (0 == strcmp(szAction, ACTION_VIEWER_PREVIOUS) || 0 == strcmp(szAction, ACTION_VIEWER_PREVIOUS_2)) 
	{
		pViewerImpl->m_ImageListPtr->Previous();
	}
	else if (0 == strcmp(szAction, ACTION_VIEWER_NEXT) || 0 == strcmp(szAction, ACTION_VIEWER_NEXT_2)) 
	{
		pViewerImpl->m_ImageListPtr->Next();
	}
	else if (0 == strcmp(szAction, ACTION_VIEWER_LAST))
	{
		pViewerImpl->m_ImageListPtr->Last();
	}
	else if (0 == strcmp(szAction, ACTION_VIEWER_VIEW_FILM_STRIP))
	{

		if( QuiverUtils::ToggleActionGetActive(g_action_get_name(G_ACTION(action))) )
		{
			if (pViewerImpl->m_bFilmstripOverlay)
			{
				pViewerImpl->ShowFilmstripOverlay();
				pViewerImpl->ScheduleFilmstripHide();
			}
			else
			{
				gtk_widget_set_visible(pViewerImpl->m_pIconView, TRUE);
			}
		}
		else
		{
			if (pViewerImpl->m_bFilmstripOverlay)
			{
				pViewerImpl->CancelFilmstripHide();
				pViewerImpl->HideFilmstripOverlay();
			}
			else
			{
				/* never leave a stale overlay-mode fade hiding it */
				pViewerImpl->CancelFilmstripFade();
				gtk_widget_set_visible(pViewerImpl->m_pIconView, FALSE);
			}
		}

		// set preference
		if (0 != pViewerImpl->m_iTimeoutSlideshowID)
		{
			// in slideshow
			prefsPtr->SetBoolean(QUIVER_PREFS_SLIDESHOW, QUIVER_PREFS_SLIDESHOW_FILMSTRIP_HIDE, !QuiverUtils::ToggleActionGetActive(g_action_get_name(G_ACTION(action))));
		}
		else
		{
			prefsPtr->SetBoolean(QUIVER_PREFS_VIEWER,QUIVER_PREFS_VIEWER_FILMSTRIP_SHOW,QuiverUtils::ToggleActionGetActive(g_action_get_name(G_ACTION(action))));
		}
	}
	else if (0 == strcmp(szAction, ACTION_VIEWER_TRASH)
			|| 0 == strcmp(szAction, ACTION_VIEWER_TRASH_FORCE))
	{
		bool bForce = (0 == strcmp(szAction, ACTION_VIEWER_TRASH_FORCE));

		if (0 == pViewerImpl->m_ImageListPtr->GetSize())
		{
			/* Nothing loaded; pressing Delete with an empty list would crash
			 * GetCurrent() below. */
			return;
		}

		QuiverFile f = pViewerImpl->m_ImageListPtr->GetCurrent();

		if (QuiverFileOps::IsTrashURI(f.GetURI()))
		{
			/* Browsing the trash: Delete removes the file permanently.
			 * Confirm unless the user held Shift. */
			bool bProceed = bForce;
			if (!bProceed)
			{
				std::string strDlgText = "Permanently delete this item from the trash?";
				bProceed = QuiverUtils::ConfirmDialog("Delete Permanently?",
					strDlgText, "Delete Permanently", "Cancel");
			}
			if (bProceed)
			{
				if (QuiverFileOps::PermanentlyDeleteTrashItem(f))
				{
					pViewerImpl->m_ImageListPtr->Remove(pViewerImpl->m_ImageListPtr->GetCurrentIndex());
					pViewerImpl->SetImageIndex(pViewerImpl->m_ImageListPtr->GetCurrentIndex(),true);
				}
			}
		}
		else
		{
			/* Normal folder: move to trash without asking, then allow undo. */
			if (QuiverFileOps::MoveToTrash(f))
			{
				std::list<QuiverFile> trashed;
				trashed.push_back(f);
				QuiverFileOps::UndoStackRecord(trashed);

				pViewerImpl->m_ImageListPtr->Remove(pViewerImpl->m_ImageListPtr->GetCurrentIndex());
				pViewerImpl->SetImageIndex(pViewerImpl->m_ImageListPtr->GetCurrentIndex(),true);
			}
		}
	}
	else if (0 == strcmp(szAction, ACTION_VIEWER_RESTORE))
	{
		/* Restore the current item from the trash to its original location. */
		if (0 == pViewerImpl->m_ImageListPtr->GetSize())
		{
			return;
		}

		QuiverFile f = pViewerImpl->m_ImageListPtr->GetCurrent();
		if (QuiverFileOps::IsTrashURI(f.GetURI()))
		{
			if (QuiverFileOps::RestoreTrashItem(f))
			{
				pViewerImpl->m_ImageListPtr->Remove(pViewerImpl->m_ImageListPtr->GetCurrentIndex());
				pViewerImpl->SetImageIndex(pViewerImpl->m_ImageListPtr->GetCurrentIndex(),true);
			}
		}
	}
	else if (0 == strcmp(szAction, ACTION_VIEWER_COPY))
	{
		if (0 == pViewerImpl->m_ImageListPtr->GetSize())
			return;

		QuiverFile f = pViewerImpl->m_ImageListPtr->GetCurrent();
		std::list<std::string> uris;
		uris.push_back(f.GetURI());
		QuiverFileOps::ClipboardSet(uris, false);
		QuiverClipboard::SetClipboard(uris, false);
	}
	else if (0 == strcmp(szAction, ACTION_VIEWER_CUT))
	{
		if (0 == pViewerImpl->m_ImageListPtr->GetSize())
			return;

		/* Wire the (previously dead) Ctrl+X: cut the shown file so a later
		 * Paste in the browser / Paste-Into-Folder moves it. */
		QuiverFile f = pViewerImpl->m_ImageListPtr->GetCurrent();
		std::list<std::string> uris;
		uris.push_back(f.GetURI());
		QuiverFileOps::ClipboardSet(uris, true);
		QuiverClipboard::SetClipboard(uris, true);
	}
	else if (0 == strcmp(szAction, ACTION_VIEWER_ROTATE_FOR_BEST_FIT))
	{
		if (pViewerImpl->m_ImageListPtr->GetSize())
		{
			QuiverFile f = pViewerImpl->m_ImageListPtr->GetCurrent();

			int old_orientation = pViewerImpl->GetMaximizedOrientation(f);

			pViewerImpl->m_bMaximizeViewableArea = 
				(TRUE == QuiverUtils::ToggleActionGetActive(g_action_get_name(G_ACTION(action))));

			if (0 != pViewerImpl->m_iTimeoutSlideshowID)
			{
				prefsPtr->SetBoolean(QUIVER_PREFS_SLIDESHOW, 
						QUIVER_PREFS_SLIDESHOW_ROTATE_FOR_BEST_FIT, 
						pViewerImpl->m_bMaximizeViewableArea);
			}
			else
			{
				prefsPtr->SetBoolean(QUIVER_PREFS_VIEWER, 
						QUIVER_PREFS_VIEWER_ROTATE_FOR_BEST_FIT, 
						pViewerImpl->m_bMaximizeViewableArea);
			}


			int new_orientation = pViewerImpl->GetMaximizedOrientation(f);

			if (old_orientation != new_orientation)
			{
				//printf("#### got a reload message from the imageview\n");
				ImageLoader::LoadParams params = {};
				params.max_width = gtk_widget_get_width(pViewerImpl->m_pImageView);
				params.max_height = gtk_widget_get_height(pViewerImpl->m_pImageView);
				params.orientation = pViewerImpl->GetCurrentOrientation(true);
				params.reload = false;
				params.fullsize = false;
				params.no_thumb_preview = true;
				params.state = ImageLoader::LOAD;

				// reload new orientation
				pViewerImpl->m_ImageLoader.LoadImage(f,params);
			}
		}
		else
		{
			pViewerImpl->m_bMaximizeViewableArea = 
				(TRUE == QuiverUtils::ToggleActionGetActive(g_action_get_name(G_ACTION(action))));

			prefsPtr->SetBoolean(QUIVER_PREFS_VIEWER, 
					QUIVER_PREFS_VIEWER_ROTATE_FOR_BEST_FIT, 
					pViewerImpl->m_bMaximizeViewableArea);
		}
	}
}

 static gboolean viewer_scrollwheel_event(GtkEventControllerScroll *controller, gdouble dx, gdouble dy, gpointer data )
 {
 	Viewer::ViewerImpl *pViewerImpl;
 	pViewerImpl = (Viewer::ViewerImpl*)data;

  GdkModifierType state = gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(controller));

  gboolean bVertical = ABS(dy) >= ABS(dx);

 	/* One controller serves every viewer widget (image view, video fixed, GL
 	 * sink).  With no modifier it navigates (DISCRETE: each wheel notch
 	 * advances exactly one image, no accumulation).  With Ctrl held it zooms
 	 * (DISCRETE cleared: smooth continuous deltas) -- the video via the GL
 	 * pipeline crop, the image via the image view's own zoom. */
 	gboolean bZoom = (state & GDK_CONTROL_MASK) != 0;
 	GtkEventControllerScrollFlags flags = GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES;
 	if (!bZoom)
 		flags = (GtkEventControllerScrollFlags)(flags | GTK_EVENT_CONTROLLER_SCROLL_DISCRETE);
 	gtk_event_controller_scroll_set_flags(GTK_EVENT_CONTROLLER_SCROLL(controller), flags);

 	/* Delta in the dominant axis (same direction sense as dy). */
 	gdouble scroll_dir = bVertical ? dy : dx;

 	if (bZoom)
 	{
 		pViewerImpl->m_bVideoZoomAnchorCenter = false;
 		quiver_image_view_set_zoom_anchor_center(QUIVER_IMAGE_VIEW(pViewerImpl->m_pImageView), FALSE);
 		/* Scale smooth (touchpad) deltas to a notch-equivalent so the zoom is
 		 * proportional; WHEEL deltas are already whole notches. */
 		GdkScrollUnit unit = gtk_event_controller_scroll_get_unit(
 			GTK_EVENT_CONTROLLER_SCROLL(controller));
 		gdouble per_unit = (unit == GDK_SCROLL_UNIT_SURFACE)
 			? QUIVER_SCROLL_SURFACE_PER_NOTCH : 1.0;

 		/* scroll up (negative) zooms in; pow(~1.25, -delta) is continuous. */
 		gdouble amount = -scroll_dir * per_unit;

 		if (pViewerImpl->IsVideo())
 		{
 			pViewerImpl->SetVideoZoom(
 				pViewerImpl->m_dVideoZoomFinal * pow(1.25, amount));
 		}
  		else
  		{
			QuiverImageViewMode zoom_mode =
				quiver_image_view_get_view_mode(QUIVER_IMAGE_VIEW(pViewerImpl->m_pImageView));
			if (!viewer_view_mode_is_zoomed(zoom_mode))
			{
  			quiver_image_view_set_view_mode(
  				QUIVER_IMAGE_VIEW(pViewerImpl->m_pImageView),
  				QUIVER_IMAGE_VIEW_MODE_ZOOM);
			}
  			/* The scroll events for smooth/device scrolling carry no usable
  			 * position, so the image view derives the zoom anchor from the
  			 * pointer device itself (see set_magnification_full). */
  			quiver_image_view_zoom_by(QUIVER_IMAGE_VIEW(pViewerImpl->m_pImageView),
  				pow(1.25, amount));
  		}
 		return TRUE;
 	}

 	/* Navigation.  With DISCRETE on, GTK quantizes deltas to whole wheel
 	 * units.  (If a scroll event still arrives as smooth SURFACE -- e.g. the
 	 * event that raced the Ctrl release that disabled DISCRETE -- skip it;
 	 * the next event is quantized.) */
 	GdkScrollUnit unit = gtk_event_controller_scroll_get_unit(
 		GTK_EVENT_CONTROLLER_SCROLL(controller));

 	if (unit == GDK_SCROLL_UNIT_SURFACE)
 	{
 		/* Two-finger smooth pan on touchpad */
		/* The quick preview is the image view too, so a touchpad pan pans it the
		* same way it pans a still - until the first frame arrives, when the
		* video's own pan takes over. */
		gboolean bPanPreview = pViewerImpl->IsVideo() && !pViewerImpl->m_bVideoPreviewHasFrame;
		if ((!pViewerImpl->IsVideo() || bPanPreview) &&
			viewer_view_mode_is_zoomed(quiver_image_view_get_view_mode(QUIVER_IMAGE_VIEW(pViewerImpl->m_pImageView))))
 		{
 			GtkAdjustment *hadj = quiver_image_view_get_hadjustment(QUIVER_IMAGE_VIEW(pViewerImpl->m_pImageView));
 			GtkAdjustment *vadj = quiver_image_view_get_vadjustment(QUIVER_IMAGE_VIEW(pViewerImpl->m_pImageView));
 			if (hadj && dx != 0.0)
 			{
 				double val = gtk_adjustment_get_value(hadj) + dx;
 				double upper = gtk_adjustment_get_upper(hadj) - gtk_adjustment_get_page_size(hadj);
 				gtk_adjustment_set_value(hadj, CLAMP(val, gtk_adjustment_get_lower(hadj), MAX(gtk_adjustment_get_lower(hadj), upper)));
 			}
 			if (vadj && dy != 0.0)
 			{
 				double val = gtk_adjustment_get_value(vadj) + dy;
 				double upper = gtk_adjustment_get_upper(vadj) - gtk_adjustment_get_page_size(vadj);
 				gtk_adjustment_set_value(vadj, CLAMP(val, gtk_adjustment_get_lower(vadj), MAX(gtk_adjustment_get_lower(vadj), upper)));
 			}
 			pViewerImpl->RefreshAutoHideTimer();
 			return TRUE;
 		}
 		else if (pViewerImpl->CanVideoPan())
 		{
 			pViewerImpl->StopVideoPanSlowdown();
 			gdouble srcPerPx = (pViewerImpl->m_dVideoZoom > 0.) ? (1. / pViewerImpl->m_dVideoZoom) : 1.;
			pViewerImpl->SetVideoPanPixels(
				pViewerImpl->GetVideoPanOffsetX() + dx * srcPerPx,
				pViewerImpl->GetVideoPanOffsetY() + dy * srcPerPx);
 			pViewerImpl->ApplyVideoZoom();
 			pViewerImpl->RefreshAutoHideTimer();
 			return TRUE;
 		}
 		return FALSE;
 	}

 	if (unit != GDK_SCROLL_UNIT_WHEEL)
 		return FALSE;

 	gint navigate = 0;
 	if (scroll_dir >= 1.0)
 		navigate = (gint)scroll_dir;
 	else if (scroll_dir <= -1.0)
 		navigate = (gint)scroll_dir;

 	if (navigate < 0)
 	{
 		while (navigate++ < 0)
 		{
 			pViewerImpl->m_ImageListPtr->Previous();
 		}
 	}
 	else if (navigate > 0)
 	{
 		while (navigate-- > 0)
 		{
 			pViewerImpl->m_ImageListPtr->Next();
 		}
 	}
 	else
 	{
 		return FALSE;
 	}

 	return TRUE;
 }


static void viewer_imageview_activated(QuiverImageView *imageview,gpointer data)
{ (void)imageview; 
	Viewer::ViewerImpl *pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)data;
	pViewerImpl->m_pViewer->EmitItemActivatedEvent();
}

static void viewer_imageview_reload(QuiverImageView *imageview,gpointer data)
{ (void)imageview; 
	//printf("#### got a reload message from the imageview\n");
	Viewer::ViewerImpl *pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)data;
	ImageLoader::LoadParams params = {};

	params.orientation = pViewerImpl->GetCurrentOrientation(true);
	params.reload = true;
	params.fullsize = true;
	params.no_thumb_preview = true;
	params.state = ImageLoader::LOAD;

	pViewerImpl->m_ImageLoader.LoadImage(pViewerImpl->m_ImageListPtr->GetCurrent(),params);
}


static void viewer_imageview_magnification_changed(QuiverImageView *imageview,gpointer data)
{ (void)imageview; 
	Viewer::ViewerImpl *pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)data;

	double mag = quiver_image_view_get_magnification(QUIVER_IMAGE_VIEW(pViewerImpl->m_pImageView));
	if (pViewerImpl->m_StatusbarPtr)
	{
		pViewerImpl->m_StatusbarPtr->SetMagnification((int)(mag*100+.5));
	}
	
	pViewerImpl->UpdateUI();

}

static void viewer_imageview_view_mode_changed(QuiverImageView *imageview,gpointer data)
{ (void)imageview; 
	Viewer::ViewerImpl *pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)data;
	
	QuiverImageViewMode mode = quiver_image_view_get_view_mode(QUIVER_IMAGE_VIEW(pViewerImpl->m_pImageView));
	/* The image view drops into 1:1 internally whenever the magnification is
	 * not the one the mode in force would show the picture at, because that is
	 * the state it has to draw and scroll a zoomed picture from.  That state is
	 * the view's business and not the user's: the mode they chose is the one
	 * they are in, and everything that names it - the tick in the list, the
	 * stored preference, the copy the video is drawn with - has to name that
	 * one, or zooming looks like it changed the view mode.
	 *
	 * "Keep zoom and pan" is not that state: it is a mode the user picked, so it
	 * passes through as itself, and a video in it keeps its framing. */
	const gboolean bInternalZoomOnly = (mode == QUIVER_IMAGE_VIEW_MODE_ZOOM);
	QuiverImageViewMode unmagnified_mode = bInternalZoomOnly
		? quiver_image_view_get_view_mode_unmagnified(QUIVER_IMAGE_VIEW(pViewerImpl->m_pImageView))
		: mode;

	/* The tick is the user's, so it is named from the mode they are in: the
	 * list is a list of modes they can pick, and the internal 1:1 is not one of
	 * them, so ticking it would be ticking an entry that is not there and
	 * untick every entry that is. */
	const gchar *action_name = NULL;
	switch (unmagnified_mode)
	{
		case QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW:
			action_name = ACTION_VIEWER_ZOOM_FIT;
			break;
		case QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW_STRETCH:
			action_name = ACTION_VIEWER_ZOOM_FIT_STRETCH;
			break;
		case QUIVER_IMAGE_VIEW_MODE_ACTUAL_SIZE:
			action_name = ACTION_VIEWER_ZOOM_100;
			break;
		case QUIVER_IMAGE_VIEW_MODE_ZOOM:
			/* not a mode in the list: it is the state a zoom puts the view in,
			 * so there is no entry to tick for it and the mode the zoom was
			 * made from stays ticked - which is also what leaves the user a way
			 * back to that mode's own framing */
			action_name = NULL;
			break;
		case QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP:
			action_name = ACTION_VIEWER_ZOOM_KEEP;
			break;
		case QUIVER_IMAGE_VIEW_MODE_FILL_SCREEN:
			action_name = ACTION_VIEWER_ZOOM_FILL_SCREEN;
			break;
		case QUIVER_IMAGE_VIEW_MODE_COUNT:
		default:
			break;
	}

	/* The video keeps a copy of the mode because it is drawn by the pipeline
	 * rather than by the image view, and this is the one signal that every route
	 * to a mode change goes through - the menu, a zoom, and the fall back to the
	 * mode the view was zoomed out of - so it is where the copy is kept from
	 * drifting away from the stills.  The copy is what the video is drawn and
	 * panned from, so it is the state the view is really in, 1:1 included; what
	 * the user is in is GetChosenViewMode(), and that is what the preference
	 * below and the tick in the list are for. */
	pViewerImpl->m_eVideoViewMode = mode;

	PreferencesPtr prefsPtr = Preferences::GetInstance();
	prefsPtr->SetInteger(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_DEFAULT_VIEW_MODE, unmagnified_mode);

	if (NULL != action_name)
	{
		/* A view at its mode's own framing ticks that mode, and an entry the
		 * user picks puts the view back at that framing - which is what makes
		 * the entry worth keeping ticked while a zoom is in force: it is the
		 * way back to the mode. */
		QuiverUtils::SetRadioActionCurrent(action_name, unmagnified_mode);
	}
}

static gboolean viewer_imageview_key_press_event(GtkEventControllerKey *controller, guint keyval, guint keycode, GdkModifierType state, gpointer userdata)
{ (void)controller; (void)keycode; (void)state;
	Viewer::ViewerImpl *pViewerImpl = (Viewer::ViewerImpl*)userdata;
	if (pViewerImpl && pViewerImpl->IsVideo() && (keyval == GDK_KEY_m || keyval == GDK_KEY_M))
	{
		pViewerImpl->ToggleMute();
		return TRUE;
	}

	if (GDK_KEY_Left == keyval || GDK_KEY_Page_Up == keyval || GDK_KEY_Up == keyval)
	{
		GAction* action = QuiverUtils::GetAction(ACTION_VIEWER_PREVIOUS);
		if (action) g_action_activate(action, NULL);
		return TRUE;
	}
	else if (GDK_KEY_Right == keyval || GDK_KEY_Page_Down == keyval || GDK_KEY_Down == keyval)
	{
		GAction* action = QuiverUtils::GetAction(ACTION_VIEWER_NEXT);
		if (action) g_action_activate(action, NULL);
		return TRUE;
	}

	return FALSE;
}

static void viewer_iconview_cell_activated(QuiverIconView *iconview,gulong cell,gpointer data)
{ (void)cell;  (void)iconview; 
	Viewer::ViewerImpl *pViewerImpl;
 (void)pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)data;

	//pViewerImpl->m_pViewer->EmitItemActivatedEvent();
}

static void viewer_iconview_cursor_changed(QuiverIconView *iconview,gulong cell,gpointer data)
{ (void)iconview; 
	Viewer::ViewerImpl *pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)data;

	bool bDirectionForward = false;

	if (pViewerImpl->m_ImageListPtr->GetSize() && pViewerImpl->m_ImageListPtr->GetCurrentIndex() < cell)
	{
		bDirectionForward = true;
	}

	pViewerImpl->SetImageIndex(cell,bDirectionForward);

}

static void viewer_icon_view_map_cb(GtkWidget *widget, gpointer user_data)
{
	(void)widget;
	Viewer::ViewerImpl* v = (Viewer::ViewerImpl*)user_data;
	v->m_ThumbnailLoader.SetMapped(true);
	/* The first UpdateList(true) often runs at startup before the
	 * filmstrip is mapped, so every thumbnail was skipped.  Re-queue
	 * the visible range now that the filmstrip is mapped (and allocated). */
	v->m_ThumbnailLoader.UpdateList(true);
}

static void viewer_icon_view_unmap_cb(GtkWidget *widget, gpointer user_data)
{
	(void)widget;
	Viewer::ViewerImpl* v = (Viewer::ViewerImpl*)user_data;
	v->m_ThumbnailLoader.SetMapped(false);
}


static void viewer_video_option_audio_cb(GtkButton *button, gpointer user_data)
{
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl*)user_data;
	gint track = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "track-id"));
	g_object_set(G_OBJECT(p->m_pPipeline), "current-audio", track, NULL);

	GtkWidget *box = gtk_widget_get_parent(GTK_WIDGET(button));
	if (box != NULL)
	{
		for (GtkWidget *child = gtk_widget_get_first_child(box);
		     child != NULL;
		     child = gtk_widget_get_next_sibling(child))
		{
			if (g_object_get_data(G_OBJECT(child), "is-audio-track"))
			{
				gint tid = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(child), "track-id"));
				const gchar *track_lang = (const gchar*)g_object_get_data(G_OBJECT(child), "track-lang");
				gchar *label = g_strdup_printf("%sTrack %d%s",
					(tid == track) ? "✓ " : "   ",
					tid + 1,
					track_lang ? track_lang : "");
				gtk_button_set_label(GTK_BUTTON(child), label);
				g_free(label);
			}
		}
	}
}

static void viewer_video_option_text_cb(GtkButton *button, gpointer user_data)
{
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl*)user_data;
	gint track = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "track-id"));
	
	guint flags = 0;
	g_object_get(G_OBJECT(p->m_pPipeline), "flags", &flags, NULL);
	
	gboolean text_enabled = FALSE;
	if (track < 0) {
		flags &= ~(1 << 2); // disable GST_PLAY_FLAG_TEXT
		g_object_set(G_OBJECT(p->m_pPipeline), "flags", flags, NULL);
		text_enabled = FALSE;
	} else {
		flags |= (1 << 2); // enable GST_PLAY_FLAG_TEXT
		g_object_set(G_OBJECT(p->m_pPipeline), "flags", flags, "current-text", track, NULL);
		text_enabled = TRUE;
	}

	GtkWidget *box = gtk_widget_get_parent(GTK_WIDGET(button));
	if (box != NULL)
	{
		for (GtkWidget *child = gtk_widget_get_first_child(box);
		     child != NULL;
		     child = gtk_widget_get_next_sibling(child))
		{
			if (g_object_get_data(G_OBJECT(child), "is-text-track"))
			{
				gint tid = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(child), "track-id"));
				if (tid < 0)
				{
					gtk_button_set_label(GTK_BUTTON(child), !text_enabled ? "✓ Off" : "   Off");
				}
				else
				{
					const gchar *track_lang = (const gchar*)g_object_get_data(G_OBJECT(child), "track-lang");
					gchar *label = g_strdup_printf("%sTrack %d%s",
						(text_enabled && tid == track) ? "✓ " : "   ",
						tid + 1,
						track_lang ? track_lang : "");
					gtk_button_set_label(GTK_BUTTON(child), label);
					g_free(label);
				}
			}
		}
	}
}

static void viewer_video_option_rotate_cw_cb(GtkButton *button, gpointer user_data)
{
	(void)button;
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl*)user_data;
	if (p)
		p->RotateVideo(true);
}

static void viewer_video_option_rotate_ccw_cb(GtkButton *button, gpointer user_data)
{
	(void)button;
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl*)user_data;
	if (p)
		p->RotateVideo(false);
}

static void viewer_video_option_loop_cb(GtkButton *button, gpointer user_data)
{
	(void)button;
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl*)user_data;
	p->m_bVideoLoop = !p->m_bVideoLoop;
	if (p->m_pVideoOptionsPopover)
		gtk_popover_popdown(GTK_POPOVER(p->m_pVideoOptionsPopover));
}

static void viewer_slideshow_delay_cb(GtkWidget *widget, gpointer user_data)
{
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl *)user_data;
	int sec = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "delay-seconds"));
	if (sec > 0)
	{
		p->m_iSlideShowDuration = sec * 1000;
		Preferences::GetInstance()->SetInteger(QUIVER_PREFS_SLIDESHOW, QUIVER_PREFS_SLIDESHOW_DURATION, p->m_iSlideShowDuration);
		if (p->m_pImageSubmenuPopover)
			gtk_popover_popdown(GTK_POPOVER(p->m_pImageSubmenuPopover));
	}
}

static void viewer_slideshow_loop_cb(GtkWidget *widget, gpointer user_data)
{
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl *)user_data;
	if (!p) return;
	p->m_bSlideShowLoop = !p->m_bSlideShowLoop;
	Preferences::GetInstance()->SetBoolean(QUIVER_PREFS_SLIDESHOW, QUIVER_PREFS_SLIDESHOW_LOOP, p->m_bSlideShowLoop);
	if (p->m_bSlideShowLoop)
		gtk_widget_add_css_class(widget, "speed-active");
	else
		gtk_widget_remove_css_class(widget, "speed-active");
}

static void viewer_submenu_flip_h_cb(GtkWidget *widget, gpointer user_data)
{
	(void)widget;
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl *)user_data;
	if (p->m_pImageSubmenuPopover)
		gtk_popover_popdown(GTK_POPOVER(p->m_pImageSubmenuPopover));
	GAction *act = QuiverUtils::GetAction(ACTION_VIEWER_FLIP_H);
	if (act) g_action_activate(act, NULL);
}

static void viewer_submenu_flip_v_cb(GtkWidget *widget, gpointer user_data)
{
	(void)widget;
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl *)user_data;
	if (p->m_pImageSubmenuPopover)
		gtk_popover_popdown(GTK_POPOVER(p->m_pImageSubmenuPopover));
	GAction *act = QuiverUtils::GetAction(ACTION_VIEWER_FLIP_V);
	if (act) g_action_activate(act, NULL);
}

static void viewer_submenu_trash_cb(GtkWidget *widget, gpointer user_data)
{
	(void)widget;
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl *)user_data;
	if (p->m_pImageSubmenuPopover)
		gtk_popover_popdown(GTK_POPOVER(p->m_pImageSubmenuPopover));
	if (p->m_pVideoOptionsPopover)
		gtk_popover_popdown(GTK_POPOVER(p->m_pVideoOptionsPopover));
	GAction *act = QuiverUtils::GetAction(ACTION_VIEWER_TRASH);
	if (act) g_action_activate(act, NULL);
}

static void viewer_submenu_copy_cb(GtkWidget *widget, gpointer user_data)
{
	(void)widget;
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl *)user_data;
	if (p->m_pImageSubmenuPopover)
		gtk_popover_popdown(GTK_POPOVER(p->m_pImageSubmenuPopover));
	GAction *act = QuiverUtils::GetAction(ACTION_VIEWER_COPY);
	if (act) g_action_activate(act, NULL);
}

static void viewer_video_submenu_speed_cb(GtkWidget *widget, gpointer user_data)
{
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl *)user_data;
	int speedInt = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "speed-value"));
	double speed = speedInt / 100.0;
	if (speed > 0.0)
	{
		p->SetPlaybackSpeed(speed);
		if (p->m_pVideoOptionsPopover)
			gtk_popover_popdown(GTK_POPOVER(p->m_pVideoOptionsPopover));
	}
}

static void viewer_submenu_slideshow_cb(GtkWidget *widget, gpointer user_data)
{
	(void)widget;
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl *)user_data;
	if (p)
	{
		viewer_overlay_slideshow_cb(p);
	}
}

/* The list of view modes for the split button in the viewer HUD.  The items are
 * the zoom radio actions themselves, so the check mark follows the current mode
 * for free and picking an item goes through the normal action path. */
/* The view-mode menu never changes, so it is built once together with the HUD
 * instead of on every click. */
static GtkWidget *viewer_view_mode_build_popover()
{
	GMenu *menu = g_menu_new();

	struct { const char *label; const char *action; const char *accel; const char *icon; } items[] = {
		{ "Fit to Window",           "quiver.ZoomFit",         "",  "zoom-fit-best-symbolic" },
		{ "Fit to Window (Stretched)", "quiver.ZoomFitStretch", "",  "zoom-fit-best-symbolic" },
		{ "Actual Size (100%)",      "quiver.Zoom100",         "",  "zoom-original-symbolic" },
		{ "Fill Screen",             "quiver.ZoomFillScreen",  "",  "zoom-fill-best-symbolic" },
		{ "Keep Zoom and Pan",       "quiver.ZoomKeep",        "",  "zoom-in-best-symbolic" },
	};

	for (gsize i = 0; i < G_N_ELEMENTS(items); i++)
	{
		GMenuItem *item = g_menu_item_new(items[i].label, items[i].action);
		if (items[i].accel != NULL && items[i].accel[0] != '\0')
			g_menu_item_set_attribute(item, "accel", "s", items[i].accel);
		if (items[i].icon != NULL && items[i].icon[0] != '\0')
			g_menu_item_set_attribute(item, "icon", "s", items[i].icon);
		g_menu_append_item(menu, item);
		g_object_unref(item);
	}

	GtkWidget *popover = gtk_popover_menu_new_from_model(G_MENU_MODEL(menu));
	g_object_unref(menu);
	gtk_widget_insert_action_group(popover, "quiver", G_ACTION_GROUP(QuiverUtils::GetActionGroup()));
	gtk_popover_set_autohide(GTK_POPOVER(popover), TRUE);
	return popover;
}

/* the menu is ready, this only aims it: the HUD sits at the bottom by default,
 * so the list has to open upwards there */
static void viewer_view_mode_split_size_sep(GtkWidget *pFitBtn, GtkWidget *pSep)
{
	/* The separator inside the split control is half as tall as the button
	 * beside it: the full-height rules the HUD's button groups are divided
	 * with are what made it read as another group divider.  It is sized from
	 * the button rather than in CSS so the ratio holds at any font size. */
	if (pSep == NULL || !GTK_IS_WIDGET(pSep))
		return;
	int min_h = 0, nat_h = 0, min_b = 0, nat_b = 0;
	if (pFitBtn != NULL && GTK_IS_WIDGET(pFitBtn))
		gtk_widget_measure(pFitBtn, GTK_ORIENTATION_VERTICAL, -1, &min_h, &nat_h, &min_b, &nat_b);
	if (min_h <= 0)
		min_h = nat_h;
	gtk_widget_set_size_request(pSep, -1, min_h / 2);
}

static void viewer_view_mode_arrow_set_direction(GtkWidget *pArrow, bool bUp)
{
	if (pArrow == NULL || !GTK_IS_BUTTON(pArrow))
		return;
	GtkWidget *icon = gtk_button_get_child(GTK_BUTTON(pArrow));
	if (icon != NULL && GTK_IS_IMAGE(icon))
	{
		/* the arrow points at the side the list opens on */
		gtk_image_set_from_icon_name(GTK_IMAGE(icon), bUp ? "pan-up-symbolic" : "pan-down-symbolic");
		gtk_image_set_pixel_size(GTK_IMAGE(icon), 12);
	}
}

static void viewer_view_mode_arrow_cb(Viewer::ViewerImpl *p)
{
	if (p == NULL || p->m_pViewModeMenuPopover == NULL || !G_IS_OBJECT(p->m_pViewModeMenuPopover))
		return;
	GtkPopover *pop = GTK_POPOVER(p->m_pViewModeMenuPopover);
	if (gtk_widget_get_visible(GTK_WIDGET(pop)))
	{
		gtk_popover_popdown(pop);
	}
	else
	{
		gtk_popover_popup(pop);
	}
}

static void viewer_view_mode_create_popup_cb(GtkWidget *popover, gpointer user_data)
{
	Viewer::ViewerImpl *pViewer = (Viewer::ViewerImpl *)user_data;
	if (pViewer == NULL || NULL == pViewer->m_pViewModeMenuPopover)
		return;

	/* A video's view mode lives beside the image view, and either way it is the
	 * mode the user is in that the list is ticked with: opening the list while a
	 * zoom is in force is the moment the user goes to see which mode they are
	 * in, and a list with nothing ticked answers "none of them" for a picture
	 * that is plainly in one. */
	const QuiverImageViewMode chosen = pViewer->GetChosenViewMode();
	if (QUIVER_IMAGE_VIEW_MODE_ZOOM != chosen)
		QuiverUtils::SetRadioActionCurrent(ACTION_VIEWER_ZOOM, chosen);

	GtkWidget *anchor = gtk_widget_get_parent(popover);
	if (pViewer->m_pViewModeMenuBtn != NULL && anchor != NULL
		&& GTK_IS_WIDGET(pViewer->m_pViewModeMenuBtn) && GTK_IS_WIDGET(anchor))
	{
		graphene_rect_t bounds;
		if (gtk_widget_compute_bounds(pViewer->m_pViewModeMenuBtn, anchor, &bounds))
		{
			GdkRectangle arrow = {
				(int)graphene_rect_get_x(&bounds), (int)graphene_rect_get_y(&bounds),
				(int)graphene_rect_get_width(&bounds), (int)graphene_rect_get_height(&bounds)
			};
			gtk_popover_set_pointing_to(GTK_POPOVER(popover), &arrow);
		}
	}

	PreferencesPtr prefs = Preferences::GetInstance();
	int hudPos = prefs->GetInteger(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_HUD_POSITION, HUD_POS_BOTTOM);
	gtk_popover_set_position(GTK_POPOVER(popover),
		(hudPos == HUD_POS_TOP) ? GTK_POS_BOTTOM : GTK_POS_TOP);
}

static void viewer_video_submenu_snapshot_cb(GtkWidget *widget, gpointer user_data)
{
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl *)user_data;
	if (p && p->m_pVideoOptionsPopover)
		gtk_popover_popdown(GTK_POPOVER(p->m_pVideoOptionsPopover));
	viewer_snapshot_button_clicked_cb(GTK_BUTTON(widget), user_data);
}

static void viewer_image_submenu_create_popup_cb(GtkMenuButton *button, gpointer user_data)
{
	(void)button;
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl *)user_data;
	if (p == NULL) return;

	PreferencesPtr prefs = Preferences::GetInstance();
	int hudPos = prefs->GetInteger(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_HUD_POSITION, HUD_POS_BOTTOM);
	GtkPositionType popPos = (hudPos == HUD_POS_TOP) ? GTK_POS_BOTTOM : GTK_POS_TOP;

	GtkWidget *popover = gtk_popover_new();
	gtk_popover_set_position(GTK_POPOVER(popover), popPos);
	gtk_popover_set_autohide(GTK_POPOVER(popover), TRUE);
	p->m_pImageSubmenuPopover = popover;
	g_signal_connect_swapped(popover, "map", G_CALLBACK(+[](Viewer::ViewerImpl *impl) {
		if (impl) impl->UpdateSlideshowButton();
	}), p);

	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	gtk_widget_set_margin_start(box, 8);
	gtk_widget_set_margin_end(box, 8);
	gtk_widget_set_margin_top(box, 8);
	gtk_widget_set_margin_bottom(box, 8);

	// Slideshow Delay section
	GtkWidget *lblDelay = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(lblDelay), "<b>Slideshow Delay</b>");
	gtk_widget_set_halign(lblDelay, GTK_ALIGN_START);
	gtk_box_append(GTK_BOX(box), lblDelay);

	GtkWidget *delayRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
	static const int delays[] = { 1, 2, 3, 5, 10 };
	int curSec = p->m_iSlideShowDuration / 1000;
	for (size_t i = 0; i < sizeof(delays)/sizeof(delays[0]); i++)
	{
		int s = delays[i];
		gchar *label = g_strdup_printf("%ds", s);
		GtkWidget *btn = gtk_button_new_with_label(label);
		g_free(label);
		gtk_widget_add_css_class(btn, "submenu-pill-btn");
		if (curSec == s)
			gtk_widget_add_css_class(btn, "speed-active");
		g_object_set_data(G_OBJECT(btn), "delay-seconds", GINT_TO_POINTER(s));
		g_signal_connect(btn, "clicked", G_CALLBACK(viewer_slideshow_delay_cb), p);
		gtk_box_append(GTK_BOX(delayRow), btn);
	}
	gtk_box_append(GTK_BOX(box), delayRow);

	gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

	// Action icon buttons row
	GtkWidget *actionRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_set_halign(actionRow, GTK_ALIGN_CENTER);

	// Flip Horizontal
	GtkWidget *fliph_btn = gtk_button_new_from_icon_name("object-flip-horizontal-symbolic");
	gtk_widget_set_tooltip_text(fliph_btn, ShortcutManager::GetInstance().GetTooltipForAction("FlipH", "Flip Horizontal").c_str());
	gtk_widget_add_css_class(fliph_btn, "media-btn");
	gtk_widget_add_css_class(fliph_btn, "submenu-icon-btn");
	g_signal_connect(fliph_btn, "clicked", G_CALLBACK(viewer_submenu_flip_h_cb), p);
	gtk_box_append(GTK_BOX(actionRow), fliph_btn);

	// Flip Vertical
	GtkWidget *flipv_btn = gtk_button_new_from_icon_name("object-flip-vertical-symbolic");
	gtk_widget_set_tooltip_text(flipv_btn, ShortcutManager::GetInstance().GetTooltipForAction("FlipV", "Flip Vertical").c_str());
	gtk_widget_add_css_class(flipv_btn, "media-btn");
	gtk_widget_add_css_class(flipv_btn, "submenu-icon-btn");
	g_signal_connect(flipv_btn, "clicked", G_CALLBACK(viewer_submenu_flip_v_cb), p);
	gtk_box_append(GTK_BOX(actionRow), flipv_btn);

	// Slideshow toggle
	GtkWidget *ss_btn = gtk_toggle_button_new();
	gtk_button_set_icon_name(GTK_BUTTON(ss_btn), "display-projector-symbolic");
	gtk_widget_set_tooltip_text(ss_btn, ShortcutManager::GetInstance().GetTooltipForAction("SlideShow", "Slideshow").c_str());
	gtk_widget_add_css_class(ss_btn, "media-btn");
	gtk_widget_add_css_class(ss_btn, "submenu-icon-btn");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ss_btn), p->m_bSlideShowRunning);
	if (p->m_bSlideShowRunning)
		gtk_widget_add_css_class(ss_btn, "speed-active");
	g_signal_connect(ss_btn, "clicked", G_CALLBACK(viewer_submenu_slideshow_cb), p);
	p->m_pViewerSlideshowBtn = ss_btn;
	g_object_add_weak_pointer(G_OBJECT(ss_btn), (gpointer*)&p->m_pViewerSlideshowBtn);
	p->UpdateUI();
	gtk_box_append(GTK_BOX(actionRow), ss_btn);

	// Loop toggle
	GtkWidget *loop_btn = gtk_button_new_from_icon_name("media-playlist-repeat-symbolic");
	gtk_widget_set_tooltip_text(loop_btn, "Loop Slideshow");
	gtk_widget_add_css_class(loop_btn, "media-btn");
	gtk_widget_add_css_class(loop_btn, "submenu-icon-btn");
	if (p->m_bSlideShowLoop)
		gtk_widget_add_css_class(loop_btn, "speed-active");
	g_signal_connect(loop_btn, "clicked", G_CALLBACK(viewer_slideshow_loop_cb), p);
	gtk_box_append(GTK_BOX(actionRow), loop_btn);

	// Copy File
	GtkWidget *copy_btn = gtk_button_new_from_icon_name("edit-copy-symbolic");
	gtk_widget_set_tooltip_text(copy_btn, "Copy File (Ctrl+C)");
	gtk_widget_add_css_class(copy_btn, "media-btn");
	gtk_widget_add_css_class(copy_btn, "submenu-icon-btn");
	g_signal_connect(copy_btn, "clicked", G_CALLBACK(viewer_submenu_copy_cb), p);
	gtk_box_append(GTK_BOX(actionRow), copy_btn);

	// Move to Trash
	GtkWidget *trash_btn = gtk_button_new_from_icon_name("user-trash-symbolic");
	gtk_widget_set_tooltip_text(trash_btn, ShortcutManager::GetInstance().GetTooltipForAction("BrowserTrash", "Move to Trash").c_str());
	gtk_widget_add_css_class(trash_btn, "media-btn");
	gtk_widget_add_css_class(trash_btn, "submenu-icon-btn");
	gtk_widget_add_css_class(trash_btn, "destructive-action");
	g_signal_connect(trash_btn, "clicked", G_CALLBACK(viewer_submenu_trash_cb), p);
	gtk_box_append(GTK_BOX(actionRow), trash_btn);

	gtk_box_append(GTK_BOX(box), actionRow);

	gtk_popover_set_child(GTK_POPOVER(popover), box);
	gtk_menu_button_set_popover(button, popover);
	gtk_popover_set_position(GTK_POPOVER(popover), popPos);
}

static void viewer_video_options_create_popup_cb(GtkMenuButton *button, gpointer user_data)
{
	(void)button;
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl *)user_data;
	if (p == NULL) return;

	PreferencesPtr prefs = Preferences::GetInstance();
	int hudPos = prefs->GetInteger(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_HUD_POSITION, HUD_POS_BOTTOM);
	GtkPositionType popPos = (hudPos == HUD_POS_TOP) ? GTK_POS_BOTTOM : GTK_POS_TOP;

	GtkWidget *popover = gtk_popover_new();
	gtk_popover_set_position(GTK_POPOVER(popover), popPos);
	gtk_popover_set_autohide(GTK_POPOVER(popover), TRUE);
	p->m_pVideoOptionsPopover = popover;
	g_signal_connect_swapped(popover, "map", G_CALLBACK(+[](Viewer::ViewerImpl *impl) {
		if (impl) impl->UpdateSlideshowButton();
	}), p);

	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	gtk_widget_set_margin_start(box, 8);
	gtk_widget_set_margin_end(box, 8);
	gtk_widget_set_margin_top(box, 8);
	gtk_widget_set_margin_bottom(box, 8);

	if (p->m_pPipeline != NULL)
	{
		// Playback Speed
		GtkWidget *lblSpeed = gtk_label_new(NULL);
		gtk_label_set_markup(GTK_LABEL(lblSpeed), "<b>Playback Speed</b>");
		gtk_widget_set_halign(lblSpeed, GTK_ALIGN_START);
		gtk_box_append(GTK_BOX(box), lblSpeed);

		GtkWidget *speedRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
		const double speeds[] = { 0.5, 0.75, 1.0, 1.25, 1.5, 2.0 };
		const int nSpeeds = sizeof(speeds) / sizeof(speeds[0]);
		for (int i = 0; i < nSpeeds; i++)
		{
			bool isCurrent = (fabs(p->m_dPlaybackSpeed - speeds[i]) < 0.05);
			gchar *label = g_strdup_printf("%.4gx", speeds[i]);
			GtkWidget *btn = gtk_button_new_with_label(label);
			g_free(label);
			gtk_widget_add_css_class(btn, "submenu-pill-btn");
			if (isCurrent)
				gtk_widget_add_css_class(btn, "speed-active");
			g_object_set_data(G_OBJECT(btn), "speed-value", GINT_TO_POINTER((int)(speeds[i] * 100)));
			g_signal_connect(btn, "clicked", G_CALLBACK(viewer_video_submenu_speed_cb), p);
			gtk_box_append(GTK_BOX(speedRow), btn);
		}
		gtk_box_append(GTK_BOX(box), speedRow);

		gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

		// Video tools & actions row (Rotate CCW, Rotate CW, Frame Grab, Loop, Trash)
		GtkWidget *actionRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
		gtk_widget_set_halign(actionRow, GTK_ALIGN_CENTER);

		GtkWidget *rotCcw = gtk_button_new_from_icon_name("object-rotate-left-symbolic");
		gtk_widget_set_tooltip_text(rotCcw, ShortcutManager::GetInstance().GetTooltipForAction("RotateCCW", "Rotate Counter-Clockwise 90°").c_str());
		gtk_widget_add_css_class(rotCcw, "media-btn");
		gtk_widget_add_css_class(rotCcw, "submenu-icon-btn");
		g_signal_connect(rotCcw, "clicked", G_CALLBACK(viewer_video_option_rotate_ccw_cb), p);
		gtk_box_append(GTK_BOX(actionRow), rotCcw);

		GtkWidget *rotCw = gtk_button_new_from_icon_name("object-rotate-right-symbolic");
		gtk_widget_set_tooltip_text(rotCw, ShortcutManager::GetInstance().GetTooltipForAction("RotateCW", "Rotate Clockwise 90°").c_str());
		gtk_widget_add_css_class(rotCw, "media-btn");
		gtk_widget_add_css_class(rotCw, "submenu-icon-btn");
		g_signal_connect(rotCw, "clicked", G_CALLBACK(viewer_video_option_rotate_cw_cb), p);
		gtk_box_append(GTK_BOX(actionRow), rotCw);

		// Snapshot (Frame Grab)
		GtkWidget *snap_btn = gtk_button_new_from_icon_name("camera-photo-symbolic");
		gtk_widget_set_tooltip_text(snap_btn, ShortcutManager::GetInstance().GetTooltipForAction("VideoSnapshot", "Take Snapshot").c_str());
		gtk_widget_add_css_class(snap_btn, "media-btn");
		gtk_widget_add_css_class(snap_btn, "submenu-icon-btn");
		g_signal_connect(snap_btn, "clicked", G_CALLBACK(viewer_video_submenu_snapshot_cb), p);
		p->m_pSnapBtn = snap_btn;
		g_object_add_weak_pointer(G_OBJECT(snap_btn), (gpointer*)&p->m_pSnapBtn);
		p->UpdateUI();
		gtk_box_append(GTK_BOX(actionRow), snap_btn);

		// Slideshow toggle
		GtkWidget *ss_btn = gtk_toggle_button_new();
		gtk_button_set_icon_name(GTK_BUTTON(ss_btn), "display-projector-symbolic");
		gtk_widget_set_tooltip_text(ss_btn, ShortcutManager::GetInstance().GetTooltipForAction("SlideShow", "Slideshow").c_str());
		gtk_widget_add_css_class(ss_btn, "media-btn");
		gtk_widget_add_css_class(ss_btn, "submenu-icon-btn");
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ss_btn), p->m_bSlideShowRunning);
		if (p->m_bSlideShowRunning)
			gtk_widget_add_css_class(ss_btn, "speed-active");
		g_signal_connect(ss_btn, "clicked", G_CALLBACK(viewer_submenu_slideshow_cb), p);
		p->m_pViewerVideoSlideshowBtn = ss_btn;
		g_object_add_weak_pointer(G_OBJECT(ss_btn), (gpointer*)&p->m_pViewerVideoSlideshowBtn);
		p->UpdateUI();
		gtk_box_append(GTK_BOX(actionRow), ss_btn);

		// Loop Video
		GtkWidget *loop_btn = gtk_button_new_from_icon_name("media-playlist-repeat-symbolic");
		gtk_widget_set_tooltip_text(loop_btn, "Loop Video");
		gtk_widget_add_css_class(loop_btn, "media-btn");
		gtk_widget_add_css_class(loop_btn, "submenu-icon-btn");
		if (p->m_bVideoLoop)
			gtk_widget_add_css_class(loop_btn, "speed-active");
		g_signal_connect(loop_btn, "clicked", G_CALLBACK(viewer_video_option_loop_cb), p);
		gtk_box_append(GTK_BOX(actionRow), loop_btn);

		// Move to Trash
		GtkWidget *trash_btn = gtk_button_new_from_icon_name("user-trash-symbolic");
		gtk_widget_set_tooltip_text(trash_btn, ShortcutManager::GetInstance().GetTooltipForAction("BrowserTrash", "Move to Trash").c_str());
		gtk_widget_add_css_class(trash_btn, "media-btn");
		gtk_widget_add_css_class(trash_btn, "submenu-icon-btn");
		gtk_widget_add_css_class(trash_btn, "destructive-action");
		g_signal_connect(trash_btn, "clicked", G_CALLBACK(viewer_submenu_trash_cb), p);
		gtk_box_append(GTK_BOX(actionRow), trash_btn);

		gtk_box_append(GTK_BOX(box), actionRow);

		// Audio tracks: ONLY show if n_audio > 1!
		gint n_audio = 0;
		g_object_get(p->m_pPipeline, "n-audio", &n_audio, NULL);
		gint current_audio = -1;
		g_object_get(p->m_pPipeline, "current-audio", &current_audio, NULL);

		if (n_audio > 1)
		{
			gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
			GtkWidget *mi = gtk_label_new(NULL);
			gtk_label_set_markup(GTK_LABEL(mi), "<b>Audio Tracks</b>");
			gtk_widget_set_halign(mi, GTK_ALIGN_START);
			gtk_box_append(GTK_BOX(box), mi);

			for (gint i = 0; i < n_audio; ++i)
			{
				GstTagList *tags = NULL;
				g_signal_emit_by_name(p->m_pPipeline, "get-audio-tags", i, &tags);
				gchar *lang = NULL;
				if (tags)
				{
					gst_tag_list_get_string(tags, GST_TAG_LANGUAGE_CODE, &lang);
					gst_tag_list_free(tags);
				}
				gchar *lang_str = lang ? g_strdup_printf(" (%s)", lang) : NULL;
				gchar *label = g_strdup_printf("%sTrack %d%s",
					(i == current_audio) ? "✓ " : "   ",
					i + 1,
					lang_str ? lang_str : "");
				GtkWidget *item = gtk_button_new_with_label(label);
				gtk_button_set_has_frame(GTK_BUTTON(item), FALSE);
				gtk_widget_set_halign(item, GTK_ALIGN_FILL);
				g_free(label);
				if (lang) g_free(lang);

				g_object_set_data(G_OBJECT(item), "track-id", GINT_TO_POINTER(i));
				g_object_set_data(G_OBJECT(item), "is-audio-track", GINT_TO_POINTER(1));
				if (lang_str)
					g_object_set_data_full(G_OBJECT(item), "track-lang", lang_str, g_free);
				g_signal_connect(item, "clicked", G_CALLBACK(viewer_video_option_audio_cb), p);
				gtk_box_append(GTK_BOX(box), item);
			}
		}

		// Subtitles: ONLY show if n_text > 0!
		gint n_text = 0;
		g_object_get(p->m_pPipeline, "n-text", &n_text, NULL);
		gint current_text = -1;
		g_object_get(p->m_pPipeline, "current-text", &current_text, NULL);

		if (n_text > 0)
		{
			guint flags = 0;
			g_object_get(p->m_pPipeline, "flags", &flags, NULL);
			gboolean text_enabled = (flags & (1 << 2)) != 0; // GST_PLAY_FLAG_TEXT

			gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
			GtkWidget *mi = gtk_label_new(NULL);
			gtk_label_set_markup(GTK_LABEL(mi), "<b>Subtitles</b>");
			gtk_widget_set_halign(mi, GTK_ALIGN_START);
			gtk_box_append(GTK_BOX(box), mi);

			GtkWidget *item_off = gtk_button_new_with_label(!text_enabled ? "✓ Off" : "   Off");
			gtk_button_set_has_frame(GTK_BUTTON(item_off), FALSE);
			gtk_widget_set_halign(item_off, GTK_ALIGN_FILL);
			g_object_set_data(G_OBJECT(item_off), "track-id", GINT_TO_POINTER(-1));
			g_object_set_data(G_OBJECT(item_off), "is-text-track", GINT_TO_POINTER(1));
			g_signal_connect(item_off, "clicked", G_CALLBACK(viewer_video_option_text_cb), p);
			gtk_box_append(GTK_BOX(box), item_off);

			for (gint i = 0; i < n_text; ++i)
			{
				GstTagList *tags = NULL;
				g_signal_emit_by_name(p->m_pPipeline, "get-text-tags", i, &tags);
				gchar *lang = NULL;
				if (tags)
				{
					gst_tag_list_get_string(tags, GST_TAG_LANGUAGE_CODE, &lang);
					gst_tag_list_free(tags);
				}
				gchar *lang_str = lang ? g_strdup_printf(" (%s)", lang) : NULL;
				gchar *label = g_strdup_printf("%sTrack %d%s",
					(text_enabled && i == current_text) ? "✓ " : "   ",
					i + 1,
					lang_str ? lang_str : "");
				GtkWidget *item = gtk_button_new_with_label(label);
				gtk_button_set_has_frame(GTK_BUTTON(item), FALSE);
				gtk_widget_set_halign(item, GTK_ALIGN_FILL);
				g_free(label);
				if (lang) g_free(lang);

				g_object_set_data(G_OBJECT(item), "track-id", GINT_TO_POINTER(i));
				g_object_set_data(G_OBJECT(item), "is-text-track", GINT_TO_POINTER(1));
				if (lang_str)
					g_object_set_data_full(G_OBJECT(item), "track-lang", lang_str, g_free);
				g_signal_connect(item, "clicked", G_CALLBACK(viewer_video_option_text_cb), p);
				gtk_box_append(GTK_BOX(box), item);
			}
		}
	}

	GtkWidget *scrolled = gtk_scrolled_window_new();
	gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scrolled), 350);
	gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scrolled), TRUE);
	gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(scrolled), TRUE);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), box);

	gtk_popover_set_child(GTK_POPOVER(popover), scrolled);
	gtk_menu_button_set_popover(button, popover);
	gtk_popover_set_position(GTK_POPOVER(popover), popPos);
}

static void
viewer_volume_value_changed (GtkRange *range, gdouble value, gpointer user_data)
{ (void)value; 
	Viewer::ViewerImpl *pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)user_data;
	pViewerImpl->m_dVolume = gtk_range_get_value(range);
	if (pViewerImpl->m_bMuted && pViewerImpl->m_dVolume > 0.01)
	{
		pViewerImpl->m_bMuted = false;
		if (pViewerImpl->m_pPipeline)
		{
			g_object_set(G_OBJECT(pViewerImpl->m_pPipeline), "mute", FALSE, NULL);
		}
	}
	if (pViewerImpl->m_pPipeline)
	{
		g_object_set(G_OBJECT(pViewerImpl->m_pPipeline), "volume", pViewerImpl->m_dVolume, NULL);
	}
	pViewerImpl->UpdateVolumeUI();
}

static void viewer_volume_mute_cb(gpointer user_data)
{
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl *)user_data;
	p->ToggleMute();
}

static void viewer_volume_button_middle_click_cb(GtkGestureClick *gesture, gint n_press, gdouble x, gdouble y, gpointer user_data)
{
	(void)gesture; (void)n_press; (void)x; (void)y;
	Viewer::ViewerImpl *pViewerImpl = (Viewer::ViewerImpl*)user_data;
	pViewerImpl->ToggleMute();
}


static GdkContentProvider* signal_drag_source_prepare(GtkDragSource *source, gdouble x, gdouble y, gpointer user_data)
{ (void)x; (void)y; 
	Viewer::ViewerImpl *pViewerImpl = (Viewer::ViewerImpl*)user_data;
	
	const gchar* uri = NULL;
	if (pViewerImpl->m_ImageListPtr->GetSize())
		uri = pViewerImpl->m_ImageListPtr->GetCurrent().GetURI();

	if (uri == NULL)
		return NULL;

	GdkModifierType state = (GdkModifierType)0;
	GdkEvent *ev = gtk_event_controller_get_current_event(GTK_EVENT_CONTROLLER(source));
	if (ev != NULL)
		state = gdk_event_get_modifier_state(ev);

	std::list<std::string> uris;
	uris.push_back(uri);
	/* File drag by default (uri-list + gnome payload, so other folders and
	 * file managers accept it), Alt-drag for the plain path text only.  See
	 * the browser's drag source for the full rationale. */
	bool bCutDrag = (0 == (state & GDK_CONTROL_MASK));
	const bool bTextOnly = 0 != (state & GDK_ALT_MASK);
	return QuiverClipboard::MakeContentProvider(uris, bCutDrag,
		bTextOnly, !bTextOnly);
}

static void signal_drag_begin (GtkDragSource *source, GdkDrag *drag, gpointer user_data)
{ (void)drag; 
	Viewer::ViewerImpl *pViewerImpl = (Viewer::ViewerImpl*)user_data;
	
	GdkTexture *texture = pViewerImpl->m_ImageListPtr->GetCurrent().GetThumbnailTexture(128);

	if (NULL != texture)
	{
		gtk_drag_source_set_icon(source, GDK_PAINTABLE(texture), -2, -2);
		g_object_unref(texture);
	}
}

static void signal_drag_end(GtkDragSource *source, GdkDrag *drag, gpointer user_data)
{ (void)drag;  (void)source; 
	Viewer::ViewerImpl *pViewerImpl;
 (void)pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)user_data;
}

static gchar*
gst_time_format(gint64 time)
{
	gint64 total_secs = GST_TIME_AS_SECONDS(time);
	gint64 secs  = total_secs % 60;
	gint64 total_mins = total_secs / 60;
	gint64 mins  = total_mins % 60;
	gint64 hours = total_mins / 60;

	gchar* str = NULL;

	if (0 != hours)
		str = g_strdup_printf("%lld:%02lld:%02lld", (long long)hours, (long long)mins, (long long)secs);
	else
		str = g_strdup_printf("%lld:%02lld", (long long)mins, (long long)secs);
	return str;
}

static gboolean 
timeout_play_position (gpointer data)
{
	Viewer::ViewerImpl *pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)data;

	pViewerImpl->UpdateTimeline();

	return TRUE;
}

static gboolean 
gstreamer_bus_watcher(GstBus* bus, GstMessage* msg, gpointer user_data)
{ (void)bus; 
	Viewer::ViewerImpl *pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)user_data;
	if (pViewerImpl == NULL || !pViewerImpl->m_spAlive || !*pViewerImpl->m_spAlive)
		return FALSE;
	switch (GST_MESSAGE_TYPE (msg)) {

		case GST_MESSAGE_EOS:
			{
				if (pViewerImpl->m_bVideoLoop)
				{
					/* Loop mode: seek back to start and keep playing */
					gst_element_seek(GST_ELEMENT(pViewerImpl->m_pPipeline),
						pViewerImpl->m_dPlaybackSpeed, GST_FORMAT_TIME,
						GstSeekFlags(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE),
						GST_SEEK_TYPE_SET, 0,
						GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);
				}
				else
				{
					/* No loop: pause at end, keep showing the video */
					gst_element_set_state(GST_ELEMENT(pViewerImpl->m_pPipeline),
						GST_STATE_PAUSED);
					pViewerImpl->SetIsPlaying(false);
					pViewerImpl->UpdateTimeline();
				}
			}
			break;
		case GST_MESSAGE_STATE_CHANGED:
				break;
		case GST_MESSAGE_ASYNC_DONE:
			{
				GstState current = GST_STATE_VOID_PENDING;
				gst_element_get_state(GST_ELEMENT(pViewerImpl->m_pPipeline), &current, NULL, 0);
				if (current == GST_STATE_PAUSED || current == GST_STATE_PLAYING)
				{
					if (pViewerImpl->m_bVideoNeedsFirstFrame && !pViewerImpl->m_bVideoFlushPending)
					{
						/* Phase 1: pipeline just prerolled the new video.
						 * Send a flushing seek to position 0 — this forces the
						 * GL sink to discard its old texture and re-decode from
						 * the start.  The second ASYNC_DONE (after the flush)
						 * will restore opacity. */
					pViewerImpl->m_bVideoFlushPending = TRUE;
					pViewerImpl->ShowVideoPage();
					gst_element_seek(GST_ELEMENT(pViewerImpl->m_pPipeline),
							1.0, GST_FORMAT_TIME,
							GstSeekFlags(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE),
							GST_SEEK_TYPE_SET, 0,
							GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);
					}
					else if (pViewerImpl->m_bVideoFlushPending)
					{
						/* Phase 2: the flushing seek completed.  The GL sink
						 * has flushed its old texture and decoded a fresh frame
						 * from position 0.  The draw callback will restore
						 * opacity on the next paint — trigger it now. */
						pViewerImpl->m_bVideoFlushPending = FALSE;
						if (pViewerImpl->m_pVideoSinkWidget != NULL)
							gtk_widget_queue_draw(pViewerImpl->m_pVideoSinkWidget);
					}
					else
					{
						pViewerImpl->ShowVideoPage();
					}
					pViewerImpl->UpdateTimeline();
				}
				break;
			}
		case GST_MESSAGE_DURATION:
				pViewerImpl->UpdateTimeline();
				break;
		case GST_MESSAGE_PROGRESS:
			break;
		case GST_MESSAGE_ERROR: 
			{
				gchar  *debug;
				GError *error = NULL;

				gst_message_parse_error (msg, &error, &debug);

				GstObject *msgSrc = GST_MESSAGE_SRC(msg);
				gboolean bPreviewError = (msgSrc != NULL
					&& GST_IS_ELEMENT(msgSrc)
					&& g_str_has_prefix(GST_OBJECT_NAME(msgSrc), "navpreview"));

				g_warning("Video playback error: %s", error->message);
				if (debug && *debug)
					g_warning("Video playback error debug: %s", debug);
				g_free (debug);

				g_error_free (error);

				if (bPreviewError)
				{
					pViewerImpl->ResetVideoPreviewViewState();
					break;
				}

				pViewerImpl->StopVideo(true);

				break;
			}
		default:
			break;
	}

	return TRUE;
}

void Viewer::ViewerImpl::UpdateTimeline()
{
	// keep the timeline visible exactly as the sticky flag says; pausing or
	// seeking never hides it (UpdateTimelineVisibility only gates on the flag)
	UpdateTimelineVisibility();

	gint64 pos = 0, len = 0;
	bool success = gst_element_query_position(m_pPipeline, GST_FORMAT_TIME, &pos);
	success |= gst_element_query_duration(m_pPipeline, GST_FORMAT_TIME, &len);

	// transient query failures (e.g. during a FLUSH seek) must not blank the
	// label or empty the progress bar; keep the last good values instead
	if (!success || len < 0 || pos < 0)
		return;

	gchar* str_pos = gst_time_format(pos);
	gchar* str_len = gst_time_format(len);
	gchar* markup;

	markup = g_strdup_printf("<b>%s</b>", str_pos);
	gtk_label_set_markup(GTK_LABEL(m_pTimeElapsedLabel), markup);
	g_free(markup);

	markup = g_strdup_printf("<b>%s</b>", str_len);
	gtk_label_set_markup(GTK_LABEL(m_pTimeDurationLabel), markup);
	g_free(markup);

	g_free(str_len);
	g_free(str_pos);

	if (pos > len)
		pos = len;

	gdouble progress = 0.;
	if (0 != len)
		progress = gdouble(pos)/len;

	/* block the signal handler to avoid re-seeking from a programmatic update */
	g_signal_handler_block(m_pPlayProgress, m_iPlayProgressChangeHandler);
	gtk_range_set_value(GTK_RANGE(m_pPlayProgress), progress);
	g_signal_handler_unblock(m_pPlayProgress, m_iPlayProgressChangeHandler);
}

void Viewer::ViewerImpl::ShowVideoPage()
{
	/* The video page is a transparent GtkFixed containing the GL sink, whose
	 * opacity is kept at 0 until the first frame of the new video is decoded
	 * (m_bVideoNeedsFirstFrame).  Switching the stack to that page before the
	 * frame is ready would therefore show the viewer's background colour for a
	 * frame or two — a flicker that overwrites the preview still on the image
	 * page.  So defer the page switch until the first frame arrives (done in
	 * video_paintable_invalidated_cb) and leave the image/preview page visible
	 * meanwhile.  When the frame is already present, switch immediately. */
	if (m_bVideoNeedsFirstFrame)
	{
		m_bVideoPagePending = TRUE;
	}
	else if (m_pStack != NULL && GTK_IS_STACK(m_pStack))
	{
		m_bVideoPagePending = FALSE;
		gtk_stack_set_visible_child_name(GTK_STACK(m_pStack), "video");
	}
	/* The picture is a child of the video fixed page; GtkStack shows the
	 * fixed page itself.  Still, StopVideo() explicitly hides the picture, so
	 * re-show it when the video page is brought up, or the sink picture stays
	 * invisible while audio plays. */
	if (m_pVideoFixed != NULL && GTK_IS_WIDGET(m_pVideoFixed))
		gtk_widget_set_visible(m_pVideoFixed, TRUE);
	if (m_pVideoSinkWidget != NULL && GTK_IS_WIDGET(m_pVideoSinkWidget))
	{
		gtk_widget_set_visible(m_pVideoSinkWidget, TRUE);
		gtk_widget_queue_draw(m_pVideoSinkWidget);
	}
}

void Viewer::ViewerImpl::PlayPauseVideo()
{
	if (!IsVideo())
	{
		return;
	}
	gchar* uri = NULL;
	g_object_get(G_OBJECT(m_pPipeline), "current-uri", &uri, NULL);
	gboolean same = (0 == g_strcmp0(uri, m_ImageListPtr->GetCurrent().GetURI()));
	if (same)
	{
		//gtk_widget_set_double_buffered (m_pImageView, FALSE); // Double buffering handled by gtk4sink
		ShowVideoPage();
		if (m_pVideoSinkWidget != NULL)
		{
			/* The GStreamer sink auto-shows its widget on the first buffer,
			 * overriding gtk_widget_hide, so use opacity to suppress the
			 * default-size display until the caps probe applies the zoom. */
			if (m_iVideoWidth > 0)
				gtk_widget_set_opacity(m_pVideoSinkWidget, 1.0);
			else
				gtk_widget_set_opacity(m_pVideoSinkWidget, 0.0);
		}
		/* has the right video */
		/* The toggle follows what the app already knows, not what the pipeline
		 * reports.  get_state only answers once every sink has prerolled, so
		 * while a preroll is pending it times out; gating the button on that
		 * made the click either do nothing or pause a second time, and the
		 * video could only be revived by seeking.  m_bIsPlaying is set by
		 * every play and pause (including EOS), so it is the state the button
		 * has been showing all along. */
		gboolean bWasPlaying = m_bIsPlaying;
		{
			if (bWasPlaying)
			{
				SetIsPlaying(false);
				gst_element_set_state(GST_ELEMENT(m_pPipeline), GST_STATE_PAUSED);
				CancelControlsFade();
				viewer_set_controls_visible(this, true);
				UpdateTimelineVisibility();
				TriggerPlayPauseAnimation(false);

				/* Freeze the slideshow at this video when the user manually
				 * pauses a video that was playing as part of the show (via
				 * hotkey, video click, or the play button), so the machine
				 * doesn't advance past it.  SlideShowPause() records the
				 * PLAYING_VIDEO state, so resuming continues this video. */
				if (m_bSlideShowRunning && !m_bSlideShowPaused && m_pViewer)
				{
					m_pViewer->SlideShowPause();
				}
			}
			else
			{
				SetIsPlaying(true);

				/* If playback stopped at the end of the video (EOS leaves the
				 * pipeline paused at the final position), resuming play from
				 * there does nothing — it immediately re-hits the end.  Restart
				 * from the beginning instead so pressing play always plays. */
				gint64 pos = 0, dur = 0;
				gst_element_query_position(GST_ELEMENT(m_pPipeline), GST_FORMAT_TIME, &pos);
				gst_element_query_duration(GST_ELEMENT(m_pPipeline), GST_FORMAT_TIME, &dur);
				if (dur > 0 && pos >= dur - 1000000) // within 1ms of the end, or past it
				{
					gst_element_seek(GST_ELEMENT(m_pPipeline),
						m_dPlaybackSpeed, GST_FORMAT_TIME,
						GstSeekFlags(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE),
						GST_SEEK_TYPE_SET, 0,
						GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);
				}

				if (m_pPipeline)
				{
					g_object_set(G_OBJECT(m_pPipeline), "mute", m_bMuted ? TRUE : FALSE, NULL);
				}
				gst_element_set_state(GST_ELEMENT(m_pPipeline), GST_STATE_PLAYING);
				TriggerPlayPauseAnimation(true);
				RefreshAutoHideTimer();
			}
		}
	}
	else
	{
		/* Playback starts where the quick preview was framed, so the stop that
		 * goes with loading the URI has to keep that framing: otherwise
		 * pressing play on a zoomed preview would reset the video to fit and
		 * the picture would jump from the framing the user chose to a fitted,
		 * centered frame.  The preview keeps being drawn (and stays borrowed)
		 * until the first frame replaces it, so there is no flicker either. */
		/* Whatever the quick preview is showing is where this video starts.
		 * AdoptVideoPreviewFraming picks up a framing that did not come from a
		 * zoom of the preview itself - a still that was zoomed to 500% carries
		 * its magnification onto the next file, so the preview is framed while
		 * the video's own remembered zoom is still the old one, and starting
		 * there would drop the picture back to 100% the moment play is pressed. */
		/* Read the framing off the preview here, while the preview is still what
		 * is on screen and its scroll position is the pan the user chose.  Every
		 * step below - stopping the pipeline, handing the image view back,
		 * bringing the video page up - takes the view off the screen or resets
		 * it, and a pan read after that is the view's own bookkeeping rather
		 * than the framing, which is how the frame used to start at the top left
		 * of the picture instead of where the preview was left. */
		CaptureVideoPanFromPreview();
		const bool bKeepPreviewFraming = m_bVideoZoomFromPreview || AdoptVideoPreviewFraming();
		StopVideo(false, bKeepPreviewFraming);
		/* Bring the video page up immediately (rather than waiting for
		 * ASYNC_DONE) so the picture area is visible from the first play.
		 * The picture is kept transparent until the first frame arrives. */
		ShowVideoPage();
		if (m_pVideoSinkWidget != NULL)
		{
			/* The GL sink needs a mapped widget (and therefore a live
			 * GL context) during preroll to upload the first frame.
			 * Keep it invisible via opacity so the old texture is never
			 * shown, but show the widget itself so the GL context
			 * exists.  The draw callback will restore opacity once the
			 * new frame is actually rendered. */
			gtk_widget_set_opacity(m_pVideoSinkWidget, 0.0);
		}
		if (m_pVideoFixed != NULL)
		/* Send explicit flush_start + flush_stop to drain any lingering
		 * buffers from the old video before loading the new URI.
		 * Going to NULL stops the pipeline but does NOT flush the
		 * downstream queue — residual decoded frames can sit in the
		 * GL sink's texture until the new video's first buffer
		 * overwrites them, causing a flash of the old frame.
		 * NOTE: events sent in NULL state are no-ops (no streaming
		 * thread).  The actual flush is sent in ASYNC_DONE below. */
		SetIsPlaying(true);
		g_object_set(G_OBJECT(m_pPipeline), "uri", m_ImageListPtr->GetCurrent().GetURI(), NULL);
		g_object_set(G_OBJECT(m_pPipeline), "mute", m_bMuted ? TRUE : FALSE, NULL);
		g_object_set(G_OBJECT(m_pPipeline), "volume", m_dVolume, NULL);
		gst_element_set_state(GST_ELEMENT(m_pPipeline), GST_STATE_PLAYING);

		TriggerPlayPauseAnimation(true);
		RefreshAutoHideTimer();
	}
	g_free(uri);
}

void Viewer::ViewerImpl::SeekRelative(gint64 seconds)
{
	if (!IsVideo() || m_pPipeline == NULL) return;
	GstFormat format = GST_FORMAT_TIME;
	gint64 clip_duration = 0;
	gint64 pos = 0;

	gboolean queried = gst_element_query_duration(GST_ELEMENT(m_pPipeline), format, &clip_duration);
	queried |= gst_element_query_position(m_pPipeline, format, &pos);
	if (queried)
	{
		gdouble speed = (m_dPlaybackSpeed > 0.0) ? m_dPlaybackSpeed : 1.0;
		gint64 target = pos + seconds * GST_SECOND;
		if (target < 0) target = 0;
		if (clip_duration > 0 && target > clip_duration) target = clip_duration;

		gboolean seek_started = gst_element_seek(GST_ELEMENT(m_pPipeline), speed,
			format, GstSeekFlags(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE),
			GST_SEEK_TYPE_SET, target,
			GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);
		(void)seek_started;
		CancelControlsFade();
		viewer_set_controls_visible(this, true);
		RefreshAutoHideTimer();
	}
}

void Viewer::ViewerImpl::SkipForward()
{
	SeekRelative(10);
}

void Viewer::ViewerImpl::SkipBack()
{
	SeekRelative(-10);
}

void Viewer::ViewerImpl::StopVideo(bool reloadImage /* = true */, bool keepPreviewFraming /* = false */)
{
	StopVideoPanSlowdown();
	m_bVideoPanSlowdownInterrupted = false;
	CancelPlayPauseAnimation();
	SetIsPlaying(false);

	gst_element_set_state(GST_ELEMENT(m_pPipeline), GST_STATE_NULL);
	if (m_pVideoSinkWidget != NULL)
	{
		gtk_widget_set_opacity(m_pVideoSinkWidget, 0.0);
	}
	/* The GL sink retains the old frame in its texture until the new
	 * video's first buffer arrives.  Mark that we need to defer the
	 * opacity restore so the old frame is never shown. */
	m_bVideoNeedsFirstFrame = TRUE;
	m_bVideoFlushPending = FALSE;
	m_bVideoPagePending = FALSE;
	/* the miniature belongs to the video that just stopped */
	ResetVideoPreviewViewState();
	//gtk_widget_set_double_buffered (m_pImageView, TRUE); // Double buffering handled by gtk4sink

	/* a zoom applied to the quick preview borrowed the image view: hand it
	 * back before the next image is delivered, so that image sees its own
	 * mode, magnification and center again - unless this stop is the one that
	 * starts the very video the preview was framing, in which case the borrow
	 * (and with it the framing) has to survive until the first frame arrives
	 * and takes the preview over */
	if (!keepPreviewFraming)
	{
		ReturnImageViewFromPreviewZoom();
		m_bVideoZoomFromPreview = false;
		m_bVideoPanFromPreview = false;
	}

	if (reloadImage && 0 != m_ImageListPtr->GetSize())
	{
		/* Load the image BEFORE switching the stack to avoid a grey flash
		 * where the image page is visible but not yet rendered. */
		if (gtk_widget_get_mapped(m_pImageView))
			gtk_widget_queue_draw(m_pImageView);
		if (0 == m_iTimeoutSlideshowID)
			LoadImage(m_ImageListPtr->GetCurrent());
	}
	if (m_pStack != NULL && GTK_IS_STACK(m_pStack))
		gtk_stack_set_visible_child_name(GTK_STACK(m_pStack), "image");

	/* reset the digital zoom so the next video starts at fit, and drop the
	 * stale frame size so a different-sized video is scaled to its own
	 * dimensions until the caps probe reports them */
	/* "keep zoom and pan" is a mode of the viewer, so it keeps the video's
	 * zoom and pan across videos exactly as the image view keeps an image's
	 * across images; every other mode starts the next video afresh. */
	if (keepPreviewFraming)
	{
		/* the zoom the preview was left at is the zoom this video starts at:
		 * only the fit level is unknown until the caps probe reports the real
		 * frame size, and the first ApplyVideoZoom() bounds it by that */
		m_dVideoZoomMin = 1.0;
	}
	else if (viewer_view_mode_is_keep(GetViewMode()))
	{
		m_dVideoZoomMin = 1.0;
	}
	else
	{
	m_dVideoZoom = 1.0;
	m_dVideoZoomFinal = 1.0;
	m_dVideoZoomMin = 1.0;
		m_eVideoViewMode = QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW_STRETCH;
	}
	m_DecoderGlitchTracker.Reset();
	m_SinkGlitchTracker.Reset();
	m_dPlaybackSpeed = 1.0;
	m_bVideoPlaybackStarted = false;
	m_bVideoZoomAnchorCenter = false;
	m_iVideoUserRotation = 0;
	if (m_pVideoRotatedPaintable)
	{
		quiver_rotated_paintable_set_rotation(m_pVideoRotatedPaintable, 0);
	}
	if (m_pPlayProgress && GTK_IS_RANGE(m_pPlayProgress))
	{
		g_signal_handler_block(m_pPlayProgress, m_iPlayProgressChangeHandler);
		gtk_range_set_value(GTK_RANGE(m_pPlayProgress), 0.0);
		g_signal_handler_unblock(m_pPlayProgress, m_iPlayProgressChangeHandler);
	}
	if (m_pTimeElapsedLabel && GTK_IS_LABEL(m_pTimeElapsedLabel))
		gtk_label_set_markup(GTK_LABEL(m_pTimeElapsedLabel), "<b>0:00</b>");
	if (m_pTimeDurationLabel && GTK_IS_LABEL(m_pTimeDurationLabel))
		gtk_label_set_markup(GTK_LABEL(m_pTimeDurationLabel), "<b>0:00</b>");
	if (m_pTimelineRow)
		set_control_visible(m_pTimelineRow, false);
	if (m_pSpeedLabel)
		gtk_label_set_markup(GTK_LABEL(m_pSpeedLabel), "<b>1x</b>");
	QuiverUtils::SetRadioActionCurrent("VideoSpeed10", 2);
	m_iVideoWidth = 0;
	m_iVideoHeight = 0;
	m_iVideoFpsNum = 0;
	m_iVideoFpsDen = 1;
	m_iVideoParN = 1;
	m_iVideoParD = 1;
	m_iVideoSinkW = 0;
	m_iVideoSinkH = 0;
	m_iVideoSinkX = 0;
	m_iVideoSinkY = 0;
	if (m_iVideoZoomTimeoutID != 0)
	{
		g_source_remove(m_iVideoZoomTimeoutID);
		m_iVideoZoomTimeoutID = 0;
	}
	if (!keepPreviewFraming && !viewer_view_mode_is_keep(GetViewMode()))
	{
		/* a kept pan is a fraction of the frame, so it needs no reset at all:
		 * the next video is framed the same way whatever its dimensions */
		ResetVideoPan();
	}
	if (!keepPreviewFraming)
	{
		/* whatever the view is showing now belongs to the item that is leaving,
		 * so its scroll position is not where this video is to play from.
		 *
		 * Unless the leaving item *is* the one whose preview is on screen and is
		 * about to be played: that scroll position is precisely where the frame
		 * is to start, and forgetting that it was still the right one stops
		 * CaptureVideoPanFromPreview() from reading it, so the zoom came across
		 * and the position did not - the frame started from wherever the pan fell
		 * back to instead of from where the user had put it. */
		m_sPreviewPanItem.clear();
	}
	m_dVideoLastWidgetW = 0.;
	m_dVideoLastFrameW = 0.;
	m_dVideoLastFrameH = 0.;
	m_dVideoLastFrameZoom = 0.;
	m_dVideoLastWidgetH = 0.;
	m_dVideoLastZc = 1.0;
	/* reset the zoom chain to its full-frame passthrough state while the
	 * source frame size is still known */
	ApplyVideoZoom();
	/* keep m_iVideoWidth/Height so the resize callback can re-apply the
	 * view mode immediately when the video page becomes visible again;
	 * the caps probe overwrites them for the new video on the first frame */
	m_bVideoZoomCropActive = FALSE;
	m_bVideoZoomInputCropActive = FALSE;
	if (m_pVideoZoomCaps != NULL)
	{
		g_object_set(G_OBJECT(m_pVideoZoomCaps), "caps", NULL, NULL);
	}
	/* undo the crop's forced system-memory conversion so the next video
	 * negotiates the fast VAMemory passthrough again */
	if (m_pVideoZoomInputCaps != NULL)
	{
		GstCaps* input = gst_caps_new_empty();
		GstCaps* va = gst_caps_new_empty_simple("video/x-raw");
		gst_caps_set_features(va, 0, gst_caps_features_from_string("memory:VAMemory"));
		gst_caps_append(input, va);
		gst_caps_append(input, gst_caps_new_empty_simple("video/x-raw"));
		g_object_set(G_OBJECT(m_pVideoZoomInputCaps), "caps", input, NULL);
		gst_caps_unref(input);
	}


	UpdateTimeline();
	UpdateCenterPlayButtonVisibility();

	if (IsVideo())
	{
		RefreshAutoHideTimer();
	}
}

bool Viewer::ViewerImpl::IsVideo() const
{
	return (0 != m_ImageListPtr->GetSize() && 
		m_ImageListPtr->GetCurrent().IsVideo());
}

void Viewer::ViewerImpl::SetPlaybackSpeed(double speed)
{
	if (speed <= 0.0) speed = 1.0;
	m_dPlaybackSpeed = speed;
	if (m_pSpeedLabel)
	{
		gchar* label = g_strdup_printf("<b>%gx</b>", speed);
		gtk_label_set_markup(GTK_LABEL(m_pSpeedLabel), label);
		g_free(label);
	}

	if (m_pPipeline != NULL)
	{
		gint64 pos = 0;
		if (gst_element_query_position(GST_ELEMENT(m_pPipeline), GST_FORMAT_TIME, &pos))
		{
			gst_element_seek(GST_ELEMENT(m_pPipeline), speed,
				GST_FORMAT_TIME,
				GstSeekFlags(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE),
				GST_SEEK_TYPE_SET, pos, GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);
		}
	}
}

void Viewer::ViewerImpl::Snapshot()
{
	if (!IsVideo())
		return;

	const gchar* uri = m_ImageListPtr->GetCurrent().GetURI();
	gint64 current_pos = 0;
	if (m_pPipeline != NULL)
		gst_element_query_position(GST_ELEMENT(m_pPipeline), GST_FORMAT_TIME, &current_pos);
	GdkTexture* texture = QuiverVideoOps::LoadTexture(uri, NULL, NULL, current_pos);
	if (texture != NULL)
	{
		gchar* path = g_filename_from_uri(uri, NULL, NULL);
		if (path != NULL)
		{
			gchar* dir = g_path_get_dirname(path);
			gchar* base = g_path_get_basename(path);
			gchar* ext = g_strrstr(base, ".");
			gchar* snap_name = NULL;
			if (ext != NULL)
			{
				*ext = '\0';
				snap_name = g_strdup_printf("%s/%s_snapshot.png", dir, base);
			}
			else
			{
				snap_name = g_strdup_printf("%s/%s.png", dir, base);
			}
			if (!gdk_texture_save_to_png(texture, snap_name))
			{
				g_warning("Snapshot save failed: %s", snap_name);
			}
			g_free(snap_name);
			g_free(dir);
			g_free(base);
			g_free(path);
		}
		g_object_unref(texture);
	}
}

/* Zooming a video before it is playing used to do nothing: the zoom lives with
 * the decoded frame, and there is no frame yet.  The quick preview the user is
 * looking at *is* a decoded still though - it is drawn by the image view while
 * the video page is still deferred - so the zoom is applied there instead,
 * scaled so the preview ends up the same size on screen the video will be.
 * The image view's own mode, magnification and center are borrowed for the
 * duration and handed back untouched, so the image that was shown before keeps
 * its "keep zoom and pan" state. */
void Viewer::ViewerImpl::BorrowImageViewForPreviewZoom()
{
	QuiverImageView* iv = QUIVER_IMAGE_VIEW(m_pImageView);
	if (NULL == iv || !QUIVER_IS_IMAGE_VIEW(iv))
		return;
	if (m_bPreviewZoomBorrowed)
		return;

	m_bPreviewZoomBorrowed = true;
	m_dPreviewZoomSavedMag = quiver_image_view_get_magnification(iv);
	m_bPreviewZoomSavedCenter = quiver_image_view_get_view_center(iv,
		&m_dPreviewZoomSavedCenterX, &m_dPreviewZoomSavedCenterY);
}

void Viewer::ViewerImpl::ReturnImageViewFromPreviewZoom()
{
	if (!m_bPreviewZoomBorrowed)
		return;
	m_bPreviewZoomBorrowed = false;

	QuiverImageView* iv = QUIVER_IMAGE_VIEW(m_pImageView);
	if (NULL == iv || !QUIVER_IS_IMAGE_VIEW(iv))
		return;

	/* The mode first, and it is the viewer's *current* mode rather than the one
	 * that happened to be set when the preview was zoomed: the view mode is the
	 * viewer's, and the user can change it while the preview is up - choosing
	 * "keep zoom and pan" there is the point of it.  Handing the image view a
	 * snapshot from before that choice silently undid it, so the next item came
	 * up in that older mode ("keep zoom and pan" turned back into "fit
	 * stretch", with the zoom and pan reset to go with it).  An unmagnified mode
	 * recomputes the magnification from the window, so the saved magnification
	 * has to be re-applied on top of it. */
	const QuiverImageViewMode mode = m_eVideoViewMode;
	quiver_image_view_set_view_mode(iv, mode);
	/* "Keep zoom and pan" is the exception: what was borrowed is the framing
	 * that mode keeps, so putting the snapshot back would undo the zoom the user
	 * just made to the preview and hand the next still the framing from before
	 * it - which is the one thing this mode exists to prevent.  The mode is
	 * still set above, so the view stays in it and keeps the framing. */
	if (viewer_view_mode_is_zoomed(mode) && !viewer_view_mode_is_keep(mode))
	{
		quiver_image_view_set_zoom_anchor_center(iv, TRUE);
		quiver_image_view_set_magnification(iv, m_dPreviewZoomSavedMag);
		quiver_image_view_set_zoom_anchor_center(iv, FALSE);
		if (m_bPreviewZoomSavedCenter)
		{
			quiver_image_view_set_view_center(iv,
				m_dPreviewZoomSavedCenterX, m_dPreviewZoomSavedCenterY);
		}
	}
}

void Viewer::ViewerImpl::ApplyVideoPreviewZoom(gdouble zoom)
{
	QuiverImageView* iv = QUIVER_IMAGE_VIEW(m_pImageView);
	if (NULL == iv || !QUIVER_IS_IMAGE_VIEW(iv) || zoom <= 0.)
		return;
	/* the preview is only on screen while the video page is still deferred */
	if (m_pStack != NULL && GTK_IS_STACK(m_pStack))
	{
		GtkWidget* page = gtk_stack_get_visible_child(GTK_STACK(m_pStack));
		if (page != m_pImageView)
			return;
	}

	GdkTexture* tex = quiver_image_view_get_texture(iv);
	if (NULL == tex || m_ImageListPtr == NULL)
		return;

	QuiverFile vf = m_ImageListPtr->GetCurrent();
	gint videoW = vf.GetWidth();
	gint videoH = vf.GetHeight();
	if (videoW <= 0 || videoH <= 0)
		return;

	/* The size to scale the zoom by is the size of the *picture*, not of the
	 * texture holding it: a magnification is relative to the picture and a zoom
	 * to the frame, so a 128x72 texture of a 1920x1080 frame would scale the zoom
	 * by 15 and put the preview at fifteen times the size it was asked for - and
	 * with it, the zoom to the pointer, whose anchor is computed from the same
	 * two magnifications. */
	gint pictureW = 0, pictureH = 0;
	if (!GetPreviewPictureSize(&pictureW, &pictureH))
		return;

	/* show the preview at the size the video will occupy at this zoom, so the
	 * framing the user chose is the framing playback starts with */
	gdouble mag = zoom * ((gdouble)videoW / (gdouble)pictureW);
	if (mag <= 0. || mag > 16.)
		return;

	BorrowImageViewForPreviewZoom();
	{
		/* Where it is looking stays where it was looking.  While the view is
		 * showing this video's own preview, its centre is that pan already; when
		 * it is not - the picture is still on its way, or a previous item is
		 * still up - the video's pan is the position to zoom about, and it is
		 * the centre of the frame until anything has put it elsewhere. */
		gdouble center_x = m_fVideoPanFX, center_y = m_fVideoPanFY;
		if (IsPreviewPanValid()
			&& !quiver_image_view_get_view_center(iv, &center_x, &center_y))
		{
			center_x = m_fVideoPanFX;
			center_y = m_fVideoPanFY;
		}
		/* Zoom toward the pointer, the way a picture does.  What a zoom to the
		 * cursor keeps is the point of the picture under the pointer, so that
		 * point is the anchor and the centre moves by however much of the
		 * picture comes and goes around it: a magnification shows 1/mag of the
		 * picture, and the pointer sits a fraction of a viewport off the middle,
		 * so its point shifts by that fraction of (1/mag before - 1/mag after).
		 *
		 * Zooming to the pointer moves the picture, so the pan is read after it
		 * rather than before: a pan taken at the old centre would have the video
		 * start where the zoom was, and not where the zoom left the picture. */
		gdouble px = -1., py = -1.;
		video_zoom_get_pointer_in(this, GTK_WIDGET(iv), &px, &py);
		const gdouble widget_w = gtk_widget_get_width(GTK_WIDGET(iv));
		const gdouble widget_h = gtk_widget_get_height(GTK_WIDGET(iv));
		const gdouble old_mag = quiver_image_view_get_magnification(iv);
		if (px >= 0. && py >= 0. && widget_w > 0. && widget_h > 0.
			&& old_mag > 0. && mag > 0.)
		{
			const gdouble shift_x = (px / widget_w - 0.5) * (1. / old_mag - 1. / mag);
			const gdouble shift_y = (py / widget_h - 0.5) * (1. / old_mag - 1. / mag);
			center_x = CLAMP(center_x + shift_x, 0., 1.);
			center_y = CLAMP(center_y + shift_y, 0., 1.);
		}
		quiver_image_view_set_framing(iv, mag, center_x, center_y);
	}

	/* Keep the framing the preview ended up at as a fraction of the frame, the
	 * same way the video's own pan is kept, and show it in the nav control: a
	 * zoomed preview is exactly when the box has something to say. */
	CaptureVideoPanFromPreview();
	UpdateVideoPreviewViewAreaFromImageView();
	UpdateNavControlVisibility();
	/* the preview has just been framed to the zoom the video will play at, so
	 * from here on where the user puts it is where the video goes */
	if (IsVideoPreviewShowing() && m_ImageListPtr != NULL)
		m_sPreviewPanItem = m_ImageListPtr->GetCurrent().GetFilePath();
	else
		m_sPreviewPanItem.clear();
}

void Viewer::ViewerImpl::RotateVideo(bool clockwise)
{
	m_iVideoUserRotation = (m_iVideoUserRotation + (clockwise ? 90 : 270)) % 360;
	if (m_pVideoRotatedPaintable)
	{
		quiver_rotated_paintable_set_rotation(m_pVideoRotatedPaintable, m_iVideoUserRotation);
	}
	m_dVideoLastWidgetW = 0.;
	m_dVideoLastFrameW = 0.;
	m_dVideoLastFrameH = 0.;
	m_dVideoLastFrameZoom = 0.;
	m_dVideoLastWidgetH = 0.;
	m_iVideoSinkW = 0;
	m_iVideoSinkH = 0;
	m_iVideoSinkX = -999999;
	m_iVideoSinkY = -999999;
	ApplyVideoZoom();
	if (m_pVideoSinkWidget)
	{
		gtk_widget_queue_draw(m_pVideoSinkWidget);
	}
}

void Viewer::ViewerImpl::ToggleMute()
{
	SetMuted(!m_bMuted);
}

void Viewer::ViewerImpl::SetMuted(bool bMute)
{
	m_bMuted = bMute;
	if (!m_bMuted && m_dVolume <= 0.01)
	{
		m_dVolume = 0.5;
		if (m_pVolumeScale)
		{
			gtk_range_set_value(GTK_RANGE(m_pVolumeScale), m_dVolume);
		}
	}
	if (m_pPipeline)
	{
		g_object_set(G_OBJECT(m_pPipeline), "mute", m_bMuted ? TRUE : FALSE, NULL);
		if (!m_bMuted)
		{
			g_object_set(G_OBJECT(m_pPipeline), "volume", m_dVolume, NULL);
		}
	}
	UpdateVolumeUI();
}

void Viewer::ViewerImpl::UpdateVolumeUI()
{
	const char *icon_name = "audio-volume-high-symbolic";
	if (m_bMuted || m_dVolume <= 0.001)
	{
		icon_name = "audio-volume-muted-symbolic";
	}
	else if (m_dVolume < 0.33)
	{
		icon_name = "audio-volume-low-symbolic";
	}
	else if (m_dVolume < 0.67)
	{
		icon_name = "audio-volume-medium-symbolic";
	}

	if (m_pVolumeButton)
	{
		gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(m_pVolumeButton), icon_name);
		gtk_widget_set_tooltip_text(m_pVolumeButton, m_bMuted ? "Volume (Muted - Click to Open, M to Unmute)" : "Volume (Click to Open, M to Mute)");
	}
	if (m_pVolumeMuteBtn)
	{
		gtk_button_set_icon_name(GTK_BUTTON(m_pVolumeMuteBtn), m_bMuted ? "audio-volume-muted-symbolic" : "audio-volume-high-symbolic");
		gtk_widget_set_tooltip_text(m_pVolumeMuteBtn, m_bMuted ? "Unmute (M)" : "Mute (M)");
	}
}

static void 
viewer_button_release_cb(GtkGestureClick *gesture, gint n_press, gdouble x, gdouble y, gpointer user_data)
{
	(void)n_press;
	GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
	Viewer::ViewerImpl *pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)user_data;
	if (widget == pViewerImpl->m_pImageView && pViewerImpl->m_bVideoPreviewClick)
	{
		pViewerImpl->m_bVideoPreviewClick = false;
		if (ABS(x - pViewerImpl->m_dVideoPreviewClickX) < 5.
			&& ABS(y - pViewerImpl->m_dVideoPreviewClickY) < 5.)
		{
			if (!pViewerImpl->IsPointOverControlsOrFilmstrip(widget, x, y))
			{
				if (pViewerImpl->IsVideo())
				{
					pViewerImpl->PlayPauseVideo();
				}
				else if (pViewerImpl->m_bSlideShowRunning && pViewerImpl->m_pViewer)
				{
					pViewerImpl->m_pViewer->SlideShowTogglePause();
					pViewerImpl->TriggerPlayPauseAnimation(!pViewerImpl->m_bSlideShowPaused);
				}
			}
		}
		pViewerImpl->RefreshAutoHideTimer();
	}
	else if ((widget == pViewerImpl->m_pVideoFixed || widget == pViewerImpl->m_pVideoSinkWidget)
		&& pViewerImpl->m_bVideoPanning)
	{
		pViewerImpl->m_bVideoPanning = FALSE;

		/* a click (no meaningful drag) toggles play/pause, unless this click was
		 * arresting an in-progress kinetic scroll */
		if (ABS(x - pViewerImpl->m_dVideoPanStartRootX) < 5.
			&& ABS(y - pViewerImpl->m_dVideoPanStartRootY) < 5.)
		{
			pViewerImpl->StopVideoPanSlowdown();
			if (!pViewerImpl->m_bVideoPanSlowdownInterrupted && pViewerImpl->IsVideo()
				&& !pViewerImpl->IsPointOverControlsOrFilmstrip(widget, x, y))
			{
				pViewerImpl->PlayPauseVideo();
			}
			pViewerImpl->m_bVideoPanSlowdownInterrupted = false;
		}
		else
		{
			pViewerImpl->m_bVideoPanSlowdownInterrupted = false;
			gint64 now = g_get_monotonic_time();
			if (pViewerImpl->m_iVideoPanLastMotionTime > 0)
			{
				gdouble dt = (gdouble)(now - pViewerImpl->m_iVideoPanLastMotionTime) / 1000000.0;
				if (dt >= 0.005)
				{
					gdouble dx = x - pViewerImpl->m_dVideoPanLastMotionX;
					gdouble dy = y - pViewerImpl->m_dVideoPanLastMotionY;
					const gdouble max_vel = 12000.0;
					gdouble dist = std::hypot(dx, dy);
					if (dist > max_vel * dt && dist > 0.)
					{
						dx = (dx / dist) * (max_vel * dt);
						dy = (dy / dist) * (max_vel * dt);
					}
					pViewerImpl->RecordVideoPanSample(dx, dy, dt);
					pViewerImpl->m_dVideoPanLastMotionX = x;
					pViewerImpl->m_dVideoPanLastMotionY = y;
					pViewerImpl->m_iVideoPanLastMotionTime = now;
				}
			}

			gdouble time_since_motion = (pViewerImpl->m_iVideoPanLastMotionTime > 0)
				? (gdouble)(now - pViewerImpl->m_iVideoPanLastMotionTime) / 1000000.0
				: 1.0;

			if (pViewerImpl->m_bKineticScrolling &&
				time_since_motion < 0.15 &&
				pViewerImpl->m_iVideoPanSampleCount > 0)
			{
				pViewerImpl->StartVideoPanSlowdown();
			}
			else
			{
				pViewerImpl->StopVideoPanSlowdown();
			}
		}
		pViewerImpl->RefreshAutoHideTimer();
	}
}

static void 
viewer_button_press_cb(GtkGestureClick *gesture, gint n_press, gdouble x, gdouble y, gpointer user_data)
{
	GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
	Viewer::ViewerImpl *pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)user_data;

	guint button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));
	if (button == 0)
	{
		button = 1;
	}

	// Middle click: toggle windowed <-> fullscreen
	if (2 == button)
	{
		GAction *fs = QuiverUtils::GetAction("FullScreen");
		if (fs != NULL)
		{
			g_action_activate(fs, NULL);
		}
		return;
	}

	// Double-click on a playing video returns to the browser, matching the
	// image view's double-click toggle.  (The video preview shown in the
	// image view is already handled by QuiverImageView's "activated"
	// signal; the playing video surface has no such signal.)
	if (1 == button && 2 == n_press
		&& (widget == pViewerImpl->m_pVideoFixed || widget == pViewerImpl->m_pVideoSinkWidget)
		&& pViewerImpl->IsVideo())
	{
		if (pViewerImpl->IsPointOverControlsOrFilmstrip(widget, x, y))
		{
			return;
		}
		pViewerImpl->m_pViewer->EmitItemActivatedEvent();
		return;
	}

	// Tap / pointer press: reveal controls if hidden
	if (1 == button)
	{
		if (viewer_controls_need_reshow(pViewerImpl))
		{
			viewer_controls_show(pViewerImpl);
			pViewerImpl->StartControlsFade(true);
			if (pViewerImpl->IsVideo())
			{
				pViewerImpl->UpdateTimelineVisibility();
			}
		}
		pViewerImpl->RefreshAutoHideTimer();
	}

	if (widget == pViewerImpl->m_pImageView || widget == pViewerImpl->m_pVideoFixed
		|| widget == pViewerImpl->m_pVideoSinkWidget) 
	{
		if (3 == button)
		{
			viewer_show_context_menu(widget, x, y, 0, user_data);
			return;
		}
		else if (1 == button)
		{
			if (pViewerImpl->IsPointOverControlsOrFilmstrip(widget, x, y))
			{
				return;
			}
			if (widget == pViewerImpl->m_pImageView &&
				(pViewerImpl->IsVideo() || pViewerImpl->m_bSlideShowRunning))
			{
				pViewerImpl->m_bVideoPreviewClick = true;
				pViewerImpl->m_dVideoPreviewClickX = x;
				pViewerImpl->m_dVideoPreviewClickY = y;
			}
			else if (widget == pViewerImpl->m_pVideoFixed || widget == pViewerImpl->m_pVideoSinkWidget)
			{
				if (pViewerImpl->IsVideo())
				{
					bool wasSlowdownActive = pViewerImpl->m_bVideoPanSlowdownActive;
					pViewerImpl->StopVideoPanSlowdown();
					pViewerImpl->m_bVideoPanSlowdownInterrupted = wasSlowdownActive;
					pViewerImpl->m_bVideoPanning = TRUE;
					pViewerImpl->m_dVideoPanStartRootX = x;
					pViewerImpl->m_dVideoPanStartRootY = y;
					pViewerImpl->m_dVideoPanStartPX = pViewerImpl->m_dVideoPanX;
					pViewerImpl->m_dVideoPanStartPY = pViewerImpl->m_dVideoPanY;
					pViewerImpl->m_dVideoPanLastMotionX = x;
					pViewerImpl->m_dVideoPanLastMotionY = y;
					pViewerImpl->m_iVideoPanLastMotionTime = g_get_monotonic_time();
					pViewerImpl->m_dVideoPanVelX = 0.;
					pViewerImpl->m_dVideoPanVelY = 0.;
					pViewerImpl->m_iVideoPanSampleCount = 0;
					viewer_set_controls_visible(pViewerImpl, true);
					pViewerImpl->RefreshAutoHideTimer();
					return;
				}
			}
		}
	}
	else
	{
		if (1 == button)
		{
			// play video
			if (pViewerImpl->IsVideo())
			{
				pViewerImpl->RefreshAutoHideTimer();
				pViewerImpl->PlayPauseVideo();
			}
		}
	}
}

/* The quick preview is drawn by the image view, so its zoom and pan are this
 * widget's scroll position: the nav control's box follows them, which is what
 * shows the viewport while a video is being framed before it plays. */
static void viewer_image_adjustment_changed_cb(GtkAdjustment *adjustment, gpointer user_data)
{
	(void)adjustment;
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl *)user_data;
	if (p == NULL || !p->m_spAlive || !*p->m_spAlive)
		return;
	if (!p->IsVideoPreviewShowing())
		return;
	/* Only the nav control's box follows the preview live here.  The video's own
	 * pan is read from the preview when the framing matters - when the preview is
	 * zoomed and when playback starts (AdoptVideoPreviewFraming) - because this
	 * callback also fires while a *new* file's thumbnail is being laid out, and
	 * the framing that arrives with a new item is not the user's; taking it
	 * would drop the pan that "keep zoom and pan" is supposed to carry over. */
	p->UpdateVideoPreviewViewAreaFromImageView();
	p->UpdateNavControlVisibility();
}

static void viewer_menu_item(GMenu *menu, const char *label, const char *action_name,
	const char *accel, const char *item_id, const char *icon_name = NULL)
{
	GMenuItem *item = g_menu_item_new(label, action_name);
	if (accel != NULL && accel[0] != '\0')
		g_menu_item_set_attribute(item, "accel", "s", accel);
	if (item_id != NULL && item_id[0] != '\0')
		g_menu_item_set_attribute(item, "id", "s", item_id);
	if (icon_name != NULL && icon_name[0] != '\0')
		g_menu_item_set_attribute(item, "icon", "s", icon_name);
	g_menu_append_item(menu, item);
	g_object_unref(item);
}

/* Undo Delete is only offered when some item in the undo stack was moved to
 * trash while the current image list was on screen, i.e. it came from the
 * same folder as the file being viewed. */
static bool viewer_undo_applies_to_current_list(Viewer::ViewerImpl *impl)
{
	if (!QuiverFileOps::UndoStackHasItems())
		return false;
	if (0 == impl->m_ImageListPtr->GetSize())
		return false;

	QuiverFile cur = impl->m_ImageListPtr->GetCurrent();
	GFile *file = g_file_new_for_uri(cur.GetURI());
	GFile *parent = g_file_get_parent(file);
	char *folder_uri = (NULL != parent) ? g_file_get_uri(parent) : NULL;
	if (NULL != parent)
		g_object_unref(parent);
	g_object_unref(file);
	if (NULL == folder_uri)
		return false;

	const QuiverFileOps::UndoEntry *top = QuiverFileOps::UndoStackEntryAt(0);
	if (NULL == top || top->type != QuiverFileOps::UNDO_TYPE_DELETE)
	{
		g_free(folder_uri);
		return false;
	}

	bool found = false;
	for (std::list<QuiverFile>::const_iterator it = top->trashed_files.begin();
		 it != top->trashed_files.end(); ++it)
	{
		GFile *tf = g_file_new_for_uri(it->GetURI());
		GFile *tp = g_file_get_parent(tf);
		char *turi = (NULL != tp) ? g_file_get_uri(tp) : NULL;
		if (NULL != tp)
			g_object_unref(tp);
		g_object_unref(tf);
		if (NULL != turi)
		{
			if (0 == strcmp(turi, folder_uri))
				found = true;
			g_free(turi);
			if (found)
				break;
		}
	}
	g_free(folder_uri);
	return found;
}

static void viewer_show_context_menu(GtkWidget *widget, gdouble x_root, gdouble y_root, guint32 /*time*/, gpointer userdata)
{
	Viewer::ViewerImpl *pViewerImpl = (Viewer::ViewerImpl*)userdata;

	if (NULL == pViewerImpl->m_pContextMenuPopover)
	{
		GMenu *menu = g_menu_new();
		GtkWidget *popover = gtk_popover_menu_new_from_model(G_MENU_MODEL(menu));
		g_object_unref(menu);

		gtk_widget_insert_action_group(popover, "quiver", G_ACTION_GROUP(QuiverUtils::GetActionGroup()));

		/* Trash title row: the original name + location of the shown item. */
		pViewerImpl->m_pContextMenuInfoLabel = QuiverUtils::MakeMenuTitleLabel("", "");
		gtk_widget_set_visible(pViewerImpl->m_pContextMenuInfoLabel, FALSE);
		gtk_popover_menu_add_child(GTK_POPOVER_MENU(popover),
			pViewerImpl->m_pContextMenuInfoLabel, "viewer-trash-title");

		pViewerImpl->m_pContextMenuPopover = popover;
		gtk_widget_set_parent(popover, widget);
	}

	const bool bTrash = (0 != pViewerImpl->m_ImageListPtr->GetSize())
		&& QuiverFileOps::IsTrashURI(pViewerImpl->m_ImageListPtr->GetCurrent().GetURI());

	/* Trash title row: file name + original location of the shown item. */
	bool bShowTitle = false;
	if (bTrash && 0 != pViewerImpl->m_ImageListPtr->GetSize())
	{
		QuiverFile f = pViewerImpl->m_ImageListPtr->GetCurrent();
		char *orig = QuiverFileOps::GetTrashItemOrigPath(f.GetURI());
		if (NULL != orig)
		{
			char *name = g_path_get_basename(orig);
			char *dir  = g_path_get_dirname(orig);
			gchar *markup = g_markup_escape_text(name ? name : orig, -1);
			gtk_label_set_markup(
				GTK_LABEL(pViewerImpl->m_pContextMenuInfoLabel), markup);
			g_free(markup);
			g_free(dir);
			g_free(name);
			g_free(orig);
			bShowTitle = true;
		}
	}
	gtk_widget_set_visible(pViewerImpl->m_pContextMenuInfoLabel, bShowTitle);
	const char *title_id = bShowTitle ? "viewer-trash-title" : NULL;

	/* Rebuild the model: items depend on trash vs. normal browsing. */
	GMenu *menu = g_menu_new();
	if (bTrash)
	{
		viewer_menu_item(menu, "Copy", "quiver." ACTION_VIEWER_COPY,
			"<Control>c", title_id, "edit-copy-symbolic");
		GMenu *trash_section = g_menu_new();
		viewer_menu_item(trash_section, "Delete Permanently", "quiver." ACTION_VIEWER_TRASH,
			"Delete", NULL, "edit-delete-symbolic");
		viewer_menu_item(trash_section, "Restore From Trash", "quiver." ACTION_VIEWER_RESTORE,
			NULL, NULL, "edit-undo-symbolic");
		if (viewer_undo_applies_to_current_list(pViewerImpl) &&
			NULL != QuiverUtils::GetAction("UndoDelete"))
			viewer_menu_item(trash_section, "Undo Delete", "quiver.UndoDelete", "<Control>z", NULL, "edit-undo-symbolic");
		g_menu_append_section(menu, NULL, G_MENU_MODEL(trash_section));
		g_object_unref(trash_section);
	}
	else
	{
		viewer_menu_item(menu, "Copy", "quiver." ACTION_VIEWER_COPY,
			"<Control>c", title_id, "edit-copy-symbolic");
		viewer_menu_item(menu, "Rename", "quiver." ACTION_QUIVER_QUICK_RENAME,
			"F2", NULL, "document-edit-symbolic");
		GMenu *rotate_section = g_menu_new();
		viewer_menu_item(rotate_section, "Rotate Counterclockwise",
			"quiver." ACTION_VIEWER_ROTATE_CCW, "<Shift>r", NULL, "object-rotate-left-symbolic");
		viewer_menu_item(rotate_section, "Rotate Clockwise",
			"quiver." ACTION_VIEWER_ROTATE_CW, "r", NULL, "object-rotate-right-symbolic");
		g_menu_append_section(menu, NULL, G_MENU_MODEL(rotate_section));
		g_object_unref(rotate_section);
		GMenu *trash_section = g_menu_new();
		viewer_menu_item(trash_section, "Move To Trash", "quiver." ACTION_VIEWER_TRASH,
			"Delete", NULL, "user-trash-symbolic");
		if (viewer_undo_applies_to_current_list(pViewerImpl) &&
			NULL != QuiverUtils::GetAction("UndoDelete"))
			viewer_menu_item(trash_section, "Undo Delete", "quiver.UndoDelete", "<Control>z", NULL, "edit-undo-symbolic");
		g_menu_append_section(menu, NULL, G_MENU_MODEL(trash_section));
		g_object_unref(trash_section);
	}

	gtk_popover_menu_set_menu_model(
		GTK_POPOVER_MENU(pViewerImpl->m_pContextMenuPopover), G_MENU_MODEL(menu));
	g_object_unref(menu);

	QuiverUtils::ShowContextMenuAt(
		GTK_POPOVER(pViewerImpl->m_pContextMenuPopover), widget, x_root, y_root);
}




/* Every HUD widget is owned by the widget tree, and the window may destroy
 * that tree before the last shared_ptr to the viewer is dropped (the caller
 * owns the window, not us).  The cached pointers would then be dangling, and
 * the guarded teardown below would type-check freed memory.  Drop them as
 * soon as the tree goes away so the destructor only ever sees NULL. */
static void viewer_widget_tree_destroyed_cb(GtkWidget *widget, gpointer user_data)
{
	(void)widget;
	Viewer::ViewerImpl *pViewer = (Viewer::ViewerImpl *)user_data;
	if (NULL == pViewer)
	{
		return;
	}

	pViewer->m_pHBox = NULL;
	pViewer->m_pVBox = NULL;
	pViewer->m_pGrid = NULL;
	pViewer->m_pOverlay = NULL;
	pViewer->m_pStack = NULL;
	pViewer->m_pControlsBox = NULL;
	pViewer->m_pMediaControls = NULL;
	pViewer->m_pTransportRow = NULL;
	pViewer->m_pImageView = NULL;
	/* borrowed from the image view, not owned: they go with it */
	pViewer->m_pAdjustmentH = NULL;
	pViewer->m_pAdjustmentV = NULL;
	pViewer->m_pIconView = NULL;
	pViewer->m_pImageErrorLabel = NULL;
	pViewer->m_pScrollbarH = NULL;
	pViewer->m_pScrollbarV = NULL;
	pViewer->m_pNavigationControl = NULL;
	pViewer->m_pNavControlPill = NULL;
	pViewer->m_pSlideShowPausedPill = NULL;

	pViewer->m_pViewerOverlayBar = NULL;
	pViewer->m_pViewerPrevBtn = NULL;
	pViewer->m_pViewerNextBtn = NULL;
	pViewer->m_pViewerSlideshowBtn = NULL;
	pViewer->m_pViewerVideoSlideshowBtn = NULL;
	pViewer->m_pPlayButton = NULL;
	pViewer->m_pCenterPlayBtn = NULL;
	pViewer->m_pPlayImage = NULL;
	pViewer->m_pPlayAnimWidget = NULL;
	pViewer->m_pPlayAnimImage = NULL;
	pViewer->m_pViewerRotateCcwBtn = NULL;
	pViewer->m_pViewerRotateCwBtn = NULL;
	pViewer->m_pViewerFlipHBtn = NULL;
	pViewer->m_pViewerFlipVBtn = NULL;
	pViewer->m_pRewindBtn = NULL;
	pViewer->m_pFfBtn = NULL;
	pViewer->m_pViewerZoomOutBtn = NULL;
	pViewer->m_pViewerZoomFitBtn = NULL;
	pViewer->m_pViewModeSplitBox = NULL;
	pViewer->m_pViewModeSplitSep = NULL;
	pViewer->m_pViewModeMenuBtn = NULL;
	pViewer->m_pViewModeMenuPopover = NULL;
	pViewer->m_pViewerZoomInBtn = NULL;
	pViewer->m_pSepNavigation = NULL;
	pViewer->m_pSepRotate = NULL;
	pViewer->m_pSepZoom = NULL;

	pViewer->m_pImageBlank1 = NULL;
	pViewer->m_pImageBlank2 = NULL;
	pViewer->m_pVideoBlank1 = NULL;
	pViewer->m_pVideoBlank2 = NULL;
	pViewer->m_pVolumeButton = NULL;
	pViewer->m_pVolumePopover = NULL;
	pViewer->m_pVolumeScale = NULL;
	pViewer->m_pVolumeMuteBtn = NULL;
	pViewer->m_pSpeedButton = NULL;
	pViewer->m_pSpeedLabel = NULL;
	pViewer->m_pSnapBtn = NULL;
	pViewer->m_pFullscreenBtn = NULL;
	pViewer->m_pViewerFullscreenBtn = NULL;
	pViewer->m_pImageSubmenuBtn = NULL;
	pViewer->m_pImageSubmenuPopover = NULL;
	pViewer->m_pVideoOptionsBtn = NULL;
	pViewer->m_pVideoOptionsPopover = NULL;
	pViewer->m_pVideoSinkWidget = NULL;
	pViewer->m_pVideoFixed = NULL;
	pViewer->m_pTimeline = NULL;
	pViewer->m_pTimelineRow = NULL;
	pViewer->m_pTimeElapsedLabel = NULL;
	pViewer->m_pTimeDurationLabel = NULL;
	pViewer->m_pPlayProgress = NULL;
	pViewer->m_pFilmstripEdge = NULL;
	pViewer->m_pFilmstripOverlayContainer = NULL;
	pViewer->m_pContextMenuPopover = NULL;
	pViewer->m_pContextMenuTrashBtn = NULL;
	pViewer->m_pContextMenuRestoreBtn = NULL;
	pViewer->m_pContextMenuInfoLabel = NULL;
	pViewer->m_pDragSource = NULL;
	pViewer->m_pDropTarget = NULL;
}

Viewer::ViewerImpl::~ViewerImpl()
{
	/* The action group is global and outlives this object: the actions
	 * registered with this pointer as user_data would keep calling into
	 * freed memory (an action toggled by a menu item, a keybinding or
	 * UpdateUI() would run the handler below). */
	QuiverUtils::RemoveActionsFor(this);
	m_bShuttingDown = true;
	StopVideoPanSlowdown();
	ShortcutManager::GetInstance().RemoveShortcutsChangedCallback(viewer_shortcuts_changed_cb, this);
	if (m_spAlive)
	{
		*m_spAlive = false;
	}
	m_ThumbnailLoader.Stop();

	if (m_pIconView && QUIVER_IS_ICON_VIEW(m_pIconView))
	{
		g_signal_handlers_disconnect_by_func(m_pIconView, (gpointer)viewer_icon_view_map_cb, this);
		g_signal_handlers_disconnect_by_func(m_pIconView, (gpointer)viewer_icon_view_unmap_cb, this);
		quiver_icon_view_set_n_items_func(QUIVER_ICON_VIEW(m_pIconView), NULL, NULL, NULL);
		quiver_icon_view_set_thumbnail_texture_func(QUIVER_ICON_VIEW(m_pIconView), NULL, NULL, NULL);
		quiver_icon_view_set_icon_texture_func(QUIVER_ICON_VIEW(m_pIconView), NULL, NULL, NULL);
		quiver_icon_view_set_text_func(QUIVER_ICON_VIEW(m_pIconView), NULL, NULL, NULL);
		quiver_icon_view_set_overlay_texture_func(QUIVER_ICON_VIEW(m_pIconView), NULL, NULL, NULL);
		quiver_icon_view_set_get_filmstrip_texture_func(QUIVER_ICON_VIEW(m_pIconView), NULL, NULL, NULL);
#if HAVE_GDK_PIXBUF
		quiver_icon_view_set_thumbnail_pixbuf_func(QUIVER_ICON_VIEW(m_pIconView), NULL, NULL, NULL);
		quiver_icon_view_set_icon_pixbuf_func(QUIVER_ICON_VIEW(m_pIconView), NULL, NULL, NULL);
		quiver_icon_view_set_overlay_pixbuf_func(QUIVER_ICON_VIEW(m_pIconView), NULL, NULL, NULL);
#endif
	}

	if (0 != m_iGstBusWatchID)
	{
		g_source_remove(m_iGstBusWatchID);
		m_iGstBusWatchID = 0;
	}

	if (m_pVideoRotatedPaintable && G_IS_OBJECT(m_pVideoRotatedPaintable))
	{
		g_object_unref(m_pVideoRotatedPaintable);
		m_pVideoRotatedPaintable = NULL;
	}

	if (m_pVideoPaintable && G_IS_OBJECT(m_pVideoPaintable))
	{
		g_signal_handlers_disconnect_by_data(m_pVideoPaintable, this);
		g_object_unref(m_pVideoPaintable);
		m_pVideoPaintable = NULL;
	}

	if (m_pPipeline != NULL)
	{
		GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(m_pPipeline));
		if (bus != NULL)
		{
			gst_bus_set_flushing(bus, TRUE);
			gst_object_unref(bus);
		}
	}

	StopVideo(false);
	CancelPlayPauseAnimation();

	if (m_pPipeline != NULL)
	{
		gst_object_unref(GST_OBJECT(m_pPipeline));
		m_pPipeline = NULL;
	}

	/* cancel every main-loop callback that captures `this` so none can fire
	 * into the freed object after the teardown idle returns */
	if (0 != m_iIdleSetIndex)
	{
		g_source_remove(m_iIdleSetIndex);
		m_iIdleSetIndex = 0;
	}
	if (0 != m_iVideoZoomIdle)
	{
		g_source_remove(m_iVideoZoomIdle);
		m_iVideoZoomIdle = 0;
	}
	if (0 != m_iTimeoutUpdateListID)
	{
		g_source_remove(m_iTimeoutUpdateListID);
		m_iTimeoutUpdateListID = 0;
	}
	if (0 != m_iTimeoutSlideshowID)
	{
		g_source_remove(m_iTimeoutSlideshowID);
		m_iTimeoutSlideshowID = 0;
	}
	if (0 != m_iTimeoutMouseMotionNotify)
	{
		g_source_remove(m_iTimeoutMouseMotionNotify);
		m_iTimeoutMouseMotionNotify = 0;
	}
	if (0 != m_iTimeoutPlayProgress)
	{
		g_source_remove(m_iTimeoutPlayProgress);
		m_iTimeoutPlayProgress = 0;
	}
	if (0 != m_iTimeoutScrollbars)
	{
		g_source_remove(m_iTimeoutScrollbars);
		m_iTimeoutScrollbars = 0;
	}
	if (0 != m_iTimeoutFilmstripFade)
	{
		g_source_remove(m_iTimeoutFilmstripFade);
		m_iTimeoutFilmstripFade = 0;
	}
	if (0 != m_iTimeoutControlsFade)
	{
		g_source_remove(m_iTimeoutControlsFade);
		m_iTimeoutControlsFade = 0;
	}
	if (0 != m_iTimeoutNavControlFade)
	{
		g_source_remove(m_iTimeoutNavControlFade);
		m_iTimeoutNavControlFade = 0;
	}
	/* The auto-hide timer hides the filmstrip by starting the fade, so it is the
	 * one timer that reaches a widget itself: left running it fires after the
	 * viewer is gone and fades a widget that has already been destroyed. */
	CancelFilmstripHide();
	if (0 != m_iVideoPreviewRedrawIdle)
	{
		g_source_remove(m_iVideoPreviewRedrawIdle);
		m_iVideoPreviewRedrawIdle = 0;
		m_bVideoPreviewRedrawQueued = FALSE;
	}
	if (0 != m_iVideoZoomTimeoutID)
	{
		g_source_remove(m_iVideoZoomTimeoutID);
		m_iVideoZoomTimeoutID = 0;
	}

	CancelFilmstripHide();

	m_ImageLoader.RemovePixbufLoaderObserver(m_StatusbarPtr.get());
	m_ImageLoader.RemovePixbufLoaderObserver(m_PixbufLoaderObserverPtr.get());

	/* Quiesce the loader before m_ThumbnailCache (which it is bridged to)
	 * is destroyed below. */
	m_ImageLoader.StopThread();

	/* popovers are parented to viewer widgets (buttons / icon view); unparent
	 * them NOW while their parents are still alive, or they would be left with
	 * a dangling parent pointer when the widget tree below is destroyed */
	if (m_pVolumeButton && GTK_IS_MENU_BUTTON(m_pVolumeButton))
	{
		gtk_menu_button_set_popover(GTK_MENU_BUTTON(m_pVolumeButton), NULL);
	}
	m_pVolumePopover = NULL;

	if (m_pVideoOptionsBtn && GTK_IS_MENU_BUTTON(m_pVideoOptionsBtn))
	{
		gtk_menu_button_set_popover(GTK_MENU_BUTTON(m_pVideoOptionsBtn), NULL);
	}
	m_pVideoOptionsPopover = NULL;

	if (m_pImageSubmenuBtn && GTK_IS_MENU_BUTTON(m_pImageSubmenuBtn))
	{
		gtk_menu_button_set_popover(GTK_MENU_BUTTON(m_pImageSubmenuBtn), NULL);
	}
	m_pImageSubmenuPopover = NULL;

	/* The popover is a child of the split box, so it goes with the HUD.  This
	 * runs first when the viewer is released while the window is still up, and
	 * comes off the box here; when the window went first the box took the
	 * popover with it and viewer_widget_tree_destroyed_cb() has already cleared
	 * the pointer. */
	if (m_pViewModeMenuPopover != NULL && GTK_IS_WIDGET(m_pViewModeMenuPopover)
		&& gtk_widget_get_parent(GTK_WIDGET(m_pViewModeMenuPopover)) != NULL)
		gtk_widget_unparent(GTK_WIDGET(m_pViewModeMenuPopover));
	m_pViewModeMenuPopover = NULL;

	if (m_pSpeedButton && GTK_IS_MENU_BUTTON(m_pSpeedButton))
	{
		gtk_menu_button_set_popover(GTK_MENU_BUTTON(m_pSpeedButton), NULL);
	}
	if (m_pContextMenuPopover)
	{
		if (gtk_widget_get_parent(m_pContextMenuPopover))
			gtk_widget_unparent(m_pContextMenuPopover);
		m_pContextMenuPopover = NULL;
	}

	/* Disconnect all GObject signal handlers that captured `this` so no
	 * callback fires into the freed ViewerImpl during widget tree teardown */
	ReleaseVideoPreview();
	/* still our widget: stop it from writing the cached pointers after the
	 * object is gone (a destroyed tree already NULLed them) */
	if (m_pHBox && G_IS_OBJECT(m_pHBox))
	{
		g_signal_handlers_disconnect_by_data(m_pHBox, this);
	}
	if (m_pAdjustmentH && G_IS_OBJECT(m_pAdjustmentH))
	{
		g_signal_handlers_disconnect_by_data(m_pAdjustmentH, this);
	}
	if (m_pAdjustmentV && G_IS_OBJECT(m_pAdjustmentV))
	{
		g_signal_handlers_disconnect_by_data(m_pAdjustmentV, this);
	}
	if (m_pImageView && G_IS_OBJECT(m_pImageView))
	{
		g_signal_handlers_disconnect_by_data(m_pImageView, this);
	}
	if (m_pIconView && G_IS_OBJECT(m_pIconView))
	{
		g_signal_handlers_disconnect_by_data(m_pIconView, this);
	}
	if (m_pVideoSinkWidget && G_IS_OBJECT(m_pVideoSinkWidget))
	{
		g_signal_handlers_disconnect_by_data(m_pVideoSinkWidget, this);
	}
	if (m_pVolumeScale && G_IS_OBJECT(m_pVolumeScale))
	{
		g_signal_handlers_disconnect_by_data(m_pVolumeScale, this);
	}
	if (m_pVolumeButton && G_IS_OBJECT(m_pVolumeButton))
	{
		g_signal_handlers_disconnect_by_data(m_pVolumeButton, this);
	}
	if (m_pVolumeMuteBtn && G_IS_OBJECT(m_pVolumeMuteBtn))
	{
		g_signal_handlers_disconnect_by_data(m_pVolumeMuteBtn, this);
		m_pVolumeMuteBtn = NULL;
	}
	if (m_pSpeedButton && G_IS_OBJECT(m_pSpeedButton))
	{
		g_signal_handlers_disconnect_by_data(m_pSpeedButton, this);
	}
	if (m_pSnapBtn && G_IS_OBJECT(m_pSnapBtn))
	{
		g_signal_handlers_disconnect_by_data(m_pSnapBtn, this);
	}
	if (m_pVideoOptionsBtn && G_IS_OBJECT(m_pVideoOptionsBtn))
	{
		g_signal_handlers_disconnect_by_data(m_pVideoOptionsBtn, this);
	}
	if (m_pFullscreenBtn && G_IS_OBJECT(m_pFullscreenBtn))
	{
		g_signal_handlers_disconnect_by_data(m_pFullscreenBtn, this);
		m_pFullscreenBtn = NULL;
	}
	if (m_pRewindBtn && G_IS_OBJECT(m_pRewindBtn))
	{
		g_signal_handlers_disconnect_by_data(m_pRewindBtn, this);
	}
	if (m_pFfBtn && G_IS_OBJECT(m_pFfBtn))
	{
		g_signal_handlers_disconnect_by_data(m_pFfBtn, this);
	}
	if (m_pImageSubmenuBtn && G_IS_OBJECT(m_pImageSubmenuBtn))
	{
		g_signal_handlers_disconnect_by_data(m_pImageSubmenuBtn, this);
	}
	if (m_pViewModeMenuBtn && G_IS_OBJECT(m_pViewModeMenuBtn))
	{
		g_signal_handlers_disconnect_by_data(m_pViewModeMenuBtn, this);
	}
	if (m_pViewerFlipHBtn && G_IS_OBJECT(m_pViewerFlipHBtn))
	{
		g_signal_handlers_disconnect_by_data(m_pViewerFlipHBtn, this);
	}
	if (m_pViewerFlipVBtn && G_IS_OBJECT(m_pViewerFlipVBtn))
	{
		g_signal_handlers_disconnect_by_data(m_pViewerFlipVBtn, this);
	}
	if (m_pPlayButton && G_IS_OBJECT(m_pPlayButton))
	{
		g_signal_handlers_disconnect_by_data(m_pPlayButton, this);
	}
	if (m_pCenterPlayBtn && G_IS_OBJECT(m_pCenterPlayBtn))
	{
		g_signal_handlers_disconnect_by_data(m_pCenterPlayBtn, this);
	}
	if (m_pDragSource && G_IS_OBJECT(m_pDragSource))
	{
		g_signal_handlers_disconnect_by_data(m_pDragSource, this);
	}

	/* m_pHBox is parented into the window tree and owned by it; the window
	 * destroy at the end of ~QuiverImpl runs AFTER ViewerImpl has been freed.
	 * Unmap the whole viewer subtree HERE, while `this` is still alive, so no
	 * widget can fire a leave/unmap/gesture callback into this freed object
	 * during the window teardown (same pattern as ~BrowserImpl). */
	if (m_pHBox)
	{
		GtkWidget *pParent = gtk_widget_get_parent(m_pHBox);
		if (pParent != NULL)
		{
			if (GTK_IS_WINDOW(pParent))
			{
				gtk_window_set_child(GTK_WINDOW(pParent), NULL);
			}
			else
			{
				gtk_widget_unparent(m_pHBox);
			}
		}
		m_pHBox = NULL;
	}

	PreferencesPtr prefsPtr = Preferences::GetInstance();
	prefsPtr->RemoveEventHandler( m_PreferencesEventHandlerPtr );

	if (m_pBlankCursor)
	{
		g_object_unref(m_pBlankCursor);
		m_pBlankCursor = NULL;
	}
}

static gboolean video_apply_zoom_idle(gpointer user_data)
{
	Viewer::ViewerImpl *pViewerImpl = (Viewer::ViewerImpl*)user_data;

	

	/* The caps probe can fire before GTK has allocated the layout or mapped
	 * the sink widget.  Without a mapped sink the GL surface does not exist
	 * and the video cannot render.  Keep retrying until the sink is mapped
	 * (which means the overlay has allocated the stack -> layout -> sink). */
	pViewerImpl->ApplyVideoZoom();
	if (pViewerImpl->m_pVideoFixed != NULL)
	if (pViewerImpl->m_pVideoSinkWidget != NULL)
	{
	}
	pViewerImpl->m_iVideoZoomIdle = 0;
	return FALSE;
}

static void video_zoom_raise_media_windows(Viewer::ViewerImpl *p)
{
	(void)p;
	/* In GTK4 overlay stacking is managed by the widget tree; no manual
	 * window raising is needed. */
}

/* ── Video free-layout container ──────────────────────────────────── *
 * A GtkLayout replacement (GtkLayout was removed in GTK 4.10).
 *
 * Two GTK3→GTK4 regressions are fixed here:
 *  1. GTK4 removed the ::size-allocate signal, so the video never re-fit on
 *     window resize.  The size_allocate vfunc below re-queues the zoom math
 *     (video_apply_zoom_idle) whenever the canvas is reallocated.
 *  2. GtkFixed reports its children's minimum size through its own request,
 *     so a large video picture size_request pinned the stack/window minimum
 *     and the window could not be shrunk.  measure() always returns (0,0),
 *     so the window minimum never depends on the (possibly oversized)
 *     picture. */

typedef struct {
	GtkWidget *child;
	gint x;
	gint y;
} QuiverFreelayoutChild;

G_DECLARE_FINAL_TYPE(QuiverFreelayout, quiver_freelayout, QUIVER, FREELAYOUT, GtkWidget)

struct _QuiverFreelayout {
	GtkWidget parent_instance;
	GArray *children;
	int last_alloc_w;
	int last_alloc_h;
};

G_DEFINE_TYPE(QuiverFreelayout, quiver_freelayout, GTK_TYPE_WIDGET)

static void
quiver_freelayout_measure(GtkWidget *widget, GtkOrientation orientation,
	int for_size, int *minimum, int *natural,
	int *minimum_baseline, int *natural_baseline)
{
	(void)widget; (void)orientation; (void)for_size;
	/* Never report the children's size so the window minimum stays free */
	if (minimum) *minimum = 0;
	if (natural) *natural = 0;
	if (minimum_baseline) *minimum_baseline = -1;
	if (natural_baseline) *natural_baseline = -1;
}

static void
quiver_freelayout_size_allocate(GtkWidget *widget, int width, int height, int baseline)
{
	(void)baseline;
	QuiverFreelayout *self = QUIVER_FREELAYOUT(widget);

	for (guint i = 0; i < self->children->len; i++)
	{
		QuiverFreelayoutChild *c = &g_array_index(self->children, QuiverFreelayoutChild, i);
		if (c->child == NULL || !gtk_widget_get_visible(c->child) ||
			!gtk_widget_get_child_visible(c->child))
			continue;

		int req_w = 0, req_h = 0;
		gtk_widget_get_size_request(c->child, &req_w, &req_h);

		GtkAllocation alloc;
		alloc.x = c->x;
		alloc.y = c->y;
		alloc.width = (req_w > 0) ? req_w : width;
		alloc.height = (req_h > 0) ? req_h : height;
		gtk_widget_size_allocate(c->child, &alloc, -1);
	}

	/* GTK4: re-fit the video when this canvas is reallocated (the old
	 * ::size-allocate signal no longer exists, so the zoom never followed
	 * window resizes). Only re-fit when the canvas dimensions actually changed,
	 * so moving child widgets during pan does not trigger redundant re-fits. */
	gboolean size_changed = (self->last_alloc_w != width || self->last_alloc_h != height);
	self->last_alloc_w = width;
	self->last_alloc_h = height;

	Viewer::ViewerImpl *p = (Viewer::ViewerImpl*)g_object_get_data(
		G_OBJECT(widget), "quiver-viewer-impl");
	if (size_changed && p != NULL && p->m_iVideoWidth > 0 && p->m_iVideoHeight > 0)
	{
		/* keep at most one pending zoom idle so none can fire after the
		 * ViewerImpl is gone (tracked so the destructor can cancel it) */
		if (p->m_iVideoZoomIdle != 0)
			g_source_remove(p->m_iVideoZoomIdle);
		p->m_iVideoZoomIdle = g_idle_add_full(G_PRIORITY_HIGH, video_apply_zoom_idle, p, NULL);
	}
}

static void
quiver_freelayout_snapshot(GtkWidget *widget, GtkSnapshot *snapshot)
{
	int width = gtk_widget_get_width(widget);
	int height = gtk_widget_get_height(widget);
	if (width > 0 && height > 0)
	{
		graphene_rect_t clip_bounds = GRAPHENE_RECT_INIT(0.f, 0.f, (float)width, (float)height);
		gtk_snapshot_push_clip(snapshot, &clip_bounds);

		QuiverFreelayout *self = QUIVER_FREELAYOUT(widget);
		for (guint i = 0; i < self->children->len; i++)
		{
			QuiverFreelayoutChild *c = &g_array_index(self->children, QuiverFreelayoutChild, i);
			if (c->child != NULL)
				gtk_widget_snapshot_child(widget, c->child, snapshot);
		}

		gtk_snapshot_pop(snapshot);
	}
	else
	{
		QuiverFreelayout *self = QUIVER_FREELAYOUT(widget);
		for (guint i = 0; i < self->children->len; i++)
		{
			QuiverFreelayoutChild *c = &g_array_index(self->children, QuiverFreelayoutChild, i);
			if (c->child != NULL)
				gtk_widget_snapshot_child(widget, c->child, snapshot);
		}
	}
}

static void
quiver_freelayout_init(QuiverFreelayout *self)
{
	self->children = g_array_new(FALSE, FALSE, sizeof(QuiverFreelayoutChild));
	self->last_alloc_w = 0;
	self->last_alloc_h = 0;
}

static void
quiver_freelayout_dispose(GObject *object)
{
	QuiverFreelayout *self = QUIVER_FREELAYOUT(object);
	if (self->children != NULL)
	{
		for (guint i = 0; i < self->children->len; i++)
		{
			QuiverFreelayoutChild *c = &g_array_index(self->children, QuiverFreelayoutChild, i);
			if (c->child != NULL)
			{
				/* only unparent a child that still has us as parent; a child
				 * with extra refs (e.g. the video sink's GtkPicture) survives
				 * as a standalone widget instead of leaking with a dangling
				 * parent pointer */
				if (gtk_widget_get_parent(c->child) == GTK_WIDGET(self))
					gtk_widget_unparent(c->child);
				c->child = NULL;
			}
		}
		g_array_set_size(self->children, 0);
	}
	G_OBJECT_CLASS(quiver_freelayout_parent_class)->dispose(object);
}

static void
quiver_freelayout_finalize(GObject *object)
{
	QuiverFreelayout *self = QUIVER_FREELAYOUT(object);
	/* The bookkeeping of where each child sits belongs to the container and goes
	 * when the container does.  Dispose empties it - that is about the children
	 * and has to happen while they are still alive - but the array itself was
	 * never freed, so every video viewer leaked one container's worth of it. */
	g_clear_pointer(&self->children, g_array_unref);

	G_OBJECT_CLASS(quiver_freelayout_parent_class)->finalize(object);
}

static void
quiver_freelayout_class_init(QuiverFreelayoutClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
	gobject_class->dispose = quiver_freelayout_dispose;
	gobject_class->finalize = quiver_freelayout_finalize;

	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);
	widget_class->measure = quiver_freelayout_measure;
	widget_class->size_allocate = quiver_freelayout_size_allocate;
	widget_class->snapshot = quiver_freelayout_snapshot;
}

static GtkWidget *
quiver_freelayout_new(void)
{
	return GTK_WIDGET(g_object_new(quiver_freelayout_get_type(), NULL));
}

static void
quiver_freelayout_put(GtkWidget *container, GtkWidget *child, gint x, gint y)
{
	QuiverFreelayout *self = QUIVER_FREELAYOUT(container);
	QuiverFreelayoutChild c;
	c.child = child;
	c.x = x;
	c.y = y;
	g_array_append_val(self->children, c);
	gtk_widget_set_parent(child, container);
}

static void
quiver_freelayout_move(GtkWidget *container, GtkWidget *child, gint x, gint y)
{
	QuiverFreelayout *self = QUIVER_FREELAYOUT(container);
	for (guint i = 0; i < self->children->len; i++)
	{
		QuiverFreelayoutChild *c = &g_array_index(self->children, QuiverFreelayoutChild, i);
		if (c->child == child)
		{
			c->x = x;
			c->y = y;
			gtk_widget_queue_allocate(container);
			return;
		}
	}
}

static void video_zoom_sink_map_cb(GtkWidget *widget, gpointer user_data)
{
	(void)widget;
	video_zoom_raise_media_windows((Viewer::ViewerImpl *)user_data);
}

static void video_glitch_save_rgb_png(const uint8_t *rgb, int w, int h, const char *path)
{
	if (!rgb || w <= 0 || h <= 0 || !path)
		return;
	GdkPixbuf *pb = gdk_pixbuf_new_from_data(
		rgb, GDK_COLORSPACE_RGB, FALSE, 8, w, h, w * 3, NULL, NULL);
	if (pb)
	{
		GError *err = NULL;
		gdk_pixbuf_save(pb, path, "png", &err, NULL);
		if (err)
			g_error_free(err);
		g_object_unref(pb);
	}
}

static void video_glitch_extract_rgb(GstVideoFrame *vf, std::vector<uint8_t> &out_rgb)
{
	int w = GST_VIDEO_FRAME_WIDTH(vf);
	int h = GST_VIDEO_FRAME_HEIGHT(vf);
	GstVideoFormat fmt = GST_VIDEO_FRAME_FORMAT(vf);
	out_rgb.resize(w * h * 3);

	if (fmt == GST_VIDEO_FORMAT_RGBA || fmt == GST_VIDEO_FORMAT_RGBx)
	{
		const uint8_t *src = (const uint8_t *)GST_VIDEO_FRAME_PLANE_DATA(vf, 0);
		int stride = GST_VIDEO_FRAME_PLANE_STRIDE(vf, 0);
		for (int y = 0; y < h; ++y)
		{
			const uint8_t *row = src + y * stride;
			uint8_t *dst = out_rgb.data() + y * w * 3;
			for (int x = 0; x < w; ++x)
			{
				dst[x * 3 + 0] = row[x * 4 + 0];
				dst[x * 3 + 1] = row[x * 4 + 1];
				dst[x * 3 + 2] = row[x * 4 + 2];
			}
		}
	}
	else if (fmt == GST_VIDEO_FORMAT_BGRA || fmt == GST_VIDEO_FORMAT_BGRx)
	{
		const uint8_t *src = (const uint8_t *)GST_VIDEO_FRAME_PLANE_DATA(vf, 0);
		int stride = GST_VIDEO_FRAME_PLANE_STRIDE(vf, 0);
		for (int y = 0; y < h; ++y)
		{
			const uint8_t *row = src + y * stride;
			uint8_t *dst = out_rgb.data() + y * w * 3;
			for (int x = 0; x < w; ++x)
			{
				dst[x * 3 + 0] = row[x * 4 + 2];
				dst[x * 3 + 1] = row[x * 4 + 1];
				dst[x * 3 + 2] = row[x * 4 + 0];
			}
		}
	}
	else if (fmt == GST_VIDEO_FORMAT_I420 || fmt == GST_VIDEO_FORMAT_YV12)
	{
		const uint8_t *y_data = (const uint8_t *)GST_VIDEO_FRAME_PLANE_DATA(vf, 0);
		const uint8_t *u_data = (const uint8_t *)GST_VIDEO_FRAME_PLANE_DATA(vf, (fmt == GST_VIDEO_FORMAT_I420) ? 1 : 2);
		const uint8_t *v_data = (const uint8_t *)GST_VIDEO_FRAME_PLANE_DATA(vf, (fmt == GST_VIDEO_FORMAT_I420) ? 2 : 1);
		int y_stride = GST_VIDEO_FRAME_PLANE_STRIDE(vf, 0);
		int u_stride = GST_VIDEO_FRAME_PLANE_STRIDE(vf, 1);
		int v_stride = GST_VIDEO_FRAME_PLANE_STRIDE(vf, 2);

		for (int y = 0; y < h; ++y)
		{
			const uint8_t *y_row = y_data + y * y_stride;
			const uint8_t *u_row = u_data + (y / 2) * u_stride;
			const uint8_t *v_row = v_data + (y / 2) * v_stride;
			uint8_t *dst = out_rgb.data() + y * w * 3;
			for (int x = 0; x < w; ++x)
			{
				int Y = y_row[x];
				int U = u_row[x / 2] - 128;
				int V = v_row[x / 2] - 128;
				dst[x * 3 + 0] = (uint8_t)std::clamp((int)(Y + 1.402 * V), 0, 255);
				dst[x * 3 + 1] = (uint8_t)std::clamp((int)(Y - 0.344136 * U - 0.714136 * V), 0, 255);
				dst[x * 3 + 2] = (uint8_t)std::clamp((int)(Y + 1.772 * U), 0, 255);
			}
		}
	}
	else if (fmt == GST_VIDEO_FORMAT_NV12)
	{
		const uint8_t *y_data = (const uint8_t *)GST_VIDEO_FRAME_PLANE_DATA(vf, 0);
		const uint8_t *uv_data = (const uint8_t *)GST_VIDEO_FRAME_PLANE_DATA(vf, 1);
		int y_stride = GST_VIDEO_FRAME_PLANE_STRIDE(vf, 0);
		int uv_stride = GST_VIDEO_FRAME_PLANE_STRIDE(vf, 1);

		for (int y = 0; y < h; ++y)
		{
			const uint8_t *y_row = y_data + y * y_stride;
			const uint8_t *uv_row = uv_data + (y / 2) * uv_stride;
			uint8_t *dst = out_rgb.data() + y * w * 3;
			for (int x = 0; x < w; ++x)
			{
				int Y = y_row[x];
				int U = uv_row[(x / 2) * 2 + 0] - 128;
				int V = uv_row[(x / 2) * 2 + 1] - 128;
				dst[x * 3 + 0] = (uint8_t)std::clamp((int)(Y + 1.402 * V), 0, 255);
				dst[x * 3 + 1] = (uint8_t)std::clamp((int)(Y - 0.344136 * U - 0.714136 * V), 0, 255);
				dst[x * 3 + 2] = (uint8_t)std::clamp((int)(Y + 1.772 * U), 0, 255);
			}
		}
	}
}

static void video_glitch_log_event(const std::string &msg)
{
	std::ofstream log_file("quiver_glitch_log.txt", std::ios::app);
	if (log_file.is_open())
	{
		log_file << msg << "\n";
		log_file.flush();
	}
}

static bool is_glitch_debug_enabled()
{
	static int s_enabled = -1;
	if (s_enabled == -1)
	{
		const char *env = g_getenv("QUIVER_DEBUG_GLITCH");
		s_enabled = (env != NULL && env[0] != '\0' && strcmp(env, "0") != 0) ? 1 : 0;
	}
	return s_enabled == 1;
}

static bool video_glitch_process_frame(Viewer::ViewerImpl *pViewerImpl,
                                       Viewer::ViewerImpl::VideoGlitchTracker &tracker,
                                       GstBuffer *buf,
                                       Viewer::ViewerImpl::VideoGlitchTracker *peer_tracker,
                                       bool is_sink_probe)
{
	if (!is_glitch_debug_enabled())
		return false;

	if (!tracker.m_bHaveVideoInfo || buf == NULL)
		return false;

	GstVideoFrame vf;
	if (!gst_video_frame_map(&vf, &tracker.m_VideoInfo, buf, GST_MAP_READ))
		return false;

	int w = GST_VIDEO_FRAME_WIDTH(&vf);
	int h = GST_VIDEO_FRAME_HEIGHT(&vf);
	if (w < 128 || h < 128)
	{
		gst_video_frame_unmap(&vf);
		return false;
	}

	GstVideoFormat fmt = GST_VIDEO_FRAME_FORMAT(&vf);
	const uint8_t *plane0 = (const uint8_t *)GST_VIDEO_FRAME_PLANE_DATA(&vf, 0);
	int stride = GST_VIDEO_FRAME_PLANE_STRIDE(&vf, 0);
	int px_step = (fmt == GST_VIDEO_FORMAT_RGBA || fmt == GST_VIDEO_FORMAT_RGBx ||
	               fmt == GST_VIDEO_FORMAT_BGRA || fmt == GST_VIDEO_FORMAT_BGRx) ? 4 : 1;

	// 1. PTS monotonicity check
	GstClockTime pts = GST_BUFFER_PTS(buf);
	bool pts_jump_backward = false;
	if (GST_CLOCK_TIME_IS_VALID(pts) && GST_CLOCK_TIME_IS_VALID(tracker.m_uLastPts))
	{
		if (pts < tracker.m_uLastPts)
		{
			GstClockTime diff = tracker.m_uLastPts - pts;
			// A large backward jump (>= 500ms) or jump to 0 indicates a normal video loop or user seek.
			// Reset history and baselines so frames across the discontinuity are not falsely compared.
			if (diff >= 500 * GST_MSECOND || pts == 0)
			{
				tracker.m_vHistory.clear();
				tracker.m_vPrevRGB.clear();
				tracker.m_vPrevRGB2.clear();
				tracker.m_dRollingMAE = 0.0;
			}
			else
			{
				// Unexpected small backward jump (e.g. 1 frame out of order) indicates an actual presentation glitch
				pts_jump_backward = true;
			}
		}
	}
	tracker.m_uLastPts = pts;

	// 2. Affine transformation metadata check
	float curr_matrix[16]{};
	bool has_matrix = false;
	GstVideoAffineTransformationMeta *aff = gst_buffer_get_video_affine_transformation_meta(buf);
	if (aff != NULL)
	{
		memcpy(curr_matrix, aff->matrix, sizeof(curr_matrix));
		has_matrix = true;
	}

	// 3. Fast downsampled 32x18 grid for motion / flicker trajectory
	constexpr int GW = 32;
	constexpr int GH = 18;
	std::vector<uint8_t> curr_grid(GW * GH);
	int step_x = w / GW;
	int step_y = h / GH;
	for (int gy = 0; gy < GH; ++gy)
	{
		int py = gy * step_y;
		const uint8_t *row = plane0 + py * stride;
		for (int gx = 0; gx < GW; ++gx)
		{
			int px = gx * step_x;
			curr_grid[gy * GW + gx] = row[px * px_step];
		}
	}

	auto calc_mae = [](const std::vector<uint8_t> &g1, const std::vector<uint8_t> &g2) -> double {
		if (g1.size() != g2.size() || g1.empty()) return 0.0;
		double sum = 0.0;
		for (size_t i = 0; i < g1.size(); ++i)
			sum += std::abs((int)g1[i] - (int)g2[i]);
		return sum / (double)g1.size();
	};

	double mae = 0.0;
	if (!tracker.m_vHistory.empty())
	{
		mae = calc_mae(curr_grid, tracker.m_vHistory.back().grid);
	}
	if (tracker.m_uFrameIndex.load(std::memory_order_relaxed) > 5 && mae > 0.0)
	{
		tracker.m_dRollingMAE = 0.95 * tracker.m_dRollingMAE + 0.05 * mae;
	}

	// 4. Trajectory Flicker / Bounce analysis
	double flicker_1 = 0.0;
	double d_prev_prev2 = 0.0;
	double d_curr_prev = mae;
	double d_curr_prev2 = 0.0;
	if (tracker.m_vHistory.size() >= 2)
	{
		d_prev_prev2 = calc_mae(tracker.m_vHistory.back().grid,
		                        tracker.m_vHistory[tracker.m_vHistory.size() - 2].grid);
		d_curr_prev2 = calc_mae(curr_grid,
		                        tracker.m_vHistory[tracker.m_vHistory.size() - 2].grid);
		flicker_1 = (d_prev_prev2 + d_curr_prev) / std::max(1.5, 2.0 * d_curr_prev2);
	}

	double flicker_2 = 0.0;
	double d_prev2_prev3 = 0.0;
	double d_curr_prev3 = 0.0;
	if (tracker.m_vHistory.size() >= 3)
	{
		d_prev2_prev3 = calc_mae(tracker.m_vHistory[tracker.m_vHistory.size() - 2].grid,
		                         tracker.m_vHistory[tracker.m_vHistory.size() - 3].grid);
		d_curr_prev3 = calc_mae(curr_grid,
		                        tracker.m_vHistory[tracker.m_vHistory.size() - 3].grid);
		flicker_2 = (d_prev2_prev3 + d_curr_prev) / std::max(2.0, 2.0 * d_curr_prev3);
	}

	// 5. Multi-scale spatial discontinuity (vertical & horizontal at 16px and 64px)
	auto calc_vertical_disc = [&](int step_px) -> std::pair<double, double> {
		double disc = 0.0;
		int count = 0;
		for (int col = step_px; col < w - step_px / 2; col += step_px)
		{
			double diff = 0.0;
			int rows = 0;
			for (int y = 0; y < h; y += 16)
			{
				diff += std::abs((int)plane0[y * stride + (col - 1) * px_step] -
				                 (int)plane0[y * stride + col * px_step]);
				rows++;
			}
			if (rows > 0)
			{
				disc += diff / rows;
				count++;
			}
		}
		disc /= (count > 0 ? count : 1);

		int offset = std::max(2, step_px / 4);
		double base = 0.0;
		int count_base = 0;
		for (int col = step_px + offset; col < w - step_px / 2; col += step_px)
		{
			double diff = 0.0;
			int rows = 0;
			for (int y = 0; y < h; y += 16)
			{
				diff += std::abs((int)plane0[y * stride + (col - 1) * px_step] -
				                 (int)plane0[y * stride + col * px_step]);
				rows++;
			}
			if (rows > 0)
			{
				base += diff / rows;
				count_base++;
			}
		}
		base /= (count_base > 0 ? count_base : 1);
		return {disc, base};
	};

	auto [disc64, disc_base] = calc_vertical_disc(64);
	double r64 = disc64 / std::max(1.0, disc_base);

	auto [disc16, disc_base16] = calc_vertical_disc(16);
	double r16 = disc16 / std::max(1.0, disc_base16);

	bool spatial_tear = (r16 >= 2.2 && (disc16 - disc_base16) >= 8.0) ||
	                    (r64 >= 2.5 && (disc64 - disc_base) >= 8.0);

	// 6. Check matrix bounce
	bool matrix_flicker = false;
	if (has_matrix && tracker.m_vHistory.size() >= 2 &&
	    tracker.m_vHistory.back().has_matrix &&
	    tracker.m_vHistory[tracker.m_vHistory.size() - 2].has_matrix)
	{
		float m_diff_1 = 0.0f, m_diff_2 = 0.0f, m_diff_base = 0.0f;
		for (int i = 0; i < 16; ++i)
		{
			m_diff_1 += std::abs(tracker.m_vHistory.back().matrix[i] - tracker.m_vHistory[tracker.m_vHistory.size() - 2].matrix[i]);
			m_diff_2 += std::abs(curr_matrix[i] - tracker.m_vHistory.back().matrix[i]);
			m_diff_base += std::abs(curr_matrix[i] - tracker.m_vHistory[tracker.m_vHistory.size() - 2].matrix[i]);
		}
		if (m_diff_1 > 0.05f && m_diff_2 > 0.05f && m_diff_base < 0.02f)
		{
			matrix_flicker = true;
		}
	}

	guint64 frame_idx = tracker.m_uFrameIndex.fetch_add(1, std::memory_order_relaxed) + 1;
	bool is_glitch = false;
	std::string glitch_cause;
	guint64 glitched_frame_idx = frame_idx;

	gint64 now_us = g_get_monotonic_time();
	gint64 last_zoom_us = pViewerImpl->m_iLastZoomApplyTimeUs.load(std::memory_order_relaxed);
	gint64 ms_since_zoom = (last_zoom_us > 0) ? ((now_us - last_zoom_us) / 1000) : -1;
	bool is_active_zooming = (ms_since_zoom >= 0 && ms_since_zoom < 100) || pViewerImpl->m_bVideoPanning || pViewerImpl->m_bVideoPanSlowdownActive;

	if (frame_idx > 5)
	{
		if (pts_jump_backward)
		{
			is_glitch = true;
			glitch_cause = "PTS_BACKWARD_JUMP";
		}
		else if (!is_active_zooming && flicker_1 >= 2.5 && (d_prev_prev2 >= 4.5 || d_curr_prev >= 4.5) && (d_prev_prev2 + d_curr_prev >= 10.0))
		{
			is_glitch = true;
			glitch_cause = "FLICKER_BOUNCE (1-frame transient on frame #" + std::to_string(frame_idx - 1) + ", score=" + std::to_string(flicker_1) + ")";
			glitched_frame_idx = frame_idx - 1;
		}
		else if (is_active_zooming && flicker_1 >= 5.0 && (d_prev_prev2 + d_curr_prev >= 14.0))
		{
			is_glitch = true;
			glitch_cause = "FLICKER_BOUNCE (1-frame transient during zoom on frame #" + std::to_string(frame_idx - 1) + ", score=" + std::to_string(flicker_1) + ")";
			glitched_frame_idx = frame_idx - 1;
		}
		else if (!is_active_zooming && flicker_2 >= 3.0 && (d_prev2_prev3 >= 6.0 || d_curr_prev >= 6.0) && d_curr_prev3 < 2.5)
		{
			is_glitch = true;
			glitch_cause = "FLICKER_BOUNCE_2FRAME (2-frame transient on frames #" + std::to_string(frame_idx - 2) + "-" + std::to_string(frame_idx - 1) + ", score=" + std::to_string(flicker_2) + ")";
			glitched_frame_idx = frame_idx - 1;
		}
		else if (tracker.m_vHistory.size() >= 6 && d_curr_prev >= 8.0 && calc_mae(curr_grid, tracker.m_vHistory[tracker.m_vHistory.size() - 6].grid) <= 2.0)
		{
			is_glitch = true;
			glitch_cause = "CYCLE_6_STALE_BUFFER (frame matches frame #" + std::to_string(tracker.m_vHistory[tracker.m_vHistory.size() - 6].frame_idx) + " from 6 frames ago)";
		}
		else if (spatial_tear)
		{
			is_glitch = true;
			glitch_cause = "SPATIAL_TEAR (R16=" + std::to_string(r16) + ", R64=" + std::to_string(r64) + ")";
		}
		else if (matrix_flicker)
		{
			is_glitch = true;
			glitch_cause = "AFFINE_MATRIX_BOUNCE (transform matrix reverted on frame #" + std::to_string(frame_idx - 1) + ")";
			glitched_frame_idx = frame_idx - 1;
		}
	}
	tracker.m_dLastR64.store(r64, std::memory_order_relaxed);
	tracker.m_dLastR16.store(r16, std::memory_order_relaxed);
	tracker.m_dLastMAE.store(mae, std::memory_order_relaxed);
	tracker.m_dLastFlicker.store(flicker_1, std::memory_order_relaxed);
	tracker.m_bLastWasGlitch.store(is_glitch, std::memory_order_relaxed);

	if (is_glitch)
	{
		guint glitch_num = tracker.m_uGlitchCount.fetch_add(1, std::memory_order_relaxed) + 1;
		guint64 prev_glitch = tracker.m_uLastGlitchFrame.exchange(glitched_frame_idx, std::memory_order_relaxed);
		guint64 period = (prev_glitch > 0) ? (glitched_frame_idx - prev_glitch) : 0;

		std::string diag;
		if (peer_tracker && peer_tracker->m_bHaveVideoInfo)
		{
			bool peer_glitched = peer_tracker->m_bLastWasGlitch.load(std::memory_order_relaxed);
			gint64 peer_glitch_pts = peer_tracker->m_iLastGlitchPts.load(std::memory_order_relaxed);
			bool peer_glitched_near_pts = (GST_CLOCK_TIME_IS_VALID(pts) && peer_glitch_pts >= 0 &&
			                               std::abs((gint64)pts - peer_glitch_pts) < 150 * (gint64)GST_MSECOND);
			double peer_r16 = peer_tracker->m_dLastR16.load(std::memory_order_relaxed);
			double peer_r64 = peer_tracker->m_dLastR64.load(std::memory_order_relaxed);
			double peer_flicker = peer_tracker->m_dLastFlicker.load(std::memory_order_relaxed);

			if (is_sink_probe)
			{
				if (!peer_glitched && !peer_glitched_near_pts && peer_flicker < 1.2 && peer_r16 < 1.4 && peer_r64 < 1.4)
				{
					diag = "DECODER IS CLEAN! Corruption occurred downstream in GL Pipeline or Sink (e.g. gltransformation / buffer pool recycling)!";
				}
				else
				{
					diag = "CORRUPTION ORIGINATES IN DECODER / DEMUX STREAM!";
				}
			}
		}

		std::cerr << "\n================================================================================\n";
		std::cerr << "\033[1;31m[QUIVER PROGRAMMATIC GLITCH DETECTED #" << glitch_num << "]\033[0m\n";
		std::cerr << "  Probe Stream:  " << (is_sink_probe ? "DISPLAY SINK (raw_sink)" : "DECODER OUTPUT (glupload input)") << "\n";
		std::cerr << "  Trigger Frame: #" << frame_idx << " (Glitched target: #" << glitched_frame_idx << ")\n";
		std::cerr << "  Glitch Cause:  \033[1;33m" << glitch_cause << "\033[0m\n";
		if (GST_CLOCK_TIME_IS_VALID(pts))
		{
			guint pts_sec = (guint)(pts / GST_SECOND);
			guint pts_ms = (guint)((pts % GST_SECOND) / (GST_SECOND / 1000));
			std::cerr << "  PTS:           " << (pts_sec / 60) << ":"
			          << std::setfill('0') << std::setw(2) << (pts_sec % 60) << "."
			          << std::setfill('0') << std::setw(3) << pts_ms << "\n";
		}
		if (period > 0)
		{
			std::cerr << "  \033[1;33mGlitch Period: " << period << " frames since previous glitch\033[0m\n";
		}
		std::cerr << "  Artifacts:     Flicker=" << flicker_1 << " (Flicker2=" << flicker_2 << ")\n";
		std::cerr << "                 R16=" << r16 << " (disc16=" << disc16 << ", base=" << disc_base16 << ")\n";
		std::cerr << "                 R64=" << r64 << " (disc64=" << disc64 << ", base=" << disc_base << ")\n";
		std::cerr << "                 MAE=" << mae << " (rolling baseline=" << tracker.m_dRollingMAE << ")\n";
		std::cerr << "  Interaction:   Zoom = " << pViewerImpl->m_dVideoZoom << "x | Panning = "
		          << (pViewerImpl->m_bVideoPanning ? "YES" : "NO")
		          << " | Last zoom update: " << ms_since_zoom << " ms ago\n";
		if (!diag.empty())
		{
			std::cerr << "  \033[1;32m===> DIAGNOSIS: " << diag << "\033[0m\n";
		}

		// Asynchronous frame capture (at most 25 capture events, spaced >= 1.0s)
		static std::atomic<gint64> s_iLastCaptureTimeUs{0};
		gint64 last_cap = s_iLastCaptureTimeUs.load(std::memory_order_relaxed);
		std::string cap_info;
		if (glitch_num <= 25 && (now_us - last_cap > 1000000))
		{
			s_iLastCaptureTimeUs.store(now_us, std::memory_order_relaxed);
			if (tracker.m_SaveThread.joinable())
				tracker.m_SaveThread.join();

			if (glitched_frame_idx == frame_idx - 1 && !tracker.m_vPrevRGB.empty())
			{
				std::string path_glitch = "glitch_" + std::string(tracker.m_pszName) + "_frame_" + std::to_string(glitched_frame_idx) + ".png";
				std::string path_prev = (!tracker.m_vPrevRGB2.empty()) ?
					("glitch_" + std::string(tracker.m_pszName) + "_frame_" + std::to_string(glitched_frame_idx) + "_prev.png") : "";

				tracker.m_SaveThread = std::thread([rgb = tracker.m_vPrevRGB, w = tracker.m_iPrevRGBW, h = tracker.m_iPrevRGBH, path_glitch,
				                                    rgb_prev = tracker.m_vPrevRGB2, wp = tracker.m_iPrevRGB2W, hp = tracker.m_iPrevRGB2H, path_prev]() {
					video_glitch_save_rgb_png(rgb.data(), w, h, path_glitch.c_str());
					if (!path_prev.empty())
						video_glitch_save_rgb_png(rgb_prev.data(), wp, hp, path_prev.c_str());
				});

				cap_info = "glitch=" + path_glitch;
				std::cerr << "  Captured:      " << path_glitch << "\n";
				if (!path_prev.empty())
				{
					cap_info += " prev=" + path_prev;
					std::cerr << "  Captured Prev: " << path_prev << "\n";
				}
			}
			else
			{
				std::vector<uint8_t> curr_rgb;
				video_glitch_extract_rgb(&vf, curr_rgb);
				std::string path_curr = "glitch_" + std::string(tracker.m_pszName) + "_frame_" + std::to_string(glitched_frame_idx) + ".png";
				tracker.m_SaveThread = std::thread([curr_rgb = std::move(curr_rgb), w, h, path_curr]() {
					video_glitch_save_rgb_png(curr_rgb.data(), w, h, path_curr.c_str());
				});
				cap_info = "glitch=" + path_curr;
				std::cerr << "  Captured:      " << path_curr << "\n";
			}
		}
		std::cerr << "================================================================================\n" << std::endl;

		std::string log_msg = "[GLITCH #" + std::to_string(glitch_num) + "] " +
			std::string(tracker.m_pszName) + " frame=" + std::to_string(glitched_frame_idx) +
			" period=" + std::to_string(period) +
			" cause=" + glitch_cause +
			" flicker=" + std::to_string(flicker_1) +
			" R16=" + std::to_string(r16) +
			" R64=" + std::to_string(r64) +
			" MAE=" + std::to_string(mae) +
			" zoom=" + std::to_string(pViewerImpl->m_dVideoZoom) +
			" panning=" + (pViewerImpl->m_bVideoPanning ? "1" : "0") +
			" ms_since_zoom=" + std::to_string(ms_since_zoom) +
			" diag=" + diag +
			(cap_info.empty() ? "" : (" " + cap_info));
		video_glitch_log_event(log_msg);
	}

	// Update history ring
	Viewer::ViewerImpl::VideoGlitchTracker::FrameItem item;
	item.frame_idx = frame_idx;
	item.pts = pts;
	item.grid = std::move(curr_grid);
	memcpy(item.matrix, curr_matrix, sizeof(item.matrix));
	item.has_matrix = has_matrix;
	tracker.m_vHistory.push_back(std::move(item));
	if (tracker.m_vHistory.size() > 12)
	{
		tracker.m_vHistory.erase(tracker.m_vHistory.begin());
	}

	// Update RGB history ring for retroactive glitch capture
	if (is_sink_probe)
	{
		tracker.m_vPrevRGB2 = std::move(tracker.m_vPrevRGB);
		tracker.m_uPrevRGB2FrameIdx = tracker.m_uPrevRGBFrameIdx;
		tracker.m_iPrevRGB2W = tracker.m_iPrevRGBW;
		tracker.m_iPrevRGB2H = tracker.m_iPrevRGBH;

		video_glitch_extract_rgb(&vf, tracker.m_vPrevRGB);
		tracker.m_uPrevRGBFrameIdx = frame_idx;
		tracker.m_iPrevRGBW = w;
		tracker.m_iPrevRGBH = h;
	}

	// Continuous telemetry logging: initialization on frame 1, heartbeat every 60 frames, or on suspicious motion
	if (frame_idx == 1 || frame_idx % 60 == 0)
	{
		std::string hb = (frame_idx == 1 ? "[INIT] probe=" : "[HEARTBEAT] probe=") +
			std::string(tracker.m_pszName) +
			" frame=" + std::to_string(frame_idx) +
			" zoom=" + std::to_string(pViewerImpl->m_dVideoZoom) +
			" r16=" + std::to_string(r16) +
			" r64=" + std::to_string(r64) +
			" mae=" + std::to_string(mae) +
			" flicker=" + std::to_string(flicker_1) +
			" status=" + (is_glitch ? "GLITCH" : "CLEAN");
		video_glitch_log_event(hb);
	}
	else if (flicker_1 >= 1.25 || r16 >= 1.5 || r64 >= 1.5)
	{
		std::string susp = "[ACTIVITY] probe=" + std::string(tracker.m_pszName) +
			" frame=" + std::to_string(frame_idx) +
			" zoom=" + std::to_string(pViewerImpl->m_dVideoZoom) +
			" r16=" + std::to_string(r16) +
			" r64=" + std::to_string(r64) +
			" mae=" + std::to_string(mae) +
			" flicker=" + std::to_string(flicker_1);
		video_glitch_log_event(susp);
	}

	gst_video_frame_unmap(&vf);
	return is_glitch;
}

static GstPadProbeReturn video_sink_glitch_probe_cb(GstPad *pad, GstPadProbeInfo *info, gpointer user_data)
{
	(void)pad;
	if (!is_glitch_debug_enabled())
		return GST_PAD_PROBE_OK;

	Viewer::ViewerImpl *pViewerImpl = (Viewer::ViewerImpl*)user_data;
	if (pViewerImpl == NULL)
		return GST_PAD_PROBE_OK;

	if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM)
	{
		GstEvent *event = GST_PAD_PROBE_INFO_EVENT(info);
		if (GST_EVENT_TYPE(event) == GST_EVENT_CAPS)
		{
			GstCaps *caps = NULL;
			gst_event_parse_caps(event, &caps);
			if (caps != NULL)
			{
				gst_video_info_from_caps(&pViewerImpl->m_SinkGlitchTracker.m_VideoInfo, caps);
				pViewerImpl->m_SinkGlitchTracker.m_bHaveVideoInfo = true;
			}
		}
	}
	else if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER)
	{
		GstBuffer *buf = GST_PAD_PROBE_INFO_BUFFER(info);
		if (buf != NULL)
		{
			video_glitch_process_frame(pViewerImpl, pViewerImpl->m_SinkGlitchTracker, buf, &pViewerImpl->m_DecoderGlitchTracker, true);
		}
	}
	return GST_PAD_PROBE_OK;
}

static GstPadProbeReturn video_crop_pad_probe(GstPad *pad, GstPadProbeInfo *info, gpointer user_data)
{ (void)pad; 
	Viewer::ViewerImpl *pViewerImpl = (Viewer::ViewerImpl*)user_data;
	if (pViewerImpl == NULL)
		return GST_PAD_PROBE_OK;

	if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM)
	{
		GstEvent *event = GST_PAD_PROBE_INFO_EVENT(info);
		if (GST_EVENT_TYPE(event) == GST_EVENT_CAPS)
		{
			GstCaps *caps = NULL;
			gst_event_parse_caps(event, &caps);
			if (caps != NULL)
			{
				gst_video_info_from_caps(&pViewerImpl->m_DecoderGlitchTracker.m_VideoInfo, caps);
				pViewerImpl->m_DecoderGlitchTracker.m_bHaveVideoInfo = true;

				GstStructure *structure = gst_caps_get_structure(caps, 0);
				gint w = 0, h = 0;
				gst_structure_get_int(structure, "width", &w);
				gst_structure_get_int(structure, "height", &h);
				if (w > 0 && h > 0)
				{
					if (gst_structure_has_field(structure, "pixel-aspect-ratio"))
						gst_structure_get_fraction(structure, "pixel-aspect-ratio", &pViewerImpl->m_iVideoParN, &pViewerImpl->m_iVideoParD);
					else { pViewerImpl->m_iVideoParN = 1; pViewerImpl->m_iVideoParD = 1; }

					pViewerImpl->m_iVideoWidth = w;
					pViewerImpl->m_iVideoHeight = h;
					/* element properties must not be touched from the streaming
					 * thread, so re-apply the zoom on the main thread */
					if (pViewerImpl->m_iVideoZoomIdle != 0)
						g_source_remove(pViewerImpl->m_iVideoZoomIdle);
					pViewerImpl->m_iVideoZoomIdle = g_idle_add_full(G_PRIORITY_HIGH, video_apply_zoom_idle, pViewerImpl, NULL);
				}
			}
		}
	}
	else if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER)
	{
		if (is_glitch_debug_enabled())
		{
			GstBuffer *buf = GST_PAD_PROBE_INFO_BUFFER(info);
			if (buf != NULL)
			{
				video_glitch_process_frame(pViewerImpl, pViewerImpl->m_DecoderGlitchTracker, buf, &pViewerImpl->m_SinkGlitchTracker, false);
			}
		}
	}
	return GST_PAD_PROBE_OK;
}

static gboolean video_preview_redraw_idle(gpointer user_data)
{
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl *)user_data;
	if (p == NULL)
		return FALSE;
	p->m_iVideoPreviewRedrawIdle = 0;
	p->m_bVideoPreviewRedrawQueued = FALSE;
	if (p->m_spAlive == NULL || !*p->m_spAlive)
		return FALSE;
	if (p->m_pNavigationControl != NULL && GTK_IS_WIDGET(p->m_pNavigationControl))
		gtk_widget_queue_draw(p->m_pNavigationControl);
	return FALSE;
}

/* Called when the sink's paintable delivers a new frame.  After a video
 * switch, the paintable still shows the OLD video's last frame until the
 * new video's first frame is actually rendered.  Restoring opacity here
 * (rather than in ASYNC_DONE or a pad probe) ensures the new frame is
 * already available when it becomes visible. */
static void video_paintable_invalidated_cb(GdkPaintable *paintable, gpointer user_data)
{
	(void)paintable;
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl *)user_data;
	if (p == NULL || !p->m_spAlive || !*p->m_spAlive)
		return;
	if (p->m_bVideoNeedsFirstFrame
		&& !p->m_bVideoFlushPending
		&& p->m_pVideoFixed != NULL && GTK_IS_WIDGET(p->m_pVideoFixed) && gtk_widget_get_visible(p->m_pVideoFixed))
	{
		p->m_bVideoNeedsFirstFrame = FALSE;
		if (p->m_pVideoSinkWidget != NULL && GTK_IS_WIDGET(p->m_pVideoSinkWidget))
			gtk_widget_set_opacity(p->m_pVideoSinkWidget, 1.0);
		/* The frame has been decoded, so the quick preview is over: from here the
		 * video owns the screen, and the image view must stop being a source for
		 * the framing.  Decided here rather than below, where the flag used to be
		 * set, because the hand-back of the image view comes first and it moves
		 * exactly the scroll position the framing is read from. */
		p->m_bVideoPreviewHasFrame = TRUE;
		p->UpdateNavControlVisibility();
		/* The first frame is now decoded, so the video page no longer shows
		 * the transparent sink over the background.  Complete the deferred
		 * page switch requested by ShowVideoPage() to avoid the background
		 * flicker that would appear if we switched before the frame. */
		if (p->m_bVideoPagePending && p->m_pStack != NULL && GTK_IS_STACK(p->m_pStack))
		{
			p->m_bVideoPagePending = FALSE;
			gtk_stack_set_visible_child_name(GTK_STACK(p->m_pStack), "video");
		}
		/* The video owns the screen from here, so a zoom the quick preview had
		 * borrowed the image view for is given back now - the stills around this
		 * video see their own mode and magnification again.  It is deliberately
		 * not done by the zoom apply that read the framing off the preview, which
		 * runs while the preview is still the visible page: handing the image view
		 * back at that point would drop the picture the user is looking at to the
		 * thumbnail's own fit size until the first frame arrived. */
		/* the adjustments the hand-back moves are the video's, not a place the
		 * user put the framing, which is what m_bFramingVideoPreview says: while
		 * it is set, what the adjustments say belongs to the viewer rather than
		 * to the user and is not read back as a pan */
		p->m_bFramingVideoPreview = TRUE;
		p->ReturnImageViewFromPreviewZoom();
		p->m_bFramingVideoPreview = FALSE;
		/* The frame just shown is the first one the user sees, so it has to be
		 * drawn at the zoom this video starts at.  The size request is normally
		 * already in place - the caps event precedes this frame and applies the
		 * zoom while the preview is still up - but if the two raced, re-queue the
		 * apply at a priority that runs before the next frame is drawn, so this
		 * frame is the only one that could be sized wrong. */
		if (p->m_iVideoWidth > 0 && p->m_iVideoHeight > 0)
		{
			if (p->m_iVideoZoomIdle != 0)
				g_source_remove(p->m_iVideoZoomIdle);
			p->m_iVideoZoomIdle = g_idle_add_full(G_PRIORITY_HIGH, video_apply_zoom_idle, p, NULL);
		}
	}
	if (!p->m_bVideoPreviewHasFrame && !p->m_bVideoNeedsFirstFrame)
	{
		p->m_bVideoPreviewHasFrame = TRUE;
		p->UpdateNavControlVisibility();
	}
	if (p->m_pNavigationControl != NULL && GTK_IS_WIDGET(p->m_pNavigationControl))
	{
		if (!p->m_bVideoPreviewRedrawQueued)
		{
			p->m_bVideoPreviewRedrawQueued = TRUE;
			p->m_iVideoPreviewRedrawIdle =
				g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, video_preview_redraw_idle, p, NULL);
		}
	}
}

/* Dragging the nav control's miniature moves the *viewport* the way dragging an
 * image does - this is navigation, not direct manipulation, so the crop window
 * travels with the pointer instead of against it.
 *
 * The control hands us the pointer's absolute position in the miniature, and
 * the box is centred on it: pan = (pos / control size) * frame size - half the
 * visible region, clamped to the frame.  That is the same mapping the control
 * already applies to an image through its adjustments, and it is 1:1 by
 * construction. */
static void nav_control_drag_delta_cb(QuiverNavigationControl *navcontrol, gdouble px, gdouble py, gpointer user_data)
{
	(void)navcontrol;
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl *)user_data;
	if (p == NULL || !p->m_spAlive || !*p->m_spAlive)
		return;
	if (!p->IsVideo() || !p->m_pVideoFixed || !GTK_IS_WIDGET(p->m_pVideoFixed))
		return;
	if (p->m_iVideoWidth <= 0 || p->m_iVideoHeight <= 0)
		return;

	gdouble dispW = p->m_dVideoPreviewDispW;
	gdouble dispH = p->m_dVideoPreviewDispH;
	gdouble ctrlW = (gdouble)gtk_widget_get_width(p->m_pNavigationControl);
	gdouble ctrlH = (gdouble)gtk_widget_get_height(p->m_pNavigationControl);
	if (dispW <= 0. || dispH <= 0. || ctrlW <= 0. || ctrlH <= 0.)
		return;

	/* On screen in the nav control, the visible box has normalized dimensions
	 * m_dVideoPreviewViewW and m_dVideoPreviewViewH. */
	gdouble viewW_disp = p->m_dVideoPreviewViewW * dispW;
	gdouble viewH_disp = p->m_dVideoPreviewViewH * dispH;
	if (viewW_disp <= 0.) viewW_disp = dispW;
	if (viewH_disp <= 0.) viewH_disp = dispH;

	/* The pointer px, py is the desired center of the viewport in the nav control.
	 * Center the visible rectangle on (px, py) in display space, clamped to [0, disp - view_disp]. */
	gdouble target_rot_cx = (px / ctrlW) * dispW;
	gdouble target_rot_cy = (py / ctrlH) * dispH;

	gdouble x_rot_start = CLAMP(target_rot_cx - viewW_disp / 2., 0., MAX(0., dispW - viewW_disp));
	gdouble y_rot_start = CLAMP(target_rot_cy - viewH_disp / 2., 0., MAX(0., dispH - viewH_disp));

	p->SetVideoPanPixels(x_rot_start, y_rot_start);

	/* ApplyVideoZoom re-derives the pan from the pointer over the *video* area
	 * to keep the pixel under it fixed across a zoom change.  During a miniature
	 * drag that is a different pointer in a different widget, so it would
	 * overwrite the pan just set.  m_bVideoPanning is the existing "the user is
	 * dragging, leave the pan alone" guard; borrow it for the length of the
	 * call. */
	gboolean bWasPanning = p->m_bVideoPanning;
	p->m_bVideoPanning = TRUE;
	p->ApplyVideoZoom();
	p->m_bVideoPanning = bWasPanning;
	p->RefreshAutoHideTimer();
	/* dragging needs the pointer visible */
	viewer_set_idle_cursor(p, false);
}

/* Pad probe for downstream ALLOCATION queries on PULL (return path):
 * Hardware video decoders (VA-API, NVDEC, etc.) calculate their hardware Decoded
 * Picture Buffer (DPB) surface pool size as (dpb_size + downstream_min_buffers).
 * When downstream elements like gtk4paintablesink, gltransformation, or tee do not
 * propose sufficient pool margins (defaulting to 0 or 1), the hardware decoder
 * allocates only the bare minimum number of surfaces (e.g. 5 reference + 1 display = 6).
 *
 * When the pipeline branches at zoomtee into both a main display sink and a preview
 * miniature sink, downstream queues and GTK paintable frames concurrently hold
 * multiple buffers (main queue + preview queue + main paintable + preview paintable).
 * With only 6 total surfaces in the pool, holding 2 to 4 buffers downstream leaves
 * the hardware decoder with fewer free surfaces than its required reference frame count.
 * This starves the decoder, forcing it to overwrite an active DPB reference frame and
 * causing cyclic macroblock corruption (e.g. flashing corrupted tiles every 6 or 9 frames).
 *
 * By intercepting the ALLOCATION query as it returns upstream through zoomtee and
 * zoombin's sink pad, we guarantee that the hardware decoder and intermediate GL
 * buffer pools allocate at least 32 surfaces.  This provides ample headroom for
 * multi-sink presentation, display vsync synchronization, and deep reference frame
 * hierarchies without any risk of surface starvation or visual glitching. */
static GstPadProbeReturn video_allocation_query_probe(GstPad *pad, GstPadProbeInfo *info, gpointer user_data)
{
	(void)user_data;
	if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_QUERY_DOWNSTREAM)
	{
		GstQuery *query = GST_PAD_PROBE_INFO_QUERY(info);
		if (query != NULL && GST_QUERY_TYPE(query) == GST_QUERY_ALLOCATION)
		{
			static thread_local bool s_bInAllocProbe = false;
			if (s_bInAllocProbe)
				return GST_PAD_PROBE_OK;
			struct ScopedGuard {
				ScopedGuard() { s_bInAllocProbe = true; }
				~ScopedGuard() { s_bInAllocProbe = false; }
			} guard;

			// Invoke element's query handler so downstream elements and allocators populate the query
			GstPadQueryFunction qfunc = GST_PAD_QUERYFUNC(pad);
			if (qfunc != NULL)
			{
				qfunc(pad, GST_OBJECT_PARENT(pad), query);
			}
			else
			{
				gst_pad_query_default(pad, GST_OBJECT_PARENT(pad), query);
			}

			const guint target_min = 32;
			guint n_pools = gst_query_get_n_allocation_pools(query);
			std::string pool_desc;
			if (n_pools > 0)
			{
				for (guint i = 0; i < n_pools; ++i)
				{
					GstBufferPool *pool = NULL;
					guint size = 0, min_buf = 0, max_buf = 0;
					gst_query_parse_nth_allocation_pool(query, i, &pool, &size, &min_buf, &max_buf);
					min_buf = std::max(min_buf, target_min);
					if (max_buf != 0 && max_buf < min_buf)
						max_buf = min_buf;
					gst_query_set_nth_allocation_pool(query, i, pool, size, min_buf, max_buf);
					if (pool != NULL)
					{
						GstStructure *config = gst_buffer_pool_get_config(pool);
						if (config != NULL)
						{
							GstCaps *pool_caps = NULL;
							guint p_size = 0, p_min = 0, p_max = 0;
							if (gst_buffer_pool_config_get_params(config, &pool_caps, &p_size, &p_min, &p_max))
							{
								p_min = std::max(p_min, target_min);
								if (p_max != 0 && p_max < p_min) p_max = p_min;
								gst_buffer_pool_config_set_params(config, pool_caps, p_size, p_min, p_max);
								gst_buffer_pool_config_add_option(config, GST_BUFFER_POOL_OPTION_GL_SYNC_META);
								gst_buffer_pool_set_config(pool, config);
							}
							else
							{
								gst_structure_free(config);
							}
						}
					}
					if (!pool_desc.empty()) pool_desc += "; ";
					pool_desc += "pool[" + std::to_string(i) + "]=" + (pool ? G_OBJECT_TYPE_NAME(pool) : "null")
						+ " min=" + std::to_string(min_buf) + " max=" + std::to_string(max_buf);
					if (pool != NULL)
						gst_object_unref(pool);
				}
			}
			else
			{
				GstCaps *caps = NULL;
				gboolean need_pool = FALSE;
				gst_query_parse_allocation(query, &caps, &need_pool);
				guint size = 0;
				if (caps != NULL)
				{
					GstVideoInfo info_caps;
					if (gst_video_info_from_caps(&info_caps, caps))
						size = info_caps.size;
				}
				GstBufferPool *gl_pool = NULL;
				Viewer::ViewerImpl *pImpl = (Viewer::ViewerImpl *)user_data;
				if (pImpl != NULL && pImpl->m_pVideoZoomScaler != NULL)
				{
					GstGLContext *gl_context = NULL;
					g_object_get(G_OBJECT(pImpl->m_pVideoZoomScaler), "context", &gl_context, NULL);
					if (gl_context != NULL)
					{
						gl_pool = gst_gl_buffer_pool_new(gl_context);
						if (gl_pool != NULL && caps != NULL)
						{
							GstStructure *config = gst_buffer_pool_get_config(gl_pool);
							if (config != NULL)
							{
								gst_buffer_pool_config_set_params(config, caps, size, target_min, 0);
								gst_buffer_pool_config_add_option(config, GST_BUFFER_POOL_OPTION_GL_SYNC_META);
								gst_buffer_pool_set_config(gl_pool, config);
							}
						}
						gst_object_unref(gl_context);
					}
				}
				gst_query_add_allocation_pool(query, gl_pool, size, target_min, 0);
				pool_desc = "allocated_pool=" + std::string(gl_pool ? G_OBJECT_TYPE_NAME(gl_pool) : "fallback")
					+ " min=" + std::to_string(target_min) + " size=" + std::to_string(size);
				if (gl_pool != NULL)
					gst_object_unref(gl_pool);
			}

			// Ensure GL sync fences are enabled so GPU commands complete before texture reuse
			gst_query_add_allocation_meta(query, GST_GL_SYNC_META_API_TYPE, NULL);

			std::string parent_name = (pad && GST_OBJECT_PARENT(pad)) ? GST_OBJECT_NAME(GST_OBJECT_PARENT(pad)) : "unknown";
			std::string pad_name = pad ? GST_PAD_NAME(pad) : "unknown";
			video_glitch_log_event("[ALLOC PROBE] element=" + parent_name + " pad=" + pad_name + " " + pool_desc);

			return GST_PAD_PROBE_HANDLED;
		}
	}
	return GST_PAD_PROBE_OK;
}

static GstPadProbeReturn video_zoom_reconfigure_probe(GstPad *pad, GstPadProbeInfo *info, gpointer user_data)
{ (void)pad; (void)user_data;

	/* gtk4paintablesink pushes an upstream reconfigure event whenever its widget is
	 * resized, and the crop/caps changes from zooming do too.  If that event
	 * reaches decodebin, the hardware decoder must renegotiate its output
	 * mid-stream, which fails and aborts playback with "Internal data stream
	 * error" (qtdemux not-negotiated).  The zoom bin is self-contained: its
	 * first element accepts whatever caps the decoder provides, so swallow
	 * the reconfigure here instead of letting it disturb the decode chain.
	 *
	 * Likewise, QoS events generated downstream during heavy zoom/pan rendering
	 * must NOT reach the hardware decoder: if the decoder drops reference frames
	 * in response to QoS late events, subsequent P/B-frames decode with macroblock
	 * corruption (scrambled squares of pixels).  Drop QoS events here so the
	 * decoder always maintains clean inter-frame reference pictures. */
	if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_EVENT_UPSTREAM)
	{
		GstEvent *event = GST_PAD_PROBE_INFO_EVENT(info);
		if (GST_EVENT_TYPE(event) == GST_EVENT_RECONFIGURE ||
		    GST_EVENT_TYPE(event) == GST_EVENT_QOS)
			return GST_PAD_PROBE_DROP;
	}
	return GST_PAD_PROBE_OK;
}

/* Probe on zoomcaps' src pad: the bin-internal crop/scaler changes push
 * downstream CAPS and RECONFIGURE events every time the user zooms, but
 * zoomcaps always forces full-frame DMABuf output so glupload's input caps
 * never actually change.  Without this probe each crop change triggers a
 * glupload re-negotiation (upload-method selection + buffer-pool allocation)
 * that can stall the streaming thread for seconds while the main thread
 * piles up more reconfigure events.  Drop redundant downstream events so
 * glupload negotiates once at startup and is left alone afterwards. */
/* Smooth zoom animation is opt-in: a smooth animation changes the crop on
 * every tick, and the VA scaler re-negotiates its output on every crop
 * change, so the scaler lags behind the state and the displayed zoom appears
 * stuck / range-limited until the pipeline catches up (waiting for the next
 * key frame to resync).  Instant zoom applies the crop once per gesture and
 * tracks the state exactly; flip this to 1 to re-enable the animation. */
#define VIDEO_ZOOM_SMOOTH_ANIMATION 1

static gboolean video_zoom_timeout(gpointer data);

/* The pan is kept as the centre of the visible region, as a fraction of the
 * frame, and every caller works in the pixel offset of that region's left/top
 * edge.  These two are the only place that converts between the two.
 *
 * The conversion is a plain shift by half the visible size, so the centre a
 * pixel pan describes does not depend on the frame's size, aspect ratio or the
 * zoom - which is the whole point: a centre that survives a switch to a file
 * with a different aspect ratio, and a pixel offset normalized against that
 * file's own scroll range, would not be the same point. */
static inline gdouble PanOffsetToCenter(gdouble offset, gdouble disp, gdouble view)
{
	if (disp <= 0. || view <= 0.)
		return 0.5;
	/* the whole frame is on screen: there is nowhere to centre it but the middle */
	if (view >= disp)
		return 0.5;
	const gdouble half = view / (2. * disp);
	return CLAMP((offset + view / 2.) / disp, half, 1. - half);
}

static inline gdouble PanCenterToOffset(gdouble center, gdouble disp, gdouble view)
{
	if (disp <= 0. || view <= 0. || view >= disp)
		return 0.;
	return CLAMP(center * disp - view / 2., 0., disp - view);
}

void Viewer::ViewerImpl::SetVideoPanPixels(gdouble px, gdouble py)
{
	/* the callers all think in pixels (a drag delta, a pointer position, a
	 * miniature position); the state is kept as the centre of the visible region
	 * so it survives a change of frame size or aspect ratio */
	m_fVideoPanFX = PanOffsetToCenter(px, m_dVideoPreviewDispW, m_dVideoViewW);
	m_fVideoPanFY = PanOffsetToCenter(py, m_dVideoPreviewDispH, m_dVideoViewH);
	/* The pixel offset is the same point in the frame and is kept in step with
	 * the centre here, rather than only being worked out again by the next
	 * apply.  An offset left describing the last frame is a pan that snaps back:
	 * ApplyVideoZoom() derives the offset from the centre, so a caller that moves
	 * the offset alone has its move undone by the very next thing it calls, which
	 * is how a flick ended on its first step with the frame where it started. */
	m_dVideoPanX = PanCenterToOffset(m_fVideoPanFX, m_dVideoPreviewDispW, m_dVideoViewW);
	m_dVideoPanY = PanCenterToOffset(m_fVideoPanFY, m_dVideoPreviewDispH, m_dVideoViewH);
}

gdouble Viewer::ViewerImpl::GetVideoPanOffsetX() const
{
	return PanCenterToOffset(m_fVideoPanFX, m_dVideoPreviewDispW, m_dVideoViewW);
}

gdouble Viewer::ViewerImpl::GetVideoPanOffsetY() const
{
	return PanCenterToOffset(m_fVideoPanFY, m_dVideoPreviewDispH, m_dVideoViewH);
}

QuiverImageViewMode Viewer::ViewerImpl::GetViewMode() const
{
	/* The image view holds the viewer's view mode: it is the one widget every
	 * item is shown in, and the paths that change the mode for a still (the
	 * mode menu, a zoom gesture, the reset on a new image) all go through it.
	 * The video is drawn by the pipeline rather than by that widget, so it reads
	 * the mode from here rather than keeping a second copy that could drift -
	 * "keep zoom and pan" then means the same thing for a photo and a video,
	 * and a still and a video are never in two different modes at once. */
	if (m_pImageView != NULL && QUIVER_IS_IMAGE_VIEW(m_pImageView))
		return quiver_image_view_get_view_mode(QUIVER_IMAGE_VIEW(m_pImageView));
	return m_eVideoViewMode;
}

/* The mode the user is in, as opposed to the 1:1 the image view drops into
 * while the picture is zoomed away from the size its mode shows it at.  The
 * image view needs that state to draw and scroll such a picture, and the video
 * reads it to know that a zoom is in force, but neither of those is the user
 * having chosen a view mode: what the user chose is the mode the zoom was
 * made from, and that is the one to answer with. */
QuiverImageViewMode Viewer::ViewerImpl::GetChosenViewMode() const
{
	if (m_pImageView != NULL && QUIVER_IS_IMAGE_VIEW(m_pImageView))
	{
		const QuiverImageViewMode mode =
			quiver_image_view_get_view_mode(QUIVER_IMAGE_VIEW(m_pImageView));
		if (QUIVER_IMAGE_VIEW_MODE_ZOOM == mode)
			return quiver_image_view_get_view_mode_unmagnified(QUIVER_IMAGE_VIEW(m_pImageView));
		return mode;
	}
	return m_eVideoViewMode;
}

void Viewer::ViewerImpl::SetVideoViewMode(QuiverImageViewMode mode)
{
	/* The view mode is the viewer's, not an item's: the image view keeps it so
	 * the stills around the video are shown the way the video is, and the video
	 * keeps its own copy because it is drawn by the pipeline rather than by
	 * the image view.  Setting it on both is what makes "keep zoom and pan"
	 * mean the same thing for an image and a video. */
	m_eVideoViewMode = mode;
	if (m_pImageView != NULL && QUIVER_IS_IMAGE_VIEW(m_pImageView)
		&& quiver_image_view_get_view_mode(QUIVER_IMAGE_VIEW(m_pImageView)) != mode)
	{
		quiver_image_view_set_view_mode(QUIVER_IMAGE_VIEW(m_pImageView), mode);
	}
	QuiverUtils::SetRadioActionCurrent(ACTION_VIEWER_ZOOM, mode);
}

bool Viewer::ViewerImpl::GetPreviewPictureSize(gint* width, gint* height) const
{
	if (width != NULL)
		*width = 0;
	if (height != NULL)
		*height = 0;
	if (m_pImageView == NULL || !QUIVER_IS_IMAGE_VIEW(m_pImageView))
		return false;
	QuiverImageView* iv = QUIVER_IMAGE_VIEW(m_pImageView);
	quiver_image_view_get_picture_size(iv, width, height);
	return width != NULL && height != NULL && *width > 0 && *height > 0;
}

bool Viewer::ViewerImpl::AdoptVideoPreviewFraming(bool bProvisional)
{
	/* The quick preview is drawn by the image view, so the framing that is on
	 * screen is the image view's magnification and scroll position - however it
	 * got there.  SetVideoZoom() covers the case where the user zoomed the
	 * preview, but the same framing also arrives by switching to this video from
	 * a still that was zoomed (the image view keeps its magnification on the new
	 * file's thumbnail, so the preview is framed while the video's own zoom is
	 * still whatever it was before).  Reading the screen instead of remembering a
	 * zoom is what makes the video start where the picture on screen already is.
	 *
	 * The image view's magnification is relative to the picture it is drawing
	 * and the video's zoom to the frame, so the two are related by the size of
	 * that picture - the same relation ApplyVideoPreviewZoom() zooms the
	 * preview by, and the same one a preview framed from the video is the other
	 * way round. */
	if (!IsVideoPreviewShowing() || m_ImageListPtr == NULL)
		return false;
	QuiverImageView* iv = QUIVER_IMAGE_VIEW(m_pImageView);
	if (iv == NULL || !QUIVER_IS_IMAGE_VIEW(iv))
		return false;
	gint pictureW = 0, pictureH = 0;
	if (!GetPreviewPictureSize(&pictureW, &pictureH))
		return false;
	QuiverFile vf = m_ImageListPtr->GetCurrent();
	const gint videoW = vf.GetWidth();
	const gint videoH = vf.GetHeight();
	if (pictureW <= 0 || pictureH <= 0 || videoW <= 0 || videoH <= 0)
		return false;
	GtkAdjustment* hadj = quiver_image_view_get_hadjustment(iv);
	GtkAdjustment* vadj = quiver_image_view_get_vadjustment(iv);
	if (hadj == NULL || vadj == NULL)
		return false;
	const gdouble upper = gtk_adjustment_get_upper(hadj);
	const gdouble page = gtk_adjustment_get_page_size(hadj);
	/* the whole frame is on screen: there is no framing of it to keep, so the
	 * video keeps the zoom it had (a fit preview must not turn into a zoom).
	 * A provisional adoption is the one case where there is no telling yet - the
	 * view has not been laid out, so "the whole frame is on screen" cannot be
	 * read from the ranges - and it is left to the first layout to decide. */
	if (!bProvisional && (upper <= 0. || page <= 0. || page >= upper))
		return false;

	/* A magnification is relative to the picture and a zoom to the frame, and
	 * the two are the same size for a video's preview - which is what makes a
	 * zoom of a still mean the same thing on this video as it did on that one.
	 * Were the preview ever a smaller picture than the frame, the fraction of it
	 * in view is the same at a proportionally smaller zoom. */
	const gdouble zoom = quiver_image_view_get_magnification(iv) * ((gdouble)pictureW / (gdouble)videoW);
	if (zoom <= 0. || zoom > 16.)
		return false;

	/* the fit level is unknown until the caps probe reports the frame size, and
	 * the first ApplyVideoZoom() bounds the zoom by the real one */
	m_dVideoZoom = zoom;
	/* A zoom that is on its way somewhere is not to be told where it is going by
	 * the picture it is being applied to: that picture is itself easing towards
	 * the target and so still reports the zoom it came from, and taking that for
	 * the target walks the zoom back where it started - the preview would set off
	 * for the target and arrive back at the beginning.  Where the zoom *is* is
	 * still taken from the view exactly as before, so a drag or a pan is followed
	 * all the same; only the target is left alone, and only while a zoom is in
	 * flight. */
	if (m_iVideoZoomTimeoutID == 0)
		m_dVideoZoomFinal = zoom;
	m_dVideoZoomMin = 1.0;
	CaptureVideoPanFromPreview();
	m_bVideoZoomFromPreview = true;
	return true;
}

bool Viewer::ViewerImpl::IsVideoPreviewFraming() const
{
	/* The framing a video is given when it arrives: the current item is a
	 * video, a zoomed mode is in force, and no frame has come out of the
	 * pipeline yet, so what is on screen is the quick preview. */
	return IsVideo() && !m_bVideoPreviewHasFrame
		&& viewer_view_mode_is_zoomed(GetViewMode());
}

void Viewer::ViewerImpl::FrameVideoPreviewFromVideo(gboolean bNewPicture)
{
	/* The quick preview is drawn by the image view, and a video that arrives
	 * while a zoomed mode is in force is going to play at m_dVideoZoom with the
	 * pan in m_fVideoPanFX/Y.  The preview therefore has to be put there, or the
	 * user is shown one framing and gets another the moment they press play -
	 * and, worse, the preview the image view was carrying over from the previous
	 * item is a *fitted* preview, whose middle of the viewport is not the middle
	 * of the frame at all, so keeping it lands somewhere else again. */
	if (!IsVideoPreviewFraming() || m_ImageListPtr == NULL)
		return;
	/* Moving the view moves the adjustments, and the adjustments are where the
	 * pan of a preview on screen is read from - so what this does is not the
	 * user framing the video, and must not be read back as one. */
	const gboolean bWasFraming = m_bFramingVideoPreview;
	m_bFramingVideoPreview = true;
	QuiverImageView* iv = QUIVER_IMAGE_VIEW(m_pImageView);
	if (iv == NULL || !QUIVER_IS_IMAGE_VIEW(iv))
	{
		m_bFramingVideoPreview = bWasFraming;
		return;
	}
	if (quiver_image_view_get_texture(iv) == NULL)
	{
		m_bFramingVideoPreview = bWasFraming;
		return;
	}
	/* the picture has landed, so it is this item's own: from here its scroll
	 * position is a pan of this video, and is read as one */
	if (m_ImageListPtr != NULL)
		m_sPreviewPanItem = m_ImageListPtr->GetCurrent().GetFilePath();

	/* "Keep zoom and pan" says the framing the user is looking at is the
	 * framing the next item comes up at, and the view has already kept it -
	 * the magnification and the centre it went into this delivery with are the
	 * ones the user chose on the item before.  So here the *view* is the
	 * authority and the video is brought in line with it: imposing the video's
	 * own remembered zoom instead would be imposing a number the user never
	 * chose (a video that has not been zoomed is at 1.0), and the zoom they did
	 * choose would be gone the moment they arrived at the video. */
	if (bNewPicture && viewer_view_mode_is_keep(GetViewMode()))
	{
		/* A picture often lands before the window has laid the view out, and
		 * "is this framing one to keep" is a question about the viewport: a
		 * picture that fills it is a fitted one whatever its magnification
		 * says.  With no viewport there is nothing to tell yet, and imposing the
		 * video's own zoom in the meantime is what put a 1.0 in front of a 2.0
		 * the user had just chosen - so the framing on screen is taken as it
		 * stands, and the first layout re-decides whether it really was a zoom.
		 * Nothing is drawn from the video until that point, so a framing taken
		 * early is only ever visible if it turns out to have been right. */
		const gboolean bLaidOut =
			gtk_widget_get_width(GTK_WIDGET(iv)) > 0
			&& gtk_widget_get_height(GTK_WIDGET(iv)) > 0;
		if (AdoptVideoPreviewFraming(!bLaidOut))
		{
			/* the pan came off the view as part of the adoption, and it is this
			 * item's own preview, so it can be read as a pan of the video */
			m_bPreviewFramingPending = !bLaidOut;
			m_dPreviewFramingMag = bLaidOut ? 0. : quiver_image_view_get_magnification(iv);
			m_bFramingVideoPreview = bWasFraming;
			return;
		}
	}
	m_bPreviewFramingPending = false;
	m_dPreviewFramingMag = 0.;

	/* The picture the view is drawing, which is the frame's own size: the
	 * magnification is relative to the picture, not to the texture decoded for
	 * it.  The size for the mode in force is the picture scaled by the current
	 * magnification, so the picture is that divided by the magnification. */
	gint picture_width = 0;
	gint picture_height = 0;
	const gdouble current_mag = quiver_image_view_get_magnification(iv);
	quiver_image_view_get_pixbuf_display_size_for_mode(iv,
		QUIVER_IMAGE_VIEW_MODE_ACTUAL_SIZE, &picture_width, &picture_height);
	if (current_mag > 0.)
	{
		picture_width = (gint)(picture_width / current_mag + 0.5);
		picture_height = (gint)(picture_height / current_mag + 0.5);
	}
	QuiverFile vf = m_ImageListPtr->GetCurrent();
	const gint videoW = vf.GetWidth();
	const gint videoH = vf.GetHeight();
	if (picture_width <= 0 || picture_height <= 0 || videoW <= 0 || videoH <= 0)
	{
		m_bFramingVideoPreview = bWasFraming;
		return;
	}

	/* the same on-screen scale the video will have: picture * mag == frame * zoom */
	gdouble mag = m_dVideoZoom * ((gdouble)videoW / (gdouble)picture_width);
	if (mag <= 0. || mag > 16.)
	{
		m_bFramingVideoPreview = bWasFraming;
		return;
	}

	/* The pan to go with it, which is the video's own: a kept pan is a fraction
	 * of the frame, so it means the same thing before a frame has come out of
	 * the pipeline as it will afterwards, and a reset one is the centre of the
	 * frame whether or not anything has been seen yet.  A video with no frame
	 * yet therefore does not come out with a pan of zero, which would put the
	 * preview in the corner rather than where the user left the last one. */
	gdouble center_x = m_fVideoPanFX;
	gdouble center_y = m_fVideoPanFY;

	BorrowImageViewForPreviewZoom();
	/* the pan as a fraction of the picture, which is a fraction of the frame:
	 * a preview of a video has the frame's aspect, so it is the same point.
	 * Both at once and at once: a zoom that eases in passes through a
	 * magnification the picture cannot be panned in, and comes back to the
	 * middle of the picture instead of to the corner that was asked for. */
	quiver_image_view_set_framing(iv, mag, center_x, center_y);

	/* the pan came from the video, not from the preview: capturing it back
	 * would round-trip the same numbers and hide which of the two is the
	 * authority */
	UpdateVideoPreviewViewAreaFromImageView();
	UpdateNavControlVisibility();
	/* the view is now showing this item's own preview at the framing the video
	 * will play at, so a pan of it is a pan of the video */
	if (m_ImageListPtr != NULL)
		m_sPreviewPanItem = m_ImageListPtr->GetCurrent().GetFilePath();
	m_bFramingVideoPreview = bWasFraming;
}

/* Set from the loader observer, which is defined before the viewer is: a
 * video's preview is a picture delivered like any other, and the framing the
 * video will play at has to be applied to it where it lands. */
static void frame_video_preview_from_frames(QuiverImageView *imageview, GdkTexture **frames,
	gsize n_frames, gint *delays_ms, gint width, gint height, gboolean reset_view_mode,
	Viewer::ViewerImpl *impl)
{
	if (impl != NULL)
	{
		impl->m_bFramingVideoPreview = true;
		impl->m_sPreviewPanItem.clear();
	}
	if (imageview != NULL && QUIVER_IS_IMAGE_VIEW(imageview))
		quiver_image_view_set_animation_frames(imageview, frames, delays_ms, n_frames,
			width, height, reset_view_mode);
	if (impl != NULL)
	{
		impl->FrameVideoPreviewFromVideo(TRUE);
		impl->m_bFramingVideoPreview = false;
	}
}

static void frame_video_preview_from_video(QuiverImageView *imageview, GdkTexture *texture,
	gint width, gint height, gboolean reset_view_mode, Viewer::ViewerImpl *impl)
{
	/* One place that hands a picture to the view for a video, so the flag, the
	 * delivery and the framing that goes on top of it cannot come apart. */
	if (impl != NULL)
	{
		impl->m_bFramingVideoPreview = true;
		impl->m_sPreviewPanItem.clear();
	}
	if (imageview != NULL && QUIVER_IS_IMAGE_VIEW(imageview) && texture != NULL)
		quiver_image_view_set_texture_at_size_ex(imageview, texture, width, height, reset_view_mode);
	if (impl != NULL)
	{
		impl->FrameVideoPreviewFromVideo(TRUE);
		impl->m_bFramingVideoPreview = false;
	}
}

bool Viewer::ViewerImpl::IsPreviewPanValid() const
{
	if (m_sPreviewPanItem.empty() || m_ImageListPtr == NULL)
		return false;
	/* The preview stands in for the video only until a frame comes out of the
	 * pipeline.  After that the video is on screen and its pan is the video's
	 * own business, and the picture the view still holds behind it is a leftover
	 * - there is a window between the first frame arriving and the video page
	 * being swapped in, and reading the view's scroll in it would hand the
	 * video a framing nobody asked for. */
	if (m_bVideoPreviewHasFrame)
		return false;
	if (m_sPreviewPanItem != m_ImageListPtr->GetCurrent().GetFilePath())
		return false;
	/* And only while the preview is actually the thing on screen.  Pressing play
	 * takes the image view off the screen, and a view that is not on screen has
	 * its scroll position clamped flat by GTK - to the very start of the
	 * content.  Reading it then hands the video the top left of the frame, which
	 * is what a pan captured after the play press would be worth.  The framing is
	 * captured on the way in to playing instead, where the preview is still
	 * showing; after that, the video's pan is the video's own business. */
	return IsVideoPreviewShowing();
}

void Viewer::ViewerImpl::CaptureVideoPanFromPreview(){
	/* A pan read off the view is the video's pan only while the view is showing
	 * this video's own preview at a framing that was chosen for it.  In the
	 * window a switch opens - the previous item still on screen, a delivery
	 * being laid out, a resize recentring the view - the scroll position is the
	 * view's own bookkeeping, and taking it wrote a framing the user never asked
	 * for into the video that was just loaded. */
	if (!IsPreviewPanValid())
		return;
	/* The quick preview is drawn by the image view, so where it shows is the
	 * image view's scroll position: the fraction of the content already
	 * scrolled past is the fraction of the frame that is off screen, for a
	 * frame of any size.  That is the same fraction the video keeps, so
	 * playback starts exactly where the preview was - whether the user got
	 * there by zooming, by dragging, or by both. */
	if (m_pImageView == NULL || !QUIVER_IS_IMAGE_VIEW(m_pImageView))
		return;
	QuiverImageView* iv = QUIVER_IMAGE_VIEW(m_pImageView);
	GtkAdjustment* hadj = quiver_image_view_get_hadjustment(iv);
	GtkAdjustment* vadj = quiver_image_view_get_vadjustment(iv);
	if (hadj == NULL || vadj == NULL)
		return;
	gdouble upper = gtk_adjustment_get_upper(hadj);
	gdouble vupper = gtk_adjustment_get_upper(vadj);
	if (upper <= 0. || vupper <= 0.)
		return;
	/* The image view scrolls a *content* whose size is the frame scaled by the
	 * preview's magnification, and its value/page/upper are all in those content
	 * pixels, so the centre of what it shows is (value + page/2) / upper of the
	 * frame - the same kind of value the video keeps.  Reading it as an offset
	 * fraction instead (value / upper) is the same number as the centre only when
	 * the whole frame is visible, and drifts by half the visible width as soon
	 * as it is not, which is what made playback start beside the framing the
	 * user had set rather than on it. */
	m_fVideoPanFX = PanOffsetToCenter(gtk_adjustment_get_value(hadj), upper,
		gtk_adjustment_get_page_size(hadj));
	m_fVideoPanFY = PanOffsetToCenter(gtk_adjustment_get_value(vadj), vupper,
		gtk_adjustment_get_page_size(vadj));
	m_bVideoPanFromPreview = true;
}

bool Viewer::ViewerImpl::IsVideoPreviewPanSource() const
{
	return IsPreviewPanValid();
}

bool Viewer::ViewerImpl::IsVideoPreviewShowing() const
{
	/* The quick preview: the video is the current item, the pipeline has not
	 * handed out a frame yet, and the image view is on screen in its place
	 * drawing the video's own thumbnail. */
	if (!IsVideo() || m_bVideoPreviewHasFrame)
		return false;
	if (m_pImageView == NULL || !QUIVER_IS_IMAGE_VIEW(m_pImageView))
		return false;
	if (!gtk_widget_get_visible(m_pImageView))
		return false;
	/* And it has to be the child the stack is showing.  Switching the stack to the
	 * video page does not clear the image view's own visible flag - it is made
	 * visible again for the next still, which is why the check above cannot be the
	 * whole answer - so between the two there is a stretch in which this says yes
	 * while the preview is off screen and its scroll position is left over from
	 * the moment before the switch.  Anything read off the view then is the view's
	 * own bookkeeping, and that is what took the pan apart on the way to playback:
	 * the hand-back below resets the scroll, the reset is read as the user's pan,
	 * and the frame is drawn from it. */
	if (m_pStack == NULL || !GTK_IS_STACK(m_pStack)
		|| gtk_stack_get_visible_child(GTK_STACK(m_pStack)) != m_pImageView)
		return false;
	return quiver_image_view_get_texture(QUIVER_IMAGE_VIEW(m_pImageView)) != NULL;
}

void Viewer::ViewerImpl::UpdateVideoPreviewViewAreaFromImageView()
{
	/* While the preview is on screen it is the image view that draws it, so the
	 * nav control's box is the image view's visible region - the same fraction
	 * of the frame, in the same normalized coordinates the stills use.  That is
	 * what lets the control appear (and follow the zoom and the pan) before any
	 * frame has come out of the pipeline. */
	if (m_pNavigationControl == NULL || m_pImageView == NULL || !QUIVER_IS_IMAGE_VIEW(m_pImageView))
		return;
	GtkAdjustment* hadj = quiver_image_view_get_hadjustment(QUIVER_IMAGE_VIEW(m_pImageView));
	GtkAdjustment* vadj = quiver_image_view_get_vadjustment(QUIVER_IMAGE_VIEW(m_pImageView));
	if (hadj == NULL || vadj == NULL)
		return;
	gdouble upper = gtk_adjustment_get_upper(hadj);
	gdouble vupper = gtk_adjustment_get_upper(vadj);
	if (upper <= 0. || vupper <= 0.)
		return;
	m_dVideoPreviewViewX = CLAMP(gtk_adjustment_get_value(hadj) / upper, 0., 1.);
	m_dVideoPreviewViewY = CLAMP(gtk_adjustment_get_value(vadj) / vupper, 0., 1.);
	m_dVideoPreviewViewW = CLAMP(gtk_adjustment_get_page_size(hadj) / upper, 0., 1.);
	m_dVideoPreviewViewH = CLAMP(gtk_adjustment_get_page_size(vadj) / vupper, 0., 1.);
	UpdateVideoPreviewViewArea();
}

void Viewer::ViewerImpl::ResetVideoPan()
{
	m_fVideoPanFX = 0.5;
	m_fVideoPanFY = 0.5;
	m_dVideoPanX = 0.;
	m_dVideoPanY = 0.;
}

void Viewer::ViewerImpl::SetVideoZoom(gdouble zoom)
{
	StopVideoPanSlowdown();
	/* Before the first frame the pipeline has no caps, so there is no frame
	 * size to scale.  The quick preview is on screen instead: zoom that, and
	 * keep the factor so playback snaps straight to it. */
	if (m_iVideoWidth <= 0 || m_iVideoHeight <= 0)
	{
		if (zoom > m_dVideoZoomMin + 0.005 && !viewer_view_mode_is_zoomed(GetViewMode()))
			SetVideoViewMode(QUIVER_IMAGE_VIEW_MODE_ZOOM);
		m_bVideoZoomFromPreview = true;
		/* Eased towards, the way a picture's magnification is and the way a
		 * playing video's zoom already is: the same halving the image view uses,
		 * driven by the same timeout, so a wheel gesture over the preview moves
		 * into the zoom instead of arriving at it.  The factor itself is kept
		 * exactly as before, so the frame still plays from where the preview was
		 * left; only the way there is animated now. */
		if (zoom != m_dVideoZoomFinal)
		{
			m_dVideoZoomFinal = zoom;
			if (0 == m_iVideoZoomTimeoutID)
				m_iVideoZoomTimeoutID = g_timeout_add(30, video_zoom_timeout, this);
		}
		else
		{
			m_dVideoZoom = zoom;
			m_bVideoZoomAnchorCenter = false;
		}
		ApplyVideoPreviewZoom(m_dVideoZoom);
		UpdateUI();
		return;
	}
	m_bVideoZoomFromPreview = false;
	/* Zooming with +/- or the wheel pins the factor: the zoom is relative to the
	 * actual size (1.0 = 100%), clamped from the fit level (the smallest scale
	 * the video has been seen at) up to 16x, so the same image-like zoom is
	 * available no matter how small the window is.
	 *
	 * It does not change the view mode.  The mode says what happens on the
	 * *next* item, so it belongs to the user and to the menu item that is
	 * showing it: switching it here made "keep zoom and pan" uncheck itself the
	 * moment the user zoomed in, which is exactly when they are zooming, and
	 * silently dropped the framing the next item was supposed to inherit.
	 * Zooming a picture that is merely *fitted* does leave the fit modes, since
	 * that is what zooming a fitted picture means. */
	const gboolean bAlreadyZoomed = viewer_view_mode_is_zoomed(GetViewMode());
	if (zoom <= m_dVideoZoomMin + 0.005)
	{
		zoom = m_dVideoZoomMin;
		/* reaching the fit level is not a new mode in "keep zoom and pan":
		 * that mode is the one that has to survive the next item, so it stays
		 * put and the zoom is simply at its floor */
		if (!bAlreadyZoomed)
			SetVideoViewMode(QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW);
	}
	else
	{
		if (zoom > 16.0) zoom = 16.0;
		if (!bAlreadyZoomed)
			SetVideoViewMode(QUIVER_IMAGE_VIEW_MODE_ZOOM);
	}

#if VIDEO_ZOOM_SMOOTH_ANIMATION
	/* animate toward the target so the video eases in like the image view.
	 * The animation only moves the crop inside the zoom bin and resizes the
	 * sink widget; the reconfigure probe swallows the resize's upstream
	 * reconfigure at the bin boundary and the always-on capsfilter keeps the
	 * GL upload's input caps fixed, so the per-tick changes no longer storm
	 * the decode chain (the failures that forced the instant version). */
	if (zoom != m_dVideoZoomFinal)
	{
		m_dVideoZoomFinal = zoom;
		if (0 == m_iVideoZoomTimeoutID)
		{
			m_iVideoZoomTimeoutID = g_timeout_add(30, video_zoom_timeout, this);
		}
	}
	else
	{
		m_bVideoZoomAnchorCenter = false;
	}
#else
	/* apply the zoom immediately instead of animating it: a smooth animation
	 * changes the crop every 30 ms and the VA scaler re-negotiates its output
	 * on every change, so the scaler lags and the displayed zoom looks stuck
	 * until the pipeline resyncs on the next key frame.  One crop change per
	 * gesture renegotiates once, the same as a window resize, which is safe. */
	if (m_iVideoZoomTimeoutID != 0)
	{
		g_source_remove(m_iVideoZoomTimeoutID);
		m_iVideoZoomTimeoutID = 0;
	}
	m_dVideoZoom = zoom;
	m_dVideoZoomFinal = zoom;
	ApplyVideoZoom();
	m_bVideoZoomAnchorCenter = false;
#endif
	UpdateUI();
}


static void video_zoom_get_pointer(Viewer::ViewerImpl *p, gdouble *px, gdouble *py)
{
	video_zoom_get_pointer_in(p, p->m_pVideoFixed, px, py);
}

static void video_zoom_get_pointer_in(Viewer::ViewerImpl *p, GtkWidget *area, gdouble *px, gdouble *py)
{
	/* pointer position in the given widget's coords, or (-1,-1) when it is
	 * outside / the widget is not realized yet / the zoom was not a pointer zoom
	 * at all (callers zoom about the center then, which is what an action, a
	 * menu item or a HUD button does to a picture too) */
	*px = -1.;
	*py = -1.;
	if (p->m_bVideoZoomAnchorCenter || p->IsPointerOverControls())
		return;
	if (area == NULL || !gtk_widget_get_realized(area))
		return;
	gint w = gtk_widget_get_width(area);
	gint h = gtk_widget_get_height(area);
	if (w <= 0 || h <= 0)
		return;
	/* use the toplevel window for the pointer lookup and translate the
	 * result back into the area's coords */
	GtkWidget *toplevel = GTK_WIDGET(gtk_widget_get_root(area));
	if (toplevel == NULL || !gtk_widget_get_realized(toplevel))
		return;
	double ddx = 0, ddy = 0;
	graphene_point_t origin = GRAPHENE_POINT_INIT(0.f, 0.f);
	graphene_point_t out;
	if (!gtk_widget_compute_point(area, toplevel, &origin, &out))
		return;
	ddx = out.x;
	ddy = out.y;
	gint dx = (gint)ddx, dy = (gint)ddy;
	GtkNative *native = gtk_widget_get_native(toplevel);
	if (native == NULL)
		return;
	GdkSurface *surface = gtk_native_get_surface(native);
	if (surface == NULL)
		return;
	GdkDisplay *display = gdk_surface_get_display(surface);
	if (display == NULL)
		return;
	GdkSeat *seat = gdk_display_get_default_seat(display);
	if (seat == NULL)
		return;
	GdkDevice *device = gdk_seat_get_pointer(seat);
	if (device == NULL)
		return;
	double surfx = 0, surfy = 0;
	GdkModifierType mask;
	gdk_surface_get_device_position(surface, device, &surfx, &surfy, &mask);
	gint x = (gint)surfx - dx;
	gint y = (gint)surfy - dy;
	if (x >= 0 && x <= w && y >= 0 && y <= h)
	{
		*px = x;
		*py = y;
	}
}

#if VIDEO_ZOOM_SMOOTH_ANIMATION
/* The zoom is the video's either way; what it is applied to is whichever picture
 * is on screen, so the tween does not have to know which: before the first frame
 * arrives the quick preview *is* the video as far as the user is concerned, and
 * after it the pipeline is. */
static void video_zoom_apply(Viewer::ViewerImpl *p)
{
	if (p->m_iVideoWidth <= 0 || p->m_iVideoHeight <= 0)
		p->ApplyVideoPreviewZoom(p->m_dVideoZoom);
	else
		p->ApplyVideoZoom();
}

static gboolean video_zoom_timeout(gpointer data)
{
	/* ease the zoom toward its target the way the image view does: halve the
	 * difference on every tick, stopping once we are within a few percent */
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl*)data;
	gdouble old_zoom = p->m_dVideoZoom;
	gdouble final = p->m_dVideoZoomFinal;
	gdouble mag_diff = final - old_zoom;

	gdouble percent = final / old_zoom;
	if (percent > 1.0)
		percent = 1.0 / percent;
	gdouble pct = 100. - percent * 100.;

	if (pct < 5.)
	{
		p->m_dVideoZoom = final;
		p->m_iVideoZoomTimeoutID = 0;
		video_zoom_apply(p);
		p->m_bVideoZoomAnchorCenter = false;
		return FALSE;
	}

	p->m_dVideoZoom = old_zoom + mag_diff / 2.;
	video_zoom_apply(p);
	return TRUE;
}
#endif

void Viewer::ViewerImpl::ApplyVideoZoom()
{
	m_iLastZoomApplyTimeUs.store(g_get_monotonic_time(), std::memory_order_relaxed);
	m_uZoomApplyCount.fetch_add(1, std::memory_order_relaxed);

	if (NULL == m_pVideoFixed || NULL == m_pVideoSinkWidget)
	{
		return;
	}

	/* wait for the first caps so we know the source frame size */
	if (m_iVideoWidth <= 0 || m_iVideoHeight <= 0)
	{
		return;
	}

	/* Wait for ASYNC_DONE to restore opacity to prevent old frame flash */

	gint areaW = gtk_widget_get_width(m_pVideoFixed);
	gint areaH = gtk_widget_get_height(m_pVideoFixed);
	if (areaW <= 0 || areaH <= 0)
	{
		/* The video page has no allocation yet, so it is not the stack's visible
		 * child: the quick preview is still on screen instead.  Both are plain
		 * stack children and get the whole stack, so the image view's allocation
		 * is the area the video page is about to get.  Using it here is what puts
		 * the framing chosen on the preview in place *before* the first frame is
		 * shown.  Without it the caps event arrives, this returns, the first
		 * frame is shown at whatever size the previous video left in the sink's
		 * size request, and the apply that follows it is the visible jump - the
		 * picture zooms out and back in on the way from the preview to playback. */
		if (m_pStack != NULL && GTK_IS_STACK(m_pStack)
			&& gtk_stack_get_visible_child(GTK_STACK(m_pStack)) == m_pImageView)
		{
			areaW = gtk_widget_get_width(m_pImageView);
			areaH = gtk_widget_get_height(m_pImageView);
		}
		else
		{
		}
		if (areaW <= 0 || areaH <= 0)
		{
			return;
		}
	}

	gdouble srcW = m_iVideoWidth;
	gdouble srcH = m_iVideoHeight;
	gdouble dispW = srcW;
	gdouble dispH = srcH;
	if (m_iVideoParD > 0 && m_iVideoParN > m_iVideoParD)
		dispW = srcW * ((gdouble)m_iVideoParN / m_iVideoParD);
	else if (m_iVideoParN > 0 && m_iVideoParD > m_iVideoParN)
		dispH = srcH * ((gdouble)m_iVideoParD / m_iVideoParN);
	/* The pipeline renders the coded frames unrotated (the display matrix is
	 * applied by the sink for the actual picture), so the caps width/height
	 * stay the *coded* dimensions even for a 90-degree-rotated video.  When
	 * the current file's display orientation is portrait but the coded frame
	 * is landscape (or vice versa) the video is rotated a quarter turn: the
	 * display aspect must be swapped so the window and FIT/zoom math use the
	 * portrait dims, while the crop in source pixels keeps using the coded
	 * srcW/srcH above. */
	if (!(m_ImageListPtr == NULL))
	{
		QuiverFile vf = m_ImageListPtr->GetCurrent();
		if (vf.GetWidth() > 0 && vf.GetHeight() > 0)
		{
			bool codedLandscape = (srcW > srcH);
			bool displayLandscape = (vf.GetWidth() > vf.GetHeight());
			if (codedLandscape != displayLandscape)
				swap(dispW, dispH);
		}
	}
	if (m_iVideoUserRotation == 90 || m_iVideoUserRotation == 270)
	{
		swap(dispW, dispH);
	}
	/* the zoom is the magnification relative to the video's actual size
	 * (1.0 = 100%), exactly like the image view: FIT never upscales a small
	 * video past its actual size, FIT_STRETCH upscales it to fill the window,
	 * ACTUAL_SIZE pins 100%, FILL_SCREEN covers the whole window, and ZOOM
	 * keeps the factor the user zoomed to with +/- or the wheel.  The FIT
	 * variants are recomputed here on every window resize so the video
	 * re-fits; the others keep their fixed zoom. */
	gdouble fitScale = MIN((gdouble)areaW / dispW, (gdouble)areaH / dispH);
	if (fitScale <= 0.)
		fitScale = 1.;
	gdouble fillScale = MAX((gdouble)areaW / dispW, (gdouble)areaH / dispH);
	gdouble fitZoom = MIN(fitScale, 1.0);

	gdouble zoom;
	/* a zoom chosen on the quick preview arrives before any fit level was
	 * known, so bound it by the real one now that the size is */
	if (m_bVideoZoomFromPreview)
	{
		m_bVideoZoomFromPreview = false;
		m_dVideoZoomMin = fitZoom;
		m_dVideoZoomFinal = CLAMP(m_dVideoZoomFinal, m_dVideoZoomMin, 16.0);
		/* The quick preview is still on screen and is the image view, so this
		 * is the last moment the framing the user chose can be read off it: the
		 * zoom and the pan it was left at are what this first frame - and so
		 * playback - starts at, instead of the frame being recentred under the
		 * user.  Read before the image view is handed back to its still.
		 *
		 * Handing it back is not done here but when the video page takes the
		 * screen: until then the image view *is* what the user is looking at, so
		 * returning it now would visibly drop the preview back to the thumbnail's
		 * own fit magnification while the preview is still on screen. */
		if (m_bPreviewZoomBorrowed)
			CaptureVideoPanFromPreview();
	}
	/* the view mode belongs to the viewer, not to the item: whatever mode the
	 * stills are shown in, the video follows it, so "keep zoom and pan" means
	 * the same thing for a photo and a video */
	QuiverImageViewMode videoViewMode = GetViewMode();
	m_eVideoViewMode = videoViewMode;
	switch (videoViewMode)
	{
		case QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP:
			/* like ZOOM - the factor the user zoomed to and the fraction of
			 * the frame in view are both kept - except that reaching the fit
			 * level does not turn into a fit mode: this is the mode that
			 * survives the next item, so it has to keep its zoom and pan */
			m_dVideoZoomMin = fitZoom;
			zoom = CLAMP(m_dVideoZoom, m_dVideoZoomMin, 16.0);
			break;
		case QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW:
			zoom = fitZoom;
			m_dVideoZoomMin = fitZoom;
			m_dVideoZoomFinal = fitZoom;
			if (m_iVideoZoomTimeoutID != 0)
			{
				g_source_remove(m_iVideoZoomTimeoutID);
				m_iVideoZoomTimeoutID = 0;
			}
			break;
		case QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW_STRETCH:
			zoom = fitScale;
			m_dVideoZoomMin = fitScale;
			m_dVideoZoomFinal = fitScale;
			if (m_iVideoZoomTimeoutID != 0)
			{
				g_source_remove(m_iVideoZoomTimeoutID);
				m_iVideoZoomTimeoutID = 0;
			}
			break;
		case QUIVER_IMAGE_VIEW_MODE_ACTUAL_SIZE:
			zoom = 1.0;
			m_dVideoZoomMin = 1.0;
			break;
		case QUIVER_IMAGE_VIEW_MODE_FILL_SCREEN:
			zoom = fillScale;
			m_dVideoZoomMin = fillScale;
			break;
		default:
			zoom = CLAMP(m_dVideoZoom, m_dVideoZoomMin, 16.0);
			/* Zooming out back to the fit level: switch the video to its fit
			 * mode (stretched, which is how every freshly loaded video
			 * starts) so a window resize re-fits it instead of keeping it
			 * pinned at the old fit size.  Only the video's own mode
			 * changes - the image view keeps whatever mode it had. */
			if (zoom <= m_dVideoZoomMin + 0.005)
			{
				videoViewMode = QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW_STRETCH;
				SetVideoViewMode(videoViewMode);
				zoom = fitScale;
				m_dVideoZoomMin = fitScale;
				m_dVideoZoomFinal = fitScale;
				if (m_iVideoZoomTimeoutID != 0)
				{
					g_source_remove(m_iVideoZoomTimeoutID);
					m_iVideoZoomTimeoutID = 0;
				}
			}
			break;
	}
	m_dVideoZoom = zoom;

	/* GTK4 paintable scaling: the video widget scales directly with zoom,
	 * rendered as a textured quad directly on the GPU by GTK4.
	 * GStreamer stays permanently in passthrough mode, eliminating all
	 * buffer recycling races, pipeline renegotiations, and macroblock glitches. */
	gdouble effZoom = zoom;
	gdouble widgetW = dispW * effZoom;
	gdouble widgetH = dispH * effZoom;


	/* The visible region in display pixels: what the window actually shows */
	gdouble vw = (effZoom > 0.) ? (areaW / effZoom) : dispW;
	gdouble vh = (effZoom > 0.) ? (areaH / effZoom) : dispH;

	m_dVideoPreviewDispW = dispW;
	m_dVideoPreviewDispH = dispH;

	/* the centre of the visible part is kept as a fraction of the frame so the
	 * same centre frames the same relative part of any frame, and so the nav
	 * control's zoom box means the same thing for a 4k photo and a 1080p video;
	 * the pixel pan below is what the window is positioned by */
	/* the visible part of the frame is stored as its centre (a fraction of the
	 * frame) so the same centre frames the same relative part of any frame, and
	 * so the nav control's zoom box means the same thing for a 4k photo and a
	 * 1080p video; the pixel pan below is what the window is positioned by */
	m_dVideoPanRangeX = MAX(0., dispW - vw);
	m_dVideoPanRangeY = MAX(0., dispH - vh);
	/* the visible size is what turns a pixel pan into that centre and back, so
	 * it has to be the new one before anything converts: the zoom anchoring
	 * below works out pixel pans for the layout being applied right now */
	m_dVideoViewW = vw;
	m_dVideoViewH = vh;

	/* keep the display coordinate under the pointer fixed while zoom changes,
	 * so the video zooms in on the cursor; when the pointer is not over the
	 * area (e.g. a toolbar zoom button) zoom about the center (skipped while
	 * dragging or kinetic slowdown so user movement is not overridden) */
	gdouble zoomViewDeltaW = fabs(widgetW - m_dVideoLastWidgetW);
	gdouble zoomViewDeltaH = fabs(widgetH - m_dVideoLastWidgetH);
	/* Only when the zoom really changed the picture.  Re-anchoring on every apply
	 * would move a framing that was set by something else - the pan captured from
	 * the quick preview, the kept pan of another item - to whatever the last
	 * pointer position was, on an apply that changed no size at all. */
	/* Anchoring is for zooming.  It keeps the display point under the pointer
	 * where it is, which is what a zoom should do - but the picture also changes
	 * size when the *frame* changes (another file, another aspect ratio) at an
	 * unchanged zoom factor, and anchoring there moves the framing off the centre
	 * that is supposed to be kept: the pointer's point of the frame is kept
	 * instead of the middle of what is on screen, so a file switch slides the
	 * picture.  The centre is a fraction of the frame, so leaving it alone is
	 * exactly what carries it over. */
	const bool bZoomChanged = (m_dVideoLastWidgetW > 0.)
		&& (fabs(effZoom - m_dVideoLastFrameZoom) > 0.0005);
	if (!m_bVideoPanning && !m_bVideoPanSlowdownActive && m_dVideoLastWidgetW > 0.
		&& bZoomChanged && (zoomViewDeltaW > 0.5 || zoomViewDeltaH > 0.5))
	{
		gdouble px = -1., py = -1.;
		video_zoom_get_pointer(this, &px, &py);
		if (px < 0. || py < 0.)
		{
			px = areaW / 2.;
			py = areaH / 2.;
		}
		/* 1. Find the display coordinate under the pointer from the PREVIOUS layout */
		gdouble dispX, dispY;
		if (m_dVideoLastWidgetW > areaW)
			dispX = m_dVideoPanX + px * dispW / m_dVideoLastWidgetW;
		else
			dispX = (px - (areaW - m_dVideoLastWidgetW) / 2.) * dispW / m_dVideoLastWidgetW;

		if (m_dVideoLastWidgetH > areaH)
			dispY = m_dVideoPanY + py * dispH / m_dVideoLastWidgetH;
		else
			dispY = (py - (areaH - m_dVideoLastWidgetH) / 2.) * dispH / m_dVideoLastWidgetH;

		/* 2. Compute the new pan so that the same coordinate stays under the pointer
		 * in the NEW layout.  One axis at a time: each is a pixel offset in the new
		 * layout, which SetVideoPanPixels turns into the centre that is kept; the
		 * axis that is not being anchored is passed as its own offset so that it
		 * keeps the centre it already had. */
		if (widgetW > areaW)
			SetVideoPanPixels(dispX - px * dispW / widgetW, GetVideoPanOffsetY());
		else
			SetVideoPanPixels(0., GetVideoPanOffsetY());

		if (widgetH > areaH)
			SetVideoPanPixels(GetVideoPanOffsetX(), dispY - py * dispH / widgetH);
		else
			SetVideoPanPixels(GetVideoPanOffsetX(), 0.);
	}
	else if (!m_bVideoPanning && !m_bVideoPanSlowdownActive && m_dVideoLastWidgetW == 0.)
	{
		/* first ApplyVideoZoom after a reset (StopVideo / init / rotate): center the
		 * visible viewport in the display area - unless a zoom on the quick preview
		 * already framed it (that is where playback has to start), or the mode is
		 * "keep zoom and pan", which keeps the pan across items and a rotate
		 * alike: it is a fraction of the frame, so it survives both */
		if (!m_bVideoPanFromPreview && !viewer_view_mode_is_keep(videoViewMode))
		{
			m_fVideoPanFX = 0.5;
			m_fVideoPanFY = 0.5;
		}
		m_bVideoPanFromPreview = false;
	}

	gdouble offX, offY;
	if (widgetW > areaW)
	{
		m_dVideoPanX = PanCenterToOffset(m_fVideoPanFX, dispW, vw);
		offX = -m_dVideoPanX * effZoom;
	}
	else
	{
		/* the whole frame fits: there is nothing to pan, and the kept pan
		 * becomes the centred one so a later zoom starts from the middle */
		m_fVideoPanFX = 0.5;
		m_dVideoPanX = 0.;
		offX = (areaW - widgetW) / 2.;
	}

	if (widgetH > areaH)
	{
		m_dVideoPanY = PanCenterToOffset(m_fVideoPanFY, dispH, vh);
		offY = -m_dVideoPanY * effZoom;
	}
	else
	{
		m_fVideoPanFY = 0.5;
		m_dVideoPanY = 0.;
		offY = (areaH - widgetH) / 2.;
	}

	/* Tell the nav control's miniature which part of the frame is on screen */
	if (m_pVideoRotatedPaintable != NULL && dispW > 0 && dispH > 0)
	{
		ConfigureVideoPreviewScale((gint)dispW, (gint)dispH);
		m_dVideoPreviewViewX = CLAMP(m_dVideoPanX / dispW, 0.0, 1.0);
		m_dVideoPreviewViewY = CLAMP(m_dVideoPanY / dispH, 0.0, 1.0);
		m_dVideoPreviewViewW = CLAMP(vw / dispW, 0.0, 1.0);
		m_dVideoPreviewViewH = CLAMP(vh / dispH, 0.0, 1.0);
		UpdateVideoPreviewViewArea();
	}

	/* A video's zoom never touches the scrollable adjustments, so the nav
	 * control has to re-evaluate itself here: the first frame sizes the
	 * miniature, and every zoom, pan, rotate or resize changes whether less
	 * than the whole frame is on screen. */
	UpdateNavControlVisibility();

	/* Keep GStreamer transformation permanently at identity (passthrough) */
	if (m_pVideoZoomScaler != NULL && m_VideoZoomType == VIDEO_ZOOM_GL)
	{
		if (fabs(m_fVideoZoomSx - 1.0f) > 1e-4f ||
		    fabs(m_fVideoZoomSy - 1.0f) > 1e-4f ||
		    fabs(m_fVideoZoomTx) > 1e-4f ||
		    fabs(m_fVideoZoomTy) > 1e-4f)
		{
			m_fVideoZoomSx = 1.0f;
			m_fVideoZoomSy = 1.0f;
			m_fVideoZoomTx = 0.0f;
			m_fVideoZoomTy = 0.0f;
			g_object_set(G_OBJECT(m_pVideoZoomScaler),
				"scale-x", 1.0f,
				"scale-y", 1.0f,
				"translation-x", 0.0f,
				"translation-y", 0.0f,
				NULL);
		}
	}

	/* size the video widget to the display and move it: centered when it fits,
	 * slid against the viewport pan when it overflows (offX/offY are negative
	 * then, and the freelayout clips the part outside the window) */
	gint newW = MAX(1, (gint)(widgetW + 0.5));
	gint newH = MAX(1, (gint)(widgetH + 0.5));
	gint newX = (gint)(offX + 0.5);
	gint newY = (gint)(offY + 0.5);
	/* only touch the widget when values actually changed; setting the same
	 * size_request / position triggers a GtkLayout re-allocate which fires
	 * size-allocate → resize_cb → another idle → infinite loop */
	if (newW != m_iVideoSinkW || newH != m_iVideoSinkH
		|| newX != m_iVideoSinkX || newY != m_iVideoSinkY)
	{
		m_iVideoSinkW = newW;
		m_iVideoSinkH = newH;
		m_iVideoSinkX = newX;
		m_iVideoSinkY = newY;
		gtk_widget_set_size_request(m_pVideoSinkWidget, newW, newH);
		quiver_freelayout_move(m_pVideoFixed, m_pVideoSinkWidget, newX, newY);
	}

	/* report the zoom percentage in the statusbar, like the image view does
	 * (only while the video is actually shown, so a resize idle while viewing
	 * an image does not clobber the image's percentage) */
	if (m_StatusbarPtr && gtk_widget_get_visible(m_pVideoSinkWidget))
		m_StatusbarPtr->SetMagnification((int)(m_dVideoZoom * 100. + 0.5));

	m_dVideoLastFrameW = dispW;
	m_dVideoLastFrameH = dispH;
	m_dVideoLastFrameZoom = effZoom;
	m_dVideoLastWidgetW = widgetW;
	m_dVideoLastWidgetH = widgetH;
	m_dVideoLastZc = 1.0;

	/* While the quick preview is the picture on screen it has to keep showing
	 * what the video is now at, and the video's framing moves after a delivery
	 * too - the fit level is only known once the caps probe has reported the
	 * frame, and the first apply with a real frame re-derives the zoom from it.
	 * A preview framed at the delivery's numbers would then be left behind, and
	 * pressing play would jump. */
	if (IsVideoPreviewFraming())
	{
		FrameVideoPreviewFromVideo(FALSE);
	}
}

bool Viewer::ViewerImpl::CanVideoPan() const
{
	if (!IsVideo())
		return false;
	if (m_dVideoZoom > m_dVideoZoomMin + 0.005 || m_dVideoZoomFinal > m_dVideoZoomMin + 0.005)
		return true;
	if (m_pVideoFixed == NULL)
		return false;
	gint areaW = gtk_widget_get_width(m_pVideoFixed);
	gint areaH = gtk_widget_get_height(m_pVideoFixed);
	if (areaW > 0 && areaH > 0 && m_dVideoLastWidgetW > 0. && m_dVideoLastWidgetH > 0.)
	{
		if (m_dVideoLastWidgetW > areaW + 0.5 || m_dVideoLastWidgetH > areaH + 0.5)
			return true;
	}
	return false;
}

void Viewer::ViewerImpl::RecordVideoPanSample(gdouble dx, gdouble dy, gdouble dt)
{
	if (dt <= 0.0)
		return;

	if (m_iVideoPanSampleCount < VIDEO_PAN_MAX_SAMPLES)
	{
		m_aVideoPanSamples[m_iVideoPanSampleCount++] = {dx, dy, dt};
	}
	else
	{
		for (int i = 0; i < VIDEO_PAN_MAX_SAMPLES - 1; ++i)
		{
			m_aVideoPanSamples[i] = m_aVideoPanSamples[i + 1];
		}
		m_aVideoPanSamples[VIDEO_PAN_MAX_SAMPLES - 1] = {dx, dy, dt};
	}
}

void Viewer::ViewerImpl::StopVideoPanSlowdown()
{
	m_bVideoPanSlowdownActive = false;
	if (m_uiVideoPanSlowdownTickID != 0)
	{
		if (m_pVideoFixed != NULL)
			gtk_widget_remove_tick_callback(m_pVideoFixed, m_uiVideoPanSlowdownTickID);
		m_uiVideoPanSlowdownTickID = 0;
	}
	if (m_uiVideoPanSlowdownTimeoutID != 0)
	{
		g_source_remove(m_uiVideoPanSlowdownTimeoutID);
		m_uiVideoPanSlowdownTimeoutID = 0;
	}
	m_dVideoPanVelX = 0.;
	m_dVideoPanVelY = 0.;
	m_iVideoPanSlowdownLastTime = 0;
	m_iVideoPanSampleCount = 0;
}

void Viewer::ViewerImpl::StartVideoPanSlowdown()
{
	if (m_uiVideoPanSlowdownTickID != 0)
	{
		if (m_pVideoFixed != NULL)
			gtk_widget_remove_tick_callback(m_pVideoFixed, m_uiVideoPanSlowdownTickID);
		m_uiVideoPanSlowdownTickID = 0;
	}
	if (m_uiVideoPanSlowdownTimeoutID != 0)
	{
		g_source_remove(m_uiVideoPanSlowdownTimeoutID);
		m_uiVideoPanSlowdownTimeoutID = 0;
	}
	m_bVideoPanSlowdownActive = false;

	if (!m_bKineticScrolling || !CanVideoPan())
	{
		m_iVideoPanSampleCount = 0;
		return;
	}

	gdouble total_dx = 0.;
	gdouble total_dy = 0.;
	gdouble total_dt = 0.;

	for (int i = 0; i < m_iVideoPanSampleCount; ++i)
	{
		total_dx += m_aVideoPanSamples[i].dx;
		total_dy += m_aVideoPanSamples[i].dy;
		total_dt += m_aVideoPanSamples[i].dt;
	}
	m_iVideoPanSampleCount = 0;

	if (total_dt <= 0.001)
		return;

	m_dVideoPanVelX = total_dx / total_dt; // screen pixels per second
	m_dVideoPanVelY = total_dy / total_dt;

	gdouble speed = std::hypot(m_dVideoPanVelX, m_dVideoPanVelY);
	if (speed < 15.0)
		return;

	const gdouble max_speed = 12000.0;
	if (speed > max_speed)
	{
		m_dVideoPanVelX = (m_dVideoPanVelX / speed) * max_speed;
		m_dVideoPanVelY = (m_dVideoPanVelY / speed) * max_speed;
	}

	m_bVideoPanSlowdownActive = true;
	m_iVideoPanSlowdownLastTime = g_get_monotonic_time();

	if (m_pVideoFixed != NULL && gtk_widget_get_mapped(m_pVideoFixed))
	{
		m_uiVideoPanSlowdownTickID = gtk_widget_add_tick_callback(
			m_pVideoFixed, video_pan_slowdown_tick_cb, this, NULL);
		gtk_widget_queue_draw(m_pVideoFixed);
	}
	else
	{
		m_uiVideoPanSlowdownTimeoutID = g_timeout_add(16, video_pan_slowdown_timeout_cb, this);
	}
}

bool Viewer::ViewerImpl::VideoPanSlowdownStep(gint64 frame_time_us)
{
	if (!m_bVideoPanSlowdownActive)
		return false;

	gdouble dt = 0.016;
	if (m_iVideoPanSlowdownLastTime > 0 && frame_time_us > m_iVideoPanSlowdownLastTime)
	{
		dt = (gdouble)(frame_time_us - m_iVideoPanSlowdownLastTime) / 1000000.0;
		if (dt > 0.05) dt = 0.05;
	}
	m_iVideoPanSlowdownLastTime = frame_time_us;

	// Frame-rate independent exponential decay: k = ln(1.04) / 0.035 ≈ 1.1206 s^-1
	gdouble decay = std::exp(-1.1206 * dt);
	m_dVideoPanVelX *= decay;
	m_dVideoPanVelY *= decay;

	gdouble speed = std::hypot(m_dVideoPanVelX, m_dVideoPanVelY);
	gdouble hdistance = dt * m_dVideoPanVelX;
	gdouble vdistance = dt * m_dVideoPanVelY;

	if (speed < 10.0 || (std::abs(hdistance) < 0.05 && std::abs(vdistance) < 0.05))
	{
		return false;
	}

	const gdouble prevPanFX = m_fVideoPanFX;
	const gdouble prevPanFY = m_fVideoPanFY;

	/* Moved through SetVideoPanPixels, which is what a drag uses, and not by
	 * writing the pixel offset: the offset is worked out again from the centre
	 * inside the apply below, so a step that wrote the offset on its own had
	 * that move taken straight back and the picture never went anywhere.  The
	 * velocity is screen velocity, so it is scaled by the zoom to become a
	 * distance in the frame, as the drag's own deltas are. */
	gdouble srcPerPx = (m_dVideoZoom > 0.) ? (1. / m_dVideoZoom) : 1.;
	SetVideoPanPixels(m_dVideoPanX - hdistance * srcPerPx,
		m_dVideoPanY - vdistance * srcPerPx);

	ApplyVideoZoom();

	/* a pan that has run into the edge of the frame stops there and takes that
	 * axis's velocity with it, the way a scroll does when it runs out */
	if (std::abs(m_fVideoPanFX - prevPanFX) < 0.0005)
		m_dVideoPanVelX = 0.;
	if (std::abs(m_fVideoPanFY - prevPanFY) < 0.0005)
		m_dVideoPanVelY = 0.;

	if (std::hypot(m_dVideoPanVelX, m_dVideoPanVelY) < 10.0)
	{
		return false;
	}

	return true;
}

gboolean Viewer::ViewerImpl::video_pan_slowdown_tick_cb(GtkWidget *widget, GdkFrameClock *frame_clock, gpointer data)
{
	(void)widget;
	Viewer::ViewerImpl *pImpl = (Viewer::ViewerImpl *)data;
	if (!pImpl || !pImpl->m_bVideoPanSlowdownActive)
		return G_SOURCE_REMOVE;

	gint64 frame_time = frame_clock ? gdk_frame_clock_get_frame_time(frame_clock) : g_get_monotonic_time();
	gboolean keep_going = pImpl->VideoPanSlowdownStep(frame_time);
	if (!keep_going)
	{
		pImpl->m_uiVideoPanSlowdownTickID = 0;
		pImpl->StopVideoPanSlowdown();
		return G_SOURCE_REMOVE;
	}

	if (pImpl->m_pVideoFixed != NULL)
		gtk_widget_queue_draw(pImpl->m_pVideoFixed);

	return G_SOURCE_CONTINUE;
}

gboolean Viewer::ViewerImpl::video_pan_slowdown_timeout_cb(gpointer data)
{
	Viewer::ViewerImpl *pImpl = (Viewer::ViewerImpl *)data;
	if (!pImpl || !pImpl->m_bVideoPanSlowdownActive)
		return G_SOURCE_REMOVE;

	gint64 now = g_get_monotonic_time();
	gboolean keep_going = pImpl->VideoPanSlowdownStep(now);
	if (!keep_going)
	{
		pImpl->m_uiVideoPanSlowdownTimeoutID = 0;
		pImpl->StopVideoPanSlowdown();
		return G_SOURCE_REMOVE;
	}

	return G_SOURCE_CONTINUE;
}



static void viewer_snapshot_button_clicked_cb(GtkButton *button, gpointer user_data)
{
	(void)button;
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl *)user_data;
	p->Snapshot();
}

static void viewer_video_rw_cb(gpointer user_data)
{
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl *)user_data;
	p->SkipBack();
}

static void viewer_play_button_clicked_cb(gpointer user_data)
{
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl *)user_data;
	if (p->IsVideo())
	{
		p->RefreshAutoHideTimer();
		p->PlayPauseVideo();
	}
}

static void viewer_slideshow_resume_clicked_cb(gpointer user_data)
{
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl *)user_data;
	if (p->m_pViewer)
	{
		p->m_pViewer->SlideShowResume();
	}
}

static void viewer_video_ff_cb(gpointer user_data)
{
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl *)user_data;
	p->SkipForward();
}

static gint64 get_pipeline_frame_duration(GstElement *pipeline)
{
	gint fps_n = 0, fps_d = 0;
	GstPad *pad = NULL;
	g_signal_emit_by_name(pipeline, "get-video-pad", 0, &pad);
	if (pad)
	{
		GstCaps *caps = gst_pad_get_current_caps(pad);
		if (!caps)
		{
			caps = gst_pad_get_allowed_caps(pad);
		}
		if (caps)
		{
			for (guint i = 0; i < gst_caps_get_size(caps); i++)
			{
				GstStructure *s = gst_caps_get_structure(caps, i);
				if (s && gst_structure_get_fraction(s, "framerate", &fps_n, &fps_d))
				{
					if (fps_n > 0 && fps_d > 0)
						break;
				}
			}
			gst_caps_unref(caps);
		}
		gst_object_unref(pad);
	}

	if (fps_n > 0 && fps_d > 0)
	{
		return (GST_SECOND * (gint64)fps_d) / (gint64)fps_n;
	}
	return GST_SECOND / 30; // default to 33.3ms (30fps)
}

static void viewer_frame_step_back_cb(gpointer user_data)
{
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl *)user_data;
	if (!p->IsVideo() || p->m_pPipeline == NULL) return;
	p->SetIsPlaying(false);
	gst_element_set_state(GST_ELEMENT(p->m_pPipeline), GST_STATE_PAUSED);

	gint64 frame_duration = get_pipeline_frame_duration(GST_ELEMENT(p->m_pPipeline));
	gint64 pos = 0;
	if (gst_element_query_position(GST_ELEMENT(p->m_pPipeline), GST_FORMAT_TIME, &pos))
	{
		gint64 target = (pos >= frame_duration) ? (pos - frame_duration) : 0;
		gst_element_seek(GST_ELEMENT(p->m_pPipeline), 1.0,
			GST_FORMAT_TIME,
			GstSeekFlags(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE),
			GST_SEEK_TYPE_SET, target,
			GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);
	}
	p->UpdateTimeline();
	p->RefreshAutoHideTimer();
}

static void viewer_frame_step_fwd_cb(gpointer user_data)
{
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl *)user_data;
	if (!p->IsVideo() || p->m_pPipeline == NULL) return;
	p->SetIsPlaying(false);
	gst_element_set_state(GST_ELEMENT(p->m_pPipeline), GST_STATE_PAUSED);
	GstEvent *ev = gst_event_new_step(GST_FORMAT_BUFFERS, 1, 1.0, TRUE, FALSE);
	if (!gst_element_send_event(GST_ELEMENT(p->m_pPipeline), ev))
	{
		gint64 frame_duration = get_pipeline_frame_duration(GST_ELEMENT(p->m_pPipeline));
		gint64 pos = 0, len = 0;
		if (gst_element_query_position(GST_ELEMENT(p->m_pPipeline), GST_FORMAT_TIME, &pos))
		{
			gst_element_query_duration(GST_ELEMENT(p->m_pPipeline), GST_FORMAT_TIME, &len);
			gint64 target = pos + frame_duration;
			if (len > 0 && target > len) target = len;
			gst_element_seek(GST_ELEMENT(p->m_pPipeline), 1.0,
				GST_FORMAT_TIME,
				static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE),
				GST_SEEK_TYPE_SET, target,
				GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);
		}
	}
	p->UpdateTimeline();
	p->RefreshAutoHideTimer();
}

static void viewer_shortcuts_changed_cb(gpointer user_data)
{
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl *)user_data;
	if (p)
	{
		p->UpdateHUDTooltips();
	}
}

Viewer::ViewerImpl::ViewerImpl(Viewer *pViewer) : 
	
	m_pHBox(NULL),
	m_pTimeElapsedLabel(NULL),
	m_pTimeDurationLabel(NULL),
	m_pControlsBox(NULL),
	m_pRewindBtn(NULL),
	m_pFfBtn(NULL),
	m_pSnapBtn(NULL),
	m_pTimelineRow(NULL),
	m_pVolumeButton(NULL),
	m_pVolumePopover(NULL),
	m_pVolumeScale(NULL),
	m_pVolumeMuteBtn(NULL),
	m_dVolume(1.0),
	m_bMuted(false),
	m_pFullscreenBtn(NULL),
	m_pVideoOptionsBtn(NULL),
	m_pVideoOptionsPopover(NULL),
	m_iVideoZoomIdle(0),
	m_ImageListPtr(new ImageList()),
	m_bIsPlaying(false),
	m_bShuttingDown(false),
	m_pPlayAnimWidget(NULL),
	m_pPlayAnimImage(NULL),
	m_iPlayAnimTickId(0),
	m_iPlayAnimStartTime(0),
	m_ThumbnailCache(100, "viewer thumbnail"),
	m_FilmstripCache(8, "viewer filmstrip"),
	m_dPlaybackSpeed(1.0),
	m_pSpeedButton(NULL),
	m_pSpeedLabel(NULL),
	m_pContextMenuPopover(NULL),
	m_bVideoPlaybackStarted(false),
	m_bFilmstripOverlay(false),
	m_bHideFilmstripFS(true),
	m_bFilmstripHiddenByFS(false),
	m_pFilmstripEdge(NULL),
	m_pFilmstripOverlayContainer(NULL),
	m_iTimeoutFilmstripHide(0),
	m_iTimeoutFilmstripFade(0),
	m_bFadingIn(false),
	m_dFadeOpacity(0.0),
	m_iTimeoutControlsFade(0),
	m_bControlsFadingIn(false),
	m_dControlsFadeOpacity(0.0),
	m_bSeekDragging(FALSE),
	m_dPointerRootX(0.0),
	m_dPointerRootY(0.0),
	m_bPointerPosValid(false),
	m_pBlankCursor(NULL),
	m_SlideShowState(SLIDESHOW_STATE_ADVANCE),
	m_SlideShowPrePauseState(SLIDESHOW_STATE_ADVANCE),
	m_bSlideShowRunning(false),
	m_bSlideShowPaused(false),
	m_pViewerOverlayBar(NULL),
	m_bPointerOverOverlayBar(false),
	m_bControlsVisible(false),
	m_bVideoZoomAnchorCenter(false),
	m_pViewerPrevBtn(NULL),
	m_pViewerNextBtn(NULL),
	m_pViewerSlideshowBtn(NULL),
	m_pViewerVideoSlideshowBtn(NULL),
	m_pImageBlank1(NULL),
	m_pImageBlank2(NULL),
	m_pVideoBlank1(NULL),
	m_pVideoBlank2(NULL),
	m_pViewerZoomOutBtn(NULL),
	m_pViewerZoomFitBtn(NULL),
	m_pViewModeSplitBox(NULL),
	m_pViewModeSplitSep(NULL),
	m_pViewModeMenuBtn(NULL),
	m_pViewModeMenuPopover(NULL),
	m_pViewerZoomInBtn(NULL),
	m_pSepNavigation(NULL),
	m_pSepRotate(NULL),
	m_pSepZoom(NULL),
	m_pViewerRotateCcwBtn(NULL),
	m_pViewerRotateCwBtn(NULL),
	m_pViewerFlipHBtn(NULL),
	m_pViewerFlipVBtn(NULL),
	m_pImageSubmenuBtn(NULL),
	m_pImageSubmenuPopover(NULL),
	m_pViewerFullscreenBtn(NULL),
	m_dGestureLastScale(1.0),
	m_dTwoFingerPanStartHAdj(0.0),
	m_dTwoFingerPanStartVAdj(0.0),
	m_dTwoFingerPanStartVidX(0.0),
	m_dTwoFingerPanStartVidY(0.0),
	m_bVideoPreviewClick(false),
	m_dVideoPreviewClickX(0.0),
	m_dVideoPreviewClickY(0.0),
	m_pCenterPlayBtn(NULL),
	m_PreferencesEventHandlerPtr ( new PreferencesEventHandler(this) ),
	m_ImageListEventHandlerPtr( new ImageListEventHandler(this) ),
	m_spAlive(std::make_shared<bool>(true)),
	m_ThumbnailLoader(this, 2, m_spAlive)
{
	QuiverStockIcons::Load();
	PreferencesPtr prefsPtr = Preferences::GetInstance();
	prefsPtr->AddEventHandler( m_PreferencesEventHandlerPtr );
	ShortcutManager::GetInstance().AddShortcutsChangedCallback(viewer_shortcuts_changed_cb, this);
	
	m_pViewer = pViewer;

	m_pIconView = quiver_icon_view_new();
 	m_pImageView = quiver_image_view_new();
	quiver_image_view_set_enable_transitions(QUIVER_IMAGE_VIEW(m_pImageView), FALSE);
	m_bKineticScrolling = prefsPtr->GetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_KINETIC_SCROLLING, true);
	quiver_image_view_set_smooth_scroll(QUIVER_IMAGE_VIEW(m_pImageView), m_bKineticScrolling);

	gtk_widget_set_size_request(m_pImageView, 100, 100);


	// FIXME:
	//GdkPixmap* bitmap = gdk_pixmap_new(NULL, w, h, 1);
	//gdk_pixbuf_render_threshold_alpha(m_pPixbufPlay, bitmap, 0,0,0,0,w,h, 0x80);

	/* ── Screen-wide CSS ──────────────────────────────────────── */
	GtkCssProvider *cssProvider = gtk_css_provider_new();
	gtk_css_provider_load_from_string(cssProvider,
		".filmstrip-overlay { background-color: transparent; }\n"
		".media-btn { border-radius: 8px; min-width: 2.4em; min-height: 2.4em; padding: 4px; background-image: none; background-color: transparent; border: none; outline: none; }\n"
		".media-btn:hover { background-image: none; background-color: alpha(@theme_bg_color, 0.60); border: none; }\n"
		".media-btn:focus, .media-btn:focus-visible { outline: none; box-shadow: none; }\n"
		"button.media-btn:checked,\n"
		"button.media-btn.speed-active,\n"
		"button.submenu-icon-btn:checked,\n"
		"button.submenu-icon-btn.speed-active,\n"
		".media-btn:checked,\n"
		".media-btn.speed-active,\n"
		".speed-active {\n"
		"  background-image: none;\n"
		"  background-color: @theme_selected_bg_color;\n"
		"  color: @theme_selected_fg_color;\n"
		"}\n"
		"button.media-btn:checked:hover,\n"
		"button.media-btn.speed-active:hover,\n"
		"button.submenu-icon-btn:checked:hover,\n"
		"button.submenu-icon-btn.speed-active:hover,\n"
		".media-btn:checked:hover,\n"
		".media-btn.speed-active:hover,\n"
		".speed-active:hover {\n"
		"  background-image: none;\n"
		"  background-color: alpha(@theme_selected_bg_color, 0.85);\n"
		"  color: @theme_selected_fg_color;\n"
		"}\n"
		".time-label { color: #ffffff; font-size: 12px; font-variant-numeric: tabular-nums; text-shadow: 0 1px 3px rgba(0, 0, 0, 0.9), 0 0 2px rgba(0, 0, 0, 0.7); }\n"
		".play-anim-badge { border-radius: 9999px; background-color: rgba(20, 20, 20, 0.65); color: #ffffff; border: none; outline: none; padding: 0; min-width: 0; min-height: 0; box-shadow: 0 2px 6px rgba(0, 0, 0, 0.25), 0 4px 14px rgba(0, 0, 0, 0.35); }\n"
		".image-load-error { color: rgba(255, 255, 255, 1.0); font-size: 18px; padding: 20px; border-radius: 12px; background-color: alpha(#000, 0.55); }\n"
		".slideshow-resume-pill { color: rgba(255, 255, 255, 1.0); font-size: 14px; padding: 8px 16px; border-radius: 9999px; background-color: alpha(#000, 0.6); font-weight: 500; }\n"
		".slideshow-resume-btn { color: #ffffff; font-size: 14px; font-weight: 600; padding: 4px 12px; border-radius: 9999px; background-color: alpha(#ffffff, 0.18); border: 1px solid alpha(#ffffff, 0.4); }\n"
		".slideshow-resume-btn:hover { background-color: alpha(#ffffff, 0.30); }\n"
		".media-btn-blank { min-width: 2.4em; min-height: 2.4em; padding: 4px; }\n"
		".viewer-overlay-bar { background-color: rgba(30, 30, 30, 0.75); border-radius: 10px; padding: 4px 8px; box-shadow: 0 4px 16px rgba(0, 0, 0, 0.4); }\n"
		".timeline-overlay-bar { background: transparent; border: none; box-shadow: none; padding: 0 4px; }\n"
		".submenu-pill-btn { border-radius: 6px; padding: 3px 8px; font-weight: 500; font-size: 11px; }\n"
		".submenu-icon-btn { border-radius: 6px; min-width: 2.2em; min-height: 2.2em; padding: 4px; }\n"
		".hud-sep { min-width: 1px; min-height: 0.75em; margin: 2px 1px; background-color: alpha(@theme_fg_color, 0.22); }\n"
		/* the divider inside the split control is half the button's height and
		 * centered, so it reads as a separator between the two halves rather
		 * than as the full-height rule that divides the HUD's button groups */
		".view-mode-split > .hud-sep { margin: 0 1px; }\n"
		".view-mode-split { border-radius: 8px; }\n"
		/* the two halves are transparent so the box's own background shows
		 * through and the whole control highlights as a single button.  The
		 * arrow is deliberately not a .media-btn: that class gives every HUD
		 * button a square 2.4em box, which is far too wide for a bare arrow */
		".view-mode-split > .view-mode-fit-btn, .view-mode-split > .view-mode-arrow { border-radius: 0; background-color: transparent; background-image: none; border: none; outline: none; }\n"
		".view-mode-split > .view-mode-fit-btn { border-top-left-radius: 8px; border-bottom-left-radius: 8px; }\n"
		".view-mode-split > .view-mode-arrow { border-top-right-radius: 8px; border-bottom-right-radius: 8px; min-width: 0; min-height: 0; padding: 2px 3px; }\n"
		/* hovering anywhere in the control lights the whole thing up, the
		 * half under the pointer lights up further */
		".view-mode-split.view-mode-split-hovered { background-color: alpha(@theme_bg_color, 0.35); }\n"
		".view-mode-split.view-mode-split-hovered > .view-mode-fit-btn:hover, .view-mode-split.view-mode-split-hovered > .view-mode-arrow:hover { background-color: alpha(@theme_bg_color, 0.60); background-image: none; }\n"
		".view-mode-split.view-mode-split-hovered .hud-sep { background-color: alpha(@theme_fg_color, 0.50); }\n"
		".center-play-btn { border-radius: 9999px; background-color: rgba(20, 20, 20, 0.65); color: #ffffff; border: none; outline: none; padding: 0; min-width: 76px; min-height: 76px; box-shadow: 0 2px 6px rgba(0, 0, 0, 0.25), 0 4px 14px rgba(0, 0, 0, 0.35); }\n"
		".center-play-btn:hover { background-color: rgba(10, 10, 10, 0.85); border: none; }\n"
		".center-play-btn:focus, .center-play-btn:focus-visible { outline: none; border: none; }\n"
		".nav-control-pill { border-radius: 10px; padding: 6px; background-color: alpha(#000, 0.65); box-shadow: 0 4px 16px rgba(0, 0, 0, 0.4); border: 1px solid rgba(255, 255, 255, 0.15); }\n");
	gtk_style_context_add_provider_for_display(gdk_display_get_default(),
		GTK_STYLE_PROVIDER(cssProvider), GTK_STYLE_PROVIDER_PRIORITY_USER);
	g_object_unref(cssProvider);

	m_iCurrentOrientation = 1;
	
	m_SlideShowState = SLIDESHOW_STATE_ADVANCE;
	m_iIdleSetIndex = 0;
	m_iTimeoutScrollbars = 0;
	m_iTimeoutUpdateListID = 0;
	m_iTimeoutSlideshowID = 0;
	m_pSlideShowPausedPill = NULL;
	m_pNavControlPill = NULL;
	m_pNavigationControl = NULL;
	m_bSlideShowMachineAdvancing = false;
	m_iTimeoutClickID = 0;
	m_iTimeoutMouseMotionNotify = 0;
	m_VideoZoomType = VIDEO_ZOOM_SOFTWARE;
	m_dVideoZoom = 1.0;
	m_dVideoZoomFinal = 1.0;
	m_dVideoZoomMin = 1.0;
	m_fVideoPanFX = 0.5;
	m_fVideoPanFY = 0.5;
	m_dVideoViewW = 0.;
	m_dVideoViewH = 0.;
	m_dVideoPanRangeX = 0.;
	m_dVideoPanRangeY = 0.;
	m_dVideoPanX = 0.;
	m_dVideoPanY = 0.;
	m_dVideoLastWidgetW = 0.;
	m_dVideoLastFrameW = 0.;
	m_dVideoLastFrameH = 0.;
	m_dVideoLastFrameZoom = 0.;
	m_dVideoLastWidgetH = 0.;
	m_dVideoLastZc = 1.0;
	m_iVideoZoomTimeoutID = 0;
	m_bVideoZoomCropActive = FALSE;
	m_bVideoZoomInputCropActive = FALSE;
	m_bVideoPanning = FALSE;
	m_iVideoWidth = 0;
	m_iVideoHeight = 0;
	m_iVideoFpsNum = 0;
	m_iVideoFpsDen = 1;
	m_iVideoParN = 1;
	m_iVideoParD = 1;
	m_iVideoSinkW = 0;
	m_iVideoSinkH = 0;
	m_iVideoSinkX = 0;
	m_iVideoSinkY = 0;
	m_dVideoPreviewViewX = 0.0;
	m_dVideoPreviewViewY = 0.0;
	m_dVideoPreviewViewW = 0.0;
	m_dVideoPreviewViewH = 0.0;
	m_dVideoPreviewDispW = 0.0;
	m_dVideoPreviewDispH = 0.0;
	m_bVideoPreviewHasFrame = FALSE;
	m_bVideoPreviewRedrawQueued = FALSE;
	m_iTimeoutPlayProgress = 0;
	m_iSlideShowWaitCount = 0;
	m_DecoderGlitchTracker.m_pszName = "decoder";
	m_SinkGlitchTracker.m_pszName = "sink";

	m_pAdjustmentH = quiver_image_view_get_hadjustment(QUIVER_IMAGE_VIEW(m_pImageView));
	m_pAdjustmentV = quiver_image_view_get_vadjustment(QUIVER_IMAGE_VIEW(m_pImageView));
	/* "changed" rather than "value-changed": the control's box has to follow the
	 * preview not only when it is scrolled but when the preview is *sized*, and
	 * that is what arriving on a new file in "keep zoom and pan" does - the new
	 * thumbnail is laid out at the magnification that was kept, with no value
	 * change to announce it, so a control that only watched the value would stay
	 * hidden exactly when the preview is zoomed in. */
	g_signal_connect(G_OBJECT(m_pAdjustmentH), "changed", G_CALLBACK(viewer_image_adjustment_changed_cb), this);
	g_signal_connect(G_OBJECT(m_pAdjustmentV), "changed", G_CALLBACK(viewer_image_adjustment_changed_cb), this);

	m_pScrollbarV = gtk_scrollbar_new (GTK_ORIENTATION_VERTICAL, m_pAdjustmentV);
	m_pScrollbarH = gtk_scrollbar_new (GTK_ORIENTATION_HORIZONTAL, m_pAdjustmentH);

	

	m_pGrid = gtk_grid_new ();
	m_pOverlay = gtk_overlay_new ();
	gtk_widget_set_hexpand(m_pOverlay, TRUE);
	gtk_widget_set_vexpand(m_pOverlay, TRUE);
	m_pStack = gtk_stack_new ();
	gtk_widget_set_hexpand(m_pStack, TRUE);
	gtk_widget_set_vexpand(m_pStack, TRUE);

	/* Play / pause animation overlay badge */
	m_pPlayAnimWidget = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_add_css_class(m_pPlayAnimWidget, "play-anim-badge");
	gtk_widget_add_css_class(m_pPlayAnimWidget, "circular");
	gtk_widget_set_halign(m_pPlayAnimWidget, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(m_pPlayAnimWidget, GTK_ALIGN_CENTER);
	gtk_widget_set_can_target(m_pPlayAnimWidget, FALSE);
	gtk_widget_set_size_request(m_pPlayAnimWidget, 92, 92);
	m_pPlayAnimImage = gtk_image_new_from_icon_name("media-playback-start");
	gtk_widget_set_halign(m_pPlayAnimImage, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(m_pPlayAnimImage, GTK_ALIGN_CENTER);
	gtk_widget_set_hexpand(m_pPlayAnimImage, TRUE);
	gtk_widget_set_vexpand(m_pPlayAnimImage, TRUE);
	gtk_widget_set_can_target(m_pPlayAnimImage, FALSE);
	gtk_box_append(GTK_BOX(m_pPlayAnimWidget), m_pPlayAnimImage);
	gtk_widget_set_visible(m_pPlayAnimWidget, FALSE);
	gtk_widget_set_opacity(m_pPlayAnimWidget, 0.0);
	gtk_overlay_add_overlay(GTK_OVERLAY(m_pOverlay), m_pPlayAnimWidget);
	gtk_overlay_set_measure_overlay(GTK_OVERLAY(m_pOverlay), m_pPlayAnimWidget, FALSE);

	/* Persistent Center Play Button (for video previews / paused videos) */
	m_pCenterPlayBtn = gtk_button_new();
	gtk_button_set_has_frame(GTK_BUTTON(m_pCenterPlayBtn), FALSE);
	gtk_widget_add_css_class(m_pCenterPlayBtn, "center-play-btn");
	gtk_widget_add_css_class(m_pCenterPlayBtn, "circular");
	gtk_widget_set_halign(m_pCenterPlayBtn, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(m_pCenterPlayBtn, GTK_ALIGN_CENTER);
	gtk_widget_set_size_request(m_pCenterPlayBtn, 76, 76);
	GtkWidget *center_play_icon = gtk_image_new_from_icon_name("media-playback-start-symbolic");
	gtk_image_set_pixel_size(GTK_IMAGE(center_play_icon), 38);
	gtk_widget_set_can_target(center_play_icon, FALSE);
	gtk_button_set_child(GTK_BUTTON(m_pCenterPlayBtn), center_play_icon);
	gtk_widget_set_tooltip_text(m_pCenterPlayBtn, "Play Video (Space / Click)");
	g_signal_connect_swapped(G_OBJECT(m_pCenterPlayBtn), "clicked", G_CALLBACK(viewer_play_button_clicked_cb), this);
	{
		GtkEventController *scroll = gtk_event_controller_scroll_new(
			(GtkEventControllerScrollFlags)GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES);
		g_signal_connect(scroll, "scroll", G_CALLBACK(viewer_scrollwheel_event), this);
		gtk_widget_add_controller(m_pCenterPlayBtn, scroll);
	}
	{
		GtkEventController *motion = gtk_event_controller_motion_new();
		g_signal_connect(motion, "motion", G_CALLBACK(controls_show_on_event_cb), this);
		gtk_widget_add_controller(m_pCenterPlayBtn, motion);
	}
	gtk_widget_set_visible(m_pCenterPlayBtn, FALSE);
	gtk_overlay_add_overlay(GTK_OVERLAY(m_pOverlay), m_pCenterPlayBtn);
	gtk_overlay_set_measure_overlay(GTK_OVERLAY(m_pOverlay), m_pCenterPlayBtn, FALSE);

	/* ── Unified Viewer Control Overlays (floating HUD for stills and videos) ── */
	/* ── Full-width Timeline row (floats at the bottom for videos) ────────────── */
	m_pTimelineRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_widget_add_css_class(m_pTimelineRow, "timeline-overlay-bar");
	gtk_widget_set_halign(m_pTimelineRow, GTK_ALIGN_FILL);
	gtk_widget_set_hexpand(m_pTimelineRow, TRUE);
	gtk_widget_set_valign(m_pTimelineRow, GTK_ALIGN_END);
	gtk_widget_set_margin_start(m_pTimelineRow, 24);
	gtk_widget_set_margin_end(m_pTimelineRow, 24);
	gtk_widget_set_margin_bottom(m_pTimelineRow, 12);

	m_pTimeElapsedLabel = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(m_pTimeElapsedLabel), "<b>0:00</b>");
	gtk_widget_add_css_class(m_pTimeElapsedLabel, "time-label");
	gtk_box_append(GTK_BOX(m_pTimelineRow), m_pTimeElapsedLabel);

	GtkWidget* scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 1.0, 0.001);
	m_pPlayProgress = scale;
	m_pTimeline = scale;
	gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);
	gtk_widget_set_focusable(scale, FALSE);
	gtk_widget_set_hexpand(scale, TRUE);
	{
		GtkEventController *motion = gtk_event_controller_motion_new();
		g_signal_connect(motion, "motion", G_CALLBACK(controls_show_on_event_cb), this);
		gtk_widget_add_controller(scale, motion);
	}
	{
		GtkGesture *click = gtk_gesture_click_new();
		g_signal_connect(click, "pressed", G_CALLBACK(viewer_scale_button_press_cb), this);
		g_signal_connect(click, "released", G_CALLBACK(viewer_scale_button_release_cb), this);
		gtk_widget_add_controller(scale, GTK_EVENT_CONTROLLER(click));
	}
	m_iPlayProgressChangeHandler = g_signal_connect(G_OBJECT(m_pPlayProgress), "change-value",
		G_CALLBACK(viewer_scale_change_value_cb), this);
	gtk_box_append(GTK_BOX(m_pTimelineRow), scale);

	m_pTimeDurationLabel = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(m_pTimeDurationLabel), "<b>0:00</b>");
	gtk_widget_add_css_class(m_pTimeDurationLabel, "time-label");
	gtk_box_append(GTK_BOX(m_pTimelineRow), m_pTimeDurationLabel);

	{
		GtkEventController *scroll = gtk_event_controller_scroll_new(
			(GtkEventControllerScrollFlags)GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES);
		g_signal_connect(scroll, "scroll", G_CALLBACK(viewer_scrollwheel_event), this);
		gtk_widget_add_controller(m_pTimelineRow, scroll);
	}
	{
		GtkEventController *motion = gtk_event_controller_motion_new();
		g_signal_connect(motion, "enter", G_CALLBACK(viewer_overlay_bar_enter_cb), this);
		g_signal_connect(motion, "leave", G_CALLBACK(viewer_overlay_bar_leave_cb), this);
		g_signal_connect(motion, "motion", G_CALLBACK(controls_show_on_event_cb), this);
		gtk_widget_add_controller(m_pTimelineRow, motion);
	}

	gtk_overlay_add_overlay(GTK_OVERLAY(m_pOverlay), m_pTimelineRow);
	gtk_overlay_set_measure_overlay(GTK_OVERLAY(m_pOverlay), m_pTimelineRow, FALSE);
	set_control_visible(m_pTimelineRow, false);

	/* ── Unified HUD controls toolbar (15-slot consistent layout) ────────────── */
	m_pViewerOverlayBar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
	gtk_widget_add_css_class(m_pViewerOverlayBar, "osd");
	gtk_widget_add_css_class(m_pViewerOverlayBar, "viewer-overlay-bar");
	gtk_widget_set_halign(m_pViewerOverlayBar, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(m_pViewerOverlayBar, GTK_ALIGN_END);
	gtk_widget_set_margin_bottom(m_pViewerOverlayBar, 50);

	m_pControlsBox = m_pViewerOverlayBar;

	auto make_overlay_btn = [this](const char *icon, const char *tooltip, GCallback cb) -> GtkWidget* {
		GtkWidget *btn = gtk_button_new_from_icon_name(icon);
		gtk_button_set_has_frame(GTK_BUTTON(btn), FALSE);
		gtk_widget_add_css_class(btn, "media-btn");
		gtk_widget_set_focus_on_click(btn, FALSE);
		gtk_widget_set_focusable(btn, FALSE);
		if (tooltip) gtk_widget_set_tooltip_text(btn, tooltip);
		if (cb) g_signal_connect_swapped(G_OBJECT(btn), "clicked", cb, this);
		GtkEventController *motion = gtk_event_controller_motion_new();
		g_signal_connect(motion, "motion", G_CALLBACK(controls_show_on_event_cb), this);
		gtk_widget_add_controller(btn, motion);
		return btn;
	};

	auto make_hud_sep = [this]() -> GtkWidget* {
		GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
		gtk_widget_add_css_class(sep, "hud-sep");
		gtk_widget_set_valign(sep, GTK_ALIGN_FILL);
		GtkEventController *motion = gtk_event_controller_motion_new();
		g_signal_connect(motion, "motion", G_CALLBACK(controls_show_on_event_cb), this);
		gtk_widget_add_controller(sep, motion);
		return sep;
	};

	auto make_blank_slot = []() -> GtkWidget* {
		GtkWidget *b = gtk_button_new();
		gtk_button_set_has_frame(GTK_BUTTON(b), FALSE);
		gtk_widget_add_css_class(b, "media-btn");
		gtk_widget_add_css_class(b, "media-btn-blank");
		gtk_widget_set_opacity(b, 0.0);
		gtk_widget_set_can_target(b, FALSE);
		gtk_widget_set_focusable(b, FALSE);
		return b;
	};

	// Slot 0: < (Previous)
	m_pViewerPrevBtn = make_overlay_btn("go-previous-symbolic", NULL, G_CALLBACK(viewer_overlay_prev_cb));
	gtk_box_append(GTK_BOX(m_pControlsBox), m_pViewerPrevBtn);

	// Slot 1: > (Next)
	m_pViewerNextBtn = make_overlay_btn("go-next-symbolic", NULL, G_CALLBACK(viewer_overlay_next_cb));
	gtk_box_append(GTK_BOX(m_pControlsBox), m_pViewerNextBtn);

	/* separates the navigation pair from the playback/transform cluster */
	m_pSepNavigation = make_hud_sep();
	gtk_box_append(GTK_BOX(m_pControlsBox), m_pSepNavigation);

	// Slot 2: Blank 1 (image) / Play (video)
	m_pImageBlank1 = make_blank_slot();
	gtk_box_append(GTK_BOX(m_pControlsBox), m_pImageBlank1);

	m_pPlayImage = gtk_image_new_from_icon_name("media-playback-start-symbolic");
	m_pPlayButton = gtk_button_new();
	gtk_button_set_has_frame(GTK_BUTTON(m_pPlayButton), FALSE);
	gtk_button_set_child(GTK_BUTTON(m_pPlayButton), m_pPlayImage);
	gtk_widget_add_css_class(m_pPlayButton, "media-btn");
	gtk_widget_set_focus_on_click(m_pPlayButton, FALSE);
	gtk_widget_set_focusable(m_pPlayButton, FALSE);
	gtk_widget_set_tooltip_text(m_pPlayButton, "Play / Pause (Space)");
	g_signal_connect_swapped(G_OBJECT(m_pPlayButton), "clicked", G_CALLBACK(viewer_play_button_clicked_cb), this);
	{
		GtkEventController *motion = gtk_event_controller_motion_new();
		g_signal_connect(motion, "motion", G_CALLBACK(controls_show_on_event_cb), this);
		gtk_widget_add_controller(m_pPlayButton, motion);
	}
	gtk_box_append(GTK_BOX(m_pControlsBox), m_pPlayButton);

	// Slot 3: Rotate Left (CCW) (image) / Skip Backward 10s (video)
	m_pViewerRotateCcwBtn = make_overlay_btn("object-rotate-left-symbolic", "Rotate Counter-Clockwise", G_CALLBACK(viewer_overlay_rotate_ccw_cb));
	gtk_box_append(GTK_BOX(m_pControlsBox), m_pViewerRotateCcwBtn);
	m_pRewindBtn = make_overlay_btn("skip-backwards-10", "Skip Backwards 10s (Left / Shift+Left)", G_CALLBACK(viewer_video_rw_cb));
	gtk_box_append(GTK_BOX(m_pControlsBox), m_pRewindBtn);

	// Slot 4: Rotate Right (CW) (image) / Skip Forward 10s (video)
	m_pViewerRotateCwBtn = make_overlay_btn("object-rotate-right-symbolic", "Rotate Clockwise", G_CALLBACK(viewer_overlay_rotate_cw_cb));
	gtk_box_append(GTK_BOX(m_pControlsBox), m_pViewerRotateCwBtn);
	m_pFfBtn = make_overlay_btn("skip-forward-10", "Skip Forward 10s (Right / Shift+Right)", G_CALLBACK(viewer_video_ff_cb));
	gtk_box_append(GTK_BOX(m_pControlsBox), m_pFfBtn);

	/* in image mode this lands right after rotate clockwise, in video mode
	 * after the skip-forward button - the two share the slot */
	m_pSepRotate = make_hud_sep();
	gtk_box_append(GTK_BOX(m_pControlsBox), m_pSepRotate);

	// Slot 5: Zoom Out (-)
	m_pViewerZoomOutBtn = make_overlay_btn("zoom-out-symbolic", "Zoom Out (-)", G_CALLBACK(viewer_overlay_zoom_out_cb));
	gtk_box_append(GTK_BOX(m_pControlsBox), m_pViewerZoomOutBtn);

	/* Zoom Fit, as a split control: the icon half resets the view mode, the
	 * arrow half opens the list of view modes (Nautilus style).  It is built
	 * here but appended after Zoom In, so the two zoom buttons sit together and
	 * this split control ends the zoom cluster. */
	m_pViewerZoomFitBtn = make_overlay_btn("zoom-fit-best-symbolic", "Zoom to Fit Window (Stretched)", G_CALLBACK(viewer_overlay_zoom_fit_cb));

	/* The arrow is a plain icon button with the list parented to it rather
	 * than a GtkMenuButton: a menu button draws its own arrow next to
	 * whatever icon it is given and keeps a 36x34 minimum that no CSS can
	 * override, which left the arrow as wide as a full HUD button. */
	m_pViewModeMenuBtn = gtk_button_new_from_icon_name("pan-up-symbolic");
	gtk_button_set_has_frame(GTK_BUTTON(m_pViewModeMenuBtn), FALSE);
	gtk_widget_add_css_class(m_pViewModeMenuBtn, "view-mode-arrow");
	{
		GtkWidget *icon = gtk_button_get_child(GTK_BUTTON(m_pViewModeMenuBtn));
		if (icon != NULL && GTK_IS_IMAGE(icon))
			gtk_image_set_pixel_size(GTK_IMAGE(icon), 12);
	}
	gtk_widget_set_focus_on_click(m_pViewModeMenuBtn, FALSE);
	gtk_widget_set_focusable(m_pViewModeMenuBtn, FALSE);
	gtk_widget_set_tooltip_text(m_pViewModeMenuBtn, "View Mode");
	g_signal_connect_swapped(m_pViewModeMenuBtn, "clicked", G_CALLBACK(viewer_view_mode_arrow_cb), this);
	{
		GtkEventController *motion = gtk_event_controller_motion_new();
		g_signal_connect(motion, "motion", G_CALLBACK(controls_show_on_event_cb), this);
		gtk_widget_add_controller(m_pViewModeMenuBtn, motion);
	}

	m_pViewModeSplitBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_add_css_class(m_pViewModeSplitBox, "view-mode-split");
	gtk_widget_add_css_class(m_pViewerZoomFitBtn, "view-mode-fit-btn");
	gtk_box_append(GTK_BOX(m_pViewModeSplitBox), m_pViewerZoomFitBtn);
	m_pViewModeSplitSep = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
	gtk_widget_add_css_class(m_pViewModeSplitSep, "hud-sep");
	/* centred, so it is half the button's height instead of the full-height
	 * rule the HUD's button groups are divided with */
	gtk_widget_set_valign(m_pViewModeSplitSep, GTK_ALIGN_CENTER);
	gtk_box_append(GTK_BOX(m_pViewModeSplitBox), m_pViewModeSplitSep);
	viewer_view_mode_split_size_sep(m_pViewerZoomFitBtn, m_pViewModeSplitSep);
	gtk_box_append(GTK_BOX(m_pViewModeSplitBox), m_pViewModeMenuBtn);
	m_pViewModeMenuPopover = viewer_view_mode_build_popover();
	/* The popover hangs off the split box and not off the arrow button: a plain
	 * button does not unparent the children it was never told about, so a
	 * popover left on one is finalized with its parent already gone, while a box
	 * unparents its children as it goes.  A popover aims at the widget it is a
	 * child of, so the arrow's place is set by hand. */
	gtk_widget_set_parent(GTK_WIDGET(m_pViewModeMenuPopover), m_pViewModeSplitBox);
	g_signal_connect(m_pViewModeMenuPopover, "show", G_CALLBACK(viewer_view_mode_create_popup_cb), this);
	{
		/* one highlight for the whole split control: the box lights up while
		 * the pointer is anywhere inside it (including the separator) and the
		 * child under the pointer picks up its own stronger hover from CSS */
		GtkEventController *motion = gtk_event_controller_motion_new();
		g_signal_connect(motion, "enter", G_CALLBACK(viewer_split_button_enter_cb), m_pViewModeSplitBox);
		g_signal_connect(motion, "leave", G_CALLBACK(viewer_split_button_leave_cb), m_pViewModeSplitBox);
		gtk_widget_add_controller(m_pViewModeSplitBox, motion);
	}
	// Slot 7: Zoom In (+) - next to Zoom Out, before the split control
	m_pViewerZoomInBtn = make_overlay_btn("zoom-in-symbolic", "Zoom In (+)", G_CALLBACK(viewer_overlay_zoom_in_cb));
	gtk_box_append(GTK_BOX(m_pControlsBox), m_pViewerZoomInBtn);

	gtk_box_append(GTK_BOX(m_pControlsBox), m_pViewModeSplitBox);

	/* closes the zoom cluster, everything after it is playback/presenting */
	m_pSepZoom = make_hud_sep();
	gtk_box_append(GTK_BOX(m_pControlsBox), m_pSepZoom);

	// Slot 8: Blank 2 (image) / Volume (video)
	m_pImageBlank2 = make_blank_slot();
	gtk_box_append(GTK_BOX(m_pControlsBox), m_pImageBlank2);

	m_pVolumeButton = gtk_menu_button_new();
	gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(m_pVolumeButton), "audio-volume-high-symbolic");
	gtk_menu_button_set_direction(GTK_MENU_BUTTON(m_pVolumeButton), GTK_ARROW_UP);
	gtk_menu_button_set_has_frame(GTK_MENU_BUTTON(m_pVolumeButton), FALSE);
	gtk_widget_add_css_class(m_pVolumeButton, "media-btn");
	gtk_widget_set_tooltip_text(m_pVolumeButton, "Volume");
	{
		GtkEventController *motion = gtk_event_controller_motion_new();
		g_signal_connect(motion, "motion", G_CALLBACK(controls_show_on_event_cb), this);
		gtk_widget_add_controller(m_pVolumeButton, motion);
	}
	{
		GtkGesture *middle_click = gtk_gesture_click_new();
		gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(middle_click), GDK_BUTTON_MIDDLE);
		g_signal_connect(middle_click, "pressed", G_CALLBACK(viewer_volume_button_middle_click_cb), this);
		gtk_widget_add_controller(m_pVolumeButton, GTK_EVENT_CONTROLLER(middle_click));
	}
	m_pVolumePopover = gtk_popover_new();
	gtk_popover_set_position(GTK_POPOVER(m_pVolumePopover), GTK_POS_TOP);
	gtk_popover_set_autohide(GTK_POPOVER(m_pVolumePopover), TRUE);
	GtkWidget* volBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	gtk_widget_set_margin_start(volBox, 8);
	gtk_widget_set_margin_end(volBox, 8);
	gtk_widget_set_margin_top(volBox, 8);
	gtk_widget_set_margin_bottom(volBox, 8);
	GtkWidget* volLabel = gtk_label_new("Volume");
	gtk_box_append(GTK_BOX(volBox), volLabel);
	m_pVolumeScale = gtk_scale_new_with_range(GTK_ORIENTATION_VERTICAL, 0.0, 1.0, 0.05);
	gtk_range_set_inverted(GTK_RANGE(m_pVolumeScale), TRUE);
	gtk_widget_set_size_request(m_pVolumeScale, -1, 120);
	gtk_range_set_value(GTK_RANGE(m_pVolumeScale), m_dVolume);
	g_signal_connect(G_OBJECT(m_pVolumeScale), "value-changed", G_CALLBACK(viewer_volume_value_changed), this);
	gtk_box_append(GTK_BOX(volBox), m_pVolumeScale);
	m_pVolumeMuteBtn = gtk_button_new_from_icon_name("audio-volume-high-symbolic");
	gtk_widget_add_css_class(m_pVolumeMuteBtn, "media-btn");
	gtk_widget_set_tooltip_text(m_pVolumeMuteBtn, "Mute (M)");
	g_signal_connect_swapped(m_pVolumeMuteBtn, "clicked", G_CALLBACK(viewer_volume_mute_cb), this);
	gtk_box_append(GTK_BOX(volBox), m_pVolumeMuteBtn);
	gtk_popover_set_child(GTK_POPOVER(m_pVolumePopover), volBox);
	gtk_menu_button_set_popover(GTK_MENU_BUTTON(m_pVolumeButton), m_pVolumePopover);
	UpdateVolumeUI();
	gtk_box_append(GTK_BOX(m_pControlsBox), m_pVolumeButton);

	// Slot 9: Submenu (image) / Submenu (video)
	m_pImageSubmenuBtn = gtk_menu_button_new();
	gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(m_pImageSubmenuBtn), "view-more-symbolic");
	gtk_menu_button_set_direction(GTK_MENU_BUTTON(m_pImageSubmenuBtn), GTK_ARROW_UP);
	gtk_menu_button_set_has_frame(GTK_MENU_BUTTON(m_pImageSubmenuBtn), FALSE);
	gtk_widget_add_css_class(m_pImageSubmenuBtn, "media-btn");
	gtk_widget_set_tooltip_text(m_pImageSubmenuBtn, "More Options");
	gtk_menu_button_set_create_popup_func(GTK_MENU_BUTTON(m_pImageSubmenuBtn), viewer_image_submenu_create_popup_cb, this, NULL);
	{
		GtkEventController *motion = gtk_event_controller_motion_new();
		g_signal_connect(motion, "motion", G_CALLBACK(controls_show_on_event_cb), this);
		gtk_widget_add_controller(m_pImageSubmenuBtn, motion);
	}
	gtk_box_append(GTK_BOX(m_pControlsBox), m_pImageSubmenuBtn);

	m_pVideoOptionsBtn = gtk_menu_button_new();
	gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(m_pVideoOptionsBtn), "view-more-symbolic");
	gtk_menu_button_set_direction(GTK_MENU_BUTTON(m_pVideoOptionsBtn), GTK_ARROW_UP);
	gtk_menu_button_set_has_frame(GTK_MENU_BUTTON(m_pVideoOptionsBtn), FALSE);
	gtk_widget_add_css_class(m_pVideoOptionsBtn, "media-btn");
	gtk_widget_set_tooltip_text(m_pVideoOptionsBtn, "Video Options");
	gtk_menu_button_set_create_popup_func(GTK_MENU_BUTTON(m_pVideoOptionsBtn), viewer_video_options_create_popup_cb, this, NULL);
	{
		GtkEventController *motion = gtk_event_controller_motion_new();
		g_signal_connect(motion, "motion", G_CALLBACK(controls_show_on_event_cb), this);
		gtk_widget_add_controller(m_pVideoOptionsBtn, motion);
	}
	gtk_box_append(GTK_BOX(m_pControlsBox), m_pVideoOptionsBtn);

	// Slot 10: Fullscreen
	m_pViewerFullscreenBtn = make_overlay_btn(get_fullscreen_icon_name(false), "Toggle Fullscreen (F11 / Middle Click)", G_CALLBACK(viewer_overlay_fullscreen_cb));
	m_pFullscreenBtn = m_pViewerFullscreenBtn;
	gtk_box_append(GTK_BOX(m_pControlsBox), m_pViewerFullscreenBtn);

	UpdateHUDTooltips();
	UpdateHUDPosition();

	m_pMediaControls = m_pViewerOverlayBar;
	m_pTransportRow = NULL;

	{
		GtkEventController *scroll = gtk_event_controller_scroll_new(
			(GtkEventControllerScrollFlags)GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES);
		g_signal_connect(scroll, "scroll", G_CALLBACK(viewer_scrollwheel_event), this);
		gtk_widget_add_controller(m_pViewerOverlayBar, scroll);
	}
	{
		GtkEventController *motion = gtk_event_controller_motion_new();
		g_signal_connect(motion, "enter", G_CALLBACK(viewer_overlay_bar_enter_cb), this);
		g_signal_connect(motion, "leave", G_CALLBACK(viewer_overlay_bar_leave_cb), this);
		g_signal_connect(motion, "motion", G_CALLBACK(controls_show_on_event_cb), this);
		gtk_widget_add_controller(m_pViewerOverlayBar, motion);
	}

	gtk_overlay_add_overlay(GTK_OVERLAY(m_pOverlay), m_pViewerOverlayBar);
	gtk_overlay_set_measure_overlay(GTK_OVERLAY(m_pOverlay), m_pViewerOverlayBar, FALSE);
	set_control_visible(m_pViewerOverlayBar, false);
	gtk_widget_set_opacity(m_pViewerOverlayBar, 0.0);

	// Hide video-specific controls initially (since default is stills)
	set_control_visible(m_pTimelineRow, false);
	set_control_visible(m_pPlayButton, false);
	set_control_visible(m_pRewindBtn, false);
	set_control_visible(m_pFfBtn, false);
	set_control_visible(m_pVolumeButton, false);
	set_control_visible(m_pVideoOptionsBtn, false);

	// Show stills controls initially
	set_control_visible(m_pImageBlank1, true);
	set_control_visible(m_pViewerRotateCcwBtn, true);
	set_control_visible(m_pViewerRotateCwBtn, true);
	set_control_visible(m_pImageBlank2, true);
	set_control_visible(m_pImageSubmenuBtn, true);
	set_control_visible(m_pViewerFullscreenBtn, true);

	// the image/video stack is the overlay's single main widget
	gtk_widget_set_hexpand(m_pImageView, TRUE);
	gtk_widget_set_vexpand(m_pImageView, TRUE);
	gtk_stack_add_named(GTK_STACK(m_pStack), m_pImageView, "image");
	gtk_overlay_set_child(GTK_OVERLAY(m_pOverlay), m_pStack);

	/* "failed to load" indicator, centered above the image.  It is only
	 * shown when the image loader reports a failed load for the current
	 * image, so the user sees that navigation actually happened instead of
	 * staring at the previous image's content. */
	m_pImageErrorLabel = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(m_pImageErrorLabel),
		"<b>Failed to load</b>\n<small>This file could not be decoded.</small>");
	gtk_widget_set_halign(m_pImageErrorLabel, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(m_pImageErrorLabel, GTK_ALIGN_CENTER);
	gtk_widget_set_hexpand(m_pImageErrorLabel, TRUE);
	gtk_widget_set_vexpand(m_pImageErrorLabel, TRUE);
	gtk_widget_add_css_class(m_pImageErrorLabel, "image-load-error");
	gtk_widget_set_can_target(m_pImageErrorLabel, FALSE);
	gtk_overlay_add_overlay(GTK_OVERLAY(m_pOverlay), m_pImageErrorLabel);
	gtk_overlay_set_measure_overlay(GTK_OVERLAY(m_pOverlay), m_pImageErrorLabel, FALSE);
	gtk_widget_set_visible(m_pImageErrorLabel, FALSE);

	/* "Slideshow paused. [Resume]" pill, shown while a slideshow is paused
	 * (on an image or a video) so the user can see the paused state and
	 * resume with a click. */
	m_pSlideShowPausedPill = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_widget_add_css_class(m_pSlideShowPausedPill, "slideshow-resume-pill");
	GtkWidget* pSlideShowPausedLabel = gtk_label_new("Slideshow paused.");
	GtkWidget* pSlideShowResumeBtn = gtk_button_new_with_label("Resume");
	gtk_widget_add_css_class(pSlideShowResumeBtn, "slideshow-resume-btn");
	gtk_box_append(GTK_BOX(m_pSlideShowPausedPill), pSlideShowPausedLabel);
	gtk_box_append(GTK_BOX(m_pSlideShowPausedPill), pSlideShowResumeBtn);
	gtk_widget_set_halign(m_pSlideShowPausedPill, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(m_pSlideShowPausedPill, GTK_ALIGN_START);
	gtk_widget_set_margin_top(m_pSlideShowPausedPill, 16);
	gtk_widget_set_can_target(m_pSlideShowPausedPill, TRUE);
	g_signal_connect_swapped(G_OBJECT(pSlideShowResumeBtn), "clicked",
		G_CALLBACK(viewer_slideshow_resume_clicked_cb), this);
	gtk_overlay_add_overlay(GTK_OVERLAY(m_pOverlay), m_pSlideShowPausedPill);
	gtk_overlay_set_measure_overlay(GTK_OVERLAY(m_pOverlay), m_pSlideShowPausedPill, FALSE);
	gtk_widget_set_visible(m_pSlideShowPausedPill, FALSE);

	gtk_grid_attach (GTK_GRID (m_pGrid), m_pOverlay, 0, 0, 1, 1);

	//gtk_widget_set_hexpand(m_pImageView, TRUE);
	gtk_widget_set_vexpand(m_pScrollbarV, TRUE);
	gtk_grid_attach (GTK_GRID (m_pGrid), m_pScrollbarV, 1, 0, 1, 1);
			  
	gtk_widget_set_hexpand(m_pScrollbarH, TRUE);
	gtk_grid_attach (GTK_GRID (m_pGrid), m_pScrollbarH, 0, 1, 1, 1);
			  
	//gtk_grid_attach (GTK_GRID (m_pGrid), m_pNavigationBox, 1, 1, 1, 1);

//	GTK_WIDGET_SET_FLAGS(m_pGrid,GTK_CAN_FOCUS);
	m_pHBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL,0);
	m_pVBox = gtk_box_new(GTK_ORIENTATION_VERTICAL,0);

	/* the window may destroy this tree before the viewer object itself is
	 * released: clear the cached widget pointers then, see
	 * viewer_widget_tree_destroyed_cb() */
	g_signal_connect(m_pHBox, "destroy", G_CALLBACK(viewer_widget_tree_destroyed_cb), this);

	// let the image/viewer area expand to fill the top-level widget, so the
	// quiver-image-view (m_pImageView) is not confined to its natural size.
	gtk_widget_set_hexpand(m_pGrid, TRUE);
	gtk_widget_set_vexpand(m_pGrid, TRUE);
	gtk_widget_set_hexpand(m_pVBox, TRUE);
	gtk_widget_set_vexpand(m_pVBox, TRUE);
	gtk_widget_set_hexpand(m_pHBox, TRUE);
	gtk_widget_set_vexpand(m_pHBox, TRUE);

	gtk_box_append (GTK_BOX (m_pVBox), m_pGrid);
	gtk_box_append (GTK_BOX (m_pHBox), m_pVBox);

	AddFilmstrip();

	string strBGColorImg   = prefsPtr->GetString(QUIVER_PREFS_APP,QUIVER_PREFS_APP_BG_IMAGEVIEW,"#000");
	string strBGColorThumb = prefsPtr->GetString(QUIVER_PREFS_APP,QUIVER_PREFS_APP_BG_ICONVIEW, "#444");

	if (!prefsPtr->GetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_USE_THEME_COLOR,true))
	{
		if (!strBGColorImg.empty())
		{
			GdkRGBA color;
			gdk_rgba_parse(&color, strBGColorImg.c_str());
			set_widget_bg_color(m_pImageView, &color);
		}
		
		if (!strBGColorThumb.empty() && !m_bFilmstripOverlay)
		{
			GdkRGBA color;
			gdk_rgba_parse(&color, strBGColorThumb.c_str());
			set_widget_bg_color(m_pIconView, &color);
		}
	}

	m_iSlideShowDuration = prefsPtr->GetInteger(QUIVER_PREFS_SLIDESHOW,QUIVER_PREFS_SLIDESHOW_DURATION, 3000);
	m_bSlideShowLoop = prefsPtr->GetBoolean(QUIVER_PREFS_SLIDESHOW,QUIVER_PREFS_SLIDESHOW_LOOP,true);
	m_bVideoLoop = false;

	m_bMaximizeViewableArea = prefsPtr->GetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_ROTATE_FOR_BEST_FIT, false);
	m_bHideFilmstripFS = prefsPtr->GetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_FILMSTRIP_HIDE_FS, true);
	
	m_bIsPlaying = false;

	quiver_image_view_set_magnification_mode(QUIVER_IMAGE_VIEW(m_pImageView),QUIVER_IMAGE_VIEW_MAGNIFICATION_MODE_SMOOTH);
	
	QuiverImageViewMode view_mode =
		(QuiverImageViewMode)prefsPtr->GetInteger(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_DEFAULT_VIEW_MODE, QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW_STRETCH);

	quiver_image_view_set_view_mode(QUIVER_IMAGE_VIEW(m_pImageView), view_mode);
	/* the mode is the viewer's, so a video is shown in the mode the stills are
	 * and neither of the two starts out in a different one */
	m_eVideoViewMode = view_mode;

	/* GTK4 drag-and-drop source via GtkDragSource controller */
	if (!m_pDragSource) {
		m_pDragSource = gtk_drag_source_new();
		gtk_drag_source_set_actions(m_pDragSource, (GdkDragAction)(GDK_ACTION_MOVE | GDK_ACTION_COPY));
		gtk_widget_add_controller(m_pImageView, GTK_EVENT_CONTROLLER(m_pDragSource));
		g_signal_connect(m_pDragSource, "prepare", G_CALLBACK(signal_drag_source_prepare), this);
		g_signal_connect(m_pDragSource, "drag-begin", G_CALLBACK(signal_drag_begin), this);
		g_signal_connect(m_pDragSource, "drag-end", G_CALLBACK(signal_drag_end), this);
	}

	/* GTK4 drop target controller on the image view */
	if (!m_pDropTarget) {
		m_pDropTarget = gtk_drop_target_new(G_TYPE_STRING, GDK_ACTION_COPY);
		gtk_widget_add_controller(m_pImageView, GTK_EVENT_CONTROLLER(m_pDropTarget));
	}


	quiver_icon_view_set_n_items_func(QUIVER_ICON_VIEW(m_pIconView),(QuiverIconViewGetNItemsFunc)n_cells_callback,this,NULL);
#if HAVE_GDK_PIXBUF
	quiver_icon_view_set_thumbnail_pixbuf_func(QUIVER_ICON_VIEW(m_pIconView),(QuiverIconViewGetThumbnailPixbufFunc)thumbnail_pixbuf_callback,this,NULL);
	quiver_icon_view_set_icon_pixbuf_func(QUIVER_ICON_VIEW(m_pIconView),(QuiverIconViewGetIconPixbufFunc)icon_pixbuf_callback,this,NULL);
#endif
	quiver_icon_view_set_thumbnail_texture_func(QUIVER_ICON_VIEW(m_pIconView),thumbnail_texture_callback,this,NULL);
	quiver_icon_view_set_icon_texture_func(QUIVER_ICON_VIEW(m_pIconView),icon_texture_callback,this,NULL);
	quiver_icon_view_set_get_filmstrip_texture_func(QUIVER_ICON_VIEW(m_pIconView),filmstrip_texture_callback,this,NULL);
	quiver_icon_view_set_scroll_type(QUIVER_ICON_VIEW(m_pIconView),QUIVER_ICON_VIEW_SCROLL_SMOOTH_CENTER);
	int iIconSize = prefsPtr->GetInteger(QUIVER_PREFS_VIEWER,QUIVER_PREFS_VIEWER_FILMSTRIP_SIZE, 128);
	quiver_icon_view_set_icon_size(QUIVER_ICON_VIEW(m_pIconView),iIconSize,iIconSize);
	bool bFilmstripSquare = prefsPtr->GetBoolean(QUIVER_PREFS_VIEWER,QUIVER_PREFS_VIEWER_FILMSTRIP_SQUARE, false);
	quiver_icon_view_set_thumbnails_square(QUIVER_ICON_VIEW(m_pIconView), bFilmstripSquare);
	bool bFilmstrip = prefsPtr->GetBoolean(QUIVER_PREFS_BROWSER,QUIVER_PREFS_BROWSER_THUMBS_FILMSTRIP, true);
	quiver_icon_view_set_filmstrip_enabled(QUIVER_ICON_VIEW(m_pIconView), bFilmstrip);
	m_ThumbnailLoader.SetIconDimensions(iIconSize, iIconSize);
	m_ThumbnailLoader.SetMapped(gtk_widget_get_mapped(m_pIconView));
	quiver_icon_view_set_drag_behavior(QUIVER_ICON_VIEW(m_pIconView),QUIVER_ICON_VIEW_DRAG_BEHAVIOR_SCROLL);

	g_signal_connect(G_OBJECT(m_pIconView),"cell_activated",G_CALLBACK(viewer_iconview_cell_activated),this);
	g_signal_connect(G_OBJECT(m_pIconView),"cursor_changed",G_CALLBACK(viewer_iconview_cursor_changed),this);
	g_signal_connect(G_OBJECT(m_pIconView),"map",G_CALLBACK(viewer_icon_view_map_cb),this);
	g_signal_connect(G_OBJECT(m_pIconView),"unmap",G_CALLBACK(viewer_icon_view_unmap_cb),this);

	attach_viewer_input_controllers(m_pImageView, this);
	{
		GtkEventController *key = gtk_event_controller_key_new();
		g_signal_connect(key, "key-pressed", G_CALLBACK(viewer_imageview_key_press_event), this);
		gtk_widget_add_controller(m_pImageView, key);
	}

    g_signal_connect (G_OBJECT (m_pImageView), "activated",
    			G_CALLBACK (viewer_imageview_activated), this);

    g_signal_connect (G_OBJECT (m_pImageView), "reload",
    			G_CALLBACK (viewer_imageview_reload), this);

    g_signal_connect (G_OBJECT (m_pImageView), "magnification-changed",
    			G_CALLBACK (viewer_imageview_magnification_changed), this);

    g_signal_connect (G_OBJECT (m_pImageView), "view-mode-changed",
    			G_CALLBACK (viewer_imageview_view_mode_changed), this);

    g_signal_connect (G_OBJECT (m_pAdjustmentH), "changed",
    			G_CALLBACK (image_view_adjustment_changed), this);

    g_signal_connect (G_OBJECT (m_pAdjustmentV), "changed",
    			G_CALLBACK (image_view_adjustment_changed), this);

	g_signal_connect (G_OBJECT (m_pAdjustmentH), "value-changed",
			G_CALLBACK (image_view_adjustment_value_changed), this);

	g_signal_connect (G_OBJECT (m_pAdjustmentV), "value-changed",
			G_CALLBACK (image_view_adjustment_value_changed), this);

	//g_signal_connect(G_OBJECT(m_pIconView),"selection_changed",G_CALLBACK(iconview_selection_changed_cb),this);
	IPixbufLoaderObserverPtr tmp( new ViewerImageViewPixbufLoaderObserver(QUIVER_IMAGE_VIEW(m_pImageView), m_pImageErrorLabel, this));
	m_PixbufLoaderObserverPtr = tmp;
	m_ImageLoader.AddPixbufLoaderObserver(m_PixbufLoaderObserverPtr.get());
	
	gtk_widget_set_visible(m_pHBox, TRUE);
	gtk_widget_set_visible(m_pHBox, FALSE);
	
	m_pNavControlPill = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_add_css_class(m_pNavControlPill, "nav-control-pill");
	gtk_widget_set_halign(m_pNavControlPill, GTK_ALIGN_END);
	gtk_widget_set_valign(m_pNavControlPill, GTK_ALIGN_END);
	gtk_widget_set_margin_end(m_pNavControlPill, 16);
	gtk_widget_set_margin_bottom(m_pNavControlPill, 16);
	gtk_widget_set_can_target(m_pNavControlPill, TRUE);

	m_pNavigationControl = quiver_navigation_control_new_with_adjustments (m_pAdjustmentH, m_pAdjustmentV);
	/* The widget drives the image view's scrollbars on its own; a video is
	 * panned through the pipeline, so it needs the raw drag distance. */
	g_signal_connect(m_pNavigationControl, "drag-delta",
		G_CALLBACK(nav_control_drag_delta_cb), this);
	gtk_box_append(GTK_BOX(m_pNavControlPill), m_pNavigationControl);

	gtk_overlay_add_overlay(GTK_OVERLAY(m_pOverlay), m_pNavControlPill);
	gtk_overlay_set_measure_overlay(GTK_OVERLAY(m_pOverlay), m_pNavControlPill, FALSE);
	gtk_widget_set_visible(m_pNavControlPill, FALSE);
	
	bool bShowFilmstrip = prefsPtr->GetBoolean(QUIVER_PREFS_VIEWER,QUIVER_PREFS_VIEWER_FILMSTRIP_SHOW,true);
	if (!bShowFilmstrip)
	{
		gtk_widget_set_visible(m_pIconView, FALSE);
	}
	gboolean bQuickPreview = (gboolean)prefsPtr->GetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_QUICK_PREVIEW, true);
	m_ImageLoader.EnableQuickPreview(bQuickPreview);
	m_ImageLoader.SetThumbnailCache(&m_ThumbnailCache);
	

	// set up the gstreamer pipeline
	m_pPipeline = gst_element_factory_make("playbin", "player");

	m_pVideoSinkWidget = NULL;
	m_pVideoFixed = NULL;
	m_dVideoZoomFinal = 1.0;
	m_dVideoZoomMin = 1.0;
	m_iVideoZoomTimeoutID = 0;
	m_fVideoPanFX = 0.5;
	m_fVideoPanFY = 0.5;
	m_dVideoViewW = 0.;
	m_dVideoViewH = 0.;
	m_dVideoPanRangeX = 0.;
	m_dVideoPanRangeY = 0.;
	m_dVideoPanX = 0.;
	m_dVideoPanY = 0.;
	m_dVideoLastWidgetW = 0.;
	m_dVideoLastFrameW = 0.;
	m_dVideoLastFrameH = 0.;
	m_dVideoLastFrameZoom = 0.;
	m_dVideoLastWidgetH = 0.;
	m_dVideoLastZc = 1.0;
	m_bVideoZoomCropActive = FALSE;
	m_bVideoZoomInputCropActive = FALSE;
	m_bVideoPanning = FALSE;
	StopVideoPanSlowdown();
	m_bVideoNeedsFirstFrame = FALSE;
	m_bVideoFlushPending = FALSE;
	m_bVideoPagePending = FALSE;
	m_pVideoPaintable = NULL;
	m_pVideoRotatedPaintable = quiver_rotated_paintable_new(NULL);
	m_pVideoSinkWidget = gtk_picture_new_for_paintable(GDK_PAINTABLE(m_pVideoRotatedPaintable));
	gtk_picture_set_content_fit(GTK_PICTURE(m_pVideoSinkWidget), GTK_CONTENT_FIT_FILL);
	gtk_picture_set_can_shrink(GTK_PICTURE(m_pVideoSinkWidget), TRUE);

	GstElement* video_sink = BuildVideoZoomBin();

	GstElement* gaudio = gst_element_factory_make("autoaudiosink", NULL);
	if (NULL == gaudio)
	{
		gaudio = gst_element_factory_make("gconfaudiosink", NULL);
	}

	/* scaletempo preserves pitch when the playback rate changes.
	 * Without it, speeding up / slowing down also shifts the pitch. */
	GstElement* scaletempo = gst_element_factory_make("scaletempo", NULL);
	if (scaletempo != NULL)
	{
		g_object_set(G_OBJECT(m_pPipeline), "audio-filter", scaletempo, NULL);
	}

	guint flags = 0;
	g_object_get(G_OBJECT(m_pPipeline), "flags", &flags, NULL);
	flags &= ~(1 << 2); // Disable subtitles by default
	/* do NOT enable GST_PLAY_FLAG_DEINTERLACE: playbin inserts a software
	 * videoconvert for it, which cannot convert the hardware decoder's
	 * VAMemory buffers and leaves the GL sink showing a black frame */

	if (video_sink != NULL)
	{
		g_object_set(G_OBJECT(m_pPipeline),
			"video-sink", video_sink,
			"audio-sink", gaudio,
			"flags", flags,
			NULL);
	}
	else if (gaudio != NULL)
	{
		g_object_set(G_OBJECT(m_pPipeline),
			"audio-sink", gaudio,
			"flags", flags,
			NULL);
	}

	if (m_pVideoSinkWidget != NULL)
	{
		m_pVideoFixed = quiver_freelayout_new();
		gtk_widget_set_hexpand(m_pVideoFixed, TRUE);
		gtk_widget_set_vexpand(m_pVideoFixed, TRUE);
		/* clip the (possibly oversized) video picture to the video area, like
		 * GtkFixed's bin window used to */
		gtk_widget_set_overflow(m_pVideoFixed, GTK_OVERFLOW_HIDDEN);
		/* the container's size_allocate vfunc re-runs the zoom math whenever
		 * the video area is reallocated (GTK4 removed ::size-allocate) */
		g_object_set_data(G_OBJECT(m_pVideoFixed), "quiver-viewer-impl", this);
		gtk_stack_add_named(GTK_STACK(m_pStack), m_pVideoFixed, "video");

		/* Match the video background to the image viewer background setting */
		if (!prefsPtr->GetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_USE_THEME_COLOR,true))
		{
			GdkRGBA color;
			string strBGColorImg = prefsPtr->GetString(QUIVER_PREFS_APP,QUIVER_PREFS_APP_BG_IMAGEVIEW,"#000");
			if (!strBGColorImg.empty() && gdk_rgba_parse(&color, strBGColorImg.c_str()))
				set_widget_bg_color(m_pVideoFixed, &color);
		}

		gtk_widget_set_size_request(m_pVideoSinkWidget, 1, 1);
		quiver_freelayout_put(m_pVideoFixed, m_pVideoSinkWidget, 0, 0);

		attach_viewer_input_controllers(m_pVideoFixed, this);
		/* NOTE: do NOT also attach the click gesture to m_pVideoSinkWidget here.
		 * The sink is a CHILD of the fixed, and pointer gestures bubble by
		 * default, so a click on the video would fire the gesture on BOTH the
		 * sink and the fixed.  The sink's press (deeper → runs first) overwrites
		 * the pan-origin with sink-relative coords and its release (also first)
		 * compares those against the fixed-relative origin, which differ by the
		 * sink's (nonzero, letterboxed) offset — every video click was treated as
		 * a drag of >5px, no toggle, and m_bVideoPanning got cleared so the
		 * fixed's release skipped too.  Long story short: never click-to-pause
		 * the actual video pixels.  One gesture on the fixed (which covers the
		 * sink and any surrounding bars via bubbling) is the correct wiring. */
		// whenever the video appears, make sure the media controls stack above
		g_signal_connect(G_OBJECT(m_pVideoSinkWidget), "map", G_CALLBACK(video_zoom_sink_map_cb), this);
	}


	gdouble volume = 0.;
	g_object_get(G_OBJECT(m_pPipeline), "volume", &volume, NULL);
	m_dVolume = volume;
	if (m_pVolumeScale != NULL)
		gtk_range_set_value(GTK_RANGE(m_pVolumeScale), volume);

	GstBus* bus = gst_pipeline_get_bus(GST_PIPELINE(m_pPipeline));
	//gst_bus_set_sync_handler (bus, (GstBusSyncHandler) gstreamer_bus_sync_handler, this, NULL); // Removed sync handler
	m_iGstBusWatchID = gst_bus_add_watch (bus, (GstBusFunc) gstreamer_bus_watcher, this);
	gst_object_unref (bus);

	UpdateUI();
}


Viewer::Viewer() : m_ViewerImplPtr(new Viewer::ViewerImpl(this))
{
	

}

Viewer::~Viewer()
{
	UnregisterActions();
	m_ViewerImplPtr->SlideShowStop(false, false);
}

GtkWidget *Viewer::GetWidget()
{
	return m_ViewerImplPtr->m_pHBox;
}

void Viewer::SetImageList(IImageListViewPtr imgList)
{
	m_ViewerImplPtr->SetImageList(imgList);
}

bool Viewer::ResetViewMode()
{
	ViewerImplPtr pViewerImpl = m_ViewerImplPtr;
	QuiverImageView* iv = QUIVER_IMAGE_VIEW(pViewerImpl->m_pImageView);
	if (iv == NULL || !QUIVER_IS_IMAGE_VIEW(iv))
		return false;

	/* "Keep zoom and pan" is not a zoom to undo - the framing on screen is what
	 * that mode is there to keep - so Escape has nothing to reset here and says
	 * so, which lets the caller go on to the next thing Escape does (leaving the
	 * viewer) instead of silently un-doing the mode the user chose. */
	if (viewer_view_mode_is_keep(quiver_image_view_get_view_mode(iv)))
		return false;

	const gdouble mag_before = quiver_image_view_get_magnification(iv);
	gdouble centre_x_before = 0., centre_y_before = 0.;
	const gboolean had_centre = quiver_image_view_get_view_center(iv, &centre_x_before, &centre_y_before);

	bool bReset = false;
	if (pViewerImpl->IsVideo())
	{
		/* a video's zoom lives in the video state, not in the image view, so
		 * the reset below would leave it where it is */
		gdouble fit = pViewerImpl->m_dVideoZoomMin;
		if (pViewerImpl->m_dVideoZoomFinal > fit + 0.005)
		{
			pViewerImpl->SetVideoZoom(fit);
			bReset = true;
		}
	}

	quiver_image_view_reset_view_mode(iv, TRUE);

	/* The reset keeps the mode, so what says whether it did anything is whether
	 * the framing moved: the magnification, or the centre of the part being
	 * shown. */
	gdouble centre_x_after = 0., centre_y_after = 0.;
	if (fabs(quiver_image_view_get_magnification(iv) - mag_before) > 0.001)
		bReset = true;
	else if (had_centre && quiver_image_view_get_view_center(iv, &centre_x_after, &centre_y_after)
		&& (fabs(centre_x_after - centre_x_before) > 0.001 || fabs(centre_y_after - centre_y_before) > 0.001))
	{
		bReset = true;
	}

	return bReset;
}

void Viewer::GrabFocus()
{
	QuiverUtils::GrabFocusForWidget(m_ViewerImplPtr->m_pImageView);
}


static gboolean idle_set_image_index (gpointer data)
{
	Viewer::ViewerImpl* pViewerImpl = (Viewer::ViewerImpl*)data;
	pViewerImpl->SetImageIndex(pViewerImpl->m_ImageListPtr->GetCurrentIndex(),true);
	pViewerImpl->m_iIdleSetIndex = 0;
	return FALSE;
}

void Viewer::Show()
{
	gtk_widget_set_visible(m_ViewerImplPtr->m_pHBox, TRUE);

	gint cursor_cell = quiver_icon_view_get_cursor_cell(QUIVER_ICON_VIEW(m_ViewerImplPtr->m_pIconView));
	if (0 == m_ViewerImplPtr->m_ImageListPtr->GetSize() || m_ViewerImplPtr->m_QuiverFileCurrent != m_ViewerImplPtr->m_ImageListPtr->GetCurrent())
	{
		show_image_load_error(m_ViewerImplPtr->m_pImageErrorLabel, false);
		quiver_image_view_set_texture(QUIVER_IMAGE_VIEW(m_ViewerImplPtr->m_pImageView),NULL);
		// set image index in an idle function
		m_ViewerImplPtr->m_iIdleSetIndex
			= g_idle_add_full(G_PRIORITY_HIGH, idle_set_image_index, m_ViewerImplPtr.get(), NULL);
	}
	else if (0 != m_ViewerImplPtr->m_ImageListPtr->GetSize())
	{
		if ( (gint)m_ViewerImplPtr->m_ImageListPtr->GetCurrentIndex() != cursor_cell  )
		{
			g_signal_handlers_block_by_func(m_ViewerImplPtr->m_pIconView,(gpointer)viewer_iconview_cursor_changed,m_ViewerImplPtr.get());
	
			quiver_icon_view_set_cursor_cell(
				QUIVER_ICON_VIEW(m_ViewerImplPtr->m_pIconView),
				m_ViewerImplPtr->m_ImageListPtr->GetCurrentIndex() );
			
			g_signal_handlers_unblock_by_func(m_ViewerImplPtr->m_pIconView,(gpointer)viewer_iconview_cursor_changed,m_ViewerImplPtr.get());
		}
	
	}


	m_ViewerImplPtr->m_ImageListPtr->UnblockHandler(m_ViewerImplPtr->m_ImageListEventHandlerPtr);
	m_ViewerImplPtr->UpdateNavigationControl();

}

void Viewer::Hide()
{
	m_ViewerImplPtr->StopVideo(true);
	m_ViewerImplPtr->SlideShowStop(true);
	/* The pointer is hidden on the window, not on the viewer, because that is
	 * what a fullscreen video needs - and the browser is in that same window.
	 * Leaving the viewer therefore has to hand the pointer back, and cancel the
	 * idle timer that would take it away again behind our back. */
	ResetIdleCursor();
	
	gtk_widget_set_visible(m_ViewerImplPtr->m_pHBox, FALSE);

	m_ViewerImplPtr->m_ImageListPtr->BlockHandler(m_ViewerImplPtr->m_ImageListEventHandlerPtr);
}

static void viewer_rotate_undo_cb(const char* uri, int undo_direction, gpointer user_data)
{
	(void)uri;
	Viewer::ViewerImpl* impl = static_cast<Viewer::ViewerImpl*>(user_data);
	if (impl == NULL || impl != s_pLastRegisteredViewerImpl)
	{
		return;
	}
	if (impl->m_pImageView && QUIVER_IS_IMAGE_VIEW(impl->m_pImageView))
	{
		QuiverImageView* imageview = QUIVER_IMAGE_VIEW(impl->m_pImageView);
		quiver_image_view_rotate(imageview, undo_direction > 0 ? TRUE : FALSE);
		int orient_op = undo_direction > 0 ? ORIENTATION_ROTATE_CW : ORIENTATION_ROTATE_CCW;
		impl->SetCurrentOrientation(orientation_matrix[orient_op][impl->GetCurrentOrientation()]);
	}
}

void Viewer::RegisterActions()
{
	s_pLastRegisteredViewerImpl = m_ViewerImplPtr.get();
	QuiverFileOps::SetRotateUndoCallback(viewer_rotate_undo_cb, m_ViewerImplPtr.get());

	/* Viewer simple actions */
	QuiverUtils::AddSimpleAction(ACTION_VIEWER_CUT, "<Control>X", viewer_action_handler_cb, m_ViewerImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_VIEWER_COPY, "<Control>C", viewer_action_handler_cb, m_ViewerImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_VIEWER_TRASH, "Delete", viewer_action_handler_cb, m_ViewerImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_VIEWER_TRASH_FORCE, "<Shift>Delete", viewer_action_handler_cb, m_ViewerImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_VIEWER_RESTORE, NULL, viewer_action_handler_cb, m_ViewerImplPtr.get());

	QuiverUtils::AddSimpleAction(ACTION_VIEWER_PREVIOUS, "Left", viewer_action_handler_cb, m_ViewerImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_VIEWER_PREVIOUS_2, NULL, viewer_action_handler_cb, m_ViewerImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_VIEWER_NEXT, "Right", viewer_action_handler_cb, m_ViewerImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_VIEWER_NEXT_2, NULL, viewer_action_handler_cb, m_ViewerImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_VIEWER_FIRST, "Home", viewer_action_handler_cb, m_ViewerImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_VIEWER_LAST, "End", viewer_action_handler_cb, m_ViewerImplPtr.get());

	QuiverUtils::AddSimpleAction(ACTION_VIEWER_ZOOM_IN, "equal", viewer_action_handler_cb, m_ViewerImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_VIEWER_ZOOM_OUT, "minus", viewer_action_handler_cb, m_ViewerImplPtr.get());

	QuiverUtils::AddSimpleAction(ACTION_VIEWER_ROTATE_CW, "r", viewer_action_handler_cb, m_ViewerImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_VIEWER_ROTATE_CW_2, NULL, viewer_action_handler_cb, m_ViewerImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_VIEWER_ROTATE_CCW, "<Shift>r", viewer_action_handler_cb, m_ViewerImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_VIEWER_ROTATE_CCW_2, NULL, viewer_action_handler_cb, m_ViewerImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_VIEWER_FLIP_H, "h", viewer_action_handler_cb, m_ViewerImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_VIEWER_FLIP_H_2, NULL, viewer_action_handler_cb, m_ViewerImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_VIEWER_FLIP_V, "v", viewer_action_handler_cb, m_ViewerImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_VIEWER_FLIP_V_2, NULL, viewer_action_handler_cb, m_ViewerImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_VIEWER_VIDEO_PLAY, "space", viewer_action_handler_cb, m_ViewerImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_VIEWER_VIDEO_PLAY_2, NULL, viewer_action_handler_cb, m_ViewerImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_VIEWER_VIDEO_SKIP_FORWARD, "l", viewer_action_handler_cb, m_ViewerImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_VIEWER_VIDEO_SKIP_BACK, "j", viewer_action_handler_cb, m_ViewerImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_VIEWER_VIDEO_SEEK_FWD_5, "period", viewer_action_handler_cb, m_ViewerImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_VIEWER_VIDEO_SEEK_BACK_5, "comma", viewer_action_handler_cb, m_ViewerImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_VIEWER_VIDEO_FRAME_FWD, "<Shift>greater", viewer_action_handler_cb, m_ViewerImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_VIEWER_VIDEO_FRAME_BACK, "<Shift>less", viewer_action_handler_cb, m_ViewerImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_VIEWER_VIDEO_SNAPSHOT, "", viewer_action_handler_cb, m_ViewerImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_VIEWER_VIDEO_MUTE, "m", viewer_action_handler_cb, m_ViewerImplPtr.get());

	/* Viewer toggle actions */
	QuiverUtils::AddToggleAction(ACTION_VIEWER_VIEW_FILM_STRIP, "", FALSE, viewer_action_handler_cb, m_ViewerImplPtr.get());
	QuiverUtils::AddToggleAction(ACTION_VIEWER_ROTATE_FOR_BEST_FIT, "", FALSE, viewer_action_handler_cb, m_ViewerImplPtr.get());

	/* Viewer zoom radio actions */
	QuiverImageViewMode mode = quiver_image_view_get_view_mode(QUIVER_IMAGE_VIEW(m_ViewerImplPtr->m_pImageView));
	const gchar *zoom_names[] = {
		ACTION_VIEWER_ZOOM_FIT,
		ACTION_VIEWER_ZOOM_FIT_STRETCH,
		ACTION_VIEWER_ZOOM_100,
		ACTION_VIEWER_ZOOM_FILL_SCREEN,
		ACTION_VIEWER_ZOOM,
		ACTION_VIEWER_ZOOM_KEEP,
	};
	gint zoom_values[] = {
		QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW,
		QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW_STRETCH,
		QUIVER_IMAGE_VIEW_MODE_ACTUAL_SIZE,
		QUIVER_IMAGE_VIEW_MODE_FILL_SCREEN,
		QUIVER_IMAGE_VIEW_MODE_ZOOM,
		QUIVER_IMAGE_VIEW_MODE_ZOOM_KEEP,
	};
	QuiverUtils::AddRadioActions(zoom_names, zoom_values, G_N_ELEMENTS(zoom_names),
		mode, viewer_radio_action_handler_cb, m_ViewerImplPtr.get());

	const gchar *speed_names[] = {
		"VideoSpeed025",
		"VideoSpeed05",
		"VideoSpeed10",
		"VideoSpeed15",
		"VideoSpeed20",
		"VideoSpeed40",
		"VideoSpeed80",
		"VideoSpeed160",
	};
	gint speed_values[] = { 0, 1, 2, 3, 4, 5, 6, 7 };
	QuiverUtils::AddRadioActions(speed_names, speed_values, G_N_ELEMENTS(speed_names),
		2, viewer_radio_action_handler_cb, m_ViewerImplPtr.get());

	/* initial toggle state from preferences */
	PreferencesPtr prefsPtr = Preferences::GetInstance();
	bool bShowFilmStrip = prefsPtr->GetBoolean(QUIVER_PREFS_VIEWER,QUIVER_PREFS_VIEWER_FILMSTRIP_SHOW);
	QuiverUtils::ToggleActionSetActive(ACTION_VIEWER_VIEW_FILM_STRIP,bShowFilmStrip ? TRUE : FALSE);

	QuiverUtils::ToggleActionSetActive(ACTION_VIEWER_ROTATE_FOR_BEST_FIT,m_ViewerImplPtr->m_bMaximizeViewableArea ? TRUE : FALSE);
}

void Viewer::UnregisterActions()
{
	if (s_pLastRegisteredViewerImpl == m_ViewerImplPtr.get())
	{
		s_pLastRegisteredViewerImpl = NULL;
		QuiverFileOps::SetRotateUndoCallback(NULL, NULL);
		const char* const actions[] = {
			ACTION_VIEWER_CUT, ACTION_VIEWER_COPY,
			ACTION_VIEWER_TRASH, ACTION_VIEWER_TRASH_FORCE, ACTION_VIEWER_RESTORE,
			ACTION_VIEWER_PREVIOUS, ACTION_VIEWER_PREVIOUS_2,
			ACTION_VIEWER_NEXT, ACTION_VIEWER_NEXT_2,
			ACTION_VIEWER_FIRST, ACTION_VIEWER_LAST,
			ACTION_VIEWER_ZOOM_IN, ACTION_VIEWER_ZOOM_OUT,
			ACTION_VIEWER_ROTATE_CW, ACTION_VIEWER_ROTATE_CW_2,
			ACTION_VIEWER_ROTATE_CCW, ACTION_VIEWER_ROTATE_CCW_2,
			ACTION_VIEWER_FLIP_H, ACTION_VIEWER_FLIP_H_2,
			ACTION_VIEWER_FLIP_V, ACTION_VIEWER_FLIP_V_2,
			ACTION_VIEWER_VIDEO_PLAY, ACTION_VIEWER_VIDEO_PLAY_2,
			ACTION_VIEWER_VIDEO_SKIP_FORWARD, ACTION_VIEWER_VIDEO_SKIP_BACK,
			ACTION_VIEWER_VIDEO_SEEK_FWD_5, ACTION_VIEWER_VIDEO_SEEK_BACK_5,
			ACTION_VIEWER_VIDEO_FRAME_FWD, ACTION_VIEWER_VIDEO_FRAME_BACK,
			ACTION_VIEWER_VIDEO_SNAPSHOT, ACTION_VIEWER_VIDEO_MUTE,
			ACTION_VIEWER_VIEW_FILM_STRIP, ACTION_VIEWER_ROTATE_FOR_BEST_FIT,
			ACTION_VIEWER_ZOOM_FIT, ACTION_VIEWER_ZOOM_FIT_STRETCH,
			ACTION_VIEWER_ZOOM_100, ACTION_VIEWER_ZOOM_FILL_SCREEN, ACTION_VIEWER_ZOOM,
			ACTION_VIEWER_ZOOM_KEEP,
			"VideoSpeed025", "VideoSpeed05", "VideoSpeed10", "VideoSpeed15",
			"VideoSpeed20", "VideoSpeed40", "VideoSpeed80", "VideoSpeed160"
		};
		for (size_t i = 0; i < sizeof(actions) / sizeof(actions[0]); ++i)
		{
			QuiverUtils::RemoveAction(actions[i]);
		}
	}
}

void Viewer::SetStatusbar(StatusbarPtr statusbarPtr)
{
	m_ViewerImplPtr->m_ImageLoader.RemovePixbufLoaderObserver(m_ViewerImplPtr->m_StatusbarPtr.get());
	
	m_ViewerImplPtr->m_StatusbarPtr = statusbarPtr;
	
	m_ViewerImplPtr->m_ImageLoader.AddPixbufLoaderObserver(m_ViewerImplPtr->m_StatusbarPtr.get());
}

double Viewer::GetMagnification() const
{
	return quiver_image_view_get_magnification(QUIVER_IMAGE_VIEW(m_ViewerImplPtr->m_pImageView));
}

bool Viewer::IsFilmstripOverlay() const
{
	return m_ViewerImplPtr->m_bFilmstripOverlay;
}

bool Viewer::IsHideFilmstripFS() const
{
	return m_ViewerImplPtr->m_bHideFilmstripFS;
}

GtkWidget *Viewer::GetFilmstripWidget() const
{
	return m_ViewerImplPtr->m_pIconView;
}

GtkWidget *Viewer::GetOverlay()
{
	return m_ViewerImplPtr->m_pOverlay;
}

void Viewer::ShowFilmstripOverlay()
{
	m_ViewerImplPtr->ShowFilmstripOverlay();
}

void Viewer::HideFilmstripOverlay()
{
	m_ViewerImplPtr->HideFilmstripOverlay();
}

void Viewer::CancelFilmstripHide()
{
	m_ViewerImplPtr->CancelFilmstripHide();
}

void Viewer::SetFilmstripHiddenByFS(bool bHidden)
{
	m_ViewerImplPtr->m_bFilmstripHiddenByFS = bHidden;
}

bool Viewer::IsFilmstripHiddenByFS() const
{
	return m_ViewerImplPtr->m_bFilmstripHiddenByFS;
}

void Viewer::UpdateHUDPosition()
{
	if (m_ViewerImplPtr)
		m_ViewerImplPtr->UpdateHUDPosition();
}

void Viewer::ResetIdleCursor()
{
	if (m_ViewerImplPtr)
	{
		m_ViewerImplPtr->CancelControlsFade();
		viewer_set_idle_cursor(m_ViewerImplPtr.get(), false);
		if (0 != m_ViewerImplPtr->m_iTimeoutMouseMotionNotify)
		{
			g_source_remove(m_ViewerImplPtr->m_iTimeoutMouseMotionNotify);
			m_ViewerImplPtr->m_iTimeoutMouseMotionNotify = 0;
		}
	}
}

void Viewer::OnExitFullscreen()
{
	if (m_ViewerImplPtr)
	{
		viewer_set_idle_cursor(m_ViewerImplPtr.get(), false);
		viewer_controls_show(m_ViewerImplPtr.get());
		m_ViewerImplPtr->m_bControlsVisible = true;
		if (m_ViewerImplPtr->IsVideo())
			m_ViewerImplPtr->UpdateTimelineVisibility();
		m_ViewerImplPtr->StartControlsFade(true);
		m_ViewerImplPtr->RefreshAutoHideTimer();
	}
}

void Viewer::RefreshAutoHideTimer()
{
	if (m_ViewerImplPtr)
	{
		m_ViewerImplPtr->RefreshAutoHideTimer();
	}
}

int Viewer::GetCurrentOrientation()
{
	return m_ViewerImplPtr->GetCurrentOrientation();	
}


static gboolean timeout_advance_slideshow (gpointer data)
{
	Viewer::ViewerImpl* pViewerImpl = (Viewer::ViewerImpl*)data;

	if (pViewerImpl->m_bSlideShowPaused)
	{
		return FALSE;
	}
	
	int iNextIndex = pViewerImpl->m_ImageListPtr->GetCurrentIndex()+1;

	if (pViewerImpl->m_ImageLoader.IsWorking() || quiver_image_view_is_in_transition(QUIVER_IMAGE_VIEW(pViewerImpl->m_pImageView)) )
	{
		// wait until the imageloader has finished working
		// before advancing the slideshow
		++pViewerImpl->m_iSlideShowWaitCount;
		pViewerImpl->m_iTimeoutSlideshowID 
			= g_timeout_add(SLIDESHOW_WAIT_DURATION,timeout_advance_slideshow, pViewerImpl);

		return FALSE;
	}

	switch (pViewerImpl->m_SlideShowState)
	{
		case Viewer::ViewerImpl::SLIDESHOW_STATE_PAUSED:
			break;
		case Viewer::ViewerImpl::SLIDESHOW_STATE_ADVANCE:
			{
				bool bStop = false;
				if (!pViewerImpl->m_ImageListPtr->HasNext()) 
				{
					if (pViewerImpl->m_bSlideShowLoop)
					{
						iNextIndex = 0;
					}
					else
					{
						pViewerImpl->m_pViewer->SlideShowStop();
						bStop = true;
					}
				}
				
				if (!bStop)
				{
					pViewerImpl->m_bSlideShowMachineAdvancing = true;
					pViewerImpl->SetImageIndex(iNextIndex,true,false);
					pViewerImpl->m_bSlideShowMachineAdvancing = false;

					++pViewerImpl->m_iSlideShowWaitCount;
					pViewerImpl->m_iTimeoutSlideshowID 
						= g_timeout_add(SLIDESHOW_WAIT_DURATION,timeout_advance_slideshow, pViewerImpl);

					pViewerImpl->m_SlideShowState = Viewer::ViewerImpl::SLIDESHOW_STATE_CACHE;
				}
			}
			break;
		case Viewer::ViewerImpl::SLIDESHOW_STATE_CACHE:
			{

				// wait time is the slideshow duration minus any amount of time
				// spent waiting for a transition or the loader to complete
				// : minimum value of 10ms
				int iWaitTime = pViewerImpl->m_iSlideShowWaitCount * SLIDESHOW_WAIT_DURATION;
				iWaitTime = pViewerImpl->m_iSlideShowDuration - iWaitTime;
				iWaitTime = MAX(10, iWaitTime);

				pViewerImpl->CacheNext(true);
				if (pViewerImpl->IsVideo())
				{
					pViewerImpl->m_SlideShowState = Viewer::ViewerImpl::SLIDESHOW_STATE_PLAY_VIDEO;
					// show the video image for one second before playing video
					iWaitTime = 1000; 
				}
				else
				{
					pViewerImpl->m_SlideShowState = Viewer::ViewerImpl::SLIDESHOW_STATE_ADVANCE;
					pViewerImpl->m_iSlideShowWaitCount = 0;
				}

				pViewerImpl->m_iTimeoutSlideshowID 
					= g_timeout_add(iWaitTime,timeout_advance_slideshow, pViewerImpl);

			}
			break;
		case Viewer::ViewerImpl::SLIDESHOW_STATE_PLAY_VIDEO:
			{

				/* If the user already started this video themselves (space,
				 * click, or play button) during the ~1s preview window, it is
				 * playing right now: don't route through PlayPauseVideo() —
				 * it would toggle it back to paused.  Otherwise let the
				 * machine start playback. */
				bool bUserStarted = false;
				if (pViewerImpl->IsPlaying())
				{
					gchar* uri = NULL;
					g_object_get(G_OBJECT(pViewerImpl->m_pPipeline), "current-uri", &uri, NULL);
					bUserStarted = (0 == g_strcmp0(uri, pViewerImpl->m_ImageListPtr->GetCurrent().GetURI()));
					g_free(uri);
				}
				if (!bUserStarted)
				{
					pViewerImpl->PlayPauseVideo();
				}
				pViewerImpl->m_SlideShowState = Viewer::ViewerImpl::SLIDESHOW_STATE_PLAYING_VIDEO;
				pViewerImpl->m_iTimeoutSlideshowID 
						= g_timeout_add(SLIDESHOW_WAIT_DURATION,timeout_advance_slideshow, pViewerImpl);

			}
		break;
		case Viewer::ViewerImpl::SLIDESHOW_STATE_PLAYING_VIDEO:
			{
				if (pViewerImpl->IsPlaying())
				{
					pViewerImpl->m_iTimeoutSlideshowID 
						= g_timeout_add(SLIDESHOW_WAIT_DURATION,timeout_advance_slideshow, pViewerImpl);
				}
				else
				{
					pViewerImpl->m_iSlideShowWaitCount = 0;
					pViewerImpl->m_SlideShowState = Viewer::ViewerImpl::SLIDESHOW_STATE_ADVANCE;
					pViewerImpl->m_iTimeoutSlideshowID 
						= g_timeout_add(SLIDESHOW_WAIT_DURATION,timeout_advance_slideshow, pViewerImpl);
				}
			}
			break;
	}
	return FALSE;
}

/* Called from SetImageIndex when the current item was changed by the user
 * (arrow keys, scroll wheel, filmstrip, First/Last/Next/Previous actions)
 * while a slideshow is running.  The state machine still holds a stale
 * advance/cache timer aimed at the previously shown item, so we cancel it
 * and pause the show: the user is browsing manually, and the item they
 * selected must not be skipped past by the machine (e.g. an ADVANCE slot
 * would move straight past a manually chosen video, which is exactly the
 * reported bug).  A manual resume (click, hotkey, or the paused pill's
 * Resume button) restarts the show on the current item. */
void Viewer::ViewerImpl::HandleSlideShowManualNavigation()
{
	if (!m_bSlideShowRunning || m_bSlideShowPaused)
	{
		return;
	}

	if (0 != m_iTimeoutSlideshowID)
	{
		g_source_remove(m_iTimeoutSlideshowID);
		m_iTimeoutSlideshowID = 0;
	}

	if (m_pViewer)
	{
		m_pViewer->SlideShowPause();
	}
}

void Viewer::ViewerImpl::ShowSlideShowPausedPill()
{
	if (m_pSlideShowPausedPill && G_IS_OBJECT(m_pSlideShowPausedPill) && GTK_IS_WIDGET(m_pSlideShowPausedPill))
	{
		gtk_widget_set_visible(m_pSlideShowPausedPill, TRUE);
	}
}

void Viewer::ViewerImpl::HideSlideShowPausedPill()
{
	if (m_pSlideShowPausedPill && G_IS_OBJECT(m_pSlideShowPausedPill) && GTK_IS_WIDGET(m_pSlideShowPausedPill))
	{
		gtk_widget_set_visible(m_pSlideShowPausedPill, FALSE);
	}
}

void Viewer::StopVideo(bool reloadImage)
{
	if (m_ViewerImplPtr)
	{
		m_ViewerImplPtr->StopVideo(reloadImage);
	}
}

void Viewer::SlideShowStart()
{
	PreferencesPtr prefsPtr = Preferences::GetInstance();
	bool bTransition = prefsPtr->GetBoolean(QUIVER_PREFS_SLIDESHOW,QUIVER_PREFS_SLIDESHOW_TRANSITION,true);

	/* If the viewer hasn't synced to the current (browser-selected) item yet
	 * — Viewer::Show() may have queued an idle to do so — flush that sync
	 * now, before the show is marked running.  Otherwise the deferred
	 * SetImageIndex would look like a manual navigation and immediately
	 * pause the freshly started show. */
	if (0 != m_ViewerImplPtr->m_iIdleSetIndex)
	{
		g_source_remove(m_ViewerImplPtr->m_iIdleSetIndex);
		m_ViewerImplPtr->m_iIdleSetIndex = 0;
		m_ViewerImplPtr->SetImageIndex(m_ViewerImplPtr->m_ImageListPtr->GetCurrentIndex(), true);
	}

	m_ViewerImplPtr->m_bSlideShowRunning = true;
	m_ViewerImplPtr->m_bSlideShowPaused = false;
	m_ViewerImplPtr->HideSlideShowPausedPill();
	m_ViewerImplPtr->TriggerPlayPauseAnimation(true);
	m_ViewerImplPtr->m_SlideShowState = ViewerImpl::SLIDESHOW_STATE_ADVANCE;
	m_ViewerImplPtr->m_iSlideShowWaitCount = 0;

	if (bTransition)
	{
		quiver_image_view_set_enable_transitions(QUIVER_IMAGE_VIEW(m_ViewerImplPtr->m_pImageView),TRUE);
	}


	if (!m_ViewerImplPtr->m_iTimeoutSlideshowID && m_ViewerImplPtr->m_ImageListPtr->GetSize() >= 2 )
	{
		int duration = m_ViewerImplPtr->m_iSlideShowDuration;
		if (m_ViewerImplPtr->IsVideo())
		{
			if (m_ViewerImplPtr->IsPlaying())
				m_ViewerImplPtr->m_SlideShowState = Viewer::ViewerImpl::SLIDESHOW_STATE_PLAYING_VIDEO;
			else
				m_ViewerImplPtr->m_SlideShowState = Viewer::ViewerImpl::SLIDESHOW_STATE_PLAY_VIDEO;
			duration = SLIDESHOW_WAIT_DURATION;
		}

		m_ViewerImplPtr->m_iTimeoutSlideshowID = g_timeout_add(duration,timeout_advance_slideshow, m_ViewerImplPtr.get());

		QuiverUtils::ToggleActionSetState(ACTION_VIEWER_SLIDESHOW, TRUE);
		EmitSlideShowStartedEvent();
	}
	else if (m_ViewerImplPtr->m_ImageListPtr->GetSize() < 2)
	{
		SlideShowStop();
	}

	m_ViewerImplPtr->UpdateSlideshowButton();
	m_ViewerImplPtr->UpdateNavigationControl();
	m_ViewerImplPtr->UpdateUI();
}


void Viewer::SlideShowStop()
{
	m_ViewerImplPtr->SlideShowStop();
}

void Viewer::SlideShowPause()
{
	if (!m_ViewerImplPtr->m_bSlideShowRunning || m_ViewerImplPtr->m_bSlideShowPaused)
		return;

	m_ViewerImplPtr->m_bSlideShowPaused = true;
	m_ViewerImplPtr->ShowSlideShowPausedPill();
	m_ViewerImplPtr->m_SlideShowPrePauseState = m_ViewerImplPtr->m_SlideShowState;
	m_ViewerImplPtr->m_SlideShowState = ViewerImpl::SLIDESHOW_STATE_PAUSED;
	if (0 != m_ViewerImplPtr->m_iTimeoutSlideshowID)
	{
		g_source_remove(m_ViewerImplPtr->m_iTimeoutSlideshowID);
		m_ViewerImplPtr->m_iTimeoutSlideshowID = 0;
	}
	if (m_ViewerImplPtr->IsVideo() && m_ViewerImplPtr->IsPlaying())
	{
		m_ViewerImplPtr->PlayPauseVideo();
	}
	m_ViewerImplPtr->UpdateSlideshowButton();
}

void Viewer::SlideShowResume()
{
	if (!m_ViewerImplPtr->m_bSlideShowRunning || !m_ViewerImplPtr->m_bSlideShowPaused)
		return;

	m_ViewerImplPtr->m_bSlideShowPaused = false;
	m_ViewerImplPtr->HideSlideShowPausedPill();
	m_ViewerImplPtr->m_SlideShowState = m_ViewerImplPtr->m_SlideShowPrePauseState;
	if (m_ViewerImplPtr->m_SlideShowState == ViewerImpl::SLIDESHOW_STATE_PAUSED)
	{
		m_ViewerImplPtr->m_SlideShowState = ViewerImpl::SLIDESHOW_STATE_ADVANCE;
	}

	if (m_ViewerImplPtr->IsVideo())
	{
		/* Resume on a video always seats the machine in PLAYING_VIDEO so the
		 * show stays on this clip until it actually ends (this also covers
		 * the case where the user paused on an image, navigated to a video
		 * and started playback manually, then resumes). */
		if (!m_ViewerImplPtr->IsPlaying())
		{
			m_ViewerImplPtr->PlayPauseVideo();
		}
		m_ViewerImplPtr->m_SlideShowState = ViewerImpl::SLIDESHOW_STATE_PLAYING_VIDEO;
		m_ViewerImplPtr->m_iTimeoutSlideshowID = g_timeout_add(SLIDESHOW_WAIT_DURATION, timeout_advance_slideshow, m_ViewerImplPtr.get());
	}
	else
	{
		/* Resume on an image: restart the dwell timer on the current image so
		 * the show stays put for a full slide duration instead of jumping
		 * straight to the next item. */
		m_ViewerImplPtr->m_iSlideShowWaitCount = 0;
		m_ViewerImplPtr->m_SlideShowState = ViewerImpl::SLIDESHOW_STATE_ADVANCE;
		m_ViewerImplPtr->m_iTimeoutSlideshowID = g_timeout_add(m_ViewerImplPtr->m_iSlideShowDuration, timeout_advance_slideshow, m_ViewerImplPtr.get());
	}
	m_ViewerImplPtr->UpdateSlideshowButton();
}

void Viewer::SlideShowTogglePause()
{
	if (!m_ViewerImplPtr->m_bSlideShowRunning)
	{
		SlideShowStart();
	}
	else if (m_ViewerImplPtr->m_bSlideShowPaused)
	{
		SlideShowResume();
	}
	else
	{
		SlideShowPause();
	}
}

bool Viewer::IsSlideShowRunning() const
{
	return m_ViewerImplPtr->IsSlideShowRunning();
}

bool Viewer::IsSlideShowPaused() const
{
	return m_ViewerImplPtr->IsSlideShowPaused();
}

GtkWidget* Viewer::GetViewerOverlayBar() const
{
	return m_ViewerImplPtr ? m_ViewerImplPtr->m_pViewerOverlayBar : NULL;
}

GtkWidget* Viewer::GetTimelineRow() const
{
	return m_ViewerImplPtr ? m_ViewerImplPtr->m_pTimelineRow : NULL;
}

GtkWidget* Viewer::GetCenterPlayButton() const
{
	return m_ViewerImplPtr ? m_ViewerImplPtr->m_pCenterPlayBtn : NULL;
}

GtkWidget* Viewer::GetViewModeMenuPopover() const
{
	return m_ViewerImplPtr ? m_ViewerImplPtr->m_pViewModeMenuPopover : NULL;
}

GtkWidget* Viewer::GetImageView() const
{
	return m_ViewerImplPtr ? m_ViewerImplPtr->m_pImageView : NULL;
}

bool Viewer::IsVideoZoomAnchorCenter() const
{
	return m_ViewerImplPtr ? m_ViewerImplPtr->m_bVideoZoomAnchorCenter : false;
}

void Viewer::ToggleMute()
{
	if (m_ViewerImplPtr)
	{
		m_ViewerImplPtr->ToggleMute();
	}
}

bool Viewer::IsMuted() const
{
	return m_ViewerImplPtr ? m_ViewerImplPtr->IsMuted() : false;
}

void Viewer::SetMuted(bool bMute)
{
	if (m_ViewerImplPtr)
	{
		m_ViewerImplPtr->SetMuted(bMute);
	}
}

void Viewer::RotateVideo(bool clockwise)
{
	if (m_ViewerImplPtr)
	{
		m_ViewerImplPtr->RotateVideo(clockwise);
	}
}

int Viewer::GetVideoUserRotation() const
{
	return m_ViewerImplPtr ? m_ViewerImplPtr->GetVideoUserRotation() : 0;
}

void Viewer::SetVideoZoom(double zoom)
{
	if (m_ViewerImplPtr)
	{
		m_ViewerImplPtr->SetVideoZoom(zoom);
	}
}

double Viewer::GetVideoZoom() const
{
	return m_ViewerImplPtr ? m_ViewerImplPtr->m_dVideoZoom : 1.0;
}

bool Viewer::IsVideoPanSlowdownActive() const
{
	return m_ViewerImplPtr ? m_ViewerImplPtr->m_bVideoPanSlowdownActive : false;
}

double Viewer::GetVideoPanVelocityX() const
{
	return m_ViewerImplPtr ? m_ViewerImplPtr->m_dVideoPanVelX : 0.0;
}

double Viewer::GetVideoPanVelocityY() const
{
	return m_ViewerImplPtr ? m_ViewerImplPtr->m_dVideoPanVelY : 0.0;
}

double Viewer::GetVideoPanX() const
{
	return m_ViewerImplPtr ? m_ViewerImplPtr->m_dVideoPanX : 0.0;
}

double Viewer::GetVideoPanY() const
{
	return m_ViewerImplPtr ? m_ViewerImplPtr->m_dVideoPanY : 0.0;
}

bool Viewer::IsVideoPreviewPanSource() const
{
	return m_ViewerImplPtr ? m_ViewerImplPtr->IsVideoPreviewPanSource() : false;
}

double Viewer::GetVideoPanRangeX() const
{
	return m_ViewerImplPtr ? m_ViewerImplPtr->m_dVideoPanRangeX : 0.0;
}

double Viewer::GetVideoPanRangeY() const
{
	return m_ViewerImplPtr ? m_ViewerImplPtr->m_dVideoPanRangeY : 0.0;
}

QuiverImageViewMode Viewer::GetVideoViewMode() const
{
	/* What the viewer is showing, which is the mode the user picked: a picture
	 * that is zoomed away from the size its mode shows it at is still that
	 * mode, and saying otherwise is how a zoom came to look like a change of
	 * view mode. */
	return m_ViewerImplPtr ? m_ViewerImplPtr->GetChosenViewMode() : QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW;
}

double Viewer::GetVideoPanFractionX() const
{
	return m_ViewerImplPtr ? m_ViewerImplPtr->m_fVideoPanFX : 0.5;
}

double Viewer::GetVideoPanFractionY() const
{
	return m_ViewerImplPtr ? m_ViewerImplPtr->m_fVideoPanFY : 0.5;
}

void Viewer::SetVideoPanFraction(double x, double y)
{
	if (!m_ViewerImplPtr)
		return;
	/* a centre cannot sit closer to an edge than half the visible part, or the
	 * visible part would hang off the frame; where it does not fit at all the
	 * frame is whole on screen and the centre is the centre */
	gdouble centre_x = CLAMP(x, 0.0, 1.0);
	gdouble centre_y = CLAMP(y, 0.0, 1.0);
	const gdouble view_x = m_ViewerImplPtr->m_dVideoViewW;
	const gdouble view_y = m_ViewerImplPtr->m_dVideoViewH;
	const gdouble disp_x = m_ViewerImplPtr->m_dVideoPreviewDispW;
	const gdouble disp_y = m_ViewerImplPtr->m_dVideoPreviewDispH;
	if (disp_x > 0. && view_x > 0. && view_x < disp_x)
		centre_x = CLAMP(centre_x, view_x / (2. * disp_x), 1. - view_x / (2. * disp_x));
	else if (disp_x > 0. && view_x >= disp_x)
		centre_x = 0.5;
	if (disp_y > 0. && view_y > 0. && view_y < disp_y)
		centre_y = CLAMP(centre_y, view_y / (2. * disp_y), 1. - view_y / (2. * disp_y));
	else if (disp_y > 0. && view_y >= disp_y)
		centre_y = 0.5;
	m_ViewerImplPtr->m_fVideoPanFX = centre_x;
	m_ViewerImplPtr->m_fVideoPanFY = centre_y;
}

bool Viewer::CanVideoPan() const
{
	return m_ViewerImplPtr ? m_ViewerImplPtr->CanVideoPan() : false;
}

void Viewer::StartVideoPanSlowdown()
{
	if (m_ViewerImplPtr)
	{
		m_ViewerImplPtr->StartVideoPanSlowdown();
	}
}

void Viewer::StopVideoPanSlowdown()
{
	if (m_ViewerImplPtr)
	{
		m_ViewerImplPtr->StopVideoPanSlowdown();
	}
}

void Viewer::RecordVideoPanSample(double dx, double dy, double dt)
{
	if (m_ViewerImplPtr)
	{
		m_ViewerImplPtr->RecordVideoPanSample(dx, dy, dt);
	}
}

bool Viewer::IsVideoPlaying() const
{
	return m_ViewerImplPtr ? m_ViewerImplPtr->IsPlaying() : false;
}

bool Viewer::IsPointOverControlsOrFilmstrip(double x, double y) const
{
	if (!m_ViewerImplPtr) return false;
	GtkWidget *area = m_ViewerImplPtr->m_pVideoFixed ? m_ViewerImplPtr->m_pVideoFixed : m_ViewerImplPtr->m_pImageView;
	return m_ViewerImplPtr->IsPointOverControlsOrFilmstrip(area, x, y);
}


/*
// FIXME: remove
GtkTableChild * GetGtkTableChild(GtkTable * table,GtkWidget	*widget_to_get)
{
	GtkTableChild *table_child = NULL;
	GList *list = gtk_container_get_children(GTK_CONTAINER(table));
	for (; list; list = list->next)
	{
		table_child = (GtkTableChild*)list->data;
		if (table_child->widget == widget_to_get)
			break;
	}
	g_list_free(list);
	return table_child;
}
*/


static gulong n_cells_callback(QuiverIconView *iconview, gpointer user_data)
{ (void)iconview; 
	Viewer::ViewerImpl* pViewerImpl = (Viewer::ViewerImpl*)user_data;
	return pViewerImpl->m_ImageListPtr->GetSize();
}

#if HAVE_GDK_PIXBUF
static GdkPixbuf* icon_pixbuf_callback(QuiverIconView *iconview, gulong cell, gpointer user_data)
{
	Viewer::ViewerImpl* pViewerImpl = (Viewer::ViewerImpl*)user_data;
	QuiverFile f = pViewerImpl->m_ImageListPtr->Get(cell);

	guint width, height;
	quiver_icon_view_get_icon_size(iconview,&width, &height);
	return f.GetIcon(width,height);
}
#endif

static GdkTexture* icon_texture_callback(QuiverIconView *iconview, gulong cell, gpointer user_data)
{
	Viewer::ViewerImpl* pViewerImpl = (Viewer::ViewerImpl*)user_data;
	QuiverFile f = pViewerImpl->m_ImageListPtr->Get(cell);

	guint width, height;
	quiver_icon_view_get_icon_size(iconview,&width, &height);
	return f.GetIconTexture(width,height);
}


static gboolean thumbnail_loader_update_list (gpointer data)
{
	Viewer::ViewerImpl* pViewerImpl = (Viewer::ViewerImpl*)data;	
	pViewerImpl->m_ThumbnailLoader.UpdateList();
	pViewerImpl->m_iTimeoutUpdateListID = 0;
	return FALSE;
}

#if HAVE_GDK_PIXBUF
static GdkPixbuf* thumbnail_pixbuf_callback(QuiverIconView *iconview, gulong cell, gint* actual_width, gint* actual_height, gpointer user_data)

{
	Viewer::ViewerImpl* pViewerImpl = (Viewer::ViewerImpl*)user_data;	

	GdkPixbuf *pixbuf = NULL;
	gboolean need_new_thumb = TRUE;
	
	guint width, height;
	guint thumb_width, thumb_height;
	guint bound_width, bound_height;
	quiver_icon_view_get_icon_size(iconview,&width,&height);
	
	pixbuf = pViewerImpl->m_ThumbnailCache.GetPixbuf(pViewerImpl->m_ImageListPtr->Get(cell).GetURI());

	if (pixbuf)
	{
		*actual_width = pViewerImpl->m_ImageListPtr->Get(cell).GetWidth();
		*actual_height = pViewerImpl->m_ImageListPtr->Get(cell).GetHeight();

		if (4 < pViewerImpl->m_ImageListPtr->Get(cell).GetOrientation())
		{
			swap(*actual_width,*actual_height);
		}

		thumb_width = gdk_pixbuf_get_width(pixbuf);
		thumb_height = gdk_pixbuf_get_height(pixbuf);

		bound_width = *actual_width;
		bound_height = *actual_height;
		quiver_rect_get_bound_size(width,height, &bound_width,&bound_height,FALSE);

		if (bound_width == thumb_width && bound_height == thumb_height)
		{
			need_new_thumb = FALSE;
		}
		else if (thumb_width >= bound_width && thumb_height >= bound_height)
		{
			need_new_thumb = FALSE;
		}
	}
	
	if (need_new_thumb)
	{
		// add a timeout
		pViewerImpl->QueueIconViewUpdate();
	}
	
	return pixbuf;
}
#endif

static GdkTexture* thumbnail_texture_callback(QuiverIconView *iconview, gulong cell, gint* actual_width, gint* actual_height, gpointer user_data)
{
	Viewer::ViewerImpl* pViewerImpl = (Viewer::ViewerImpl*)user_data;	

	GdkTexture *texture = NULL;
	gboolean need_new_thumb = TRUE;
	
	guint width, height;
	guint thumb_width, thumb_height;
	guint bound_width, bound_height;
	quiver_icon_view_get_icon_size(iconview,&width,&height);
	
	texture = pViewerImpl->m_ThumbnailCache.GetTexture(pViewerImpl->m_ImageListPtr->Get(cell).GetURI());

	if (texture)
	{
		*actual_width = pViewerImpl->m_ImageListPtr->Get(cell).GetWidth();
		*actual_height = pViewerImpl->m_ImageListPtr->Get(cell).GetHeight();

		if (4 < pViewerImpl->m_ImageListPtr->Get(cell).GetOrientation())
		{
			swap(*actual_width,*actual_height);
		}

		thumb_width = gdk_texture_get_width(texture);
		thumb_height = gdk_texture_get_height(texture);

		bound_width = *actual_width;
		bound_height = *actual_height;
		quiver_rect_get_bound_size(width,height, &bound_width,&bound_height,FALSE);

		if (bound_width == thumb_width && bound_height == thumb_height)
		{
			need_new_thumb = FALSE;
		}
		else if (thumb_width >= bound_width && thumb_height >= bound_height)
		{
			need_new_thumb = FALSE;
		}
	}
	
	if (need_new_thumb)
	{
		// add a timeout
		pViewerImpl->QueueIconViewUpdate();
	}
	
	return texture;
}

static GdkTexture* filmstrip_texture_callback(QuiverIconView* iconview, gulong cell,
	gint thumb_natural_w, gint thumb_natural_h,
	gint thumb_drawn_w, gint thumb_drawn_h,
	QuiverIconViewFilmstripSide side, gpointer user_data)
{ (void)iconview; (void)thumb_natural_w; (void)thumb_natural_h; (void)side;
	Viewer::ViewerImpl* pViewerImpl = (Viewer::ViewerImpl*)user_data;
	QuiverFile f = pViewerImpl->m_ImageListPtr->Get(cell);
	if (!f.IsVideo())
		return NULL;

	gint thumb_drawn_max = MAX(thumb_drawn_w, thumb_drawn_h);
	std::string path = QuiverUtils::GetFilmstripPath(thumb_drawn_max);
	GdkTexture* texture = pViewerImpl->m_FilmstripCache.GetTexture(path);
	if (NULL == texture)
	{
		texture = gdk_texture_new_from_filename(path.c_str(), NULL);
		if (NULL != texture)
		{
			pViewerImpl->m_FilmstripCache.AddTexture(path, texture);
		}
	}

	return texture;
}

void Viewer::ViewerImpl::QueueIconViewUpdate(int timeout)
{
	if (!m_iTimeoutUpdateListID)
	{
		m_iTimeoutUpdateListID = g_timeout_add(timeout,thumbnail_loader_update_list,this);
	}
}

void Viewer::ViewerImpl::SlideShowStop(bool bEmitStopEvent, bool bUpdateUI)
{
	bool wasRunning = m_bSlideShowRunning;
	m_bSlideShowRunning = false;
	m_bSlideShowPaused = false;

	HideSlideShowPausedPill();

	if (m_pImageView != NULL && G_IS_OBJECT(m_pImageView) && QUIVER_IS_IMAGE_VIEW(m_pImageView))
	{
		quiver_image_view_set_enable_transitions(QUIVER_IMAGE_VIEW(m_pImageView), FALSE);
	}

	if (0 != m_iTimeoutSlideshowID)
	{

		g_source_remove (m_iTimeoutSlideshowID);
		m_iTimeoutSlideshowID = 0;
	}

	QuiverUtils::ToggleActionSetState(ACTION_VIEWER_SLIDESHOW, FALSE);
	if (bUpdateUI)
	{
		UpdateSlideshowButton();
	}

	if (bEmitStopEvent && wasRunning)
	{
		m_pViewer->EmitSlideShowStoppedEvent();
	}

	if (bUpdateUI)
	{
		UpdateNavigationControl();
		UpdateUI();
	}
}

static void update_single_slideshow_btn(GtkWidget *btn, bool isRunning, Viewer::ViewerImpl *impl)
{
	if (!btn) return;
	gtk_button_set_icon_name(GTK_BUTTON(btn), "display-projector-symbolic");
	std::string tooltip = ShortcutManager::GetInstance().GetTooltipForAction("SlideShow", "Slideshow");
	gtk_widget_set_tooltip_text(btn, tooltip.c_str());
	if (GTK_IS_TOGGLE_BUTTON(btn))
	{
		g_signal_handlers_block_by_func(btn, (gpointer)viewer_submenu_slideshow_cb, impl);
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btn), isRunning);
		g_signal_handlers_unblock_by_func(btn, (gpointer)viewer_submenu_slideshow_cb, impl);
	}
	if (isRunning)
		gtk_widget_add_css_class(btn, "speed-active");
	else
		gtk_widget_remove_css_class(btn, "speed-active");
}

void Viewer::ViewerImpl::UpdateSlideshowButton()
{
	update_single_slideshow_btn(m_pViewerSlideshowBtn, m_bSlideShowRunning, this);
	update_single_slideshow_btn(m_pViewerVideoSlideshowBtn, m_bSlideShowRunning, this);
}

void Viewer::ViewerImpl::UpdateHUDTooltips()
{
	const ShortcutManager &sm = ShortcutManager::GetInstance();
	if (m_pViewerPrevBtn)
		gtk_widget_set_tooltip_text(m_pViewerPrevBtn, sm.GetTooltipForAction("ImagePrevious", "Previous Image").c_str());
	if (m_pViewerNextBtn)
		gtk_widget_set_tooltip_text(m_pViewerNextBtn, sm.GetTooltipForAction("ImageNext", "Next Image").c_str());
	if (m_pPlayButton)
		gtk_widget_set_tooltip_text(m_pPlayButton, sm.GetTooltipForAction("VideoPlay", "Play / Pause").c_str());
	if (m_pCenterPlayBtn)
		gtk_widget_set_tooltip_text(m_pCenterPlayBtn, (sm.GetTooltipForAction("VideoPlay", "Play Video") + " / Click").c_str());
	if (m_pViewerRotateCcwBtn)
		gtk_widget_set_tooltip_text(m_pViewerRotateCcwBtn, sm.GetTooltipForAction("RotateCCW", "Rotate Counter-Clockwise").c_str());
	if (m_pRewindBtn)
		gtk_widget_set_tooltip_text(m_pRewindBtn, sm.GetTooltipForAction("VideoSkipBack", "Skip Backwards 10s").c_str());
	if (m_pViewerRotateCwBtn)
		gtk_widget_set_tooltip_text(m_pViewerRotateCwBtn, sm.GetTooltipForAction("RotateCW", "Rotate Clockwise").c_str());
	if (m_pFfBtn)
		gtk_widget_set_tooltip_text(m_pFfBtn, sm.GetTooltipForAction("VideoSkipForward", "Skip Forward 10s").c_str());
	if (m_pViewerZoomOutBtn)
		gtk_widget_set_tooltip_text(m_pViewerZoomOutBtn, sm.GetTooltipForAction("ZoomOut", "Zoom Out").c_str());
	if (m_pViewerZoomFitBtn)
		gtk_widget_set_tooltip_text(m_pViewerZoomFitBtn, sm.GetTooltipForAction("ZoomFitStretch", "Zoom to Fit Window (Stretched)").c_str());
	if (m_pViewerZoomInBtn)
		gtk_widget_set_tooltip_text(m_pViewerZoomInBtn, sm.GetTooltipForAction("ZoomIn", "Zoom In").c_str());
	if (m_pSnapBtn)
		gtk_widget_set_tooltip_text(m_pSnapBtn, sm.GetTooltipForAction("VideoSnapshot", "Take Snapshot").c_str());
	if (m_pViewerFullscreenBtn && m_pOverlay)
	{
		GtkWidget *root = GTK_WIDGET(gtk_widget_get_root(m_pOverlay));
		bool bFS = root && GTK_IS_WINDOW(root) && gtk_window_is_fullscreen(GTK_WINDOW(root));
		std::string fs_tip = sm.GetTooltipForAction("FullScreen", bFS ? "Exit Fullscreen" : "Fullscreen");
		if (bFS)
			fs_tip += " / Middle Click / Esc";
		else
			fs_tip += " / Middle Click";
		gtk_widget_set_tooltip_text(m_pViewerFullscreenBtn, fs_tip.c_str());
	}
	UpdateSlideshowButton();
}

void Viewer::ViewerImpl::UpdateCenterPlayButtonVisibility()
{
	if (!m_pCenterPlayBtn) return;
	bool show = IsVideo() && !m_bVideoPlaybackStarted;
	gtk_widget_set_visible(m_pCenterPlayBtn, show);
}

void Viewer::ViewerImpl::UpdateHUDPosition()
{
	if (!m_pViewerOverlayBar || !m_pTimelineRow)
		return;

	PreferencesPtr prefs = Preferences::GetInstance();
	int hudPos = prefs->GetInteger(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_HUD_POSITION, HUD_POS_BOTTOM);
	bool bFilmstripVisible = prefs->GetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_FILMSTRIP_SHOW, true);
	if (QuiverUtils::GetAction(ACTION_VIEWER_VIEW_FILM_STRIP) != NULL)
	{
		bFilmstripVisible = bFilmstripVisible && QuiverUtils::ToggleActionGetActive(ACTION_VIEWER_VIEW_FILM_STRIP);
	}
	int filmstripPos = prefs->GetInteger(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_FILMSTRIP_POSITION, FSTRIP_POS_RIGHT);
	bool bOverlay = prefs->GetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_FILMSTRIP_OVERLAY, true);

	int offset = 0;
	if (bFilmstripVisible && bOverlay && m_pIconView)
	{
		int cell_h = quiver_icon_view_get_cell_height(QUIVER_ICON_VIEW(m_pIconView));
		if (cell_h <= 0)
			cell_h = 100;
		if (hudPos == HUD_POS_BOTTOM && filmstripPos == FSTRIP_POS_BOTTOM)
		{
			offset = cell_h + 10;
		}
		else if (hudPos == HUD_POS_TOP && filmstripPos == FSTRIP_POS_TOP)
		{
			offset = cell_h + 10;
		}
	}

	if (hudPos == HUD_POS_TOP)
	{
		gtk_widget_set_valign(m_pTimelineRow, GTK_ALIGN_START);
		gtk_widget_set_margin_top(m_pTimelineRow, 12 + offset);
		gtk_widget_set_margin_bottom(m_pTimelineRow, 0);

		gtk_widget_set_valign(m_pViewerOverlayBar, GTK_ALIGN_START);
		gtk_widget_set_margin_top(m_pViewerOverlayBar, 50 + offset);
		gtk_widget_set_margin_bottom(m_pViewerOverlayBar, 0);

		if (m_pVolumePopover)
			gtk_popover_set_position(GTK_POPOVER(m_pVolumePopover), GTK_POS_BOTTOM);
		if (m_pImageSubmenuBtn)
			gtk_menu_button_set_direction(GTK_MENU_BUTTON(m_pImageSubmenuBtn), GTK_ARROW_DOWN);
		if (m_pViewModeMenuBtn)
			viewer_view_mode_arrow_set_direction(m_pViewModeMenuBtn, false);
		viewer_view_mode_split_size_sep(m_pViewerZoomFitBtn, m_pViewModeSplitSep);
		if (m_pViewModeMenuPopover)
			gtk_popover_set_position(GTK_POPOVER(m_pViewModeMenuPopover), GTK_POS_BOTTOM);
		if (m_pVideoOptionsBtn)
			gtk_menu_button_set_direction(GTK_MENU_BUTTON(m_pVideoOptionsBtn), GTK_ARROW_DOWN);
		if (m_pVolumeButton)
			gtk_menu_button_set_direction(GTK_MENU_BUTTON(m_pVolumeButton), GTK_ARROW_DOWN);
	}
	else
	{
		gtk_widget_set_valign(m_pTimelineRow, GTK_ALIGN_END);
		gtk_widget_set_margin_bottom(m_pTimelineRow, 12 + offset);
		gtk_widget_set_margin_top(m_pTimelineRow, 0);

		gtk_widget_set_valign(m_pViewerOverlayBar, GTK_ALIGN_END);
		gtk_widget_set_margin_bottom(m_pViewerOverlayBar, 50 + offset);
		gtk_widget_set_margin_top(m_pViewerOverlayBar, 0);

		if (m_pVolumePopover)
			gtk_popover_set_position(GTK_POPOVER(m_pVolumePopover), GTK_POS_TOP);
		if (m_pImageSubmenuBtn)
			gtk_menu_button_set_direction(GTK_MENU_BUTTON(m_pImageSubmenuBtn), GTK_ARROW_UP);
		if (m_pViewModeMenuBtn)
			viewer_view_mode_arrow_set_direction(m_pViewModeMenuBtn, true);
		viewer_view_mode_split_size_sep(m_pViewerZoomFitBtn, m_pViewModeSplitSep);
		if (m_pViewModeMenuPopover)
			gtk_popover_set_position(GTK_POPOVER(m_pViewModeMenuPopover), GTK_POS_TOP);
		if (m_pVideoOptionsBtn)
			gtk_menu_button_set_direction(GTK_MENU_BUTTON(m_pVideoOptionsBtn), GTK_ARROW_UP);
		if (m_pVolumeButton)
			gtk_menu_button_set_direction(GTK_MENU_BUTTON(m_pVolumeButton), GTK_ARROW_UP);
	}

	UpdateNavControlPosition();
	/* the reserved width depends on where the pill row lives */
	ApplyNavControlMinSize(m_bNavControlShown);
}

void Viewer::ViewerImpl::UpdateNavControlPosition()
{
	if (!m_pNavControlPill)
		return;

	/* The nav control is anchored in the bottom-right corner with the same
	 * inset on both edges, regardless of where the filmstrip lives: it is a
	 * viewer overlay that only surfaces on pointer hover, so offsetting it
	 * for the filmstrip would only push it away from the corner the user
	 * asked for.  A vertical scrollbar occupying the rightmost strip of the
	 * grid is narrower than this inset, so the control never sits on it. */
	gtk_widget_set_margin_end(m_pNavControlPill, NAV_CONTROL_EDGE_MARGIN);
	gtk_widget_set_margin_bottom(m_pNavControlPill, NAV_CONTROL_EDGE_MARGIN);
}

bool Viewer::ViewerImpl::UpdateNavigationControlTexture()
{
	if (!m_pNavigationControl)
		return false;

	if (!m_ImageListPtr || m_ImageListPtr->GetSize() == 0)
	{
		quiver_navigation_control_set_texture(QUIVER_NAVIGATION_CONTROL(m_pNavigationControl), NULL);
		quiver_navigation_control_set_paintable(QUIVER_NAVIGATION_CONTROL(m_pNavigationControl), NULL, 0, 0);
		return false;
	}

	/* A video is drawn from the live miniature the pipeline feeds us, so a
	 * still poster is not needed - and looking one up on every zoom would
	 * stall the UI on video formats with no cached thumbnail.  The miniature
	 * paintable is only usable once it has actually received a frame: before
	 * that it hands out uninitialized memory, which the control would draw as
	 * garbage. */
	if (IsVideo())
	{
		if (m_pVideoRotatedPaintable == NULL)
			return false;
		if (!m_bVideoPreviewHasFrame)
		{
			/* No frame out of the pipeline yet, but the quick preview is on
			 * screen: it is the image view drawing the video's own thumbnail,
			 * so the control draws that - the same image, so its box lines up
			 * with what the user is looking at. */
			GdkTexture* preview_tex = IsVideoPreviewShowing()
				? quiver_image_view_get_texture(QUIVER_IMAGE_VIEW(m_pImageView))
				: NULL;
			quiver_navigation_control_set_texture(QUIVER_NAVIGATION_CONTROL(m_pNavigationControl), preview_tex);
			return preview_tex != NULL;
		}
		quiver_navigation_control_set_texture(QUIVER_NAVIGATION_CONTROL(m_pNavigationControl), NULL);
		return true;
	}

	QuiverFile f = m_ImageListPtr->GetCurrent();
	GdkTexture *nav_tex = NULL;
	if (f.HasThumbnail(128)) {
		nav_tex = f.GetThumbnailTexture(128);
	}
	if (NULL == nav_tex && f.HasThumbnail(256)) {
		nav_tex = f.GetThumbnailTexture(256);
	}
	if (NULL == nav_tex) {
		nav_tex = m_ThumbnailCache.GetTexture(f.GetURI());
	}
	if (NULL == nav_tex) {
		nav_tex = f.GetThumbnailTexture(128);
	}

	quiver_navigation_control_set_texture(QUIVER_NAVIGATION_CONTROL(m_pNavigationControl), nav_tex);
	if (NULL != nav_tex)
	{
		g_object_unref(nav_tex);
	}
	return NULL != nav_tex;
}

bool Viewer::ViewerImpl::IsNavPreviewEnabled() const
{
	PreferencesPtr prefs = Preferences::GetInstance();
	return prefs->GetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_NAV_CONTROL_VIDEO, false);
}

/* Forget everything that belongs to one particular video.  The miniature size,
 * the paintable the control draws and the viewport rectangle are per-video:
 * without this the second video would keep the first one's miniature size
 * (stretching the frame to the wrong aspect) and the control would keep showing
 * the previous frame. */
void Viewer::ViewerImpl::ResetVideoPreviewViewState()
{
	if (m_pNavigationControl != NULL)
	{
		quiver_navigation_control_set_view_area_normalized(
			QUIVER_NAVIGATION_CONTROL(m_pNavigationControl), 0.0, 0.0, 0.0, 0.0);
		quiver_navigation_control_set_paintable(QUIVER_NAVIGATION_CONTROL(m_pNavigationControl),
			NULL, 0, 0);
		quiver_navigation_control_set_texture(QUIVER_NAVIGATION_CONTROL(m_pNavigationControl), NULL);
	}
	m_dVideoPreviewViewX = 0.0;
	m_dVideoPreviewViewY = 0.0;
	m_dVideoPreviewViewW = 0.0;
	m_dVideoPreviewViewH = 0.0;
	m_dVideoPreviewDispW = 0.0;
	m_dVideoPreviewDispH = 0.0;
	m_bVideoPreviewHasFrame = FALSE;
	m_fVideoZoomSx = -999.0f;
	m_fVideoZoomSy = -999.0f;
	m_fVideoZoomTx = -999.0f;
	m_fVideoZoomTy = -999.0f;
}

/* Tell the control how big the frame is.  The miniature scales the frame to the
 * size the nav control draws it at.  dispW/dispH already account for display
 * orientation and user rotation. */
void Viewer::ViewerImpl::ConfigureVideoPreviewScale(gint dispW, gint dispH)
{
	if (m_pNavigationControl == NULL || m_pVideoRotatedPaintable == NULL)
		return;
	if (dispW <= 0 || dispH <= 0)
		return;

	quiver_navigation_control_set_paintable(QUIVER_NAVIGATION_CONTROL(m_pNavigationControl),
		GDK_PAINTABLE(m_pVideoRotatedPaintable), dispW, dispH);
	UpdateVideoPreviewViewArea();
}

/* The miniature shows the whole frame, so mark the part of it the window
 * actually shows.  The video zoom lives in GTK scaling rather than in the
 * scrollable adjustments, so the rectangle comes from the zoom/pan state
 * computed in ApplyVideoZoom() and is expressed in normalized display
 * coordinates (the miniature is drawn through the same rotation as the main
 * view, so a quarter turn swaps the axes and a half turn mirrors them). */
void Viewer::ViewerImpl::UpdateVideoPreviewViewArea()
{
	if (m_pNavigationControl == NULL)
		return;

	if (!IsVideo() || m_pVideoRotatedPaintable == NULL)
	{
		quiver_navigation_control_set_view_area_normalized(
			QUIVER_NAVIGATION_CONTROL(m_pNavigationControl), 0.0, 0.0, 0.0, 0.0);
		return;
	}

	quiver_navigation_control_set_view_area_normalized(
		QUIVER_NAVIGATION_CONTROL(m_pNavigationControl),
		m_dVideoPreviewViewX, m_dVideoPreviewViewY,
		m_dVideoPreviewViewW, m_dVideoPreviewViewH);
}

/* Detach the miniature from the nav control.  Because the nav control shares
 * the main view's m_pVideoRotatedPaintable directly, only the control's reference
 * is released here. */
void Viewer::ViewerImpl::ReleaseVideoPreview()
{
	if (m_pNavigationControl != NULL)
	{
		quiver_navigation_control_set_view_area_normalized(
			QUIVER_NAVIGATION_CONTROL(m_pNavigationControl), 0.0, 0.0, 0.0, 0.0);
		quiver_navigation_control_set_paintable(QUIVER_NAVIGATION_CONTROL(m_pNavigationControl),
			NULL, 0, 0);
	}
	m_dVideoPreviewViewX = 0.0;
	m_dVideoPreviewViewY = 0.0;
	m_dVideoPreviewViewW = 0.0;
	m_dVideoPreviewViewH = 0.0;
	m_dVideoPreviewDispW = 0.0;
	m_dVideoPreviewDispH = 0.0;
	m_bVideoPreviewHasFrame = FALSE;
}

GstElement* Viewer::ViewerImpl::BuildVideoZoomBin()
{
	ReleaseVideoPreview();

	m_pVideoZoomInput = NULL;
	m_pVideoCrop = NULL;
	m_pVideoZoomConvert = NULL;
	m_pVideoZoomScaler = NULL;
	m_pVideoZoomCaps = NULL;
	m_pVideoZoomInputCaps = NULL;
	m_VideoZoomType = VIDEO_ZOOM_GL;

	GstElement *raw_sink = gst_element_factory_make("gtk4paintablesink", NULL);
	if (raw_sink != NULL)
	{
		if (g_object_class_find_property(G_OBJECT_GET_CLASS(raw_sink), "force-aspect-ratio") != NULL)
			g_object_set(G_OBJECT(raw_sink), "force-aspect-ratio", TRUE, NULL);
	}
	else
	{
		raw_sink = gst_element_factory_make("gtk4sink", NULL);
		if (raw_sink != NULL && g_object_class_find_property(G_OBJECT_GET_CLASS(raw_sink), "force-aspect-ratio") != NULL)
			g_object_set(G_OBJECT(raw_sink), "force-aspect-ratio", TRUE, NULL);
	}

	if (raw_sink == NULL)
	{
		// Fallback for headless environments without gtk4paintablesink
		raw_sink = gst_element_factory_make("fakesink", "fallback_fakesink");
	}

	if (raw_sink == NULL)
	{
		return NULL;
	}

	if (m_pVideoPaintable != NULL && G_IS_OBJECT(m_pVideoPaintable))
	{
		g_signal_handlers_disconnect_by_data(m_pVideoPaintable, this);
		g_object_unref(m_pVideoPaintable);
		m_pVideoPaintable = NULL;
	}

	if (g_object_class_find_property(G_OBJECT_GET_CLASS(raw_sink), "paintable") != NULL)
	{
		g_object_get(G_OBJECT(raw_sink), "paintable", &m_pVideoPaintable, NULL);
		if (m_pVideoPaintable != NULL)
		{
			g_signal_connect(m_pVideoPaintable, "invalidate-contents", G_CALLBACK(video_paintable_invalidated_cb), this);
		}
	}

	if (m_pVideoRotatedPaintable != NULL)
	{
		quiver_rotated_paintable_set_underlying(m_pVideoRotatedPaintable, m_pVideoPaintable);
	}

	if (g_object_class_find_property(G_OBJECT_GET_CLASS(raw_sink), "qos") != NULL)
	{
		g_object_set(G_OBJECT(raw_sink), "qos", FALSE, NULL);
	}

	GstElement *zoombin = gst_bin_new("videozoom");
	GstElement *first_element = NULL;

	GstElement *glupload_zoom = gst_element_factory_make("glupload", "zoomupload");
	GstElement *glcolorconvert = gst_element_factory_make("glcolorconvert", "zoomcolorconvert");
	m_pVideoZoomScaler = gst_element_factory_make("gltransformation", "zoomtransform");

	if (m_pVideoZoomScaler != NULL)
	{
		g_object_set(G_OBJECT(m_pVideoZoomScaler), "ortho", TRUE, NULL);
		if (g_object_class_find_property(G_OBJECT_GET_CLASS(m_pVideoZoomScaler), "qos") != NULL)
			g_object_set(G_OBJECT(m_pVideoZoomScaler), "qos", FALSE, NULL);
	}
	if (glcolorconvert != NULL && g_object_class_find_property(G_OBJECT_GET_CLASS(glcolorconvert), "qos") != NULL)
	{
		g_object_set(G_OBJECT(glcolorconvert), "qos", FALSE, NULL);
	}

	GstElement *chain[4] = { glupload_zoom, glcolorconvert, m_pVideoZoomScaler, raw_sink };
	gboolean bChainOk = TRUE;
	guint n_added = 0;
	GstElement *added_elements[4];
	for (guint i = 0; i < 4; ++i)
	{
		if (chain[i] == NULL || !gst_bin_add(GST_BIN(zoombin), chain[i]))
		{
			bChainOk = FALSE;
			break;
		}
		added_elements[n_added++] = chain[i];
	}
	if (bChainOk)
	{
		for (guint i = 0; i + 1 < 4; ++i)
		{
			if (!gst_element_link(chain[i], chain[i + 1]))
			{
				bChainOk = FALSE;
				break;
			}
		}
	}
	if (bChainOk)
	{
		first_element = glupload_zoom;
	}

	if (bChainOk)
	{
		auto attach_alloc_probe = [this](GstElement *element, const char *pad_name) {
			if (element == NULL) return;
			GstPad *pad = gst_element_get_static_pad(element, pad_name);
			if (pad != NULL)
			{
				gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_QUERY_DOWNSTREAM,
					video_allocation_query_probe, this, NULL);
				gst_object_unref(pad);
			}
		};

		attach_alloc_probe(glupload_zoom, "sink");
		attach_alloc_probe(glcolorconvert, "sink");
		attach_alloc_probe(m_pVideoZoomScaler, "sink");

		bool bGlitchDebug = is_glitch_debug_enabled();

		if (first_element != NULL)
		{
			GstPad *crop_sinkpad = gst_element_get_static_pad(first_element, "sink");
			if (crop_sinkpad != NULL)
			{
				GstPadProbeType ptype = bGlitchDebug ?
					(GstPadProbeType)(GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM | GST_PAD_PROBE_TYPE_BUFFER) :
					GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM;
				gst_pad_add_probe(crop_sinkpad, ptype, video_crop_pad_probe, this, NULL);
				gst_pad_add_probe(crop_sinkpad, GST_PAD_PROBE_TYPE_EVENT_UPSTREAM, video_zoom_reconfigure_probe, this, NULL);

				gst_element_add_pad(zoombin, gst_ghost_pad_new("sink", crop_sinkpad));
				gst_object_unref(crop_sinkpad);
			}
		}

		GstPad *raw_sink_pad = gst_element_get_static_pad(raw_sink, "sink");
		if (raw_sink_pad != NULL)
		{
			if (bGlitchDebug)
			{
				gst_pad_add_probe(raw_sink_pad,
					(GstPadProbeType)(GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM | GST_PAD_PROBE_TYPE_BUFFER),
					video_sink_glitch_probe_cb, this, NULL);
			}
			gst_pad_add_probe(raw_sink_pad,
				GST_PAD_PROBE_TYPE_QUERY_DOWNSTREAM,
				video_allocation_query_probe, this, NULL);
			gst_object_unref(raw_sink_pad);
		}

		video_glitch_log_event("=== Quiver Video Active (zoombin) ===");
		if (bGlitchDebug)
		{
			std::cerr << "\033[1;36m[QUIVER GLITCH DETECTOR ACTIVE] Probing Decoder Output and Display Sink. Logging to quiver_glitch_log.txt\033[0m\n";
		}

		return zoombin;
	}
	else
	{
		g_object_ref(raw_sink);
		for (guint i = 0; i < n_added; ++i)
		{
			gst_bin_remove(GST_BIN(zoombin), added_elements[i]);
		}
		gst_object_unref(zoombin);
		if (m_pVideoZoomScaler != NULL)
			gst_object_unref(m_pVideoZoomScaler);
		m_pVideoCrop = NULL;
		m_pVideoZoomConvert = NULL;
		m_pVideoZoomScaler = NULL;
		m_pVideoZoomCaps = NULL;
		m_pVideoZoomInputCaps = NULL;
		m_VideoZoomType = VIDEO_ZOOM_SOFTWARE;

		bool bGlitchDebug = is_glitch_debug_enabled();
		GstPad *raw_sink_pad = gst_element_get_static_pad(raw_sink, "sink");
		if (raw_sink_pad != NULL)
		{
			if (bGlitchDebug)
			{
				gst_pad_add_probe(raw_sink_pad,
					(GstPadProbeType)(GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM | GST_PAD_PROBE_TYPE_BUFFER),
					video_sink_glitch_probe_cb, this, NULL);
			}
			gst_pad_add_probe(raw_sink_pad,
				GST_PAD_PROBE_TYPE_QUERY_DOWNSTREAM,
				video_allocation_query_probe, this, NULL);
			gst_object_unref(raw_sink_pad);
		}

		video_glitch_log_event("=== Quiver Video Active (fallback raw_sink) ===");
		return raw_sink;
	}
}

void Viewer::ViewerImpl::RebuildVideoZoomBin()
{
	if (m_pPipeline == NULL)
		return;

	bool hasVideo = IsVideo() && m_bVideoPlaybackStarted;
	gint64 pos = 0;
	bool wasPlaying = m_bIsPlaying;

	if (hasVideo)
	{
		gst_element_query_position(GST_ELEMENT(m_pPipeline), GST_FORMAT_TIME, &pos);
	}

	gst_element_set_state(GST_ELEMENT(m_pPipeline), GST_STATE_NULL);

	ReleaseVideoPreview();

	GstElement *new_sink = BuildVideoZoomBin();
	if (new_sink != NULL)
	{
		g_object_set(G_OBJECT(m_pPipeline), "video-sink", new_sink, NULL);
	}

	if (hasVideo)
	{
		m_bVideoNeedsFirstFrame = FALSE;
		m_bVideoFlushPending = FALSE;

		g_object_set(G_OBJECT(m_pPipeline), "uri", m_ImageListPtr->GetCurrent().GetURI(), NULL);
		g_object_set(G_OBJECT(m_pPipeline), "mute", m_bMuted ? TRUE : FALSE, NULL);
		g_object_set(G_OBJECT(m_pPipeline), "volume", m_dVolume, NULL);

		gst_element_set_state(GST_ELEMENT(m_pPipeline), wasPlaying ? GST_STATE_PLAYING : GST_STATE_PAUSED);
		if (pos > 0)
		{
			gst_element_seek(GST_ELEMENT(m_pPipeline),
				m_dPlaybackSpeed, GST_FORMAT_TIME,
				GstSeekFlags(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE),
				GST_SEEK_TYPE_SET, pos,
				GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);
		}
		ApplyVideoZoom();
	}

	UpdateNavigationControl();
}

/* The nav control only earns its corner when the image does not fit: with
 * upper = MAX(viewport, image) on both axes, an upper that exceeds the page
 * size means there is something to scroll to, i.e. the image is zoomed in or
 * simply larger than the viewer. */
bool Viewer::ViewerImpl::IsNavControlNeeded() const
{
	if (m_ImageListPtr == NULL || m_ImageListPtr->GetSize() == 0)
		return false;
	if (m_bSlideShowRunning)
		return false;

	PreferencesPtr prefsPtr = Preferences::GetInstance();

	if (IsVideo())
	{
		/* the video zoom lives in the pipeline, not in the scrollable
		 * adjustments, so the image view's upper/page test below says
		 * nothing about it.  The control is needed exactly when the window
		 * shows less than the whole frame, which is the normalized viewport
		 * rectangle ApplyVideoZoom() keeps up to date. */
		if (!prefsPtr->GetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_NAV_CONTROL_VIDEO, false))
			return false;
		if (m_pVideoRotatedPaintable == NULL)
			return false;
		/* A quick preview that is being zoomed or panned needs the control just
		 * as much as a playing video does: it is the same viewport, only drawn
		 * by the image view until the first frame arrives. */
		if (!m_bVideoPreviewHasFrame && !IsVideoPreviewShowing())
			return false;
		const double veps = 0.005;
		return (m_dVideoPreviewViewW > veps && m_dVideoPreviewViewW < 1.0 - veps)
		    || (m_dVideoPreviewViewH > veps && m_dVideoPreviewViewH < 1.0 - veps);
	}

	if (!prefsPtr->GetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_NAV_CONTROL, false))
		return false;
	if (m_pAdjustmentH == NULL || m_pAdjustmentV == NULL)
		return false;

	const double eps = 0.5;
	return (gtk_adjustment_get_upper(m_pAdjustmentH) >
	        gtk_adjustment_get_page_size(m_pAdjustmentH) + eps) ||
	       (gtk_adjustment_get_upper(m_pAdjustmentV) >
	        gtk_adjustment_get_page_size(m_pAdjustmentV) + eps);
}

void Viewer::ViewerImpl::ApplyNavControlMinSize(bool bApply)
{
	if (m_pHBox == NULL)
		return;

	if (!bApply)
	{
		gtk_widget_set_size_request(m_pHBox, -1, -1);
		return;
	}

	PreferencesPtr prefs = Preferences::GetInstance();
	int hudPos = prefs->GetInteger(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_HUD_POSITION, HUD_POS_BOTTOM);
	if (hudPos == HUD_POS_TOP)
	{
		/* the pill row sits at the top, far away from the bottom-right
		 * corner: nothing to keep clear of */
		gtk_widget_set_size_request(m_pHBox, -1, -1);
		return;
	}

	/* The nav control and the pill row are both anchored to the bottom, so
	 * they can only be kept apart horizontally.  Measure what is actually
	 * there (theme, font scale and the set of visible pill buttons all move
	 * these numbers) and reserve just enough width that the control can
	 * never end up on top of - and swallowing clicks from - the pill row. */
	int nav_min = 0, nav_nat = 0;
	int bar_min = 0, bar_nat = 0;
	if (m_pNavControlPill)
		gtk_widget_measure(m_pNavControlPill, GTK_ORIENTATION_HORIZONTAL, -1, &nav_min, &nav_nat, NULL, NULL);
	if (m_pViewerOverlayBar)
		gtk_widget_measure(m_pViewerOverlayBar, GTK_ORIENTATION_HORIZONTAL, -1, &bar_min, &bar_nat, NULL, NULL);

	int nav_w = MAX(nav_min, nav_nat);
	if (nav_w <= 0)
		nav_w = NAV_CONTROL_FALLBACK_WIDTH;
	int bar_w = MAX(bar_min, bar_nat);
	if (bar_w <= 0)
		bar_w = PILL_ROW_FALLBACK_WIDTH;

	/* pill row centred  ->  bar_w/2 + inset + nav_w must fit on one side,
	 * and the pill row itself has to fit across the whole viewer */
	int min_w = bar_w / 2 + NAV_CONTROL_EDGE_MARGIN + nav_w;
	min_w = MAX(min_w, bar_w + 2 * NAV_CONTROL_EDGE_MARGIN);

	gtk_widget_set_size_request(m_pHBox, min_w, -1);
}

static gboolean nav_control_fade_cb(gpointer user_data)
{
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl *)user_data;

	if (p->m_bNavControlFadingIn)
	{
		p->m_dNavControlFadeOpacity = MIN(p->m_dNavControlFadeOpacity + FADE_STEP, 1.0);
		gtk_widget_set_opacity(p->m_pNavControlPill, p->m_dNavControlFadeOpacity);
		if (p->m_dNavControlFadeOpacity >= 1.0)
		{
			p->m_iTimeoutNavControlFade = 0;
			return G_SOURCE_REMOVE;
		}
	}
	else
	{
		p->m_dNavControlFadeOpacity = MAX(p->m_dNavControlFadeOpacity - FADE_STEP, 0.0);
		gtk_widget_set_opacity(p->m_pNavControlPill, p->m_dNavControlFadeOpacity);
		if (p->m_dNavControlFadeOpacity <= 0.0)
		{
			gtk_widget_set_visible(p->m_pNavControlPill, FALSE);
			p->ApplyNavControlMinSize(false);
			p->m_iTimeoutNavControlFade = 0;
			return G_SOURCE_REMOVE;
		}
	}
	return G_SOURCE_CONTINUE;
}

void Viewer::ViewerImpl::CancelNavControlFade()
{
	if (0 != m_iTimeoutNavControlFade)
	{
		g_source_remove(m_iTimeoutNavControlFade);
		m_iTimeoutNavControlFade = 0;
	}
}

void Viewer::ViewerImpl::StartNavControlFade(bool fadeIn)
{
	CancelNavControlFade();
	m_bNavControlFadingIn = fadeIn;
	m_iTimeoutNavControlFade = g_timeout_add(FADE_INTERVAL, nav_control_fade_cb, this);
}

/* Zoom/resize driver: cheap enough to run on every adjustment change, it
 * only does work on a transition edge. */
void Viewer::ViewerImpl::UpdateNavControlVisibility()
{
	if (NULL == m_pNavControlPill)
		return;

	bool bNeeded = IsNavControlNeeded();
	if (bNeeded)
	{
		UpdateNavControlPosition();
		/* without a thumbnail there is nothing to draw: leave the state
		 * untouched so a later call can still show the control once one
		 * lands */
		bNeeded = UpdateNavigationControlTexture();
	}


	if (bNeeded == m_bNavControlShown)
		return;

	m_bNavControlShown = bNeeded;

	if (bNeeded)
	{
		/* keep the control out of the pill row's way for as long as it is
		 * on screen, then fade it in from the current opacity */
		ApplyNavControlMinSize(true);
		if (m_dNavControlFadeOpacity >= 1.0)
			m_dNavControlFadeOpacity = 0.0;
		gtk_widget_set_opacity(m_pNavControlPill, m_dNavControlFadeOpacity);
		gtk_widget_set_visible(m_pNavControlPill, TRUE);
		StartNavControlFade(true);
	}
	else if (gtk_widget_get_visible(m_pNavControlPill))
	{
		StartNavControlFade(false);
	}
	else
	{
		CancelNavControlFade();
		ApplyNavControlMinSize(false);
	}
}

void Viewer::ViewerImpl::UpdateNavigationControl()
{
	if (NULL == m_pNavControlPill || NULL == m_pNavigationControl)
		return;

	if (IsVideo())
	{
		if (IsNavControlNeeded())
		{
			gint dispW = (gint)m_dVideoPreviewDispW;
			gint dispH = (gint)m_dVideoPreviewDispH;
			if (dispW <= 0 || dispH <= 0)
			{
				dispW = m_iVideoWidth;
				dispH = m_iVideoHeight;
				if (dispW > 0 && dispH > 0)
				{
					if (m_ImageListPtr != NULL)
					{
						QuiverFile vf = m_ImageListPtr->GetCurrent();
						if (vf.GetWidth() > 0 && vf.GetHeight() > 0)
						{
							bool codedLandscape = (dispW > dispH);
							bool displayLandscape = (vf.GetWidth() > vf.GetHeight());
							if (codedLandscape != displayLandscape)
								swap(dispW, dispH);
						}
					}
					if (m_iVideoUserRotation == 90 || m_iVideoUserRotation == 270)
					{
						swap(dispW, dispH);
					}
				}
			}
			if (dispW > 0 && dispH > 0)
			{
				ConfigureVideoPreviewScale(dispW, dispH);
				UpdateVideoPreviewViewArea();
			}
		}
		else
		{
			quiver_navigation_control_set_texture(QUIVER_NAVIGATION_CONTROL(m_pNavigationControl), NULL);
			quiver_navigation_control_set_paintable(QUIVER_NAVIGATION_CONTROL(m_pNavigationControl), NULL, 0, 0);
		}
	}
	else
	{
		if (!IsNavControlNeeded())
			quiver_navigation_control_set_texture(QUIVER_NAVIGATION_CONTROL(m_pNavigationControl), NULL);
	}

	UpdateNavControlVisibility();
}

static gboolean timeout_update_scrollbars(gpointer user_data)
{
	Viewer::ViewerImpl* pViewerImpl = (Viewer::ViewerImpl*)user_data;
	/* one-shot, and the slot goes before the work rather than after it: the
	 * updates below can arm the next one, and clearing afterwards would drop
	 * its id, leaving a source nothing tracks that fires into a torn-down
	 * viewer later on */
	pViewerImpl->m_iTimeoutScrollbars = 0;
	pViewerImpl->UpdateScrollbars();

	return FALSE;
}

/* The preview is panned, which is "value-changed" and not "changed": the latter
 * is about the range and the page, i.e. zoom, view mode and window size.  While
 * a video's quick preview is on screen it is the picture the user is framing, so
 * where it is scrolled to is where the video has to play from - otherwise play
 * jumps away from the framing that was just set up in front of them.  Not while
 * a picture is being delivered or a framing is being put on it: the adjustments
 * move then too, and what they say there belongs to that, not to the user. */
static void image_view_adjustment_value_changed (GtkAdjustment *adjustment, gpointer user_data)
{
	(void)adjustment;
	Viewer::ViewerImpl* pViewerImpl = (Viewer::ViewerImpl*)user_data;
	if (pViewerImpl == NULL)
		return;
	if (pViewerImpl->IsPreviewPanValid() && !pViewerImpl->m_bFramingVideoPreview
		&& pViewerImpl->IsVideoPreviewShowing())
	{
		pViewerImpl->CaptureVideoPanFromPreview();
		pViewerImpl->UpdateVideoPreviewViewAreaFromImageView();
	}
}

static void image_view_adjustment_changed (GtkAdjustment *adjustment, gpointer user_data)
{ (void)adjustment; 
	Viewer::ViewerImpl* pViewerImpl = (Viewer::ViewerImpl*)user_data;

	/* "changed" fires whenever the image stops fitting the viewport (zoom,
	 * view mode, window resize): that is exactly when the nav control has to
	 * fade in or out. */
	pViewerImpl->UpdateNavControlVisibility();

	/* While a video's quick preview is on screen it is the picture the user is
	 * framing, so where it is scrolled to is where the video has to play from -
	 * otherwise play jumps away from the framing that was just set up in front
	 * of them.  Not while a picture is being delivered: the adjustments move
	 * then too, and what they say there belongs to the delivery, not the user. */
	/* A preview whose framing could not be decided when it landed - the view
	 * had no viewport yet - is decided now: this fires when the view is laid out
	 * and its scroll ranges exist, which is exactly the information the decision
	 * was waiting for. */
	if (pViewerImpl->m_bPreviewFramingPending && !pViewerImpl->m_bFramingVideoPreview
		&& pViewerImpl->IsVideoPreviewShowing())
	{
		const gdouble shown =
			quiver_image_view_get_magnification(QUIVER_IMAGE_VIEW(pViewerImpl->m_pImageView));
		/* only while the framing is still the one the decision was made at: a
		 * preview the user has since zoomed or panned is theirs, and re-deciding
		 * would put a framing back over it */
		if (fabs(shown - pViewerImpl->m_dPreviewFramingMag) < 0.001)
		{
			pViewerImpl->m_bPreviewFramingPending = false;
			pViewerImpl->FrameVideoPreviewFromVideo(TRUE);
		}
		else
		{
			/* the user has since made a framing of their own, which is the most
			 * recent thing said about the zoom: stop waiting for a layout */
			pViewerImpl->m_bPreviewFramingPending = false;
		}
	}

	if (pViewerImpl->IsPreviewPanValid() && !pViewerImpl->m_bFramingVideoPreview
		&& pViewerImpl->IsVideoPreviewShowing())
	{
		pViewerImpl->CaptureVideoPanFromPreview();
		pViewerImpl->UpdateVideoPreviewViewAreaFromImageView();
	}

	if (0 != pViewerImpl->m_iTimeoutScrollbars)
	{
		g_source_remove(pViewerImpl->m_iTimeoutScrollbars);
		pViewerImpl->m_iTimeoutScrollbars = 0;
	}
	
	pViewerImpl->m_iTimeoutScrollbars = g_timeout_add(20, timeout_update_scrollbars, pViewerImpl);
}


//=============================================================================
// private viewer implementation nested classes:
//=============================================================================
void Viewer::ViewerImpl::ImageListEventHandler::HandleContentsChanged(ImageListEventPtr event)
{ (void)event; 
	parent->SetImageIndex(parent->m_ImageListPtr->GetCurrentIndex(),true);
	parent->m_ThumbnailLoader.UpdateList(true);
}
void Viewer::ViewerImpl::ImageListEventHandler::HandleCurrentIndexChanged(ImageListEventPtr event) 
{
	bool bDirectionForward = (event->GetIndex() >= event->GetOldIndex());
	parent->SetImageIndex(event->GetIndex(), bDirectionForward);
}
void Viewer::ViewerImpl::ImageListEventHandler::HandleItemAdded(ImageListEventPtr event)
{ (void)event; 
	parent->SetImageIndex(parent->m_ImageListPtr->GetCurrentIndex(),true);
	parent->m_ThumbnailLoader.UpdateList(true);
}
void Viewer::ViewerImpl::ImageListEventHandler::HandleItemRemoved(ImageListEventPtr event)
{ (void)event; 
	parent->SetImageIndex(parent->m_ImageListPtr->GetCurrentIndex(),true);
	parent->m_ThumbnailLoader.UpdateList(true);
}
void Viewer::ViewerImpl::ImageListEventHandler::HandleItemChanged(ImageListEventPtr event)
{
	if (parent->m_ImageListPtr->GetCurrentIndex() == event->GetIndex())
	{
		parent->m_ThumbnailCache.RemoveTexture(parent->m_ImageListPtr->GetCurrent().GetURI());
		parent->m_ThumbnailLoader.UpdateList(true);
	
		ImageLoader::LoadParams params = {};
	
		params.orientation = parent->GetCurrentOrientation(true);
		params.reload = true;
		params.fullsize = true;
		params.no_thumb_preview = true;
		params.state = ImageLoader::LOAD;
		parent->m_ImageLoader.LoadImage(parent->m_ImageListPtr->GetCurrent(),params);
	}
}


void Viewer::ViewerImpl::PreferencesEventHandler::HandlePreferenceChanged(PreferencesEventPtr event)
{
	PreferencesPtr prefsPtr = Preferences::GetInstance();
	if (QUIVER_PREFS_APP == event->GetSection() )
	{
		if (QUIVER_PREFS_APP_USE_THEME_COLOR == event->GetKey() )
		{
			if (event->GetNewBoolean())
			{
				// use theme color
				set_widget_bg_color(parent->m_pIconView, NULL);
				set_widget_bg_color(parent->m_pImageView, NULL);
				set_widget_bg_color(parent->m_pVideoFixed, NULL);
			}
			else
			{
				GdkRGBA color;
				
				string strBGColorImg   = prefsPtr->GetString(QUIVER_PREFS_APP,QUIVER_PREFS_APP_BG_IMAGEVIEW, "#000");
				string strBGColorThumb = prefsPtr->GetString(QUIVER_PREFS_APP,QUIVER_PREFS_APP_BG_ICONVIEW, "#444");
						
				if (!parent->m_bFilmstripOverlay)
				{
					if (gdk_rgba_parse(&color, strBGColorThumb.c_str()))
						set_widget_bg_color(parent->m_pIconView, &color);
				}
				
				if (gdk_rgba_parse(&color, strBGColorImg.c_str()))
				{
					set_widget_bg_color(parent->m_pImageView, &color);
					set_widget_bg_color(parent->m_pVideoFixed, &color);
				}
			}
		}
		else if (QUIVER_PREFS_APP_BG_IMAGEVIEW == event->GetKey() )
		{
			if ( !prefsPtr->GetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_USE_THEME_COLOR,true) )
			{
				GdkRGBA color;
				
				string strBGColorImg = prefsPtr->GetString(QUIVER_PREFS_APP,QUIVER_PREFS_APP_BG_IMAGEVIEW, "#000");
				
				if (gdk_rgba_parse(&color, strBGColorImg.c_str()))
				{
					set_widget_bg_color(parent->m_pImageView, &color);
					set_widget_bg_color(parent->m_pVideoFixed, &color);
				}
			}			
		}
		else if (QUIVER_PREFS_APP_BG_ICONVIEW == event->GetKey() )
		{
			if ( !prefsPtr->GetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_USE_THEME_COLOR,true) )
			{
				if (!parent->m_bFilmstripOverlay)
				{
					GdkRGBA color;
					
					string strBGColorThumb = prefsPtr->GetString(QUIVER_PREFS_APP,QUIVER_PREFS_APP_BG_ICONVIEW, "#444");						

					if (gdk_rgba_parse(&color, strBGColorThumb.c_str()))
						set_widget_bg_color(parent->m_pIconView, &color);
				}
			}
		}
	}
	else if ( QUIVER_PREFS_BROWSER == event->GetSection () )
	{
		if (QUIVER_PREFS_BROWSER_THUMBS_FILMSTRIP == event->GetKey() )
		{
			quiver_icon_view_set_filmstrip_enabled(QUIVER_ICON_VIEW(parent->m_pIconView), event->GetNewBoolean());
			quiver_icon_view_invalidate_window(QUIVER_ICON_VIEW(parent->m_pIconView));
		}
	}
	else if ( QUIVER_PREFS_VIEWER == event->GetSection () )
	{
		if (QUIVER_PREFS_VIEWER_FILMSTRIP_SHOW == event->GetKey() )
		{
			parent->UpdateHUDPosition();
		}
		else if (QUIVER_PREFS_VIEWER_FILMSTRIP_POSITION == event->GetKey() )
		{
			parent->AddFilmstrip();
			parent->UpdateHUDPosition();
		}
		else if (QUIVER_PREFS_VIEWER_FILMSTRIP_SIZE == event->GetKey() )
		{
			quiver_icon_view_set_icon_size(QUIVER_ICON_VIEW(parent->m_pIconView), event->GetNewInteger(), event->GetNewInteger());
			parent->m_ThumbnailLoader.SetIconDimensions(event->GetNewInteger(), event->GetNewInteger());
			parent->UpdateHUDPosition();
		}
		else if (QUIVER_PREFS_VIEWER_FILMSTRIP_OVERLAY == event->GetKey() )
		{
			parent->AddFilmstrip();
			parent->UpdateHUDPosition();
		}
		else if (QUIVER_PREFS_VIEWER_HUD_POSITION == event->GetKey() )
		{
			parent->UpdateHUDPosition();
		}
		else if (QUIVER_PREFS_VIEWER_FILMSTRIP_HIDE_FS == event->GetKey() )
		{
			parent->m_bHideFilmstripFS = event->GetNewBoolean();
		}
		else if (QUIVER_PREFS_VIEWER_FILMSTRIP_SQUARE == event->GetKey() )
		{
			quiver_icon_view_set_thumbnails_square(QUIVER_ICON_VIEW(parent->m_pIconView), event->GetNewBoolean());
			parent->m_ThumbnailLoader.UpdateList(true);
		}
		else if (QUIVER_PREFS_VIEWER_QUICK_PREVIEW == event->GetKey() )
		{
			parent->m_ImageLoader.EnableQuickPreview(event->GetNewBoolean());
		}
		else if (QUIVER_PREFS_VIEWER_SCROLLBARS_HIDE == event->GetKey() )
		{
			parent->UpdateScrollbars();
		}
		else if (QUIVER_PREFS_VIEWER_NAV_CONTROL == event->GetKey() )
		{
			parent->UpdateNavigationControl();
		}
		else if (QUIVER_PREFS_VIEWER_NAV_CONTROL_VIDEO == event->GetKey() )
		{
			parent->UpdateNavigationControl();
		}
		else if (QUIVER_PREFS_VIEWER_KINETIC_SCROLLING == event->GetKey() )
		{
			parent->m_bKineticScrolling = event->GetNewBoolean();
			quiver_image_view_set_smooth_scroll(QUIVER_IMAGE_VIEW(parent->m_pImageView), parent->m_bKineticScrolling);
			if (!parent->m_bKineticScrolling)
			{
				parent->StopVideoPanSlowdown();
			}
		}
	}
	else if (QUIVER_PREFS_SLIDESHOW == event->GetSection() )
	{
		if (QUIVER_PREFS_SLIDESHOW_DURATION == event->GetKey() )
		{
			parent->m_iSlideShowDuration = event->GetNewInteger(); 
		}
		else if (QUIVER_PREFS_SLIDESHOW_LOOP == event->GetKey() )
		{
			parent->m_bSlideShowLoop = event->GetNewBoolean();
		}
		else if (QUIVER_PREFS_SLIDESHOW_TRANSITION == event->GetKey() )
		{
			if (0 != parent->m_iTimeoutSlideshowID)
			{
				quiver_image_view_set_enable_transitions(QUIVER_IMAGE_VIEW(parent->m_pImageView),(gboolean)event->GetNewBoolean());
			}
		}
	}
}

QuiverFile Viewer::ViewerImpl::ViewerThumbLoader::GetQuiverFile(gulong index)
{
	if (index < m_pViewerImpl->m_ImageListPtr->GetSize())
	{
		return m_pViewerImpl->m_ImageListPtr->Get(index);
	}
	return QuiverFile();
}


struct ViewerThumbLoaderSyncData {
	GtkWidget* iconview;
	gulong index;
	bool is_running;
	Statusbar* statusbar;
	std::weak_ptr<bool> aliveToken;
};

static gboolean idle_invalidate_cell_v(gpointer data) {
	ViewerThumbLoaderSyncData* pData = (ViewerThumbLoaderSyncData*)data;
	auto alive = pData->aliveToken.lock();
	if (alive && *alive && pData->iconview && QUIVER_IS_ICON_VIEW(pData->iconview))
	{
		quiver_icon_view_invalidate_cell(QUIVER_ICON_VIEW(pData->iconview), pData->index);
	}
	delete pData;
	return G_SOURCE_REMOVE;
}

static gboolean idle_set_is_running_v(gpointer data) {
	ViewerThumbLoaderSyncData* pData = (ViewerThumbLoaderSyncData*)data;
	auto alive = pData->aliveToken.lock();
	if (alive && *alive && pData->statusbar)
	{
		if (pData->is_running) {
			pData->statusbar->StartProgressPulse();
		} else {
			pData->statusbar->StopProgressPulse();
		}
	}
	delete pData;
	return G_SOURCE_REMOVE;
}

void Viewer::ViewerImpl::ViewerThumbLoader::LoadThumbnail(const ThumbLoaderItem &item, guint uiWidth, guint uiHeight)
{
	if (IsStopped() || !m_spAlive || !*m_spAlive || !m_pViewerImpl)
		return;

	bool is_mapped = m_bMapped.load(std::memory_order_relaxed);

	if (is_mapped && m_pViewerImpl->m_ImageListPtr && item.m_ulIndex < m_pViewerImpl->m_ImageListPtr->GetSize())
	{
		QuiverFile f(item.m_QuiverFile);
		if (NULL == f.GetURI())
			return;

		/* Square mode center-crops at draw time, but the cell size is what gets
		 * fetched; see the matching note in BrowserThumbLoader::LoadThumbnail. */

		GdkTexture *texture = NULL;
		texture = m_pViewerImpl->m_ThumbnailCache.GetTexture(f.GetURI());				
	
		if (NULL != texture)
		{
			// check if the thumbnail is the correct size
			guint thumb_width, thumb_height;
			guint bound_width = f.GetWidth();
			guint bound_height = f.GetHeight();

			if (4 < f.GetOrientation())
			{
				swap(bound_width,bound_height);
			}

			thumb_width = gdk_texture_get_width(texture);
			thumb_height = gdk_texture_get_height(texture);
			
			quiver_rect_get_bound_size(uiWidth, uiHeight, &bound_width, &bound_height, FALSE);
			if (bound_width > 0 && bound_height > 0 && thumb_width == bound_width && thumb_height == bound_height)
			{
				// Cache hit! Thumbnail is already present in cache at the target size.
				g_object_unref(texture);
				return;
			}

			// need a new thumbnail because the current cached size
			// is not the same as the size needed
			g_object_unref(texture);
			texture = NULL;
		}

		/* A request, not a size: GetThumbnailTexture() rounds this up to the
		 * nearest standard thumbnail size. */
		const guint uiRequestedSize = std::max(uiWidth, uiHeight);

		if (NULL == texture)
		{
			/* Show whatever smaller thumbnail is already cached rather than the
			 * generic icon for the whole decode.  See the matching note in
			 * BrowserThumbLoader::LoadThumbnail. */
			GdkTexture *smaller = f.GetCachedThumbnailAtMost(uiRequestedSize);
			if (NULL != smaller)
			{
				m_pViewerImpl->m_ThumbnailCache.AddTexture(f.GetURI(), smaller);
				g_object_unref(smaller);
				InvalidateCell(item.m_ulIndex);
			}

			if (m_pViewerImpl->m_ImageLoader.IsWorking())
			{
				usleep(2000);
			}
			texture = f.GetThumbnailTexture(uiRequestedSize);
		}

		if (NULL != texture)
		{
			guint thumb_width, thumb_height;
			thumb_width = gdk_texture_get_width(texture);
			thumb_height = gdk_texture_get_height(texture);

			guint bound_width = f.GetWidth();
			guint bound_height = f.GetHeight();
			
			if (4 < f.GetOrientation())
			{
				swap(bound_width,bound_height);
			}
			quiver_rect_get_bound_size(uiWidth,uiHeight, &bound_width,&bound_height,FALSE);

			if (bound_width > 0 && bound_height > 0 && (thumb_width != bound_width || thumb_height != bound_height))
			{
				GdkTexture* scaled = QuiverUtils::ScaleTexture(texture, bound_width, bound_height);
				g_object_unref(texture);
				texture = scaled;
			}

			if (NULL != texture)
			{
				m_pViewerImpl->m_ThumbnailCache.AddTexture(f.GetURI(), texture);
				g_object_unref(texture);
			}

			InvalidateCell(item.m_ulIndex);
		}
	}
}

void Viewer::ViewerImpl::ViewerThumbLoader::InvalidateCell(gulong index)
{
	ViewerThumbLoaderSyncData* pInvData = new ViewerThumbLoaderSyncData();
	pInvData->iconview = m_pViewerImpl->m_pIconView;
	pInvData->index = index;
	pInvData->aliveToken = m_spAlive;
	if (!ThreadUtil::IsGUIThread()) { g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, idle_invalidate_cell_v, pInvData, NULL); } else { idle_invalidate_cell_v(pInvData); }
}

void Viewer::ViewerImpl::ViewerThumbLoader::GetVisibleRange(gulong* pulStart, gulong* pulEnd)
{
	if (pulStart) *pulStart = 0;
	if (pulEnd) *pulEnd = 0;
	if (IsStopped() || !m_spAlive || !*m_spAlive || !m_pViewerImpl)
	{
		return;
	}
	if (m_pViewerImpl->m_pIconView && QUIVER_IS_ICON_VIEW(m_pViewerImpl->m_pIconView))
	{
		quiver_icon_view_get_visible_range(QUIVER_ICON_VIEW(m_pViewerImpl->m_pIconView), pulStart, pulEnd);
	}
}

void Viewer::ViewerImpl::ViewerThumbLoader::GetIconSize(guint* puiWidth, guint* puiHeight)
{
	if (puiWidth) *puiWidth = m_uiThumbWidth.load(std::memory_order_relaxed);
	if (puiHeight) *puiHeight = m_uiThumbHeight.load(std::memory_order_relaxed);
}

gulong Viewer::ViewerImpl::ViewerThumbLoader::GetNumItems()
{
	if (IsStopped() || !m_spAlive || !*m_spAlive || !m_pViewerImpl || !m_pViewerImpl->m_ImageListPtr)
		return 0;
	return m_pViewerImpl->m_ImageListPtr->GetSize();
}

void Viewer::ViewerImpl::ViewerThumbLoader::SetIsRunning(bool bIsRunning)
{
	if (IsStopped() || !m_spAlive || !*m_spAlive || !m_pViewerImpl)
		return;
	ViewerThumbLoaderSyncData* pData = new ViewerThumbLoaderSyncData();
	pData->statusbar = m_pViewerImpl->m_StatusbarPtr.get();
	pData->is_running = bIsRunning;
	pData->aliveToken = m_spAlive;
	if (!ThreadUtil::IsGUIThread()) { g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, idle_set_is_running_v, pData, NULL); } else { idle_set_is_running_v(pData); }
}

void Viewer::ViewerImpl::ViewerThumbLoader::SetCacheSize(guint uiCacheSize)
{
	if (IsStopped() || !m_spAlive || !*m_spAlive || !m_pViewerImpl)
		return;
	m_pViewerImpl->m_ThumbnailCache.SetSize(uiCacheSize);
}

GtkWidget *Viewer::GetNavControlPill() const
{
	return m_ViewerImplPtr->m_pNavControlPill;
}

GtkWidget *Viewer::GetNavigationControl() const
{
	return m_ViewerImplPtr->m_pNavigationControl;
}

void Viewer::UpdateNavigationControl()
{
	m_ViewerImplPtr->UpdateNavigationControl();
}
