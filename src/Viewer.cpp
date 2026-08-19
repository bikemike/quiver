#include <config.h>

#include <gdk/gdk.h>
#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#endif

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
#include <libexif/exif-utils.h>

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

static gboolean viewer_scrollwheel_event(GtkWidget *widget, GdkEventScroll *event, gpointer data );
static void viewer_imageview_activated(QuiverImageView *imageview,gpointer data);
static void viewer_imageview_reload(QuiverImageView *imageview,gpointer data);
static void viewer_imageview_magnification_changed(QuiverImageView *imageview,gpointer data);
static void viewer_imageview_view_mode_changed(QuiverImageView *imageview,gpointer data);
static gboolean viewer_imageview_key_press_event(GtkWidget *imageview, GdkEventKey *event, gpointer userdata);

static void viewer_iconview_cell_activated(QuiverIconView *iconview,gulong cell,gpointer data);
static void viewer_iconview_cursor_changed(QuiverIconView *iconview,gulong cell,gpointer data);

static void viewer_volume_value_changed (GtkScaleButton *button, gdouble value, gpointer user_data);

static gboolean viewer_navigation_button_press_event(GtkWidget *widget, GdkEventButton *event, gpointer userdata);
gboolean navigation_control_button_release_event (GtkWidget *widget, GdkEventButton *event, gpointer data );
// FIXME: remove
//static GtkTableChild * GetGtkTableChild(GtkTable * table,GtkWidget	*widget_to_get);



static void set_widget_bg_color(GtkWidget *widget, GdkRGBA *color) {
    GtkStyleContext *context = gtk_widget_get_style_context(widget);
    GtkCssProvider *provider = (GtkCssProvider *)g_object_get_data(G_OBJECT(widget), "quiver-bg-provider");
    
    if (color == NULL) {
        if (provider) {
            gtk_style_context_remove_provider(context, GTK_STYLE_PROVIDER(provider));
            g_object_set_data(G_OBJECT(widget), "quiver-bg-provider", NULL);
        }
        return;
    }
    
    if (!provider) {
        provider = gtk_css_provider_new();
        g_object_set_data_full(G_OBJECT(widget), "quiver-bg-provider", provider, g_object_unref);
        gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }
    
    gchar *color_str = gdk_rgba_to_string(color);
    gchar *css = g_strdup_printf("* { background-color: %s; }", color_str);
    gtk_css_provider_load_from_data(provider, css, -1, NULL);
    g_free(css);
    g_free(color_str);
}


// popup menu callbacks
static void viewer_show_context_menu(GdkEventButton *event, gpointer userdata);
static gboolean viewer_popup_menu_cb (GtkWidget *treeview, gpointer userdata);
static gboolean viewer_button_press_cb(GtkWidget   *widget, GdkEventButton *event, gpointer user_data); 
static gboolean viewer_button_release_cb(GtkWidget   *widget, GdkEventButton *event, gpointer user_data); 
static void viewer_show_context_menu(GdkEventButton *event, gpointer userdata);
static void viewer_speed_button_clicked_cb(GtkButton *button, gpointer user_data);
static void viewer_speed_toggle_popover_cb(gpointer user_data, GtkWidget *widget);
static void viewer_fullscreen_button_clicked_cb(GtkButton *button, gpointer user_data);
static void viewer_snapshot_button_clicked_cb(GtkButton *button, gpointer user_data);
static void viewer_skip_back_cb(gpointer user_data);
static void viewer_skip_fwd_cb(gpointer user_data);
static void viewer_frame_step_back_cb(gpointer user_data);
static void viewer_frame_step_fwd_cb(gpointer user_data);



// drag/drop targets
enum {
	QUIVER_TARGET_STRING,
	QUIVER_TARGET_URI
};

static const GtkTargetEntry quiver_drag_target_table[] = {
		{ (gchar*)"STRING",     0, QUIVER_TARGET_STRING }, // STRING is used for legacy motif apps
		{ (gchar*)"text/plain", 0, QUIVER_TARGET_STRING },  // the real mime types to support
		 { (gchar*)"text/uri-list", 0, QUIVER_TARGET_URI },
};

static void signal_drag_data_get  (GtkWidget *widget, GdkDragContext *context, GtkSelectionData *selection_data, guint info, guint time,gpointer user_data);
static void signal_drag_data_delete  (GtkWidget *widget,GdkDragContext *context,gpointer user_data);
static void signal_drag_data_received(GtkWidget *widget,GdkDragContext *drag_context, gint x,gint y, GtkSelectionData *data, guint info, guint time,gpointer user_data);
static void signal_drag_begin (GtkWidget *widget,GdkDragContext *drag_context,gpointer user_data);
static void signal_drag_end(GtkWidget *widget,GdkDragContext *drag_context,gpointer user_data);
static void signal_drag_motion (GtkWidget *widget, GdkDragContext *context, gint x, gint y, guint time, gpointer user_data);
static gboolean signal_drag_drop (GtkWidget *widget, GdkDragContext *drag_context, gint x, gint y, guint time,  gpointer user_data);


static gboolean timeout_play_position (gpointer data);
static gboolean timeout_event_motion_notify (gpointer user_data);

static gchar* gst_time_format(gint64 time);
static void viewer_scrub_seek(Viewer::ViewerImpl *pViewerImpl, GtkWidget *widget, gdouble x, gboolean final);

#if VIDEO_ZOOM_SMOOTH_ANIMATION
static gboolean video_zoom_timeout(gpointer data);
#endif
static void video_zoom_get_pointer(Viewer::ViewerImpl *p, gdouble *px, gdouble *py);
static void video_zoom_resize_cb(GtkWidget *widget, GdkRectangle *allocation, gpointer user_data);
static void video_zoom_raise_media_windows(Viewer::ViewerImpl *p);
static void video_zoom_sink_map_cb(GtkWidget *widget, gpointer user_data);

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

#ifdef QUIVER_MAEMO
#define ACTION_VIEWER_ZOOM_IN_MAEMO     ACTION_VIEWER_ZOOM_IN"_MAEMO"
#define ACTION_VIEWER_ZOOM_OUT_MAEMO    ACTION_VIEWER_ZOOM_OUT"_MAEMO"
#endif






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



struct AsyncPixbufData {
    QuiverImageView *pImageView;
    GdkPixbuf *pixbuf;
    gint width, height;
    gboolean bReset;
    bool bAtSize;
};

static gboolean idle_set_pixbuf_v(gpointer data) {
    AsyncPixbufData *p = (AsyncPixbufData*)data;
    if (p->bAtSize) {
        quiver_image_view_set_pixbuf_at_size_ex(p->pImageView, p->pixbuf, p->width, p->height, p->bReset);
    } else {
        quiver_image_view_set_pixbuf(p->pImageView, p->pixbuf);
    }
    if (p->pixbuf) g_object_unref(p->pixbuf);
    delete p;
    return FALSE;
}

class ViewerImageViewPixbufLoaderObserver : public IPixbufLoaderObserver
{
public:
	ViewerImageViewPixbufLoaderObserver(QuiverImageView *imageview){m_pImageView = imageview;};
	virtual ~ViewerImageViewPixbufLoaderObserver(){};

	virtual void ConnectSignals(GdkPixbufLoader *loader){
		quiver_image_view_connect_pixbuf_loader_signals(m_pImageView,loader);
		};
	virtual void ConnectSignalSizePrepared(GdkPixbufLoader * loader){
		quiver_image_view_connect_pixbuf_size_prepared_signal(m_pImageView,loader);
		};

	// custom calls
	virtual void SetPixbuf(GdkPixbuf * pixbuf){
		if (ThreadUtil::IsGUIThread()) {
			quiver_image_view_set_pixbuf(m_pImageView,pixbuf);
		} else {
			if (pixbuf) g_object_ref(pixbuf);
			AsyncPixbufData *data = new AsyncPixbufData{m_pImageView, pixbuf, 0, 0, FALSE, false};
			g_idle_add_full(G_PRIORITY_HIGH, idle_set_pixbuf_v, data, NULL);
		}
	};
	virtual void SetPixbufAtSize(GdkPixbuf *pixbuf, gint width, gint height, bool bResetViewMode = true ){
		gboolean bReset = bResetViewMode ? TRUE : FALSE;
		if (ThreadUtil::IsGUIThread()) {
			quiver_image_view_set_pixbuf_at_size_ex(m_pImageView,pixbuf,width,height,bReset);
		} else {
			if (pixbuf) g_object_ref(pixbuf);
			AsyncPixbufData *data = new AsyncPixbufData{m_pImageView, pixbuf, width, height, bReset, true};
			g_idle_add_full(G_PRIORITY_HIGH, idle_set_pixbuf_v, data, NULL);
		}
	};
	
	virtual void SignalBytesRead(long bytes_read,long total){ (void)total;  (void)bytes_read; };
private:
	QuiverImageView *m_pImageView;
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
	void SetImageList(ImageListPtr imgList);
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
		if (IsVideo() && m_bTimelineVisible)
			gtk_widget_show(m_pTimeline);
		else
			gtk_widget_hide(m_pTimeline);
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
			gtk_image_set_from_icon_name(GTK_IMAGE(m_pPlayImage), "media-playback-pause", GTK_ICON_SIZE_DIALOG);

			/* show time label */
			gtk_widget_show(m_pTimeLabel);

			/* show skip-back / skip-forward (they have no_show_all set) */
			if (m_pTransportBox)
			{
				GtkWidget *parent = gtk_widget_get_parent(m_pTransportBox);
				if (parent != NULL)
				{
					GList *sibs = gtk_container_get_children(GTK_CONTAINER(parent));
					for (GList *l = sibs; l != NULL; l = l->next)
					{
						GtkWidget *w = (GtkWidget *)l->data;
						if (gtk_widget_get_no_show_all(w))
						{
							gboolean ns = gtk_widget_get_no_show_all(w);
							gtk_widget_set_no_show_all(w, FALSE);
							gtk_widget_show(w);
							gtk_widget_set_no_show_all(w, ns);
						}
					}
					g_list_free(sibs);
				}
			}

			/* show right controls (speed, vol, fs, snap) */
			if (m_pRightControls)
			{
				gboolean ns = gtk_widget_get_no_show_all(m_pRightControls);
				gtk_widget_set_no_show_all(m_pRightControls, FALSE);
				gtk_widget_show_all(m_pRightControls);
				gtk_widget_set_no_show_all(m_pRightControls, ns);
			}

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
			gtk_image_set_from_icon_name(GTK_IMAGE(m_pPlayImage), "media-playback-start", GTK_ICON_SIZE_DIALOG);
		}
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

	// video zoom (in-pipeline crop, optionally HW-accelerated upscale)
	void SetVideoZoom(gdouble zoom);
	void ApplyVideoZoom();


