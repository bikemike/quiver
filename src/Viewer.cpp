#include <config.h>

#include <libquiver/quiver-icon-view.h>
#include <libquiver/quiver-image-view.h>
#include <libquiver/quiver-pixbuf-utils.h>
#include <libquiver/quiver-navigation-control.h>

#include <gst/gst.h>
#include <gdk/gdkx.h>  // for GDK_WINDOW_XID
#include <gst/interfaces/xoverlay.h>


#include "Viewer.h"
#include "Timer.h"
#include "icons/nav_button.xpm"

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

static GdkPixbuf* icon_pixbuf_callback(QuiverIconView *iconview, guint cell,gpointer user_data);
static GdkPixbuf* thumbnail_pixbuf_callback(QuiverIconView *iconview, guint cell, gint* actual_width, gint* actual_height, gpointer user_data);
static guint n_cells_callback(QuiverIconView *iconview, gpointer user_data);
static void image_view_adjustment_changed (GtkAdjustment *adjustment, gpointer user_data);

static void viewer_radio_action_handler_cb(GtkRadioAction *action, GtkRadioAction *current, gpointer user_data);
static void viewer_action_handler_cb(GtkAction *action, gpointer data);

static gboolean viewer_scrollwheel_event(GtkWidget *widget, GdkEventScroll *event, gpointer data );
static void viewer_imageview_activated(QuiverImageView *imageview,gpointer data);
static void viewer_imageview_reload(QuiverImageView *imageview,gpointer data);
static void viewer_imageview_magnification_changed(QuiverImageView *imageview,gpointer data);
static void viewer_imageview_view_mode_changed(QuiverImageView *imageview,gpointer data);
static gboolean viewer_imageview_key_press_event(GtkWidget *imageview, GdkEventKey *event, gpointer userdata);

static void viewer_iconview_cell_activated(QuiverIconView *iconview,gulong cell,gpointer data);
static void viewer_iconview_cursor_changed(QuiverIconView *iconview,gulong cell,gpointer data);

static gboolean viewer_navigation_button_press_event(GtkWidget *widget, GdkEventButton *event, gpointer userdata);
gboolean navigation_control_button_release_event (GtkWidget *widget, GdkEventButton *event, gpointer data );
static GtkTableChild * GetGtkTableChild(GtkTable * table,GtkWidget	*widget_to_get);


// popup menu callbacks
static gboolean viewer_popup_menu_cb (GtkWidget *treeview, gpointer userdata);
static gboolean viewer_button_press_cb(GtkWidget   *widget, GdkEventButton *event, gpointer user_data); 
static gboolean viewer_button_release_cb(GtkWidget   *widget, GdkEventButton *event, gpointer user_data); 
static void viewer_show_context_menu(GdkEventButton *event, gpointer userdata);



// drag/drop targets
enum {
	QUIVER_TARGET_STRING,
	QUIVER_TARGET_URI
};

static GtkTargetEntry quiver_drag_target_table[] = {
		{ "STRING",     0, QUIVER_TARGET_STRING }, // STRING is used for legacy motif apps
		{ "text/plain", 0, QUIVER_TARGET_STRING },  // the real mime types to support
		 { "text/uri-list", 0, QUIVER_TARGET_URI },
};

static void signal_drag_data_get  (GtkWidget *widget, GdkDragContext *context, GtkSelectionData *selection_data, guint info, guint time,gpointer user_data);
static void signal_drag_data_delete  (GtkWidget *widget,GdkDragContext *context,gpointer user_data);
static void signal_drag_data_received(GtkWidget *widget,GdkDragContext *drag_context, gint x,gint y, GtkSelectionData *data, guint info, guint time,gpointer user_data);
static void signal_drag_begin (GtkWidget *widget,GdkDragContext *drag_context,gpointer user_data);
static void signal_drag_end(GtkWidget *widget,GdkDragContext *drag_context,gpointer user_data);
static void signal_drag_motion (GtkWidget *widget, GdkDragContext *context, gint x, gint y, guint time, gpointer user_data);
static gboolean signal_drag_drop (GtkWidget *widget, GdkDragContext *drag_context, gint x, gint y, guint time,  gpointer user_data);   


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
#define ACTION_VIEWER_NEXT_2            ACTION_VIEWER_NEXT"_2"
#define ACTION_VIEWER_PREVIOUS_2        ACTION_VIEWER_PREVIOUS"_2"
#define ACTION_VIEWER_ROTATE_CW_2       ACTION_VIEWER_ROTATE_CW"_2"
#define ACTION_VIEWER_ROTATE_CCW_2      ACTION_VIEWER_ROTATE_CCW"_2"
#define ACTION_VIEWER_ROTATE_FOR_BEST_FIT "MaximizeForDisplay"
#define ACTION_VIEWER_FLIP_H_2          ACTION_VIEWER_FLIP_H"_2"
#define ACTION_VIEWER_FLIP_V_2          ACTION_VIEWER_FLIP_V"_2"

#ifdef QUIVER_MAEMO
#define ACTION_VIEWER_ZOOM_IN_MAEMO     ACTION_VIEWER_ZOOM_IN"_MAEMO"
#define ACTION_VIEWER_ZOOM_OUT_MAEMO    ACTION_VIEWER_ZOOM_OUT"_MAEMO"
#endif

static const char *ui_viewer =
"<ui>"
#ifdef QUIVER_MAEMO
"	<popup name='MenubarMain'>"
#else
"	<menubar name='MenubarMain'>"
#endif
"		<menu action='MenuEdit'>"
"			<placeholder name='CopyPaste'>"
#ifdef FIXME_DISABLED
"				<menuitem action='"ACTION_VIEWER_CUT"'/>"
#endif
"				<menuitem action='"ACTION_VIEWER_COPY"'/>"
"			</placeholder>"
"			<placeholder name='Trash'>"
"				<menuitem action='"ACTION_VIEWER_TRASH"'/>"
"			</placeholder>"
"		</menu>"
"		<menu action='MenuView'>"
"			<placeholder name='UIItems'>"
"				<menuitem action='"ACTION_VIEWER_VIEW_FILM_STRIP"'/>"
"			</placeholder>"
"			<placeholder name='Zoom'>"
"				<menu action='MenuZoom'>"
"					<menuitem action='"ACTION_VIEWER_ZOOM_IN"'/>"
"					<menuitem action='"ACTION_VIEWER_ZOOM_OUT"'/>"
"					<menuitem action='"ACTION_VIEWER_ZOOM_100"'/>"
"					<menuitem action='"ACTION_VIEWER_ZOOM_FIT"'/>"
"					<menuitem action='"ACTION_VIEWER_ZOOM_FIT_STRETCH"'/>"
"					<menuitem action='"ACTION_VIEWER_ZOOM_FILL_SCREEN"'/>"
"				</menu>"
"				<separator/>"
"				<menuitem action='"ACTION_VIEWER_ROTATE_FOR_BEST_FIT"'/>"
"			</placeholder>"
"		</menu>"
"		<placeholder name='MenuImage'>"
"			<menu action='MenuImage'>"
"				<menuitem action='"ACTION_VIEWER_ROTATE_CW"'/>"
"				<menuitem action='"ACTION_VIEWER_ROTATE_CCW"'/>"
"				<separator/>"
"				<menuitem action='"ACTION_VIEWER_FLIP_H"'/>"
"				<menuitem action='"ACTION_VIEWER_FLIP_V"'/>"
"			</menu>"
"		</placeholder>"
"		<menu action='MenuGo'>"
"			<placeholder name='ImageNavigation'>"
"				<menuitem action='"ACTION_VIEWER_FIRST"'/>"
"				<menuitem action='"ACTION_VIEWER_PREVIOUS"'/>"
"				<menuitem action='"ACTION_VIEWER_NEXT"'/>"
"				<menuitem action='"ACTION_VIEWER_LAST"'/>"
"			</placeholder>"
"			<separator/>"
"			<placeholder name='FolderNavigation'/>"
"			<separator/>"
"		</menu>"
#ifdef QUIVER_MAEMO
"	</popup>"
#else
"	</menubar>"
#endif
"	<toolbar name='ToolbarMain'>"
"		<placeholder name='NavToolItems'>"
"			<separator/>"
"			<toolitem action='"ACTION_VIEWER_PREVIOUS"'/>"
"			<toolitem action='"ACTION_VIEWER_NEXT"'/>"
"			<separator/>"
"		</placeholder>"
"		<placeholder name='ZoomToolItems'>"
"			<toolitem action='"ACTION_VIEWER_ZOOM_IN"'/>"
"			<toolitem action='"ACTION_VIEWER_ZOOM_OUT"'/>"
"			<toolitem action='"ACTION_VIEWER_ZOOM_100"'/>"
"			<toolitem action='"ACTION_VIEWER_ZOOM_FIT"'/>"
//"			<toolitem action='"ACTION_VIEWER_ZOOM_FIT_STRETCH"'/>"
"			<separator/>"
#ifdef QUIVER_MAEMO
"			<toolitem action='"ACTION_VIEWER_ROTATE_FOR_BEST_FIT"'/>"
#endif
"		</placeholder>"
"		<placeholder name='TransformToolItems'>"
"			<toolitem action='"ACTION_VIEWER_ROTATE_CCW"'/>"
"			<toolitem action='"ACTION_VIEWER_ROTATE_CW"'/>"
"		</placeholder>"
"		<placeholder name='Trash'>"
"			<toolitem action='"ACTION_VIEWER_TRASH"'/>"
"		</placeholder>"
"	</toolbar>"
"	<popup name='ContextMenuMain'>"
"		<menuitem action='"ACTION_VIEWER_COPY"'/>"
"		<separator/>"
"		<menuitem action='"ACTION_VIEWER_ROTATE_CCW"'/>"
"		<menuitem action='"ACTION_VIEWER_ROTATE_CW"'/>"
"		<separator/>"
"		<menuitem action='"ACTION_VIEWER_TRASH"'/>"
"	</popup>"
"	<accelerator action='"ACTION_VIEWER_ROTATE_CW_2"'/>"
"	<accelerator action='"ACTION_VIEWER_ROTATE_CCW_2"'/>"
"	<accelerator action='"ACTION_VIEWER_FLIP_H_2"'/>"
"	<accelerator action='"ACTION_VIEWER_FLIP_V_2"'/>"
"	<accelerator action='"ACTION_VIEWER_PREVIOUS_2"'/>"
"	<accelerator action='"ACTION_VIEWER_NEXT_2"'/>"
#ifdef QUIVER_MAEMO
"	<accelerator action='"ACTION_VIEWER_ZOOM_IN_MAEMO"'/>"
"	<accelerator action='"ACTION_VIEWER_ZOOM_OUT_MAEMO"'/>"
#endif
"</ui>";



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
#ifdef QUIVER_MAEMO
	ACTION_VIEWER_ZOOM_IN_MAEMO,
	ACTION_VIEWER_ZOOM_OUT_MAEMO,
