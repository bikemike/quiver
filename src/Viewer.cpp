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
#include "QuiverVideoOps.h"
#include "ImageLoader.h"
#include "ImageList.h"

#include "QuiverFile.h"

#include "QuiverPrefs.h"
#include "IPreferencesEventHandler.h"

#include "QuiverStockIcons.h"
#include "QuiverFileOps.h"

#include "IImageListEventHandler.h"

#include "Statusbar.h"

#include "IPixbufLoaderObserver.h"
#include "IconViewThumbLoader.h"

#include <gdk/gdkkeysyms.h>
#include <exiv2/exiv2.hpp>
#include <memory>
#include <math.h>

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

static GdkPixbuf* icon_pixbuf_callback(QuiverIconView *iconview, gulong cell, gpointer user_data);
static GdkPixbuf* thumbnail_pixbuf_callback(QuiverIconView *iconview, gulong cell, gint* actual_width, gint* actual_height, gpointer user_data);
static gulong n_cells_callback(QuiverIconView *iconview, gpointer user_data);
static void image_view_adjustment_changed (GtkAdjustment *adjustment, gpointer user_data);

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

static void viewer_volume_value_changed (GtkRange *range, gdouble value, gpointer user_data);
static void viewer_volume_toggle_popover_cb(gpointer user_data, GtkWidget *widget);

static gboolean viewer_scale_change_value_cb(GtkRange *range, GtkScrollType scroll, gdouble value, gpointer user_data);

static void viewer_navigation_button_press_event(GtkGestureClick *gesture, gint n_press, gdouble x, gdouble y, gpointer userdata);
void navigation_control_button_release_event (GtkGestureClick *gesture, gint n_press, gdouble x, gdouble y, gpointer userdata);
// FIXME: remove
//static GtkTableChild * GetGtkTableChild(GtkTable * table,GtkWidget	*widget_to_get);



static void set_widget_bg_color(GtkWidget *widget, GdkRGBA *color) {
    static guint class_counter = 0;
    gchar *old_class = (gchar *)g_object_get_data(G_OBJECT(widget), "quiver-bg-class");
    
    GtkCssProvider *old = (GtkCssProvider *)g_object_get_data(G_OBJECT(widget), "quiver-bg-provider");
    if (old != NULL) {
        gtk_style_context_remove_provider_for_display(
            gdk_display_get_default(), GTK_STYLE_PROVIDER(old));
        g_object_set_data(G_OBJECT(widget), "quiver-bg-provider", NULL);
        g_object_unref(old);
    }
    
    if (old_class != NULL) {
        gtk_widget_remove_css_class(widget, old_class);
        g_object_set_data(G_OBJECT(widget), "quiver-bg-class", NULL);
        g_free(old_class);
    }
    
    if (color == NULL)
        return;
    
    GtkCssProvider *provider = gtk_css_provider_new();
    
    gchar *class_name = g_strdup_printf("quiver-bg-%u", ++class_counter);
    gchar *color_str = gdk_rgba_to_string(color);
    gchar *css = g_strdup_printf(".%s { background-color: %s; }", class_name, color_str);
    gtk_css_provider_load_from_string(provider, css);
    g_free(css);
    g_free(color_str);
    
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(), GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_set_data(G_OBJECT(widget), "quiver-bg-provider", provider);
    
    gtk_widget_add_css_class(widget, class_name);
    g_object_set_data_full(G_OBJECT(widget), "quiver-bg-class", class_name, g_free);
}


// popup menu callbacks
static void viewer_show_context_menu(GtkWidget *widget, gdouble x_root, gdouble y_root, guint32 time, gpointer userdata);
static void viewer_button_press_cb(GtkGestureClick *gesture, gint n_press, gdouble x, gdouble y, gpointer user_data);
static void viewer_button_release_cb(GtkGestureClick *gesture, gint n_press, gdouble x, gdouble y, gpointer user_data);
static void viewer_show_context_menu(GtkWidget *widget, gdouble x_root, gdouble y_root, guint32 time, gpointer userdata);
static void viewer_speed_button_clicked_cb(GtkButton *button, gpointer user_data);
static void viewer_speed_toggle_popover_cb(gpointer user_data, GtkWidget *widget);
static void viewer_fullscreen_button_clicked_cb(GtkButton *button, gpointer user_data);
static void viewer_snapshot_button_clicked_cb(GtkButton *button, gpointer user_data);
static void viewer_skip_back_cb(gpointer user_data);
static void viewer_skip_fwd_cb(gpointer user_data);
static void viewer_frame_step_back_cb(gpointer user_data);
static void viewer_frame_step_fwd_cb(gpointer user_data);

static GdkContentProvider* signal_drag_source_prepare(GtkDragSource *source, gdouble x, gdouble y, gpointer user_data);
static void signal_drag_begin (GtkDragSource *source, GdkDrag *drag, gpointer user_data);
static void signal_drag_end(GtkDragSource *source, GdkDrag *drag, gpointer user_data);

/* Attach the shared input controllers (click/scroll/motion) to a widget
 * so the viewer area responds over the image, the video fixed and the GL
 * sink widget alike. */
static void attach_viewer_input_controllers(GtkWidget *widget, gpointer user_data)
{
	GtkGesture *click = gtk_gesture_click_new();
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
}


static gboolean timeout_play_position (gpointer data);
static gboolean timeout_event_motion_notify (gpointer user_data);
static void set_control_visible(GtkWidget* w, bool visible);

static gchar* gst_time_format(gint64 time);

#if VIDEO_ZOOM_SMOOTH_ANIMATION
static gboolean video_zoom_timeout(gpointer data);
#endif
static void video_zoom_get_pointer(Viewer::ViewerImpl *p, gdouble *px, gdouble *py);
static void video_zoom_raise_media_windows(Viewer::ViewerImpl *p);
static void video_zoom_sink_map_cb(GtkWidget *widget, gpointer user_data);
static void video_paintable_invalidated_cb(GdkPaintable *paintable, gpointer user_data);

#define ACTION_VIEWER_CUT              "ViewerCut"
#define ACTION_VIEWER_COPY             "ViewerCopy"
#define ACTION_VIEWER_TRASH            "ViewerTrash"
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
	int iRefs;
};

static void view_pixbuf_target_destroyed(GtkWidget *widget, gpointer data)
{
	(void)widget;
	ViewPixbufTarget *t = (ViewPixbufTarget*)data;
	t->pImageView = NULL;
	t->pErrorLabel = NULL;
}

static ViewPixbufTarget* view_pixbuf_target_new(QuiverImageView *pImageView, GtkWidget *pErrorLabel)
{
	ViewPixbufTarget *t = new ViewPixbufTarget{pImageView, pErrorLabel, 1};
	g_signal_connect(G_OBJECT(pImageView), "destroy", G_CALLBACK(view_pixbuf_target_destroyed), t);
	return t;
}

static void view_pixbuf_target_ref(ViewPixbufTarget *t)
{
	g_atomic_int_inc(&t->iRefs);
}

static void view_pixbuf_target_unref(ViewPixbufTarget *t)
{
	if (g_atomic_int_dec_and_test(&t->iRefs))
		delete t;
}

struct AsyncPixbufData {
	ViewPixbufTarget *pTarget;
	GdkPixbuf *pixbuf;
	gint width, height;
	gboolean bReset;
	bool bAtSize;
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

class ViewerImageViewPixbufLoaderObserver : public IPixbufLoaderObserver
{
public:
	ViewerImageViewPixbufLoaderObserver(QuiverImageView *imageview, GtkWidget *pErrorLabel)
		: m_pTarget(view_pixbuf_target_new(imageview, pErrorLabel)) {};
	virtual ~ViewerImageViewPixbufLoaderObserver(){ view_pixbuf_target_unref(m_pTarget); };

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
		gboolean bReset = bResetViewMode ? TRUE : FALSE;
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
	void UpdateUI();
	
	void CacheNext(bool bDirectionForward);
	void SetImageIndex(int index, bool bDirectionForward, bool bCacheNext = true);

	int  GetCurrentOrientation(bool bCombinedWithMaximizedOrientation = false);
	int  GetMaximizedOrientation(QuiverFile f, bool bCombinedWithFileOrientation = false);

	void CacheImageAtSize(QuiverFile f, int w, int h);
	void LoadImageAtSize(QuiverFile f, int w, int h);
	void LoadImage(QuiverFile f);

	void SetCurrentOrientation(int iOrientation, bool bUpdateExif = true);
	void AddFilmstrip();
	
	void UpdateScrollbars();

	void QueueIconViewUpdate(int timeout = 100 /* ms */);

	void SlideShowStop(bool bEmitStopEvent = true);

	bool IsPlaying() const
	{
		return m_bIsPlaying;
	}

	/* the timeline (time label, progress bar, volume button) is hidden while
	 * the video is shown and only appears once play has been pressed; from
	 * then on it stays visible until the current item changes */
	void UpdateTimelineVisibility()
	{
		set_control_visible(m_pTimelineRow, IsVideo() && m_bTimelineVisible);
	}

	void SetIsPlaying(bool isPlaying)
	{
		m_bIsPlaying = isPlaying;

		if (0 != m_iTimeoutPlayProgress)
		{
			g_source_remove(m_iTimeoutPlayProgress);
			m_iTimeoutPlayProgress = 0;
		}

		if (IsPlaying())
		{
			m_bTimelineVisible = true;
			UpdateTimelineVisibility();
			gtk_image_set_from_icon_name(GTK_IMAGE(m_pPlayImage), "media-playback-pause");

			/* show all controls via opacity fade-in */
			set_control_visible(m_pTimeElapsedLabel, true);
			set_control_visible(m_pTimeDurationLabel, true);
			set_control_visible(m_pRewindBtn, true);
			set_control_visible(m_pSpeedButton, true);
			set_control_visible(m_pFfBtn, true);
			set_control_visible(m_pSnapBtn, true);
			set_control_visible(m_pVolumeButton, true);
			set_control_visible(m_pVideoOptionsBtn, true);
			set_control_visible(m_pFullscreenBtn, true);
			StartControlsFade(true);

			m_iTimeoutPlayProgress = g_timeout_add(200,timeout_play_position,this);
		}
		else
		{
			/* don't leave a stale auto-hide timer running once paused, or the
			 * whole media bar (timeline included) can vanish after a click */
			if (0 != m_iTimeoutMouseMotionNotify)
			{
				g_source_remove(m_iTimeoutMouseMotionNotify);
				m_iTimeoutMouseMotionNotify = 0;
			}
			CancelControlsFade();
			gtk_image_set_from_icon_name(GTK_IMAGE(m_pPlayImage), "media-playback-start");
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
		if (IsPlaying())
		{
			m_iTimeoutMouseMotionNotify = g_timeout_add(1500,timeout_event_motion_notify,this);
		}
	}

	// methods for playing videos
	void PlayPauseVideo();
	void SkipForward();
	void SkipBack();
	void UpdateTimeline();
	void StopVideo(bool reloadImage = true);
	void SetPlaybackSpeed(double speed);
	void Snapshot();
	// returns true if current item is a video
	bool IsVideo();
	// switch the stack to the video page and unhide the video widgets
	void ShowVideoPage();

	// video zoom (in-pipeline crop, optionally HW-accelerated upscale)
	void SetVideoZoom(gdouble zoom);
	void ApplyVideoZoom();


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

	GtkWidget *m_pNavigationBox;

	GtkWidget *m_pHBox;
	GtkWidget *m_pVBox;
	
	gdouble m_dAdjustmentValueLastH;
	gdouble m_dAdjustmentValueLastV;
	
	GtkWidget *m_pNavigationWindow;
	GtkWidget *m_pNavigationControl;

	GtkWidget* m_pMediaControls;
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
	double m_dVolume;
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
	
	typedef enum _SlideShowState
	{
		SLIDESHOW_STATE_ADVANCE,
		SLIDESHOW_STATE_CACHE,
		SLIDESHOW_STATE_PLAY_VIDEO,
		SLIDESHOW_STATE_PLAYING_VIDEO
	} SlideShowState;

	SlideShowState m_SlideShowState;
	guint m_iIdleSetIndex;
	guint m_iTimeoutScrollbars;
	guint m_iTimeoutUpdateListID;
	guint m_iTimeoutSlideshowID;
	guint m_iTimeoutClickID;
	guint m_iTimeoutMouseMotionNotify;
	guint m_iTimeoutPlayProgress;

	int   m_iSlideShowDuration;
	int   m_iSlideShowWaitCount;
	bool  m_bSlideShowLoop;

	bool m_bMaximizeViewableArea;
	bool m_bMaximizeViewabe;

	bool m_bIsPlaying;

	ImageCache m_ThumbnailCache;

	double m_dPlaybackSpeed;
	GtkWidget* m_pSpeedButton;
	GtkWidget* m_pSpeedLabel;

	GtkDragSource* m_pDragSource = nullptr;
	GtkDropTarget* m_pDropTarget;
	GtkWidget* m_pContextMenuPopover;

	// gstreamer elements for playing videos
	GstElement* m_pPipeline;
	GdkPaintable* m_pVideoPaintable; // paintable exposed by the sink
	GtkWidget*  m_pVideoSinkWidget; // GtkPicture wrapping the paintable
	GtkWidget*  m_pVideoFixed;      // GtkFixed canvas (fills the viewer area, clips the video sink widget)
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
	VideoZoomType m_VideoZoomType;
	GstElement* m_pVideoZoomInput;  // input capsfilter: permissive caps so playbin template check passes with HW decoders
	GstElement* m_pVideoCrop;       // in-pipeline crop element for digital zoom (software path only)
	GstElement* m_pVideoZoomConvert; // normalizes formats for the scaler
	GstElement* m_pVideoZoomScaler; // scaler (HW-accelerated if available, else videoscale)
	GstElement* m_pVideoZoomCaps;   // capsfilter forcing the scaled output frame size
	GstElement* m_pVideoZoomInputCaps; // widens the chain's sink template to the decoder's memory type
	gdouble     m_dVideoZoom;       // current video zoom factor (1.0 = actual size, like the image view)
	gdouble     m_dVideoZoomFinal;  // target video zoom factor for the smooth animation
	gdouble     m_dVideoZoomMin;    // lowest zoom allowed: the fit level seen so far (1.0 when actual size)
	guint       m_iVideoZoomTimeoutID; // timer driving the smooth video zoom animation
	gdouble     m_dVideoPanX;       // viewport (visible part of the frame) left edge, in source px
	gdouble     m_dVideoPanY;       // viewport top edge, in source pixels
	gdouble     m_dVideoLastWidgetW; // last applied video widget width (for zoom anchoring)
	gdouble     m_dVideoLastWidgetH; // last applied video widget height
	gdouble     m_dVideoLastZc;      // last applied pipeline crop factor
	gboolean    m_bVideoZoomCropActive; // zoomcaps is forcing the scaled output size
	gboolean    m_bVideoZoomInputCropActive; // zoominputcaps is forcing system-memory input for the crop
	gboolean    m_bVideoPanning;    // left-button pan drag in progress
	gboolean    m_bVideoNeedsFirstFrame; // TRUE after switching videos: defer opacity restore until new frame is decoded
	gboolean    m_bVideoFlushPending; // TRUE between the flushing seek and the next ASYNC_DONE
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
	gint        m_iVideoSinkW;      // last set_size_request width  (skip if unchanged)
	gint        m_iVideoSinkH;      // last set_size_request height
	gint        m_iVideoSinkX;      // last layout_move x
	gint        m_iVideoSinkY;      // last layout_move y (from caps probe)

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

	gboolean    m_bSeekDragging;

	/* last pointer position in surface coords (to ignore synthetic motion
	 * events caused by the controls re-laying-out under a parked pointer) */
	double      m_dPointerRootX;
	double      m_dPointerRootY;
	bool        m_bPointerPosValid;

	bool IsFilmstripOverlay() const { return m_bFilmstripOverlay; }
	bool IsHideFilmstripFS() const { return m_bHideFilmstripFS; }
	bool IsPointerOverFilmstrip() const;
	bool IsPointerOverMediaControls() const;
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
		ViewerThumbLoader(ViewerImpl* pViewerImpl, guint iNumThreads)  : IconViewThumbLoader(iNumThreads)
		{
			m_pViewerImpl = pViewerImpl;
		}
		
		~ViewerThumbLoader(){}
		
	protected:
		
		virtual void LoadThumbnail(const ThumbLoaderItem &item, guint uiWidth, guint uiHeight);
		virtual QuiverFile GetQuiverFile(gulong index);
		virtual void GetVisibleRange(gulong* pulStart, gulong* pulEnd);
		virtual void GetIconSize(guint* puiWidth, guint* puiHeight);
		virtual gulong GetNumItems();
		virtual void SetIsRunning(bool bIsRunning);
		virtual void SetCacheSize(guint uiCacheSize);
	
		
	private:
		ViewerImpl* m_pViewerImpl; 
		
	};

	IPreferencesEventHandlerPtr  m_PreferencesEventHandlerPtr;
	IImageListEventHandlerPtr    m_ImageListEventHandlerPtr;
	ViewerThumbLoader            m_ThumbnailLoader;      

};

void Viewer::ViewerImpl::SetImageList(IImageListViewPtr imgList)
{
	m_ImageListPtr->RemoveEventHandler(m_ImageListEventHandlerPtr);
	
	m_ImageListPtr = imgList;
	
	m_ImageListPtr->AddEventHandler(m_ImageListEventHandlerPtr);
	
}
// has image


void Viewer::ViewerImpl::UpdateUI()
{
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
	
	{
		/* For videos the zoom factor is applied in the pipeline (from the fit
		 * level up to 8x of the actual size); for stills it comes from the
		 * image view's magnification. */
		gboolean bCanZoomIn, bCanZoomOut;
		if (IsVideo())
		{
			bCanZoomIn = (m_dVideoZoomFinal < 16.0);
			bCanZoomOut = (m_dVideoZoomFinal > m_dVideoZoomMin);
		}
		else
		{
			bCanZoomIn = quiver_image_view_can_magnify(QUIVER_IMAGE_VIEW(m_pImageView), TRUE);
			bCanZoomOut = quiver_image_view_can_magnify(QUIVER_IMAGE_VIEW(m_pImageView), FALSE);
		}

		GAction* action;
		action = QuiverUtils::GetAction(ACTION_VIEWER_ZOOM_IN);
		if (NULL != action)
			g_simple_action_set_enabled(G_SIMPLE_ACTION(action), bCanZoomIn);
		action = QuiverUtils::GetAction(ACTION_VIEWER_ZOOM_OUT);
		if (NULL != action)
			g_simple_action_set_enabled(G_SIMPLE_ACTION(action), bCanZoomOut);
	}

	PreferencesPtr prefsPtr = Preferences::GetInstance();

	if (0 != m_iTimeoutSlideshowID)
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

		bool bShowFilmStrip = 	prefsPtr->GetBoolean(QUIVER_PREFS_VIEWER,QUIVER_PREFS_VIEWER_FILMSTRIP_SHOW);
		QuiverUtils::ToggleActionSetActive(ACTION_VIEWER_VIEW_FILM_STRIP, bShowFilmStrip ? TRUE : FALSE);
	}
}

