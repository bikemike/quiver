#include <config.h>
#include <algorithm> // For std::min, std::max

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h> // For GDK_KEY_Left etc.

#include <libquiver/quiver-icon-view.h>
#include <libquiver/quiver-image-view.h>
#include <libquiver/quiver-pixbuf-utils.h>
#include <libquiver/quiver-navigation-control.h>

#include <gst/gst.h>
#include <gst/video/video.h>
// #include <gst/gtk/gtksink.h> // GstGtkSink is GTK3, for GTK4 use GtkVideo or similar

#include "Viewer.h"
#include "Timer.h"
#include "icons/nav_button.xpm"
#include "IconManager.h"

// #include "QuiverUtils.h" // Contains GtkUIManager helpers, commented out for now
#include "QuiverVideoOps.h"
#include "ImageLoader.h"
#include "ImageList.h"

#include "QuiverFile.h"

#include "QuiverPrefs.h"
#include "IPreferencesEventHandler.h"

// #include "QuiverStockIcons.h" // Stock items are deprecated, and QuiverStockIcons.cpp is currently commented out
#include "QuiverFileOps.h"

#include "IImageListEventHandler.h"

#include "Statusbar.h"

#include "IPixbufLoaderObserver.h"
#include "IconViewThumbLoader.h"


#include <libexif/exif-utils.h>

#include <iostream> // For cout (debugging, should be removed later)

using namespace std;

#define ORIENTATION_ROTATE_CW	0
#define ORIENTATION_ROTATE_CCW 	1
#define ORIENTATION_FLIP_H    	2
#define ORIENTATION_FLIP_V    	3

#define SLIDESHOW_WAIT_DURATION 100 // milliseconds

/*
static int orientation_matrix[4][9] =
{
	{0,6,7,8,5,2,3,4,1}, //cw rotation
	{0,8,5,6,7,4,1,2,3}, //ccw rotation
	{0,2,1,4,3,6,5,8,7}, //flip h
	{0,4,3,2,1,8,7,6,5}, //flip v
};
*/

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

static GdkPixbuf* icon_pixbuf_callback(QuiverIconView *iconview, guint cell,gpointer user_data);
static GdkPixbuf* thumbnail_pixbuf_callback(QuiverIconView *iconview, guint cell, gint* actual_width, gint* actual_height, gpointer user_data);
static guint n_cells_callback(QuiverIconView *iconview, gpointer user_data);
static void image_view_adjustment_changed (GtkAdjustment *adjustment, gpointer user_data);

static void viewer_action_handler_cb(GAction *action, GVariant *parameter, gpointer user_data);
static void viewer_toggle_action_handler_cb(GAction *action, GVariant *state, gpointer user_data);
static void viewer_zoom_action_handler_cb(GAction *action, GVariant *state, gpointer user_data);

// static void viewer_radio_action_handler_cb(GtkRadioAction *action, GtkRadioAction *current, gpointer user_data); // Commented out - GtkRadioAction is deprecated
// static void viewer_action_handler_cb(GtkAction *action, gpointer data); // Commented out - GtkAction is deprecated

// static gboolean viewer_scrollwheel_event(GtkWidget *widget, GdkEventScroll *event, gpointer data ); // GTK3
static gboolean viewer_scrollwheel_event_cb(GtkEventControllerScroll* controller, gdouble dx, gdouble dy, gpointer data); // GTK4 scroll event
static void viewer_imageview_activated(QuiverImageView *imageview,gpointer data);
static void viewer_imageview_reload(QuiverImageView *imageview,gpointer data);
static void viewer_imageview_magnification_changed(QuiverImageView *imageview,gpointer data);
static void viewer_imageview_view_mode_changed(QuiverImageView *imageview,gpointer data);
// static gboolean viewer_imageview_key_press_event(GtkWidget *imageview, GdkEventKey *event, gpointer userdata); // GTK3
static gboolean viewer_imageview_key_press_event_cb(GtkEventControllerKey* controller, guint keyval, guint keycode, GdkModifierType state, gpointer userdata);


static void viewer_iconview_cell_activated(QuiverIconView *iconview,gulong cell,gpointer data);
static void viewer_iconview_cursor_changed(QuiverIconView *iconview,gulong cell,gpointer data);

static void viewer_volume_value_changed (GtkScaleButton *button, gdouble value, gpointer user_data);

// static gboolean viewer_navigation_button_press_event(GtkWidget *widget, GdkEventButton *event, gpointer userdata); // GdkEventButton is GTK3
// static gboolean navigation_control_button_release_event (GtkWidget *widget, GdkEventButton *event, gpointer data ); // GdkEventButton is GTK3
// Event controllers for navigation control gestures will be needed.

// popup menu callbacks - GtkMenu is deprecated, use GtkPopoverMenu
// static gboolean viewer_popup_menu_cb (GtkWidget *treeview, gpointer userdata);
// static gboolean viewer_button_press_cb(GtkWidget   *widget, GdkEventButton *event, gpointer user_data);
// static gboolean viewer_button_release_cb(GtkWidget   *widget, GdkEventButton *event, gpointer user_data);
// static void viewer_show_context_menu(GdkEventButton *event, gpointer userdata);


// drag/drop targets - GtkTargetEntry and GdkDragContext are part of GTK3 DND. GTK4 uses GdkDrop, GdkDrag, GtkDropTarget, GtkDragSource.
/*
enum {
	QUIVER_TARGET_STRING,
	QUIVER_TARGET_URI
};

static GtkTargetEntry quiver_drag_target_table[] = {
		{ "STRING",     0, QUIVER_TARGET_STRING },
		{ "text/plain", 0, QUIVER_TARGET_STRING },
		 { "text/uri-list", 0, QUIVER_TARGET_URI },
};

static void signal_drag_data_get  (GtkWidget *widget, GdkDragContext *context, GtkSelectionData *selection_data, guint info, guint time,gpointer user_data);
static void signal_drag_data_delete  (GtkWidget *widget,GdkDragContext *context,gpointer user_data);
static void signal_drag_data_received(GtkWidget *widget,GdkDragContext *drag_context, gint x,gint y, GtkSelectionData *data, guint info, guint time,gpointer user_data);
static void signal_drag_begin (GtkWidget *widget,GdkDragContext *drag_context,gpointer user_data);
static void signal_drag_end(GtkWidget *widget,GdkDragContext *drag_context,gpointer user_data);
static void signal_drag_motion (GtkWidget *widget, GdkDragContext *context, gint x, gint y, guint time, gpointer user_data);
static gboolean signal_drag_drop (GtkWidget *widget, GdkDragContext *drag_context, gint x, gint y, guint time,  gpointer user_data);
*/

static gboolean timeout_play_position (gpointer data);

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
#define ACTION_VIEWER_NEXT_2            ACTION_VIEWER_NEXT "_2"
#define ACTION_VIEWER_PREVIOUS_2        ACTION_VIEWER_PREVIOUS "_2"
#define ACTION_VIEWER_ROTATE_CW_2       ACTION_VIEWER_ROTATE_CW "_2"
#define ACTION_VIEWER_ROTATE_CCW_2      ACTION_VIEWER_ROTATE_CCW "_2"
#define ACTION_VIEWER_ROTATE_FOR_BEST_FIT "MaximizeForDisplay"
#define ACTION_VIEWER_FLIP_H_2          ACTION_VIEWER_FLIP_H "_2"
#define ACTION_VIEWER_FLIP_V_2          ACTION_VIEWER_FLIP_V "_2"

#define ACTION_VIEWER_VIDEO_PLAY         "VideoPlay"
#define ACTION_VIEWER_VIDEO_SKIP_FORWARD "VideoSkipForward"
#define ACTION_VIEWER_VIDEO_SKIP_BACK    "VideoSkipBack"
#define ACTION_VIEWER_VIDEO_PLAY_2       ACTION_VIEWER_VIDEO_PLAY "_2"


// GtkUIManager related UI definition string - will need replacement with GMenuModel / GtkBuilder
/*
static const char *ui_viewer =
"<ui>"
#ifdef QUIVER_MAEMO
"	<popup name='MenubarMain'>"
#else
"	<menubar name='MenubarMain'>"
#endif
// ... (rest of UI string commented out as it's very long and all GtkUIManager based) ...
"</ui>";
*/