#endif
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
	
/*	{ "MenuFile", NULL, N_("_File") }, */
	{ ACTION_VIEWER_CUT, QUIVER_STOCK_CUT, "_Cut", "<Control>X", "Cut image", G_CALLBACK(viewer_action_handler_cb)},
	{ ACTION_VIEWER_COPY, QUIVER_STOCK_COPY, "Copy", "<Control>C", "Copy image", G_CALLBACK(viewer_action_handler_cb)},
#ifdef QUIVER_MAEMO
	{ ACTION_VIEWER_TRASH, QUIVER_STOCK_DELETE, "_Delete", "Delete", "Delete image", G_CALLBACK(viewer_action_handler_cb)},
#else
	{ ACTION_VIEWER_TRASH, QUIVER_STOCK_DELETE, "_Move To Trash", "Delete", "Move image to the Trash", G_CALLBACK(viewer_action_handler_cb)},
#endif
	
	{ ACTION_VIEWER_PREVIOUS, QUIVER_STOCK_GO_BACK, "_Previous Image", "BackSpace", "Go to previous image", G_CALLBACK(viewer_action_handler_cb)},
	{ ACTION_VIEWER_PREVIOUS_2, QUIVER_STOCK_GO_BACK, "_Previous Image", "<Shift>space", "Go to previous image", G_CALLBACK(viewer_action_handler_cb)},
	{ ACTION_VIEWER_NEXT, QUIVER_STOCK_GO_FORWARD, "_Next Image", "space", "Go to next image", G_CALLBACK(viewer_action_handler_cb)},
	{ ACTION_VIEWER_NEXT_2, QUIVER_STOCK_GO_FORWARD, "_Next Image", "<Shift>BackSpace", "Go to next image", G_CALLBACK(viewer_action_handler_cb)},
#ifdef QUIVER_MAEMO
	{ ACTION_VIEWER_ZOOM_IN_MAEMO, QUIVER_STOCK_ZOOM_IN,"Zoom _In", "F7", "Zoom In", G_CALLBACK(viewer_action_handler_cb)},
	{ ACTION_VIEWER_ZOOM_OUT_MAEMO, QUIVER_STOCK_ZOOM_OUT,"Zoom _Out", "F8", "Zoom Out", G_CALLBACK(viewer_action_handler_cb)},
#endif
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
};


static GtkRadioActionEntry zoom_radio_action_entries[] = {
	{ ACTION_VIEWER_ZOOM_FIT, QUIVER_STOCK_ZOOM_FIT,"Zoom _Fit", "<Control>1", "Fit to Window",QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW},
	{ ACTION_VIEWER_ZOOM_FIT_STRETCH, QUIVER_STOCK_ZOOM_FIT,"Zoom _Fit Stretch", "", "Fit to Window Stretch",QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW_STRETCH},
	{ ACTION_VIEWER_ZOOM_100, QUIVER_STOCK_ZOOM_100, "_Actual Size", "<Control>0", "Actual Size",QUIVER_IMAGE_VIEW_MODE_ACTUAL_SIZE},
	{ ACTION_VIEWER_ZOOM_FILL_SCREEN, "", "Fill Screen", NULL, "Fill the screen with the image",QUIVER_IMAGE_VIEW_MODE_FILL_SCREEN},
	{ ACTION_VIEWER_ZOOM, "", NULL, NULL, NULL, QUIVER_IMAGE_VIEW_MODE_ZOOM},
};


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
		gdk_threads_enter();
		quiver_image_view_set_pixbuf(m_pImageView,pixbuf);
		gdk_threads_leave();
	};
	virtual void SetPixbufAtSize(GdkPixbuf *pixbuf, gint width, gint height, bool bResetViewMode = true ){
		gdk_threads_enter();
		gboolean bReset = bResetViewMode ? TRUE : FALSE;
		quiver_image_view_set_pixbuf_at_size_ex(m_pImageView,pixbuf,width,height,bReset);
		gdk_threads_leave();
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
			gtk_image_set_from_pixbuf(GTK_IMAGE(m_pPlayImage), m_pPixbufPause);

			m_iTimeoutPlayProgress = g_timeout_add(200,timeout_play_position,this);
		}
		else
		{
			gtk_image_set_from_pixbuf(GTK_IMAGE(m_pPlayImage), m_pPixbufPlay);
		}
	}

	// methods for playing videos
	void PlayPauseVideo();
	void UpdateTimeline();
	void StopVideo(bool reloadImage = true);
	// returns true if current item is a video
	bool IsVideo();