// member variables

	GtkWidget *m_pIconView;
	GtkWidget *m_pImageView;

	GtkWidget * m_pGrid;
	GtkWidget * m_pOverlay;
	GtkWidget * m_pStack;
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
	GtkWidget* m_pTimeLabel;
	GtkWidget* m_pPlayProgress;
	GtkWidget* m_pPlayProgressEventBox;
	GtkWidget* m_pVolumeButton;

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
	ImageListPtr m_ImageListPtr;

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

	/* accumulated smooth-scroll delta for next/previous navigation
	 * (Wayland delivers smooth axis events, not one event per wheel notch) */
	gdouble m_dSmoothScrollAccum;

	int   m_iSlideShowDuration;
	int   m_iSlideShowWaitCount;
	bool  m_bSlideShowLoop;

	bool m_bMaximizeViewableArea;
	bool m_bMaximizeViewabe;

	bool m_bIsPlaying;
	bool m_bWasPlayingBeforeSeek;
	bool m_bTimelineSeeking;    // scrub drag in progress (gtk_grab_add, no GDK grab)
	gint64 m_iTimelineLastSeekTarget; // last target passed to the pipeline (throttle)

	ImageCache m_ThumbnailCache;

	double m_dPlaybackSpeed;
	GtkWidget* m_pSpeedButton;
	GtkWidget* m_pSpeedLabel;
	GtkWidget* m_pTransportBox;
	GtkWidget* m_pRightControls;

	// gstreamer elements for playing videos
	GstElement* m_pPipeline;
	GtkWidget*  m_pVideoSinkWidget; // Changed from GstElement* to GtkWidget*
	GtkWidget*  m_pVideoFixed;      // GtkLayout canvas (fills the viewer area, clips the video sink widget)
	// how the digital zoom crop+scale chain is implemented, chosen at build time
	// from the GPU acceleration actually available on the platform
	typedef enum
	{
		VIDEO_ZOOM_SOFTWARE = 0, // videocrop + videoscale (CPU)
		VIDEO_ZOOM_MEDIA_SDK,    // Intel Media SDK: videocrop + vapostproc
		VIDEO_ZOOM_VAAPI,        // Intel gstreamer-vaapi: native crop-* props
		VIDEO_ZOOM_NVIDIA        // NVIDIA: nvvidconv coordinate-based crop props
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
	gboolean    m_bVideoZoomToCursor; // zoom toward cursor position (set by scroll-wheel only)
	gdouble     m_dVideoPanStartRootX; // pan drag start point, root (screen) coords
	gdouble     m_dVideoPanStartRootY;
	gdouble     m_dVideoPanStartPX; // crop window left/top at drag start
	gdouble     m_dVideoPanStartPY;
	gdouble     m_dVideoScrollAccum; // accumulated smooth-scroll delta for video zoom
	gint        m_iVideoWidth;      // current video frame width (from caps probe)
	gint        m_iVideoHeight;     // current video frame height
	gint        m_iVideoFpsNum;
	gint        m_iVideoFpsDen;
	gint        m_iVideoSinkW;      // last set_size_request width  (skip if unchanged)
	gint        m_iVideoSinkH;      // last set_size_request height
	gint        m_iVideoSinkX;      // last layout_move x
	gint        m_iVideoSinkY;      // last layout_move y (from caps probe)

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

void Viewer::ViewerImpl::SetImageList(ImageListPtr imgList)
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
	
	{
		/* For videos the zoom factor is applied in the pipeline (from the fit
		 * level up to 8x of the actual size); for stills it comes from the
		 * image view's magnification. */
		gboolean bCanZoomIn, bCanZoomOut;
		if (IsVideo())
		{
			bCanZoomIn = (m_dVideoZoomFinal < 8.0);
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
#ifdef QUIVER_MAEMO
		action = QuiverUtils::GetAction(ACTION_VIEWER_ZOOM_IN_MAEMO);
		if (NULL != action)
			g_simple_action_set_enabled(G_SIMPLE_ACTION(action), bCanZoomIn);
#endif
		action = QuiverUtils::GetAction(ACTION_VIEWER_ZOOM_OUT);
		if (NULL != action)
			g_simple_action_set_enabled(G_SIMPLE_ACTION(action), bCanZoomOut);
#ifdef QUIVER_MAEMO
		action = QuiverUtils::GetAction(ACTION_VIEWER_ZOOM_OUT_MAEMO);
		if (NULL != action)
			g_simple_action_set_enabled(G_SIMPLE_ACTION(action), bCanZoomOut);
#endif
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

	gint sb_width = gtk_widget_get_allocated_width(pScrollbarV);
	gint sb_height = gtk_widget_get_allocated_height(pScrollbarH);
	
	GtkWidget * pNavigationBox = m_pNavigationBox;

// FIXME: remove
	//GtkTableChild * child = GetGtkTableChild(GTK_TABLE(m_pGrid),m_pImageView);
	
	gint area_w = gtk_widget_get_allocated_width(m_pGrid);
	gint area_h = gtk_widget_get_allocated_height(m_pGrid);

	PreferencesPtr prefsPtr = Preferences::GetInstance();
	bool bHideScrollbars = 	prefsPtr->GetBoolean(QUIVER_PREFS_VIEWER,QUIVER_PREFS_VIEWER_SCROLLBARS_HIDE);
	
	if (bHideScrollbars || QUIVER_IMAGE_VIEW_MODE_FILL_SCREEN == view_mode || IsVideo())
	{
		//FIXME: just don't show scrollbars because they jump around in an
		// infinite loop for certain situations in this mode
		// hide h hide v
		gtk_widget_hide (pScrollbarV); 	
		gtk_widget_hide (pScrollbarH);
		gtk_widget_hide (pNavigationBox);

	}
	else if ( (area_w < width && area_h < height) ||
		(area_w < width && area_h - sb_height < height) ||
		(area_w - sb_width < width && area_h < height) )
	{
		// show h show v
		gtk_widget_show (pScrollbarV); 	
		gtk_widget_show (pScrollbarH);
		gtk_widget_show (pNavigationBox);
	}
	else if (area_w < width)
	{
		// show h hide v
		gtk_widget_show (pNavigationBox);	
		gtk_widget_hide (pScrollbarV);
		gtk_widget_show (pScrollbarH);		
	}
	else if (area_h < height)
	{
		// hide h show v
		gtk_widget_show (pNavigationBox);	
		gtk_widget_hide (pScrollbarH);
		gtk_widget_show (pScrollbarV);	
	}
	else
	{
		// hide h hide v
		gtk_widget_hide (pScrollbarV); 	
		gtk_widget_hide (pScrollbarH);
		gtk_widget_hide (pNavigationBox);
	}

	m_iTimeoutScrollbars = 0;


	GtkAdjustment *h = m_pAdjustmentH;
	GtkAdjustment *v = m_pAdjustmentV;

	if (NULL == h || NULL == v)
		return;
	

	if (gtk_adjustment_get_page_size(h) >= gtk_adjustment_get_upper(h) &&
		gtk_adjustment_get_page_size(v) >= gtk_adjustment_get_upper(v))
	{
		// enable drag n drop
		gtk_drag_source_set (m_pImageView, (GdkModifierType)(GDK_BUTTON1_MASK | GDK_BUTTON3_MASK),
				   quiver_drag_target_table, G_N_ELEMENTS(quiver_drag_target_table), (GdkDragAction)( GDK_ACTION_COPY |
			       GDK_ACTION_MOVE |
		           GDK_ACTION_LINK |
		           GDK_ACTION_ASK ));

	}
	else
	{
		// disable drag n drop
		gtk_widget_grab_focus(m_pImageView);
		gtk_drag_source_unset (m_pImageView);
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
		width = gtk_widget_get_allocated_width(m_pImageView);
		height = gtk_widget_get_allocated_height(m_pImageView);
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
		width = gtk_widget_get_allocated_width(m_pImageView);
		height = gtk_widget_get_allocated_height(m_pImageView);
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
			QuiverFile f = (*m_ImageListPtr)[0];
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

		g_signal_handlers_block_by_func(m_pIconView,(gpointer)viewer_iconview_cursor_changed,this);

		// a new current item: show only the play button; the timeline appears
		// once play is pressed
		m_bTimelineVisible = false;
		UpdateTimelineVisibility();

		if (IsVideo())
		{
			// show media controls but only the play button
			gtk_widget_show(m_pMediaControls);
			gtk_widget_hide(m_pTimeLabel);
			gtk_widget_hide(m_pRightControls);
			// hide skip-back/skip-forward (they have no_show_all)
			if (m_pTransportBox)
			{
				GtkWidget *parent = gtk_widget_get_parent(m_pTransportBox);
				if (parent != NULL)
				{
					GList *sibs = gtk_container_get_children(GTK_CONTAINER(parent));
					for (GList *l = sibs; l != NULL; l = l->next)
					{
						GtkWidget *w = (GtkWidget *)l->data;
						if (gtk_widget_get_no_show_all(w))
							gtk_widget_hide(w);
					}
					g_list_free(sibs);
				}
			}
		}
		else
		{
			// hide them
			gtk_widget_hide(m_pMediaControls);
		}

		gtk_window_resize (GTK_WINDOW (m_pNavigationWindow),1,1);
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
		width = gtk_widget_get_allocated_width(m_pImageView);
		height = gtk_widget_get_allocated_height(m_pImageView);

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
		ExifData* pExifData = f.GetExifData();
		
		if (NULL != pExifData)
		{
			ExifEntry *pExifEntry;
		
			// If the entry doesn't exist, create it. /
			pExifEntry = exif_content_get_entry (pExifData->ifd[EXIF_IFD_0], EXIF_TAG_ORIENTATION);
			if (!pExifEntry) 
			{
				pExifEntry = exif_entry_new ();
				exif_content_add_entry (pExifData->ifd[EXIF_IFD_0], pExifEntry);
				exif_entry_initialize (pExifEntry, EXIF_TAG_ORIENTATION);
			}
		
			// Now set the value and save the data. /
			exif_set_short (pExifEntry->data , exif_data_get_byte_order (pExifData), m_iCurrentOrientation);
			
			f.SetExifData(pExifData);
			
			exif_data_unref(pExifData);
		}
		
	}
	m_ImageLoader.SetLoadOrientation(GetCurrentOrientation(true));
}

void Viewer::ViewerImpl::AddFilmstrip()
{
	PreferencesPtr prefsPtr = Preferences::GetInstance();
	int iFilmstripPos = prefsPtr->GetInteger(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_FILMSTRIP_POSITION, FSTRIP_POS_LEFT);

	GtkBox* box = NULL;
	
	GtkWidget *current_parent = gtk_widget_get_parent(m_pIconView);
	if (current_parent != NULL) {
		g_object_ref(m_pIconView);
		gtk_container_remove(GTK_CONTAINER(current_parent), m_pIconView);
	}

	switch (iFilmstripPos)
	{
		case FSTRIP_POS_TOP:
		case FSTRIP_POS_BOTTOM:
			box = GTK_BOX(m_pVBox);
			quiver_icon_view_set_n_rows(QUIVER_ICON_VIEW(m_pIconView),1);
			gtk_box_pack_start (box, m_pIconView, FALSE, TRUE, 0);
			
			break;
		case FSTRIP_POS_LEFT:
		case FSTRIP_POS_RIGHT:
			box = GTK_BOX(m_pHBox);
			quiver_icon_view_set_n_columns(QUIVER_ICON_VIEW(m_pIconView),1);
			gtk_box_pack_start (box, m_pIconView, FALSE, TRUE, 0);
			break;		
	}

	if (NULL != box)
	{
		switch (iFilmstripPos)
		{
			case FSTRIP_POS_TOP:
			case FSTRIP_POS_LEFT:
				gtk_box_reorder_child (box, m_pIconView,0);
				break;
			case FSTRIP_POS_BOTTOM:
			case FSTRIP_POS_RIGHT:
				gtk_box_reorder_child (box, m_pIconView,-1);
				break;		
		}
	}

	// hand our reference over to the new parent container
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

	// keep the controls on screen while the pointer is over them
	gint x = -1, y = -1;
	GdkWindow* media_window = gtk_widget_get_window(pViewerImpl->m_pMediaControls);
	if (NULL != media_window)
	{
		GdkDisplay* display = gdk_window_get_display(media_window);
		GdkDevice* device = gdk_seat_get_pointer(gdk_display_get_default_seat(display));
		gdk_window_get_device_position(media_window, device, &x, &y, NULL);
	}
	GtkAllocation a = {};
	gtk_widget_get_allocation(pViewerImpl->m_pMediaControls, &a);
	if (0 <= x && 0 <= y && x <= a.width && y <= a.height)
		bKeepVisible = TRUE;

	// ...and while the volume popup is open (it floats above the bar)
	if (!bKeepVisible)
	{
		GtkWidget* popup = gtk_scale_button_get_popup(GTK_SCALE_BUTTON(pViewerImpl->m_pVolumeButton));
		if (popup != NULL && gtk_widget_get_visible(popup))
			bKeepVisible = TRUE;
	}

	// ...and while the speed popover is open
	if (!bKeepVisible && pViewerImpl->m_pSpeedButton != NULL)
	{
		GtkPopover* speedPop = (GtkPopover*)g_object_get_data(G_OBJECT(pViewerImpl->m_pSpeedButton), "speed-popover");
		if (speedPop != NULL && gtk_widget_get_visible(GTK_WIDGET(speedPop)))
			bKeepVisible = TRUE;
	}

	if (pViewerImpl->IsPlaying())
	{
		if (bKeepVisible)
			pViewerImpl->m_iTimeoutMouseMotionNotify = g_timeout_add(1500,timeout_event_motion_notify,pViewerImpl);
		else
		{
			gtk_widget_hide(pViewerImpl->m_pMediaControls);
			pViewerImpl->m_iTimeoutMouseMotionNotify = 0;
		}
	}
	else
		pViewerImpl->m_iTimeoutMouseMotionNotify = 0;

	return FALSE;
}


static void
viewer_scrub_seek(Viewer::ViewerImpl *pViewerImpl, GtkWidget *widget, gdouble x, gboolean final)
{
	GtkAllocation allocation = {};
	gtk_widget_get_allocation(widget, &allocation);
	if (allocation.width <= 0)
		return;

	GstFormat format = GST_FORMAT_TIME;
	gint64 clip_duration = 0;
	if (!gst_element_query_duration(GST_ELEMENT(pViewerImpl->m_pPipeline), format, &clip_duration) || clip_duration <= 0)
		return;

	gint64 target = (gint64)((clip_duration * x) / allocation.width);
	if (target < 0)
		target = 0;
	if (target > clip_duration)
		target = clip_duration;

	/* immediate visual feedback: track the pointer instead of waiting for
	 * the pipeline to report the new position */
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(pViewerImpl->m_pPlayProgress),
		(gdouble)target / clip_duration);

	gchar* str_pos = gst_time_format(target);
	gchar* str_len = gst_time_format(clip_duration);
	gchar* text = g_strdup_printf("%s / %s", str_pos, str_len);
	gtk_label_set_text(GTK_LABEL(pViewerImpl->m_pTimeLabel), text);
	g_free(text);
	g_free(str_len);
	g_free(str_pos);

	/* issue a FLUSH seek (safe on a paused/windowed-sink pipeline) but only
	 * when the target moved a meaningful amount or this is the final seek;
	 * seeking on every motion event both flooded the pipeline (bar lagged)
	 * and, without FLUSH, deadlocked the main loop against the sink */
	if (final || pViewerImpl->m_iTimelineLastSeekTarget < 0 ||
		ABS(target - pViewerImpl->m_iTimelineLastSeekTarget) > clip_duration / 100)
	{
		pViewerImpl->m_iTimelineLastSeekTarget = target;
		gst_element_seek_simple(GST_ELEMENT(pViewerImpl->m_pPipeline), format,
			GstSeekFlags(GST_SEEK_FLAG_FLUSH), target);
	}
}

static gboolean
viewer_motion_notify(GtkWidget* widget, GdkEventMotion* event, gpointer user_data)
{
	Viewer::ViewerImpl *pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)user_data;
	if ((widget == pViewerImpl->m_pVideoFixed || widget == pViewerImpl->m_pVideoSinkWidget)
		&& pViewerImpl->m_bVideoPanning)
	{
		/* drag-to-pan: move the viewport so the video follows the pointer.
		 * The delta uses root coords because the widget slides against the
		 * window while it overflows, so widget-relative motion is unreliable.
		 * 1 screen px is (crop width)/(widget width) source px - or, since the
		 * display keeps the frame's aspect, 1/(fit-to-area scale * zoom). */
		gdouble srcPerPxX = 1., srcPerPxY = 1.;
		if (pViewerImpl->m_iVideoWidth > 0 && pViewerImpl->m_iVideoHeight > 0)
		{
			GtkAllocation allocation = {};
			gtk_widget_get_allocation(pViewerImpl->m_pVideoFixed, &allocation);
			gdouble scale = MIN((gdouble)allocation.width / pViewerImpl->m_iVideoWidth,
				(gdouble)allocation.height / pViewerImpl->m_iVideoHeight);
			gdouble zoom = MAX(pViewerImpl->m_dVideoZoom, 1.0);
			if (scale * zoom > 0.)
			{
				srcPerPxX = 1. / (scale * zoom);
				srcPerPxY = srcPerPxX;
			}
		}
		pViewerImpl->m_dVideoPanX = pViewerImpl->m_dVideoPanStartPX - (event->x_root - pViewerImpl->m_dVideoPanStartRootX) * srcPerPxX;
		pViewerImpl->m_dVideoPanY = pViewerImpl->m_dVideoPanStartPY - (event->y_root - pViewerImpl->m_dVideoPanStartRootY) * srcPerPxY;
		pViewerImpl->ApplyVideoZoom();
		pViewerImpl->RefreshAutoHideTimer();
		return TRUE;
	}

	if (widget == pViewerImpl->m_pImageView || widget == pViewerImpl->m_pVideoFixed
		|| widget == pViewerImpl->m_pVideoSinkWidget)
	{
		if (0 != pViewerImpl->m_iTimeoutMouseMotionNotify)
		{
			g_source_remove(pViewerImpl->m_iTimeoutMouseMotionNotify);
			pViewerImpl->m_iTimeoutMouseMotionNotify = 0;
		}

		if (0 != pViewerImpl->m_ImageListPtr->GetSize() && 
				pViewerImpl->m_ImageListPtr->GetCurrent().IsVideo())
		{
			gtk_widget_show(pViewerImpl->m_pMediaControls);
			pViewerImpl->UpdateTimelineVisibility();
		}

		if (pViewerImpl->IsPlaying())
			pViewerImpl->m_iTimeoutMouseMotionNotify = g_timeout_add(1500,timeout_event_motion_notify,pViewerImpl);
	}
	else if (widget == pViewerImpl->m_pPlayProgressEventBox)
	{
		if (pViewerImpl->m_bTimelineSeeking)
			viewer_scrub_seek(pViewerImpl, widget, event->x, FALSE);
	}
		
	return FALSE;
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
#ifdef QUIVER_MAEMO
	        || 0 == strcmp(szAction, ACTION_VIEWER_ZOOM_IN_MAEMO)
#endif
	)
	{
		if (pViewerImpl->IsVideo())
		{
			pViewerImpl->SetVideoZoom(pViewerImpl->m_dVideoZoom * 1.25);
			return;
		}

		if (QUIVER_IMAGE_VIEW_MODE_ZOOM != quiver_image_view_get_view_mode(imageview))
			quiver_image_view_set_view_mode(imageview,QUIVER_IMAGE_VIEW_MODE_ZOOM);
				
		quiver_image_view_set_magnification(imageview,
						quiver_image_view_get_magnification(imageview)*1.25);
	}
	else if (0 == strcmp(szAction, ACTION_VIEWER_ZOOM_OUT)
#ifdef QUIVER_MAEMO
	        || 0 == strcmp(szAction, ACTION_VIEWER_ZOOM_OUT_MAEMO)
#endif
	)
	{
		if (pViewerImpl->IsVideo())
		{
			pViewerImpl->SetVideoZoom(pViewerImpl->m_dVideoZoom / 1.25);
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
			gtk_widget_show(pViewerImpl->m_pIconView);
		}
		else
		{
			gtk_widget_hide(pViewerImpl->m_pIconView);
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
#ifdef QUIVER_MAEMO
		strDlgText = "Delete the selected image?";
#else
		strDlgText = "Move the selected image to the trash?";
#endif
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
#ifdef QUIVER_MAEMO
				if (QuiverFileOps::Delete(f))
#else
				if (QuiverFileOps::MoveToTrash(f))
#endif
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
				cout << "not trashing file : " << endl;//pViewerImpl->m_ImageListPtr->GetCurrent().GetURI() << endl;
				break;
		}
	}
	else if (0 == strcmp(szAction, ACTION_VIEWER_COPY))
	{
		GtkClipboard* clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
		
		string strClipText;
		if (pViewerImpl->m_ImageListPtr->GetSize())
		{
			string strPath = pViewerImpl->m_ImageListPtr->GetCurrent().GetFilePath();
			gtk_clipboard_set_text (clipboard, strPath.c_str(), strPath.length());
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
				params.max_width = gtk_widget_get_allocated_width(pViewerImpl->m_pImageView);
				params.max_height = gtk_widget_get_allocated_height(pViewerImpl->m_pImageView);
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

static gboolean viewer_scrollwheel_event(GtkWidget *widget, GdkEventScroll *event, gpointer data )
{ (void)widget; 

	Viewer::ViewerImpl *pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)data;

	/* Videos render in their own sink widget: Ctrl/Shift+wheel zooms the
	 * video by cropping in the pipeline, so the image view never zooms in on
	 * the poster/preview frame underneath it.  A plain wheel over a video
	 * still navigates to the next/previous file. */
	if (pViewerImpl->IsVideo() &&
		(event->state & GDK_CONTROL_MASK || event->state & GDK_SHIFT_MASK))
	{
		const gdouble zoom_step = 1.25;
		pViewerImpl->m_bVideoZoomToCursor = TRUE;

		switch (event->direction)
		{
			case GDK_SCROLL_UP:
			case GDK_SCROLL_LEFT:
				pViewerImpl->m_dVideoScrollAccum = 0.;
				pViewerImpl->SetVideoZoom(pViewerImpl->m_dVideoZoom * zoom_step);
				break;
			case GDK_SCROLL_DOWN:
			case GDK_SCROLL_RIGHT:
				pViewerImpl->m_dVideoScrollAccum = 0.;
				pViewerImpl->SetVideoZoom(pViewerImpl->m_dVideoZoom / zoom_step);
				break;
			case GDK_SCROLL_SMOOTH:
			{
				/* Wayland / touchpads deliver smooth axis events, one per
				 * frame, not one event per wheel notch.  Accumulate the deltas
				 * and only zoom once a full notch (+/- 1.0) has been scrolled
				 * so a wheel still zooms one step at a time. */
				gdouble delta = event->delta_y;
				if (0. == delta)
					delta = event->delta_x;

				pViewerImpl->m_dVideoScrollAccum += delta;
				if (pViewerImpl->m_dVideoScrollAccum >= 1.0)
				{
					/* positive delta scrolls down, which zooms out, matching
					 * the image view's smooth-scroll zoom direction */
					pViewerImpl->m_dVideoScrollAccum = 0.;
					pViewerImpl->SetVideoZoom(pViewerImpl->m_dVideoZoom / zoom_step);
				}
				else if (pViewerImpl->m_dVideoScrollAccum <= -1.0)
				{
					pViewerImpl->m_dVideoScrollAccum = 0.;
					pViewerImpl->SetVideoZoom(pViewerImpl->m_dVideoZoom * zoom_step);
				}
				break;
			}
			default:
				break;
		}
		return TRUE;
	}

	if (event->state & GDK_CONTROL_MASK || event->state & GDK_SHIFT_MASK)
	{
		return FALSE;
	}

	gboolean bPrevious = FALSE;
	gboolean bNext = FALSE;

	switch (event->direction)
	{
		case GDK_SCROLL_UP:
		case GDK_SCROLL_LEFT:
			pViewerImpl->m_dSmoothScrollAccum = 0.;
			bPrevious = TRUE;
			break;
		case GDK_SCROLL_DOWN:
		case GDK_SCROLL_RIGHT:
			pViewerImpl->m_dSmoothScrollAccum = 0.;
			bNext = TRUE;
			break;
		case GDK_SCROLL_SMOOTH:
		{
			/* Wayland / touchpads deliver smooth axis events, one per frame,
			 * not one event per wheel notch.  Accumulate the deltas and only
			 * navigate once a full notch (+/- 1.0) has been scrolled so that
			 * a wheel still moves exactly one file at a time. */
			gdouble delta = event->delta_y;
			if (0. == delta)
				delta = event->delta_x;

			pViewerImpl->m_dSmoothScrollAccum += delta;
			if (pViewerImpl->m_dSmoothScrollAccum >= 1.0)
			{
				pViewerImpl->m_dSmoothScrollAccum -= 1.0;
				bNext = TRUE;
			}
			else if (pViewerImpl->m_dSmoothScrollAccum <= -1.0)
			{
				pViewerImpl->m_dSmoothScrollAccum += 1.0;
				bPrevious = TRUE;
			}
			break;
		}
		default:
			break;
	}

	if (bPrevious)
	{
		pViewerImpl->m_ImageListPtr->Previous();
	}
	else if (bNext)
	{
		pViewerImpl->m_ImageListPtr->Next();
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

static gboolean viewer_imageview_key_press_event(GtkWidget *imageview, GdkEventKey *event, gpointer userdata)
{ (void)imageview; 
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

	if (GDK_KEY_Left == event->keyval || GDK_KEY_Up == event->keyval)
	{
		if (bPanMode)
		{
			if (GDK_KEY_Left == event->keyval)
			{
				adjustment = h;
			}
			else
			{
				adjustment = v;
			}
			increment = -gtk_adjustment_get_step_increment(adjustment);
			
		}
		//else if (pViewerImpl->IsPlaying())
		//{
			// skip back
			//GAction* action = QuiverUtils::GetAction( ACTION_VIEWER_VIDEO_SKIP_BACK);
			//gtk_action_activate(action);
		//}
		else
		{
			GAction* action = QuiverUtils::GetAction( ACTION_VIEWER_PREVIOUS);
			g_action_activate(action,NULL);
		}
		rval = TRUE;
	}
	else if (GDK_KEY_Right == event->keyval || GDK_KEY_Down == event->keyval)
	{
		if (bPanMode)
		{
			if (GDK_KEY_Right == event->keyval)
			{
				adjustment = h;
			}
			else
			{
				adjustment = v;
			}
			increment = gtk_adjustment_get_step_increment(adjustment);
		}
		//else if (pViewerImpl->IsPlaying())
		//{
			// skip forward
			//GAction* action = QuiverUtils::GetAction( ACTION_VIEWER_VIDEO_SKIP_FORWARD);
			//gtk_action_activate(action);
		//}
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

static void
viewer_volume_value_changed (GtkScaleButton *button, gdouble value, gpointer user_data)
{ (void)value; 
	Viewer::ViewerImpl *pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)user_data;
	g_object_set(G_OBJECT(pViewerImpl->m_pPipeline), "volume", gtk_scale_button_get_value(button), NULL);
	//g_object_get(G_OBJECT(m_pPipeline), "current-uri", &uri, NULL);
}


static gboolean
viewer_navigation_button_press_event(GtkWidget *widget, GdkEventButton *event, gpointer userdata)
{ (void)widget; 
	Viewer::ViewerImpl *pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)userdata;
	
	GdkModifierType state;
 (void)state;
	gint x =0, y=0;

	x = (gint)event->x_root;
	y = (gint)event->y_root;
	state = (GdkModifierType)event->state;
	

	gtk_widget_show_all (pViewerImpl->m_pNavigationWindow);

	gint w,h;
	gtk_window_get_size(GTK_WINDOW (pViewerImpl->m_pNavigationWindow),&w,&h);
  	int ww, wh, pos_x,pos_y;
  	ww = w+2;
  	wh = h+2;
  	
  	pos_x = (int)x - ww/2;
  	pos_y = (int)y - wh/2;
  		
	
  	GdkDisplay *display = gdk_display_get_default();
    GdkMonitor *monitor = gdk_display_get_primary_monitor(display);
    GdkRectangle workarea;
    gdk_monitor_get_workarea(monitor, &workarea);
    if (workarea.width < pos_x + ww)
        pos_x = workarea.width - ww;
    else if (0 > pos_x)
        pos_x = 0;
    if (workarea.height < pos_y + wh)
        pos_y = workarea.height - wh;
  	else if (0 > pos_y)
  		pos_y = 0;
	
	gtk_window_move (GTK_WINDOW (pViewerImpl->m_pNavigationWindow), pos_x, pos_y);

	GdkCursor *cursor;	
	GdkDisplay* nav_display = gdk_window_get_display(gtk_widget_get_window(pViewerImpl->m_pNavigationWindow));
	cursor = gdk_cursor_new_for_display (nav_display, GDK_FLEUR); 
		
	gdk_seat_grab(gdk_display_get_default_seat(nav_display),
			  gtk_widget_get_window(pViewerImpl->m_pNavigationWindow), 
			  GDK_SEAT_CAPABILITY_ALL, TRUE,
			  cursor,
			  NULL, NULL, NULL);

	g_object_unref (cursor);
	return TRUE;
}

gboolean navigation_control_button_release_event (GtkWidget *widget, GdkEventButton *event, gpointer data )
{
	(void)event;
	Viewer::ViewerImpl *pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)data;

	GdkDisplay* nav_display = gtk_widget_get_display(widget);
	gdk_seat_ungrab(gdk_display_get_default_seat(nav_display));

	gtk_widget_hide (pViewerImpl->m_pNavigationWindow);
	return TRUE;	
}

static void signal_drag_data_get  (GtkWidget *widget, GdkDragContext *context, GtkSelectionData *selection_data, guint info, guint time,gpointer user_data)
{ (void)time;  (void)context;  (void)widget; 
	Viewer::ViewerImpl *pViewerImpl = (Viewer::ViewerImpl*)user_data;
	
	
	
	if (info == QUIVER_TARGET_STRING)
    {
		if (pViewerImpl->m_ImageListPtr->GetSize())
		{
    		gtk_selection_data_set (selection_data,
			    gtk_selection_data_get_target(selection_data),
			    8, (const guchar*)pViewerImpl->m_ImageListPtr->GetCurrent().GetURI(),strlen(pViewerImpl->m_ImageListPtr->GetCurrent().GetURI()));
		}
	}
	else if (info == QUIVER_TARGET_URI)
	{
		if (pViewerImpl->m_ImageListPtr->GetSize())
		{
			//selection data set
			//context->suggested_action = GDK_ACTION_LINK;
    		gtk_selection_data_set (selection_data,
			    gtk_selection_data_get_target(selection_data),
			    8, (const guchar*)pViewerImpl->m_ImageListPtr->GetCurrent().GetURI(),strlen(pViewerImpl->m_ImageListPtr->GetCurrent().GetURI()));
		}
	}
  	else
	{
		gtk_selection_data_set (selection_data,
			    gtk_selection_data_get_target(selection_data),
				8, (const guchar*)"I'm Data!", 9);
	}
}

static void signal_drag_data_delete  (GtkWidget *widget,GdkDragContext *context,gpointer user_data)
{ (void)context;  (void)widget; 
	Viewer::ViewerImpl *pViewerImpl;
 (void)pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)user_data;
}

static void signal_drag_data_received(GtkWidget *widget,GdkDragContext *drag_context, gint x,gint y, GtkSelectionData *data, guint info, guint time,gpointer user_data)
{ (void)time;  (void)info;  (void)data;  (void)y;  (void)x;  (void)drag_context;  (void)widget; 
	Viewer::ViewerImpl *pViewerImpl;
 (void)pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)user_data;
}