// GtkActionEntry arrays are deprecated
/*
static  GtkToggleActionEntry action_entries_toggle[] = {
	{ ACTION_VIEWER_VIEW_FILM_STRIP, "","Film Strip", "<Control><Shift>f", "Show/Hide Film Strip", G_CALLBACK(viewer_action_handler_cb),TRUE},
	{ ACTION_VIEWER_ROTATE_FOR_BEST_FIT, "","Rotate to Maximize View", "", "Rotate Images to Maximize Display Area", G_CALLBACK(viewer_action_handler_cb),TRUE},
};

static const gchar* pszActionsImage[] =
{
	ACTION_VIEWER_CUT,
	ACTION_VIEWER_COPY,
	ACTION_VIEWER_TRASH,
	ACTION_VIEWER_ZOOM_FIT,
	ACTION_VIEWER_ZOOM_FIT_STRETCH,
	ACTION_VIEWER_ZOOM_100,
	ACTION_VIEWER_ZOOM_IN,
	ACTION_VIEWER_ZOOM_OUT,
	ACTION_VIEWER_ROTATE_FOR_BEST_FIT,
	ACTION_VIEWER_ROTATE_CW,
	ACTION_VIEWER_ROTATE_CCW,
	ACTION_VIEWER_ROTATE_CW_2,
	ACTION_VIEWER_ROTATE_CCW_2,
	ACTION_VIEWER_FLIP_H,
	ACTION_VIEWER_FLIP_V,
	ACTION_VIEWER_FLIP_H_2,
	ACTION_VIEWER_FLIP_V_2,
};

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


static GtkActionEntry action_entries[] = {

//	{ "MenuFile", NULL, N_("_File") },
	{ ACTION_VIEWER_CUT, QUIVER_STOCK_CUT, "_Cut", "<Control>X", "Cut image", G_CALLBACK(viewer_action_handler_cb)},
	{ ACTION_VIEWER_COPY, QUIVER_STOCK_COPY, "Copy", "<Control>C", "Copy image", G_CALLBACK(viewer_action_handler_cb)},
// #ifdef QUIVER_MAEMO
//	{ ACTION_VIEWER_TRASH, QUIVER_STOCK_DELETE, "_Delete", "Delete", "Delete image", G_CALLBACK(viewer_action_handler_cb)},
// #else
	{ ACTION_VIEWER_TRASH, QUIVER_STOCK_DELETE, "_Move To Trash", "Delete", "Move image to the Trash", G_CALLBACK(viewer_action_handler_cb)},
// #endif

	{ ACTION_VIEWER_PREVIOUS, QUIVER_STOCK_GO_BACK, "_Previous Image", "BackSpace", "Go to previous image", G_CALLBACK(viewer_action_handler_cb)},
	{ ACTION_VIEWER_PREVIOUS_2, QUIVER_STOCK_GO_BACK, "_Previous Image", "<Shift>space", "Go to previous image", G_CALLBACK(viewer_action_handler_cb)},
	{ ACTION_VIEWER_NEXT, QUIVER_STOCK_GO_FORWARD, "_Next Image", "space", "Go to next image", G_CALLBACK(viewer_action_handler_cb)},
	{ ACTION_VIEWER_NEXT_2, QUIVER_STOCK_GO_FORWARD, "_Next Image", "<Shift>BackSpace", "Go to next image", G_CALLBACK(viewer_action_handler_cb)},
	{ ACTION_VIEWER_FIRST, QUIVER_STOCK_GOTO_FIRST, "_First Image", "Home", "Go to first image", G_CALLBACK(viewer_action_handler_cb)},
	{ ACTION_VIEWER_LAST, QUIVER_STOCK_GOTO_LAST, "_Last Image", "End", "Go to last image", G_CALLBACK(viewer_action_handler_cb)},

	{ ACTION_VIEWER_ZOOM_IN, QUIVER_STOCK_ZOOM_IN,"Zoom _In", "equal", "Zoom In", G_CALLBACK(viewer_action_handler_cb)},
	{ ACTION_VIEWER_ZOOM_OUT, QUIVER_STOCK_ZOOM_OUT,"Zoom _Out", "minus", "Zoom Out", G_CALLBACK(viewer_action_handler_cb)},

	{ ACTION_VIEWER_ROTATE_CW, QUIVER_STOCK_ROTATE_CW, "_Rotate Clockwise", "r", "Rotate Clockwise", G_CALLBACK(viewer_action_handler_cb)},
	{ ACTION_VIEWER_ROTATE_CW_2, QUIVER_STOCK_ROTATE_CW, "_Rotate Clockwise", "<Shift>l", "Rotate Clockwise", G_CALLBACK(viewer_action_handler_cb)},
	{ ACTION_VIEWER_ROTATE_CCW, QUIVER_STOCK_ROTATE_CCW, "Rotate _Counterclockwise", "l", "Rotate Counterclockwise", G_CALLBACK(viewer_action_handler_cb)},
	{ ACTION_VIEWER_ROTATE_CCW_2, QUIVER_STOCK_ROTATE_CCW, "Rotate _Counterclockwise", "<Shift>r", "Rotate Counterclockwise", G_CALLBACK(viewer_action_handler_cb)},
	{ ACTION_VIEWER_FLIP_H, "", "Flip _Horizontally", "h", "Flip Horizontally", G_CALLBACK(viewer_action_handler_cb)},
	{ ACTION_VIEWER_FLIP_H_2, "", "Flip _Horizontally", "<Shift>v", "Flip Horizontally", G_CALLBACK(viewer_action_handler_cb)},
	{ ACTION_VIEWER_FLIP_V, "", "Flip _Vertically", "v", "Flip Vertically", G_CALLBACK(viewer_action_handler_cb)},
	{ ACTION_VIEWER_FLIP_V_2, "", "Flip _Vertically", "<Shift>h", "Flip Vertically", G_CALLBACK(viewer_action_handler_cb)},
	{ ACTION_VIEWER_VIDEO_PLAY, "", "Play/Pause Video", "P", "Play/Pause Video", G_CALLBACK(viewer_action_handler_cb)},
	{ ACTION_VIEWER_VIDEO_PLAY_2, "", "Play/Pause Video", "<Control>space", "Play/Pause Video", G_CALLBACK(viewer_action_handler_cb)},
	{ ACTION_VIEWER_VIDEO_SKIP_FORWARD, "", "Skip Forward", "period", "Skip Forward", G_CALLBACK(viewer_action_handler_cb)},
	{ ACTION_VIEWER_VIDEO_SKIP_BACK, "", "Skip Back", "comma", "Skip Back", G_CALLBACK(viewer_action_handler_cb)},
};


static GtkRadioActionEntry zoom_radio_action_entries[] = { // GtkRadioAction is deprecated
	{ ACTION_VIEWER_ZOOM_FIT, QUIVER_STOCK_ZOOM_FIT,"Zoom _Fit", "<Control>1", "Fit to Window",QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW},
	{ ACTION_VIEWER_ZOOM_FIT_STRETCH, QUIVER_STOCK_ZOOM_FIT,"Zoom _Fit Stretch", "", "Fit to Window Stretch",QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW_STRETCH},
	{ ACTION_VIEWER_ZOOM_100, QUIVER_STOCK_ZOOM_100, "_Actual Size", "<Control>0", "Actual Size",QUIVER_IMAGE_VIEW_MODE_ACTUAL_SIZE},
	{ ACTION_VIEWER_ZOOM_FILL_SCREEN, "", "Fill Screen", NULL, "Fill the screen with the image",QUIVER_IMAGE_VIEW_MODE_FILL_SCREEN},
	{ ACTION_VIEWER_ZOOM, "", NULL, NULL, NULL, QUIVER_IMAGE_VIEW_MODE_ZOOM},
};
*/


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
		// gdk_threads_enter(); // GTK4: UI updates must be on main thread.
		quiver_image_view_set_pixbuf(m_pImageView,pixbuf);
		// gdk_threads_leave();
	};
	virtual void SetPixbufAtSize(GdkPixbuf *pixbuf, gint width, gint height, bool bResetViewMode = true ){
		// gdk_threads_enter(); // GTK4: UI updates must be on main thread.
		gboolean bReset = bResetViewMode ? TRUE : FALSE;
		quiver_image_view_set_pixbuf_at_size_ex(m_pImageView,pixbuf,width,height,bReset);
		// gdk_threads_leave();
	};

	virtual void SignalBytesRead(long bytes_read,long total){};
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
	void UpdateUI(); // Was for GtkUIManager, may need rework or removal for GAction/GMenuModel

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
			// gtk_image_set_from_pixbuf(GTK_IMAGE(m_pPlayImage), m_pPixbufPause); // GtkImage is for static images
			gtk_image_set_from_icon_name(GTK_IMAGE(m_pPlayImage), "media-playback-pause-symbolic");


			m_iTimeoutPlayProgress = g_timeout_add(200,timeout_play_position,this);
		}
		else
		{
			// gtk_image_set_from_pixbuf(GTK_IMAGE(m_pPlayImage), m_pPixbufPlay); // GtkImage is for static images
			gtk_image_set_from_icon_name(GTK_IMAGE(m_pPlayImage), "media-playback-start-symbolic");
		}
	}

	// methods for playing videos
	void PlayPauseVideo();
	void SkipForward();
	void SkipBack();
	void UpdateTimeline();
	void StopVideo(bool reloadImage = true);
	// returns true if current item is a video
	bool IsVideo();


// member variables

	GtkWidget *m_pIconView;
	GtkWidget *m_pImageView;

	GtkWidget * m_pGrid;
	GtkAdjustment * m_pAdjustmentH;
	GtkAdjustment * m_pAdjustmentV;

	GtkWidget * m_pScrollbarH;
	GtkWidget * m_pScrollbarV;

	GtkWidget *m_pNavigationBox; // This was an event box for the nav button, might simplify

	GtkWidget *m_pHBox; // Main horizontal box
	GtkWidget *m_pVBox; // Vertical box for image area + filmstrip (if top/bottom)

	gdouble m_dAdjustmentValueLastH;
	gdouble m_dAdjustmentValueLastV;

	GtkWidget *m_pNavigationWindow; // A popup window for navigation control
	GtkWidget *m_pNavigationControl; // The custom QuiverNavigationControl widget

	GtkWidget* m_pMediaControls; // Box for media controls
	GtkWidget* m_pPlayButton;    // The GtkButton for play/pause
	GtkWidget* m_pPlayImage;     // GtkImage for play/pause button icon
	GtkWidget* m_pTimeline;      // Box for timeline controls (progress, label)
	GtkWidget* m_pTimeLabel;
	GtkWidget* m_pPlayProgress;       // GtkProgressBar
	GtkWidget* m_pPlayProgressEventBox; // EventBox for progress bar interaction
	GtkWidget* m_pVolumeButton;       // GtkVolumeButton (deprecated) -> GtkScaleButton or custom

	// GdkPixbuf* m_pPixbufPlayHighlight; // Not needed if using icon names
	// GdkPixbuf* m_pPixbufPlay;
	// GdkPixbuf* m_pPixbufPauseHighlight;
	// GdkPixbuf* m_pPixbufPause;

	QuiverFile m_QuiverFileCurrent;

	int m_iCurrentOrientation;

	// GtkUIManager *m_pUIManager; // Deprecated - Commented out
	guint m_iMergedViewerUI; // Related to GtkUIManager, will likely be removed or repurposed

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
	guint m_iTimeoutClickID; // Maemo specific, can remove
	guint m_iTimeoutMouseMotionNotify;
	guint m_iTimeoutPlayProgress;

	int   m_iSlideShowDuration;
	int   m_iSlideShowWaitCount;
	bool  m_bSlideShowLoop;

	bool m_bMaximizeViewableArea;
	bool m_bMaximizeViewabe; // Typo? Should be m_bMaximizeViewableArea?

	ImageCache m_ThumbnailCache;


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
	GtkCssProvider*              m_pCssProvider;
	bool m_bIsPlaying;
	bool m_bWasPlayingBeforeSeek;
	ViewerThumbLoader            m_ThumbnailLoader;
    GSimpleActionGroup *m_pActionGroup;
    GtkWidget* m_pContextMenu;
	// gstreamer elements for playing videos
	GstElement* m_pPipeline;
	GtkWidget*  m_pVideoWidget; // The widget for video display (e.g., GtkVideo)
};

void Viewer::ViewerImpl::SetImageList(ImageListPtr imgList)
{
	if (m_ImageListPtr) { // Check if it was already set
		m_ImageListPtr->RemoveEventHandler(m_ImageListEventHandlerPtr);
	}

	m_ImageListPtr = imgList;

	if (m_ImageListPtr) { // Check if successfully set
		m_ImageListPtr->AddEventHandler(m_ImageListEventHandlerPtr);
	}
}

static void viewer_action_handler_cb(GAction* /*action*/, GVariant* /*parameter*/, gpointer /*user_data*/)
{
    // Viewer::ViewerImpl *pViewerImpl = (Viewer::ViewerImpl *)user_data;
    // const gchar *name = g_action_get_name(action);

    // TODO: Implement handlers for stateless actions
}

static void viewer_toggle_action_handler_cb(GAction *action, GVariant *state, gpointer user_data)
{
    Viewer::ViewerImpl *pViewerImpl = (Viewer::ViewerImpl *)user_data;
    const gchar *name = g_action_get_name(action);
    gboolean is_active = g_variant_get_boolean(state);

    if (strcmp(name, ACTION_VIEWER_VIEW_FILM_STRIP) == 0)
    {
        gtk_widget_set_visible(pViewerImpl->m_pIconView, is_active);
        Preferences::GetInstance()->SetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_FILMSTRIP_SHOW, is_active);
    }
    else if (strcmp(name, ACTION_VIEWER_ROTATE_FOR_BEST_FIT) == 0)
    {
        pViewerImpl->m_bMaximizeViewableArea = is_active;
        Preferences::GetInstance()->SetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_ROTATE_FOR_BEST_FIT, is_active);
    }

    g_action_change_state(action, state);
}

static void viewer_zoom_action_handler_cb(GAction *action, GVariant *state, gpointer user_data)
{
    Viewer::ViewerImpl *pViewerImpl = (Viewer::ViewerImpl *)user_data;
    // const gchar *name = g_action_get_name(action);

    QuiverImageViewMode mode = (QuiverImageViewMode)g_variant_get_int32(state);
    quiver_image_view_set_view_mode(QUIVER_IMAGE_VIEW(pViewerImpl->m_pImageView), mode);

    g_action_change_state(action, state);
}
// has image