// member variables

	GtkWidget *m_pIconView;
	GtkWidget *m_pImageView;

	GtkWidget * m_pTable;
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
	GtkWidget* m_pTimeline;
	GtkWidget* m_pTimeLabel;
	GtkWidget* m_pPlayProgress;

	GdkPixbuf* m_pPixbufPlayHighlight;
	GdkPixbuf* m_pPixbufPlay;
	GdkPixbuf* m_pPixbufPauseHighlight;
	GdkPixbuf* m_pPixbufPause;

	QuiverFile m_QuiverFileCurrent;

	int m_iCurrentOrientation;
	
	GtkUIManager *m_pUIManager;
	guint m_iMergedViewerUI;

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

	int   m_iSlideShowDuration;
	int   m_iSlideShowWaitCount;
	bool  m_bSlideShowLoop;

	bool m_bMaximizeViewableArea;
	bool m_bMaximizeViewabe;

	bool m_bIsPlaying;
	bool m_bWasPlayingBeforeSeek;

	ImageCache m_ThumbnailCache;

	// gstreamer elements for playing videos
	GstElement* m_pPipeline;
	GstElement* m_pXVImageSink;


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
			QuiverUtils::SetActionsSensitive(m_pUIManager, pszActionsNext, G_N_ELEMENTS(pszActionsNext), FALSE);
			QuiverUtils::SetActionsSensitive(m_pUIManager, pszActionsPrev, G_N_ELEMENTS(pszActionsPrev), FALSE);
		}
		else if (m_ImageListPtr->GetCurrentIndex() == m_ImageListPtr->GetSize() - 1 )
		{
			// disable next
			QuiverUtils::SetActionsSensitive(m_pUIManager, pszActionsNext, G_N_ELEMENTS(pszActionsNext), FALSE);

			// enable previous
			QuiverUtils::SetActionsSensitive(m_pUIManager, pszActionsPrev, G_N_ELEMENTS(pszActionsPrev), TRUE);
		}
		else if ( 0 == m_ImageListPtr->GetCurrentIndex() ) 
		{
			// disable previous
			QuiverUtils::SetActionsSensitive(m_pUIManager, pszActionsPrev, G_N_ELEMENTS(pszActionsPrev), FALSE);

			// enable next
			QuiverUtils::SetActionsSensitive(m_pUIManager, pszActionsNext, G_N_ELEMENTS(pszActionsNext), TRUE);

		}
		else
		{
			// enable both
			QuiverUtils::SetActionsSensitive(m_pUIManager, pszActionsPrev, G_N_ELEMENTS(pszActionsPrev), TRUE);
			QuiverUtils::SetActionsSensitive(m_pUIManager, pszActionsNext, G_N_ELEMENTS(pszActionsNext), TRUE);
		}
		QuiverUtils::SetActionsSensitive(m_pUIManager, pszActionsImage, G_N_ELEMENTS(pszActionsImage), TRUE);
	}
	else
	{
		// disable both
		QuiverUtils::SetActionsSensitive(m_pUIManager, pszActionsPrev, G_N_ELEMENTS(pszActionsPrev), FALSE);
		QuiverUtils::SetActionsSensitive(m_pUIManager, pszActionsNext, G_N_ELEMENTS(pszActionsNext), FALSE);
		QuiverUtils::SetActionsSensitive(m_pUIManager, pszActionsImage, G_N_ELEMENTS(pszActionsImage), FALSE);
		
	}
	
	if (!quiver_image_view_can_magnify(QUIVER_IMAGE_VIEW(m_pImageView), TRUE))
	{
		GtkAction* action;
		action = QuiverUtils::GetAction(m_pUIManager, ACTION_VIEWER_ZOOM_IN);
		if (NULL != action)
		{
			gtk_action_set_sensitive(action,FALSE);
		}
#ifdef QUIVER_MAEMO
		action = QuiverUtils::GetAction(m_pUIManager, ACTION_VIEWER_ZOOM_IN_MAEMO);
		if (NULL != action)
		{
			gtk_action_set_sensitive(action,FALSE);
		}
#endif
	}

	if (!quiver_image_view_can_magnify(QUIVER_IMAGE_VIEW(m_pImageView), FALSE))
	{
		GtkAction* action;
		action = QuiverUtils::GetAction(m_pUIManager, ACTION_VIEWER_ZOOM_OUT);
		if (NULL != action)
		{
			gtk_action_set_sensitive(action,FALSE);
		}
#ifdef QUIVER_MAEMO
		action = QuiverUtils::GetAction(m_pUIManager, ACTION_VIEWER_ZOOM_OUT_MAEMO);
		if (NULL != action)
		{
			gtk_action_set_sensitive(action,FALSE);
		}
#endif
	}

	PreferencesPtr prefsPtr = Preferences::GetInstance();
	GtkAction* action;

	if (0 != m_iTimeoutSlideshowID)
	{
		bool bMaximize = prefsPtr->GetBoolean(QUIVER_PREFS_SLIDESHOW, QUIVER_PREFS_SLIDESHOW_ROTATE_FOR_BEST_FIT, false);
		action = QuiverUtils::GetAction(m_pUIManager, ACTION_VIEWER_ROTATE_FOR_BEST_FIT);
		if (NULL != action)
		{
			gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action),bMaximize ? TRUE : FALSE);
		}

		bool bHideFilmStrip = prefsPtr->GetBoolean(QUIVER_PREFS_SLIDESHOW, QUIVER_PREFS_SLIDESHOW_FILMSTRIP_HIDE, true);

		// hide film strip if necessary
		action = QuiverUtils::GetAction(m_pUIManager,ACTION_VIEWER_VIEW_FILM_STRIP);
		if (NULL != action)
		{
			gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action),bHideFilmStrip ? FALSE : TRUE);
		}


	}
	else
	{
		bool bMaximize = prefsPtr->GetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_ROTATE_FOR_BEST_FIT, false);
		action = QuiverUtils::GetAction(m_pUIManager, ACTION_VIEWER_ROTATE_FOR_BEST_FIT);
		if (NULL != action)
		{
			gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action),bMaximize ? TRUE : FALSE);
		}

		bool bShowFilmStrip = 	prefsPtr->GetBoolean(QUIVER_PREFS_VIEWER,QUIVER_PREFS_VIEWER_FILMSTRIP_SHOW);
		GtkAction* action = QuiverUtils::GetAction(m_pUIManager,ACTION_VIEWER_VIEW_FILM_STRIP);
		if (NULL != action)
		{
			gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action),bShowFilmStrip ? TRUE : FALSE);
		}
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
	
	GtkAdjustment *h = m_pAdjustmentH;
	GtkAdjustment *v = m_pAdjustmentV;
	
	
	gint sb_width = pScrollbarV->allocation.width;
	gint sb_height = pScrollbarH->allocation.height;
	
	GtkWidget * pNavigationBox = m_pNavigationBox;

	GtkTableChild * child = GetGtkTableChild(GTK_TABLE(m_pTable),m_pImageView);
	
	gint area_w = m_pTable->allocation.width;
	gint area_h = m_pTable->allocation.height;

	PreferencesPtr prefsPtr = Preferences::GetInstance();
	bool bHideScrollbars = 	prefsPtr->GetBoolean(QUIVER_PREFS_VIEWER,QUIVER_PREFS_VIEWER_SCROLLBARS_HIDE);
	
	if (bHideScrollbars || QUIVER_IMAGE_VIEW_MODE_FILL_SCREEN == view_mode)
	{
		//FIXME: just don't show scrollbars because they jump around in an
		// infinite loop for certain situations in this mode
		// hide h hide v
		gtk_widget_hide (pScrollbarV); 	
		gtk_widget_hide (pScrollbarH);
		gtk_widget_hide (pNavigationBox);

		child->bottom_attach = 1;
		child->right_attach = 1;
	}
	else if ( (area_w < width && area_h < height) ||
		(area_w < width && area_h - sb_height < height) ||
		(area_w - sb_width < width && area_h < height) )
	{
		// show h show v
		child->bottom_attach = 1;
		child->right_attach = 1;

		gtk_widget_show (pScrollbarV); 	
		gtk_widget_show (pScrollbarH);
		gtk_widget_show (pNavigationBox);
	}
	else if (area_w < width)
	{
		// show h hide v
		child->right_attach = 2;
		child->bottom_attach = 1;
		gtk_widget_show (pNavigationBox);	
		gtk_widget_hide (pScrollbarV);
		gtk_widget_show (pScrollbarH);		
	}
	else if (area_h < height)
	{
		// hide h show v
		child->bottom_attach = 2;
		child->right_attach = 1;
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

		child->bottom_attach = 1;
		child->right_attach = 1;
	}

	m_iTimeoutScrollbars = 0;

	if (h->page_size >= h->upper &&
		v->page_size >= v->upper)
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
	
	if (mode != QUIVER_IMAGE_VIEW_MODE_ACTUAL_SIZE && GTK_WIDGET_REALIZED(m_pImageView))
	{
		width = m_pImageView->allocation.width;
		height = m_pImageView->allocation.height;
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
	else if (mode != QUIVER_IMAGE_VIEW_MODE_ACTUAL_SIZE && GTK_WIDGET_REALIZED(m_pImageView))
	{
		width = m_pImageView->allocation.width;
		height = m_pImageView->allocation.height;
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
		
		if (IsVideo())
		{
			// show media controls
			gtk_widget_show(m_pMediaControls);
		}
		else
		{
			// hide them
			gtk_widget_hide(m_pMediaControls);
		}

		gtk_window_resize (GTK_WINDOW (m_pNavigationWindow),1,1);
		QuiverFile f = m_ImageListPtr->GetCurrent();
		GdkPixbuf *pixbuf = f.GetThumbnail(128);
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
		width = m_pImageView->allocation.width;
		height = m_pImageView->allocation.height;

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

}

static gboolean 
timeout_event_motion_notify (gpointer user_data)
{
	Viewer::ViewerImpl *pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)user_data;
	gdk_threads_enter();
	gint x, y;
	gtk_widget_get_pointer(pViewerImpl->m_pMediaControls, &x, &y);
	GtkAllocation a = {0};
	gtk_widget_get_allocation(pViewerImpl->m_pMediaControls, &a);

	if (0 >= x || 0 >= y || x > a.width || y > a.height)
	{
		// hide the controls
		if (pViewerImpl->IsPlaying())
			gtk_widget_hide(pViewerImpl->m_pMediaControls);
	}
	gdk_threads_leave();
	pViewerImpl->m_iTimeoutMouseMotionNotify = 0;
	return FALSE;
}


static gboolean
viewer_motion_notify(GtkWidget* widget, GdkEventMotion* event, gpointer user_data)
{
	Viewer::ViewerImpl *pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)user_data;
	if (widget == pViewerImpl->m_pImageView)
	{
		if (0 != pViewerImpl->m_iTimeoutMouseMotionNotify)
		{
			g_source_remove(pViewerImpl->m_iTimeoutMouseMotionNotify);
			pViewerImpl->m_iTimeoutMouseMotionNotify = 0;
		}

		if (0 != pViewerImpl->m_ImageListPtr->GetSize() && 
				pViewerImpl->m_ImageListPtr->GetCurrent().IsVideo())
			gtk_widget_show(pViewerImpl->m_pMediaControls);

		if (pViewerImpl->IsPlaying())
			pViewerImpl->m_iTimeoutMouseMotionNotify = g_timeout_add(1500,timeout_event_motion_notify,pViewerImpl);
	}
	else if (widget == pViewerImpl->m_pPlayProgress)
	{
		if (gdk_pointer_is_grabbed())
		{
			GtkAllocation allocation = {0};
			gtk_widget_get_allocation(widget, &allocation);

			GstFormat format = GST_FORMAT_TIME;
			gint64 clip_duration = 0;

			gboolean queried = gst_element_query_duration(GST_ELEMENT(pViewerImpl->m_pPipeline), &format, &clip_duration);
			if (queried)
			{
				gboolean seek_started = gst_element_seek_simple(GST_ELEMENT(pViewerImpl->m_pPipeline), format, GstSeekFlags(GST_SEEK_FLAG_FLUSH), ((clip_duration * event->x) / allocation.width));
			}

		}
	}
		
	return FALSE;
}

static gboolean
viewer_play_button_mouse_in(GtkWidget* widget, GdkEvent* event, gpointer user_data)
{
	Viewer::ViewerImpl *pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)user_data;

	if (pViewerImpl->IsPlaying())
		gtk_image_set_from_pixbuf(GTK_IMAGE(pViewerImpl->m_pPlayImage), pViewerImpl->m_pPixbufPauseHighlight);
	else
		gtk_image_set_from_pixbuf(GTK_IMAGE(pViewerImpl->m_pPlayImage), pViewerImpl->m_pPixbufPlayHighlight);
	return FALSE;
}