void Viewer::ViewerImpl::UpdateScrollbars()
{
	gint width, height;
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
	
	GtkWidget * pNavigationBox = m_pNavigationBox;

// FIXME: remove
	//GtkTableChild * child = GetGtkTableChild(GTK_TABLE(m_pGrid),m_pImageView);
	
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
		gtk_widget_set_visible(pNavigationBox, FALSE);

	}
	else if ( (area_w < width && area_h < height) ||
		(area_w < width && area_h - sb_height < height) ||
		(area_w - sb_width < width && area_h < height) )
	{
		// show h show v
		gtk_widget_set_visible(pScrollbarV, TRUE); 	
		gtk_widget_set_visible(pScrollbarH, TRUE);
		gtk_widget_set_visible(pNavigationBox, TRUE);
	}
	else if (area_w < width)
	{
		// show h hide v
		gtk_widget_set_visible(pNavigationBox, TRUE);	
		gtk_widget_set_visible(pScrollbarV, FALSE);
		gtk_widget_set_visible(pScrollbarH, TRUE);		
	}
	else if (area_h < height)
	{
		// hide h show v
		gtk_widget_set_visible(pNavigationBox, TRUE);	
		gtk_widget_set_visible(pScrollbarH, FALSE);
		gtk_widget_set_visible(pScrollbarV, TRUE);	
	}
	else
	{
		// hide h hide v
		gtk_widget_set_visible(pScrollbarV, FALSE); 	
		gtk_widget_set_visible(pScrollbarH, FALSE);
		gtk_widget_set_visible(pNavigationBox, FALSE);
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

	LoadImageAtSize(f, width, height);
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
	
	if (m_ImageListPtr->SetCurrentIndex(index))
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

		if (IsVideo())
		{
			// show media controls but only the play button
			set_control_visible(m_pMediaControls, true);
			set_control_visible(m_pTimeElapsedLabel, false);
			set_control_visible(m_pTimeDurationLabel, false);
			set_control_visible(m_pRewindBtn, false);
			set_control_visible(m_pSpeedButton, false);
			set_control_visible(m_pFfBtn, false);
			set_control_visible(m_pSnapBtn, false);
			set_control_visible(m_pVolumeButton, false);
			set_control_visible(m_pVideoOptionsBtn, false);
			set_control_visible(m_pFullscreenBtn, false);
		}
		else
		{
			// hide them
			set_control_visible(m_pMediaControls, false);
		}

		gtk_window_set_default_size (GTK_WINDOW (m_pNavigationWindow),1,1);
		QuiverFile f = m_ImageListPtr->GetCurrent();
		GdkPixbuf *pixbuf = NULL;
		if (f.HasThumbnail(128)) {
			pixbuf = f.GetThumbnail(128);
		}
		if (NULL == pixbuf && f.HasThumbnail(256)) {
			pixbuf = f.GetThumbnail(256);
		}
		
		quiver_navigation_control_set_pixbuf(QUIVER_NAVIGATION_CONTROL(m_pNavigationControl),pixbuf);
		
		if (NULL != pixbuf)
		{
			g_object_unref(pixbuf);
		}
		
		LoadImage(f);
		
		quiver_icon_view_set_cursor_cell( QUIVER_ICON_VIEW(m_pIconView),
		      m_ImageListPtr->GetCurrentIndex() );	

		if (bCacheNext)
		{
			CacheNext(bDirectionForward);
		}
			
		g_signal_handlers_unblock_by_func(m_pIconView,(gpointer)viewer_iconview_cursor_changed,this);
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
		quiver_image_view_set_pixbuf(QUIVER_IMAGE_VIEW(m_pImageView),NULL);
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
		std::shared_ptr<Exiv2::ExifData> pExifData = f.GetExifData();

		if (NULL != pExifData.get())
		{
			// operator[] creates the entry if it doesn't exist
			(*pExifData)["Exif.Image.Orientation"] = m_iCurrentOrientation;

			f.SetExifData(pExifData);
		}

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

/* ── media controls opacity-based visibility ────────────────────── */

static void set_control_visible(GtkWidget* w, bool visible)
{
	if (!w) return;
	gtk_widget_set_opacity(w, visible ? 1.0 : 0.0);
	gtk_widget_set_can_focus(w, FALSE);
}

#define CONTROLS_FADE_STEP  0.12
#define CONTROLS_FADE_INTERVAL 16

static gboolean controls_fade_cb(gpointer user_data)
{
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl *)user_data;

	if (p->m_bControlsFadingIn)
	{
		p->m_dControlsFadeOpacity = MIN(p->m_dControlsFadeOpacity + CONTROLS_FADE_STEP, 1.0);
		gtk_widget_set_opacity(p->m_pMediaControls, p->m_dControlsFadeOpacity);
		if (p->m_dControlsFadeOpacity >= 1.0)
		{
			p->m_iTimeoutControlsFade = 0;
			return G_SOURCE_REMOVE;
		}
	}
	else
	{
		p->m_dControlsFadeOpacity = MAX(p->m_dControlsFadeOpacity - CONTROLS_FADE_STEP, 0.0);
		gtk_widget_set_opacity(p->m_pMediaControls, p->m_dControlsFadeOpacity);
		if (p->m_dControlsFadeOpacity <= 0.0)
		{
			set_control_visible(p->m_pMediaControls, false);
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

/* ── filmstrip overlay (continued) ──────────────────────────────── */

void Viewer::ViewerImpl::ShowFilmstripOverlay()
{
	if (!m_bFilmstripOverlay) return;
	if (!QuiverUtils::ToggleActionGetActive(ACTION_VIEWER_VIEW_FILM_STRIP)) return;
	if (m_bFilmstripHiddenByFS) return;
	if (IsVideo() && IsPlaying()) return;
	g_printerr("[FILMSTRIP] ShowFilmstripOverlay: visible=%d opacity=%.2f\n",
		gtk_widget_get_visible(m_pIconView), gtk_widget_get_opacity(m_pIconView));
	/* The icon view is created hidden and never shown again after a
	 * fade-out completes; a fade-in only animates opacity.  Reveal the
	 * widget itself or the strip can never be displayed. */
	if (!gtk_widget_get_visible(m_pIconView))
	{
		gtk_widget_set_visible(m_pIconView, TRUE);
		g_printerr("[FILMSTRIP] ShowFilmstripOverlay: revealed icon view\n");
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
		/* hide edge and filmstrip so they don't interfere with video controls;
		 * force-hide unconditionally even if the pointer is over the filmstrip
		 * (e.g. user pressed play while hovering the filmstrip) */
		CancelFilmstripFade();
		m_dFadeOpacity = gtk_widget_get_opacity(m_pIconView);
		if (m_dFadeOpacity > 0.0)
			StartFilmstripFade(false);
		else
			gtk_widget_set_visible(m_pIconView, FALSE);
		gtk_widget_set_visible(m_pFilmstripEdge, FALSE);
	}
	else
	{
		/* restore the hover edge */
	}
}

bool Viewer::ViewerImpl::IsPointerOverFilmstrip() const
{
	if (!m_bFilmstripOverlay || !m_pFilmstripOverlayContainer) return false;

	GtkNative *native = GTK_NATIVE(gtk_widget_get_native(m_pFilmstripOverlayContainer));
	if (!native) return false;
	GdkSurface *surface = gtk_native_get_surface(native);

	GdkDisplay *display = gtk_widget_get_display(m_pFilmstripOverlayContainer);
	GdkSeat *seat = gdk_display_get_default_seat(display);
	GdkDevice *device = gdk_seat_get_pointer(seat);
	double surface_x, surface_y;
	gdk_surface_get_device_position(surface, device, &surface_x, &surface_y, NULL);

	graphene_point_t src = GRAPHENE_POINT_INIT((float)surface_x, (float)surface_y);
	graphene_point_t dest;
	if (!gtk_widget_compute_point(m_pFilmstripOverlayContainer, GTK_WIDGET(native), &src, &dest))
		return false;
	return dest.x >= 0 && dest.x < gtk_widget_get_width(m_pFilmstripOverlayContainer)
		&& dest.y >= 0 && dest.y < gtk_widget_get_height(m_pFilmstripOverlayContainer);
}

bool Viewer::ViewerImpl::IsPointerOverMediaControls() const
{
	if (!m_pPlayButton || !m_pMediaControls) return false;

	GtkNative *native = GTK_NATIVE(gtk_widget_get_native(m_pMediaControls));
	if (!native) return false;
	GdkSurface *surface = gtk_native_get_surface(native);

	GdkDisplay *display = gtk_widget_get_display(m_pPlayButton);
	GdkSeat *seat = gdk_display_get_default_seat(display);
	GdkDevice *device = gdk_seat_get_pointer(seat);
	double surface_x, surface_y;
	gdk_surface_get_device_position(surface, device, &surface_x, &surface_y, NULL);

	graphene_point_t src = GRAPHENE_POINT_INIT((float)surface_x, (float)surface_y);
	graphene_point_t dest;
	if (!gtk_widget_compute_point(m_pMediaControls, GTK_WIDGET(native), &src, &dest))
		return false;
	float local_x = dest.x;
	float local_y = dest.y;

	/* X: anywhere across the full width of the media controls bar */
	bool in_x = local_x >= 0
		&& local_x < gtk_widget_get_width(m_pMediaControls);

	/* Y: at or below the media controls top, extending to its bottom */
	bool in_y = local_y >= 0
		&& local_y <= gtk_widget_get_height(m_pMediaControls);

	return in_x && in_y;
}

void Viewer::ViewerImpl::ScheduleFilmstripHide()
{
	if (!m_bFilmstripOverlay) return;
	if (IsPointerOverFilmstrip()) return;
	CancelFilmstripHide();
	m_iTimeoutFilmstripHide = g_timeout_add(800, filmstrip_hide_timeout_cb, this);
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
	int iFilmstripPos = prefsPtr->GetInteger(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_FILMSTRIP_POSITION, FSTRIP_POS_LEFT);
	bool bOverlay = prefsPtr->GetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_FILMSTRIP_OVERLAY, true);

	g_printerr("[FILMSTRIP] AddFilmstrip overlay=%d pos=%d toggle_active=%d\n",
		bOverlay, iFilmstripPos,
		QuiverUtils::ToggleActionGetActive(ACTION_VIEWER_VIEW_FILM_STRIP));

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
		 * visible while the mouse is anywhere over the filmstrip */
		{
			GtkEventController *motion = gtk_event_controller_motion_new();
			g_signal_connect(motion, "enter", G_CALLBACK(on_filmstrip_overlay_enter), this);
			g_signal_connect(motion, "leave", G_CALLBACK(on_filmstrip_overlay_leave), this);
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

/* The video paintable is referenced by the sink AND by us (g_object_get).
	 * The sink widget holds its own reference, so our ref can be dropped here;
	 * also disconnect the invalidate handler so it can never fire with this
	 * (freed) object after the pipeline teardown */
	if (m_pVideoPaintable)
	{
		g_signal_handlers_disconnect_matched(
			G_OBJECT(m_pVideoPaintable),
			G_SIGNAL_MATCH_DATA,
			0,
			0,
			NULL,
			NULL,
			this);
		g_object_unref(m_pVideoPaintable);
		m_pVideoPaintable = NULL;
	}

	CancelFilmstripHide();
		CancelFilmstripFade();

		/* icon view may have been hidden or faded out in overlay mode;
		 * ensure it is fully shown and opaque for docked mode */
		gtk_widget_set_visible(m_pIconView, TRUE);
		gtk_widget_set_opacity(m_pIconView, 1.0);
		g_printerr("[FILMSTRIP] docked: box=%p visible=%d opacity=%.2f imglist=%lu parent=%p\n",
			(void*)box,
			gtk_widget_get_visible(m_pIconView),
			gtk_widget_get_opacity(m_pIconView),
			(unsigned long)m_ImageListPtr->GetSize(),
			(void*)gtk_widget_get_parent(m_pIconView));
	}

	/* hand our reference over to the new parent container */
	if (NULL != current_parent)
	{
		g_object_unref(m_pIconView);
	}
}

static gboolean 
timeout_event_motion_notify (gpointer user_data)
{
	Viewer::ViewerImpl *pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)user_data;

	gboolean bKeepVisible = FALSE;

	// keep visible while the volume popup is open (it floats above the bar)
	{
		GtkWidget* popup = pViewerImpl->m_pVolumePopover;
		if (popup != NULL && gtk_widget_get_visible(popup))
			bKeepVisible = TRUE;
	}

	/* Note: gtk_menu_popup_at_widget grabs the pointer, so we don't need explicit keep visible for the gear menu */

	// ...and while the speed popover is open
	if (!bKeepVisible && pViewerImpl->m_pSpeedButton != NULL)
	{
		GtkPopover* speedPop = (GtkPopover*)g_object_get_data(G_OBJECT(pViewerImpl->m_pSpeedButton), "speed-popover");
		if (speedPop != NULL && gtk_widget_get_visible(GTK_WIDGET(speedPop)))
			bKeepVisible = TRUE;
	}

	// ...and while a GTK grab is active (e.g. dragging the timeline scale)
	if (!bKeepVisible && FALSE)
		bKeepVisible = TRUE;

	if (pViewerImpl->IsPlaying())
	{
		if (bKeepVisible)
		{
			pViewerImpl->m_iTimeoutMouseMotionNotify = g_timeout_add(1500,timeout_event_motion_notify,pViewerImpl);
		}
		else
		{
			pViewerImpl->StartControlsFade(false);
			pViewerImpl->m_iTimeoutMouseMotionNotify = 0;
		}
	}
	else
	{
		pViewerImpl->m_iTimeoutMouseMotionNotify = 0;
	}

	return FALSE;
}


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

	gst_element_seek_simple(GST_ELEMENT(p->m_pPipeline), format,
		GstSeekFlags(GST_SEEK_FLAG_FLUSH), target);
	return FALSE;
}

static void
viewer_scale_button_press_cb(GtkGestureClick *gesture, gint n_press, gdouble x, gdouble y, gpointer user_data)
{
	(void)gesture; (void)n_press; (void)x; (void)y;
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl *)user_data;
	p->m_bSeekDragging = TRUE;
	/* Keep the whole media bar (transport row + timeline + time labels)
	 * visible while seeking by cancelling any in-flight fade-out. */
	p->CancelControlsFade();
	set_control_visible(p->m_pMediaControls, true);
	/* ensure the transport row is not left hidden from a prior seek */
	if (p->m_pControlsBox)
		gtk_widget_set_visible(p->m_pControlsBox, TRUE);
}

static void
viewer_scale_button_release_cb(GtkGestureClick *gesture, gint n_press, gdouble x, gdouble y, gpointer user_data)
{
	(void)gesture; (void)n_press; (void)x; (void)y;
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl *)user_data;
	p->m_bSeekDragging = FALSE;
	/* restore the top transport row that had been hidden while dragging the scale */
	if (p->m_pControlsBox)
		gtk_widget_set_visible(p->m_pControlsBox, TRUE);
	p->RefreshAutoHideTimer();
}

/* Only treat a motion event as real "activity" when the pointer has actually
 * moved in surface coordinates.  GTK synthesizes motion events when a widget
 * re-lays-out under a parked pointer (e.g. the elapsed-time label growing and
 * nudging the transport row every tick); re-arming the auto-hide timer on
 * those kept the controls visible forever while the pointer sat still. */
static bool viewer_pointer_moved(Viewer::ViewerImpl *p)
{
	GdkDevice *dev = gdk_seat_get_pointer(gdk_display_get_default_seat(
		gtk_widget_get_display(p->m_pMediaControls)));
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

/* Connected on control buttons and the timeline scale via motion
 * controllers so the bar stays visible while hovering. */
static void controls_show_on_event_cb(GtkEventControllerMotion *controller, gdouble x, gdouble y, gpointer user_data)
{
	(void)controller; (void)x; (void)y;
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl *)user_data;
	/* Only re-show the media controls while the current item is a video.
	 * Otherwise hovering the transport buttons after navigating to a plain
	 * image would resurrect the controls over the image. */
	if (p->IsVideo())
	{
		if (!viewer_pointer_moved(p))
		{
			return;
		}
		gboolean was_hidden = gtk_widget_get_opacity(p->m_pMediaControls) < 0.5;
		if (was_hidden)
			set_control_visible(p->m_pMediaControls, true);
		p->RefreshAutoHideTimer();
	}
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
		gdouble srcPerPxX = 1., srcPerPxY = 1.;
		if (pViewerImpl->m_iVideoWidth > 0 && pViewerImpl->m_iVideoHeight > 0)
		{
			gdouble scale = MIN((gdouble)gtk_widget_get_width(pViewerImpl->m_pVideoFixed) / pViewerImpl->m_iVideoWidth,
				(gdouble)gtk_widget_get_height(pViewerImpl->m_pVideoFixed) / pViewerImpl->m_iVideoHeight);
			gdouble zoom = MAX(pViewerImpl->m_dVideoZoom, 1.0);
			if (scale * zoom > 0.)
			{
				srcPerPxX = 1. / (scale * zoom);
				srcPerPxY = srcPerPxX;
			}
		}
		pViewerImpl->m_dVideoPanX = pViewerImpl->m_dVideoPanStartPX - (x - pViewerImpl->m_dVideoPanStartRootX) * srcPerPxX;
		pViewerImpl->m_dVideoPanY = pViewerImpl->m_dVideoPanStartPY - (y - pViewerImpl->m_dVideoPanStartRootY) * srcPerPxY;
		pViewerImpl->ApplyVideoZoom();
		pViewerImpl->RefreshAutoHideTimer();
		return;
	}

	if (widget == pViewerImpl->m_pImageView || widget == pViewerImpl->m_pVideoFixed
		|| widget == pViewerImpl->m_pVideoSinkWidget)
	{
		if (!viewer_pointer_moved(pViewerImpl))
		{
			return;
		}

		if (0 != pViewerImpl->m_iTimeoutMouseMotionNotify)
		{
			g_source_remove(pViewerImpl->m_iTimeoutMouseMotionNotify);
			pViewerImpl->m_iTimeoutMouseMotionNotify = 0;
		}

		if (0 != pViewerImpl->m_ImageListPtr->GetSize() && 
				pViewerImpl->m_ImageListPtr->GetCurrent().IsVideo())
		{
			/* Only re-show the controls on motion if they are not already
			 * visible.  Once visible, motion merely re-arms the auto-hide
			 * timer, so a fade-out that has started is allowed to complete
			 * instead of being cancelled on every motion event (which is why
			 * the controls never hid while the cursor stayed in the viewer). */
			if (gtk_widget_get_opacity(pViewerImpl->m_pMediaControls) < 0.5)
			{
				pViewerImpl->CancelControlsFade();
				set_control_visible(pViewerImpl->m_pMediaControls, true);
				pViewerImpl->UpdateTimelineVisibility();
			}
		}

		if (pViewerImpl->IsPlaying())
		{
			pViewerImpl->m_iTimeoutMouseMotionNotify = g_timeout_add(1500,timeout_event_motion_notify,pViewerImpl);
		}
	}
}

static void viewer_radio_action_handler_cb(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	viewer_action_handler_cb(action, parameter, user_data);
}