void Viewer::ViewerImpl::UpdateUI()
{
	// This function was heavily reliant on GtkUIManager and GtkAction.
	// For GTK4, this would involve updating GAction states or GMenuModel items.
	// For now, commenting out the GtkUIManager/GtkAction specific parts.
	// The logic for enabling/disabling next/prev actions might be reusable with GAction.

	/*
	if (m_ImageListPtr->GetSize())
	{
		// ... (logic for pszActionsNext/Prev based on image list state) ...
		// QuiverUtils::SetActionsSensitive(m_pUIManager, pszActionsImage, G_N_ELEMENTS(pszActionsImage), TRUE);
	}
	else
	{
		// QuiverUtils::SetActionsSensitive(m_pUIManager, pszActionsPrev, G_N_ELEMENTS(pszActionsPrev), FALSE);
		// QuiverUtils::SetActionsSensitive(m_pUIManager, pszActionsNext, G_N_ELEMENTS(pszActionsNext), FALSE);
		// QuiverUtils::SetActionsSensitive(m_pUIManager, pszActionsImage, G_N_ELEMENTS(pszActionsImage), FALSE);
	}

	if (!quiver_image_view_can_magnify(QUIVER_IMAGE_VIEW(m_pImageView), TRUE))
	{
		// GtkAction* action;
		// action = QuiverUtils::GetAction(m_pUIManager, ACTION_VIEWER_ZOOM_IN);
		// if (NULL != action) gtk_action_set_sensitive(action,FALSE);
	}

	if (!quiver_image_view_can_magnify(QUIVER_IMAGE_VIEW(m_pImageView), FALSE))
	{
		// GtkAction* action;
		// action = QuiverUtils::GetAction(m_pUIManager, ACTION_VIEWER_ZOOM_OUT);
		// if (NULL != action) gtk_action_set_sensitive(action,FALSE);
	}

	// PreferencesPtr prefsPtr = Preferences::GetInstance();
	// GtkAction* action;

	if (0 != m_iTimeoutSlideshowID) // Slideshow mode
	{
		// bool bMaximize = prefsPtr->GetBoolean(QUIVER_PREFS_SLIDESHOW, QUIVER_PREFS_SLIDESHOW_ROTATE_FOR_BEST_FIT, false);
		// action = QuiverUtils::GetAction(m_pUIManager, ACTION_VIEWER_ROTATE_FOR_BEST_FIT);
		// if (NULL != action) gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action),bMaximize ? TRUE : FALSE);

		// bool bHideFilmStrip = prefsPtr->GetBoolean(QUIVER_PREFS_SLIDESHOW, QUIVER_PREFS_SLIDESHOW_FILMSTRIP_HIDE, true);
		// action = QuiverUtils::GetAction(m_pUIManager,ACTION_VIEWER_VIEW_FILM_STRIP);
		// if (NULL != action) gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action),bHideFilmStrip ? FALSE : TRUE);
	}
	else // Normal viewer mode
	{
		// bool bMaximize = prefsPtr->GetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_ROTATE_FOR_BEST_FIT, false);
		// action = QuiverUtils::GetAction(m_pUIManager, ACTION_VIEWER_ROTATE_FOR_BEST_FIT);
		// if (NULL != action) gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action),bMaximize ? TRUE : FALSE);

		// bool bShowFilmStrip = prefsPtr->GetBoolean(QUIVER_PREFS_VIEWER,QUIVER_PREFS_VIEWER_FILMSTRIP_SHOW);
		// GtkAction* action = QuiverUtils::GetAction(m_pUIManager,ACTION_VIEWER_VIEW_FILM_STRIP);
		// if (NULL != action) gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action),bShowFilmStrip ? TRUE : FALSE);
	}
	*/
}

void Viewer::ViewerImpl::UpdateScrollbars()
{
	gint image_display_width, image_display_height;
	QuiverImageViewMode view_mode = quiver_image_view_get_view_mode(QUIVER_IMAGE_VIEW(m_pImageView));

	quiver_image_view_get_pixbuf_display_size_for_mode(
		QUIVER_IMAGE_VIEW(m_pImageView),
		view_mode,
		&image_display_width,
		&image_display_height);

	GtkWidget *scrollbar_h = m_pScrollbarH;
	GtkWidget *scrollbar_v = m_pScrollbarV;

	if (NULL == scrollbar_v || NULL == scrollbar_h) return;

	gint sb_v_width = gtk_widget_get_width(scrollbar_v);
	gint sb_h_height = gtk_widget_get_height(scrollbar_h);

	GtkWidget * nav_box = m_pNavigationBox;

	gint area_w = gtk_widget_get_width(m_pGrid);
	gint area_h = gtk_widget_get_height(m_pGrid);

	PreferencesPtr prefsPtr = Preferences::GetInstance();
	bool bHideScrollbars = prefsPtr->GetBoolean(QUIVER_PREFS_VIEWER,QUIVER_PREFS_VIEWER_SCROLLBARS_HIDE);

	bool show_v_scrollbar = false;
	bool show_h_scrollbar = false;

	if (bHideScrollbars || QUIVER_IMAGE_VIEW_MODE_FILL_SCREEN == view_mode)
	{
		// Hide all
	}
	else if ( (area_w < image_display_width && area_h < image_display_height) ||
		(area_w < image_display_width && area_h - sb_h_height < image_display_height) ||
		(area_w - sb_v_width < image_display_width && area_h < image_display_height) )
	{
		show_h_scrollbar = true;
		show_v_scrollbar = true;
	}
	else if (area_w < image_display_width)
	{
		show_h_scrollbar = true;
	}
	else if (area_h < image_display_height)
	{
		show_v_scrollbar = true;
	}

	gtk_widget_set_visible(scrollbar_v, show_v_scrollbar);
	gtk_widget_set_visible(scrollbar_h, show_h_scrollbar);
	gtk_widget_set_visible(nav_box, show_v_scrollbar && show_h_scrollbar);

	m_iTimeoutScrollbars = 0;

	// DND logic related to scrollbar state needs to be re-evaluated with GTK4 DND.
	/*
	GtkAdjustment *h_adj = m_pAdjustmentH;
	GtkAdjustment *v_adj = m_pAdjustmentV;

	if (NULL == h_adj || NULL == v_adj) return;

	if (gtk_adjustment_get_page_size(h_adj) >= gtk_adjustment_get_upper(h_adj) &&
		gtk_adjustment_get_page_size(v_adj) >= gtk_adjustment_get_upper(v_adj))
	{
		// gtk_drag_source_set (m_pImageView, (GdkModifierType)(GDK_BUTTON1_MASK | GDK_BUTTON3_MASK),
		// 		   quiver_drag_target_table, G_N_ELEMENTS(quiver_drag_target_table), (GdkDragAction)( GDK_ACTION_COPY |
		// 	       GDK_ACTION_MOVE | GDK_ACTION_LINK | GDK_ACTION_ASK ));
	}
	else
	{
		gtk_widget_grab_focus(m_pImageView);
		// gtk_drag_source_unset (m_pImageView);
	}
	*/
}