static gboolean
viewer_play_button_mouse_out(GtkWidget* widget, GdkEvent* event, gpointer user_data)
{
	Viewer::ViewerImpl *pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)user_data;

	if (pViewerImpl->IsPlaying())
		gtk_image_set_from_pixbuf(GTK_IMAGE(pViewerImpl->m_pPlayImage), pViewerImpl->m_pPixbufPause);
	else
		gtk_image_set_from_pixbuf(GTK_IMAGE(pViewerImpl->m_pPlayImage), pViewerImpl->m_pPixbufPlay);
	
	return FALSE;
}

static void viewer_radio_action_handler_cb(GtkRadioAction *action, GtkRadioAction *current, gpointer user_data)
{
	viewer_action_handler_cb(GTK_ACTION(action), user_data);
}

static void viewer_action_handler_cb(GtkAction *action, gpointer data)
{
	Viewer::ViewerImpl *pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)data;
	
	QuiverImageView *imageview = QUIVER_IMAGE_VIEW(pViewerImpl->m_pImageView);

	PreferencesPtr prefsPtr = Preferences::GetInstance();
	
	//printf("Viewer Action: %s\n",gtk_action_get_name(action));
	
	const gchar * szAction = gtk_action_get_name(action);
	
	if (0 == strcmp(szAction, ACTION_VIEWER_ZOOM_FIT))
	{
		QuiverImageViewMode zoom_mode = (QuiverImageViewMode)gtk_radio_action_get_current_value(GTK_RADIO_ACTION(action));
		quiver_image_view_set_view_mode(imageview,zoom_mode);		
	}
	else if (0 == strcmp(szAction, ACTION_VIEWER_ZOOM_IN)
#ifdef QUIVER_MAEMO
	        || 0 == strcmp(szAction, ACTION_VIEWER_ZOOM_IN_MAEMO)
#endif
	)
	{
		if (QUIVER_IMAGE_VIEW_MODE_ZOOM != quiver_image_view_get_view_mode(imageview))
			quiver_image_view_set_view_mode(imageview,QUIVER_IMAGE_VIEW_MODE_ZOOM);
				
		quiver_image_view_set_magnification(imageview,
						quiver_image_view_get_magnification(imageview)*2);
	}
	else if (0 == strcmp(szAction, ACTION_VIEWER_ZOOM_OUT)
#ifdef QUIVER_MAEMO
	        || 0 == strcmp(szAction, ACTION_VIEWER_ZOOM_OUT_MAEMO)
#endif
	)
	{
		if (QUIVER_IMAGE_VIEW_MODE_ZOOM != quiver_image_view_get_view_mode(imageview))
			quiver_image_view_set_view_mode(imageview,QUIVER_IMAGE_VIEW_MODE_ZOOM);
				
		quiver_image_view_set_magnification(imageview,
			quiver_image_view_get_magnification(imageview)/2);
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
	else if (0 == strcmp(szAction, ACTION_VIEWER_FIRST))
	{
		pViewerImpl->SetImageIndex(0,true);
	}
	else if (0 == strcmp(szAction, ACTION_VIEWER_PREVIOUS) || 0 == strcmp(szAction, ACTION_VIEWER_PREVIOUS_2)) 
	{
		pViewerImpl->SetImageIndex(pViewerImpl->m_ImageListPtr->GetCurrentIndex()-1,false);
	}
	else if (0 == strcmp(szAction, ACTION_VIEWER_NEXT) || 0 == strcmp(szAction, ACTION_VIEWER_NEXT_2)) 
	{
		pViewerImpl->SetImageIndex(pViewerImpl->m_ImageListPtr->GetCurrentIndex()+1,true);
	}
	else if (0 == strcmp(szAction, ACTION_VIEWER_LAST))
	{
		pViewerImpl->SetImageIndex(pViewerImpl->m_ImageListPtr->GetSize()-1,false);
	}
	else if (0 == strcmp(szAction, ACTION_VIEWER_VIEW_FILM_STRIP))
	{

		if( gtk_toggle_action_get_active(GTK_TOGGLE_ACTION(action)) )
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
			prefsPtr->SetBoolean(QUIVER_PREFS_SLIDESHOW, QUIVER_PREFS_SLIDESHOW_FILMSTRIP_HIDE, !gtk_toggle_action_get_active(GTK_TOGGLE_ACTION(action)));
		}
		else
		{
			prefsPtr->SetBoolean(QUIVER_PREFS_VIEWER,QUIVER_PREFS_VIEWER_FILMSTRIP_SHOW,gtk_toggle_action_get_active(GTK_TOGGLE_ACTION(action)));
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
				(TRUE == gtk_toggle_action_get_active(GTK_TOGGLE_ACTION(action)));

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
				ImageLoader::LoadParams params = {0};
				params.max_width = pViewerImpl->m_pImageView->allocation.width;
				params.max_height = pViewerImpl->m_pImageView->allocation.height;
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
				(TRUE == gtk_toggle_action_get_active(GTK_TOGGLE_ACTION(action)));

			prefsPtr->SetBoolean(QUIVER_PREFS_VIEWER, 
					QUIVER_PREFS_VIEWER_ROTATE_FOR_BEST_FIT, 
					pViewerImpl->m_bMaximizeViewableArea);
		}
	}
}

static gboolean viewer_scrollwheel_event(GtkWidget *widget, GdkEventScroll *event, gpointer data )
{

	Viewer::ViewerImpl *pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)data;

	if (event->state & GDK_CONTROL_MASK || event->state & GDK_SHIFT_MASK)
		return FALSE;

	
	if (GDK_SCROLL_UP == event->direction || GDK_SCROLL_LEFT == event->direction)
	{
		pViewerImpl->SetImageIndex(pViewerImpl->m_ImageListPtr->GetCurrentIndex()-1,false);
	}
	else if (GDK_SCROLL_DOWN == event->direction || GDK_SCROLL_RIGHT == event->direction)
	{
		pViewerImpl->SetImageIndex(pViewerImpl->m_ImageListPtr->GetCurrentIndex()+1,true);
	}


	return TRUE;
}


static void viewer_imageview_activated(QuiverImageView *imageview,gpointer data)
{
	Viewer::ViewerImpl *pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)data;
	pViewerImpl->m_pViewer->EmitItemActivatedEvent();
}

static void viewer_imageview_reload(QuiverImageView *imageview,gpointer data)
{
	//printf("#### got a reload message from the imageview\n");
	Viewer::ViewerImpl *pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)data;
	ImageLoader::LoadParams params = {0};

	params.orientation = pViewerImpl->GetCurrentOrientation(true);
	params.reload = true;
	params.fullsize = true;
	params.no_thumb_preview = true;
	params.state = ImageLoader::LOAD;

	pViewerImpl->m_ImageLoader.LoadImage(pViewerImpl->m_ImageListPtr->GetCurrent(),params);
}


static void viewer_imageview_magnification_changed(QuiverImageView *imageview,gpointer data)
{
	Viewer::ViewerImpl *pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)data;

	double mag = quiver_image_view_get_magnification(QUIVER_IMAGE_VIEW(pViewerImpl->m_pImageView));
	pViewerImpl->m_StatusbarPtr->SetMagnification((int)(mag*100+.5));
	
	pViewerImpl->UpdateUI();

}

static void viewer_imageview_view_mode_changed(QuiverImageView *imageview,gpointer data)
{
	Viewer::ViewerImpl *pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)data;
	
	QuiverImageViewMode mode = quiver_image_view_get_view_mode(QUIVER_IMAGE_VIEW(pViewerImpl->m_pImageView));

	GtkAction* action = NULL;
	switch (mode)
	{
		case QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW:
			action = QuiverUtils::GetAction(pViewerImpl->m_pUIManager, ACTION_VIEWER_ZOOM_FIT);
			break;
		case QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW_STRETCH:
			action = QuiverUtils::GetAction(pViewerImpl->m_pUIManager, ACTION_VIEWER_ZOOM_FIT_STRETCH);
			break;
		case QUIVER_IMAGE_VIEW_MODE_ACTUAL_SIZE:
			action = QuiverUtils::GetAction(pViewerImpl->m_pUIManager, ACTION_VIEWER_ZOOM_100);
			break;
		case QUIVER_IMAGE_VIEW_MODE_ZOOM:
			action = QuiverUtils::GetAction(pViewerImpl->m_pUIManager, ACTION_VIEWER_ZOOM);
			break;
		case QUIVER_IMAGE_VIEW_MODE_FILL_SCREEN:
			action = QuiverUtils::GetAction(pViewerImpl->m_pUIManager, ACTION_VIEWER_ZOOM_FILL_SCREEN);
		case QUIVER_IMAGE_VIEW_MODE_COUNT:
		default:
			break;
	}

	if (NULL != action)
	{
		gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action),TRUE);
	}
}