static void signal_drag_begin (GtkWidget *widget,GdkDragContext *drag_context,gpointer user_data)
{ (void)widget; 
	Viewer::ViewerImpl *pViewerImpl = (Viewer::ViewerImpl*)user_data;
	
	// disable drop 
	//gtk_drag_dest_unset(pViewerImpl->m_pImageView);
	
	// TODO
	// set icon
	GdkPixbuf *thumb = pViewerImpl->m_ImageListPtr->GetCurrent().GetThumbnail(128);

	if (NULL != thumb)
	{
		gtk_drag_set_icon_pixbuf(drag_context,thumb,-2,-2);
		g_object_unref(thumb);
	}
}

static void signal_drag_end(GtkWidget *widget,GdkDragContext *drag_context,gpointer user_data)
{ (void)drag_context;  (void)widget; 
	Viewer::ViewerImpl *pViewerImpl;
 (void)pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)user_data;
}

static void signal_drag_motion (GtkWidget *widget, GdkDragContext *context, gint x, gint y, guint time, gpointer user_data)
{ (void)time;  (void)y;  (void)x;  (void)context;  (void)widget; 
	Viewer::ViewerImpl *pViewerImpl;
 (void)pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)user_data;
}


static gboolean signal_drag_drop (GtkWidget *widget, GdkDragContext *drag_context, gint x, gint y, guint time,  gpointer user_data)
{ (void)time;  (void)y;  (void)x;  (void)drag_context;  (void)widget; 
	Viewer::ViewerImpl *pViewerImpl;
 (void)pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)user_data;
	return TRUE;

}