void Viewer::ViewerImpl::CacheImageAtSize(QuiverFile f, int w, int h)
{
	if (m_bMaximizeViewableArea)
	{
		ImageLoader::LoadParams params = {0};
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
			std::swap(in_width,in_height);
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
		ImageLoader::LoadParams params = {0};
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
		QuiverFile f_next_prev;
		bool bGetSize = false;

		if (bDirectionForward)
		{
			if (m_ImageListPtr->HasNext())
			{
				f_next_prev = m_ImageListPtr->GetNext();
				bGetSize = true;
			}
		}
		else
		{
			if (m_ImageListPtr->HasPrevious())
			{
				f_next_prev = m_ImageListPtr->GetPrevious();
				bGetSize = true;
			}
		}

		if (bGetSize)
		{
			int in_width = f_next_prev.GetWidth();
			int in_height = f_next_prev.GetHeight();

			if (4 < GetMaximizedOrientation(f_next_prev,true) )
			{
				std::swap(in_width,in_height);
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
		StopVideo(false); // Stop any video before changing image

		m_pViewer->EmitCursorChangedEvent();

		g_signal_handlers_block_by_func(m_pIconView,(gpointer)viewer_iconview_cursor_changed,this);

		if (IsVideo())
		{
			gtk_widget_set_visible(m_pMediaControls, TRUE);
			gtk_widget_set_visible(m_pImageView, FALSE);
			gtk_widget_set_visible(m_pVideoWidget, TRUE);
			if (m_pPipeline) { // Ensure pipeline exists
				g_object_set(G_OBJECT(m_pPipeline), "uri", m_ImageListPtr->GetCurrent().GetURI(), NULL);
				// Optionally set to PAUSED or READY state until user clicks play
				// gst_element_set_state(GST_ELEMENT(m_pPipeline), GST_STATE_PAUSED);
			}
		}
		else
		{
			gtk_widget_set_visible(m_pMediaControls, FALSE);
			gtk_widget_set_visible(m_pImageView, TRUE);
			gtk_widget_set_visible(m_pVideoWidget, FALSE);
		}

		QuiverFile f = m_ImageListPtr->GetCurrent();
		GdkPixbuf *pixbuf = f.GetThumbnail(128);
		quiver_navigation_control_set_pixbuf(QUIVER_NAVIGATION_CONTROL(m_pNavigationControl),pixbuf);

		if (NULL != pixbuf)
		{
			g_object_unref(pixbuf);
		}

		if (!IsVideo()) { // Only load image if not a video
			LoadImage(f);
		}

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
		QuiverFile f_empty;
		m_QuiverFileCurrent = f_empty;
		quiver_image_view_set_pixbuf(QUIVER_IMAGE_VIEW(m_pImageView),NULL);
	}

}

int  Viewer::ViewerImpl::GetMaximizedOrientation(QuiverFile f, bool bCombinedWithFileOrientation /* = false*/)
{
	int orientation = 1;
	if (m_bMaximizeViewableArea && gtk_widget_get_realized(m_pImageView))
	{
		int aw = f.GetWidth();
		int ah = f.GetHeight();
		if (4 < f.GetOrientation())
		{
			std::swap(aw,ah);
		}

		gint view_width = gtk_widget_get_width(m_pImageView);
		gint view_height = gtk_widget_get_height(m_pImageView);

		if (view_width == 0 || view_height == 0) return orientation;

		double rimg = (ah > 0) ? (aw / (double)ah) : 1.0;
		double rscreen = (view_height > 0) ? (view_width / (double)view_height) : 1.0;

		if ( (rimg < 1 && rscreen < 1) || (rimg >= 1 && rscreen >= 1) )
		{
			orientation = 1;
		}
		else
		{
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
		if (m_ImageListPtr->GetSize() > 0) {
			QuiverFile f = m_ImageListPtr->GetCurrent();
			orientation = combine_matrix[orientation][GetMaximizedOrientation(f)];
		}
	}
	return orientation;
}

void Viewer::ViewerImpl::SetCurrentOrientation(int iOrientation, bool bUpdateExif /*= true*/)
{
	m_iCurrentOrientation = iOrientation;

	if (bUpdateExif && m_ImageListPtr->GetSize() > 0)
	{
		QuiverFile f = m_ImageListPtr->GetCurrent();
		ExifData* pExifData = f.GetExifData();

		if (NULL != pExifData)
		{
			ExifEntry *pExifEntry;
			pExifEntry = exif_content_get_entry (pExifData->ifd[EXIF_IFD_0], EXIF_TAG_ORIENTATION);
			if (!pExifEntry)
			{
				pExifEntry = exif_entry_new ();
				exif_content_add_entry (pExifData->ifd[EXIF_IFD_0], pExifEntry);
				exif_entry_initialize (pExifEntry, EXIF_TAG_ORIENTATION);
			}
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

	GtkBox* target_box = NULL;

	switch (iFilmstripPos)
	{
		case FSTRIP_POS_TOP:
		case FSTRIP_POS_BOTTOM:
			target_box = GTK_BOX(m_pVBox);
			quiver_icon_view_set_n_rows(QUIVER_ICON_VIEW(m_pIconView),1);
			quiver_icon_view_set_n_columns(QUIVER_ICON_VIEW(m_pIconView),0);
			gtk_orientable_set_orientation(GTK_ORIENTABLE(m_pIconView), GTK_ORIENTATION_HORIZONTAL);
			break;
		case FSTRIP_POS_LEFT:
		case FSTRIP_POS_RIGHT:
			target_box = GTK_BOX(m_pHBox);
			quiver_icon_view_set_n_columns(QUIVER_ICON_VIEW(m_pIconView),1);
			quiver_icon_view_set_n_rows(QUIVER_ICON_VIEW(m_pIconView),0);
			gtk_orientable_set_orientation(GTK_ORIENTABLE(m_pIconView), GTK_ORIENTATION_VERTICAL);
			break;
	}

	if (NULL != target_box)
	{
        GtkWidget* old_parent = gtk_widget_get_parent(m_pIconView);
        if (old_parent && GTK_IS_BOX(old_parent)) {
             gtk_box_remove(GTK_BOX(old_parent), m_pIconView);
        }


		if (iFilmstripPos == FSTRIP_POS_TOP || iFilmstripPos == FSTRIP_POS_LEFT) {
			gtk_box_prepend(target_box, m_pIconView);
		} else {
			gtk_box_append(target_box, m_pIconView);
		}
	}
}

static gboolean
timeout_event_motion_notify (gpointer user_data)
{
	Viewer::ViewerImpl *pViewerImpl = (Viewer::ViewerImpl*)user_data;

	if (pViewerImpl->IsPlaying()) {
		gtk_widget_set_visible(pViewerImpl->m_pMediaControls, FALSE);
	}

	pViewerImpl->m_iTimeoutMouseMotionNotify = 0;
	return G_SOURCE_REMOVE;
}


static void viewer_motion_notify_cb(GtkEventControllerMotion* controller, double x, double y, gpointer user_data)
{
	Viewer::ViewerImpl *pViewerImpl = (Viewer::ViewerImpl*)user_data;
    GtkWidget* widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(controller));

	if (widget == pViewerImpl->m_pImageView || widget == pViewerImpl->m_pVideoWidget) // Motion over image or video area
	{
		if (0 != pViewerImpl->m_iTimeoutMouseMotionNotify)
		{
			g_source_remove(pViewerImpl->m_iTimeoutMouseMotionNotify);
			pViewerImpl->m_iTimeoutMouseMotionNotify = 0;
		}

		if (pViewerImpl->IsVideo()) { // Only show controls if current item is video
			gtk_widget_set_visible(pViewerImpl->m_pMediaControls, TRUE);
        }

		if (pViewerImpl->IsPlaying()) { // If playing, set timeout to hide controls
			pViewerImpl->m_iTimeoutMouseMotionNotify = g_timeout_add(1500,timeout_event_motion_notify,pViewerImpl);
        }
	}
	else if (widget == pViewerImpl->m_pPlayProgressEventBox)
	{
        GdkDevice* device = gtk_event_controller_get_current_event_device(GTK_EVENT_CONTROLLER(controller));
        // Check if button 1 is pressed during motion as a proxy for drag, since gdk_device_get_grab_surface is gone
        // and this isn't a full GtkGestureDrag yet.
        if (device && (gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(controller)) & GDK_BUTTON1_MASK))
		{
            gint widget_width = gtk_widget_get_width(widget);

			GstFormat format = GST_FORMAT_TIME;
			gint64 clip_duration = 0;

			gboolean queried = FALSE;
            if (pViewerImpl->m_pPipeline) {
                queried = gst_element_query_duration(GST_ELEMENT(pViewerImpl->m_pPipeline), format, &clip_duration);
            }

			if (queried && widget_width > 0 && pViewerImpl->m_pPipeline)
			{
                gint64 seek_pos = (clip_duration * x) / widget_width;
                gst_element_seek_simple(GST_ELEMENT(pViewerImpl->m_pPipeline), format, (GstSeekFlags)(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT), seek_pos);
			}
		}
	}
}

static void viewer_play_button_mouse_in_cb(GtkEventControllerMotion* /*controller*/, double /*x*/, double /*y*/, gpointer user_data)
{
	Viewer::ViewerImpl *pViewerImpl = (Viewer::ViewerImpl*)user_data;
	if (pViewerImpl->IsPlaying())
        gtk_image_set_from_icon_name(GTK_IMAGE(pViewerImpl->m_pPlayImage), "media-playback-pause-symbolic");
	else
        gtk_image_set_from_icon_name(GTK_IMAGE(pViewerImpl->m_pPlayImage), "media-playback-start-symbolic");
}

static void viewer_play_button_mouse_out_cb(GtkEventControllerMotion* /*controller*/, gpointer user_data)
{
	Viewer::ViewerImpl *pViewerImpl = (Viewer::ViewerImpl*)user_data;
	if (pViewerImpl->IsPlaying())
        gtk_image_set_from_icon_name(GTK_IMAGE(pViewerImpl->m_pPlayImage), "media-playback-pause-symbolic");
	else
        gtk_image_set_from_icon_name(GTK_IMAGE(pViewerImpl->m_pPlayImage), "media-playback-start-symbolic");
}


static gboolean viewer_scrollwheel_event_cb(GtkEventControllerScroll* controller, gdouble /*dx*/, gdouble dy, gpointer data)
{
	Viewer::ViewerImpl *pViewerImpl = (Viewer::ViewerImpl*)data;
    GdkModifierType state = gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(controller));

	if (state & GDK_CONTROL_MASK || state & GDK_SHIFT_MASK)
		return FALSE;

	if (dy < 0)
	{
		pViewerImpl->m_ImageListPtr->Previous();
	}
	else if (dy > 0)
	{
		pViewerImpl->m_ImageListPtr->Next();
	}

	return TRUE;
}


static void viewer_imageview_activated(QuiverImageView* /*imageview*/,gpointer data)
{
	Viewer::ViewerImpl *pViewerImpl = (Viewer::ViewerImpl*)data;
	pViewerImpl->m_pViewer->EmitItemActivatedEvent();
}

static void viewer_imageview_reload(QuiverImageView* /*imageview*/,gpointer data)
{
	Viewer::ViewerImpl *pViewerImpl = (Viewer::ViewerImpl*)data;
	ImageLoader::LoadParams params = {0};

	params.orientation = pViewerImpl->GetCurrentOrientation(true);
	params.reload = true;
	params.fullsize = true;
	params.no_thumb_preview = true;
	params.state = ImageLoader::LOAD;

    if (pViewerImpl->m_ImageListPtr->GetSize() > 0) {
	    pViewerImpl->m_ImageLoader.LoadImage(pViewerImpl->m_ImageListPtr->GetCurrent(),params);
    }
}


static void viewer_imageview_magnification_changed(QuiverImageView* /*imageview*/,gpointer data)
{
	Viewer::ViewerImpl *pViewerImpl = (Viewer::ViewerImpl*)data;
	double mag = quiver_image_view_get_magnification(QUIVER_IMAGE_VIEW(pViewerImpl->m_pImageView));
	if (pViewerImpl->m_StatusbarPtr) {
	    pViewerImpl->m_StatusbarPtr->SetMagnification((int)(mag*100+.5));
    }
}

static void viewer_imageview_view_mode_changed(QuiverImageView* /*imageview*/,gpointer data)
{
	Viewer::ViewerImpl *pViewerImpl = (Viewer::ViewerImpl*)data;

	QuiverImageViewMode unmagnified_mode =
		quiver_image_view_get_view_mode_unmagnified(QUIVER_IMAGE_VIEW(pViewerImpl->m_pImageView));

	PreferencesPtr prefsPtr = Preferences::GetInstance();
	prefsPtr->SetInteger(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_DEFAULT_VIEW_MODE, unmagnified_mode);
}

static gboolean viewer_imageview_key_press_event_cb(GtkEventControllerKey* /*controller*/, guint keyval, guint /*keycode*/, GdkModifierType /*state*/, gpointer userdata)
{
	Viewer::ViewerImpl *pViewerImpl = (Viewer::ViewerImpl*)userdata;
	gboolean rval = FALSE;
	bool bPanMode = true;

	GtkAdjustment *h_adj = pViewerImpl->m_pAdjustmentH;
	GtkAdjustment *v_adj = pViewerImpl->m_pAdjustmentV;

	if (gtk_adjustment_get_page_size(h_adj) >= gtk_adjustment_get_upper(h_adj) &&
		gtk_adjustment_get_page_size(v_adj) >= gtk_adjustment_get_upper(v_adj))
	{
		bPanMode = false;
	}

	GtkAdjustment *adjustment_to_change = NULL;
	gdouble increment = 0.;

	if (GDK_KEY_Left == keyval || GDK_KEY_Up == keyval)
	{
		if (bPanMode)
		{
			if (GDK_KEY_Left == keyval) adjustment_to_change = h_adj;
			else adjustment_to_change = v_adj;
			increment = -gtk_adjustment_get_step_increment(adjustment_to_change);
		}
		else
		{
            pViewerImpl->m_ImageListPtr->Previous();
		}
		rval = TRUE;
	}
	else if (GDK_KEY_Right == keyval || GDK_KEY_Down == keyval)
	{
		if (bPanMode)
		{
			if (GDK_KEY_Right == keyval) adjustment_to_change = h_adj;
			else adjustment_to_change = v_adj;
			increment = gtk_adjustment_get_step_increment(adjustment_to_change);
		}
		else
		{
            pViewerImpl->m_ImageListPtr->Next();
		}
		rval = TRUE;
	}

	if (NULL != adjustment_to_change)
	{
		gdouble value = gtk_adjustment_get_value(adjustment_to_change);
		value += increment;
		value = std::max(value, gtk_adjustment_get_lower(adjustment_to_change));
		value = std::min(value, gtk_adjustment_get_upper(adjustment_to_change) - gtk_adjustment_get_page_size(adjustment_to_change));
		gtk_adjustment_set_value(adjustment_to_change,value);
	}
	return rval;
}

static void viewer_iconview_cell_activated(QuiverIconView* /*iconview*/,gulong /*cell*/,gpointer /*data*/)
{
}

static void viewer_iconview_cursor_changed(QuiverIconView* /*iconview*/,gulong cell,gpointer data)
{
	Viewer::ViewerImpl *pViewerImpl = (Viewer::ViewerImpl*)data;
	bool bDirectionForward = (pViewerImpl->m_ImageListPtr->GetSize() && pViewerImpl->m_ImageListPtr->GetCurrentIndex() < cell);
	pViewerImpl->SetImageIndex(cell,bDirectionForward);
}

static void viewer_volume_value_changed (GtkScaleButton* /*button*/, gdouble value, gpointer user_data)
{
	Viewer::ViewerImpl *pViewerImpl = (Viewer::ViewerImpl*)user_data;
    if (pViewerImpl->m_pPipeline) {
        g_object_set(G_OBJECT(pViewerImpl->m_pPipeline), "volume", value, NULL);
    }
}


static gchar*
gst_time_format(gint64 time_ns)
{
	gint64 secs_total  = time_ns / GST_SECOND;
	gint64 hours = secs_total / 3600;
	gint64 mins  = (secs_total % 3600) / 60;
	gint64 secs  = secs_total % 60;

	gchar* str = NULL;
	if (hours > 0)
		str = g_strdup_printf("%" G_GINT64_FORMAT ":%02" G_GINT64_FORMAT ":%02" G_GINT64_FORMAT, hours, mins, secs);
	else
		str = g_strdup_printf("%" G_GINT64_FORMAT ":%02" G_GINT64_FORMAT, mins, secs);
	return str;
}

static gboolean
timeout_play_position (gpointer data)
{
	Viewer::ViewerImpl *pViewerImpl = (Viewer::ViewerImpl*)data;
	pViewerImpl->UpdateTimeline();
	return G_SOURCE_CONTINUE;
}

static gboolean
gstreamer_bus_watcher(GstBus* /*bus*/, GstMessage* msg, gpointer user_data)
{
	Viewer::ViewerImpl *pViewerImpl = (Viewer::ViewerImpl*)user_data;
    if (!pViewerImpl || !pViewerImpl->m_pPipeline) return TRUE;

	switch (GST_MESSAGE_TYPE (msg)) {
		case GST_MESSAGE_EOS:
			gst_element_set_state(GST_ELEMENT(pViewerImpl->m_pPipeline), GST_STATE_READY);
			pViewerImpl->StopVideo(true);
			break;
		case GST_MESSAGE_STATE_CHANGED:
			break;
		case GST_MESSAGE_ASYNC_DONE:
		case GST_MESSAGE_DURATION_CHANGED:
			pViewerImpl->UpdateTimeline();
			break;
		case GST_MESSAGE_ERROR:
			{
				gchar  *debug = NULL;
				GError *error = NULL;
				gst_message_parse_error (msg, &error, &debug);
				g_printerr ("ERROR from element %s: %s\n", GST_OBJECT_NAME (GST_MESSAGE_SRC(msg)), error->message);
				g_printerr ("Debugging info: %s\n", (debug) ? debug : "none");
				g_error_free (error);
				g_free (debug);
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
	gint64 pos_ns = 0, len_ns = 0;
	if (!m_pPipeline) return;

	if (!gst_element_query_position(m_pPipeline, GST_FORMAT_TIME, &pos_ns) ||
	    !gst_element_query_duration(m_pPipeline, GST_FORMAT_TIME, &len_ns)) {
		pos_ns = 0;
		len_ns = 0;
	}

	if (len_ns <= 0) {
		gtk_widget_set_visible(m_pTimeline, FALSE);
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(m_pPlayProgress), 0.0);
        gtk_label_set_text(GTK_LABEL(m_pTimeLabel), "0:00 / 0:00");
		return;
	}

	gtk_widget_set_visible(m_pTimeline, TRUE);

	gchar* str_pos = gst_time_format(pos_ns);
	gchar* str_len = gst_time_format(len_ns);
	gchar* text = g_strdup_printf("%s / %s", str_pos, str_len);

	gtk_label_set_text(GTK_LABEL(m_pTimeLabel), text);
	g_free(text);
	g_free(str_len);
	g_free(str_pos);

	gdouble progress = (len_ns > 0) ? ((gdouble)pos_ns / len_ns) : 0.0;
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(m_pPlayProgress), progress);
}

void Viewer::ViewerImpl::PlayPauseVideo()
{
	if (!IsVideo() || !m_pPipeline) return;

	gchar* current_pipeline_uri = NULL;
	g_object_get(G_OBJECT(m_pPipeline), "uri", &current_pipeline_uri, NULL);

	QuiverFile current_qf = m_ImageListPtr->GetCurrent();
	const char* new_uri = current_qf.GetURI();


	if (current_pipeline_uri && new_uri && 0 == g_strcmp0(current_pipeline_uri, new_uri))
	{
		gtk_widget_set_visible(m_pImageView, FALSE);
		gtk_widget_set_visible(m_pVideoWidget, TRUE);
		GstState current_state;
		GstStateChangeReturn ret = gst_element_get_state(GST_ELEMENT(m_pPipeline), &current_state, NULL, GST_SECOND);

		if (ret == GST_STATE_CHANGE_SUCCESS || ret == GST_STATE_CHANGE_ASYNC)
		{
			if (current_state == GST_STATE_PLAYING)
			{
				gst_element_set_state(GST_ELEMENT(m_pPipeline), GST_STATE_PAUSED);
				gtk_widget_set_visible(m_pMediaControls, TRUE);
				SetIsPlaying(false);
			}
			else
			{
				SetIsPlaying(true);
				gst_element_set_state(GST_ELEMENT(m_pPipeline), GST_STATE_PLAYING);
				if (0 != m_iTimeoutMouseMotionNotify) g_source_remove(m_iTimeoutMouseMotionNotify);
				m_iTimeoutMouseMotionNotify = g_timeout_add(1500,timeout_event_motion_notify,this);
			}
		}
	}
	else
	{
		StopVideo(false);
		gtk_widget_set_visible(m_pImageView, FALSE);
		gtk_widget_set_visible(m_pVideoWidget, TRUE);
		if (new_uri) {
			g_object_set(G_OBJECT(m_pPipeline), "uri", new_uri, NULL);
			// For GtkVideo, you'd set the media stream
			if (GTK_IS_VIDEO(m_pVideoWidget)) {
                if (g_str_has_prefix(new_uri, "file://")) {
                    GFile* file = g_file_new_for_uri(new_uri);
                    char* path = g_file_get_path(file);
                    gtk_video_set_filename(GTK_VIDEO(m_pVideoWidget), path);
                    g_free(path);
                    g_object_unref(file);
                } else {
                    // Assuming new_uri might be a direct path or other URI GtkVideo can handle
                    gtk_video_set_filename(GTK_VIDEO(m_pVideoWidget), new_uri);
                }
                // The GtkMediaFile method is more for when you manage the stream explicitly.
                // GtkMediaStream* stream = GTK_MEDIA_STREAM(gtk_media_file_new_for_filename(path_from_new_uri));
				// gtk_video_set_media_stream(GTK_VIDEO(m_pVideoWidget), stream);
				// if(stream) g_object_unref(stream);
			}
		}
		gst_element_set_state(GST_ELEMENT(m_pPipeline), GST_STATE_PLAYING);
		SetIsPlaying(true);
		if (0 != m_iTimeoutMouseMotionNotify) g_source_remove(m_iTimeoutMouseMotionNotify);
		m_iTimeoutMouseMotionNotify = g_timeout_add(1500,timeout_event_motion_notify,this);
	}
    g_free(current_pipeline_uri);
}

void Viewer::ViewerImpl::SkipForward()
{
	if (IsPlaying() && m_pPipeline)
	{
		gint64 pos_ns = 0, len_ns = 0;
		if (gst_element_query_position(m_pPipeline, GST_FORMAT_TIME, &pos_ns) &&
		    gst_element_query_duration(m_pPipeline, GST_FORMAT_TIME, &len_ns) && len_ns > 0)
		{
			gint64 seek_pos = std::min(len_ns, pos_ns + 10 * GST_SECOND);
			gst_element_seek_simple(GST_ELEMENT(m_pPipeline), GST_FORMAT_TIME, (GstSeekFlags)(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT), seek_pos);
			gtk_widget_set_visible(m_pMediaControls, TRUE);
			if (0 != m_iTimeoutMouseMotionNotify) g_source_remove(m_iTimeoutMouseMotionNotify);
			m_iTimeoutMouseMotionNotify = g_timeout_add(1500,timeout_event_motion_notify,this);
		}
	}
}

void Viewer::ViewerImpl::SkipBack()
{
	if (IsPlaying() && m_pPipeline)
	{
		gint64 pos_ns = 0;
		if (gst_element_query_position(m_pPipeline, GST_FORMAT_TIME, &pos_ns))
		{
			gint64 seek_pos = std::max((gint64)0, pos_ns - 10 * GST_SECOND);
			gst_element_seek_simple(GST_ELEMENT(m_pPipeline), GST_FORMAT_TIME, (GstSeekFlags)(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT), seek_pos);
			gtk_widget_set_visible(m_pMediaControls, TRUE);
			if (0 != m_iTimeoutMouseMotionNotify) g_source_remove(m_iTimeoutMouseMotionNotify);
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

    if(m_pPipeline) {
	    gst_element_set_state(GST_ELEMENT(m_pPipeline), GST_STATE_NULL);
    }
    if(m_pVideoWidget) gtk_widget_set_visible(m_pVideoWidget, FALSE);
	gtk_widget_set_visible(m_pImageView, TRUE);

	UpdateTimeline();

	if (IsVideo())
	{
		gtk_widget_set_visible(m_pMediaControls, TRUE);
	}

	if (reloadImage && m_ImageListPtr && 0 != m_ImageListPtr->GetSize())
	{
        if(gtk_widget_get_mapped(m_pImageView)) gtk_widget_queue_draw(m_pImageView);

		if (0 == m_iTimeoutSlideshowID)
			LoadImage(m_ImageListPtr->GetCurrent());
	}
}

bool Viewer::ViewerImpl::IsVideo()
{
	return (m_ImageListPtr && 0 != m_ImageListPtr->GetSize() &&
		m_ImageListPtr->GetCurrent().IsVideo());
}


static void viewer_button_release_cb(GtkGestureClick* gesture, int /*n_press*/, double /*x*/, double /*y*/, gpointer user_data)
{
	Viewer::ViewerImpl *pViewerImpl = (Viewer::ViewerImpl*)user_data;
    GtkWidget* widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));

	if (widget == pViewerImpl->m_pPlayProgressEventBox)
	{
		if (pViewerImpl->m_bWasPlayingBeforeSeek) {
		   pViewerImpl->PlayPauseVideo();
        }
        gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_NONE);
	}
}

static void viewer_button_press_cb(GtkGestureClick* gesture, int /*n_press*/, double x, double y, gpointer user_data)
{
	Viewer::ViewerImpl *pViewerImpl = (Viewer::ViewerImpl*)user_data;
    GtkWidget* widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    guint button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));

	if (widget == pViewerImpl->m_pImageView)
	{
		if (button == GDK_BUTTON_SECONDARY)
		{
			return;
		}
	}
	else if (widget == pViewerImpl->m_pPlayProgressEventBox)
	{
        if (button == GDK_BUTTON_PRIMARY) {
            pViewerImpl->m_bWasPlayingBeforeSeek = pViewerImpl->IsPlaying();

            gint widget_width = gtk_widget_get_width(widget);
            gint64 clip_duration = 0;

            if (pViewerImpl->m_pPipeline && gst_element_query_duration(GST_ELEMENT(pViewerImpl->m_pPipeline), GST_FORMAT_TIME, &clip_duration) && widget_width > 0)
            {
                gint64 seek_pos = (clip_duration * x) / widget_width;
                gst_element_seek_simple(GST_ELEMENT(pViewerImpl->m_pPipeline), GST_FORMAT_TIME, (GstSeekFlags)(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT), seek_pos);
            }

            if (pViewerImpl->IsPlaying()) {
                pViewerImpl->PlayPauseVideo();
            }
        }
	}
	else if (widget == pViewerImpl->m_pPlayButton)
	{
		if (button == GDK_BUTTON_PRIMARY)
		{
			if (pViewerImpl->IsVideo()) {
				pViewerImpl->PlayPauseVideo();
            }
		}
	}
}