static void viewer_action_handler_cb(GSimpleAction *action, GVariant *parameter, gpointer data)
{ (void)parameter; 
	Viewer::ViewerImpl *pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)data;
	
	QuiverImageView *imageview = QUIVER_IMAGE_VIEW(pViewerImpl->m_pImageView);

	PreferencesPtr prefsPtr = Preferences::GetInstance();
	
	//printf("Viewer Action: %s\n",g_action_get_name(G_ACTION(action)));
	
	const gchar * szAction = g_action_get_name(G_ACTION(action));
	
	if (0 == strcmp(szAction, ACTION_VIEWER_ZOOM_FIT)
		|| 0 == strcmp(szAction, ACTION_VIEWER_ZOOM_FIT_STRETCH)
		|| 0 == strcmp(szAction, ACTION_VIEWER_ZOOM_100)
		|| 0 == strcmp(szAction, ACTION_VIEWER_ZOOM_FILL_SCREEN))
	{
		QuiverImageViewMode zoom_mode = (QuiverImageViewMode)QuiverUtils::GetRadioActionCurrent(szAction);
		if (pViewerImpl->IsVideo())
		{
			/* update the image view first so ApplyVideoZoom reads the new mode */
			quiver_image_view_set_view_mode(imageview, zoom_mode);
			/* re-center the visible viewport when changing modes during
			 * playback, just like the first ApplyVideoZoom after a reset */
			pViewerImpl->m_dVideoLastWidgetW = 0.;
			pViewerImpl->ApplyVideoZoom();
			pViewerImpl->UpdateUI();
			if (!pViewerImpl->IsPlaying())
			{
				gint64 pos = 0;
				if (gst_element_query_position(GST_ELEMENT(pViewerImpl->m_pPipeline), GST_FORMAT_TIME, &pos))
				{
					gst_element_seek_simple(GST_ELEMENT(pViewerImpl->m_pPipeline),
						GST_FORMAT_TIME,
						GstSeekFlags(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT),
						pos);
				}
			}
		}
		else
		{
			quiver_image_view_set_view_mode(imageview, zoom_mode);
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

		if (QUIVER_IMAGE_VIEW_MODE_ZOOM != quiver_image_view_get_view_mode(imageview))
			quiver_image_view_set_view_mode(imageview,QUIVER_IMAGE_VIEW_MODE_ZOOM);
				
		quiver_image_view_set_magnification(imageview,
						quiver_image_view_get_magnification(imageview)*1.25);
	}
	else if (0 == strcmp(szAction, ACTION_VIEWER_ZOOM_OUT)
	)
	{
		if (pViewerImpl->IsVideo())
		{
			pViewerImpl->SetVideoZoom(pViewerImpl->m_dVideoZoomFinal / 1.25);
			return;
		}

		if (QUIVER_IMAGE_VIEW_MODE_ZOOM != quiver_image_view_get_view_mode(imageview))
			quiver_image_view_set_view_mode(imageview,QUIVER_IMAGE_VIEW_MODE_ZOOM);
				
		quiver_image_view_set_magnification(imageview,
			quiver_image_view_get_magnification(imageview)/1.25);
	}
	else if (0 == strcmp(szAction,ACTION_VIEWER_ROTATE_CW) || 0 == strcmp(szAction,ACTION_VIEWER_ROTATE_CW_2))
	{
		quiver_image_view_rotate(imageview,TRUE);
		pViewerImpl->SetCurrentOrientation( orientation_matrix[ORIENTATION_ROTATE_CW][pViewerImpl->GetCurrentOrientation()] );
	}
	else if (0 == strcmp(szAction,ACTION_VIEWER_ROTATE_CCW) || 0 == strcmp(szAction,ACTION_VIEWER_ROTATE_CCW_2))
	{
		quiver_image_view_rotate(imageview,FALSE);
		pViewerImpl->SetCurrentOrientation( orientation_matrix[ORIENTATION_ROTATE_CCW][pViewerImpl->GetCurrentOrientation()] );
	}
	else if (0 == strcmp(szAction,ACTION_VIEWER_FLIP_H) || 0 == strcmp(szAction,ACTION_VIEWER_FLIP_H_2))
	{
		quiver_image_view_flip(imageview,TRUE);
		pViewerImpl->SetCurrentOrientation( orientation_matrix[ORIENTATION_FLIP_H][pViewerImpl->GetCurrentOrientation()] );
	}
	else if (0 == strcmp(szAction,ACTION_VIEWER_FLIP_V) || 0 == strcmp(szAction,ACTION_VIEWER_FLIP_V_2))
	{
		quiver_image_view_flip(imageview,FALSE);
		pViewerImpl->SetCurrentOrientation( orientation_matrix[ORIENTATION_FLIP_V][pViewerImpl->GetCurrentOrientation()] );
	}
	else if (0 == strcmp(szAction,ACTION_VIEWER_VIDEO_PLAY) || 0 == strcmp(szAction,ACTION_VIEWER_VIDEO_PLAY_2))
	{
		pViewerImpl->PlayPauseVideo();
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
		viewer_skip_fwd_cb(pViewerImpl);
	}
	else if (0 == strcmp(szAction,ACTION_VIEWER_VIDEO_SEEK_BACK_5))
	{
		viewer_skip_back_cb(pViewerImpl);
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
	else if (g_str_has_prefix(szAction, "VideoSpeed"))
	{
		int idx = QuiverUtils::GetRadioActionCurrent(szAction);
		double speeds[] = { 0.25, 0.5, 1.0, 1.5, 2.0, 4.0, 8.0, 16.0 };
		if (idx >= 0 && idx < 8)
		{
			pViewerImpl->m_dPlaybackSpeed = speeds[idx];
			gchar* label = g_strdup_printf("<b>%gx</b>", speeds[idx]);
			if (pViewerImpl->m_pSpeedLabel) gtk_label_set_markup(GTK_LABEL(pViewerImpl->m_pSpeedLabel), label);
			g_free(label);
			if (pViewerImpl->m_pPipeline)
			{
				// We need to seek to apply speed
				gint64 pos = 0;
				gst_element_query_position(GST_ELEMENT(pViewerImpl->m_pPipeline), GST_FORMAT_TIME, &pos);
				gst_element_seek(GST_ELEMENT(pViewerImpl->m_pPipeline), pViewerImpl->m_dPlaybackSpeed, GST_FORMAT_TIME,
					(GstSeekFlags)(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE),
					GST_SEEK_TYPE_SET, pos, GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);
			}
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
	else if (0 == strcmp(szAction, ACTION_VIEWER_TRASH))
	{
		gint rval = GTK_RESPONSE_YES;

		QuiverFile f = pViewerImpl->m_ImageListPtr->GetCurrent();

/*
		string strDlgText;
		strDlgText = "Move the selected image to the trash?";
		GtkWidget* dialog = gtk_message_dialog_new (NULL,GTK_DIALOG_MODAL,
								GTK_MESSAGE_QUESTION,GTK_BUTTONS_YES_NO,strDlgText.c_str());
		rval = gtk_dialog_run(GTK_DIALOG(dialog));

		gtk_widget_destroy(dialog);
	
*/
		switch (rval)
		{
			case GTK_RESPONSE_YES:
			{
				// delete the items!
				if (QuiverFileOps::MoveToTrash(f))
				{
					pViewerImpl->m_ImageListPtr->Remove(pViewerImpl->m_ImageListPtr->GetCurrentIndex());
					pViewerImpl->SetImageIndex(pViewerImpl->m_ImageListPtr->GetCurrentIndex(),true);
				}
				break;
			}
			case GTK_RESPONSE_NO:
				//fall through
			default:
				// do not delete
				break;
		}
	}
	else if (0 == strcmp(szAction, ACTION_VIEWER_COPY))
	{
		GdkClipboard* clipboard = gdk_display_get_clipboard(gdk_display_get_default());
		
		string strClipText;
		if (pViewerImpl->m_ImageListPtr->GetSize())
		{
			string strPath = pViewerImpl->m_ImageListPtr->GetCurrent().GetFilePath();
			gdk_clipboard_set_text (clipboard, strPath.c_str());
		}		
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
  			quiver_image_view_set_view_mode(
  				QUIVER_IMAGE_VIEW(pViewerImpl->m_pImageView),
  				QUIVER_IMAGE_VIEW_MODE_ZOOM);
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
	pViewerImpl->m_StatusbarPtr->SetMagnification((int)(mag*100+.5));
	
	pViewerImpl->UpdateUI();

}

static void viewer_imageview_view_mode_changed(QuiverImageView *imageview,gpointer data)
{ (void)imageview; 
	Viewer::ViewerImpl *pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)data;
	
	QuiverImageViewMode mode = quiver_image_view_get_view_mode(QUIVER_IMAGE_VIEW(pViewerImpl->m_pImageView));

	const gchar *action_name = NULL;
	switch (mode)
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
			action_name = ACTION_VIEWER_ZOOM;
			break;
		case QUIVER_IMAGE_VIEW_MODE_FILL_SCREEN:
			action_name = ACTION_VIEWER_ZOOM_FILL_SCREEN;
		case QUIVER_IMAGE_VIEW_MODE_COUNT:
		default:
			break;
	}

	QuiverImageViewMode unmagnified_mode = 
		quiver_image_view_get_view_mode_unmagnified(QUIVER_IMAGE_VIEW(pViewerImpl->m_pImageView));

	PreferencesPtr prefsPtr = Preferences::GetInstance();
	prefsPtr->SetInteger(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_DEFAULT_VIEW_MODE, unmagnified_mode);

	if (NULL != action_name)
	{
		QuiverUtils::SetRadioActionCurrent(action_name, mode);
	}
}

static gboolean viewer_imageview_key_press_event(GtkEventControllerKey *controller, guint keyval, guint keycode, GdkModifierType state, gpointer userdata)
{ (void)controller; (void)keycode; (void)state; 
	Viewer::ViewerImpl *pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)userdata;

	gboolean rval = FALSE;
	bool bPanMode = true;

	GtkAdjustment *h = pViewerImpl->m_pAdjustmentH;
	GtkAdjustment *v = pViewerImpl->m_pAdjustmentV;

	if (gtk_adjustment_get_page_size(h) >= gtk_adjustment_get_upper(h) &&
		gtk_adjustment_get_page_size(v) >= gtk_adjustment_get_upper(v))
	{
		bPanMode = false;

	}
	
	GtkAdjustment *adjustment = NULL;
	gdouble increment = 0.;

	if (GDK_KEY_Left == keyval || GDK_KEY_Up == keyval)
	{
		if (bPanMode)
		{
			if (GDK_KEY_Left == keyval)
			{
				adjustment = h;
			}
			else
			{
				adjustment = v;
			}
			increment = -gtk_adjustment_get_step_increment(adjustment);
			
		}
		else
		{
			GAction* action = QuiverUtils::GetAction( ACTION_VIEWER_PREVIOUS);
			g_action_activate(action,NULL);
		}
		rval = TRUE;
	}
	else if (GDK_KEY_Right == keyval || GDK_KEY_Down == keyval)
	{
		if (bPanMode)
		{
			if (GDK_KEY_Right == keyval)
			{
				adjustment = h;
			}
			else
			{
				adjustment = v;
			}
			increment = gtk_adjustment_get_step_increment(adjustment);
		}
		else
		{
			GAction* action = QuiverUtils::GetAction( ACTION_VIEWER_NEXT);
			g_action_activate(action,NULL);
		}
		rval = TRUE;
	}

	if (NULL != adjustment)
	{
		gdouble value = gtk_adjustment_get_value(adjustment);
		value += increment;

		if (value < gtk_adjustment_get_lower(adjustment))
		{
			value = gtk_adjustment_get_lower(adjustment);
		}
		else if (value > gtk_adjustment_get_upper(adjustment) - gtk_adjustment_get_page_size(adjustment))
		{
			value = gtk_adjustment_get_upper(adjustment) - gtk_adjustment_get_page_size(adjustment);
		}
		gtk_adjustment_set_value(adjustment,value);
	}

	return rval;
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


static void viewer_video_option_audio_cb(GtkButton *button, gpointer user_data)
{
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl*)user_data;
	gint track = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "track-id"));
	g_object_set(G_OBJECT(p->m_pPipeline), "current-audio", track, NULL);
}

static void viewer_video_option_text_cb(GtkButton *button, gpointer user_data)
{
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl*)user_data;
	gint track = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "track-id"));
	
	GstPlayFlags flags = (GstPlayFlags)0;
	g_object_get(G_OBJECT(p->m_pPipeline), "flags", &flags, NULL);
	
	if (track < 0) {
		flags = (GstPlayFlags)(flags & ~(1 << 2)); // disable GST_PLAY_FLAG_TEXT
		g_object_set(G_OBJECT(p->m_pPipeline), "flags", flags, NULL);
	} else {
		flags = (GstPlayFlags)(flags | (1 << 2)); // enable GST_PLAY_FLAG_TEXT
		g_object_set(G_OBJECT(p->m_pPipeline), "flags", flags, "current-text", track, NULL);
	}
}

static void viewer_video_option_rotate_cb(GtkButton *button, gpointer user_data)
{
	(void)button;
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl*)user_data;
	if (p->m_VideoZoomType == Viewer::ViewerImpl::VIDEO_ZOOM_GL && p->m_pVideoZoomScaler != NULL)
	{
		gfloat rot = 0.0f;
		g_object_get(G_OBJECT(p->m_pVideoZoomScaler), "rotation-z", &rot, NULL);
		rot += 90.0f;
		if (rot >= 360.0f) rot -= 360.0f;
		g_object_set(G_OBJECT(p->m_pVideoZoomScaler), "rotation-z", rot, NULL);
	}
}

static void viewer_video_options_popover_closed_cb(GtkPopover *popover, gpointer user_data)
{
	(void)user_data;
	/* each options popover is created on click and parented to the button;
	 * on close, unparent it so it is destroyed and cannot linger as a child
	 * of the button (a later button finalize would leave a dangling parent
	 * pointer in the child). */
	GtkWidget *w = GTK_WIDGET(popover);
	if (gtk_widget_get_parent(w))
		gtk_widget_unparent(w);
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl*)user_data;
	if (p != NULL && p->m_pVideoOptionsPopover == w)
		p->m_pVideoOptionsPopover = NULL;
}

static void viewer_video_options_btn_clicked_cb(GtkButton *button, gpointer user_data)
{
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl*)user_data;

	GtkWidget *popover = gtk_popover_new();
	gtk_widget_set_parent(popover, GTK_WIDGET(button));
	gtk_popover_set_autohide(GTK_POPOVER(popover), TRUE);
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_popover_set_child(GTK_POPOVER(popover), box);
	g_signal_connect(G_OBJECT(popover), "closed", G_CALLBACK(viewer_video_options_popover_closed_cb), p);
	p->m_pVideoOptionsPopover = popover;

	gint n_audio = 0;
	g_object_get(p->m_pPipeline, "n-audio", &n_audio, NULL);
	gint current_audio = -1;
	g_object_get(p->m_pPipeline, "current-audio", &current_audio, NULL);
	
	if (n_audio > 0) {
		GtkWidget *mi = gtk_button_new_with_label("Audio Tracks:");
		gtk_widget_set_sensitive(mi, FALSE);
		gtk_box_append(GTK_BOX(box), mi);
		
		for (gint i = 0; i < n_audio; ++i) {
			GstTagList *tags = NULL;
			g_signal_emit_by_name(p->m_pPipeline, "get-audio-tags", i, &tags);
			gchar *lang = NULL;
			if (tags) {
				gst_tag_list_get_string(tags, GST_TAG_LANGUAGE_CODE, &lang);
				gst_tag_list_free(tags);
			}
			gchar *label = g_strdup_printf("%sTrack %d%s", 
				(i == current_audio) ? "✓ " : "   ", 
				i + 1, 
				lang ? g_strdup_printf(" (%s)", lang) : "");
			GtkWidget *item = gtk_button_new_with_label(label);
			g_free(label);
			if (lang) g_free(lang);
			
			g_object_set_data(G_OBJECT(item), "track-id", GINT_TO_POINTER(i));
			g_signal_connect(item, "clicked", G_CALLBACK(viewer_video_option_audio_cb), p);
			gtk_box_append(GTK_BOX(box), item);
		}
		gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
	}
	
	gint n_text = 0;
	g_object_get(p->m_pPipeline, "n-text", &n_text, NULL);
	gint current_text = -1;
	g_object_get(p->m_pPipeline, "current-text", &current_text, NULL);
	
	GstPlayFlags flags = (GstPlayFlags)0;
	g_object_get(p->m_pPipeline, "flags", &flags, NULL);
	gboolean text_enabled = (flags & (1 << 2)) != 0; // GST_PLAY_FLAG_TEXT
	
	GtkWidget *mi = gtk_button_new_with_label("Subtitles:");
	gtk_widget_set_sensitive(mi, FALSE);
	gtk_box_append(GTK_BOX(box), mi);
	
	GtkWidget *item_off = gtk_button_new_with_label(!text_enabled ? "✓ Off" : "   Off");
	g_object_set_data(G_OBJECT(item_off), "track-id", GINT_TO_POINTER(-1));
	g_signal_connect(item_off, "clicked", G_CALLBACK(viewer_video_option_text_cb), p);
	gtk_box_append(GTK_BOX(box), item_off);
	
	if (n_text > 0) {
		for (gint i = 0; i < n_text; ++i) {
			GstTagList *tags = NULL;
			g_signal_emit_by_name(p->m_pPipeline, "get-text-tags", i, &tags);
			gchar *lang = NULL;
			if (tags) {
				gst_tag_list_get_string(tags, GST_TAG_LANGUAGE_CODE, &lang);
				gst_tag_list_free(tags);
			}
			gchar *label = g_strdup_printf("%sTrack %d%s", 
				(text_enabled && i == current_text) ? "✓ " : "   ", 
				i + 1, 
				lang ? g_strdup_printf(" (%s)", lang) : "");
			GtkWidget *item = gtk_button_new_with_label(label);
			g_free(label);
			if (lang) g_free(lang);
			
			g_object_set_data(G_OBJECT(item), "track-id", GINT_TO_POINTER(i));
			g_signal_connect(item, "clicked", G_CALLBACK(viewer_video_option_text_cb), p);
			gtk_box_append(GTK_BOX(box), item);
		}
	}
	
	if (p->m_VideoZoomType == Viewer::ViewerImpl::VIDEO_ZOOM_GL) {
		gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
		GtkWidget *item_rot = gtk_button_new_with_label("Rotate 90°");
		g_signal_connect(item_rot, "clicked", G_CALLBACK(viewer_video_option_rotate_cb), p);
		gtk_box_append(GTK_BOX(box), item_rot);
	}
	
	gtk_popover_popup(GTK_POPOVER(popover));
}

static void
viewer_volume_value_changed (GtkRange *range, gdouble value, gpointer user_data)
{ (void)value; 
	Viewer::ViewerImpl *pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)user_data;
	pViewerImpl->m_dVolume = gtk_range_get_value(range);
	g_object_set(G_OBJECT(pViewerImpl->m_pPipeline), "volume", pViewerImpl->m_dVolume, NULL);
}

static void viewer_volume_popover_closed_cb(GtkPopover *popover, gpointer user_data)
{
	(void)popover;
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl *)user_data;
	if (p->m_pVolumeButton != NULL)
	{
		/* reset the toggle so it doesn't stay visually pressed.  set_active
		 * re-emits "toggled", but our toggle handler sees active==FALSE and
		 * only hides an already-hidden popover, so there is no recursion. */
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p->m_pVolumeButton), FALSE);
	}
}

static void viewer_volume_toggle_popover_cb(gpointer user_data, GtkWidget *widget)
{
	(void)widget;
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl *)user_data;
	if (p->m_pVolumeButton == NULL || p->m_pVolumePopover == NULL) return;
	/* The volume button is a GtkToggleButton, so GTK already flipped its
	 * active state when the user clicked it.  Show or hide the (parented,
	 * anchored) popover to match.  Never call set_active() here: a toggle
	 * button emits "toggled" (and this handler) again, recursing to a crash. */
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(p->m_pVolumeButton)))
		gtk_popover_popup(GTK_POPOVER(p->m_pVolumePopover));
	else
		gtk_widget_set_visible(p->m_pVolumePopover, FALSE);
}


static void
viewer_navigation_button_press_event(GtkGestureClick *gesture, gint n_press, gdouble x, gdouble y, gpointer userdata)
{
	(void)gesture; (void)n_press;
	Viewer::ViewerImpl *pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)userdata;
	
	gtk_widget_set_visible(pViewerImpl->m_pNavigationWindow, TRUE);

	gint w,h;
	gtk_window_get_default_size(GTK_WINDOW (pViewerImpl->m_pNavigationWindow),&w,&h);
  	int ww, wh, pos_x,pos_y;
  	ww = w+2;
  	wh = h+2;
  	
  	pos_x = (int)x - ww/2;
  	pos_y = (int)y - wh/2;
  		
	
  	GdkDisplay *display = gdk_display_get_default();
    GdkMonitor *monitor = gdk_display_get_monitor_at_surface(display, gtk_native_get_surface(gtk_widget_get_native(pViewerImpl->m_pNavigationWindow)));
    GdkRectangle workarea;
    gdk_monitor_get_geometry(monitor, &workarea);
    if (workarea.width < pos_x + ww)
        pos_x = workarea.width - ww;
    else if (0 > pos_x)
        pos_x = 0;
    if (workarea.height < pos_y + wh)
        pos_y = workarea.height - wh;
  	else if (0 > pos_y)
  		pos_y = 0;
	
	GdkDisplay* nav_display = gtk_widget_get_display(GTK_WIDGET(gtk_widget_get_root(pViewerImpl->m_pNavigationWindow)));
	(void)nav_display;
}