static void viewer_show_context_menu(GdkEventButton *event, gpointer userdata);
static gboolean viewer_popup_menu_cb (GtkWidget *widget, gpointer userdata)
{ (void)widget; 
	viewer_show_context_menu(NULL, userdata);
	return TRUE; 
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
				if (pViewerImpl->m_pVideoFixed != NULL)
					gtk_widget_show(pViewerImpl->m_pVideoFixed);
				gtk_stack_set_visible_child_name(GTK_STACK(pViewerImpl->m_pStack), "video");
				if (pViewerImpl->m_pVideoSinkWidget != NULL && gtk_widget_get_visible(pViewerImpl->m_pVideoFixed))
					gtk_widget_set_opacity(pViewerImpl->m_pVideoSinkWidget, 1.0);
				pViewerImpl->UpdateTimeline();
				break;
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

	/* during a scrub the label and progress bar track the pointer directly;
	 * don't let the pipeline's ASYNC_DONE reports snap them back */
	if (m_bTimelineSeeking)
		return;

	gchar* str_pos = gst_time_format(pos);
	gchar* str_len = gst_time_format(len);
	gchar* text = g_strdup_printf("%s / %s", str_pos, str_len);

	gtk_label_set_text(GTK_LABEL(m_pTimeLabel), text);

	g_free(text);
	g_free(str_len);
	g_free(str_pos);

	if (pos > len)
		pos = len;

	gdouble progress = 0.;
	if (0 != len)
		progress = gdouble(pos)/len;

	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(m_pPlayProgress), progress);
}

void Viewer::ViewerImpl::PlayPauseVideo()
{
	g_print("[VP] PlayPauseVideo ENTER\n");
	if (!IsVideo())
	{
		g_print("[VP]   not video, bail\n");
		return;
	}
	gchar* uri = NULL;
	g_object_get(G_OBJECT(m_pPipeline), "current-uri", &uri, NULL);
	gboolean same = (0 == g_strcmp0(uri, m_ImageListPtr->GetCurrent().GetURI()));
	g_print("[VP]   same=%d vW=%d vH=%d uri=%s\n", same, m_iVideoWidth, m_iVideoHeight, uri ? uri : "(null)");
	if (same)
	{
		//gtk_widget_set_double_buffered (m_pImageView, FALSE); // Double buffering handled by gtksink
		if (m_pVideoFixed != NULL)
			gtk_widget_show(m_pVideoFixed);
		gtk_stack_set_visible_child_name(GTK_STACK(m_pStack), "video");
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
		g_print("[VP] same: stack=video fixed_show=%d sink_mapped=%d sink_opa=%.1f\n",
			m_pVideoFixed ? gtk_widget_get_visible(m_pVideoFixed) : -1,
			m_pVideoSinkWidget ? gtk_widget_get_mapped(m_pVideoSinkWidget) : -1,
			m_pVideoSinkWidget ? gtk_widget_get_opacity(m_pVideoSinkWidget) : -1);
		GstState current;
		// has the right video
		GstStateChangeReturn rval = gst_element_get_state(GST_ELEMENT(m_pPipeline), &current, NULL, GST_SECOND);
		if (GST_STATE_CHANGE_SUCCESS == rval)
		{
			if (GST_STATE_PLAYING == current)
			{
				g_print("[VP]   -> PAUSE\n");
				gst_element_set_state(GST_ELEMENT(m_pPipeline), GST_STATE_PAUSED);
				gtk_widget_show(m_pMediaControls);
				UpdateTimelineVisibility();
				SetIsPlaying(false);
			}
			else
			{
				g_print("[VP]   -> PLAY (gst_state=%d)\n", current);
				SetIsPlaying(true);
				gst_element_set_state(GST_ELEMENT(m_pPipeline), GST_STATE_PLAYING);

				if (0 != m_iTimeoutMouseMotionNotify)
				{
					g_source_remove(m_iTimeoutMouseMotionNotify);
					m_iTimeoutMouseMotionNotify = 0;
				}

				m_iTimeoutMouseMotionNotify = g_timeout_add(1500,timeout_event_motion_notify,this);
			}
			g_print("[VP] same: after state change fixed_show=%d sink_mapped=%d sink_opa=%.1f\n",
				m_pVideoFixed ? gtk_widget_get_visible(m_pVideoFixed) : -1,
				m_pVideoSinkWidget ? gtk_widget_get_mapped(m_pVideoSinkWidget) : -1,
				m_pVideoSinkWidget ? gtk_widget_get_opacity(m_pVideoSinkWidget) : -1);
		}
	}
	else
	{
		g_print("[VP]   -> NEW URI: %s\n", m_ImageListPtr->GetCurrent().GetURI());
		StopVideo(false);
		if (m_pVideoSinkWidget != NULL)
		{
			/* hide until the caps probe applies the correct zoom for the
			 * new video; stale m_iVideoWidth from a different video would
			 * compute the wrong zoom anyway */
			gtk_widget_set_opacity(m_pVideoSinkWidget, 0.0);
		}
		g_print("[VP] new: stack=video fixed_show=%d sink_mapped=%d sink_opa=%.1f sink_show=%d\n",
			m_pVideoFixed ? gtk_widget_get_visible(m_pVideoFixed) : -1,
			m_pVideoSinkWidget ? gtk_widget_get_mapped(m_pVideoSinkWidget) : -1,
			m_pVideoSinkWidget ? gtk_widget_get_opacity(m_pVideoSinkWidget) : -1,
			m_pVideoSinkWidget ? gtk_widget_get_visible(m_pVideoSinkWidget) : -1);
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
	g_print("[VP] PlayPauseVideo EXIT\n");
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
			gtk_widget_show(m_pMediaControls);
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
			gtk_widget_show(m_pMediaControls);
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
	g_print("[VP] StopVideo ENTER vW=%d vH=%d sink_mapped=%d sink_show=%d fixed_show=%d\n",
		m_iVideoWidth, m_iVideoHeight,
		m_pVideoSinkWidget ? gtk_widget_get_mapped(m_pVideoSinkWidget) : -1,
		m_pVideoSinkWidget ? gtk_widget_get_visible(m_pVideoSinkWidget) : -1,
		m_pVideoFixed ? gtk_widget_get_visible(m_pVideoFixed) : -1);
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
		gtk_widget_hide(m_pVideoSinkWidget);
	}
	if (m_pVideoFixed != NULL)
		gtk_widget_hide(m_pVideoFixed);
	//gtk_widget_set_double_buffered (m_pImageView, TRUE); // Double buffering handled by gtksink
	gtk_stack_set_visible_child_name(GTK_STACK(m_pStack), "image");
	g_print("[VP] StopVideo: stack->image sink_mapped=%d sink_show=%d fixed_show=%d\n",
		m_pVideoSinkWidget ? gtk_widget_get_mapped(m_pVideoSinkWidget) : -1,
		m_pVideoSinkWidget ? gtk_widget_get_visible(m_pVideoSinkWidget) : -1,
		m_pVideoFixed ? gtk_widget_get_visible(m_pVideoFixed) : -1);

	/* reset the digital zoom so the next video starts at fit, and drop the
	 * stale frame size so a different-sized video is scaled to its own
	 * dimensions until the caps probe reports them */
	m_dVideoZoom = 1.0;
	m_dVideoZoomFinal = 1.0;
	m_dVideoZoomMin = 1.0;
	m_dVideoScrollAccum = 0.;
	m_dPlaybackSpeed = 1.0;
	if (m_pSpeedLabel)
		gtk_label_set_text(GTK_LABEL(m_pSpeedLabel), "1x");
	m_iVideoWidth = 0;
	m_iVideoHeight = 0;
	m_iVideoFpsNum = 0;
	m_iVideoFpsDen = 1;
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
		gtk_widget_show(m_pMediaControls);
	}

	if (reloadImage && 0 != m_ImageListPtr->GetSize())
	{
		if (gtk_widget_get_mapped(m_pImageView))
			gdk_window_invalidate_rect(gtk_widget_get_window(m_pImageView), NULL, TRUE);
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
	gchar* label = g_strdup_printf("%.4g", speed);
	gtk_label_set_text(GTK_LABEL(m_pSpeedLabel), label);
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

#ifdef QUIVER_MAEMO
static gboolean timeout_click (gpointer data)
{
	Viewer::ViewerImpl *pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)data;

	pViewerImpl->m_pViewer->EmitItemClickedEvent();

	pViewerImpl->m_iTimeoutClickID  = 0;
	return FALSE;
}
#endif

static gboolean 
viewer_button_release_cb(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
	Viewer::ViewerImpl *pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)user_data;
	if (widget == pViewerImpl->m_pPlayProgressEventBox)
	{
		/* snap the pipeline to the exact release position */
		viewer_scrub_seek(pViewerImpl, widget, event->x, TRUE);

		if (pViewerImpl->m_bWasPlayingBeforeSeek)
		   pViewerImpl->PlayPauseVideo(); 

		pViewerImpl->m_iTimelineLastSeekTarget = -1;
		pViewerImpl->m_bTimelineSeeking = FALSE;
		gtk_grab_remove(widget);
		pViewerImpl->RefreshAutoHideTimer();
	}
	else if ((widget == pViewerImpl->m_pVideoFixed || widget == pViewerImpl->m_pVideoSinkWidget)
		&& pViewerImpl->m_bVideoPanning)
	{
		pViewerImpl->m_bVideoPanning = FALSE;
		GdkDisplay* pan_display = gtk_widget_get_display(widget);
		gdk_seat_ungrab(gdk_display_get_default_seat(pan_display));

		/* a click (no meaningful drag) toggles play/pause */
		if (ABS(event->x_root - pViewerImpl->m_dVideoPanStartRootX) < 5.
			&& ABS(event->y_root - pViewerImpl->m_dVideoPanStartRootY) < 5.)
		{
			if (pViewerImpl->IsVideo())
				pViewerImpl->PlayPauseVideo();
		}
		pViewerImpl->RefreshAutoHideTimer();
		return TRUE;
	}
	return FALSE;
}

static gboolean 
viewer_button_press_cb(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
	Viewer::ViewerImpl *pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)user_data;

#ifdef QUIVER_MAEMO
	/* FIXME - one click to fullscreen
	 * 2 problems:  too slow and 
	 * when zoomsed in the scroll dragging triggers it
	if (GDK_BUTTON_PRESS == event->type && 1 == event->button)
	{
		gint double_click_time = 0;
		GtkSettings* settings = gtk_settings_get_default();
		g_object_get(settings, "gtk-double-click-time",&double_click_time,NULL);
		printf("double click time! %d\n", double_click_time);
		double_click_time += 5; // add another 10ms to make sure
		
		if (0 == pViewerImpl->m_iTimeoutClickID )
		{
			pViewerImpl->m_iTimeoutClickID 
				= g_timeout_add(double_click_time, timeout_click, pViewerImpl);
		}
		else 
		{
			g_source_remove(pViewerImpl->m_iTimeoutClickID );
			pViewerImpl->m_iTimeoutClickID  = 0;
		}
	}
	*/
#endif
	if (widget == pViewerImpl->m_pImageView || widget == pViewerImpl->m_pVideoFixed
		|| widget == pViewerImpl->m_pVideoSinkWidget) 
	{
		if (GDK_BUTTON_PRESS == event->type && 3 == event->button)
		{
			viewer_show_context_menu(event, user_data);
			return TRUE;
		}
		else if ((widget == pViewerImpl->m_pVideoFixed || widget == pViewerImpl->m_pVideoSinkWidget)
			&& GDK_BUTTON_PRESS == event->type && 1 == event->button)
		{
			/* begin a potential pan drag; a click without movement toggles
			 * play/pause when the button is released.  Grab the GL sink's own
			 * window (the fixed is a no-window container) so the drag keeps
			 * tracking even when the pointer leaves the viewer area. */
			if (pViewerImpl->IsVideo())
			{
				pViewerImpl->m_bVideoPanning = TRUE;
				pViewerImpl->m_dVideoPanStartRootX = event->x_root;
				pViewerImpl->m_dVideoPanStartRootY = event->y_root;
				pViewerImpl->m_dVideoPanStartPX = pViewerImpl->m_dVideoPanX;
				pViewerImpl->m_dVideoPanStartPY = pViewerImpl->m_dVideoPanY;
				if (pViewerImpl->m_pVideoSinkWidget != NULL)
				{
				GdkWindow *win = gtk_widget_get_window(pViewerImpl->m_pVideoSinkWidget);
				if (win != NULL)
				{
					gdk_seat_grab(gdk_display_get_default_seat(gdk_window_get_display(win)), win, GDK_SEAT_CAPABILITY_ALL, TRUE,
						NULL, NULL, NULL, NULL);
				}
				}
				pViewerImpl->RefreshAutoHideTimer();
				return TRUE;
			}
		}
	}
	else if (widget == pViewerImpl->m_pPlayProgressEventBox)
	{
		pViewerImpl->m_bWasPlayingBeforeSeek = pViewerImpl->IsPlaying(); 

		viewer_scrub_seek(pViewerImpl, widget, event->x, TRUE);

		/* a GDK pointer grab on the progress bar's window hides the bar, so a
		 * GTK grab of the eventbox keeps the scrub drag tracked */
		pViewerImpl->m_bTimelineSeeking = TRUE;
		gtk_grab_add(widget);

		pViewerImpl->RefreshAutoHideTimer();

		if (pViewerImpl->IsPlaying())
			pViewerImpl->PlayPauseVideo();
	}
	else
	{
		if (GDK_BUTTON_PRESS == event->type && 1 == event->button)
		{
			// play video
			if (pViewerImpl->IsVideo())
			{
				pViewerImpl->RefreshAutoHideTimer();
				pViewerImpl->PlayPauseVideo();
			}
		}
	}
	return FALSE;
} 