static gboolean viewer_imageview_key_press_event(GtkWidget *imageview, GdkEventKey *event, gpointer userdata)
{
	Viewer::ViewerImpl *pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)userdata;

	gboolean rval = FALSE;
	bool bPanMode = true;

	GtkAdjustment *h = pViewerImpl->m_pAdjustmentH;
	GtkAdjustment *v = pViewerImpl->m_pAdjustmentV;

	if (h->page_size >= h->upper &&
		v->page_size >= v->upper)
	{
		bPanMode = false;

	}
	
	GtkAdjustment *adjustment = NULL;
	gdouble increment = 0.;

	if (GDK_Left == event->keyval || GDK_Up == event->keyval)
	{
		if (bPanMode)
		{
			if (GDK_Left == event->keyval)
			{
				adjustment = h;
			}
			else
			{
				adjustment = v;
			}
			increment = -adjustment->step_increment;
			
		}
		else
		{
			GtkAction* action = QuiverUtils::GetAction(pViewerImpl->m_pUIManager, ACTION_VIEWER_PREVIOUS);
			gtk_action_activate(action);
		}
		rval = TRUE;
	}
	else if (GDK_Right == event->keyval || GDK_Down == event->keyval)
	{
		if (bPanMode)
		{
			if (GDK_Right == event->keyval)
			{
				adjustment = h;
			}
			else
			{
				adjustment = v;
			}
			increment = adjustment->step_increment;
		}
		else
		{
			GtkAction* action = QuiverUtils::GetAction(pViewerImpl->m_pUIManager, ACTION_VIEWER_NEXT);
			gtk_action_activate(action);
		}
		rval = TRUE;
	}

	if (NULL != adjustment)
	{
		gdouble value = gtk_adjustment_get_value(adjustment);
		value += increment;

		if (value < adjustment->lower)
		{
			value = adjustment->lower;
		}
		else if (value > adjustment->upper - adjustment->page_size)
		{
			value = adjustment->upper - adjustment->page_size;
		}
		gtk_adjustment_set_value(adjustment,value);
	}

	return rval;
}

static void viewer_iconview_cell_activated(QuiverIconView *iconview,gulong cell,gpointer data)
{
	Viewer::ViewerImpl *pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)data;

	//pViewerImpl->m_pViewer->EmitItemActivatedEvent();
}

static void viewer_iconview_cursor_changed(QuiverIconView *iconview,gulong cell,gpointer data)
{
	Viewer::ViewerImpl *pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)data;

	bool bDirectionForward = false;

	if (pViewerImpl->m_ImageListPtr->GetSize() && pViewerImpl->m_ImageListPtr->GetCurrentIndex() < cell)
	{
		bDirectionForward = true;
	}

	pViewerImpl->SetImageIndex(cell,bDirectionForward);

}

static gboolean
viewer_navigation_button_press_event(GtkWidget *widget, GdkEventButton *event, gpointer userdata)
{
	Viewer::ViewerImpl *pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)userdata;
	
	GdkModifierType state;
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
  		
	
  	if (gdk_screen_width () < pos_x + ww)
  		pos_x = gdk_screen_width() - ww;
  	else if (0 > pos_x)
  		pos_x = 0;
	
  	if (gdk_screen_height () < pos_y + wh)
  		pos_y = gdk_screen_height() - wh;
  	else if (0 > pos_y)
  		pos_y = 0;
	
	gtk_window_move (GTK_WINDOW (pViewerImpl->m_pNavigationWindow), pos_x, pos_y);

	GdkCursor *cursor;	
	cursor = gdk_cursor_new (GDK_FLEUR); 
		
	gdk_pointer_grab (pViewerImpl->m_pNavigationWindow->window,  TRUE, (GdkEventMask)(GDK_BUTTON_RELEASE_MASK  | GDK_POINTER_MOTION_HINT_MASK    | GDK_BUTTON_MOTION_MASK   | GDK_EXTENSION_EVENTS_ALL),
			  pViewerImpl->m_pNavigationControl->window, 
			  cursor,
			  GDK_CURRENT_TIME);

	gdk_cursor_unref (cursor);
	return TRUE;
}

gboolean navigation_control_button_release_event (GtkWidget *widget, GdkEventButton *event, gpointer data )
{
	Viewer::ViewerImpl *pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)data;

	gdk_pointer_ungrab (event->time);

	gtk_widget_hide_all (pViewerImpl->m_pNavigationWindow);
	return TRUE;	
}

static void signal_drag_data_get  (GtkWidget *widget, GdkDragContext *context, GtkSelectionData *selection_data, guint info, guint time,gpointer user_data)
{
	Viewer::ViewerImpl *pViewerImpl = (Viewer::ViewerImpl*)user_data;
	
	
	
	if (info == QUIVER_TARGET_STRING)
    {
		if (pViewerImpl->m_ImageListPtr->GetSize())
		{
    		gtk_selection_data_set (selection_data,
			    selection_data->target,
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
			    selection_data->target,
			    8, (const guchar*)pViewerImpl->m_ImageListPtr->GetCurrent().GetURI(),strlen(pViewerImpl->m_ImageListPtr->GetCurrent().GetURI()));
		}
	}
  	else
	{
		gtk_selection_data_set (selection_data,
				selection_data->target,
				8, (const guchar*)"I'm Data!", 9);
	}
}

static void signal_drag_data_delete  (GtkWidget *widget,GdkDragContext *context,gpointer user_data)
{
	Viewer::ViewerImpl *pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)user_data;
}

static void signal_drag_data_received(GtkWidget *widget,GdkDragContext *drag_context, gint x,gint y, GtkSelectionData *data, guint info, guint time,gpointer user_data)
{
	Viewer::ViewerImpl *pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)user_data;
}

static void signal_drag_begin (GtkWidget *widget,GdkDragContext *drag_context,gpointer user_data)
{
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
{
	Viewer::ViewerImpl *pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)user_data;
}

static void signal_drag_motion (GtkWidget *widget, GdkDragContext *context, gint x, gint y, guint time, gpointer user_data)
{
	Viewer::ViewerImpl *pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)user_data;
}


static gboolean signal_drag_drop (GtkWidget *widget, GdkDragContext *drag_context, gint x, gint y, guint time,  gpointer user_data)
{
	Viewer::ViewerImpl *pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)user_data;
	return TRUE;

}

static gboolean viewer_popup_menu_cb (GtkWidget *widget, gpointer userdata)
{
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
		str = g_strdup_printf("%lld:%02lld:%02lld", hours, mins, secs);
	else
		str = g_strdup_printf("%lld:%02lld", mins, secs);
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
{
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
				GError *error;

				gst_message_parse_error (msg, &error, &debug);
				g_free (debug);

				g_printerr ("error: %s\n", error->message);
				g_error_free (error);

				pViewerImpl->StopVideo(true);

				break;
			}
		default:
			break;
	}

	return TRUE;
}

static GstBusSyncReply
gstreamer_bus_sync_handler (GstBus * bus, GstMessage * message, gpointer user_data)
{
	Viewer::ViewerImpl *pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)user_data;

	// ignore anything but 'prepare-xwindow-id' element messages
	if (GST_MESSAGE_TYPE (message) != GST_MESSAGE_ELEMENT)
		return GST_BUS_PASS;
	if (!gst_structure_has_name (message->structure, "prepare-xwindow-id"))
		return GST_BUS_PASS;

	GstXOverlay *xoverlay;
	// GST_MESSAGE_SRC (message) will be the video sink element
	xoverlay = GST_X_OVERLAY (GST_MESSAGE_SRC (message));

	gulong video_area_xid = 0;

	gdk_threads_enter();
	video_area_xid = GDK_WINDOW_XID (gtk_widget_get_window(pViewerImpl->m_pImageView));
	gst_x_overlay_set_window_handle (xoverlay, video_area_xid);
	gdk_threads_leave();

	gst_message_unref (message);
	return GST_BUS_DROP;
}