void navigation_control_button_release_event (GtkGestureClick *gesture, gint n_press, gdouble x, gdouble y, gpointer data )
{
	(void)gesture; (void)n_press; (void)x; (void)y;
	Viewer::ViewerImpl *pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)data;

	gtk_widget_set_visible(pViewerImpl->m_pNavigationWindow, FALSE);
}

static GdkContentProvider* signal_drag_source_prepare(GtkDragSource *source, gdouble x, gdouble y, gpointer user_data)
{ (void)source; (void)x; (void)y; 
	Viewer::ViewerImpl *pViewerImpl = (Viewer::ViewerImpl*)user_data;
	
	const gchar* uri = NULL;
	if (pViewerImpl->m_ImageListPtr->GetSize())
		uri = pViewerImpl->m_ImageListPtr->GetCurrent().GetURI();

	if (uri == NULL)
		return NULL;

	GValue value = G_VALUE_INIT;
	g_value_init(&value, G_TYPE_STRING);
	g_value_set_string(&value, uri);
	GdkContentProvider *provider = gdk_content_provider_new_for_value(&value);
	g_value_unset(&value);
	return provider;
}

static void signal_drag_begin (GtkDragSource *source, GdkDrag *drag, gpointer user_data)
{ (void)source; (void)drag; 
	Viewer::ViewerImpl *pViewerImpl = (Viewer::ViewerImpl*)user_data;
	
	GdkPixbuf *thumb = pViewerImpl->m_ImageListPtr->GetCurrent().GetThumbnail(128);

	if (NULL != thumb)
	{
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
		GdkTexture *texture = gdk_texture_new_for_pixbuf(thumb);
G_GNUC_END_IGNORE_DEPRECATIONS
		if (texture) {
			gtk_drag_source_set_icon(source, GDK_PAINTABLE(texture), -2, -2);
			g_object_unref(texture);
		}
		g_object_unref(thumb);
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
	gint64 secs  = GST_TIME_AS_SECONDS(time);
	gint64 mins  = secs / 60;
	gint64 hours = mins / 60;
	mins = mins - hours*60;
	secs = secs - mins*60;

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
	switch (GST_MESSAGE_TYPE (msg)) {

		case GST_MESSAGE_EOS:
			{
				// reset state to ready
				gst_element_set_state(GST_ELEMENT(pViewerImpl->m_pPipeline), GST_STATE_READY);
				pViewerImpl->StopVideo(true);
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

				g_warning("Video playback error: %s", error->message);
				if (debug && *debug)
					g_warning("Video playback error debug: %s", debug);
				g_free (debug);

				g_error_free (error);

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
	if (m_pStack != NULL)
	{
		gtk_stack_set_visible_child_name(GTK_STACK(m_pStack), "video");
	}
	/* The picture is a child of the video fixed page; GtkStack shows the
	 * fixed page itself.  Still, StopVideo() explicitly hides the picture, so
	 * re-show it when the video page is brought up, or the sink picture stays
	 * invisible while audio plays. */
	if (m_pVideoFixed != NULL)
		gtk_widget_set_visible(m_pVideoFixed, TRUE);
	if (m_pVideoSinkWidget != NULL)
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
		GstState current;
		// has the right video
		GstStateChangeReturn rval = gst_element_get_state(GST_ELEMENT(m_pPipeline), &current, NULL, GST_SECOND);
		if (GST_STATE_CHANGE_SUCCESS == rval)
		{
			if (GST_STATE_PLAYING == current)
			{
				gst_element_set_state(GST_ELEMENT(m_pPipeline), GST_STATE_PAUSED);
				CancelControlsFade();
				set_control_visible(m_pMediaControls, true);
				UpdateTimelineVisibility();
				SetIsPlaying(false);
			}
			else
			{
				SetIsPlaying(true);
				gst_element_set_state(GST_ELEMENT(m_pPipeline), GST_STATE_PLAYING);

				if (0 != m_iTimeoutMouseMotionNotify)
				{
					g_source_remove(m_iTimeoutMouseMotionNotify);
					m_iTimeoutMouseMotionNotify = 0;
				}

				m_iTimeoutMouseMotionNotify = g_timeout_add(1500,timeout_event_motion_notify,this);
			}
		}
	}
	else
	{
		StopVideo(false);
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
		g_object_set(G_OBJECT(m_pPipeline), "uri", m_ImageListPtr->GetCurrent().GetURI(), NULL);
		gst_element_set_state(GST_ELEMENT(m_pPipeline), GST_STATE_PLAYING);

		SetIsPlaying(true);

		if (0 != m_iTimeoutMouseMotionNotify)
		{
			g_source_remove(m_iTimeoutMouseMotionNotify);
			m_iTimeoutMouseMotionNotify = 0;
		}

		m_iTimeoutMouseMotionNotify = g_timeout_add(1500,timeout_event_motion_notify,this);
	}
	g_free(uri);
}

void Viewer::ViewerImpl::SkipForward()
{
	{
		GstFormat format = GST_FORMAT_TIME;
		gint64 clip_duration = 0;
		gint64 pos = 0;

		gboolean queried = gst_element_query_duration(GST_ELEMENT(m_pPipeline), format, &clip_duration);
		queried |= gst_element_query_position(m_pPipeline, format, &pos);
		if (queried)
		{
			gboolean seek_started = gst_element_seek_simple(GST_ELEMENT(m_pPipeline), format, GstSeekFlags(GST_SEEK_FLAG_FLUSH), std::min(clip_duration, pos + GST_SECOND*10));
 (void)seek_started;
			CancelControlsFade();
			set_control_visible(m_pMediaControls, true);
			if (0 != m_iTimeoutMouseMotionNotify)
			{
				g_source_remove(m_iTimeoutMouseMotionNotify);
				m_iTimeoutMouseMotionNotify = 0;
			}

			m_iTimeoutMouseMotionNotify = g_timeout_add(1500,timeout_event_motion_notify,this);
		}
	}
}

void Viewer::ViewerImpl::SkipBack()
{
	{
		GstFormat format = GST_FORMAT_TIME;
		gint64 clip_duration = 0;
		gint64 pos = 0;

		gboolean queried = gst_element_query_duration(GST_ELEMENT(m_pPipeline), format, &clip_duration);
		queried |= gst_element_query_position(m_pPipeline, format, &pos);
		if (queried)
		{
			gboolean seek_started = gst_element_seek_simple(GST_ELEMENT(m_pPipeline), format, GstSeekFlags(GST_SEEK_FLAG_FLUSH), std::max((gint64)0,pos - GST_SECOND*10));
 (void)seek_started;
			CancelControlsFade();
			set_control_visible(m_pMediaControls, true);
			if (0 != m_iTimeoutMouseMotionNotify)
			{
				g_source_remove(m_iTimeoutMouseMotionNotify);
				m_iTimeoutMouseMotionNotify = 0;
			}

			m_iTimeoutMouseMotionNotify = g_timeout_add(1500,timeout_event_motion_notify,this);
		}
	}
}

void Viewer::ViewerImpl::StopVideo(bool reloadImage /* = true */)
{
	SetIsPlaying(false);
	if (0 != m_iTimeoutMouseMotionNotify)
	{
		g_source_remove(m_iTimeoutMouseMotionNotify);
		m_iTimeoutMouseMotionNotify = 0;
	}

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
	//gtk_widget_set_double_buffered (m_pImageView, TRUE); // Double buffering handled by gtk4sink
	gtk_stack_set_visible_child_name(GTK_STACK(m_pStack), "image");

	/* reset the digital zoom so the next video starts at fit, and drop the
	 * stale frame size so a different-sized video is scaled to its own
	 * dimensions until the caps probe reports them */
	m_dVideoZoom = 1.0;
	m_dVideoZoomFinal = 1.0;
	m_dVideoZoomMin = 1.0;
	m_dPlaybackSpeed = 1.0;
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
	m_dVideoPanX = 0.;
	m_dVideoPanY = 0.;
	m_dVideoLastWidgetW = 0.;
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

	if (IsVideo())
	{
		CancelControlsFade();
		set_control_visible(m_pMediaControls, true);
	}

	if (reloadImage && 0 != m_ImageListPtr->GetSize())
	{
		if (gtk_widget_get_mapped(m_pImageView))
			gtk_widget_queue_draw(m_pImageView);
		if (0 == m_iTimeoutSlideshowID)
			LoadImage(m_ImageListPtr->GetCurrent());
	}
}

bool Viewer::ViewerImpl::IsVideo()
{
	return (0 != m_ImageListPtr->GetSize() && 
		m_ImageListPtr->GetCurrent().IsVideo());
}

void Viewer::ViewerImpl::SetPlaybackSpeed(double speed)
{
	if (speed <= 0.0) speed = 1.0;
	m_dPlaybackSpeed = speed;
	gchar* label = g_strdup_printf("<b>%gx</b>", speed);
	gtk_label_set_markup(GTK_LABEL(m_pSpeedLabel), label);
	g_free(label);

	if (IsPlaying())
	{
		gint64 pos = 0;
		gst_element_query_position(m_pPipeline, GST_FORMAT_TIME, &pos);
		gst_element_seek(GST_ELEMENT(m_pPipeline), speed,
			GST_FORMAT_TIME,
			GstSeekFlags(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE),
			GST_SEEK_TYPE_SET, pos, GST_SEEK_TYPE_NONE, 0);
	}
	else
	{
		gint64 pos = 0;
		if (gst_element_query_position(GST_ELEMENT(m_pPipeline), GST_FORMAT_TIME, &pos))
		{
			gst_element_seek(GST_ELEMENT(m_pPipeline), speed,
				GST_FORMAT_TIME,
				GstSeekFlags(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE),
				GST_SEEK_TYPE_SET, pos, GST_SEEK_TYPE_NONE, 0);
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
	GdkPixbuf* pixbuf = QuiverVideoOps::LoadPixbuf(uri, NULL, NULL, current_pos);
	if (pixbuf != NULL)
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
			GError *err = NULL;
			if (!gdk_pixbuf_save(pixbuf, snap_name, "png", &err, NULL))
			{
				g_warning("Snapshot save failed: %s", err ? err->message : "unknown error");
				g_error_free(err);
			}
			g_free(snap_name);
			g_free(dir);
			g_free(base);
			g_free(path);
		}
		g_object_unref(pixbuf);
	}
}

static void 
viewer_button_release_cb(GtkGestureClick *gesture, gint n_press, gdouble x, gdouble y, gpointer user_data)
{
	(void)n_press;
	GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
	Viewer::ViewerImpl *pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)user_data;
	if ((widget == pViewerImpl->m_pVideoFixed || widget == pViewerImpl->m_pVideoSinkWidget)
		&& pViewerImpl->m_bVideoPanning)
	{
		pViewerImpl->m_bVideoPanning = FALSE;

		/* a click (no meaningful drag) toggles play/pause */
		if (ABS(x - pViewerImpl->m_dVideoPanStartRootX) < 5.
			&& ABS(y - pViewerImpl->m_dVideoPanStartRootY) < 5.)
		{
			if (pViewerImpl->IsVideo())
				pViewerImpl->PlayPauseVideo();
		}
		pViewerImpl->RefreshAutoHideTimer();
	}
}

static void 
viewer_button_press_cb(GtkGestureClick *gesture, gint n_press, gdouble x, gdouble y, gpointer user_data)
{
	(void)n_press;
	GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
	Viewer::ViewerImpl *pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)user_data;

	guint button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));

	if (widget == pViewerImpl->m_pImageView || widget == pViewerImpl->m_pVideoFixed
		|| widget == pViewerImpl->m_pVideoSinkWidget) 
	{
		if (3 == button)
		{
			viewer_show_context_menu(widget, x, y, 0, user_data);
			return;
		}
		else if ((widget == pViewerImpl->m_pVideoFixed || widget == pViewerImpl->m_pVideoSinkWidget)
			&& 1 == button)
		{
			if (pViewerImpl->IsVideo())
			{
				pViewerImpl->m_bVideoPanning = TRUE;
				pViewerImpl->m_dVideoPanStartRootX = x;
				pViewerImpl->m_dVideoPanStartRootY = y;
				pViewerImpl->m_dVideoPanStartPX = pViewerImpl->m_dVideoPanX;
				pViewerImpl->m_dVideoPanStartPY = pViewerImpl->m_dVideoPanY;
				set_control_visible(pViewerImpl->m_pMediaControls, true);
				pViewerImpl->RefreshAutoHideTimer();
				return;
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

static void viewer_show_context_menu(GtkWidget *widget, gdouble x_root, gdouble y_root, guint32 /*time*/, gpointer userdata)
{
	(void)userdata;

	Viewer::ViewerImpl *pViewerImpl = (Viewer::ViewerImpl*)userdata;

	if (NULL == pViewerImpl->m_pContextMenuPopover)
	{
		GtkWidget *popover = gtk_popover_new();
		GtkWidget *menu_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

		gtk_widget_insert_action_group(popover, "quiver", G_ACTION_GROUP(QuiverUtils::GetActionGroup()));

		GtkWidget *item = gtk_button_new_with_label("Copy");
		gtk_actionable_set_action_name(GTK_ACTIONABLE(item), "quiver." ACTION_VIEWER_COPY);
		gtk_box_append(GTK_BOX(menu_box), item);

		GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
		gtk_box_append(GTK_BOX(menu_box), sep);

		item = gtk_button_new_with_label("Rotate Counterclockwise");
		gtk_actionable_set_action_name(GTK_ACTIONABLE(item), "quiver." ACTION_VIEWER_ROTATE_CCW);
		gtk_box_append(GTK_BOX(menu_box), item);

		item = gtk_button_new_with_label("Rotate Clockwise");
		gtk_actionable_set_action_name(GTK_ACTIONABLE(item), "quiver." ACTION_VIEWER_ROTATE_CW);
		gtk_box_append(GTK_BOX(menu_box), item);

		sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
		gtk_box_append(GTK_BOX(menu_box), sep);

		item = gtk_button_new_with_label("Move To Trash");
		gtk_actionable_set_action_name(GTK_ACTIONABLE(item), "quiver." ACTION_VIEWER_TRASH);
		gtk_box_append(GTK_BOX(menu_box), item);

		gtk_popover_set_child(GTK_POPOVER(popover), menu_box);
		pViewerImpl->m_pContextMenuPopover = popover;
		gtk_widget_set_parent(popover, widget);
	}

	if (x_root >= 0 && y_root >= 0)
	{
		GdkRectangle rect = { (gint)x_root, (gint)y_root, 1, 1 };
		gtk_popover_set_pointing_to(GTK_POPOVER(pViewerImpl->m_pContextMenuPopover), &rect);
	}
	gtk_popover_popup(GTK_POPOVER(pViewerImpl->m_pContextMenuPopover));
}




Viewer::ViewerImpl::~ViewerImpl()
{
	StopVideo(false);


	gst_object_unref(GST_OBJECT(m_pPipeline));

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
	if (0 != m_iVideoZoomTimeoutID)
	{
		g_source_remove(m_iVideoZoomTimeoutID);
		m_iVideoZoomTimeoutID = 0;
	}

	CancelFilmstripHide();

	m_ImageLoader.RemovePixbufLoaderObserver(m_StatusbarPtr.get());
	m_ImageLoader.RemovePixbufLoaderObserver(m_PixbufLoaderObserverPtr.get());

	/* popovers are parented to viewer widgets (buttons / icon view); unparent
	 * them NOW while their parents are still alive, or they would be left with
	 * a dangling parent pointer when the widget tree below is destroyed */
	if (m_pVolumePopover)
	{
		if (gtk_widget_get_parent(m_pVolumePopover))
			gtk_widget_unparent(m_pVolumePopover);
		m_pVolumePopover = NULL;
	}
	if (m_pVideoOptionsPopover)
	{
		if (gtk_widget_get_parent(m_pVideoOptionsPopover))
			gtk_widget_unparent(m_pVideoOptionsPopover);
		m_pVideoOptionsPopover = NULL;
	}
	if (m_pSpeedButton)
	{
		GtkWidget *speedPop = (GtkWidget*)g_object_get_data(G_OBJECT(m_pSpeedButton), "speed-popover");
		if (speedPop)
		{
			if (gtk_widget_get_parent(speedPop))
				gtk_widget_unparent(speedPop);
			g_object_set_data(G_OBJECT(m_pSpeedButton), "speed-popover", NULL);
		}
	}
	if (m_pContextMenuPopover)
	{
		if (gtk_widget_get_parent(m_pContextMenuPopover))
			gtk_widget_unparent(m_pContextMenuPopover);
		m_pContextMenuPopover = NULL;
	}

	/* m_pHBox is parented into the window tree and owned by it; the window
	 * destroy at the end of ~QuiverImpl runs AFTER ViewerImpl has been freed.
	 * Unmap the whole viewer subtree HERE, while `this` is still alive, so no
	 * widget can fire a leave/unmap/gesture callback into this freed object
	 * during the window teardown (same pattern as ~BrowserImpl). */
	if (m_pHBox && gtk_widget_get_parent(m_pHBox))
	{
		gtk_widget_unparent(m_pHBox);
	}
	m_pHBox = NULL;

	PreferencesPtr prefsPtr = Preferences::GetInstance();
	prefsPtr->RemoveEventHandler( m_PreferencesEventHandlerPtr );
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
	 * window resizes). */
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl*)g_object_get_data(
		G_OBJECT(widget), "quiver-viewer-impl");
	if (p != NULL && p->m_iVideoWidth > 0 && p->m_iVideoHeight > 0)
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
	QuiverFreelayout *self = QUIVER_FREELAYOUT(widget);
	for (guint i = 0; i < self->children->len; i++)
	{
		QuiverFreelayoutChild *c = &g_array_index(self->children, QuiverFreelayoutChild, i);
		if (c->child != NULL)
			gtk_widget_snapshot_child(widget, c->child, snapshot);
	}
}

static void
quiver_freelayout_init(QuiverFreelayout *self)
{
	self->children = g_array_new(FALSE, FALSE, sizeof(QuiverFreelayoutChild));
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
quiver_freelayout_class_init(QuiverFreelayoutClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
	gobject_class->dispose = quiver_freelayout_dispose;

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

static GstPadProbeReturn video_crop_pad_probe(GstPad *pad, GstPadProbeInfo *info, gpointer user_data)
{ (void)pad; 
	Viewer::ViewerImpl *pViewerImpl = (Viewer::ViewerImpl*)user_data;

	if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM)
	{
		GstEvent *event = GST_PAD_PROBE_INFO_EVENT(info);
		if (GST_EVENT_TYPE(event) == GST_EVENT_CAPS)
		{
			GstCaps *caps = NULL;
			gst_event_parse_caps(event, &caps);
			if (caps != NULL)
			{
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
	return GST_PAD_PROBE_OK;
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
	if (p->m_bVideoNeedsFirstFrame
		&& !p->m_bVideoFlushPending
		&& p->m_pVideoFixed != NULL && gtk_widget_get_visible(p->m_pVideoFixed))
	{
		p->m_bVideoNeedsFirstFrame = FALSE;
		gtk_widget_set_opacity(p->m_pVideoSinkWidget, 1.0);
	}
}

static GstPadProbeReturn video_zoom_reconfigure_probe(GstPad *pad, GstPadProbeInfo *info, gpointer user_data)
{ (void)pad; (void)user_data;

	/* gtk4paintablesink pushes an upstream reconfigure event whenever its widget is
	 * resized, and the crop/caps changes from zooming do too.  If that event
	 * reaches decodebin, the hardware decoder must renegotiate its output
	 * mid-stream, which fails and aborts playback with "Internal data stream
	 * error" (qtdemux not-negotiated).  The zoom bin is self-contained: its
	 * first element accepts whatever caps the decoder provides, so swallow
	 * the reconfigure here instead of letting it disturb the decode chain. */
	if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_EVENT_UPSTREAM)
	{
		GstEvent *event = GST_PAD_PROBE_INFO_EVENT(info);
		if (GST_EVENT_TYPE(event) == GST_EVENT_RECONFIGURE)
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
void Viewer::ViewerImpl::SetVideoZoom(gdouble zoom)
{
	/* zooming with +/- or the wheel leaves the view modes and pins the
	 * factor: the zoom is relative to the actual size (1.0 = 100%), clamped
	 * from the fit level (the smallest scale the video has been seen at) up
	 * to 8x of the actual size, so the same image-like zoom is available no
	 * matter how small the window is */
	quiver_image_view_set_view_mode(QUIVER_IMAGE_VIEW(m_pImageView),
		QUIVER_IMAGE_VIEW_MODE_ZOOM);
	if (zoom < m_dVideoZoomMin)
		zoom = m_dVideoZoomMin;
	else if (zoom > 16.0)
		zoom = 16.0;
	QuiverUtils::SetRadioActionCurrent(ACTION_VIEWER_ZOOM, QUIVER_IMAGE_VIEW_MODE_ZOOM);

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
#endif
	UpdateUI();
}

static GstCaps* video_zoom_scaled_caps(GstElement *scaler, gint srcW, gint srcH)
{
	/* build the caps the scaler must emit: source frame size, fixated from the
	 * element's own src template so it matches the output format the scaler
	 * actually produces (e.g. NVMM on NVIDIA, system memory elsewhere) */
	GstCaps *caps = NULL;
	GstPad *srcpad = gst_element_get_static_pad(scaler, "src");
	if (srcpad != NULL)
	{
		caps = gst_pad_get_pad_template_caps(srcpad);
		gst_object_unref(srcpad);
	}
	if (caps == NULL || gst_caps_is_any(caps) || gst_caps_is_empty(caps))
	{
		if (caps != NULL)
			gst_caps_unref(caps);
		caps = gst_caps_new_simple("video/x-raw",
			"width", G_TYPE_INT, srcW,
			"height", G_TYPE_INT, srcH,
			NULL);
		return caps;
	}
	gst_caps_set_simple(caps, "width", G_TYPE_INT, srcW, "height", G_TYPE_INT, srcH, NULL);
	GstCaps *fixed = gst_caps_fixate(caps);
	gst_caps_unref(caps);
	return fixed;
}

static void video_zoom_get_pointer(Viewer::ViewerImpl *p, gdouble *px, gdouble *py)
{
	/* pointer position in viewer-area coords, or (-1,-1) when it is outside
	 * the area / the area is not realized yet (callers zoom about the center) */
	*px = -1.;
	*py = -1.;
	GtkWidget *area = p->m_pVideoFixed;
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
	GdkDevice *device = gdk_seat_get_pointer(
		gdk_display_get_default_seat(gdk_surface_get_display(surface)));
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
		p->ApplyVideoZoom();
		return FALSE;
	}

	p->m_dVideoZoom = old_zoom + mag_diff / 2.;
	p->ApplyVideoZoom();
	return TRUE;
}
#endif

void Viewer::ViewerImpl::ApplyVideoZoom()
{
	if (NULL == m_pVideoFixed || NULL == m_pVideoSinkWidget)
		return;

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
		return;
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
	QuiverImageViewMode videoViewMode =
		quiver_image_view_get_view_mode(QUIVER_IMAGE_VIEW(m_pImageView));
	switch (videoViewMode)
	{
		case QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW:
			zoom = fitZoom;
			m_dVideoZoomMin = fitZoom;
			break;
		case QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW_STRETCH:
			zoom = fitScale;
			m_dVideoZoomMin = fitScale;
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
			/* when zooming out lands back at the fit level, snap back to
			 * FIT_WINDOW so a window resize will re-fit the video instead
			 * of keeping it pinned at the old fit size */
			if (zoom <= m_dVideoZoomMin)
			{
				quiver_image_view_set_view_mode(QUIVER_IMAGE_VIEW(m_pImageView),
					QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW);
				QuiverUtils::SetRadioActionCurrent(ACTION_VIEWER_ZOOM,
					QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW);
				videoViewMode = QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW;
				zoom = fitZoom;
				m_dVideoZoomMin = fitZoom;
			}
			break;
	}
	m_dVideoZoom = zoom;

	/* the video display grows with the zoom until it covers the viewer area,
	 * like an image; zooming beyond that is covered by the crop + upscale
	 * below.  The widget is also capped so the sink's GL surface stays
	 * within limits on huge windows; past the cap the crop covers the extra
	 * zoom. */
	gdouble effZoom = MIN(zoom, MIN(fillScale, 4096. / MAX(dispW, dispH)));
	gdouble widgetW = dispW * effZoom;
	gdouble widgetH = dispH * effZoom;

	/* the part of the zoom the display cannot cover is a crop + upscale in
	 * the pipeline (crop the source region and scale it back to full frame) */
	gdouble zc = zoom / effZoom;
	if (zc < 1.0)
		zc = 1.0;
	gdouble cropW = srcW / zc;
	gdouble cropH = srcH / zc;

	gboolean cropping = (zc > 1.01);

	/* the visible region in source pixels: what the window actually shows of
	 * the (up-scaled) crop.  Computed before the pan so the centering below
	 * aligns the viewport — not the larger crop — with the window center. */
	gdouble vw = areaW * cropW / widgetW;
	gdouble vh = areaH * cropH / widgetH;

	/* keep the source pixel under the pointer fixed while the zoom changes,
	 * so the video zooms in on the cursor; when the pointer is not over the
	 * area (e.g. a toolbar zoom button) zoom about the center instead of the
	 * crop's corner (skipped while dragging so the user's pan is not overridden,
	 * and skipped for menu/toolbar zooms which should center) */
	if (!m_bVideoPanning && m_dVideoLastWidgetW > 0.)
	{
		gdouble px = -1., py = -1.;
		video_zoom_get_pointer(this, &px, &py);
		if (px < 0. || py < 0.)
		{
			px = areaW / 2.;
			py = areaH / 2.;
		}
		/* 1. Find the source pixel under the pointer from the PREVIOUS frame's layout */
		gdouble srcX, srcY;
		if (m_dVideoLastWidgetW > areaW)
			srcX = m_dVideoPanX + px * (srcW / m_dVideoLastZc) / m_dVideoLastWidgetW;
		else
			srcX = m_dVideoPanX + (px - (areaW - m_dVideoLastWidgetW) / 2.) * (srcW / m_dVideoLastZc) / m_dVideoLastWidgetW;

		if (m_dVideoLastWidgetH > areaH)
			srcY = m_dVideoPanY + py * (srcH / m_dVideoLastZc) / m_dVideoLastWidgetH;
		else
			srcY = m_dVideoPanY + (py - (areaH - m_dVideoLastWidgetH) / 2.) * (srcH / m_dVideoLastZc) / m_dVideoLastWidgetH;

		/* 2. Compute the new pan so that the same source pixel stays under the pointer in the NEW layout */
		if (widgetW > areaW)
			m_dVideoPanX = srcX - px * (srcW / zc) / widgetW;
		else
			m_dVideoPanX = srcX - (px - (areaW - widgetW) / 2.) * (srcW / zc) / widgetW;

		if (widgetH > areaH)
			m_dVideoPanY = srcY - py * (srcH / zc) / widgetH;
		else
			m_dVideoPanY = srcY - (py - (areaH - widgetH) / 2.) * (srcH / zc) / widgetH;

	}
	else if (!m_bVideoPanning && m_dVideoLastWidgetW == 0.)
	{
		/* first ApplyVideoZoom after a reset (StopVideo / init): center the
		 * visible viewport in the source, matching the image view's
		 * set_default_adjustment_values centering */
		m_dVideoPanX = MAX(0., (srcW - vw) / 2.);
		m_dVideoPanY = MAX(0., (srcH - vh) / 2.);
	}

	/* The window shows a viewport of the (up-scaled) crop, and the pan is the
	 * viewport's left/top in source pixels.  When the widget overflows the
	 * window, it is slid so the viewport can reach the very edges of the frame
	 * (otherwise the clip keeps the corners - e.g. a security camera's OSD
	 * timestamp - out of reach); when it fits, the whole crop is visible and
	 * panning moves the crop itself. */
	gdouble cropLeftD = 0., cropTopD = 0., offX, offY;

	if (widgetW > areaW)
	{
		m_dVideoPanX = CLAMP(m_dVideoPanX, 0., srcW - vw);
		gdouble d = cropW - vw;   /* crop margin the window cannot show */
		cropLeftD = CLAMP(m_dVideoPanX - d / 2., 0., srcW - cropW);
		offX = (cropLeftD - m_dVideoPanX) * widgetW / cropW;
	}
	else
	{
		m_dVideoPanX = CLAMP(m_dVideoPanX, 0., srcW - cropW);
		cropLeftD = m_dVideoPanX;
		offX = (areaW - widgetW) / 2.;
	}
	if (widgetH > areaH)
	{
		m_dVideoPanY = CLAMP(m_dVideoPanY, 0., srcH - vh);
		gdouble d = cropH - vh;
		cropTopD = CLAMP(m_dVideoPanY - d / 2., 0., srcH - cropH);
		offY = (cropTopD - m_dVideoPanY) * widgetH / cropH;
	}
	else
	{
		m_dVideoPanY = CLAMP(m_dVideoPanY, 0., srcH - cropH);
		cropTopD = m_dVideoPanY;
		offY = (areaH - widgetH) / 2.;
	}

	gint cropLeft = 0, cropRight = 0, cropTop = 0, cropBottom = 0;
	if (cropping)
	{
		gint cropWidth = MAX(1, (gint)(cropW + 0.5));
		gint cropHeight = MAX(1, (gint)(cropH + 0.5));
		cropLeft = (gint)(cropLeftD + 0.5);
		cropTop = (gint)(cropTopD + 0.5);
		cropRight = (gint)srcW - cropWidth - cropLeft;
		cropBottom = (gint)srcH - cropHeight - cropTop;
	}

	switch (m_VideoZoomType)
	{
		case VIDEO_ZOOM_SOFTWARE:
		case VIDEO_ZOOM_MEDIA_SDK:
			if (m_pVideoCrop != NULL)
			{
				g_object_set(G_OBJECT(m_pVideoCrop),
					"left", cropLeft, "right", cropRight,
					"top", cropTop, "bottom", cropBottom,
					NULL);
			}
			break;
		case VIDEO_ZOOM_VAAPI:
			g_object_set(G_OBJECT(m_pVideoZoomScaler),
				"crop-left", cropLeft, "crop-right", cropRight,
				"crop-top", cropTop, "crop-bottom", cropBottom,
				NULL);
			break;
		case VIDEO_ZOOM_NVIDIA:
			g_object_set(G_OBJECT(m_pVideoZoomScaler),
				"left", cropLeft,
				"right", (gint)srcW - cropRight - 1,
				"top", cropTop,
				"bottom", (gint)srcH - cropBottom - 1,
				NULL);
			break;
		case VIDEO_ZOOM_GL:
			if (m_pVideoZoomScaler != NULL)
			{
				gdouble sx = srcW / (gdouble)(srcW - cropLeft - cropRight);
				gdouble sy = srcH / (gdouble)(srcH - cropTop - cropBottom);
				/* gltransformation's prepare_output_buffer composes:
				 *   result = yflip * mvp * inv_aspect
				 * which reduces to: x_out = x*sx + tx*2, y_out = y*sy - ty*2
				 * on the meta's [0,1]→NDC vertices.
				 * Solve for tx/ty so cropLeft→x_out=-1, cropTop→y_out=+1. */
				gdouble tx = (sx - 1.0) / 2.0 - cropLeft * sx / srcW;
				gdouble ty = (sy - 1.0) / 2.0 - cropTop * sy / srcH;

				g_object_set(G_OBJECT(m_pVideoZoomScaler),
					"scale-x", (gfloat)sx,
					"scale-y", (gfloat)sy,
					"translation-x", (gfloat)tx,
					"translation-y", (gfloat)ty,
					NULL);
			}
			break;
	}

	/* force the scaler to upscale the cropped region back to the full frame
	 * only while the crop is actually engaged, so the passthrough state
	 * keeps negotiating directly with the GL sink (the known-good reference) */
	if (m_pVideoZoomCaps != NULL)
	{
		if (m_VideoZoomType == VIDEO_ZOOM_SOFTWARE)
		{
			/* software scaler: force the output to system-memory full-frame
			 * video permanently instead of flipping the caps with the crop.
			 * glupload's zero-copy DMABuf EGLImage import path cannot be
			 * re-negotiated when the videoscale output changes size/format
			 * mid-playback ("Failed to upload buffer"), while the CPU upload
			 * path it selects for system memory is stable.  The caps are set
			 * once, so nothing downstream re-negotiates on later zooms. */
			if (!m_bVideoZoomCropActive)
			{
				GstCaps *caps = gst_caps_new_simple("video/x-raw",
					"width", G_TYPE_INT, (gint)srcW,
					"height", G_TYPE_INT, (gint)srcH,
					NULL);
				gst_caps_set_features(caps, 0,
					gst_caps_features_from_string("memory:SystemMemory"));
				g_object_set(G_OBJECT(m_pVideoZoomCaps), "caps", caps, NULL);
				gst_caps_unref(caps);
				m_bVideoZoomCropActive = TRUE;
			}
		}
		else if (m_VideoZoomType == VIDEO_ZOOM_MEDIA_SDK)
		{
			/* the va scaler (vapostproc) can only emit VAMemory/DMABuf, and
			 * glupload's zero-copy DMABuf EGLImage import cannot be
			 * re-negotiated mid-playback: glupload responds to a caps change
			 * by sending a caps event upstream, which makes decodebin
			 * re-negotiate the hardware decoder and aborts playback with
			 * qtdemux "not-negotiated" or glupload "Failed to upload buffer".
			 * Force the scaler to always emit full-frame DMABuf instead of
			 * flipping the caps with the crop: the output caps never change
			 * while zooming, so glupload negotiates once at startup, and a
			 * crop change only touches the videocrop -> scaler link inside
			 * the bin.  The videocrop itself only crops system memory, so
			 * the first time the crop actually engages the zoominput caps
			 * also force a VAMemory -> system-memory conversion; the reconfigure
			 * probe on the bin's input swallows the re-negotiation so the
			 * hardware decoder is never disturbed.  Until then the chain stays
			 * in the fast VAMemory passthrough (no full-frame copy). */
			if (!m_bVideoZoomCropActive)
			{
				GstCaps *caps = gst_caps_new_simple("video/x-raw",
					"width", G_TYPE_INT, (gint)srcW,
					"height", G_TYPE_INT, (gint)srcH,
					NULL);
				gst_caps_set_features(caps, 0,
					gst_caps_features_from_string("memory:DMABuf"));
				g_object_set(G_OBJECT(m_pVideoZoomCaps), "caps", caps, NULL);
				gst_caps_unref(caps);
				m_bVideoZoomCropActive = TRUE;
			}
			if (cropping && m_pVideoZoomInputCaps != NULL &&
				!m_bVideoZoomInputCropActive)
			{
				GstCaps *in_caps = gst_caps_new_empty_simple("video/x-raw");
				gst_caps_set_features(in_caps, 0,
					gst_caps_features_from_string("memory:SystemMemory"));
				g_object_set(G_OBJECT(m_pVideoZoomInputCaps), "caps", in_caps, NULL);
				gst_caps_unref(in_caps);
				m_bVideoZoomInputCropActive = TRUE;
			}
		}
		else if (cropping && !m_bVideoZoomCropActive)
		{
			GstCaps *caps;
			if (m_VideoZoomType == VIDEO_ZOOM_NVIDIA)
				caps = video_zoom_scaled_caps(m_pVideoZoomScaler, (gint)srcW, (gint)srcH);
			else
				caps = gst_caps_new_simple("video/x-raw",
					"width", G_TYPE_INT, (gint)srcW,
					"height", G_TYPE_INT, (gint)srcH,
					NULL);
			g_object_set(G_OBJECT(m_pVideoZoomCaps), "caps", caps, NULL);
			gst_caps_unref(caps);
			m_bVideoZoomCropActive = TRUE;
		}
		else if (!cropping && m_bVideoZoomCropActive)
		{
			g_object_set(G_OBJECT(m_pVideoZoomCaps), "caps", NULL, NULL);
			m_bVideoZoomCropActive = FALSE;
		}
	}

	/* size the video widget to the display and move it: centered when it fits,
	 * slid against the viewport pan when it overflows (offX/offY are negative
	 * then, and the fixed clips the part outside the window) */
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
	if (gtk_widget_get_visible(m_pVideoSinkWidget))
		m_StatusbarPtr->SetMagnification((int)(m_dVideoZoom * 100. + 0.5));

	g_printerr("[QLAYOUT applyzoom] mode=%d zoom=%.3f effZoom=%.3f widget=%dx%d area=%dx%d -> sink %dx%d@%d,%d\n",
		(int)videoViewMode, m_dVideoZoom, effZoom,
		(gint)(widgetW+0.5), (gint)(widgetH+0.5), areaW, areaH,
		newW, newH, newX, newY);

	{
		GtkRoot *root = gtk_widget_get_root(m_pVideoFixed);
		g_printerr("[QLAYOUT sizes] window=%dx%d fixed=%dx%d stack=%dx%d sinkreq=%dx%d\n",
			root ? gtk_widget_get_width(GTK_WIDGET(root)) : -1,
			root ? gtk_widget_get_height(GTK_WIDGET(root)) : -1,
			gtk_widget_get_width(m_pVideoFixed), gtk_widget_get_height(m_pVideoFixed),
			m_pStack ? gtk_widget_get_width(m_pStack) : -1, m_pStack ? gtk_widget_get_height(m_pStack) : -1,
			newW, newH);
	}

	m_dVideoLastWidgetW = widgetW;
	m_dVideoLastWidgetH = widgetH;
	m_dVideoLastZc = zc;

	/* When paused, no buffers flow through gltransformation so property
	 * changes are invisible until the next seek or state change.  Push the
	 * new transform values through by re-seeking to the current position.
	 * A flushing seek is safe in PAUSED and triggers exactly one buffer
	 * through the pipeline. */
	if (m_pPipeline != NULL)
	{
		GstState current = GST_STATE_VOID_PENDING;
		gst_element_get_state(GST_ELEMENT(m_pPipeline), &current, NULL, 0);
		if (current == GST_STATE_PAUSED)
		{
			gint64 pos = 0;
			if (gst_element_query_position(m_pPipeline, GST_FORMAT_TIME, &pos))
			{
				gst_element_seek(GST_ELEMENT(m_pPipeline), 1.0,
					GST_FORMAT_TIME,
					GstSeekFlags(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE),
					GST_SEEK_TYPE_SET, pos, GST_SEEK_TYPE_NONE, 0);
			}
		}
	}
}

static void viewer_speed_button_clicked_cb(GtkButton *button, gpointer user_data)
{
	(void)button;
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl *)user_data;
	int val = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "speed-value"));
	gdouble speed = val / 1000.0;
	
	int idx = -1;
	if (speed == 0.25) idx = 0;
	else if (speed == 0.5) idx = 1;
	else if (speed == 1.0) idx = 2;
	else if (speed == 1.5) idx = 3;
	else if (speed == 2.0) idx = 4;
	else if (speed == 4.0) idx = 5;
	else if (speed == 8.0) idx = 6;
	else if (speed == 16.0) idx = 7;

	if (idx >= 0)
	{
		const gchar* speed_names[] = {
			"VideoSpeed025", "VideoSpeed05", "VideoSpeed10", "VideoSpeed15",
			"VideoSpeed20", "VideoSpeed40", "VideoSpeed80", "VideoSpeed160"
		};
		const gchar *action_name = speed_names[idx];
		
		GAction *action = QuiverUtils::GetAction(action_name);
		if (action)
		{
			g_action_activate(action, NULL); // radio actions are activated with NULL to just select them
		}
	}
	else
	{
		p->SetPlaybackSpeed(speed);
	}

	/* close the popover */
	GtkWidget *speedBtn = p->m_pSpeedButton;
	if (speedBtn != NULL)
	{
		GtkPopover *popover = (GtkPopover *)g_object_get_data(G_OBJECT(speedBtn), "speed-popover");
		if (popover != NULL)
			gtk_widget_set_visible(GTK_WIDGET(popover), FALSE);
	}
}

static void viewer_speed_toggle_popover_cb(gpointer user_data, GtkWidget *widget)
{
	(void)widget;
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl *)user_data;
	GtkWidget *speedBtn = p->m_pSpeedButton;
	if (speedBtn == NULL) return;
	GtkPopover *popover = (GtkPopover *)g_object_get_data(G_OBJECT(speedBtn), "speed-popover");
	if (popover == NULL) return;
	if (gtk_widget_get_visible(GTK_WIDGET(popover)))
	{
		gtk_widget_set_visible(GTK_WIDGET(popover), FALSE);
	}
		else
		{
			/* highlight current speed */
			GtkWidget *box = (GtkWidget *)g_object_get_data(G_OBJECT(popover), "speed-box");
			if (box != NULL)
			{
				gdouble current = p->m_dPlaybackSpeed;
				for (GtkWidget *child = gtk_widget_get_first_child(box);
					 child != NULL;
					 child = gtk_widget_get_next_sibling(child))
				{
					int val2 = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(child), "speed-value"));
					gdouble spd = val2 / 1000.0;
					if (spd == current)
						gtk_widget_add_css_class(child, "speed-active");
					else
						gtk_widget_remove_css_class(child, "speed-active");
				}
			}
			gtk_popover_popup(popover);
		}
	}