static void viewer_show_context_menu(GdkEventButton *event, gpointer userdata)
{
	(void)userdata;

	// FIXME - add more actions
	GtkWidget *menu = gtk_menu_new();
	gtk_widget_insert_action_group(menu, "quiver", G_ACTION_GROUP(QuiverUtils::GetActionGroup()));

	GtkWidget *item = gtk_menu_item_new_with_label("Copy");
	gtk_actionable_set_action_name(GTK_ACTIONABLE(item), "quiver." ACTION_VIEWER_COPY);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	item = gtk_separator_menu_item_new();
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	item = gtk_menu_item_new_with_label("Rotate Counterclockwise");
	gtk_actionable_set_action_name(GTK_ACTIONABLE(item), "quiver." ACTION_VIEWER_ROTATE_CCW);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	item = gtk_menu_item_new_with_label("Rotate Clockwise");
	gtk_actionable_set_action_name(GTK_ACTIONABLE(item), "quiver." ACTION_VIEWER_ROTATE_CW);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	item = gtk_separator_menu_item_new();
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	item = gtk_menu_item_new_with_label("Move To Trash");
	gtk_actionable_set_action_name(GTK_ACTIONABLE(item), "quiver." ACTION_VIEWER_TRASH);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	gtk_widget_show_all(menu);
	gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent*)event);
}




Viewer::ViewerImpl::~ViewerImpl()
{
	StopVideo(false);


	gst_object_unref(GST_OBJECT(m_pPipeline));

	if (0 != m_iIdleSetIndex)
	{
		g_source_remove(m_iIdleSetIndex);
		m_iIdleSetIndex = 0;
	}	

	m_ImageLoader.RemovePixbufLoaderObserver(m_StatusbarPtr.get());
	m_ImageLoader.RemovePixbufLoaderObserver(m_PixbufLoaderObserverPtr.get());

	if (NULL != m_pHBox)
		g_object_unref(m_pHBox);

	PreferencesPtr prefsPtr = Preferences::GetInstance();
	prefsPtr->RemoveEventHandler( m_PreferencesEventHandlerPtr );
}

static gboolean video_apply_zoom_idle(gpointer user_data)
{
	Viewer::ViewerImpl *pViewerImpl = (Viewer::ViewerImpl*)user_data;

	int areaW = pViewerImpl->m_pVideoFixed
		? gtk_widget_get_allocated_width(pViewerImpl->m_pVideoFixed) : 0;
	int areaH = pViewerImpl->m_pVideoFixed
		? gtk_widget_get_allocated_height(pViewerImpl->m_pVideoFixed) : 0;
	g_print("[VP] idle: vW=%d vH=%d area=%dx%d sinkW=%d sinkH=%d fixed_show=%d sink_show=%d sink_opa=%.1f\n",
		pViewerImpl->m_iVideoWidth, pViewerImpl->m_iVideoHeight,
		areaW, areaH,
		pViewerImpl->m_iVideoSinkW, pViewerImpl->m_iVideoSinkH,
		pViewerImpl->m_pVideoFixed ? gtk_widget_get_visible(pViewerImpl->m_pVideoFixed) : -1,
		pViewerImpl->m_pVideoSinkWidget ? gtk_widget_get_mapped(pViewerImpl->m_pVideoSinkWidget) : -1,
		pViewerImpl->m_pVideoSinkWidget ? gtk_widget_get_opacity(pViewerImpl->m_pVideoSinkWidget) : -1);

	/* The caps probe can fire before GTK has allocated the layout or mapped
	 * the sink widget.  Without a mapped sink the GL surface does not exist
	 * and the video cannot render.  Keep retrying until the sink is mapped
	 * (which means the overlay has allocated the stack -> layout -> sink). */
	pViewerImpl->ApplyVideoZoom();
	if (pViewerImpl->m_pVideoFixed != NULL)
		gtk_widget_show(pViewerImpl->m_pVideoFixed);
	if (pViewerImpl->m_pVideoSinkWidget != NULL)
	{
		gtk_widget_show(pViewerImpl->m_pVideoSinkWidget);
	}
	g_print("[VP] idle: DONE fixed_show=%d sink_mapped=%d sink_opa=%.1f\n",
		pViewerImpl->m_pVideoFixed ? gtk_widget_get_visible(pViewerImpl->m_pVideoFixed) : -1,
		pViewerImpl->m_pVideoSinkWidget ? gtk_widget_get_mapped(pViewerImpl->m_pVideoSinkWidget) : -1,
		pViewerImpl->m_pVideoSinkWidget ? gtk_widget_get_opacity(pViewerImpl->m_pVideoSinkWidget) : -1);
	return FALSE;
}

static void video_zoom_raise_windows_cb(GtkWidget *widget, gpointer user_data)
{
	(void)user_data;
	if (widget == NULL || !gtk_widget_get_realized(widget))
		return;
	if (GTK_IS_CONTAINER(widget))
	{
		/* raise children first so the outermost window of each branch ends up
		 * on top of its ancestors */
		GList *children = gtk_container_get_children(GTK_CONTAINER(widget));
		for (GList *l = children; l != NULL; l = l->next)
			video_zoom_raise_windows_cb((GtkWidget *)l->data, NULL);
		g_list_free(children);
	}
	if (gtk_widget_get_has_window(widget))
	{
		GdkWindow *win = gtk_widget_get_window(widget);
		if (win != NULL)
			gdk_window_raise(win);
	}
}

static void video_zoom_raise_media_windows(Viewer::ViewerImpl *p)
{
	/* A GtkOverlay gives each overlay child its own GdkWindow.  Showing an
	 * overlay child implicitly raises its window, so when the video's fixed
	 * container is shown it is stacked above the media controls even though
	 * the overlay still draws the controls on top; clicks then land on the
	 * video and the timeline / volume button are dead.  Raise the controls'
	 * overlay window (the no-window alignment's parent window) and every
	 * windowed descendant whenever the video appears or the layout changes
	 * so the controls always stay on top of the video. */
	if (p->m_pMediaControls != NULL)
	{
		GtkWidget *alignment = gtk_widget_get_parent(p->m_pMediaControls);
		if (alignment != NULL && gtk_widget_get_realized(alignment))
		{
			GdkWindow *win = gtk_widget_get_window(alignment);
			if (win != NULL)
				gdk_window_raise(win);
		}
		video_zoom_raise_windows_cb(p->m_pMediaControls, NULL);
	}
}

static void video_zoom_sink_map_cb(GtkWidget *widget, gpointer user_data)
{
	(void)widget;
	g_print("[VP] sink_map_cb: sink_mapped=%d\n",
		gtk_widget_get_mapped(widget));
	video_zoom_raise_media_windows((Viewer::ViewerImpl *)user_data);
}

static void video_zoom_resize_cb(GtkWidget *widget, GdkRectangle *allocation, gpointer user_data)
{
	(void)widget;
	Viewer::ViewerImpl *pViewerImpl = (Viewer::ViewerImpl*)user_data;
	g_print("[VP] resize_cb: alloc=%dx%d videoW=%d videoH=%d\n",
		allocation->width, allocation->height,
		pViewerImpl->m_iVideoWidth, pViewerImpl->m_iVideoHeight);
	if (pViewerImpl->m_iVideoWidth > 0 && pViewerImpl->m_iVideoHeight > 0)
		g_idle_add_full(G_PRIORITY_HIGH, video_apply_zoom_idle, pViewerImpl, NULL);
	video_zoom_raise_media_windows(pViewerImpl);
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
					g_print("[VP] caps_probe: w=%d h=%d\n", w, h);
					pViewerImpl->m_iVideoWidth = w;
					pViewerImpl->m_iVideoHeight = h;
					/* element properties must not be touched from the streaming
					 * thread, so re-apply the zoom on the main thread */
					g_idle_add_full(G_PRIORITY_HIGH, video_apply_zoom_idle, pViewerImpl, NULL);
				}
			}
		}
	}
	return GST_PAD_PROBE_OK;
}

static GstPadProbeReturn video_zoom_reconfigure_probe(GstPad *pad, GstPadProbeInfo *info, gpointer user_data)
{ (void)pad; (void)user_data;

	/* gtkglsink pushes an upstream reconfigure event whenever its widget is
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
#define VIDEO_ZOOM_SMOOTH_ANIMATION 0

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
	else if (zoom > 8.0)
		zoom = 8.0;
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
	gint w = gtk_widget_get_allocated_width(area);
	gint h = gtk_widget_get_allocated_height(area);
	if (w <= 0 || h <= 0)
		return;
	/* use the toplevel window for the pointer lookup and translate the
	 * result back into the area's coords */
	GtkWidget *toplevel = gtk_widget_get_toplevel(area);
	if (toplevel == NULL || !gtk_widget_get_realized(toplevel))
		return;
	gint dx = 0, dy = 0;
	if (!gtk_widget_translate_coordinates(area, toplevel, 0, 0, &dx, &dy))
		return;
	GdkWindow *win = gtk_widget_get_window(toplevel);
	gint x = 0, y = 0;
	GdkModifierType mask;
	GdkDevice *device = gdk_seat_get_pointer(
		gdk_display_get_default_seat(gdk_window_get_display(win)));
	gdk_window_get_device_position(win, device, &x, &y, &mask);
	x -= dx;
	y -= dy;
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
		g_print("[VP] ApplyVideoZoom: bail vW=%d vH=%d (no dims)\n",
			m_iVideoWidth, m_iVideoHeight);
		return;
	}

	/* Wait for ASYNC_DONE to restore opacity to prevent old frame flash */

	gint areaW = gtk_widget_get_allocated_width(m_pVideoFixed);
	gint areaH = gtk_widget_get_allocated_height(m_pVideoFixed);
	if (areaW <= 0 || areaH <= 0)
	{
		g_print("[VP] ApplyVideoZoom: bail area=%dx%d (no alloc yet)\n", areaW, areaH);
		return;
	}

	gdouble srcW = m_iVideoWidth;
	gdouble srcH = m_iVideoHeight;

	/* the zoom is the magnification relative to the video's actual size
	 * (1.0 = 100%), exactly like the image view: FIT never upscales a small
	 * video past its actual size, FIT_STRETCH upscales it to fill the window,
	 * ACTUAL_SIZE pins 100%, FILL_SCREEN covers the whole window, and ZOOM
	 * keeps the factor the user zoomed to with +/- or the wheel.  The FIT
	 * variants are recomputed here on every window resize so the video
	 * re-fits; the others keep their fixed zoom. */
	gdouble fitScale = MIN((gdouble)areaW / srcW, (gdouble)areaH / srcH);
	if (fitScale <= 0.)
		fitScale = 1.;
	gdouble fillScale = MAX((gdouble)areaW / srcW, (gdouble)areaH / srcH);
	gdouble fitZoom = MIN(fitScale, 1.0);

	m_dVideoZoomMin = MIN(m_dVideoZoomMin, fitZoom);

	gdouble zoom;
	QuiverImageViewMode videoViewMode =
		quiver_image_view_get_view_mode(QUIVER_IMAGE_VIEW(m_pImageView));
	switch (videoViewMode)
	{
		case QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW:
			zoom = fitZoom;
			break;
		case QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW_STRETCH:
			zoom = fitScale;
			break;
		case QUIVER_IMAGE_VIEW_MODE_ACTUAL_SIZE:
			zoom = 1.0;
			break;
		case QUIVER_IMAGE_VIEW_MODE_FILL_SCREEN:
			zoom = fillScale;
			break;
		default:
			zoom = CLAMP(m_dVideoZoom, m_dVideoZoomMin, 8.0);
			break;
	}
	m_dVideoZoom = zoom;

	/* the video display grows with the zoom until it covers the viewer area,
	 * like an image; zooming beyond that is covered by the crop + upscale
	 * below.  The widget is also capped so gtkglsink's GL surface stays
	 * within limits on huge windows; past the cap the crop covers the extra
	 * zoom. */
	gdouble effZoom = MIN(zoom, MIN(fillScale, 4096. / MAX(srcW, srcH)));
	gdouble widgetW = srcW * effZoom;
	gdouble widgetH = srcH * effZoom;

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
	if (!m_bVideoPanning && m_bVideoZoomToCursor && m_dVideoLastWidgetW > 0.)
	{
		m_bVideoZoomToCursor = FALSE;
		gdouble px = -1., py = -1.;
		video_zoom_get_pointer(this, &px, &py);
		if (px < 0. || py < 0.)
		{
			px = areaW / 2.;
			py = areaH / 2.;
		}
		/* the source pixel under screen point (px,py) is pan + px*crop/widget */
		{
			gdouble srcX = m_dVideoPanX + px * (srcW / m_dVideoLastZc) / m_dVideoLastWidgetW;
			gdouble srcY = m_dVideoPanY + py * (srcH / m_dVideoLastZc) / m_dVideoLastWidgetH;
			m_dVideoPanX = srcX - px * (srcW / zc) / widgetW;
			m_dVideoPanY = srcY - py * (srcH / zc) / widgetH;
		}
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
			/* nvvidconv crop properties are coordinates, not margins:
			 * right = srcW - rightMargin - 1, bottom = srcH - bottomMargin - 1 */
			g_object_set(G_OBJECT(m_pVideoZoomScaler),
				"left", cropLeft,
				"right", (gint)srcW - cropRight - 1,
				"top", cropTop,
				"bottom", (gint)srcH - cropBottom - 1,
				NULL);
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
	g_print("[VP] ApplyVideoZoom: area=%dx%d newSink=%dx%d@%d,%d cached=%dx%d@%d,%d\n",
		areaW, areaH, newW, newH, newX, newY,
		m_iVideoSinkW, m_iVideoSinkH, m_iVideoSinkX, m_iVideoSinkY);
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
		gtk_layout_move(GTK_LAYOUT(m_pVideoFixed), m_pVideoSinkWidget, newX, newY);
		g_print("[VP] ApplyVideoZoom: SET size_request + layout_move\n");
	}
	else
	{
		g_print("[VP] ApplyVideoZoom: SKIP (unchanged)\n");
	}

	/* report the zoom percentage in the statusbar, like the image view does
	 * (only while the video is actually shown, so a resize idle while viewing
	 * an image does not clobber the image's percentage) */
	if (gtk_widget_get_visible(m_pVideoSinkWidget))
		m_StatusbarPtr->SetMagnification((int)(m_dVideoZoom * 100. + 0.5));

	m_dVideoLastWidgetW = widgetW;
	m_dVideoLastWidgetH = widgetH;
	m_dVideoLastZc = zc;
}