void Viewer::ViewerImpl::UpdateTimeline()
{
	GstFormat fmt = GST_FORMAT_TIME;

	gint64 pos = 0, len = 0;
	bool success = gst_element_query_position(m_pPipeline, &fmt, &pos);
	success |= gst_element_query_duration(m_pPipeline, &fmt, &len);

	if (!success || len < 0 || pos < 0 )
	{
		pos = 0;
		len = 0;
	}

	if (0 == pos && 0 == len)
	{
		gtk_widget_hide(m_pTimeline);
	}
	else
	{
		gtk_widget_show(m_pTimeline);
	}

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

	gchar* uri = NULL;
	g_object_get(G_OBJECT(m_pPipeline), "uri", &uri, NULL);
	if (0 == g_strcmp0(uri, m_ImageListPtr->GetCurrent().GetURI()))
	{
		gtk_widget_set_double_buffered (m_pImageView, FALSE);
		GstState current;
		// has the right video
		GstStateChangeReturn rval = gst_element_get_state(GST_ELEMENT(m_pPipeline), &current, NULL, GST_SECOND);
		if (GST_STATE_CHANGE_SUCCESS == rval)
		{
			if (GST_STATE_PLAYING == current)
			{
				gst_element_set_state(GST_ELEMENT(m_pPipeline), GST_STATE_PAUSED);
				gtk_widget_show(m_pMediaControls);
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
		quiver_image_view_set_pixbuf(QUIVER_IMAGE_VIEW(m_pImageView), NULL);
		gtk_widget_set_double_buffered (m_pImageView, FALSE);
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
	gtk_widget_set_double_buffered (m_pImageView, TRUE);
	gst_x_overlay_set_window_handle (GST_X_OVERLAY(m_pXVImageSink), 0);

	UpdateTimeline();

	if (IsVideo())
	{
		gtk_widget_show(m_pMediaControls);
	}

	if (reloadImage && 0 != m_ImageListPtr->GetSize())
	{
		if (GTK_WIDGET_MAPPED(m_pImageView))
			gdk_window_invalidate_rect(m_pImageView->window, NULL, TRUE);
		if (0 == m_iTimeoutSlideshowID)
			LoadImage(m_ImageListPtr->GetCurrent());
	}
}

bool Viewer::ViewerImpl::IsVideo()
{
	return (0 != m_ImageListPtr->GetSize() && 
		m_ImageListPtr->GetCurrent().IsVideo());
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
	if (widget == pViewerImpl->m_pPlayProgress)
	{
		if (pViewerImpl->m_bWasPlayingBeforeSeek)
		   pViewerImpl->PlayPauseVideo(); 

		gdk_pointer_ungrab (event->time);
	}
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
	if (widget == pViewerImpl->m_pImageView) 
	{
		if (GDK_BUTTON_PRESS == event->type && 3 == event->button)
		{
			viewer_show_context_menu(event, user_data);
			return TRUE;
		}
	}
	else if (widget == pViewerImpl->m_pPlayProgress)
	{
		pViewerImpl->m_bWasPlayingBeforeSeek = pViewerImpl->IsPlaying(); 

		GtkAllocation allocation = {0};
		gtk_widget_get_allocation(widget, &allocation);

		GstFormat format = GST_FORMAT_TIME;
		gint64 clip_duration = 0;

		gst_element_query_duration(GST_ELEMENT(pViewerImpl->m_pPipeline), &format, &clip_duration);

		gboolean seek_started = gst_element_seek_simple(GST_ELEMENT(pViewerImpl->m_pPipeline), format, GstSeekFlags(GST_SEEK_FLAG_FLUSH), ((clip_duration * event->x) / allocation.width));

		gdk_pointer_grab (
			pViewerImpl->m_pPlayProgress->window,  
			TRUE, 
			(GdkEventMask)
				(GDK_BUTTON_RELEASE_MASK  | 
				 GDK_POINTER_MOTION_HINT_MASK | 
				 GDK_BUTTON_MOTION_MASK | 
				 GDK_EXTENSION_EVENTS_ALL),
			pViewerImpl->m_pPlayProgress->window, 
			NULL,
			GDK_CURRENT_TIME);

		if (pViewerImpl->IsPlaying())
			pViewerImpl->PlayPauseVideo();
	}
	else
	{
		if (GDK_BUTTON_PRESS == event->type && 1 == event->button)
		{
			// play video
			if (pViewerImpl->IsVideo())
				pViewerImpl->PlayPauseVideo();
		}
	}
	return FALSE;
} 

static void viewer_show_context_menu(GdkEventButton *event, gpointer userdata)
{
	Viewer::ViewerImpl* pViewerImpl = (Viewer::ViewerImpl*)userdata;
	
	if (NULL != pViewerImpl->m_pUIManager)
	{
		// FIXME - add more actions
		GtkWidget *menu;
		menu = gtk_ui_manager_get_widget (pViewerImpl->m_pUIManager,"/ui/ContextMenuMain");
	
		gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL,
	                  (event != NULL) ? event->button : 0,
	                  gdk_event_get_time((GdkEvent*)event));
	}
}




Viewer::ViewerImpl::~ViewerImpl()
{
	StopVideo(false);

	g_object_unref(m_pPixbufPause);
	g_object_unref(m_pPixbufPauseHighlight);
	g_object_unref(m_pPixbufPlay);
	g_object_unref(m_pPixbufPlayHighlight);

	gst_object_unref(GST_OBJECT(m_pPipeline));

	if (0 != m_iIdleSetIndex)
	{
		g_source_remove(m_iIdleSetIndex);
		m_iIdleSetIndex = 0;
	}	

	m_ImageLoader.RemovePixbufLoaderObserver(m_StatusbarPtr.get());
	m_ImageLoader.RemovePixbufLoaderObserver(m_PixbufLoaderObserverPtr.get());

	if (NULL != m_pUIManager)
		g_object_unref(m_pUIManager);

	g_object_unref(m_pHBox);

	PreferencesPtr prefsPtr = Preferences::GetInstance();
	prefsPtr->RemoveEventHandler( m_PreferencesEventHandlerPtr );
}

Viewer::ViewerImpl::ViewerImpl(Viewer *pViewer) : 
	m_ImageListPtr(new ImageList()),
	m_ThumbnailCache(100),
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

	gtk_widget_set_size_request(m_pImageView, 100, 100);


	m_pPixbufPlay = gdk_pixbuf_new_from_file(QUIVER_DATADIR "/play.png", NULL);
	m_pPixbufPlayHighlight = gdk_pixbuf_copy(m_pPixbufPlay);
	pixbuf_brighten(m_pPixbufPlayHighlight, m_pPixbufPlayHighlight, 50); 

	m_pPixbufPause = gdk_pixbuf_new_from_file(QUIVER_DATADIR "/pause.png", NULL);
	m_pPixbufPauseHighlight = gdk_pixbuf_copy(m_pPixbufPause);
	pixbuf_brighten(m_pPixbufPauseHighlight, m_pPixbufPauseHighlight, 50); 

	int w, h;
	w = gdk_pixbuf_get_width(m_pPixbufPlay);
	h = gdk_pixbuf_get_height(m_pPixbufPlay);
	GdkPixmap* bitmap = gdk_pixmap_new(NULL, w, h, 1);
	gdk_pixbuf_render_threshold_alpha(m_pPixbufPlay, bitmap, 0,0,0,0,w,h, 0x80);

	GtkWidget* alignment = gtk_alignment_new(0., 1., 1., 0.);
	gtk_widget_set_no_show_all(alignment,TRUE);
	//m_pMediaControls = alignment;
	gtk_alignment_set_padding(GTK_ALIGNMENT(alignment), 0, 10, 10,10);

	GtkWidget* align2    = gtk_alignment_new(0.,1.,1., 0.);
	GtkWidget* hbox1     = gtk_hbox_new(FALSE,0);
	m_pMediaControls     = hbox1;
	GtkWidget* hbox2     = gtk_hbox_new(FALSE,0);
	m_pPlayProgress      = gtk_progress_bar_new();
	m_pTimeLabel         = gtk_label_new("");
	GtkWidget* eventbox2 = gtk_event_box_new();

	gtk_box_pack_start (GTK_BOX (hbox2), m_pTimeLabel, FALSE, TRUE, 10);
	gtk_box_pack_start (GTK_BOX (hbox2), m_pPlayProgress, TRUE, TRUE, 0);

	GtkWidget* align3   = gtk_alignment_new(0.,0.,1., 1.);
	gtk_alignment_set_padding(GTK_ALIGNMENT(align3), 5, 5, 5,10);
	gtk_container_add(GTK_CONTAINER(align3), hbox2);
	gtk_container_add(GTK_CONTAINER(eventbox2), align3);

	gtk_alignment_set_padding(GTK_ALIGNMENT(align2), 0, 5, 0, 0);
	gtk_container_add(GTK_CONTAINER(align2), eventbox2);

	m_pTimeline = eventbox2;

	GtkWidget* eventbox = gtk_event_box_new();

	g_signal_connect(
		G_OBJECT(eventbox), 
		"enter-notify-event",
		G_CALLBACK(viewer_play_button_mouse_in),
		this);
	g_signal_connect(
		G_OBJECT(eventbox), 
		"leave-notify-event",
		G_CALLBACK(viewer_play_button_mouse_out),
		this);
	g_signal_connect(G_OBJECT(eventbox), "button-press-event", G_CALLBACK(viewer_button_press_cb), this);

	gtk_widget_add_events (m_pPlayProgress, GDK_BUTTON_PRESS_MASK|GDK_BUTTON_RELEASE_MASK);
 	gtk_widget_add_events (m_pPlayProgress, GDK_POINTER_MOTION_MASK|GDK_POINTER_MOTION_HINT_MASK);
	g_signal_connect(G_OBJECT(m_pPlayProgress), "button-press-event", G_CALLBACK(viewer_button_press_cb), this);
	g_signal_connect (G_OBJECT (m_pPlayProgress), "button_release_event",  
				G_CALLBACK (viewer_button_release_cb), this);
	g_signal_connect (G_OBJECT (m_pPlayProgress), "motion-notify-event",G_CALLBACK (viewer_motion_notify), this);

	gtk_widget_shape_combine_mask(eventbox, bitmap, 0,0);

	m_pPlayImage = gtk_image_new_from_pixbuf(m_pPixbufPlay);
	gtk_container_add(GTK_CONTAINER(eventbox), m_pPlayImage);

	//gtk_widget_set_size_request(m_pPlayImage, w, h);

	gtk_widget_set_size_request(eventbox, w, h);

	//gtk_container_add(GTK_CONTAINER(alignment), eventbox);

	gtk_box_pack_start (GTK_BOX (hbox1), eventbox, FALSE, TRUE, 10);
	gtk_box_pack_start (GTK_BOX (hbox1), align2, TRUE, TRUE, 0);

	gtk_container_add(GTK_CONTAINER(alignment), hbox1);
	gtk_widget_show_all(eventbox);
	gtk_widget_show_all(hbox1);
	gtk_widget_hide(hbox1);
	gtk_widget_show(alignment);

	m_iCurrentOrientation = 1;
	
	m_pUIManager = NULL;
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

	m_pScrollbarV = gtk_vscrollbar_new (m_pAdjustmentV);
	m_pScrollbarH = gtk_hscrollbar_new (m_pAdjustmentH);
	
	m_pNavigationBox = gtk_event_box_new ();

	GdkPixbuf *pixbuf = gdk_pixbuf_new_from_xpm_data ((const char**) nav_button_xpm);
	GtkWidget *image = gtk_image_new_from_pixbuf (pixbuf);
	g_object_unref (G_OBJECT (pixbuf));
	gtk_container_add (GTK_CONTAINER (m_pNavigationBox),image);
	gtk_widget_show(image);

	gtk_widget_set_no_show_all(m_pScrollbarV,TRUE);
	gtk_widget_set_no_show_all(m_pScrollbarH,TRUE);
	gtk_widget_set_no_show_all(m_pNavigationBox,TRUE);


	g_signal_connect (G_OBJECT (m_pNavigationBox), 
			  "button_press_event",
			  G_CALLBACK (viewer_navigation_button_press_event), 
			  this);

	

	m_pTable = gtk_table_new (2, 2, FALSE);
	
	gtk_table_attach (GTK_TABLE (m_pTable), alignment, 0, 1, 0, 1,
			  (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
			  (GtkAttachOptions) (GTK_EXPAND | GTK_FILL), 0, 0);

	gtk_table_attach (GTK_TABLE (m_pTable), m_pImageView, 0, 1, 0, 1,
			  (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
			  (GtkAttachOptions) (GTK_EXPAND | GTK_FILL), 0, 0);

	gtk_table_attach (GTK_TABLE (m_pTable), m_pScrollbarV, 1, 2, 0, 1,
			  (GtkAttachOptions) (GTK_FILL),
			  (GtkAttachOptions) (GTK_EXPAND | GTK_FILL), 0, 0);
			  
	gtk_table_attach (GTK_TABLE (m_pTable), m_pScrollbarH, 0, 1, 1, 2,
			  (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
			  (GtkAttachOptions) (GTK_FILL), 0, 0);
			  
	gtk_table_attach (GTK_TABLE (m_pTable), m_pNavigationBox, 1, 2, 1, 2,
			  (GtkAttachOptions) (0),
			  (GtkAttachOptions) (0), 0, 0);

//	GTK_WIDGET_SET_FLAGS(m_pTable,GTK_CAN_FOCUS);
	m_pHBox = gtk_hbox_new(FALSE,0);
	g_object_ref(m_pHBox);
	m_pVBox = gtk_vbox_new(FALSE,0);

	gtk_box_pack_start (GTK_BOX (m_pVBox), m_pTable, TRUE, TRUE, 0);
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
			GdkColor color;
			gdk_color_parse(strBGColorImg.c_str(),&color);
			gtk_widget_modify_bg (m_pImageView, GTK_STATE_NORMAL, &color );
		}
		
		if (!strBGColorThumb.empty())
		{
			GdkColor color;
			gdk_color_parse(strBGColorThumb.c_str(),&color);
			gtk_widget_modify_bg (m_pIconView, GTK_STATE_NORMAL, &color );
		}
	}

	m_iSlideShowDuration = prefsPtr->GetInteger(QUIVER_PREFS_SLIDESHOW,QUIVER_PREFS_SLIDESHOW_DURATION, 3000);
	m_bSlideShowLoop = prefsPtr->GetBoolean(QUIVER_PREFS_SLIDESHOW,QUIVER_PREFS_SLIDESHOW_LOOP,true);

	m_bMaximizeViewableArea = prefsPtr->GetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_ROTATE_FOR_BEST_FIT, false);
	
	m_bIsPlaying = false;

	quiver_image_view_set_magnification_mode(QUIVER_IMAGE_VIEW(m_pImageView),QUIVER_IMAGE_VIEW_MAGNIFICATION_MODE_SMOOTH);
	
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
	m_pPipeline = gst_element_factory_make("playbin2", "player");
	m_pXVImageSink = gst_element_factory_make("xvimagesink", NULL);

	g_object_set(G_OBJECT(m_pXVImageSink), "force-aspect-ratio", true, NULL);

	GstPlayFlags flags = (GstPlayFlags)0;
	g_object_get(G_OBJECT(m_pPipeline), "flags", &flags, NULL);
	flags = (GstPlayFlags) (GST_PLAY_FLAG_DEINTERLACE|flags);

	g_object_set(G_OBJECT(m_pPipeline), 
		"video-sink", m_pXVImageSink,
		"flags", flags, 
		NULL);

	GstBus* bus = gst_pipeline_get_bus(GST_PIPELINE(m_pPipeline));
	gst_bus_set_sync_handler (bus, (GstBusSyncHandler) gstreamer_bus_sync_handler, this);
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
	gdk_threads_enter();
	pViewerImpl->SetImageIndex(pViewerImpl->m_ImageListPtr->GetCurrentIndex(),true);
	gdk_threads_leave();
	pViewerImpl->m_iIdleSetIndex = 0;
	return FALSE;
}

void Viewer::Show()
{
	GError *tmp_error;
	tmp_error = NULL;

	gint cursor_cell = quiver_icon_view_get_cursor_cell(QUIVER_ICON_VIEW(m_ViewerImplPtr->m_pIconView));
	if (0 == m_ViewerImplPtr->m_ImageListPtr->GetSize() || m_ViewerImplPtr->m_QuiverFileCurrent != m_ViewerImplPtr->m_ImageListPtr->GetCurrent())
	{
		quiver_image_view_set_pixbuf(QUIVER_IMAGE_VIEW(m_ViewerImplPtr->m_pImageView),NULL);
		// set image index in an idle function
		m_ViewerImplPtr->m_iIdleSetIndex
			= g_idle_add( idle_set_image_index, m_ViewerImplPtr.get());
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

	if (m_ViewerImplPtr->m_pUIManager && 0 == m_ViewerImplPtr->m_iMergedViewerUI)
	{
		m_ViewerImplPtr->m_iMergedViewerUI = gtk_ui_manager_add_ui_from_string(m_ViewerImplPtr->m_pUIManager,
				ui_viewer,
				strlen(ui_viewer),
				&tmp_error);

		gtk_ui_manager_ensure_update(m_ViewerImplPtr->m_pUIManager);

		if (NULL != tmp_error)
		{
			g_warning("Viewer::Show() Error: %s\n",tmp_error->message);
		}
	}
	
	m_ViewerImplPtr->m_ImageListPtr->UnblockHandler(m_ViewerImplPtr->m_ImageListEventHandlerPtr);

}

void Viewer::Hide()
{
	m_ViewerImplPtr->StopVideo(true);
	SlideShowStop();
	
	gtk_widget_hide(m_ViewerImplPtr->m_pHBox);
	if (m_ViewerImplPtr->m_pUIManager && 0 != m_ViewerImplPtr->m_iMergedViewerUI)
	{	
		gtk_ui_manager_remove_ui(m_ViewerImplPtr->m_pUIManager,
			m_ViewerImplPtr->m_iMergedViewerUI);
		m_ViewerImplPtr->m_iMergedViewerUI = 0;
		gtk_ui_manager_ensure_update(m_ViewerImplPtr->m_pUIManager);
	}

	m_ViewerImplPtr->m_ImageListPtr->BlockHandler(m_ViewerImplPtr->m_ImageListEventHandlerPtr);
}

void Viewer::SetUIManager(GtkUIManager *ui_manager)
{
	GError *tmp_error;
	tmp_error = NULL;
	
	if (m_ViewerImplPtr->m_pUIManager)
	{
		g_object_unref(m_ViewerImplPtr->m_pUIManager);
	}

	m_ViewerImplPtr->m_pUIManager = ui_manager;
	
	g_object_ref(m_ViewerImplPtr->m_pUIManager);

	guint n_entries = G_N_ELEMENTS (action_entries);

	
	GtkActionGroup* actions = gtk_action_group_new ("BrowserActions");
	
	gtk_action_group_add_actions(actions, action_entries, n_entries, m_ViewerImplPtr.get());
                                 
	gtk_action_group_add_toggle_actions(actions,
										action_entries_toggle, 
										G_N_ELEMENTS (action_entries_toggle),
										m_ViewerImplPtr.get());
										
	gtk_action_group_add_radio_actions(actions,
										zoom_radio_action_entries, 
										G_N_ELEMENTS (zoom_radio_action_entries),
										QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW,
										G_CALLBACK(viewer_radio_action_handler_cb),
										m_ViewerImplPtr.get());										

	gtk_ui_manager_insert_action_group (m_ViewerImplPtr->m_pUIManager,actions,0);

	g_object_unref(actions);
	
	
	GtkAction* action = QuiverUtils::GetAction(m_ViewerImplPtr->m_pUIManager,ACTION_VIEWER_VIEW_FILM_STRIP);
	if (NULL != action)
	{
		PreferencesPtr prefsPtr = Preferences::GetInstance();
		bool bShowFilmStrip = prefsPtr->GetBoolean(QUIVER_PREFS_VIEWER,QUIVER_PREFS_VIEWER_FILMSTRIP_SHOW);
				
		gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action),bShowFilmStrip ? TRUE : FALSE);
	}
	
	action = QuiverUtils::GetAction(m_ViewerImplPtr->m_pUIManager,ACTION_VIEWER_ROTATE_FOR_BEST_FIT);
	if (NULL != action)
	{
		gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action),m_ViewerImplPtr->m_bMaximizeViewableArea ? TRUE : FALSE);
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