static void viewer_fullscreen_button_clicked_cb(GtkButton *button, gpointer user_data)
{
	(void)button;
	(void)user_data;
	GAction *fs = QuiverUtils::GetAction("FullScreen");
	if (fs != NULL)
	{
		g_action_activate(fs, NULL);
	}
}

static void viewer_snapshot_button_clicked_cb(GtkButton *button, gpointer user_data)
{
	(void)button;
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl *)user_data;
	p->Snapshot();
}

static void viewer_skip_back_cb(gpointer user_data)
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

static void viewer_skip_fwd_cb(gpointer user_data)
{
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl *)user_data;
	p->SkipForward();
}

static void viewer_frame_step_back_cb(gpointer user_data)
{
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl *)user_data;
	if (!p->IsVideo() || p->m_pPipeline == NULL) return;
	p->SetIsPlaying(false);
	gst_element_set_state(GST_ELEMENT(p->m_pPipeline), GST_STATE_PAUSED);
	GstEvent *ev = gst_event_new_step(GST_FORMAT_BUFFERS, 1, -1.0, TRUE, FALSE);
	gst_element_send_event(GST_ELEMENT(p->m_pPipeline), ev);
	p->RefreshAutoHideTimer();
}

static void viewer_frame_step_fwd_cb(gpointer user_data)
{
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl *)user_data;
	if (!p->IsVideo() || p->m_pPipeline == NULL) return;
	p->SetIsPlaying(false);
	gst_element_set_state(GST_ELEMENT(p->m_pPipeline), GST_STATE_PAUSED);
	GstEvent *ev = gst_event_new_step(GST_FORMAT_BUFFERS, 1, 1.0, TRUE, FALSE);
	gst_element_send_event(GST_ELEMENT(p->m_pPipeline), ev);
	p->RefreshAutoHideTimer();
}