Viewer::ViewerImpl::~ViewerImpl()
{
	StopVideo(false);

    if(m_pPipeline) {
	    gst_object_unref(GST_OBJECT(m_pPipeline));
        m_pPipeline = NULL;
    }

	if (0 != m_iIdleSetIndex) g_source_remove(m_iIdleSetIndex);
    if (0 != m_iTimeoutScrollbars) g_source_remove(m_iTimeoutScrollbars);
    if (0 != m_iTimeoutUpdateListID) g_source_remove(m_iTimeoutUpdateListID);
    if (0 != m_iTimeoutSlideshowID) g_source_remove(m_iTimeoutSlideshowID);
    if (0 != m_iTimeoutClickID) g_source_remove(m_iTimeoutClickID);
    if (0 != m_iTimeoutMouseMotionNotify) g_source_remove(m_iTimeoutMouseMotionNotify);
    if (0 != m_iTimeoutPlayProgress) g_source_remove(m_iTimeoutPlayProgress);

	if (m_StatusbarPtr) m_ImageLoader.RemovePixbufLoaderObserver(m_StatusbarPtr.get());
	if (m_PixbufLoaderObserverPtr) m_ImageLoader.RemovePixbufLoaderObserver(m_PixbufLoaderObserverPtr.get());


	PreferencesPtr prefsPtr = Preferences::GetInstance();
	if (prefsPtr && m_PreferencesEventHandlerPtr) {
	    prefsPtr->RemoveEventHandler( m_PreferencesEventHandlerPtr );
    }

    if (m_pCssProvider) {
        gtk_style_context_remove_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(m_pCssProvider));
        g_object_unref(m_pCssProvider);
        m_pCssProvider = NULL;
    }
}