int Viewer::GetCurrentOrientation()
{
	return m_ViewerImplPtr->GetCurrentOrientation();	
}


static gboolean timeout_advance_slideshow (gpointer data)
{
	Viewer::ViewerImpl* pViewerImpl = (Viewer::ViewerImpl*)data;
	
	int iNextIndex = pViewerImpl->m_ImageListPtr->GetCurrentIndex()+1;

	gdk_threads_enter();
	if (pViewerImpl->m_ImageLoader.IsWorking() || quiver_image_view_is_in_transition(QUIVER_IMAGE_VIEW(pViewerImpl->m_pImageView)) )
	{
		// wait until the imageloader has finished working
		// before advancing the slideshow
		++pViewerImpl->m_iSlideShowWaitCount;
		pViewerImpl->m_iTimeoutSlideshowID 
			= g_timeout_add(SLIDESHOW_WAIT_DURATION,timeout_advance_slideshow, pViewerImpl);

		gdk_threads_leave();
		return FALSE;
	}
	gdk_threads_leave();

	switch (pViewerImpl->m_SlideShowState)
	{
		case Viewer::ViewerImpl::SLIDESHOW_STATE_ADVANCE:
			{
				gdk_threads_enter();
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
				gdk_threads_leave();
			}
			break;
		case Viewer::ViewerImpl::SLIDESHOW_STATE_CACHE:
			{
				gdk_threads_enter();

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

				gdk_threads_leave();
			}
			break;
		case Viewer::ViewerImpl::SLIDESHOW_STATE_PLAY_VIDEO:
			{
				gdk_threads_enter();

				pViewerImpl->PlayPauseVideo();
				pViewerImpl->m_SlideShowState = Viewer::ViewerImpl::SLIDESHOW_STATE_PLAYING_VIDEO;
				pViewerImpl->m_iTimeoutSlideshowID 
						= g_timeout_add(SLIDESHOW_WAIT_DURATION,timeout_advance_slideshow, pViewerImpl);

				gdk_threads_leave();
			}
		break;
		case Viewer::ViewerImpl::SLIDESHOW_STATE_PLAYING_VIDEO:
			{
				gdk_threads_enter();
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
				gdk_threads_leave();
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


GtkTableChild * GetGtkTableChild(GtkTable * table,GtkWidget	*widget_to_get)
{
	GtkTableChild *table_child = NULL;
	GList *list;
	for (list = table->children; list; list = list->next)
	{
		table_child = (GtkTableChild*)list->data;
		if (table_child->widget == widget_to_get)
			break;
	}
	return table_child;
}


static guint n_cells_callback(QuiverIconView *iconview, gpointer user_data)
{
	Viewer::ViewerImpl* pViewerImpl = (Viewer::ViewerImpl*)user_data;
	return pViewerImpl->m_ImageListPtr->GetSize();
}

static GdkPixbuf* icon_pixbuf_callback(QuiverIconView *iconview, guint cell,gpointer user_data)
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
	gdk_threads_enter();
	pViewerImpl->m_ThumbnailLoader.UpdateList();
	pViewerImpl->m_iTimeoutUpdateListID = 0;
	gdk_threads_leave();
	return FALSE;
}


static GdkPixbuf* thumbnail_pixbuf_callback(QuiverIconView *iconview, guint cell, gint* actual_width, gint* actual_height, gpointer user_data)

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
	gdk_threads_enter();
	pViewerImpl->UpdateScrollbars();
	gdk_threads_leave();

	return FALSE;
}

static void image_view_adjustment_changed (GtkAdjustment *adjustment, gpointer user_data)
{
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
{
	parent->SetImageIndex(parent->m_ImageListPtr->GetCurrentIndex(),true);
	parent->m_ThumbnailLoader.UpdateList(true);
}
void Viewer::ViewerImpl::ImageListEventHandler::HandleCurrentIndexChanged(ImageListEventPtr event) 
{
	parent->SetImageIndex(event->GetIndex(),true);
}
void Viewer::ViewerImpl::ImageListEventHandler::HandleItemAdded(ImageListEventPtr event)
{
	parent->SetImageIndex(parent->m_ImageListPtr->GetCurrentIndex(),true);
	parent->m_ThumbnailLoader.UpdateList(true);
}
void Viewer::ViewerImpl::ImageListEventHandler::HandleItemRemoved(ImageListEventPtr event)
{
	parent->SetImageIndex(parent->m_ImageListPtr->GetCurrentIndex(),true);
	parent->m_ThumbnailLoader.UpdateList(true);
}
void Viewer::ViewerImpl::ImageListEventHandler::HandleItemChanged(ImageListEventPtr event)
{
	if (parent->m_ImageListPtr->GetCurrentIndex() == event->GetIndex())
	{
		parent->m_ThumbnailCache.RemovePixbuf(parent->m_ImageListPtr->GetCurrent().GetURI());
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
				gtk_widget_modify_bg (parent->m_pIconView, GTK_STATE_NORMAL, NULL );
				gtk_widget_modify_bg (parent->m_pImageView, GTK_STATE_NORMAL, NULL );
			}
			else
			{
				GdkColor color;
				
				string strBGColorImg   = prefsPtr->GetString(QUIVER_PREFS_APP,QUIVER_PREFS_APP_BG_IMAGEVIEW);
				string strBGColorThumb = prefsPtr->GetString(QUIVER_PREFS_APP,QUIVER_PREFS_APP_BG_ICONVIEW);
						
				gdk_color_parse(strBGColorThumb.c_str(),&color);
				gtk_widget_modify_bg (parent->m_pIconView, GTK_STATE_NORMAL, &color );
				
				gdk_color_parse(strBGColorImg.c_str(),&color);
				gtk_widget_modify_bg (parent->m_pImageView, GTK_STATE_NORMAL, &color);
				
			}
		}
		else if (QUIVER_PREFS_APP_BG_IMAGEVIEW == event->GetKey() )
		{
			if ( !prefsPtr->GetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_USE_THEME_COLOR,true) )
			{
				GdkColor color;
				
				string strBGColorImg   = prefsPtr->GetString(QUIVER_PREFS_APP,QUIVER_PREFS_APP_BG_IMAGEVIEW);
				
				gdk_color_parse(strBGColorImg.c_str(),&color);
				gtk_widget_modify_bg (parent->m_pImageView, GTK_STATE_NORMAL, &color );
			}			
		}
		else if (QUIVER_PREFS_APP_BG_ICONVIEW == event->GetKey() )
		{
			if ( !prefsPtr->GetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_USE_THEME_COLOR,true) )
			{
				GdkColor color;
				
				string strBGColorThumb = prefsPtr->GetString(QUIVER_PREFS_APP,QUIVER_PREFS_APP_BG_ICONVIEW);						

				gdk_color_parse(strBGColorThumb.c_str(),&color);
				gtk_widget_modify_bg (parent->m_pIconView, GTK_STATE_NORMAL, &color );
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
			
			int iFilmstripPos = event->GetOldInteger();

			GtkContainer* container = NULL;
			
			switch (iFilmstripPos)
			{
				case FSTRIP_POS_TOP:
				case FSTRIP_POS_BOTTOM:
					container = GTK_CONTAINER(parent->m_pVBox);
					break;
				case FSTRIP_POS_LEFT:
				case FSTRIP_POS_RIGHT:
					container = GTK_CONTAINER(parent->m_pHBox);
					break;		
			}

			if (NULL != container)
			{
				g_object_ref(parent->m_pIconView);
				gtk_container_remove(container, parent->m_pIconView);
				parent->AddFilmstrip();
				g_object_unref(parent->m_pIconView);
			}
			
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

void Viewer::ViewerImpl::ViewerThumbLoader::LoadThumbnail(const ThumbLoaderItem &item, guint uiWidth, guint uiHeight)
{
	guint width, height;
	quiver_icon_view_get_icon_size(QUIVER_ICON_VIEW(m_pViewerImpl->m_pIconView),&width,&height);

	if (m_pViewerImpl->m_ImageLoader.IsWorking() || quiver_image_view_is_in_transition(QUIVER_IMAGE_VIEW(m_pViewerImpl->m_pImageView)) )
	{
		// put some delay in this thread if it is churning away
		// loading an image or doing a transition
		// 100000 = 100milliseconds
		usleep(100000);
	}

	if (GTK_WIDGET_MAPPED(m_pViewerImpl->m_pIconView) &&
		item.m_ulIndex < m_pViewerImpl->m_ImageListPtr->GetSize())
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

			gdk_threads_enter();
			
			quiver_icon_view_invalidate_cell(QUIVER_ICON_VIEW(m_pViewerImpl->m_pIconView),item.m_ulIndex);
			
			gdk_threads_leave();
		}
	}
}