Viewer::ViewerImpl::ViewerImpl(Viewer *pViewer) : 
	
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
	m_dVolume(1.0),
	m_pFullscreenBtn(NULL),
	m_pVideoOptionsBtn(NULL),
	m_pVideoOptionsPopover(NULL),
	m_iVideoZoomIdle(0),
	m_ImageListPtr(new ImageList()),
	m_bIsPlaying(false),
	m_ThumbnailCache(100),
	m_dPlaybackSpeed(1.0),
	m_pSpeedButton(NULL),
	m_pSpeedLabel(NULL),
	m_pContextMenuPopover(NULL),
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
	m_PreferencesEventHandlerPtr ( new PreferencesEventHandler(this) ),
	m_ImageListEventHandlerPtr( new ImageListEventHandler(this) ),
	m_ThumbnailLoader(this,2)
{
	PreferencesPtr prefsPtr = Preferences::GetInstance();
	prefsPtr->AddEventHandler( m_PreferencesEventHandlerPtr );
	
	m_pViewer = pViewer;

	m_pIconView = quiver_icon_view_new();
 	m_pImageView = quiver_image_view_new();
	quiver_image_view_set_enable_transitions(QUIVER_IMAGE_VIEW(m_pImageView), FALSE);

	gtk_widget_set_size_request(m_pImageView, 100, 100);


	// FIXME:
	//GdkPixmap* bitmap = gdk_pixmap_new(NULL, w, h, 1);
	//gdk_pixbuf_render_threshold_alpha(m_pPixbufPlay, bitmap, 0,0,0,0,w,h, 0x80);

	GtkWidget* alignment = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_halign(alignment, GTK_ALIGN_FILL);
	gtk_widget_set_valign(alignment, GTK_ALIGN_END);
	gtk_widget_set_margin_top(alignment, 0);
	gtk_widget_set_margin_bottom(alignment, 0);
	gtk_widget_set_margin_start(alignment, 0);
	gtk_widget_set_margin_end(alignment, 0);
	gtk_box_set_spacing(GTK_BOX(alignment), 0);

	/* ── Row 1: Transport controls (overlay layout) ─────────────── */
	/* Main content: two equal expanding spacers center [rewind][play][ff].
	 * The spacers have natural size 0 and hexpand=TRUE, so they each get
	 * exactly half the remaining space — guaranteeing true centering. */

	/* Speed button — bold label, manual popover */
	m_dPlaybackSpeed = 1.0;
	GtkWidget* speedPopover = gtk_popover_new();
	gtk_popover_set_autohide(GTK_POPOVER(speedPopover), TRUE);
	gtk_widget_set_size_request(speedPopover, 100, -1);
	GtkWidget* speedBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
	gtk_widget_set_margin_start(speedBox, 3);
	gtk_widget_set_margin_end(speedBox, 3);
	gtk_widget_set_margin_top(speedBox, 3);
	gtk_widget_set_margin_bottom(speedBox, 3);
	g_object_set_data(G_OBJECT(speedPopover), "speed-box", speedBox);

	const double speeds[] = { 0.25, 0.5, 1.0, 1.5, 2.0, 4.0, 8.0, 16.0 };
	const int nSpeeds = sizeof(speeds) / sizeof(speeds[0]);
	for (int i = 0; i < nSpeeds; i++)
	{
		GtkWidget* btn = gtk_button_new();
		gchar* label = g_strdup_printf("<b>%.4g</b>", speeds[i]);
		GtkWidget* lbl = gtk_label_new(NULL);
		gtk_label_set_use_markup(GTK_LABEL(lbl), TRUE);
		gtk_label_set_markup(GTK_LABEL(lbl), label);
		gtk_button_set_child(GTK_BUTTON(btn), lbl);
		g_free(label);
		g_object_set_data(G_OBJECT(btn), "speed-value", GINT_TO_POINTER((int)(speeds[i] * 1000)));
		g_signal_connect(G_OBJECT(btn), "clicked", G_CALLBACK(viewer_speed_button_clicked_cb), this);
		gtk_box_append(GTK_BOX(speedBox), btn);
	}

	gtk_popover_set_child(GTK_POPOVER(speedPopover), speedBox);

	m_pSpeedButton = gtk_button_new();
	gtk_button_set_has_frame(GTK_BUTTON(m_pSpeedButton), FALSE);
	m_pSpeedLabel = gtk_label_new(NULL);
	gtk_label_set_use_markup(GTK_LABEL(m_pSpeedLabel), TRUE);
	gtk_label_set_markup(GTK_LABEL(m_pSpeedLabel), "<b>1x</b>");
	gtk_button_set_child(GTK_BUTTON(m_pSpeedButton), m_pSpeedLabel);
	g_object_set_data(G_OBJECT(m_pSpeedButton), "speed-popover", speedPopover);
	/* Anchor the popover to the speed button (GTK4 takes ownership by parenting) */
	gtk_widget_set_parent(speedPopover, m_pSpeedButton);
	gtk_popover_set_position(GTK_POPOVER(speedPopover), GTK_POS_TOP);
	g_signal_connect_swapped(G_OBJECT(m_pSpeedButton), "clicked", G_CALLBACK(viewer_speed_toggle_popover_cb), this);
	gtk_widget_add_css_class(m_pSpeedButton, "media-btn");

	/* Snapshot button */
	m_pSnapBtn = gtk_button_new_from_icon_name("camera-photo");
	gtk_button_set_has_frame(GTK_BUTTON(m_pSnapBtn), FALSE);
	g_signal_connect(G_OBJECT(m_pSnapBtn), "clicked", G_CALLBACK(viewer_snapshot_button_clicked_cb), this);
	gtk_widget_add_css_class(m_pSnapBtn, "media-btn");

	/* Volume button */
	m_pVolumeButton = gtk_toggle_button_new();
	gtk_button_set_has_frame(GTK_BUTTON(m_pVolumeButton), FALSE);
	GtkWidget* volIcon = gtk_image_new_from_icon_name("audio-volume-high");
	gtk_button_set_child(GTK_BUTTON(m_pVolumeButton), volIcon);
	gtk_widget_add_css_class(m_pVolumeButton, "media-btn");

	m_pVolumePopover = gtk_popover_new();
	gtk_popover_set_autohide(GTK_POPOVER(m_pVolumePopover), TRUE);
	GtkWidget* volBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	gtk_widget_set_margin_start(volBox, 8);
	gtk_widget_set_margin_end(volBox, 8);
	gtk_widget_set_margin_top(volBox, 8);
	gtk_widget_set_margin_bottom(volBox, 8);
	GtkWidget* volLabel = gtk_label_new("Volume");
	gtk_box_append(GTK_BOX(volBox), volLabel);
	m_pVolumeScale = gtk_scale_new_with_range(GTK_ORIENTATION_VERTICAL, 0.0, 1.0, 0.05);
	gtk_widget_set_size_request(m_pVolumeScale, -1, 120);
	gtk_range_set_value(GTK_RANGE(m_pVolumeScale), m_dVolume);
	g_signal_connect(G_OBJECT(m_pVolumeScale), "value-changed", G_CALLBACK(viewer_volume_value_changed), this);
	gtk_box_append(GTK_BOX(volBox), m_pVolumeScale);
	gtk_popover_set_child(GTK_POPOVER(m_pVolumePopover), volBox);
	/* Anchor the popover to the volume button (GTK4 takes ownership via
	 * parenting) */
	gtk_widget_set_parent(m_pVolumePopover, m_pVolumeButton);
	gtk_popover_set_position(GTK_POPOVER(m_pVolumePopover), GTK_POS_TOP);
	g_signal_connect_swapped(G_OBJECT(m_pVolumeButton), "toggled", G_CALLBACK(viewer_volume_toggle_popover_cb), this);
	/* when the popover auto-hides, reset the toggle button's pressed state */
	g_signal_connect(m_pVolumePopover, "closed", G_CALLBACK(viewer_volume_popover_closed_cb), this);

		/* Video Options button */
	m_pVideoOptionsBtn = gtk_button_new_from_icon_name("emblem-system-symbolic");
	gtk_button_set_has_frame(GTK_BUTTON(m_pVideoOptionsBtn), FALSE);
	g_signal_connect(G_OBJECT(m_pVideoOptionsBtn), "clicked", G_CALLBACK(viewer_video_options_btn_clicked_cb), this);
	gtk_widget_add_css_class(m_pVideoOptionsBtn, "media-btn");

	/* Fullscreen button */
	m_pFullscreenBtn = gtk_button_new_from_icon_name("view-fullscreen");
	gtk_button_set_has_frame(GTK_BUTTON(m_pFullscreenBtn), FALSE);
	g_signal_connect(G_OBJECT(m_pFullscreenBtn), "clicked", G_CALLBACK(viewer_fullscreen_button_clicked_cb), this);
	gtk_widget_add_css_class(m_pFullscreenBtn, "media-btn");

	/* Attach motion controllers so hovering any button keeps the
	 * controls bar (and the timeline) visible. */
	{
		GtkWidget *btns[] = { m_pSpeedButton, m_pSnapBtn, m_pVolumeButton,
			m_pVideoOptionsBtn, m_pFullscreenBtn };
		for (size_t i = 0; i < sizeof(btns)/sizeof(btns[0]); i++)
		{
			GtkEventController *motion = gtk_event_controller_motion_new();
			g_signal_connect(motion, "motion", G_CALLBACK(controls_show_on_event_cb), this);
			gtk_widget_add_controller(btns[i], motion);
		}
	}

	/* Far-right group: speed | snap | vol | fullscreen */
	GtkWidget* extraBtns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_append(GTK_BOX(extraBtns), m_pSpeedButton);
	gtk_box_append(GTK_BOX(extraBtns), m_pSnapBtn);
	gtk_box_append(GTK_BOX(extraBtns), m_pVolumeButton);
	gtk_box_append(GTK_BOX(extraBtns), m_pVideoOptionsBtn);
	gtk_box_append(GTK_BOX(extraBtns), m_pFullscreenBtn);
	gtk_widget_set_halign(extraBtns, GTK_ALIGN_END);
	gtk_widget_set_valign(extraBtns, GTK_ALIGN_CENTER);

	/* Rewind button */
	m_pRewindBtn = gtk_button_new_from_icon_name("media-seek-backward");
	{
		GtkEventController *motion = gtk_event_controller_motion_new();
		g_signal_connect(motion, "motion", G_CALLBACK(controls_show_on_event_cb), this);
		gtk_widget_add_controller(m_pRewindBtn, motion);
	}
	gtk_button_set_has_frame(GTK_BUTTON(m_pRewindBtn), FALSE);
	g_signal_connect_swapped(G_OBJECT(m_pRewindBtn), "clicked", G_CALLBACK(viewer_skip_back_cb), this);
	gtk_widget_add_css_class(m_pRewindBtn, "media-btn");

	/* Fast-forward button */
	m_pFfBtn = gtk_button_new_from_icon_name("media-seek-forward");
	{
		GtkEventController *motion = gtk_event_controller_motion_new();
		g_signal_connect(motion, "motion", G_CALLBACK(controls_show_on_event_cb), this);
		gtk_widget_add_controller(m_pFfBtn, motion);
	}
	gtk_button_set_has_frame(GTK_BUTTON(m_pFfBtn), FALSE);
	g_signal_connect_swapped(G_OBJECT(m_pFfBtn), "clicked", G_CALLBACK(viewer_skip_fwd_cb), this);
	gtk_widget_add_css_class(m_pFfBtn, "media-btn");

	/* Play button */
	m_pPlayImage = gtk_image_new_from_icon_name("media-playback-start");
	m_pPlayButton = gtk_button_new();
	{
		GtkEventController *motion = gtk_event_controller_motion_new();
		g_signal_connect(motion, "motion", G_CALLBACK(controls_show_on_event_cb), this);
		gtk_widget_add_controller(m_pPlayButton, motion);
	}
	gtk_button_set_has_frame(GTK_BUTTON(m_pPlayButton), FALSE);
	gtk_button_set_child(GTK_BUTTON(m_pPlayButton), m_pPlayImage);
	g_signal_connect_swapped(G_OBJECT(m_pPlayButton), "clicked", G_CALLBACK(viewer_play_button_clicked_cb), this);
	gtk_widget_add_css_class(m_pPlayButton, "media-btn");

	/* Centered group: [rewind][play][ff] */
	GtkWidget* centerGroup = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_append(GTK_BOX(centerGroup), m_pRewindBtn);
	gtk_box_append(GTK_BOX(centerGroup), m_pPlayButton);
	gtk_box_append(GTK_BOX(centerGroup), m_pFfBtn);

	/* GtkCenterBox keeps the [rewind][play][ff] transport group pinned to the
	 * visual center while the far-right controls sit at the end. The start
	 * side is deliberately empty so the transport group stays centered
	 * instead of being shifted by the asymmetric right-hand group.
	 * GtkCenterBox requires GTK 4.4+ (we require 4.12). */
	GtkWidget *controlsInner = gtk_center_box_new();
	gtk_widget_set_hexpand(controlsInner, TRUE);
	gtk_center_box_set_center_widget(GTK_CENTER_BOX(controlsInner), centerGroup);
	gtk_center_box_set_end_widget(GTK_CENTER_BOX(controlsInner), extraBtns);

	m_pControlsBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_set_margin_start(m_pControlsBox, 6);
	gtk_widget_set_margin_end(m_pControlsBox, 6);
	gtk_box_append(GTK_BOX(m_pControlsBox), controlsInner);

	/* ── Row 2: Timeline scale ─────────────────────────────────── */
	GtkWidget* scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 1.0, 0.001);
	m_pPlayProgress = scale;
	gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);
	gtk_widget_set_can_focus(scale, FALSE);
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

	m_pTimelineRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_set_margin_start(m_pTimelineRow, 6);
	gtk_widget_set_margin_end(m_pTimelineRow, 6);
	gtk_box_append(GTK_BOX(m_pTimelineRow), scale);

	/* ── Row 3: Time labels — at the edges of the scale ──────── */
	m_pTimeElapsedLabel = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(m_pTimeElapsedLabel), "<b>0:00</b>");
	gtk_widget_add_css_class(m_pTimeElapsedLabel, "time-label");

	m_pTimeDurationLabel = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(m_pTimeDurationLabel), "<b>0:00</b>");
	gtk_widget_add_css_class(m_pTimeDurationLabel, "time-label");

	GtkWidget* timeRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_set_margin_start(timeRow, 18);
	gtk_widget_set_margin_end(timeRow, 18);
	gtk_box_append(GTK_BOX(timeRow), m_pTimeElapsedLabel);
	GtkWidget* timeSpacer = gtk_label_new("");
	gtk_widget_set_hexpand(timeSpacer, TRUE);
	gtk_box_append(GTK_BOX(timeRow), timeSpacer);
	gtk_box_append(GTK_BOX(timeRow), m_pTimeDurationLabel);

	/* ── Assemble alignment ──────────────────────────────────── */
	gtk_box_append(GTK_BOX(alignment), m_pControlsBox);
	gtk_box_append(GTK_BOX(alignment), m_pTimelineRow);
	gtk_box_append(GTK_BOX(alignment), timeRow);

	m_pMediaControls = alignment;
	m_pTimeline = scale;

	/* Scroll over the floating controls must reach the viewer's unified
	 * handler too: Ctrl+wheel zooms (video), plain wheel navigates.  The
	 * controls are an overlay child above the video/image widget, so they
	 * would otherwise swallow these scroll events. */
	{
		GtkEventController *scroll = gtk_event_controller_scroll_new(
			(GtkEventControllerScrollFlags)GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES);
		g_signal_connect(scroll, "scroll", G_CALLBACK(viewer_scrollwheel_event), this);
		gtk_widget_add_controller(m_pMediaControls, scroll);
	}

	/* ── Screen-wide CSS ──────────────────────────────────────── */
	GtkCssProvider *cssProvider = gtk_css_provider_new();
	gtk_css_provider_load_from_string(cssProvider,
		".speed-active { background-image: none; background-color: @theme_selected_bg_color; color: @theme_selected_fg_color; }\n"
		".filmstrip-overlay { background-color: transparent; }\n"
".media-btn { border-radius: 8px; min-width: 36px; min-height: 36px; padding: 4px; background-image: none; background-color: transparent; border: none; outline: none; }\n"
		".media-btn:hover { background-image: none; background-color: alpha(@theme_bg_color, 0.60); border: none; }\n"
		".media-btn:focus, .media-btn:focus-visible { outline: none; box-shadow: none; }\n"
		".time-label { color: rgba(255, 255, 255, 0.85); font-size: 12px; }\n"
		".image-load-error { color: rgba(255, 255, 255, 1.0); font-size: 18px; padding: 20px; border-radius: 12px; background-color: alpha(#000, 0.55); }\n");
	gtk_style_context_add_provider_for_display(gdk_display_get_default(),
		GTK_STYLE_PROVIDER(cssProvider), GTK_STYLE_PROVIDER_PRIORITY_USER);
	g_object_unref(cssProvider);

	/* ── Wire events ──────────────────────────────────────────── */
	m_iPlayProgressChangeHandler = g_signal_connect(G_OBJECT(m_pPlayProgress), "change-value",
		G_CALLBACK(viewer_scale_change_value_cb), this);

	/* Show everything initially, then hide what should be invisible at startup */
	/* Only the play button should be visible when a video is loaded. */
	set_control_visible(m_pTimeElapsedLabel, false);
	set_control_visible(m_pTimeDurationLabel, false);
	set_control_visible(m_pTimelineRow, false);
	set_control_visible(m_pRewindBtn, false);
	set_control_visible(m_pSpeedButton, false);
	set_control_visible(m_pFfBtn, false);
	set_control_visible(m_pSnapBtn, false);
	set_control_visible(m_pVolumeButton, false);
			set_control_visible(m_pVideoOptionsBtn, false);
	set_control_visible(m_pFullscreenBtn, false);

	m_iCurrentOrientation = 1;
	
	m_SlideShowState = SLIDESHOW_STATE_ADVANCE;
	m_iIdleSetIndex = 0;
	m_iTimeoutScrollbars = 0;
	m_iTimeoutUpdateListID = 0;
	m_iTimeoutSlideshowID = 0;
	m_iTimeoutClickID = 0;
	m_iTimeoutMouseMotionNotify = 0;
	m_VideoZoomType = VIDEO_ZOOM_SOFTWARE;
	m_dVideoZoom = 1.0;
	m_dVideoZoomFinal = 1.0;
	m_dVideoZoomMin = 1.0;
	m_dVideoPanX = 0.;
	m_dVideoPanY = 0.;
	m_dVideoLastWidgetW = 0.;
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
	m_iTimeoutPlayProgress = 0;
	m_iSlideShowWaitCount = 0;

	m_pAdjustmentH = quiver_image_view_get_hadjustment(QUIVER_IMAGE_VIEW(m_pImageView));
	m_pAdjustmentV = quiver_image_view_get_vadjustment(QUIVER_IMAGE_VIEW(m_pImageView));

	m_pScrollbarV = gtk_scrollbar_new (GTK_ORIENTATION_VERTICAL, m_pAdjustmentV);
	m_pScrollbarH = gtk_scrollbar_new (GTK_ORIENTATION_HORIZONTAL, m_pAdjustmentH);
	
	m_pNavigationBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