Viewer::ViewerImpl::ViewerImpl(Viewer *pViewer) :
	m_ImageListPtr(new ImageList()),
	m_ThumbnailCache(100),
	m_PreferencesEventHandlerPtr ( new PreferencesEventHandler(this) ),
	m_ImageListEventHandlerPtr( new ImageListEventHandler(this) ),
	m_pCssProvider(gtk_css_provider_new()),
	m_bIsPlaying(false),
	m_bWasPlayingBeforeSeek(false),
	m_ThumbnailLoader(this,2),
    m_pPipeline(NULL), m_pVideoWidget(NULL)
{
	PreferencesPtr prefsPtr = Preferences::GetInstance();
	prefsPtr->AddEventHandler( m_PreferencesEventHandlerPtr );

	m_pViewer = pViewer;

    GtkBuilder* builder = gtk_builder_new_from_file(QUIVER_DATADIR "/viewer.ui");
    m_pHBox = GTK_WIDGET(gtk_builder_get_object(builder, "viewer_hbox"));
    m_pVBox = GTK_WIDGET(gtk_builder_get_object(builder, "viewer_vbox"));
    m_pGrid = GTK_WIDGET(gtk_builder_get_object(builder, "viewer_grid"));
    m_pIconView = GTK_WIDGET(gtk_builder_get_object(builder, "viewer_icon_view"));
    m_pImageView = GTK_WIDGET(gtk_builder_get_object(builder, "viewer_image_view"));
    m_pVideoWidget = GTK_WIDGET(gtk_builder_get_object(builder, "viewer_video_widget"));
    m_pMediaControls = GTK_WIDGET(gtk_builder_get_object(builder, "viewer_media_controls"));
    m_pPlayButton = GTK_WIDGET(gtk_builder_get_object(builder, "viewer_play_button"));
    m_pPlayImage = GTK_WIDGET(gtk_builder_get_object(builder, "viewer_play_image"));
    m_pTimeline = GTK_WIDGET(gtk_builder_get_object(builder, "viewer_timeline_box"));
    m_pTimeLabel = GTK_WIDGET(gtk_builder_get_object(builder, "viewer_time_label"));
    m_pPlayProgress = GTK_WIDGET(gtk_builder_get_object(builder, "viewer_play_progress"));
    m_pPlayProgressEventBox = GTK_WIDGET(gtk_builder_get_object(builder, "viewer_progress_box"));
    m_pVolumeButton = GTK_WIDGET(gtk_builder_get_object(builder, "viewer_volume_button"));
    m_pScrollbarV = GTK_WIDGET(gtk_builder_get_object(builder, "viewer_scrollbar_v"));
    m_pScrollbarH = GTK_WIDGET(gtk_builder_get_object(builder, "viewer_scrollbar_h"));
    m_pNavigationBox = GTK_WIDGET(gtk_builder_get_object(builder, "viewer_navigation_box"));
    m_pContextMenu = GTK_WIDGET(gtk_builder_get_object(builder, "viewer_context_menu"));
    gtk_widget_set_parent(m_pContextMenu, m_pHBox);
    g_object_unref(builder);

	gtk_widget_set_size_request(m_pImageView, 100, 100);

    string strBGColorImg   = prefsPtr->GetString(QUIVER_PREFS_APP,QUIVER_PREFS_APP_BG_IMAGEVIEW);
    string strBGColorThumb = prefsPtr->GetString(QUIVER_PREFS_APP,QUIVER_PREFS_APP_BG_ICONVIEW);
    string css = ".icon-view.custom-bg { background-color: " + strBGColorThumb + "; } .image-view.custom-bg { background-color: " + strBGColorImg + "; }";
    gtk_css_provider_load_from_string(m_pCssProvider, css.c_str());
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(m_pCssProvider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    if (!prefsPtr->GetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_USE_THEME_COLOR,true))
    {
        gtk_widget_add_css_class(m_pIconView, "custom-bg");
        gtk_widget_add_css_class(m_pImageView, "custom-bg");
    }

	g_signal_connect(G_OBJECT(m_pVolumeButton), "value-changed", G_CALLBACK(viewer_volume_value_changed), this);

    GtkGesture* play_button_click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(play_button_click), GDK_BUTTON_PRIMARY);
    g_signal_connect(play_button_click, "pressed", G_CALLBACK(viewer_button_press_cb), this);
    gtk_widget_add_controller(m_pPlayButton, GTK_EVENT_CONTROLLER(play_button_click));


    GtkEventController* play_button_motion = gtk_event_controller_motion_new();
    g_signal_connect(play_button_motion, "enter", G_CALLBACK(viewer_play_button_mouse_in_cb), this);
    g_signal_connect(play_button_motion, "leave", G_CALLBACK(viewer_play_button_mouse_out_cb), this);
    gtk_widget_add_controller(m_pPlayButton, play_button_motion);


    GtkGesture* progress_click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(progress_click), GDK_BUTTON_PRIMARY);
    g_signal_connect(progress_click, "pressed", G_CALLBACK(viewer_button_press_cb), this);
    gtk_widget_add_controller(m_pPlayProgressEventBox, GTK_EVENT_CONTROLLER(progress_click));

    GtkGesture* progress_release = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(progress_release), GDK_BUTTON_PRIMARY);
    g_signal_connect(progress_release, "released", G_CALLBACK(viewer_button_release_cb), this);
    gtk_widget_add_controller(m_pPlayProgressEventBox, GTK_EVENT_CONTROLLER(progress_release));

    GtkEventController* progress_motion = gtk_event_controller_motion_new();
    g_signal_connect(progress_motion, "motion", G_CALLBACK(viewer_motion_notify_cb), this);
    gtk_widget_add_controller(m_pPlayProgressEventBox, progress_motion);


    gtk_widget_set_visible(m_pMediaControls, FALSE);

	m_iCurrentOrientation = 1;
	m_iMergedViewerUI = 0;

	m_SlideShowState = SLIDESHOW_STATE_ADVANCE;
	m_iIdleSetIndex = 0;
	m_iTimeoutScrollbars = 0;
	m_iTimeoutUpdateListID = 0;
	m_iTimeoutSlideshowID = 0;
	m_iTimeoutClickID = 0;
	m_iTimeoutMouseMotionNotify = 0;
	m_iTimeoutPlayProgress = 0;
	m_iSlideShowWaitCount = 0;

	m_pAdjustmentH = quiver_image_view_get_hadjustment(QUIVER_IMAGE_VIEW(m_pImageView));
	m_pAdjustmentV = quiver_image_view_get_vadjustment(QUIVER_IMAGE_VIEW(m_pImageView));

    gtk_scrollbar_set_adjustment(GTK_SCROLLBAR(m_pScrollbarH), m_pAdjustmentH);
    gtk_scrollbar_set_adjustment(GTK_SCROLLBAR(m_pScrollbarV), m_pAdjustmentV);

	GdkPixbuf *nav_pixbuf = gdk_pixbuf_new_from_xpm_data ((const char**) nav_button_xpm);
	GtkWidget *nav_image = NULL;
    if (nav_pixbuf) {
        nav_image = gtk_image_new_from_paintable (GDK_PAINTABLE(nav_pixbuf));
	    g_object_unref (G_OBJECT (nav_pixbuf));
    } else {
        nav_image = gtk_image_new_from_paintable(IconManager::GetInstance()->GetIcon("image-missing-symbolic"));
    }
	gtk_box_append (GTK_BOX(m_pNavigationBox),nav_image);
	gtk_widget_set_visible(nav_image, TRUE);

	gtk_widget_set_visible(m_pScrollbarV,FALSE);
	gtk_widget_set_visible(m_pScrollbarH,FALSE);
	gtk_widget_set_visible(m_pNavigationBox,FALSE);

    gtk_widget_set_visible(m_pVideoWidget, FALSE);

	AddFilmstrip();

    m_pActionGroup = g_simple_action_group_new();

    // Add toggle actions
    GAction *toggle_action_filmstrip = (GAction*)g_simple_action_new_stateful(
        ACTION_VIEWER_VIEW_FILM_STRIP, NULL, g_variant_new_boolean(prefsPtr->GetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_FILMSTRIP_SHOW, true)));
    g_signal_connect(toggle_action_filmstrip, "change-state", G_CALLBACK(viewer_toggle_action_handler_cb), this);
    g_action_map_add_action(G_ACTION_MAP(m_pActionGroup), toggle_action_filmstrip);

    GAction *toggle_action_rotate_best_fit = (GAction*)g_simple_action_new_stateful(
        ACTION_VIEWER_ROTATE_FOR_BEST_FIT, NULL, g_variant_new_boolean(prefsPtr->GetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_ROTATE_FOR_BEST_FIT, false)));
    g_signal_connect(toggle_action_rotate_best_fit, "change-state", G_CALLBACK(viewer_toggle_action_handler_cb), this);
    g_action_map_add_action(G_ACTION_MAP(m_pActionGroup), toggle_action_rotate_best_fit);

    // Add zoom action
    GVariant* current_zoom_state = g_variant_new_int32(prefsPtr->GetInteger(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_DEFAULT_VIEW_MODE, QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW));
    GAction *zoom_action = (GAction*)g_simple_action_new_stateful("zoom", g_variant_type_new("i"), current_zoom_state);
    g_signal_connect(zoom_action, "change-state", G_CALLBACK(viewer_zoom_action_handler_cb), this);
    g_action_map_add_action(G_ACTION_MAP(m_pActionGroup), zoom_action);


    // Add stateless actions
    const gchar *actions[] = {
        ACTION_VIEWER_CUT,
        ACTION_VIEWER_COPY,
        ACTION_VIEWER_TRASH,
        ACTION_VIEWER_PREVIOUS,
        ACTION_VIEWER_NEXT,
        ACTION_VIEWER_FIRST,
        ACTION_VIEWER_LAST,
        ACTION_VIEWER_ZOOM_IN,
        ACTION_VIEWER_ZOOM_OUT,
        ACTION_VIEWER_ROTATE_CW,
        ACTION_VIEWER_ROTATE_CCW,
        ACTION_VIEWER_FLIP_H,
        ACTION_VIEWER_FLIP_V,
        ACTION_VIEWER_VIDEO_PLAY,
        ACTION_VIEWER_VIDEO_SKIP_FORWARD,
        ACTION_VIEWER_VIDEO_SKIP_BACK,
        NULL
    };

    for (int i = 0; actions[i] != NULL; ++i)
    {
        GAction *action = (GAction*)g_simple_action_new(actions[i], NULL);
        g_signal_connect(action, "activate", G_CALLBACK(viewer_action_handler_cb), this);
        g_action_map_add_action(G_ACTION_MAP(m_pActionGroup), action);
    }

    gtk_widget_insert_action_group(m_pHBox, "viewer", G_ACTION_GROUP(m_pActionGroup));


    if (!prefsPtr->GetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_USE_THEME_COLOR,true)) {
    }


	m_iSlideShowDuration = prefsPtr->GetInteger(QUIVER_PREFS_SLIDESHOW,QUIVER_PREFS_SLIDESHOW_DURATION, 3000);
	m_bSlideShowLoop = prefsPtr->GetBoolean(QUIVER_PREFS_SLIDESHOW,QUIVER_PREFS_SLIDESHOW_LOOP,true);
	m_bMaximizeViewableArea = prefsPtr->GetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_ROTATE_FOR_BEST_FIT, false);
	m_bIsPlaying = false;

	quiver_image_view_set_magnification_mode(QUIVER_IMAGE_VIEW(m_pImageView),QUIVER_IMAGE_VIEW_MAGNIFICATION_MODE_SMOOTH);
	QuiverImageViewMode view_mode = (QuiverImageViewMode)prefsPtr->GetInteger(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_DEFAULT_VIEW_MODE, QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW);
	quiver_image_view_set_view_mode(QUIVER_IMAGE_VIEW(m_pImageView), view_mode);

    GtkEventController* image_view_motion_controller = gtk_event_controller_motion_new();
    g_signal_connect(image_view_motion_controller, "motion", G_CALLBACK(viewer_motion_notify_cb), this);
    gtk_widget_add_controller(m_pImageView, image_view_motion_controller);
	// Add motion controller also to video widget if it exists
	if (m_pVideoWidget) {
		GtkEventController* video_widget_motion_controller = gtk_event_controller_motion_new();
		g_signal_connect(video_widget_motion_controller, "motion", G_CALLBACK(viewer_motion_notify_cb), this);
		gtk_widget_add_controller(m_pVideoWidget, video_widget_motion_controller);
	}


	quiver_icon_view_set_n_items_func(QUIVER_ICON_VIEW(m_pIconView),(QuiverIconViewGetNItemsFunc)n_cells_callback,this,NULL);
	quiver_icon_view_set_thumbnail_pixbuf_func(QUIVER_ICON_VIEW(m_pIconView),(QuiverIconViewGetThumbnailPixbufFunc)thumbnail_pixbuf_callback,this,NULL);
	quiver_icon_view_set_icon_pixbuf_func(QUIVER_ICON_VIEW(m_pIconView),(QuiverIconViewGetIconPixbufFunc)icon_pixbuf_callback,this,NULL);
	quiver_icon_view_set_scroll_type(QUIVER_ICON_VIEW(m_pIconView),QUIVER_ICON_VIEW_SCROLL_SMOOTH_CENTER);
	int iIconSize = prefsPtr->GetInteger(QUIVER_PREFS_VIEWER,QUIVER_PREFS_VIEWER_FILMSTRIP_SIZE, 128);
	quiver_icon_view_set_icon_size(QUIVER_ICON_VIEW(m_pIconView),iIconSize,iIconSize);

	g_signal_connect(G_OBJECT(m_pIconView),"cell_activated",G_CALLBACK(viewer_iconview_cell_activated),this);
	g_signal_connect(G_OBJECT(m_pIconView),"cursor_changed",G_CALLBACK(viewer_iconview_cursor_changed),this);

    GtkGesture* image_view_click_gesture = gtk_gesture_click_new();
    g_signal_connect(image_view_click_gesture, "pressed", G_CALLBACK(viewer_button_press_cb), this);
    gtk_widget_add_controller(m_pImageView, GTK_EVENT_CONTROLLER(image_view_click_gesture));

    GtkGesture* context_menu_gesture = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(context_menu_gesture), GDK_BUTTON_SECONDARY);
    g_signal_connect_swapped(context_menu_gesture, "pressed", G_CALLBACK(gtk_popover_popup), m_pContextMenu);
    gtk_widget_add_controller(m_pImageView, GTK_EVENT_CONTROLLER(context_menu_gesture));
    gtk_widget_add_controller(m_pVideoWidget, GTK_EVENT_CONTROLLER(g_object_ref(context_menu_gesture)));

    GtkEventController* scroll_controller = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    g_signal_connect(scroll_controller, "scroll", G_CALLBACK(viewer_scrollwheel_event_cb), this);
    gtk_widget_add_controller(m_pImageView, scroll_controller);
	// Add scroll controller also to video widget if it exists
	if (m_pVideoWidget) {
		GtkEventController* video_scroll_controller = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
		g_signal_connect(video_scroll_controller, "scroll", G_CALLBACK(viewer_scrollwheel_event_cb), this); // Assuming same handling
		gtk_widget_add_controller(m_pVideoWidget, video_scroll_controller);
	}


    g_signal_connect (G_OBJECT (m_pImageView), "activated", G_CALLBACK (viewer_imageview_activated), this);
    g_signal_connect (G_OBJECT (m_pImageView), "reload", G_CALLBACK (viewer_imageview_reload), this);
    g_signal_connect (G_OBJECT (m_pImageView), "magnification-changed", G_CALLBACK (viewer_imageview_magnification_changed), this);
    g_signal_connect (G_OBJECT (m_pImageView), "view-mode-changed", G_CALLBACK (viewer_imageview_view_mode_changed), this);

    GtkEventController* key_controller_imageview = gtk_event_controller_key_new();
    g_signal_connect(key_controller_imageview, "key-pressed", G_CALLBACK(viewer_imageview_key_press_event_cb), this);
    gtk_widget_add_controller(m_pImageView, key_controller_imageview);
	// Add key controller also to video widget if it exists
	if (m_pVideoWidget) {
		GtkEventController* key_controller_video = gtk_event_controller_key_new();
		g_signal_connect(key_controller_video, "key-pressed", G_CALLBACK(viewer_imageview_key_press_event_cb), this); // Assuming same handling
		gtk_widget_add_controller(m_pVideoWidget, key_controller_video);
	}


    g_signal_connect (G_OBJECT (m_pAdjustmentH), "changed", G_CALLBACK (image_view_adjustment_changed), this);
    g_signal_connect (G_OBJECT (m_pAdjustmentV), "changed", G_CALLBACK (image_view_adjustment_changed), this);

	IPixbufLoaderObserverPtr tmp( new ViewerImageViewPixbufLoaderObserver(QUIVER_IMAGE_VIEW(m_pImageView)));
	m_PixbufLoaderObserverPtr = tmp;
	m_ImageLoader.AddPixbufLoaderObserver(m_PixbufLoaderObserverPtr.get());

	gtk_widget_set_visible(m_pHBox, TRUE);
	gtk_widget_set_visible(m_pHBox, FALSE);

	m_pNavigationWindow = gtk_window_new ();
    gtk_window_set_decorated(GTK_WINDOW(m_pNavigationWindow), FALSE);
    GtkWidget* top_level_ancestor = gtk_widget_get_ancestor(m_pHBox, GTK_TYPE_WINDOW);
    if (top_level_ancestor) {
        gtk_window_set_transient_for(GTK_WINDOW(m_pNavigationWindow), GTK_WINDOW(top_level_ancestor));
    }


	m_pNavigationControl = quiver_navigation_control_new_with_adjustments (m_pAdjustmentH, m_pAdjustmentV);


	GtkWidget * frame = gtk_frame_new(NULL);
    const char* frame_classes[] = {"navigation-frame", NULL};
    gtk_widget_set_css_classes(frame, frame_classes);

	gtk_window_set_child(GTK_WINDOW(m_pNavigationWindow), frame);
    gtk_frame_set_child(GTK_FRAME(frame), m_pNavigationControl);

	bool bShowFilmstrip = prefsPtr->GetBoolean(QUIVER_PREFS_VIEWER,QUIVER_PREFS_VIEWER_FILMSTRIP_SHOW,true);
	gtk_widget_set_visible(m_pIconView, bShowFilmstrip);

	gboolean bQuickPreview = (gboolean)prefsPtr->GetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_QUICK_PREVIEW, true);
	m_ImageLoader.EnableQuickPreview(bQuickPreview);


	m_pPipeline = gst_element_factory_make("playbin", "player");
    if (!m_pPipeline) {
        g_printerr("Failed to create playbin.\n");
    }


    GstElement* audiosink_element = gst_element_factory_make("autoaudiosink",NULL);

    if(m_pPipeline) {
		// For GtkVideo, playbin's video-sink should be set to a sink that GtkVideo can use,
        // or GtkVideo should be given a GtkMediaStream derived from the pipeline.
        // Let's try setting video-sink to NULL and then use GtkMediaStream for GtkVideo
        g_object_set(G_OBJECT(m_pPipeline), "video-sink", NULL, NULL);

        if(audiosink_element) {
            g_object_set(G_OBJECT(m_pPipeline), "audio-sink", audiosink_element, NULL);
        } else {
        }
        // Commenting out direct GtkMediaStream creation from GstPipeline for now,
        // as it's complex and gtk_media_stream_new is not the correct API for this.
        // Video playback might be affected until GtkVideo is fully integrated with playbin
        // (e.g. by setting filename on GtkVideo directly, or using GstPlayer).
		// if (GTK_IS_VIDEO(m_pVideoWidget)) {
			// GtkMediaStream* stream = gtk_media_stream_new(m_pPipeline, NULL); // This API is not for this purpose
			// gtk_video_set_media_stream(GTK_VIDEO(m_pVideoWidget), stream);
			// if (stream) g_object_unref(stream);
		// }
    }

	gdouble volume = 0.;
    if(m_pPipeline) {
	    g_object_get(G_OBJECT(m_pPipeline), "volume", &volume, NULL);
    }
	gtk_scale_button_set_value(GTK_SCALE_BUTTON(m_pVolumeButton), volume);

    if(m_pPipeline) {
	    GstBus* bus = gst_pipeline_get_bus(GST_PIPELINE(m_pPipeline));
	    gst_bus_add_watch (bus, (GstBusFunc) gstreamer_bus_watcher, this);
	    gst_object_unref (bus);
    }
}