static void viewer_speed_button_clicked_cb(GtkButton *button, gpointer user_data)
{
	(void)button;
	Viewer::ViewerImpl *p = (Viewer::ViewerImpl *)user_data;
	int val = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "speed-value"));
	gdouble speed = val / 1000.0;
	p->SetPlaybackSpeed(speed);
	/* close the popover */
	GtkWidget *speedBtn = p->m_pSpeedButton;
	if (speedBtn != NULL)
	{
		GtkPopover *popover = (GtkPopover *)g_object_get_data(G_OBJECT(speedBtn), "speed-popover");
		if (popover != NULL)
			gtk_widget_hide(GTK_WIDGET(popover));
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
		gtk_widget_hide(GTK_WIDGET(popover));
	}
	else
	{
		/* highlight current speed */
		GtkWidget *box = (GtkWidget *)g_object_get_data(G_OBJECT(popover), "speed-box");
		if (box != NULL)
		{
			GList *children = gtk_container_get_children(GTK_CONTAINER(box));
			gdouble current = p->m_dPlaybackSpeed;
			for (GList *l = children; l != NULL; l = l->next)
			{
				GtkWidget *child = (GtkWidget *)l->data;
				int val2 = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(child), "speed-value"));
				gdouble spd = val2 / 1000.0;
				GtkStyleContext *ctx = gtk_widget_get_style_context(child);
				if (spd == current)
					gtk_style_context_add_class(ctx, "speed-active");
				else
					gtk_style_context_remove_class(ctx, "speed-active");
			}
			g_list_free(children);
		}
		gtk_popover_set_relative_to(GTK_POPOVER(popover), speedBtn);
		gtk_widget_show(GTK_WIDGET(popover));
	}
}

static void viewer_fullscreen_button_clicked_cb(GtkButton *button, gpointer user_data)
{
	(void)button;
	(void)user_data;
	GAction *fs = QuiverUtils::GetAction("FullScreen");
	if (fs != NULL)
	{
		gboolean active = g_variant_get_boolean(g_action_get_state(fs));
		g_action_activate(fs, g_variant_new_boolean(!active));
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
	
	m_ImageListPtr(new ImageList()),
	
	m_bIsPlaying(false),
	m_bWasPlayingBeforeSeek(false),
	m_bTimelineSeeking(false),
	m_iTimelineLastSeekTarget(-1),
	m_ThumbnailCache(100),
	m_dPlaybackSpeed(1.0),
	m_pSpeedButton(NULL),
	m_pSpeedLabel(NULL),
	m_pTransportBox(NULL),
	m_pRightControls(NULL),
	m_PreferencesEventHandlerPtr ( new PreferencesEventHandler(this) ),
	m_ImageListEventHandlerPtr( new ImageListEventHandler(this) ),
#ifdef QUIVER_MAEMO
	m_ThumbnailLoader(this,1)
#else
	m_ThumbnailLoader(this,2)
#endif
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
	gtk_widget_set_no_show_all(alignment,TRUE);
	gtk_box_set_spacing(GTK_BOX(alignment), 0);

	/* Row 1: full-width progress bar inside a button-height event box */
	GtkWidget* row1 = gtk_event_box_new();
	m_pPlayProgressEventBox = row1;
	m_pPlayProgress = gtk_progress_bar_new();
	{
		gtk_widget_set_size_request(row1, -1, 36);
		gtk_widget_set_valign(m_pPlayProgress, GTK_ALIGN_CENTER);
	}
	gtk_widget_add_events(row1, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_BUTTON_MOTION_MASK | GDK_POINTER_MOTION_MASK);
	gtk_container_add(GTK_CONTAINER(row1), m_pPlayProgress);
	gtk_widget_show_all(row1);

	/* Row 2: play + time + skip-back + skip-fwd + spacer + speed + vol + fs + snap */
	GtkWidget* row2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_set_margin_bottom(row2, 2);
	gtk_widget_set_margin_start(row2, 2);
	gtk_widget_set_margin_end(row2, 2);

	/* Play button — always visible when a video is loaded */
	GtkWidget* playEventBox = gtk_event_box_new();
	m_pPlayImage = gtk_image_new_from_icon_name("media-playback-start", GTK_ICON_SIZE_DIALOG);
	gtk_container_add(GTK_CONTAINER(playEventBox), m_pPlayImage);
	m_pPlayButton = playEventBox;
	gtk_box_pack_start(GTK_BOX(row2), playEventBox, FALSE, TRUE, 0);

	/* Time label — hidden until play */
	m_pTimeLabel = gtk_label_new("");
	gtk_widget_set_no_show_all(m_pTimeLabel, TRUE);
	gtk_widget_set_margin_start(m_pTimeLabel, 10);
	gtk_box_pack_start(GTK_BOX(row2), m_pTimeLabel, FALSE, TRUE, 0);

	/* Skip back / skip forward — hidden until play */
	GtkWidget* skipBackBtn = gtk_button_new_from_icon_name("media-skip-backward", GTK_ICON_SIZE_BUTTON);
	gtk_button_set_relief(GTK_BUTTON(skipBackBtn), GTK_RELIEF_NONE);
	g_signal_connect_swapped(G_OBJECT(skipBackBtn), "clicked", G_CALLBACK(viewer_skip_back_cb), this);
	gtk_widget_set_no_show_all(skipBackBtn, TRUE);
	gtk_box_pack_start(GTK_BOX(row2), skipBackBtn, FALSE, TRUE, 0);

	GtkWidget* skipFwdBtn = gtk_button_new_from_icon_name("media-skip-forward", GTK_ICON_SIZE_BUTTON);
	gtk_button_set_relief(GTK_BUTTON(skipFwdBtn), GTK_RELIEF_NONE);
	g_signal_connect_swapped(G_OBJECT(skipFwdBtn), "clicked", G_CALLBACK(viewer_skip_fwd_cb), this);
	gtk_widget_set_no_show_all(skipFwdBtn, TRUE);
	gtk_box_pack_start(GTK_BOX(row2), skipFwdBtn, FALSE, TRUE, 0);

	/* Frame step back / frame step forward — hidden until play */
	GtkWidget* frameStepBackBtn = gtk_button_new_from_icon_name("media-seek-backward", GTK_ICON_SIZE_BUTTON);
	gtk_button_set_relief(GTK_BUTTON(frameStepBackBtn), GTK_RELIEF_NONE);
	g_signal_connect_swapped(G_OBJECT(frameStepBackBtn), "clicked", G_CALLBACK(viewer_frame_step_back_cb), this);
	gtk_widget_set_no_show_all(frameStepBackBtn, TRUE);
	gtk_box_pack_start(GTK_BOX(row2), frameStepBackBtn, FALSE, TRUE, 0);

	GtkWidget* frameStepFwdBtn = gtk_button_new_from_icon_name("media-seek-forward", GTK_ICON_SIZE_BUTTON);
	gtk_button_set_relief(GTK_BUTTON(frameStepFwdBtn), GTK_RELIEF_NONE);
	g_signal_connect_swapped(G_OBJECT(frameStepFwdBtn), "clicked", G_CALLBACK(viewer_frame_step_fwd_cb), this);
	gtk_widget_set_no_show_all(frameStepFwdBtn, TRUE);
	gtk_box_pack_start(GTK_BOX(row2), frameStepFwdBtn, FALSE, TRUE, 0);

	/* m_pTransportBox is a hidden bookkeeping container for skip-back/skip-fwd
	 * widget pointers; its children are packed directly into row2 above. */
	m_pTransportBox = skipBackBtn;

	/* Spacer between left and right controls */
	GtkWidget* spacer = gtk_label_new("");
	gtk_widget_set_hexpand(spacer, TRUE);
	gtk_box_pack_start(GTK_BOX(row2), spacer, TRUE, TRUE, 0);

	/* Right controls: speed + volume + fullscreen + snapshot, hidden until play */
	m_pRightControls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_set_no_show_all(m_pRightControls, TRUE);

	/* Speed button — plain GtkButton with manual popover toggle.
	 * GtkMenuButton's popover grab causes the popover to vanish when the
	 * pointer crosses from the button into the popover window. */
	m_dPlaybackSpeed = 1.0;
	GtkWidget* speedPopover = gtk_popover_new(NULL);
	g_object_ref_sink(speedPopover);
	gtk_popover_set_modal(GTK_POPOVER(speedPopover), FALSE);
	gtk_widget_set_size_request(speedPopover, 120, -1);
	GtkWidget* speedBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
	g_object_set_data(G_OBJECT(speedPopover), "speed-box", speedBox);

	const double speeds[] = { 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0 };
	const int nSpeeds = sizeof(speeds) / sizeof(speeds[0]);
	for (int i = 0; i < nSpeeds; i++)
	{
		GtkWidget* btn = gtk_button_new();
		gchar* label = g_strdup_printf("%.4g", speeds[i]);
		gtk_button_set_label(GTK_BUTTON(btn), label);
		g_free(label);
		g_object_set_data(G_OBJECT(btn), "speed-value", GINT_TO_POINTER((int)(speeds[i] * 1000)));
		g_signal_connect(G_OBJECT(btn), "clicked", G_CALLBACK(viewer_speed_button_clicked_cb), this);
		gtk_box_pack_start(GTK_BOX(speedBox), btn, TRUE, TRUE, 0);
	}

	gtk_container_add(GTK_CONTAINER(speedPopover), speedBox);
	gtk_widget_show_all(speedBox);

	m_pSpeedButton = gtk_button_new();
	gtk_button_set_relief(GTK_BUTTON(m_pSpeedButton), GTK_RELIEF_NONE);
	m_pSpeedLabel = gtk_label_new("1x");
	gtk_container_add(GTK_CONTAINER(m_pSpeedButton), m_pSpeedLabel);
	g_object_set_data(G_OBJECT(m_pSpeedButton), "speed-popover", speedPopover);
	g_signal_connect_swapped(G_OBJECT(m_pSpeedButton), "clicked", G_CALLBACK(viewer_speed_toggle_popover_cb), this);
	gtk_box_pack_start(GTK_BOX(m_pRightControls), m_pSpeedButton, FALSE, TRUE, 0);

	/* screen-wide CSS for speed-active highlight */
	GtkCssProvider *cssProvider = gtk_css_provider_new();
	gtk_css_provider_load_from_data(cssProvider,
		".speed-active { background-image: none; background-color: @theme_selected_bg_color; color: @theme_selected_fg_color; }", -1, NULL);
	gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
		GTK_STYLE_PROVIDER(cssProvider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_unref(cssProvider);

	/* Volume button */
	m_pVolumeButton = gtk_volume_button_new();
	gtk_box_pack_start(GTK_BOX(m_pRightControls), m_pVolumeButton, FALSE, TRUE, 0);

	/* Fullscreen button */
	GtkWidget* fsBtn = gtk_button_new_from_icon_name("view-fullscreen", GTK_ICON_SIZE_BUTTON);
	gtk_button_set_relief(GTK_BUTTON(fsBtn), GTK_RELIEF_NONE);
	g_signal_connect(G_OBJECT(fsBtn), "clicked", G_CALLBACK(viewer_fullscreen_button_clicked_cb), this);
	gtk_box_pack_start(GTK_BOX(m_pRightControls), fsBtn, FALSE, TRUE, 0);

	/* Snapshot button */
	GtkWidget* snapBtn = gtk_button_new_from_icon_name("camera-photo", GTK_ICON_SIZE_BUTTON);
	gtk_button_set_relief(GTK_BUTTON(snapBtn), GTK_RELIEF_NONE);
	g_signal_connect(G_OBJECT(snapBtn), "clicked", G_CALLBACK(viewer_snapshot_button_clicked_cb), this);
	gtk_box_pack_start(GTK_BOX(m_pRightControls), snapBtn, FALSE, TRUE, 0);

	gtk_box_pack_start(GTK_BOX(row2), m_pRightControls, FALSE, TRUE, 0);

	gtk_box_pack_start(GTK_BOX(alignment), row1, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(alignment), row2, FALSE, TRUE, 0);

	m_pMediaControls = alignment;
	m_pTimeline = row1;

	/* Wire events */
	g_signal_connect(G_OBJECT(m_pVolumeButton), "value-changed", G_CALLBACK(viewer_volume_value_changed), this);
	g_signal_connect(G_OBJECT(m_pPlayButton), "button-press-event", G_CALLBACK(viewer_button_press_cb), this);
	g_signal_connect(G_OBJECT(m_pPlayProgressEventBox), "button-press-event", G_CALLBACK(viewer_button_press_cb), this);
	g_signal_connect(G_OBJECT(m_pPlayProgressEventBox), "button_release_event", G_CALLBACK(viewer_button_release_cb), this);
	g_signal_connect(G_OBJECT(m_pPlayProgressEventBox), "motion-notify-event", G_CALLBACK(viewer_motion_notify), this);

	{
		gboolean ns = gtk_widget_get_no_show_all(alignment);
		gtk_widget_set_no_show_all(alignment, FALSE);
		gtk_widget_show_all(alignment);
		gtk_widget_set_no_show_all(alignment, ns);
	}
	/* Only the play button should be visible when a video is loaded. */
	gtk_widget_hide(m_pTimeLabel);
	gtk_widget_hide(skipBackBtn);
	gtk_widget_hide(skipFwdBtn);
	gtk_widget_hide(m_pRightControls);

	m_iCurrentOrientation = 1;
	
	m_SlideShowState = SLIDESHOW_STATE_ADVANCE;
	m_iIdleSetIndex = 0;
	m_iTimeoutScrollbars = 0;
	m_iTimeoutUpdateListID = 0;
	m_iTimeoutSlideshowID = 0;
	m_iTimeoutClickID = 0;
	m_iTimeoutMouseMotionNotify = 0;
	m_dSmoothScrollAccum = 0.;
	m_VideoZoomType = VIDEO_ZOOM_SOFTWARE;
	m_dVideoZoom = 1.0;
	m_dVideoZoomFinal = 1.0;
	m_dVideoZoomMin = 1.0;
	m_dVideoScrollAccum = 0.;
	m_dVideoPanX = 0.;
	m_dVideoPanY = 0.;
	m_dVideoLastWidgetW = 0.;
	m_dVideoLastWidgetH = 0.;
	m_dVideoLastZc = 1.0;
	m_iVideoZoomTimeoutID = 0;
	m_bVideoZoomCropActive = FALSE;
	m_bVideoZoomInputCropActive = FALSE;
	m_bVideoPanning = FALSE;
	m_bVideoZoomToCursor = FALSE;
	m_iVideoWidth = 0;
	m_iVideoHeight = 0;
	m_iVideoFpsNum = 0;
	m_iVideoFpsDen = 1;
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
	
	m_pNavigationBox = gtk_event_box_new ();

GtkWidget *image = gtk_image_new_from_icon_name("view-fullscreen", GTK_ICON_SIZE_BUTTON);
	gtk_container_add (GTK_CONTAINER (m_pNavigationBox),image);
	gtk_widget_show(image);

	gtk_widget_set_no_show_all(m_pScrollbarV,TRUE);
	gtk_widget_set_no_show_all(m_pScrollbarH,TRUE);
	gtk_widget_set_no_show_all(m_pNavigationBox,TRUE);


	g_signal_connect (G_OBJECT (m_pNavigationBox), 
			  "button_press_event",
			  G_CALLBACK (viewer_navigation_button_press_event), 
			  this);

	

	m_pGrid = gtk_grid_new ();
	m_pOverlay = gtk_overlay_new ();
	m_pStack = gtk_stack_new ();
	gtk_widget_set_hexpand(m_pStack, TRUE);
	gtk_widget_set_vexpand(m_pStack, TRUE);

	// left right top bottom
	// media controls float above the image/video area
	gtk_widget_set_hexpand(alignment, TRUE);
	gtk_widget_set_valign(alignment, GTK_ALIGN_END);
	gtk_overlay_add_overlay(GTK_OVERLAY(m_pOverlay), alignment);
	gtk_overlay_set_overlay_pass_through(GTK_OVERLAY(m_pOverlay), alignment, TRUE);

	// the image/video stack is the overlay's single main widget
	gtk_widget_set_hexpand(m_pImageView, TRUE);
	gtk_widget_set_vexpand(m_pImageView, TRUE);
	gtk_stack_add_named(GTK_STACK(m_pStack), m_pImageView, "image");
	gtk_container_add(GTK_CONTAINER(m_pOverlay), m_pStack);

	gtk_grid_attach (GTK_GRID (m_pGrid), m_pOverlay, 0, 0, 1, 1);

	//gtk_widget_set_hexpand(m_pImageView, TRUE);
	gtk_widget_set_vexpand(m_pScrollbarV, TRUE);
	gtk_grid_attach (GTK_GRID (m_pGrid), m_pScrollbarV, 1, 0, 1, 1);
			  
	gtk_widget_set_hexpand(m_pScrollbarH, TRUE);
	gtk_grid_attach (GTK_GRID (m_pGrid), m_pScrollbarH, 0, 1, 1, 1);
			  
	//gtk_grid_attach (GTK_GRID (m_pGrid), m_pNavigationBox, 1, 1, 1, 1);

//	GTK_WIDGET_SET_FLAGS(m_pGrid,GTK_CAN_FOCUS);
	m_pHBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL,0);
	g_object_ref(m_pHBox);
	m_pVBox = gtk_box_new(GTK_ORIENTATION_VERTICAL,0);

	gtk_box_pack_start (GTK_BOX (m_pVBox), m_pGrid, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (m_pHBox), m_pVBox, TRUE, TRUE, 0);

	AddFilmstrip();

	string strBGColorImg   = prefsPtr->GetString(QUIVER_PREFS_APP,QUIVER_PREFS_APP_BG_IMAGEVIEW,"#000");
	string strBGColorThumb = prefsPtr->GetString(QUIVER_PREFS_APP,QUIVER_PREFS_APP_BG_ICONVIEW, "#444");

#ifdef QUIVER_MAEMO
	if (!prefsPtr->GetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_USE_THEME_COLOR,false))
#else
	if (!prefsPtr->GetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_USE_THEME_COLOR,true))