GtkWidget *image = gtk_image_new_from_icon_name("view-fullscreen");
	gtk_box_append (GTK_BOX (m_pNavigationBox), image);

	{
		GtkGesture *click = gtk_gesture_click_new();
		g_signal_connect(click, "pressed", G_CALLBACK(viewer_navigation_button_press_event), this);
		gtk_widget_add_controller(m_pNavigationBox, GTK_EVENT_CONTROLLER(click));
	}

	

	m_pGrid = gtk_grid_new ();
	m_pOverlay = gtk_overlay_new ();
	gtk_widget_set_hexpand(m_pOverlay, TRUE);
	gtk_widget_set_vexpand(m_pOverlay, TRUE);
	m_pStack = gtk_stack_new ();
	gtk_widget_set_hexpand(m_pStack, TRUE);
	gtk_widget_set_vexpand(m_pStack, TRUE);

	// left right top bottom
	// media controls float above the image/video area
	gtk_widget_set_hexpand(alignment, TRUE);
	gtk_widget_set_valign(alignment, GTK_ALIGN_END);
	gtk_overlay_add_overlay(GTK_OVERLAY(m_pOverlay), alignment);
	// In GTK4 overlay children contribute to the overlay's size by default;
	// the transport buttons' min-widths would otherwise pin the viewer area
	// wider than the window (controls off-center, timeline scale overflowing
	// past the edge).  Mark the floating bar as non-measuring so it always
	// spans exactly the viewer's actual width.
	gtk_overlay_set_measure_overlay(GTK_OVERLAY(m_pOverlay), alignment, FALSE);

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

	m_bMaximizeViewableArea = prefsPtr->GetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_ROTATE_FOR_BEST_FIT, false);
	m_bHideFilmstripFS = prefsPtr->GetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_FILMSTRIP_HIDE_FS, true);
	
	m_bIsPlaying = false;

	quiver_image_view_set_magnification_mode(QUIVER_IMAGE_VIEW(m_pImageView),QUIVER_IMAGE_VIEW_MAGNIFICATION_MODE_SMOOTH);
	
	QuiverImageViewMode view_mode =
		(QuiverImageViewMode)prefsPtr->GetInteger(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_DEFAULT_VIEW_MODE, QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW);

	quiver_image_view_set_view_mode(QUIVER_IMAGE_VIEW(m_pImageView), view_mode);

	/* GTK4 drag-and-drop source via GtkDragSource controller */
	if (!m_pDragSource) {
		m_pDragSource = gtk_drag_source_new();
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
	quiver_icon_view_set_thumbnail_pixbuf_func(QUIVER_ICON_VIEW(m_pIconView),(QuiverIconViewGetThumbnailPixbufFunc)thumbnail_pixbuf_callback,this,NULL);
	quiver_icon_view_set_icon_pixbuf_func(QUIVER_ICON_VIEW(m_pIconView),(QuiverIconViewGetIconPixbufFunc)icon_pixbuf_callback,this,NULL);
	quiver_icon_view_set_scroll_type(QUIVER_ICON_VIEW(m_pIconView),QUIVER_ICON_VIEW_SCROLL_SMOOTH_CENTER);
	int iIconSize = prefsPtr->GetInteger(QUIVER_PREFS_VIEWER,QUIVER_PREFS_VIEWER_FILMSTRIP_SIZE, 128);
	quiver_icon_view_set_icon_size(QUIVER_ICON_VIEW(m_pIconView),iIconSize,iIconSize);
	quiver_icon_view_set_drag_behavior(QUIVER_ICON_VIEW(m_pIconView),QUIVER_ICON_VIEW_DRAG_BEHAVIOR_SCROLL);

	g_signal_connect(G_OBJECT(m_pIconView),"cell_activated",G_CALLBACK(viewer_iconview_cell_activated),this);
	g_signal_connect(G_OBJECT(m_pIconView),"cursor_changed",G_CALLBACK(viewer_iconview_cursor_changed),this);

	//popup menu stuff: GTK4 gesture controllers on the image view
	{
		GtkGesture *click = gtk_gesture_click_new();
		g_signal_connect(click, "pressed", G_CALLBACK(viewer_button_press_cb), this);
		g_signal_connect(click, "released", G_CALLBACK(viewer_button_release_cb), this);
		gtk_widget_add_controller(m_pImageView, GTK_EVENT_CONTROLLER(click));
	}
	{
		GtkEventController *scroll = gtk_event_controller_scroll_new(
			(GtkEventControllerScrollFlags)GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES);
		g_signal_connect(scroll, "scroll", G_CALLBACK(viewer_scrollwheel_event), this);
		gtk_widget_add_controller(m_pImageView, scroll);
	}
	{
		GtkEventController *motion = gtk_event_controller_motion_new();
		g_signal_connect(motion, "motion", G_CALLBACK(viewer_motion_notify), this);
		gtk_widget_add_controller(m_pImageView, motion);
	}
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

	//g_signal_connect(G_OBJECT(m_pIconView),"selection_changed",G_CALLBACK(iconview_selection_changed_cb),this);
	IPixbufLoaderObserverPtr tmp( new ViewerImageViewPixbufLoaderObserver(QUIVER_IMAGE_VIEW(m_pImageView), m_pImageErrorLabel));
	m_PixbufLoaderObserverPtr = tmp;
	m_ImageLoader.AddPixbufLoaderObserver(m_PixbufLoaderObserverPtr.get());
	
	gtk_widget_set_visible(m_pHBox, TRUE);
	gtk_widget_set_visible(m_pHBox, FALSE);
	
	m_pNavigationWindow = gtk_window_new ();
	m_pNavigationControl = quiver_navigation_control_new_with_adjustments (m_pAdjustmentH, m_pAdjustmentV);

	// GTK4: navigation control release handled via gesture click controller
	{
		GtkGesture *click = gtk_gesture_click_new();
		g_signal_connect(click, "released", G_CALLBACK(navigation_control_button_release_event), this);
		gtk_widget_add_controller(m_pNavigationControl, GTK_EVENT_CONTROLLER(click));
	}
	gtk_window_set_decorated(GTK_WINDOW(m_pNavigationWindow), FALSE);
	gtk_window_set_transient_for(GTK_WINDOW(m_pNavigationWindow), GTK_WINDOW(gtk_widget_get_root(m_pImageView)));

	GtkWidget * frame = gtk_frame_new(NULL);

	
	gtk_frame_set_child (GTK_FRAME(frame), m_pNavigationControl);
	gtk_window_set_child (GTK_WINDOW(m_pNavigationWindow), frame);
	
	bool bShowFilmstrip = prefsPtr->GetBoolean(QUIVER_PREFS_VIEWER,QUIVER_PREFS_VIEWER_FILMSTRIP_SHOW,true);
	if (!bShowFilmstrip)
	{
		gtk_widget_set_visible(m_pIconView, FALSE);
	}
	gboolean bQuickPreview = (gboolean)prefsPtr->GetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_QUICK_PREVIEW, true);
	m_ImageLoader.EnableQuickPreview(bQuickPreview);
	

	// set up the gstreamer pipeline
	m_pPipeline = gst_element_factory_make("playbin", "player");

	m_pVideoSinkWidget = NULL;
	m_pVideoFixed = NULL;
	m_dVideoZoomFinal = 1.0;
	m_dVideoZoomMin = 1.0;
	m_iVideoZoomTimeoutID = 0;
	m_dVideoPanX = 0.;
	m_dVideoPanY = 0.;
	m_dVideoLastWidgetW = 0.;
	m_dVideoLastWidgetH = 0.;
	m_dVideoLastZc = 1.0;
	m_bVideoZoomCropActive = FALSE;
	m_bVideoZoomInputCropActive = FALSE;
	m_bVideoPanning = FALSE;
	m_bVideoNeedsFirstFrame = FALSE;
	m_bVideoFlushPending = FALSE;
	m_pVideoPaintable = NULL;
	GstElement* video_sink = NULL;
	GstElement* gtk4glsink = gst_element_factory_make("gtk4paintablesink", NULL);
	GstElement* glupload = NULL;
	if (gtk4glsink != NULL)
	{
		if (g_object_class_find_property(G_OBJECT_GET_CLASS(gtk4glsink), "force-aspect-ratio") != NULL)
			g_object_set(G_OBJECT(gtk4glsink), "force-aspect-ratio", TRUE, NULL);
		g_object_get(G_OBJECT(gtk4glsink), "paintable", &m_pVideoPaintable, NULL);

		m_pVideoSinkWidget = gtk_picture_new_for_paintable(m_pVideoPaintable);
		gtk_picture_set_content_fit(GTK_PICTURE(m_pVideoSinkWidget), GTK_CONTENT_FIT_FILL);
		gtk_picture_set_can_shrink(GTK_PICTURE(m_pVideoSinkWidget), TRUE);

		/* Restore opacity after the sink delivers a new frame. */
		g_signal_connect(m_pVideoPaintable, "invalidate-contents", G_CALLBACK(video_paintable_invalidated_cb), this);

		/* Link glupload explicitly to gtk4paintablesink (matching the
		 * known-good `glupload ! gtk4paintablesink` pipeline).
		 * glupload + the sink must be added to the zoombin DIRECTLY,
		 * not wrapped in a separate glsinkbin. */
		glupload = gst_element_factory_make("glupload", NULL);
		if (glupload != NULL && gst_element_link(glupload, gtk4glsink))
		{
			video_sink = gtk4glsink;
		}
		else
		{
			if (glupload != NULL)
				gst_object_unref(glupload);
			glupload = NULL;
			video_sink = gtk4glsink;
		}
	}
	else
	{
		video_sink = gst_element_factory_make("gtk4sink", NULL);
		if (video_sink != NULL)
		{
			if (g_object_class_find_property(G_OBJECT_GET_CLASS(video_sink), "force-aspect-ratio") != NULL)
				g_object_set(G_OBJECT(video_sink), "force-aspect-ratio", TRUE, NULL);
			g_object_get(G_OBJECT(video_sink), "paintable", &m_pVideoPaintable, NULL);

			m_pVideoSinkWidget = gtk_picture_new_for_paintable(m_pVideoPaintable);
			gtk_picture_set_content_fit(GTK_PICTURE(m_pVideoSinkWidget), GTK_CONTENT_FIT_FILL);
			gtk_picture_set_can_shrink(GTK_PICTURE(m_pVideoSinkWidget), TRUE);
			g_signal_connect(m_pVideoPaintable, "invalidate-contents", G_CALLBACK(video_paintable_invalidated_cb), this);
		}
	}

	/* digital video zoom: wrap the sink in a crop + scale chain so the
	 * pipeline itself implements the zoom.  The chain is built dynamically
	 * depending on which GPU acceleration the platform provides: NVIDIA
	 * (nvvidconv), Intel Media SDK (videocrop + vapostproc), Intel
	 * gstreamer-vaapi (vaapipostproc native crop) or plain software
	 * (videocrop + videoscale).  The scaler elements accept the memory type
	 * the decoder produces on their platform (NVMM / VAMemory / VASurface /
	 * system), so no videoconvert is inserted into the accelerated paths. */
	m_pVideoZoomInput = NULL;
	m_pVideoCrop = NULL;
	m_pVideoZoomConvert = NULL;
	m_pVideoZoomScaler = NULL;
	m_pVideoZoomCaps = NULL;
	m_pVideoZoomInputCaps = NULL;
	m_VideoZoomType = VIDEO_ZOOM_SOFTWARE;
	if (video_sink != NULL)
	{
		GstElement *zoombin = gst_bin_new("videozoom");
		GstElement *first_element = NULL;

		m_VideoZoomType = VIDEO_ZOOM_GL;
		
		GstElement *glupload_zoom = gst_element_factory_make("glupload", "zoomupload");
		GstElement *glcolorconvert = gst_element_factory_make("glcolorconvert", "zoomcolorconvert");
		m_pVideoZoomScaler = gst_element_factory_make("gltransformation", "zoomtransform");
		
		GstElement *chain[4] = { glupload_zoom, glcolorconvert, m_pVideoZoomScaler, video_sink };
		guint n_chain = 4;
		first_element = chain[0];

		if (m_pVideoZoomScaler != NULL)
		{
			g_object_set(G_OBJECT(m_pVideoZoomScaler), "ortho", TRUE, NULL);
		}

		gboolean bChainOk = TRUE;
		guint n_added = 0;
		for (guint i = 0; i < n_chain; ++i)
		{
			if (chain[i] == NULL)
			{
				bChainOk = FALSE;
				break;
			}
			if (!gst_bin_add(GST_BIN(zoombin), chain[i]))
			{
				bChainOk = FALSE;
				break;
			}
			n_added++;
		}
		if (bChainOk)
		{
			for (guint i = 0; i + 1 < n_chain; ++i)
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
			/* learn the source frame size from the incoming caps so the
			 * crop amounts can be computed from it */
			if (first_element != NULL)
			{
				GstPad *crop_sinkpad = gst_element_get_static_pad(first_element, "sink");
				if (crop_sinkpad != NULL)
				{
				gst_pad_add_probe(crop_sinkpad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, video_crop_pad_probe, this, NULL);
				gst_pad_add_probe(crop_sinkpad, GST_PAD_PROBE_TYPE_EVENT_UPSTREAM, video_zoom_reconfigure_probe, this, NULL);

					/* expose this pad as the bin's sink so playbin can
					 * link to the zoombin; the capsfilter's ANY template
					 * lets HW decoders pass the template check */
					gst_element_add_pad(zoombin, gst_ghost_pad_new("sink", crop_sinkpad));

					gst_object_unref(crop_sinkpad);
				}
			}

			video_sink = zoombin;
		}
		else
		{
			/* chain could not be built: fall back to the plain, unwrapped sink */
			for (guint i = 0; i < n_added; ++i)
			{
				gst_bin_remove(GST_BIN(zoombin), chain[i]);
			}
			gst_object_unref(zoombin);
			if (m_pVideoZoomInput != NULL)
				gst_object_unref(m_pVideoZoomInput);
			if (m_pVideoCrop != NULL)
				gst_object_unref(m_pVideoCrop);
			if (m_pVideoZoomConvert != NULL)
				gst_object_unref(m_pVideoZoomConvert);
			if (m_pVideoZoomScaler != NULL)
				gst_object_unref(m_pVideoZoomScaler);
			if (m_pVideoZoomCaps != NULL)
				gst_object_unref(m_pVideoZoomCaps);
			if (m_pVideoZoomInputCaps != NULL)
				gst_object_unref(m_pVideoZoomInputCaps);
			if (glupload != NULL)
				gst_object_unref(glupload);
			m_pVideoCrop = NULL;
			m_pVideoZoomConvert = NULL;
			m_pVideoZoomScaler = NULL;
			m_pVideoZoomCaps = NULL;
			m_pVideoZoomInputCaps = NULL;
			m_VideoZoomType = VIDEO_ZOOM_SOFTWARE;
		}
	}

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

	GstPlayFlags flags = (GstPlayFlags)0;
	g_object_get(G_OBJECT(m_pPipeline), "flags", &flags, NULL);
	flags = (GstPlayFlags)(flags & ~(1 << 2)); // Disable subtitles by default
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

		gtk_widget_set_size_request(m_pVideoSinkWidget, 1, 1);
		quiver_freelayout_put(m_pVideoFixed, m_pVideoSinkWidget, 0, 0);

		attach_viewer_input_controllers(m_pVideoFixed, this);
		attach_viewer_input_controllers(m_pVideoSinkWidget, this);
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
	gst_bus_add_watch (bus, (GstBusFunc) gstreamer_bus_watcher, this);
	gst_object_unref (bus);
}


Viewer::Viewer() : m_ViewerImplPtr(new Viewer::ViewerImpl(this))
{
	

}

Viewer::~Viewer()
{
	m_ViewerImplPtr->SlideShowStop(false);
	
	gtk_window_destroy(GTK_WINDOW(m_ViewerImplPtr->m_pNavigationWindow));
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
	bool bReset = false;
	QuiverImageViewMode mode = quiver_image_view_get_view_mode(QUIVER_IMAGE_VIEW(m_ViewerImplPtr->m_pImageView));

	quiver_image_view_reset_view_mode(QUIVER_IMAGE_VIEW(m_ViewerImplPtr->m_pImageView), TRUE);
	QuiverImageViewMode mode2 = quiver_image_view_get_view_mode(QUIVER_IMAGE_VIEW(m_ViewerImplPtr->m_pImageView));
	bReset = (mode != mode2);

	return bReset;
}

void Viewer::GrabFocus()
{
	gtk_widget_grab_focus(m_ViewerImplPtr->m_pImageView);
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
		quiver_image_view_set_pixbuf(QUIVER_IMAGE_VIEW(m_ViewerImplPtr->m_pImageView),NULL);
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

}