void Viewer::ViewerImpl::ViewerThumbLoader::GetVisibleRange(gulong* pulStart, gulong* pulEnd)
{
	quiver_icon_view_get_visible_range(QUIVER_ICON_VIEW(m_pViewerImpl->m_pIconView),pulStart, pulEnd);
}


void Viewer::ViewerImpl::ViewerThumbLoader::GetIconSize(guint* puiWidth, guint* puiHeight)
{
	quiver_icon_view_get_icon_size(QUIVER_ICON_VIEW(m_pViewerImpl->m_pIconView), puiWidth, puiHeight);
}

gulong Viewer::ViewerImpl::ViewerThumbLoader::GetNumItems()
{
	return m_pViewerImpl->m_ImageListPtr->GetSize();
}

void Viewer::ViewerImpl::ViewerThumbLoader::SetIsRunning(bool bIsRunning)
{
	if (m_pViewerImpl->m_StatusbarPtr.get())
	{
		gdk_threads_enter();
		if (bIsRunning)
		{
			m_pViewerImpl->m_StatusbarPtr->StartProgressPulse();
		}
		else
		{
			m_pViewerImpl->m_StatusbarPtr->StopProgressPulse();
		}
		gdk_threads_leave();
	}
	
}

void Viewer::ViewerImpl::ViewerThumbLoader::SetCacheSize(guint uiCacheSize)
{
	m_pViewerImpl->m_ThumbnailCache.SetSize(uiCacheSize);
}