#endif
	{
		if (!strBGColorImg.empty())
		{
			GdkRGBA color;
			gdk_rgba_parse(&color, strBGColorImg.c_str());
			set_widget_bg_color(m_pImageView, &color);
		}
		
		if (!strBGColorThumb.empty())
		{
			GdkRGBA color;
			gdk_rgba_parse(&color, strBGColorThumb.c_str());
			set_widget_bg_color(m_pIconView, &color);
		}
	}

	m_iSlideShowDuration = prefsPtr->GetInteger(QUIVER_PREFS_SLIDESHOW,QUIVER_PREFS_SLIDESHOW_DURATION, 3000);
	m_bSlideShowLoop = prefsPtr->GetBoolean(QUIVER_PREFS_SLIDESHOW,QUIVER_PREFS_SLIDESHOW_LOOP,true);

	m_bMaximizeViewableArea = prefsPtr->GetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_ROTATE_FOR_BEST_FIT, false);
	
	m_bIsPlaying = false;

	quiver_image_view_set_magnification_mode(QUIVER_IMAGE_VIEW(m_pImageView),QUIVER_IMAGE_VIEW_MAGNIFICATION_MODE_SMOOTH);
	
	QuiverImageViewMode view_mode =
		(QuiverImageViewMode)prefsPtr->GetInteger(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_DEFAULT_VIEW_MODE, QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW);

	quiver_image_view_set_view_mode(QUIVER_IMAGE_VIEW(m_pImageView), view_mode);

	//QuiverImageViewMode mode = quiver_image_view_get_view_mode(QUIVER_IMAGE_VIEW(m_pImageView));
	//if (_ == mode || _ == mode)
	
	gtk_drag_source_set (m_pImageView, (GdkModifierType)(GDK_BUTTON1_MASK | GDK_BUTTON3_MASK),
			   quiver_drag_target_table, G_N_ELEMENTS(quiver_drag_target_table), (GdkDragAction)( GDK_ACTION_COPY |
		       GDK_ACTION_MOVE |
	           GDK_ACTION_LINK |
	           GDK_ACTION_ASK ));
	           
	g_signal_connect (G_OBJECT (m_pImageView), "drag_data_received",
				G_CALLBACK (signal_drag_data_received), this);
	           
	g_signal_connect (G_OBJECT (m_pImageView), "drag_data_get",
		      G_CALLBACK (signal_drag_data_get), this);

	g_signal_connect (G_OBJECT (m_pImageView), "drag_data_delete",
		      G_CALLBACK (signal_drag_data_delete), this);

	g_signal_connect (G_OBJECT (m_pImageView), "drag_begin",
	      G_CALLBACK (signal_drag_begin), this);
	
	g_signal_connect (G_OBJECT (m_pImageView), "drag_end",
	      G_CALLBACK (signal_drag_end), this);		  


	g_signal_connect (G_OBJECT (m_pImageView), "drag_drop",
				G_CALLBACK (signal_drag_drop), this);

	g_signal_connect (G_OBJECT (m_pImageView), "drag_motion",
				G_CALLBACK (signal_drag_motion), this);

	g_signal_connect (G_OBJECT (m_pImageView), "motion-notify-event",
				G_CALLBACK (viewer_motion_notify), this);


	quiver_icon_view_set_n_items_func(QUIVER_ICON_VIEW(m_pIconView),(QuiverIconViewGetNItemsFunc)n_cells_callback,this,NULL);
	quiver_icon_view_set_thumbnail_pixbuf_func(QUIVER_ICON_VIEW(m_pIconView),(QuiverIconViewGetThumbnailPixbufFunc)thumbnail_pixbuf_callback,this,NULL);
	quiver_icon_view_set_icon_pixbuf_func(QUIVER_ICON_VIEW(m_pIconView),(QuiverIconViewGetIconPixbufFunc)icon_pixbuf_callback,this,NULL);
	quiver_icon_view_set_scroll_type(QUIVER_ICON_VIEW(m_pIconView),QUIVER_ICON_VIEW_SCROLL_SMOOTH_CENTER);
	int iIconSize = prefsPtr->GetInteger(QUIVER_PREFS_VIEWER,QUIVER_PREFS_VIEWER_FILMSTRIP_SIZE, 128);
	quiver_icon_view_set_icon_size(QUIVER_ICON_VIEW(m_pIconView),iIconSize,iIconSize);
	quiver_icon_view_set_drag_behavior(QUIVER_ICON_VIEW(m_pIconView),QUIVER_ICON_VIEW_DRAG_BEHAVIOR_SCROLL);

	g_signal_connect(G_OBJECT(m_pIconView),"cell_activated",G_CALLBACK(viewer_iconview_cell_activated),this);
	g_signal_connect(G_OBJECT(m_pIconView),"cursor_changed",G_CALLBACK(viewer_iconview_cursor_changed),this);

	//popup menu stuff
	g_signal_connect(G_OBJECT(m_pImageView), "button-press-event", G_CALLBACK(viewer_button_press_cb), this);
	g_signal_connect(G_OBJECT(m_pImageView), "popup-menu", G_CALLBACK(viewer_popup_menu_cb), this);
#ifdef QUIVER_MAEMO
	g_signal_connect (G_OBJECT (m_pImageView), "tap-and-hold", G_CALLBACK (viewer_popup_menu_cb), this);
	gtk_widget_tap_and_hold_setup (m_pImageView, NULL, NULL, (GtkWidgetTapAndHoldFlags)0);