void Viewer::Hide()
{
	m_ViewerImplPtr->StopVideo(true);
	SlideShowStop();
	
	gtk_widget_set_visible(m_ViewerImplPtr->m_pHBox, FALSE);

	m_ViewerImplPtr->m_ImageListPtr->BlockHandler(m_ViewerImplPtr->m_ImageListEventHandlerPtr);
}

void Viewer::RegisterActions()
{
	/* Viewer simple actions */
	QuiverUtils::AddSimpleAction(ACTION_VIEWER_CUT, "<Control>X", viewer_action_handler_cb, m_ViewerImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_VIEWER_COPY, "<Control>C", viewer_action_handler_cb, m_ViewerImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_VIEWER_TRASH, "Delete", viewer_action_handler_cb, m_ViewerImplPtr.get());

	QuiverUtils::AddSimpleAction(ACTION_VIEWER_PREVIOUS, "BackSpace", viewer_action_handler_cb, m_ViewerImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_VIEWER_PREVIOUS_2, "<Shift>space", viewer_action_handler_cb, m_ViewerImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_VIEWER_NEXT, "space", viewer_action_handler_cb, m_ViewerImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_VIEWER_NEXT_2, "<Shift>BackSpace", viewer_action_handler_cb, m_ViewerImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_VIEWER_FIRST, "Home", viewer_action_handler_cb, m_ViewerImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_VIEWER_LAST, "End", viewer_action_handler_cb, m_ViewerImplPtr.get());

	QuiverUtils::AddSimpleAction(ACTION_VIEWER_ZOOM_IN, "equal", viewer_action_handler_cb, m_ViewerImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_VIEWER_ZOOM_OUT, "minus", viewer_action_handler_cb, m_ViewerImplPtr.get());

	QuiverUtils::AddSimpleAction(ACTION_VIEWER_ROTATE_CW, "r", viewer_action_handler_cb, m_ViewerImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_VIEWER_ROTATE_CW_2, "<Shift>l", viewer_action_handler_cb, m_ViewerImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_VIEWER_ROTATE_CCW, "l", viewer_action_handler_cb, m_ViewerImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_VIEWER_ROTATE_CCW_2, "<Shift>r", viewer_action_handler_cb, m_ViewerImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_VIEWER_FLIP_H, "h", viewer_action_handler_cb, m_ViewerImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_VIEWER_FLIP_H_2, "<Shift>v", viewer_action_handler_cb, m_ViewerImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_VIEWER_FLIP_V, "v", viewer_action_handler_cb, m_ViewerImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_VIEWER_FLIP_V_2, "<Shift>h", viewer_action_handler_cb, m_ViewerImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_VIEWER_VIDEO_PLAY, "P", viewer_action_handler_cb, m_ViewerImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_VIEWER_VIDEO_PLAY_2, "<Control>space", viewer_action_handler_cb, m_ViewerImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_VIEWER_VIDEO_SKIP_FORWARD, "period", viewer_action_handler_cb, m_ViewerImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_VIEWER_VIDEO_SKIP_BACK, "comma", viewer_action_handler_cb, m_ViewerImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_VIEWER_VIDEO_SEEK_FWD_5, "<Shift>period", viewer_action_handler_cb, m_ViewerImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_VIEWER_VIDEO_SEEK_BACK_5, "<Shift>comma", viewer_action_handler_cb, m_ViewerImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_VIEWER_VIDEO_FRAME_FWD, "", viewer_action_handler_cb, m_ViewerImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_VIEWER_VIDEO_FRAME_BACK, "", viewer_action_handler_cb, m_ViewerImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_VIEWER_VIDEO_SNAPSHOT, "", viewer_action_handler_cb, m_ViewerImplPtr.get());

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
	};
	gint zoom_values[] = {
		QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW,
		QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW_STRETCH,
		QUIVER_IMAGE_VIEW_MODE_ACTUAL_SIZE,
		QUIVER_IMAGE_VIEW_MODE_FILL_SCREEN,
		QUIVER_IMAGE_VIEW_MODE_ZOOM,
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

int Viewer::GetCurrentOrientation()
{
	return m_ViewerImplPtr->GetCurrentOrientation();	
}


static gboolean timeout_advance_slideshow (gpointer data)
{
	Viewer::ViewerImpl* pViewerImpl = (Viewer::ViewerImpl*)data;
	
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
					pViewerImpl->SetImageIndex(iNextIndex,true,false);

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

				pViewerImpl->PlayPauseVideo();
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


void Viewer::SlideShowStart()
{
	PreferencesPtr prefsPtr = Preferences::GetInstance();
	bool bTransition = prefsPtr->GetBoolean(QUIVER_PREFS_SLIDESHOW,QUIVER_PREFS_SLIDESHOW_TRANSITION,true);
	
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

		EmitSlideShowStartedEvent();
	}
	else
	{
		SlideShowStop();
	}

	m_ViewerImplPtr->UpdateUI();
}


void Viewer::SlideShowStop()
{
	m_ViewerImplPtr->SlideShowStop();
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
	gulong n = pViewerImpl->m_ImageListPtr->GetSize();
	static gulong last = (gulong)-1;
	if (n != last)
	{
		g_printerr("[ICONVIEW] n_cells=%lu\n", n);
		last = n;
	}
	return n;
}

static GdkPixbuf* icon_pixbuf_callback(QuiverIconView *iconview, gulong cell, gpointer user_data)
{
	Viewer::ViewerImpl* pViewerImpl = (Viewer::ViewerImpl*)user_data;
	QuiverFile f = pViewerImpl->m_ImageListPtr->Get(cell);

	guint width, height;
	quiver_icon_view_get_icon_size(iconview,&width, &height);
	return f.GetIcon(width,height);
}


static gboolean thumbnail_loader_update_list (gpointer data)
{
	Viewer::ViewerImpl* pViewerImpl = (Viewer::ViewerImpl*)data;	
	pViewerImpl->m_ThumbnailLoader.UpdateList();
	pViewerImpl->m_iTimeoutUpdateListID = 0;
	return FALSE;
}


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
	}
	
	if (need_new_thumb)
	{
		// add a timeout
		pViewerImpl->QueueIconViewUpdate();
	}
	
	return pixbuf;
}

void Viewer::ViewerImpl::QueueIconViewUpdate(int timeout)
{
	if (!m_iTimeoutUpdateListID)
	{
		m_iTimeoutUpdateListID = g_timeout_add(timeout,thumbnail_loader_update_list,this);
	}
}

void Viewer::ViewerImpl::SlideShowStop(bool bEmitStopEvent)
{
	PreferencesPtr prefsPtr = Preferences::GetInstance();

	quiver_image_view_set_enable_transitions(QUIVER_IMAGE_VIEW(m_pImageView),FALSE);

	if (0 != m_iTimeoutSlideshowID)
	{

		g_source_remove (m_iTimeoutSlideshowID);
		m_iTimeoutSlideshowID = 0;
	}

	if (bEmitStopEvent)
	{
		m_pViewer->EmitSlideShowStoppedEvent();
	}

	UpdateUI();
}

static gboolean timeout_update_scrollbars(gpointer user_data)
{
	Viewer::ViewerImpl* pViewerImpl = (Viewer::ViewerImpl*)user_data;
	pViewerImpl->UpdateScrollbars();

	return FALSE;
}

static void image_view_adjustment_changed (GtkAdjustment *adjustment, gpointer user_data)
{ (void)adjustment; 
	Viewer::ViewerImpl* pViewerImpl = (Viewer::ViewerImpl*)user_data;
	
	if (0 != pViewerImpl->m_iTimeoutScrollbars)
	{
		g_source_remove(pViewerImpl->m_iTimeoutScrollbars);
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
		parent->m_ThumbnailCache.RemovePixbuf(parent->m_ImageListPtr->GetCurrent().GetURI());
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
			}
			else
			{
				GdkRGBA color;
				
				string strBGColorImg   = prefsPtr->GetString(QUIVER_PREFS_APP,QUIVER_PREFS_APP_BG_IMAGEVIEW);
				string strBGColorThumb = prefsPtr->GetString(QUIVER_PREFS_APP,QUIVER_PREFS_APP_BG_ICONVIEW);
						
				if (!parent->m_bFilmstripOverlay)
				{
					gdk_rgba_parse(&color, strBGColorThumb.c_str());
					set_widget_bg_color(parent->m_pIconView, &color);
				}
				
				gdk_rgba_parse(&color, strBGColorImg.c_str());
				set_widget_bg_color(parent->m_pImageView, &color);
				
			}
		}
		else if (QUIVER_PREFS_APP_BG_IMAGEVIEW == event->GetKey() )
		{
			if ( !prefsPtr->GetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_USE_THEME_COLOR,true) )
			{
				GdkRGBA color;
				
				string strBGColorImg   = prefsPtr->GetString(QUIVER_PREFS_APP,QUIVER_PREFS_APP_BG_IMAGEVIEW);
				
				gdk_rgba_parse(&color, strBGColorImg.c_str());
				set_widget_bg_color(parent->m_pImageView, &color);
			}			
		}
		else if (QUIVER_PREFS_APP_BG_ICONVIEW == event->GetKey() )
		{
			if ( !prefsPtr->GetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_USE_THEME_COLOR,true) )
			{
				if (!parent->m_bFilmstripOverlay)
				{
					GdkRGBA color;
					
					string strBGColorThumb = prefsPtr->GetString(QUIVER_PREFS_APP,QUIVER_PREFS_APP_BG_ICONVIEW);						

					gdk_rgba_parse(&color, strBGColorThumb.c_str());
					set_widget_bg_color(parent->m_pIconView, &color);
				}
			}
		}
	}
	else if ( QUIVER_PREFS_VIEWER == event->GetSection () )
	{
		if (QUIVER_PREFS_VIEWER_FILMSTRIP_SHOW == event->GetKey() )
		{
		}
		else if (QUIVER_PREFS_VIEWER_FILMSTRIP_POSITION == event->GetKey() )
		{
			parent->AddFilmstrip();
		}
		else if (QUIVER_PREFS_VIEWER_FILMSTRIP_SIZE == event->GetKey() )
		{
			quiver_icon_view_set_icon_size(QUIVER_ICON_VIEW(parent->m_pIconView), event->GetNewInteger(), event->GetNewInteger());
		}
		else if (QUIVER_PREFS_VIEWER_FILMSTRIP_OVERLAY == event->GetKey() )
		{
			parent->AddFilmstrip();
		}
		else if (QUIVER_PREFS_VIEWER_FILMSTRIP_HIDE_FS == event->GetKey() )
		{
			parent->m_bHideFilmstripFS = event->GetNewBoolean();
		}
		else if (QUIVER_PREFS_VIEWER_QUICK_PREVIEW == event->GetKey() )
		{
			parent->m_ImageLoader.EnableQuickPreview(event->GetNewBoolean());
		}
		else if (QUIVER_PREFS_VIEWER_SCROLLBARS_HIDE == event->GetKey() )
		{
			parent->UpdateScrollbars();
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
	GtkWidget* imageview;
	Statusbar* statusbar;
	ImageLoader* imageloader;
	gulong index;
	guint width;
	guint height;
	gulong start;
	gulong end;
	bool is_mapped;
	bool is_working;
	GMutex mutex;
	GCond cond;
	bool done;
};

static gboolean idle_invalidate_cell_v(gpointer data) {
	ViewerThumbLoaderSyncData* pData = (ViewerThumbLoaderSyncData*)data;
	quiver_icon_view_invalidate_cell(QUIVER_ICON_VIEW(pData->iconview), pData->index);
	delete pData;
	return G_SOURCE_REMOVE;
}

static gboolean idle_get_visible_range_v(gpointer data) {
	ViewerThumbLoaderSyncData* pData = (ViewerThumbLoaderSyncData*)data;
	quiver_icon_view_get_visible_range(QUIVER_ICON_VIEW(pData->iconview), &pData->start, &pData->end);
	g_mutex_lock(&pData->mutex);
	pData->done = true;
	g_cond_signal(&pData->cond);
	g_mutex_unlock(&pData->mutex);
	return G_SOURCE_REMOVE;
}

static gboolean idle_get_icon_size_v(gpointer data) {
	ViewerThumbLoaderSyncData* pData = (ViewerThumbLoaderSyncData*)data;
	quiver_icon_view_get_icon_size(QUIVER_ICON_VIEW(pData->iconview), &pData->width, &pData->height);
	g_mutex_lock(&pData->mutex);
	pData->done = true;
	g_cond_signal(&pData->cond);
	g_mutex_unlock(&pData->mutex);
	return G_SOURCE_REMOVE;
}

static gboolean idle_is_mapped_v(gpointer data) {
	ViewerThumbLoaderSyncData* pData = (ViewerThumbLoaderSyncData*)data;
	pData->is_mapped = gtk_widget_get_mapped(pData->iconview) ? true : false;
	pData->is_working = pData->imageloader->IsWorking() || quiver_image_view_is_in_transition(QUIVER_IMAGE_VIEW(pData->imageview));
	g_mutex_lock(&pData->mutex);
	pData->done = true;
	g_cond_signal(&pData->cond);
	g_mutex_unlock(&pData->mutex);
	return G_SOURCE_REMOVE;
}

static gboolean idle_set_is_running_v(gpointer data) {
	ViewerThumbLoaderSyncData* pData = (ViewerThumbLoaderSyncData*)data;
	if (pData->statusbar) {
		if (pData->is_mapped) {
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
	ViewerThumbLoaderSyncData syncData;
	syncData.iconview = m_pViewerImpl->m_pIconView;
	syncData.imageview = m_pViewerImpl->m_pImageView;
	syncData.imageloader = &m_pViewerImpl->m_ImageLoader;
	syncData.done = false;
	g_mutex_init(&syncData.mutex);
	g_cond_init(&syncData.cond);
	if (!ThreadUtil::IsGUIThread()) { g_idle_add_full(G_PRIORITY_HIGH, idle_is_mapped_v, &syncData, NULL);
	g_mutex_lock(&syncData.mutex);
	while(!syncData.done) g_cond_wait(&syncData.cond, &syncData.mutex);
	g_mutex_unlock(&syncData.mutex); } else { idle_is_mapped_v(&syncData); }
	bool is_mapped = syncData.is_mapped;
	bool is_working = syncData.is_working;
	g_mutex_clear(&syncData.mutex);
	g_cond_clear(&syncData.cond);

	if (is_working)
	{
		usleep(100000);
	}

	if (is_mapped && item.m_ulIndex < m_pViewerImpl->m_ImageListPtr->GetSize())
	{
		// don't copy the quiver file, instead make a new one
		// based on the uri. this is to get around an issue with
		// concurrent writes to shared pointers from different threads
		QuiverFile f(item.m_QuiverFile);

		GdkPixbuf *pixbuf = NULL;
		pixbuf = m_pViewerImpl->m_ThumbnailCache.GetPixbuf(f.GetURI());				
	
		if (NULL != pixbuf)
		{
			// check if the thumbnail is the correct size
			guint thumb_width, thumb_height;
			guint bound_width = f.GetWidth();
			guint bound_height = f.GetHeight();

			if (4 < f.GetOrientation())
			{
				swap(bound_width,bound_height);
			}

			thumb_width = gdk_pixbuf_get_width(pixbuf);
			thumb_height = gdk_pixbuf_get_height(pixbuf);
			
			quiver_rect_get_bound_size(uiWidth,uiHeight, &bound_width,&bound_height,FALSE);
			if (thumb_width != bound_width || thumb_height != bound_height)
			{
				// need a new thumbnail because the current cached size
				// is not the same as the size needed
				g_object_unref(pixbuf);
				pixbuf = NULL;
			}
				
		}

		if (NULL == pixbuf)
		{
			pixbuf = f.GetThumbnail(MAX(uiWidth,uiHeight));
	
		}

		if (NULL != pixbuf)
		{
			guint thumb_width, thumb_height;
			thumb_width = gdk_pixbuf_get_width(pixbuf);
			thumb_height = gdk_pixbuf_get_height(pixbuf);

			guint bound_width = f.GetWidth();
			guint bound_height = f.GetHeight();
			
			if (4 < f.GetOrientation())
			{
				swap(bound_width,bound_height);
			}
			quiver_rect_get_bound_size(uiWidth,uiHeight, &bound_width,&bound_height,FALSE);

			if (thumb_width != bound_width || thumb_height != bound_height)
			{
				GdkPixbuf* newpixbuf = gdk_pixbuf_scale_simple (
								pixbuf,
								bound_width,
								bound_height,
								GDK_INTERP_BILINEAR);
				g_object_unref(pixbuf);
				pixbuf = newpixbuf;
			}

			m_pViewerImpl->m_ThumbnailCache.AddPixbuf(f.GetURI(),pixbuf);
			g_object_unref(pixbuf);

			
			ViewerThumbLoaderSyncData* pInvData = new ViewerThumbLoaderSyncData();
			pInvData->iconview = m_pViewerImpl->m_pIconView;
			pInvData->index = item.m_ulIndex;
			if (!ThreadUtil::IsGUIThread()) { g_idle_add_full(G_PRIORITY_HIGH, idle_invalidate_cell_v, pInvData, NULL); } else { idle_invalidate_cell_v(pInvData); }
			
		}
	}
}

void Viewer::ViewerImpl::ViewerThumbLoader::GetVisibleRange(gulong* pulStart, gulong* pulEnd)
{
	ViewerThumbLoaderSyncData syncData;
	syncData.iconview = m_pViewerImpl->m_pIconView;
	syncData.done = false;
	g_mutex_init(&syncData.mutex);
	g_cond_init(&syncData.cond);
	if (!ThreadUtil::IsGUIThread()) { g_idle_add_full(G_PRIORITY_HIGH, idle_get_visible_range_v, &syncData, NULL);
	g_mutex_lock(&syncData.mutex);
	while(!syncData.done) g_cond_wait(&syncData.cond, &syncData.mutex);
	g_mutex_unlock(&syncData.mutex); } else { idle_get_visible_range_v(&syncData); }
	*pulStart = syncData.start;
	*pulEnd = syncData.end;
	g_mutex_clear(&syncData.mutex);
	g_cond_clear(&syncData.cond);
}


void Viewer::ViewerImpl::ViewerThumbLoader::GetIconSize(guint* puiWidth, guint* puiHeight)
{
	ViewerThumbLoaderSyncData syncData;
	syncData.iconview = m_pViewerImpl->m_pIconView;
	syncData.done = false;
	g_mutex_init(&syncData.mutex);
	g_cond_init(&syncData.cond);
	if (!ThreadUtil::IsGUIThread()) { g_idle_add_full(G_PRIORITY_HIGH, idle_get_icon_size_v, &syncData, NULL);
	g_mutex_lock(&syncData.mutex);
	while(!syncData.done) g_cond_wait(&syncData.cond, &syncData.mutex);
	g_mutex_unlock(&syncData.mutex); } else { idle_get_icon_size_v(&syncData); }
	if (puiWidth) *puiWidth = syncData.width;
	if (puiHeight) *puiHeight = syncData.height;
	g_mutex_clear(&syncData.mutex);
	g_cond_clear(&syncData.cond);
}

gulong Viewer::ViewerImpl::ViewerThumbLoader::GetNumItems()
{
	return m_pViewerImpl->m_ImageListPtr->GetSize();
}

void Viewer::ViewerImpl::ViewerThumbLoader::SetIsRunning(bool bIsRunning)
{
	ViewerThumbLoaderSyncData* pData = new ViewerThumbLoaderSyncData();
	pData->statusbar = m_pViewerImpl->m_StatusbarPtr.get();
	pData->is_mapped = bIsRunning;
	if (!ThreadUtil::IsGUIThread()) { g_idle_add_full(G_PRIORITY_HIGH, idle_set_is_running_v, pData, NULL); } else { idle_set_is_running_v(pData); }
}

void Viewer::ViewerImpl::ViewerThumbLoader::SetCacheSize(guint uiCacheSize)
{
	m_pViewerImpl->m_ThumbnailCache.SetSize(uiCacheSize);
}