Viewer::Viewer() : m_ViewerImplPtr(new Viewer::ViewerImpl(this))
{


}

Viewer::~Viewer()
{
	m_ViewerImplPtr->SlideShowStop(false);

    if (m_ViewerImplPtr->m_pNavigationWindow) {
        gtk_window_destroy(GTK_WINDOW(m_ViewerImplPtr->m_pNavigationWindow));
        m_ViewerImplPtr->m_pNavigationWindow = NULL;
    }
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
	return G_SOURCE_REMOVE;
}

void Viewer::Show()
{

	gint cursor_cell = quiver_icon_view_get_cursor_cell(QUIVER_ICON_VIEW(m_ViewerImplPtr->m_pIconView));
	if (0 == m_ViewerImplPtr->m_ImageListPtr->GetSize() || m_ViewerImplPtr->m_QuiverFileCurrent != m_ViewerImplPtr->m_ImageListPtr->GetCurrent())
	{
		quiver_image_view_set_pixbuf(QUIVER_IMAGE_VIEW(m_ViewerImplPtr->m_pImageView),NULL);
		if (m_ViewerImplPtr->m_iIdleSetIndex == 0) {
			m_ViewerImplPtr->m_iIdleSetIndex = g_idle_add( idle_set_image_index, m_ViewerImplPtr.get());
        }
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

	gtk_widget_set_visible(m_ViewerImplPtr->m_pHBox, TRUE);


    if (m_ViewerImplPtr->m_ImageListPtr) {
	    m_ViewerImplPtr->m_ImageListPtr->UnblockHandler(m_ViewerImplPtr->m_ImageListEventHandlerPtr);
    }
}

void Viewer::Hide()
{
	m_ViewerImplPtr->StopVideo(true);
	SlideShowStop();

	gtk_widget_set_visible(m_ViewerImplPtr->m_pHBox, FALSE);
    if (m_ViewerImplPtr->m_ImageListPtr) {
	    m_ViewerImplPtr->m_ImageListPtr->BlockHandler(m_ViewerImplPtr->m_ImageListEventHandlerPtr);
    }
}


void Viewer::SetStatusbar(StatusbarPtr statusbarPtr)
{
	if(m_ViewerImplPtr->m_StatusbarPtr) {
        m_ViewerImplPtr->m_ImageLoader.RemovePixbufLoaderObserver(m_ViewerImplPtr->m_StatusbarPtr.get());
    }
	m_ViewerImplPtr->m_StatusbarPtr = statusbarPtr;
	if(m_ViewerImplPtr->m_StatusbarPtr) {
	    m_ViewerImplPtr->m_ImageLoader.AddPixbufLoaderObserver(m_ViewerImplPtr->m_StatusbarPtr.get());
    }
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
	if (!pViewerImpl || !pViewerImpl->m_ImageListPtr) return G_SOURCE_REMOVE;

	int iNextIndex = pViewerImpl->m_ImageListPtr->GetCurrentIndex()+1;

	if (pViewerImpl->m_ImageLoader.IsWorking() || quiver_image_view_is_in_transition(QUIVER_IMAGE_VIEW(pViewerImpl->m_pImageView)) )
	{
		++pViewerImpl->m_iSlideShowWaitCount;
		pViewerImpl->m_iTimeoutSlideshowID = g_timeout_add(SLIDESHOW_WAIT_DURATION,timeout_advance_slideshow, pViewerImpl);
		return G_SOURCE_REMOVE;
	}

	switch (pViewerImpl->m_SlideShowState)
	{
		case Viewer::ViewerImpl::SLIDESHOW_STATE_ADVANCE:
			{
				bool bStop = false;
				if (!pViewerImpl->m_ImageListPtr->HasNext())
				{
					if (pViewerImpl->m_bSlideShowLoop) iNextIndex = 0;
					else { pViewerImpl->m_pViewer->SlideShowStop(); bStop = true; }
				}
				if (!bStop)
				{
					pViewerImpl->SetImageIndex(iNextIndex,true,false);
					++pViewerImpl->m_iSlideShowWaitCount;
					pViewerImpl->m_iTimeoutSlideshowID = g_timeout_add(SLIDESHOW_WAIT_DURATION,timeout_advance_slideshow, pViewerImpl);
					pViewerImpl->m_SlideShowState = Viewer::ViewerImpl::SLIDESHOW_STATE_CACHE;
				}
			}
			break;
		case Viewer::ViewerImpl::SLIDESHOW_STATE_CACHE:
			{
				int iWaitTime = pViewerImpl->m_iSlideShowWaitCount * SLIDESHOW_WAIT_DURATION;
				iWaitTime = pViewerImpl->m_iSlideShowDuration - iWaitTime;
				iWaitTime = std::max(10, iWaitTime);

				pViewerImpl->CacheNext(true);
				if (pViewerImpl->IsVideo())
				{
					pViewerImpl->m_SlideShowState = Viewer::ViewerImpl::SLIDESHOW_STATE_PLAY_VIDEO;
					iWaitTime = 1000;
				}
				else
				{
					pViewerImpl->m_SlideShowState = Viewer::ViewerImpl::SLIDESHOW_STATE_ADVANCE;
					pViewerImpl->m_iSlideShowWaitCount = 0;
				}
				pViewerImpl->m_iTimeoutSlideshowID = g_timeout_add(iWaitTime,timeout_advance_slideshow, pViewerImpl);
			}
			break;
		case Viewer::ViewerImpl::SLIDESHOW_STATE_PLAY_VIDEO:
			{
				pViewerImpl->PlayPauseVideo();
				pViewerImpl->m_SlideShowState = Viewer::ViewerImpl::SLIDESHOW_STATE_PLAYING_VIDEO;
				pViewerImpl->m_iTimeoutSlideshowID = g_timeout_add(SLIDESHOW_WAIT_DURATION,timeout_advance_slideshow, pViewerImpl);
			}
		break;
		case Viewer::ViewerImpl::SLIDESHOW_STATE_PLAYING_VIDEO:
			{
				if (pViewerImpl->IsPlaying())
				{
					pViewerImpl->m_iTimeoutSlideshowID = g_timeout_add(SLIDESHOW_WAIT_DURATION,timeout_advance_slideshow, pViewerImpl);
				}
				else
				{
					pViewerImpl->m_iSlideShowWaitCount = 0;
					pViewerImpl->m_SlideShowState = Viewer::ViewerImpl::SLIDESHOW_STATE_ADVANCE;
					pViewerImpl->m_iTimeoutSlideshowID = g_timeout_add(SLIDESHOW_WAIT_DURATION,timeout_advance_slideshow, pViewerImpl);
				}
			}
			break;
	}
	return G_SOURCE_REMOVE;
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

	if (!m_ViewerImplPtr->m_iTimeoutSlideshowID && m_ViewerImplPtr->m_ImageListPtr && m_ViewerImplPtr->m_ImageListPtr->GetSize() >= 2 )
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
}


void Viewer::SlideShowStop()
{
	m_ViewerImplPtr->SlideShowStop();
}


static guint n_cells_callback(QuiverIconView *iconview, gpointer user_data)
{
	Viewer::ViewerImpl* pViewerImpl = (Viewer::ViewerImpl*)user_data;
	return pViewerImpl->m_ImageListPtr->GetSize();
}

static GdkPixbuf* icon_pixbuf_callback(QuiverIconView *iconview, guint cell,gpointer user_data)
{
	Viewer::ViewerImpl* pViewerImpl = (Viewer::ViewerImpl*)user_data;
    if (cell >= pViewerImpl->m_ImageListPtr->GetSize()) return NULL;
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
	return G_SOURCE_REMOVE;
}


static GdkPixbuf* thumbnail_pixbuf_callback(QuiverIconView *iconview, guint cell, gint* actual_width, gint* actual_height, gpointer user_data)
{
	Viewer::ViewerImpl* pViewerImpl = (Viewer::ViewerImpl*)user_data;
    if (cell >= pViewerImpl->m_ImageListPtr->GetSize()) return NULL;

	GdkPixbuf *pixbuf = NULL;
	gboolean need_new_thumb = TRUE;

	guint icon_cell_width, icon_cell_height;
	quiver_icon_view_get_icon_size(iconview,&icon_cell_width,&icon_cell_height);

	QuiverFile current_file = (*pViewerImpl->m_ImageListPtr)[cell];
	pixbuf = pViewerImpl->m_ThumbnailCache.GetPixbuf(current_file.GetURI());

	if (pixbuf)
	{
		*actual_width = current_file.GetWidth();
		*actual_height = current_file.GetHeight();

		if (4 < current_file.GetOrientation())
		{
			std::swap(*actual_width,*actual_height);
		}

		guint current_thumb_width = gdk_pixbuf_get_width(pixbuf);
		guint current_thumb_height = gdk_pixbuf_get_height(pixbuf);

		guint target_bound_width = *actual_width;
		guint target_bound_height = *actual_height;
		quiver_rect_get_bound_size(icon_cell_width, icon_cell_height, &target_bound_width, &target_bound_height, FALSE);

		if (target_bound_width == current_thumb_width && target_bound_height == current_thumb_height)
		{
			need_new_thumb = FALSE;
		} else {
            pixbuf = NULL;
        }
	}

	if (need_new_thumb)
	{
		pViewerImpl->QueueIconViewUpdate();
        if (pixbuf) {
            pixbuf = NULL;
        }
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
}

static gboolean timeout_update_scrollbars(gpointer user_data)
{
	Viewer::ViewerImpl* pViewerImpl = (Viewer::ViewerImpl*)user_data;
	pViewerImpl->UpdateScrollbars();
	pViewerImpl->m_iTimeoutScrollbars = 0;
	return G_SOURCE_REMOVE;
}

static void image_view_adjustment_changed (GtkAdjustment* /*adjustment*/, gpointer user_data)
{
	Viewer::ViewerImpl* pViewerImpl = (Viewer::ViewerImpl*)user_data;
	if (0 != pViewerImpl->m_iTimeoutScrollbars)
	{
		g_source_remove(pViewerImpl->m_iTimeoutScrollbars);
        pViewerImpl->m_iTimeoutScrollbars = 0;
	}
	pViewerImpl->m_iTimeoutScrollbars = g_timeout_add(20, timeout_update_scrollbars, pViewerImpl);
}


void Viewer::ViewerImpl::ImageListEventHandler::HandleContentsChanged(ImageListEventPtr /*event*/)
{
	parent->SetImageIndex(parent->m_ImageListPtr->GetCurrentIndex(),true);
	parent->m_ThumbnailLoader.UpdateList(true);
}
void Viewer::ViewerImpl::ImageListEventHandler::HandleCurrentIndexChanged(ImageListEventPtr event)
{
	parent->SetImageIndex(event->GetIndex(),true);
}
void Viewer::ViewerImpl::ImageListEventHandler::HandleItemAdded(ImageListEventPtr /*event*/)
{
	parent->SetImageIndex(parent->m_ImageListPtr->GetCurrentIndex(),true);
	parent->m_ThumbnailLoader.UpdateList(true);
}
void Viewer::ViewerImpl::ImageListEventHandler::HandleItemRemoved(ImageListEventPtr /*event*/)
{
	parent->SetImageIndex(parent->m_ImageListPtr->GetCurrentIndex(),true);
	parent->m_ThumbnailLoader.UpdateList(true);
}
void Viewer::ViewerImpl::ImageListEventHandler::HandleItemChanged(ImageListEventPtr event)
{
    if (!parent || !parent->m_ImageListPtr) return;
	if (parent->m_ImageListPtr->GetCurrentIndex() == event->GetIndex())
	{
        if (parent->m_ImageListPtr->GetSize() > event->GetIndex()) {
		    parent->m_ThumbnailCache.RemovePixbuf(parent->m_ImageListPtr->Get(event->GetIndex()).GetURI());
		    parent->m_ThumbnailLoader.UpdateList(true);

		    ImageLoader::LoadParams params = {0};
		    params.orientation = parent->GetCurrentOrientation(true);
		    params.reload = true;
		    params.fullsize = true;
		    params.no_thumb_preview = true;
		    params.state = ImageLoader::LOAD;
		    parent->m_ImageLoader.LoadImage(parent->m_ImageListPtr->GetCurrent(),params);
        }
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
				gtk_widget_remove_css_class(parent->m_pIconView, "custom-bg");
				gtk_widget_remove_css_class(parent->m_pImageView, "custom-bg");
			}
			else
			{
				gtk_widget_add_css_class(parent->m_pIconView, "custom-bg");
				gtk_widget_add_css_class(parent->m_pImageView, "custom-bg");
			}
		}
		else if (QUIVER_PREFS_APP_BG_IMAGEVIEW == event->GetKey() || QUIVER_PREFS_APP_BG_ICONVIEW == event->GetKey() )
		{
			if ( !prefsPtr->GetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_USE_THEME_COLOR,true) )
			{
                // The provider is reloaded in the main constructor, so no need to do anything here.
			}
		}
	}
	else if ( QUIVER_PREFS_VIEWER == event->GetSection () )
	{
		if (QUIVER_PREFS_VIEWER_FILMSTRIP_SHOW == event->GetKey() )
		{
            gtk_widget_set_visible(parent->m_pIconView, event->GetNewBoolean());
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
    if (!m_pViewerImpl || !m_pViewerImpl->m_ImageListPtr) return QuiverFile();
	if (index < m_pViewerImpl->m_ImageListPtr->GetSize())
	{
		return (*m_pViewerImpl->m_ImageListPtr)[index];
	}
	return QuiverFile();
}

void Viewer::ViewerImpl::ViewerThumbLoader::LoadThumbnail(const ThumbLoaderItem &item, guint uiWidth, guint uiHeight)
{

	if (m_pViewerImpl->m_ImageLoader.IsWorking() || quiver_image_view_is_in_transition(QUIVER_IMAGE_VIEW(m_pViewerImpl->m_pImageView)) )
	{
		usleep(100000);
	}

	if (gtk_widget_get_mapped(m_pViewerImpl->m_pIconView) &&
		item.m_ulIndex < m_pViewerImpl->m_ImageListPtr->GetSize())
	{
		QuiverFile f(item.m_QuiverFile);
		GdkPixbuf *pixbuf = m_pViewerImpl->m_ThumbnailCache.GetPixbuf(f.GetURI());

		if (NULL != pixbuf)
		{
			guint current_thumb_width = gdk_pixbuf_get_width(pixbuf);
			guint current_thumb_height = gdk_pixbuf_get_height(pixbuf);

            guint target_bound_width = f.GetWidth();
            guint target_bound_height = f.GetHeight();
            if (4 < f.GetOrientation()) std::swap(target_bound_width, target_bound_height);
			quiver_rect_get_bound_size(uiWidth, uiHeight, &target_bound_width, &target_bound_height, FALSE);

			if (current_thumb_width != target_bound_width || current_thumb_height != target_bound_height)
			{
				pixbuf = NULL;
			}
		}

		if (NULL == pixbuf)
		{
			pixbuf = f.GetThumbnail(std::max(uiWidth,uiHeight));
		}

		if (NULL != pixbuf)
		{
			guint final_thumb_width = gdk_pixbuf_get_width(pixbuf);
			guint final_thumb_height = gdk_pixbuf_get_height(pixbuf);

			guint target_bound_width_final = f.GetWidth();
			guint target_bound_height_final = f.GetHeight();
			if (4 < f.GetOrientation()) std::swap(target_bound_width_final,target_bound_height_final);
			quiver_rect_get_bound_size(uiWidth,uiHeight, &target_bound_width_final,&target_bound_height_final,FALSE);

			if (final_thumb_width != target_bound_width_final || final_thumb_height != target_bound_height_final)
			{
				GdkPixbuf* scaled_pixbuf = gdk_pixbuf_scale_simple (pixbuf, target_bound_width_final, target_bound_height_final, GDK_INTERP_BILINEAR);
				g_object_unref(pixbuf);
				pixbuf = scaled_pixbuf;
			}

			m_pViewerImpl->m_ThumbnailCache.AddPixbuf(f.GetURI(),pixbuf);
			quiver_icon_view_invalidate_cell(QUIVER_ICON_VIEW(m_pViewerImpl->m_pIconView),item.m_ulIndex);
            g_object_unref(pixbuf);
		}
	}
}

void Viewer::ViewerImpl::ViewerThumbLoader::GetVisibleRange(gulong* pulStart, gulong* pulEnd)
{
    if (!m_pViewerImpl || !m_pViewerImpl->m_pIconView) return;
	quiver_icon_view_get_visible_range(QUIVER_ICON_VIEW(m_pViewerImpl->m_pIconView),pulStart, pulEnd);
}


void Viewer::ViewerImpl::ViewerThumbLoader::GetIconSize(guint* puiWidth, guint* puiHeight)
{
    if (!m_pViewerImpl || !m_pViewerImpl->m_pIconView) return;
	quiver_icon_view_get_icon_size(QUIVER_ICON_VIEW(m_pViewerImpl->m_pIconView), puiWidth, puiHeight);
}

gulong Viewer::ViewerImpl::ViewerThumbLoader::GetNumItems()
{
    if (!m_pViewerImpl || !m_pViewerImpl->m_ImageListPtr) return 0;
	return m_pViewerImpl->m_ImageListPtr->GetSize();
}

void Viewer::ViewerImpl::ViewerThumbLoader::SetIsRunning(bool bIsRunning)
{
	if (m_pViewerImpl && m_pViewerImpl->m_StatusbarPtr.get())
	{
		if (bIsRunning) m_pViewerImpl->m_StatusbarPtr->StartProgressPulse();
		else m_pViewerImpl->m_StatusbarPtr->StopProgressPulse();
	}
}

void Viewer::ViewerImpl::ViewerThumbLoader::SetCacheSize(guint uiCacheSize)
{
    if(m_pViewerImpl) {
	    m_pViewerImpl->m_ThumbnailCache.SetSize(uiCacheSize);
    }
}