#endif
	
	
    g_signal_connect (G_OBJECT (m_pImageView), "scroll_event",
    			G_CALLBACK (viewer_scrollwheel_event), this);

    g_signal_connect (G_OBJECT (m_pImageView), "activated",
    			G_CALLBACK (viewer_imageview_activated), this);

    g_signal_connect (G_OBJECT (m_pImageView), "reload",
    			G_CALLBACK (viewer_imageview_reload), this);

    g_signal_connect (G_OBJECT (m_pImageView), "magnification-changed",
    			G_CALLBACK (viewer_imageview_magnification_changed), this);

    g_signal_connect (G_OBJECT (m_pImageView), "view-mode-changed",
    			G_CALLBACK (viewer_imageview_view_mode_changed), this);

    g_signal_connect (G_OBJECT (m_pImageView), "key-press-event",
    			G_CALLBACK (viewer_imageview_key_press_event), this);

    g_signal_connect (G_OBJECT (m_pAdjustmentH), "changed",
    			G_CALLBACK (image_view_adjustment_changed), this);

    g_signal_connect (G_OBJECT (m_pAdjustmentV), "changed",
    			G_CALLBACK (image_view_adjustment_changed), this);

	//g_signal_connect(G_OBJECT(m_pIconView),"selection_changed",G_CALLBACK(iconview_selection_changed_cb),this);
	IPixbufLoaderObserverPtr tmp( new ViewerImageViewPixbufLoaderObserver(QUIVER_IMAGE_VIEW(m_pImageView)));
	m_PixbufLoaderObserverPtr = tmp;
	m_ImageLoader.AddPixbufLoaderObserver(m_PixbufLoaderObserverPtr.get());
	
	gtk_widget_show_all(m_pHBox);
	gtk_widget_hide(m_pHBox);
	gtk_widget_set_no_show_all(m_pHBox,TRUE);
	
	
	m_pNavigationWindow = gtk_window_new (GTK_WINDOW_POPUP);
	m_pNavigationControl = quiver_navigation_control_new_with_adjustments (m_pAdjustmentH, m_pAdjustmentV);

	g_signal_connect (G_OBJECT (m_pNavigationControl), "button_release_event",  
				G_CALLBACK (navigation_control_button_release_event), this);

 	gtk_widget_add_events (m_pNavigationControl, GDK_POINTER_MOTION_MASK|GDK_POINTER_MOTION_HINT_MASK);
	gtk_widget_add_events (m_pNavigationControl, GDK_BUTTON_PRESS_MASK|GDK_BUTTON_RELEASE_MASK);

	GtkWidget * frame = gtk_frame_new(NULL);
	gtk_frame_set_shadow_type(GTK_FRAME(frame),GTK_SHADOW_ETCHED_IN);	

	
	gtk_container_add (GTK_CONTAINER (frame), m_pNavigationControl);
	gtk_container_add (GTK_CONTAINER (m_pNavigationWindow), frame);
	
	bool bShowFilmstrip = prefsPtr->GetBoolean(QUIVER_PREFS_VIEWER,QUIVER_PREFS_VIEWER_FILMSTRIP_SHOW,true);
	if (!bShowFilmstrip)
	{
		gtk_widget_hide(m_pIconView);
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
	m_bVideoZoomToCursor = FALSE;
	GstElement* video_sink = NULL;
	GstElement* gtkglsink = gst_element_factory_make("gtkglsink", NULL);
	GstElement* glupload = NULL;
	if (gtkglsink != NULL)
	{
		g_object_set(G_OBJECT(gtkglsink), "force-aspect-ratio", TRUE, NULL);
		g_object_get(G_OBJECT(gtkglsink), "widget", &m_pVideoSinkWidget, NULL);

		/* glsinkbin fails to share its GL display with gtkglsink's widget
		 * context on Wayland and leaves the window black.  Link glupload
		 * explicitly instead, matching the known-good
		 * `vapostproc ! glupload ! gtkglsink` reference pipeline.
		 *
		 * NOTE: glupload + gtkglsink must be added to the zoombin DIRECTLY,
		 * not wrapped in a separate glsinkbin.  A wrapper bin widens the
		 * sink template seen by playsink (video/x-raw ANY via the ghost
		 * pad), which breaks the sink-link caps negotiation during preroll
		 * and makes playbin fail with a "not-linked" stream error.  With
		 * the elements in the zoombin directly, glupload negotiates an
		 * XB24 DMABuf output and playback prerolls. */
		glupload = gst_element_factory_make("glupload", NULL);
		if (glupload != NULL && gst_element_link(glupload, gtkglsink))
		{
			video_sink = gtkglsink;
		}
		else
		{
			if (glupload != NULL)
				gst_object_unref(glupload);
			glupload = NULL;
			video_sink = gtkglsink;
		}
	}
	else
	{
		video_sink = gst_element_factory_make("gtksink", NULL);
		if (video_sink != NULL)
		{
			g_object_set(G_OBJECT(video_sink), "force-aspect-ratio", TRUE, NULL);
			g_object_get(G_OBJECT(video_sink), "widget", &m_pVideoSinkWidget, NULL);
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

		/* NVIDIA: nvvidconv crops via its native left/right/top/bottom
		 * coordinate properties and scales to the caps on its src pad */
		m_pVideoZoomScaler = gst_element_factory_make("nvvidconv", "zoomscaler");
		if (m_pVideoZoomScaler != NULL)
		{
			m_VideoZoomType = VIDEO_ZOOM_NVIDIA;
		}
		/* Intel Media SDK: vapostproc has no crop properties, so the crop is
		 * done with videocrop (which handles VAMemory and system memory) and
		 * vapostproc scales to the caps forced downstream */
		else if ((m_pVideoZoomScaler = gst_element_factory_make("vapostproc", "zoomscaler")) != NULL)
		{
			m_VideoZoomType = VIDEO_ZOOM_MEDIA_SDK;
			m_pVideoCrop = gst_element_factory_make("videocrop", "zoomcrop");
		}
		else
		{
			/* Intel gstreamer-vaapi: vavideoprocess/vaapipostproc expose native
			 * crop-* properties and scale to the caps forced downstream */
			static const gchar * const vaapi_scalers[] = { "vavideoprocess", "vaapipostproc" };
			for (guint i = 0; i < G_N_ELEMENTS(vaapi_scalers); ++i)
			{
				m_pVideoZoomScaler = gst_element_factory_make(vaapi_scalers[i], "zoomscaler");
				if (m_pVideoZoomScaler == NULL)
					continue;
				GObjectClass *klass = G_OBJECT_GET_CLASS(m_pVideoZoomScaler);
				if (g_object_class_find_property(klass, "crop-left") != NULL &&
				    g_object_class_find_property(klass, "crop-top") != NULL)
				{
					m_VideoZoomType = VIDEO_ZOOM_VAAPI;
					break;
				}
				gst_object_unref(m_pVideoZoomScaler);
				m_pVideoZoomScaler = NULL;
			}
			if (m_VideoZoomType == VIDEO_ZOOM_SOFTWARE)
			{
				/* software fallback */
				m_pVideoZoomScaler = gst_element_factory_make("videoscale", "zoomscaler");
				m_pVideoCrop = gst_element_factory_make("videocrop", "zoomcrop");
				m_pVideoZoomConvert = gst_element_factory_make("videoconvert", "zoomconvert");
			}
		}

		/* the capsfilter after the scaler forces the output memory type and,
		 * when zoomed, the scaled frame size.  It must always be present:
		 * without a forced output the Intel elements can leave the pipeline
		 * unable to preroll */
		m_pVideoZoomCaps = gst_element_factory_make("capsfilter", "zoomcaps");

		/* Input converter: two problems must be solved at the bin's
		 * entrance when a HW decoder (e.g. vah264dec) is upstream:
		 *
		 *  1) Template check – playbin verifies the video-sink's sink
		 *     pad *template* caps intersect the decoder output before
		 *     linking.  videocrop's template is plain video/x-raw
		 *     (system memory only), which excludes VAMemory / DMABuf.
		 *
		 *  2) Runtime conversion – even if templates matched, videocrop
		 *     cannot process VAMemory buffers; an element that actually
		 *     converts the memory type is required.
		 *
		 * vapostproc solves both: its sink template includes
		 * video/x-raw(memory:VAMemory) (and DMABuf), and during caps
		 * negotiation it sees that downstream (videocrop) only accepts
		 * system-memory video/x-raw, so it converts accordingly.
		 *
		 * Fallback: vaapipostproc (older VA stack) or capsfilter (for
		 * pure software-decoder systems where no conversion is needed). */
		m_pVideoZoomInput = gst_element_factory_make("vapostproc", "zoominput");
		if (m_pVideoZoomInput == NULL)
			m_pVideoZoomInput = gst_element_factory_make("vaapipostproc", "zoominput");
		if (m_pVideoZoomInput == NULL)
			m_pVideoZoomInput = gst_element_factory_make("capsfilter", "zoominput");

		/* playbin links its custom video-sink using the sink's template
		 * caps, and the hardware decoder only emits VAMemory.  A plain
		 * videocrop/vapostproc first element exposes a system-memory-only
		 * template, so playbin cannot link it to the decoder.  Prepend a
		 * capsfilter whose caps accept both VAMemory and system memory to
		 * widen the chain's sink template */
		if (m_VideoZoomType == VIDEO_ZOOM_MEDIA_SDK || m_VideoZoomType == VIDEO_ZOOM_VAAPI)
		{
			m_pVideoZoomInputCaps = gst_element_factory_make("capsfilter", "zoominputcaps");
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
		}

		/* assemble the chain for the chosen backend:
		 *   software: videocrop -> videoconvert -> videoscale -> capsfilter
		 *   MediaSDK: videocrop -> vapostproc -> capsfilter
		 *   VA-API:   vaapipostproc -> capsfilter
		 *   NVIDIA:   nvvidconv -> capsfilter */
		GstElement *chain[9] = { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL };
		guint n_chain = 0;
		/* input capsfilter is always first: it provides the permissive
		 * sink template that lets playbin link hardware decoders */
		if (m_pVideoZoomInput != NULL)
			chain[n_chain++] = m_pVideoZoomInput;
		switch (m_VideoZoomType)
		{
			case VIDEO_ZOOM_SOFTWARE:
				chain[n_chain++] = m_pVideoCrop;
				chain[n_chain++] = m_pVideoZoomConvert;
				chain[n_chain++] = m_pVideoZoomScaler;
				chain[n_chain++] = m_pVideoZoomCaps;
				break;
			case VIDEO_ZOOM_MEDIA_SDK:
				if (m_pVideoZoomInputCaps != NULL)
					chain[n_chain++] = m_pVideoZoomInputCaps;
				chain[n_chain++] = m_pVideoCrop;
				chain[n_chain++] = m_pVideoZoomScaler;
				chain[n_chain++] = m_pVideoZoomCaps;
				break;
			case VIDEO_ZOOM_VAAPI:
				if (m_pVideoZoomInputCaps != NULL)
					chain[n_chain++] = m_pVideoZoomInputCaps;
				chain[n_chain++] = m_pVideoZoomScaler;
				chain[n_chain++] = m_pVideoZoomCaps;
				break;
			case VIDEO_ZOOM_NVIDIA:
				chain[n_chain++] = m_pVideoZoomScaler;
				chain[n_chain++] = m_pVideoZoomCaps;
				break;
		}
		/* the GL upload goes directly into the zoombin before the sink so
		 * playsink sees the sink chain with glupload's negotiated caps; see
		 * the comment at the glupload creation above */
		if (glupload != NULL && video_sink == gtkglsink)
		{
			chain[n_chain++] = glupload;
		}
		chain[n_chain++] = video_sink;
		first_element = chain[0];

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
		// A GtkLayout (a scrolling canvas whose size request is always 0,
		// independent of its children) is the video container.  GtkFixed and
		// GtkScrolledWindow both report the child's minimum size in their own
		// request, so the zoomed-up sink widget's size request propagated to
		// the stack and pinned the window minimum -- the window could no
		// longer be resized smaller (and grew as you zoomed in).  GtkLayout
		// never contributes its children's requests to the window minimum,
		// fills the viewer area via hexpand/vexpand (its allocation is the
		// zoom math's "area" size), and clips the oversized sink widget to
		// its own allocation via its bin window.
		m_pVideoFixed = gtk_layout_new(NULL, NULL);
		gtk_widget_set_no_show_all(m_pVideoFixed, TRUE);
		gtk_widget_set_hexpand(m_pVideoFixed, TRUE);
		gtk_widget_set_vexpand(m_pVideoFixed, TRUE);
		gtk_stack_add_named(GTK_STACK(m_pStack), m_pVideoFixed, "video");
		gtk_widget_show(m_pVideoFixed);

		gtk_widget_set_size_request(m_pVideoSinkWidget, 1, 1);
		gtk_layout_put(GTK_LAYOUT(m_pVideoFixed), m_pVideoSinkWidget, 0, 0);
		gtk_widget_show(m_pVideoSinkWidget);

		gtk_widget_add_events(m_pVideoFixed, GDK_POINTER_MOTION_MASK | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK);
		g_signal_connect(G_OBJECT(m_pVideoFixed), "button-press-event", G_CALLBACK(viewer_button_press_cb), this);
		g_signal_connect(G_OBJECT(m_pVideoFixed), "button-release-event", G_CALLBACK(viewer_button_release_cb), this);
		g_signal_connect(G_OBJECT(m_pVideoFixed), "motion-notify-event", G_CALLBACK(viewer_motion_notify), this);
		g_signal_connect(G_OBJECT(m_pVideoFixed), "scroll-event", G_CALLBACK(viewer_scrollwheel_event), this);
		g_signal_connect(G_OBJECT(m_pVideoFixed), "popup-menu", G_CALLBACK(viewer_popup_menu_cb), this);
		// The GL sink widget has its own GdkWindow, so everything the pointer
		// does while over the video lands on the sink widget rather than the
		// fixed; wire both so scroll/click/pan work over the video as well as
		// over the letterboxed margins (and so a pan drag keeps tracking).
		gtk_widget_add_events(m_pVideoSinkWidget, GDK_POINTER_MOTION_MASK | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK);
		g_signal_connect(G_OBJECT(m_pVideoSinkWidget), "button-press-event", G_CALLBACK(viewer_button_press_cb), this);
		g_signal_connect(G_OBJECT(m_pVideoSinkWidget), "button-release-event", G_CALLBACK(viewer_button_release_cb), this);
		g_signal_connect(G_OBJECT(m_pVideoSinkWidget), "motion-notify-event", G_CALLBACK(viewer_motion_notify), this);
		g_signal_connect(G_OBJECT(m_pVideoSinkWidget), "scroll-event", G_CALLBACK(viewer_scrollwheel_event), this);
		g_signal_connect(G_OBJECT(m_pVideoSinkWidget), "popup-menu", G_CALLBACK(viewer_popup_menu_cb), this);
		// whenever the video appears, make sure the media controls stack above
		// the sink's GdkWindow so the timeline and volume button stay clickable
		g_signal_connect(G_OBJECT(m_pVideoSinkWidget), "map", G_CALLBACK(video_zoom_sink_map_cb), this);
		// re-fit the video display when the viewer area is resized
		g_signal_connect(G_OBJECT(m_pVideoFixed), "size-allocate", G_CALLBACK(video_zoom_resize_cb), this);
	}


	gdouble volume = 0.;
	g_object_get(G_OBJECT(m_pPipeline), "volume", &volume, NULL);
	gtk_scale_button_set_value(GTK_SCALE_BUTTON(m_pVolumeButton), volume);

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
	
	gtk_widget_destroy(m_ViewerImplPtr->m_pNavigationWindow);
}

GtkWidget *Viewer::GetWidget()
{
	return m_ViewerImplPtr->m_pHBox;
}

void Viewer::SetImageList(ImageListPtr imgList)
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
	gint cursor_cell = quiver_icon_view_get_cursor_cell(QUIVER_ICON_VIEW(m_ViewerImplPtr->m_pIconView));
	if (0 == m_ViewerImplPtr->m_ImageListPtr->GetSize() || m_ViewerImplPtr->m_QuiverFileCurrent != m_ViewerImplPtr->m_ImageListPtr->GetCurrent())
	{
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

	gtk_widget_show(m_ViewerImplPtr->m_pHBox);

	m_ViewerImplPtr->m_ImageListPtr->UnblockHandler(m_ViewerImplPtr->m_ImageListEventHandlerPtr);

}

void Viewer::Hide()
{
	m_ViewerImplPtr->StopVideo(true);
	SlideShowStop();
	
	gtk_widget_hide(m_ViewerImplPtr->m_pHBox);

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
#ifdef QUIVER_MAEMO
	QuiverUtils::AddSimpleAction(ACTION_VIEWER_ZOOM_IN_MAEMO, "F7", viewer_action_handler_cb, m_ViewerImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_VIEWER_ZOOM_OUT_MAEMO, "F8", viewer_action_handler_cb, m_ViewerImplPtr.get());
#endif
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

	/* Viewer toggle actions */
	QuiverUtils::AddToggleAction(ACTION_VIEWER_VIEW_FILM_STRIP, "f", FALSE, viewer_action_handler_cb, m_ViewerImplPtr.get());
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
	return pViewerImpl->m_ImageListPtr->GetSize();
}

static GdkPixbuf* icon_pixbuf_callback(QuiverIconView *iconview, gulong cell, gpointer user_data)
{
	Viewer::ViewerImpl* pViewerImpl = (Viewer::ViewerImpl*)user_data;
	QuiverFile f = (*pViewerImpl->m_ImageListPtr)[cell];

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
	
	pixbuf = pViewerImpl->m_ThumbnailCache.GetPixbuf((*pViewerImpl->m_ImageListPtr)[cell].GetURI());

	if (pixbuf)
	{
		*actual_width = (*pViewerImpl->m_ImageListPtr)[cell].GetWidth();
		*actual_height = (*pViewerImpl->m_ImageListPtr)[cell].GetHeight();

		if (4 < (*pViewerImpl->m_ImageListPtr)[cell].GetOrientation())
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
						
				gdk_rgba_parse(&color, strBGColorThumb.c_str());
				set_widget_bg_color(parent->m_pIconView, &color);
				
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
				GdkRGBA color;
				
				string strBGColorThumb = prefsPtr->GetString(QUIVER_PREFS_APP,QUIVER_PREFS_APP_BG_ICONVIEW);						

				gdk_rgba_parse(&color, strBGColorThumb.c_str());
				set_widget_bg_color(parent->m_pIconView, &color);
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
		return (*m_pViewerImpl->m_ImageListPtr)[index];
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
