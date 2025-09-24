#include <config.h>

#include <pthread.h> // For std::swap on some older systems, or pthread_yield if used (currently sched_yield)

#include <gtk/gtk.h>
#include <string.h> // For strlen etc.
#include <list>
#include <map>
#include <set>
#include <algorithm> // For std::min, std::max

// #include <gdk/gdkkeysyms.h> // Included by gtk/gtk.h.
// For GDK_KEY_Escape etc., gtk/gtk.h should be sufficient.
// #include <gdk/gdkkeysyms-compat.h> // Removed as it was causing a build error.

#include <gio/gio.h>

#include <libquiver/quiver-icon-view.h>
#include <libquiver/quiver-image-view.h>
#include <libquiver/quiver-pixbuf-utils.h>

#include "Browser.h"
#include "FolderTree.h"
#include "ImageList.h"
#include "ImageCache.h"
#include "ImageLoader.h"
#include "IPixbufLoaderObserver.h"
// #include "QuiverUtils.h" // Contains GtkUIManager helpers, comment out for now
#include "QuiverPrefs.h"
#include "QuiverFileOps.h"
#include "BrowserHistory.h"

#include "Statusbar.h"

#include "IImageListEventHandler.h"
#include "IPreferencesEventHandler.h"
#include "IFolderTreeEventHandler.h"
#include "IconViewThumbLoader.h"


#include "QuiverStockIcons.h" // Assuming this is still relevant or will be adapted

using namespace std;

static std::vector<std::string> g_clipboard;
static bool g_is_cut = false;



// ============================================================================
// Browser::BrowserImpl: private implementation (hidden from header file)
// ============================================================================

typedef boost::shared_ptr<IPixbufLoaderObserver> IPixbufLoaderObserverPtr;

class ImageViewPixbufLoaderObserver : public IPixbufLoaderObserver
{
public:
	ImageViewPixbufLoaderObserver(QuiverImageView *imageview){m_pImageView = imageview;};
	virtual ~ImageViewPixbufLoaderObserver(){};

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
		gboolean bReset = FALSE;
		if (bResetViewMode) bReset = TRUE;
		quiver_image_view_set_pixbuf_at_size_ex(m_pImageView,pixbuf,width,height,bReset);
		// gdk_threads_leave();
	};
	virtual void SignalBytesRead(long bytes_read,long total){};
private:
	QuiverImageView *m_pImageView;
};


class Browser::BrowserImpl
{
public:
/* constructors and destructor */
	BrowserImpl(Browser *parent);
	~BrowserImpl();
	
/* member functions */

	// void SetUIManager(GtkUIManager *ui_manager); // GtkUIManager is deprecated
	void UpdateUI(); // enable/disable toolbar/menu items
	void Show();
	void Hide();

	GtkWidget* GetWidget(){return m_pBrowserWidget;};
	
	ImageListPtr GetImageList();
	
	void SetImageList(ImageListPtr list);
	
	void SetImageIndex(int index, bool bDirectionForward, bool bFromIconView = false);

	void QueueIconViewUpdate(int timeout = 50 /* ms */);

/* member variables */
	FolderTreePtr m_FolderTreePtr;
	bool m_bFolderTreeEvent;
	bool m_bBrowserHistoryEvent;

	BrowserHistory m_BrowserHistory;
		
	GtkWidget *m_pIconView;

	GtkWidget *m_pBrowserWidget;
	
	//GtkWidget *hpaned; // This was a member, but a local var hpaned_widget is used for construction.
	GtkWidget *vpaned;
	GtkWidget *m_pNotebook;
	GtkWidget *m_pSWFolderTree;
	
	GtkWidget *m_pImageView;

	GtkWidget *m_pLocationEntry;
	GtkWidget *hscale; // Was GtkHScale, now GtkScale

	// GtkToolItem *m_pToolItemThumbSizer; // GtkToolItem is deprecated
	
	// GtkUIManager *m_pUIManager; // GtkUIManager is deprecated
	guint m_iMergedBrowserUI; // Related to GtkUIManager
	
	StatusbarPtr m_StatusbarPtr;
	
	ImageListPtr m_ImageListPtr;
	QuiverFile m_QuiverFileCurrent;
	ImageCache m_ThumbnailCache;
	ImageCache m_IconCache;
	ImageCache m_IconOverlayCache;
	
	guint m_iTimeoutUpdateListID;
	guint m_iTimeoutHideLocationID;

	Browser *m_BrowserParent;

	GtkCssProvider* m_pCssProvider;

	ImageLoader m_ImageLoader;
	IPixbufLoaderObserverPtr m_ImageViewPixbufLoaderObserverPtr;

	map<string, string> m_mapFolderToFile;
	
/* nested classes */
	//class ViewerEventHandler;
	class ImageListEventHandler : public IImageListEventHandler
	{
	public:
		ImageListEventHandler(Browser::BrowserImpl *parent){this->parent = parent;};
		virtual void HandleContentsChanged(ImageListEventPtr event);
		virtual void HandleCurrentIndexChanged(ImageListEventPtr event) ;
		virtual void HandleItemAdded(ImageListEventPtr event);
		virtual void HandleItemRemoved(ImageListEventPtr event);
		virtual void HandleItemChanged(ImageListEventPtr event);
	private:
		Browser::BrowserImpl *parent;
	};
	
	class PreferencesEventHandler : public IPreferencesEventHandler
	{
	public:
		PreferencesEventHandler(BrowserImpl* parent) {this->parent = parent;};
		virtual void HandlePreferenceChanged(PreferencesEventPtr event);
	private:
		BrowserImpl* parent;
	};
	

	class FolderTreeEventHandler : public IFolderTreeEventHandler
	{
	public:
		FolderTreeEventHandler(BrowserImpl* pBrowserImpl){this->parent = pBrowserImpl;};
		virtual void HandleSelectionChanged(FolderTreeEventPtr event);
		virtual ~FolderTreeEventHandler(){};
	private:
		BrowserImpl* parent;
	};

	class BrowserThumbLoader : public IconViewThumbLoader
	{
	public:
		BrowserThumbLoader(BrowserImpl* pBrowserImpl, guint iNumThreads)  : IconViewThumbLoader(iNumThreads)
		{
			m_pBrowserImpl = pBrowserImpl;
		}
		
		~BrowserThumbLoader(){}
		
	protected:
		
		virtual void LoadThumbnail(const ThumbLoaderItem &item, guint uiWidth, guint uiHeight);
		virtual void GetVisibleRange(gulong* pulStart, gulong* pulEnd);
		virtual void GetIconSize(guint* puiWidth, guint* puiHeight);
		virtual gulong GetNumItems();
		virtual QuiverFile GetQuiverFile(gulong index);
		virtual void SetIsRunning(bool bIsRunning);
		virtual void SetCacheSize(guint uiCacheSize);
	
		
	private:
		BrowserImpl* m_pBrowserImpl; 
		
	};


	IImageListEventHandlerPtr    m_ImageListEventHandlerPtr;
	IPreferencesEventHandlerPtr  m_PreferencesEventHandlerPtr;
	IFolderTreeEventHandlerPtr m_FolderTreeEventHandlerPtr;
	
	BrowserThumbLoader m_ThumbnailLoader;
	
    GSimpleActionGroup *m_pActionGroup;
};
// ============================================================================


// static void browser_action_handler_cb(GtkAction *action, gpointer data); // GtkAction is deprecated

static void browser_action_handler_cb(GAction *action, GVariant *parameter, gpointer user_data);
static void browser_toggle_action_handler_cb(GAction *action, GVariant *state, gpointer user_data);

#define ACTION_BROWSER_OPEN_LOCATION                      "BrowserOpenLocation"
#define ACTION_BROWSER_HISTORY_BACK                       "BrowserHistoryBack"
#define ACTION_BROWSER_HISTORY_FORWARD                    "BrowserHistoryForward"
#define ACTION_BROWSER_CUT                                "BrowserCut"
#define ACTION_BROWSER_COPY                               "BrowserCopy"
#define ACTION_BROWSER_PASTE                              "BrowserPaste"
#define ACTION_BROWSER_SELECT_ALL                         "BrowserSelectAll"
#define ACTION_BROWSER_TRASH                              "BrowserTrash"
#define ACTION_BROWSER_RELOAD                             "BrowserReload"
#define ACTION_BROWSER_VIEW_PREVIEW                       "BrowserViewPreview"
#define ACTION_BROWSER_VIEW_SIDEBAR                       "BrowserViewSidebar"
#define ACTION_BROWSER_ZOOM_IN                            "BrowserZoomIn"
#define ACTION_BROWSER_ZOOM_OUT                           "BrowserZoomOut"


// GtkUIManager related UI definition strings - will need replacement with GMenuModel / GtkBuilder
/*
static const char *ui_browser =
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
	{ ACTION_BROWSER_VIEW_SIDEBAR, QUIVER_STOCK_DIRECTORY, "Sidebar", "<Control><Shift>F", "Show/Hide Sidebar (Folder Tree, Preview Window, etc)", G_CALLBACK(browser_action_handler_cb),TRUE},
	{ ACTION_BROWSER_VIEW_PREVIEW, QUIVER_STOCK_PROPERTIES, "Preview", "<Control><Shift>p", "Show/Hide Image Preview", G_CALLBACK(browser_action_handler_cb),TRUE},
};

static GtkActionEntry action_entries[] = {
// ... (action entries commented out as they are GtkAction based) ...
};
*/


Browser::Browser() : m_BrowserImplPtr( new BrowserImpl(this) )
{

}


Browser::~Browser()
{

}

list<unsigned int> Browser::GetSelection()
{
	list<unsigned int> selection_list;
	GList *selection = quiver_icon_view_get_selection(QUIVER_ICON_VIEW(m_BrowserImplPtr->m_pIconView));
	GList *item = selection;
	while (NULL != item)
	{
		selection_list.push_back(GPOINTER_TO_UINT(item->data)); // Corrected conversion
		item = g_list_next(item);
	}
	g_list_free(selection);
	return selection_list;
}

std::string Browser::GetCurrentFolderChild()
{
	string item;
	if (0 != m_BrowserImplPtr->m_ImageListPtr->GetSize())
	{
		QuiverFile f = m_BrowserImplPtr->m_ImageListPtr->GetCurrent();
		map<string,string>::iterator itr = m_BrowserImplPtr->m_mapFolderToFile.find(f.GetURI());
		if (m_BrowserImplPtr->m_mapFolderToFile.end() != itr)
		{
			item = itr->second;
		}
	}
	return item;
}

/*
void 
Browser::SetUIManager(GtkUIManager *ui_manager) // GtkUIManager is deprecated
{
	// m_BrowserImplPtr->SetUIManager(ui_manager);
}
*/

void
Browser::SetStatusbar(StatusbarPtr statusbarPtr)
{
	if (m_BrowserImplPtr->m_StatusbarPtr) { // Check if it was already set
		m_BrowserImplPtr->m_ImageLoader.RemovePixbufLoaderObserver(m_BrowserImplPtr->m_StatusbarPtr.get());
	}
	
	m_BrowserImplPtr->m_StatusbarPtr = statusbarPtr;
	
	if (m_BrowserImplPtr->m_StatusbarPtr) { // Check if successfully set
		m_BrowserImplPtr->m_ImageLoader.AddPixbufLoaderObserver(m_BrowserImplPtr->m_StatusbarPtr.get());
	}
}

void 
Browser::GrabFocus()
{
	gtk_widget_grab_focus (m_BrowserImplPtr->m_pIconView);
}

void 
Browser::Show()
{
	m_BrowserImplPtr->Show();
}

void 
Browser::Hide()
{
	m_BrowserImplPtr->Hide();
}


void 
Browser::SetImageList(ImageListPtr list)
{
	m_BrowserImplPtr->SetImageList(list);
}


ImageListPtr 
Browser::GetImageList()
{
	return m_BrowserImplPtr->GetImageList();
}

GtkWidget* 
Browser::GetWidget()
{
	return m_BrowserImplPtr->GetWidget();
};

BrowserHistory& Browser::GetBrowserHistory()
{
    return m_BrowserImplPtr->m_BrowserHistory;
}


//=============================================================================
//=============================================================================
// private browser implementation:
//=============================================================================


//=============================================================================
// BrowswerImpl Callback Prototypes
//=============================================================================

static GdkPixbuf* icon_pixbuf_callback(QuiverIconView *iconview, guint cell,gpointer user_data);
static GdkPixbuf* thumbnail_pixbuf_callback(QuiverIconView *iconview, guint cell, gint* actual_width, gint* actual_height, gpointer user_data);
static GdkPixbuf* overlay_pixbuf_callback(QuiverIconView* iconview, guint cell, QuiverIconOverlayType type, gpointer user_data);
static guint n_cells_callback(QuiverIconView *iconview, gpointer user_data);
static void icon_size_value_changed (GtkRange *range,gpointer  user_data);

static void iconview_cell_activated_cb(QuiverIconView *iconview, guint cell, gpointer user_data);
static void iconview_cursor_changed_cb(QuiverIconView *iconview, guint cell, gpointer user_data);
static void iconview_selection_changed_cb(QuiverIconView *iconview, gpointer user_data);
// static gboolean iconview_motion_notify(GtkWidget *widget, GdkEventMotion *event, gpointer user_data); // GdkEventMotion is GTK3
static void iconview_motion_notify_cb(GtkEventControllerMotion* controller, gdouble x, gdouble y, gpointer user_data);


// static gboolean browser_popup_menu_cb (GtkWidget *treeview, gpointer userdata); // For GtkMenu, deprecated
// static gboolean browser_button_press_cb(GtkWidget   *widget, GdkEventButton *event, gpointer user_data); // GdkEventButton is GTK3
// static void browser_button_press_cb(GtkGestureClick* gesture, int n_press, double x, double y, gpointer user_data); // For GtkGestureClick
// static void browser_show_context_menu(GdkEventButton *event, gpointer userdata); // GdkEventButton is GTK3, GtkMenu based
// static void browser_show_context_menu(GtkGestureClick* gesture, int n_press, double x, double y, gpointer userdata); // For GtkPopoverMenu

static void entry_activate(GtkEntry *entry, gpointer user_data);
// static gboolean entry_key_press (GtkWidget   *widget, GdkEventKey *event, gpointer user_data); // GdkEventKey is GTK3
static gboolean entry_key_press_cb (GtkEventControllerKey* controller, guint keyval, guint keycode, GdkModifierType state, gpointer user_data);


static void browser_imageview_magnification_changed(QuiverImageView *imageview,gpointer data);
static void browser_imageview_reload(QuiverImageView *imageview,gpointer data);


// static gboolean entry_focus_in ( GtkWidget *widget, GdkEventFocus *event, gpointer user_data) // GdkEventFocus is GTK3
static void entry_focus_in_cb (GtkEventControllerFocus *controller, GParamSpec *pspec, gpointer user_data)
{
	Browser::BrowserImpl *pBrowserImpl = (Browser::BrowserImpl*)user_data;
	// QuiverUtils::DisconnectUnmodifiedAccelerators(pBrowserImpl->m_pUIManager); // GtkUIManager is deprecated
	gtk_widget_set_visible(pBrowserImpl->m_pLocationEntry, TRUE);
	if (0 != pBrowserImpl->m_iTimeoutHideLocationID)
	{
		g_source_remove(pBrowserImpl->m_iTimeoutHideLocationID);
		pBrowserImpl->m_iTimeoutHideLocationID = 0;
	}
}

static gboolean timeout_hide_location (gpointer data)
{
	GtkWidget *widget = (GtkWidget*)data;
	gtk_widget_set_visible(widget, FALSE);
	return G_SOURCE_REMOVE; // Correct return for g_timeout_add
}

// static gboolean entry_focus_out ( GtkWidget *widget, GdkEventFocus *event, gpointer user_data) // GdkEventFocus is GTK3
static void entry_focus_out_cb (GtkEventControllerFocus *controller, GParamSpec *pspec, gpointer user_data)
{
	Browser::BrowserImpl *pBrowserImpl = (Browser::BrowserImpl*)user_data;
    GtkWidget* entry = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(controller));
	// QuiverUtils::ConnectUnmodifiedAccelerators(pBrowserImpl->m_pUIManager); // GtkUIManager is deprecated

	if (0 == pBrowserImpl->m_iTimeoutHideLocationID)
	{
		pBrowserImpl->m_iTimeoutHideLocationID = g_timeout_add(10,timeout_hide_location,entry);
	}
}

static void pane_position_notify_cb (GObject *gobject, GParamSpec *pspec, gpointer user_data)
{
    GtkPaned* paned = GTK_PANED(gobject);
	PreferencesPtr prefsPtr = Preferences::GetInstance();
    if (gtk_orientable_get_orientation(GTK_ORIENTABLE(paned)) == GTK_ORIENTATION_HORIZONTAL)
    {
        prefsPtr->SetInteger(QUIVER_PREFS_BROWSER,QUIVER_PREFS_BROWSER_FOLDER_HPANE,gtk_paned_get_position(paned));
    }
    else // GTK_ORIENTATION_VERTICAL
    {
        prefsPtr->SetInteger(QUIVER_PREFS_BROWSER,QUIVER_PREFS_BROWSER_FOLDER_VPANE,gtk_paned_get_position(paned));
    }
}

void notebook_page_added_cb  (GtkNotebook *notebook,
	GtkWidget *child, guint page_num, gpointer user_data)
{
	Browser::BrowserImpl *pBrowserImpl = (Browser::BrowserImpl*)user_data;

	gtk_notebook_set_show_tabs(notebook, gtk_notebook_get_n_pages(notebook) > 1);
	gtk_widget_set_visible(GTK_WIDGET(notebook), TRUE);

	gtk_widget_set_visible(pBrowserImpl->vpaned, TRUE);
}

void notebook_page_removed_cb  (GtkNotebook *notebook,
	GtkWidget *child, guint page_num, gpointer     user_data)
{
	Browser::BrowserImpl *pBrowserImpl = (Browser::BrowserImpl*)user_data;
	
	gtk_notebook_set_show_tabs(notebook, gtk_notebook_get_n_pages(notebook) > 1);
	if (0 == gtk_notebook_get_n_pages(notebook))
	{
		gtk_widget_set_visible(GTK_WIDGET(notebook), FALSE);
		if (!gtk_widget_is_visible(pBrowserImpl->m_pImageView))
		{
			gtk_widget_set_visible(pBrowserImpl->vpaned, FALSE);
		}
	}
}


Browser::BrowserImpl::BrowserImpl(Browser *parent) : 
	m_FolderTreePtr(new FolderTree()),
	m_ImageListPtr(new ImageList()),
	m_ThumbnailCache(100),
	m_IconCache(100),
	m_IconOverlayCache(100),
	m_ImageListEventHandlerPtr( new ImageListEventHandler(this) ),
	m_PreferencesEventHandlerPtr(new PreferencesEventHandler(this) ),
	m_FolderTreeEventHandlerPtr( new FolderTreeEventHandler(this) ),
	m_ThumbnailLoader(this,4)
{
	PreferencesPtr prefsPtr = Preferences::GetInstance();
	prefsPtr->AddEventHandler( m_PreferencesEventHandlerPtr );
	m_FolderTreePtr->AddEventHandler(m_FolderTreeEventHandlerPtr);

	m_BrowserParent = parent;
	// m_pUIManager = NULL; // GtkUIManager is deprecated
	m_bFolderTreeEvent = false;
	m_bBrowserHistoryEvent = false;

	m_iTimeoutUpdateListID = 0;
	m_iTimeoutHideLocationID = 0;
	m_iMergedBrowserUI = 0;

	m_pCssProvider = gtk_css_provider_new();

    GtkBuilder* builder = gtk_builder_new_from_file(QUIVER_DATADIR "/browser.ui");
    m_pBrowserWidget = GTK_WIDGET(gtk_builder_get_object(builder, "browser_hpaned"));
    vpaned = GTK_WIDGET(gtk_builder_get_object(builder, "browser_vpaned"));
    m_pNotebook = GTK_WIDGET(gtk_builder_get_object(builder, "browser_notebook"));
    m_pImageView = GTK_WIDGET(gtk_builder_get_object(builder, "browser_image_preview"));
    m_pLocationEntry = GTK_WIDGET(gtk_builder_get_object(builder, "browser_location_entry"));
    hscale = GTK_WIDGET(gtk_builder_get_object(builder, "browser_thumb_sizer"));
    m_pIconView = GTK_WIDGET(gtk_builder_get_object(builder, "browser_icon_view"));
    g_object_unref(builder);

	g_signal_connect (G_OBJECT (m_pNotebook), "page-added", G_CALLBACK (notebook_page_added_cb), this);
	g_signal_connect (G_OBJECT (m_pNotebook), "page-removed", G_CALLBACK (notebook_page_removed_cb), this);
	
    gtk_widget_set_visible(m_pImageView, FALSE);

	bool bShowPreview = prefsPtr->GetBoolean(QUIVER_PREFS_BROWSER,QUIVER_PREFS_BROWSER_PREVIEW_SHOW,true);
	if (bShowPreview)
	{
		gtk_widget_set_visible(m_pImageView, TRUE);
	}
	
	int hpaned_pos = prefsPtr->GetInteger(QUIVER_PREFS_BROWSER,QUIVER_PREFS_BROWSER_FOLDER_HPANE,200);
	gtk_paned_set_position(GTK_PANED(m_pBrowserWidget),hpaned_pos);

	int vpaned_pos = prefsPtr->GetInteger(QUIVER_PREFS_BROWSER,QUIVER_PREFS_BROWSER_FOLDER_VPANE,300);
	gtk_paned_set_position(GTK_PANED(vpaned),vpaned_pos);
	
	
	g_signal_connect (G_OBJECT (m_pBrowserWidget), "notify::position", G_CALLBACK (pane_position_notify_cb), this);
	g_signal_connect (G_OBJECT (vpaned), "notify::position", G_CALLBACK (pane_position_notify_cb), this);
	// g_object_ref(m_pBrowserWidget); // Not needed if parent takes ownership, which it should.
	
	m_pSWFolderTree = gtk_scrolled_window_new();
	// g_object_ref(m_pSWFolderTree); // Not needed if parent takes ownership.
	GtkWidget *pFolderTree = m_FolderTreePtr->GetWidget();
	
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(m_pSWFolderTree),pFolderTree);
	gtk_widget_set_visible(m_pSWFolderTree, TRUE);
    gtk_widget_set_visible(pFolderTree, TRUE);
	gtk_notebook_append_page(GTK_NOTEBOOK(m_pNotebook), m_pSWFolderTree,gtk_label_new("Folders"));	
	gtk_widget_set_visible(m_pNotebook, TRUE);

	if (!prefsPtr->GetBoolean(QUIVER_PREFS_BROWSER,QUIVER_PREFS_BROWSER_FOLDERTREE_SHOW,true))
	{	
		gtk_widget_set_visible(vpaned, FALSE);
	}

	// gtk_notebook_popup_enable(GTK_NOTEBOOK(m_pNotebook)); // Deprecated, no direct GTK4 equivalent
	gtk_notebook_set_scrollable (GTK_NOTEBOOK(m_pNotebook),TRUE);

	quiver_image_view_set_enable_transitions(QUIVER_IMAGE_VIEW(m_pImageView), true);
	quiver_image_view_set_magnification_mode(QUIVER_IMAGE_VIEW(m_pImageView),QUIVER_IMAGE_VIEW_MAGNIFICATION_MODE_SMOOTH);

    g_signal_connect (G_OBJECT (m_pImageView), "magnification-changed", G_CALLBACK (browser_imageview_magnification_changed), this);
    g_signal_connect (G_OBJECT (m_pImageView), "reload", G_CALLBACK (browser_imageview_reload), this);

	//popup menu stuff - GtkMenu is deprecated, use GtkPopoverMenu. Commenting out for now.
    // GtkGesture* gesture_image_view_click = gtk_gesture_click_new();
    // gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture_image_view_click), GDK_BUTTON_SECONDARY);
    // g_signal_connect_swapped(gesture_image_view_click, "pressed", G_CALLBACK(browser_show_context_menu), this);
    // gtk_widget_add_controller(m_pImageView, GTK_EVENT_CONTROLLER(gesture_image_view_click));


	quiver_icon_view_set_scroll_type(QUIVER_ICON_VIEW(m_pIconView),QUIVER_ICON_VIEW_SCROLL_SMOOTH);
	quiver_icon_view_set_n_items_func(QUIVER_ICON_VIEW(m_pIconView),(QuiverIconViewGetNItemsFunc)n_cells_callback,this,NULL);
	quiver_icon_view_set_thumbnail_pixbuf_func(QUIVER_ICON_VIEW(m_pIconView),(QuiverIconViewGetThumbnailPixbufFunc)thumbnail_pixbuf_callback,this,NULL);
	quiver_icon_view_set_icon_pixbuf_func(QUIVER_ICON_VIEW(m_pIconView),(QuiverIconViewGetIconPixbufFunc)icon_pixbuf_callback,this,NULL);

	g_signal_connect (G_OBJECT (hscale), "value-changed", G_CALLBACK (icon_size_value_changed), this);

	g_signal_connect(G_OBJECT(m_pIconView),"cell_activated",G_CALLBACK(iconview_cell_activated_cb),this);
	g_signal_connect(G_OBJECT(m_pIconView),"cursor_changed",G_CALLBACK(iconview_cursor_changed_cb),this);
	g_signal_connect(G_OBJECT(m_pIconView),"selection_changed",G_CALLBACK(iconview_selection_changed_cb),this);

	// popup menu stuff for icon view - GtkMenu is deprecated.
    // GtkGesture* gesture_icon_view_click = gtk_gesture_click_new();
    // gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture_icon_view_click), GDK_BUTTON_SECONDARY);
    // g_signal_connect_swapped(gesture_icon_view_click, "pressed", G_CALLBACK(browser_show_context_menu), this);
    // gtk_widget_add_controller(m_pIconView, GTK_EVENT_CONTROLLER(gesture_icon_view_click));

    GtkEventController* motion_controller = gtk_event_controller_motion_new();
    g_signal_connect(motion_controller, "motion", G_CALLBACK(iconview_motion_notify_cb), this);
    gtk_widget_add_controller(m_pIconView, motion_controller);
	
	g_signal_connect(G_OBJECT(m_pLocationEntry),"activate",G_CALLBACK(entry_activate),this);
    GtkEventController* key_controller_entry = gtk_event_controller_key_new();
    g_signal_connect(key_controller_entry, "key-pressed", G_CALLBACK(entry_key_press_cb), this);
    gtk_widget_add_controller(m_pLocationEntry, key_controller_entry);

    GtkEventController* focus_controller_entry = gtk_event_controller_focus_new();
    g_signal_connect (focus_controller_entry, "notify::is-focus", G_CALLBACK (entry_focus_in_cb), this); // "enter" is for crossing
    // g_signal_connect (focus_controller_entry, "leave", G_CALLBACK (entry_focus_out_cb), this); // "leave" is for crossing
    // Using notify::is-focus for both focus in and out detection based on state
    g_signal_connect (focus_controller_entry, "notify::is-focus", G_CALLBACK (entry_focus_out_cb), this);
    gtk_widget_add_controller(m_pLocationEntry, focus_controller_entry);


	string strBGColorImg   = prefsPtr->GetString(QUIVER_PREFS_APP,QUIVER_PREFS_APP_BG_IMAGEVIEW, "#000");
	string strBGColorThumb = prefsPtr->GetString(QUIVER_PREFS_APP,QUIVER_PREFS_APP_BG_ICONVIEW, "#444");

// #ifdef QUIVER_MAEMO // Hildon specific
//	if (!prefsPtr->GetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_USE_THEME_COLOR,false))
// #else
	if (!prefsPtr->GetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_USE_THEME_COLOR,true))
// #endif
	{
		// Assuming QuiverIconView and QuiverImageView have CSS names set in their class_init
		std::string strCSS =  "QuiverIconView { background-color:" + strBGColorThumb + ";}\n";
		strCSS += "QuiverImageView { background-color:" + strBGColorImg + ";}\n";
        gtk_css_provider_load_from_string(m_pCssProvider, strCSS.c_str());
		gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(m_pCssProvider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	}

	quiver_icon_view_set_overlay_pixbuf_func(QUIVER_ICON_VIEW(m_pIconView),(QuiverIconViewGetOverlayPixbufFunc)overlay_pixbuf_callback,this,NULL);

	IPixbufLoaderObserverPtr tmp ( new ImageViewPixbufLoaderObserver(QUIVER_IMAGE_VIEW(m_pImageView)) );
	m_ImageViewPixbufLoaderObserverPtr = tmp;
	m_ImageLoader.AddPixbufLoaderObserver(m_ImageViewPixbufLoaderObserverPtr.get());

	gtk_widget_set_visible(m_pBrowserWidget, TRUE);
	gtk_widget_set_visible(m_pBrowserWidget, FALSE); // Then hide it, as per original logic.


	gdouble thumb_size = (gdouble)prefsPtr->GetInteger(QUIVER_PREFS_BROWSER,QUIVER_PREFS_BROWSER_THUMB_SIZE);	

	if (thumb_size < 20. || 256. < thumb_size)
	{
		thumb_size = 128.;
	}
// #ifndef QUIVER_MAEMO // Hildon specific
	gtk_range_set_value(GTK_RANGE(hscale),thumb_size);
// #else
	// // gtk_range_set_value(GTK_RANGE(hscale),get_range_val_from_thumb_size((gint)thumb_size)); // Hildon specific
    // gtk_range_set_value(GTK_RANGE(hscale),thumb_size);
// #endif

    m_pActionGroup = g_simple_action_group_new();

    // Add toggle actions
    GAction *toggle_action_sidebar = (GAction*)g_simple_action_new_stateful(
        ACTION_BROWSER_VIEW_SIDEBAR, NULL, g_variant_new_boolean(prefsPtr->GetBoolean(QUIVER_PREFS_BROWSER, QUIVER_PREFS_BROWSER_FOLDERTREE_SHOW, true)));
    g_signal_connect(toggle_action_sidebar, "change-state", G_CALLBACK(browser_toggle_action_handler_cb), this);
    g_action_map_add_action(G_ACTION_MAP(m_pActionGroup), toggle_action_sidebar);

    GAction *toggle_action_preview = (GAction*)g_simple_action_new_stateful(
        ACTION_BROWSER_VIEW_PREVIEW, NULL, g_variant_new_boolean(prefsPtr->GetBoolean(QUIVER_PREFS_BROWSER, QUIVER_PREFS_BROWSER_PREVIEW_SHOW, true)));
    g_signal_connect(toggle_action_preview, "change-state", G_CALLBACK(browser_toggle_action_handler_cb), this);
    g_action_map_add_action(G_ACTION_MAP(m_pActionGroup), toggle_action_preview);

    // Add stateless actions
    const gchar *actions[] = {
        ACTION_BROWSER_OPEN_LOCATION,
        ACTION_BROWSER_HISTORY_BACK,
        ACTION_BROWSER_HISTORY_FORWARD,
        ACTION_BROWSER_CUT,
        ACTION_BROWSER_COPY,
        ACTION_BROWSER_PASTE,
        ACTION_BROWSER_SELECT_ALL,
        ACTION_BROWSER_TRASH,
        ACTION_BROWSER_RELOAD,
        ACTION_BROWSER_ZOOM_IN,
        ACTION_BROWSER_ZOOM_OUT,
        NULL
    };

    for (int i = 0; actions[i] != NULL; ++i)
    {
        GAction *action = (GAction*)g_simple_action_new(actions[i], NULL);
        g_signal_connect(action, "activate", G_CALLBACK(browser_action_handler_cb), this);
        g_action_map_add_action(G_ACTION_MAP(m_pActionGroup), action);
    }

    gtk_widget_insert_action_group(m_pBrowserWidget, "browser", G_ACTION_GROUP(m_pActionGroup));
}

Browser::BrowserImpl::~BrowserImpl()
{
	if (m_StatusbarPtr) {
		m_ImageLoader.RemovePixbufLoaderObserver(m_StatusbarPtr.get());
	}
	if (m_ImageViewPixbufLoaderObserverPtr) {
		m_ImageLoader.RemovePixbufLoaderObserver(m_ImageViewPixbufLoaderObserverPtr.get());
	}
	
	PreferencesPtr prefsPtr = Preferences::GetInstance();
	gdouble value = gtk_range_get_value (GTK_RANGE(hscale));

	gint val;

// #ifdef QUIVER_MAEMO // Hildon specific
	// // g_object_unref(m_pToolItemThumbSizer); // GtkToolItem is deprecated
	// // val = get_interpreted_thumb_size(value); // Hildon specific
    // val = (gint)value;
// #else
	val = (int)value;
// #endif

	prefsPtr->SetInteger(QUIVER_PREFS_BROWSER,QUIVER_PREFS_BROWSER_THUMB_SIZE,val);

	prefsPtr->RemoveEventHandler( m_PreferencesEventHandlerPtr );
	m_ImageListPtr->RemoveEventHandler(m_ImageListEventHandlerPtr);
	
	// g_object_unref(m_pBrowserWidget); // Widgets are unreffed when parent is destroyed. Only unref if explicitly reffed and not parented.
	// g_object_unref(m_pToolItemThumbSizer); // GtkToolItem is deprecated

	// if (m_pUIManager) // GtkUIManager is deprecated
	// {
	// 	g_object_unref(m_pUIManager);
	// }

	// g_object_unref(m_pSWFolderTree); // Should be unreffed when m_pNotebook is destroyed if it was a child

	g_signal_handlers_disconnect_matched( // Disconnect signals connected with 'this' as user_data
		m_pNotebook,
		G_SIGNAL_MATCH_DATA,
		0,
		0,
		NULL,
		NULL,
		this);
    // And for other widgets if signals were connected with 'this'
    // ... (hpaned_widget, vpaned, m_pImageView, m_pIconView, hscale, m_pLocationEntry)

	if (m_pCssProvider) {
		gtk_style_context_remove_provider_for_display(gdk_display_get_default(),GTK_STYLE_PROVIDER(m_pCssProvider));
		g_object_unref(m_pCssProvider);
		m_pCssProvider = NULL;
	}
    if (m_pSWFolderTree) { // If it was g_object_ref'd and not parented
        g_object_unref(m_pSWFolderTree);
        m_pSWFolderTree = NULL;
    }
     if (m_pBrowserWidget) { // If it was g_object_ref'd and not parented
        // g_object_unref(m_pBrowserWidget); // Usually not needed if it's the main widget returned by GetWidget()
        // m_pBrowserWidget = NULL;
    }


}

/*
void Browser::BrowserImpl::SetUIManager(GtkUIManager *ui_manager) // GtkUIManager is deprecated
{
	// PreferencesPtr prefsPtr = Preferences::GetInstance();
	
	// // if (m_pUIManager)
	// // {
	// // 	g_object_unref(m_pUIManager);
	// // }

	// // m_pUIManager = ui_manager;
	
	// // g_object_ref(m_pUIManager);


	// // guint n_entries = G_N_ELEMENTS (action_entries);
	// // GtkActionGroup* actions = gtk_action_group_new ("BrowserActions"); // GtkActionGroup is deprecated
	// // gtk_action_group_add_actions(actions, action_entries, n_entries, this); // GtkAction is deprecated

	// // gtk_action_group_add_toggle_actions(actions, // GtkToggleAction is deprecated
	// // 									action_entries_toggle,
	// // 									G_N_ELEMENTS (action_entries_toggle),
	// // 									this);
	// // gtk_ui_manager_insert_action_group (m_pUIManager,actions,0);	// GtkUIManager is deprecated

	// // g_object_unref(actions);

		
	// // GtkAction* action = QuiverUtils::GetAction(m_pUIManager,ACTION_BROWSER_VIEW_PREVIEW); // GtkAction is deprecated
	// // if (NULL != action)
	// // {
	// // 	bool bShowPreview = prefsPtr->GetBoolean(QUIVER_PREFS_BROWSER,QUIVER_PREFS_BROWSER_PREVIEW_SHOW);
	// // 	gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), bShowPreview ? TRUE : FALSE); // GtkToggleAction is deprecated
	// // }

	// // action = QuiverUtils::GetAction(m_pUIManager,ACTION_BROWSER_VIEW_SIDEBAR); // GtkAction is deprecated
	// // if (NULL != action)
	// // {
	// // 	bool bShowFolderTree = prefsPtr->GetBoolean(QUIVER_PREFS_BROWSER,QUIVER_PREFS_BROWSER_FOLDERTREE_SHOW);
	// // 	gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), bShowFolderTree ? TRUE : FALSE); // GtkToggleAction is deprecated
	// // }
}
*/

void Browser::BrowserImpl::UpdateUI()
{	
	// PreferencesPtr prefsPtr = Preferences::GetInstance();
	// // GtkAction* action; // GtkAction is deprecated
	// // action = QuiverUtils::GetAction(m_pUIManager, ACTION_BROWSER_HISTORY_FORWARD); // GtkUIManager is deprecated
	// // if (NULL != action)
	// // {
	// // 	gtk_action_set_sensitive(action,m_BrowserHistory.CanGoForward() ? TRUE : FALSE); // GtkAction is deprecated
	// // }
	// // action = QuiverUtils::GetAction(m_pUIManager, ACTION_BROWSER_HISTORY_BACK); // GtkUIManager is deprecated
	// // if (NULL != action)
	// // {
	// // 	gtk_action_set_sensitive(action,m_BrowserHistory.CanGoBack() ? TRUE : FALSE); // GtkAction is deprecated
	// // }

	// // action = QuiverUtils::GetAction(m_pUIManager, ACTION_BROWSER_HISTORY_BACK); // GtkUIManager is deprecated
	// // if (NULL != action)
	// // {
	// // 	gtk_action_set_sensitive(action,m_BrowserHistory.CanGoBack() ? TRUE : FALSE); // GtkAction is deprecated
	// // }

	// // action = QuiverUtils::GetAction(m_pUIManager, ACTION_BROWSER_VIEW_SIDEBAR); // GtkUIManager is deprecated
	// // if (NULL != action)
	// // {
	// // 	bool bFullscreen = prefsPtr->GetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_WINDOW_FULLSCREEN);
	// // 	if (bFullscreen)
	// // 	{
	// // 		if (prefsPtr->GetBoolean(QUIVER_PREFS_BROWSER,QUIVER_PREFS_BROWSER_FOLDERTREE_SHOW,true))
	// // 		{
	// // 			if (prefsPtr->GetBoolean(QUIVER_PREFS_BROWSER,QUIVER_PREFS_BROWSER_FOLDERTREE_HIDE_FS,true))
	// // 			{
	// // 				// gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action),false); // GtkToggleAction is deprecated
	// // 			}
	// // 		}

	// // 	}
	// // 	else
	// // 	{
	// // 		if (prefsPtr->GetBoolean(QUIVER_PREFS_BROWSER,QUIVER_PREFS_BROWSER_FOLDERTREE_SHOW,true))
	// // 		{
	// // 			if (prefsPtr->GetBoolean(QUIVER_PREFS_BROWSER,QUIVER_PREFS_BROWSER_FOLDERTREE_HIDE_FS,true))
	// // 			{
	// // 				// gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), true); // GtkToggleAction is deprecated
	// // 			}
	// // 		}
	// // 	}
	// // }
}

void Browser::BrowserImpl::Show()
{
	// // GError *tmp_error; // Not used
	// // tmp_error = NULL; // Not used

	// // if (m_pUIManager && 0 == m_iMergedBrowserUI) // GtkUIManager is deprecated
	// // {
	// // 	m_iMergedBrowserUI = gtk_ui_manager_add_ui_from_string(m_pUIManager, // GtkUIManager is deprecated
	// // 			ui_browser,
	// // 			strlen(ui_browser),
	// // 			&tmp_error);
	// // 	gtk_ui_manager_ensure_update(m_pUIManager); // GtkUIManager is deprecated

	// // 	GtkWidget* toolbar = gtk_ui_manager_get_widget(m_pUIManager,"/ui/ToolbarMain/"); // GtkUIManager is deprecated
	// // 	if (NULL != toolbar)
	// // 	{
	// // 		// gtk_toolbar_insert(GTK_TOOLBAR(toolbar),GTK_WIDGET(m_pToolItemThumbSizer),-1); // GtkToolbar & GtkToolItem deprecated
	// // 	}

	// // 	if (NULL != tmp_error)
	// // 	{
	// // 		g_warning("Browser::Show() Error: %s\n",tmp_error->message);
	// // 		g_error_free(tmp_error);
	// // 	}
	// // }

 	if (0 != m_ImageListPtr->GetSize())
	{
		quiver_icon_view_set_cursor_cell( QUIVER_ICON_VIEW(m_pIconView),
			m_ImageListPtr->GetCurrentIndex() );
	}
	
	gint cursor_cell = quiver_icon_view_get_cursor_cell(QUIVER_ICON_VIEW(m_pIconView));
 
	if (0 == m_ImageListPtr->GetSize() || m_QuiverFileCurrent != m_ImageListPtr->GetCurrent())
	{
		quiver_image_view_set_pixbuf(QUIVER_IMAGE_VIEW(m_pImageView),NULL);
	}
	else if (0 != m_ImageListPtr->GetSize())
	{
		if ( (gint)m_ImageListPtr->GetCurrentIndex() != cursor_cell  )
		{
		
			g_signal_handlers_block_by_func(m_pIconView,(gpointer)iconview_cursor_changed_cb,this);
	
			quiver_icon_view_set_cursor_cell(
				QUIVER_ICON_VIEW(m_pIconView),
				m_ImageListPtr->GetCurrentIndex() );
			
			g_signal_handlers_unblock_by_func(m_pIconView,(gpointer)iconview_cursor_changed_cb,this);
		}
	
	}

	gtk_widget_set_visible(m_pBrowserWidget, TRUE);
	
	if (m_ImageListPtr->GetSize() && m_QuiverFileCurrent != m_ImageListPtr->GetCurrent() )
	{
		SetImageIndex(m_ImageListPtr->GetCurrentIndex(), true);
	}
	m_ImageListPtr->UnblockHandler(m_ImageListEventHandlerPtr);
}

void Browser::BrowserImpl::Hide()
{
	gtk_widget_set_visible(m_pBrowserWidget, FALSE);
	// // if (m_pUIManager && 0 != m_iMergedBrowserUI) // GtkUIManager is deprecated
	// // {
	// // 	gtk_ui_manager_remove_ui(m_pUIManager, m_iMergedBrowserUI); // GtkUIManager is deprecated
	// // 	m_iMergedBrowserUI = 0;
	// // 	gtk_ui_manager_ensure_update(m_pUIManager); // GtkUIManager is deprecated

	// // 	GtkWidget* toolbar = gtk_ui_manager_get_widget(m_pUIManager,"/ui/ToolbarMain/"); // GtkUIManager is deprecated
	// // 	if (NULL != toolbar)
	// // 	{
	// // 		// gtk_container_remove(GTK_CONTAINER(toolbar),GTK_WIDGET(m_pToolItemThumbSizer)); // GtkContainer & GtkToolItem deprecated
	// // 	}
	// // }
	
	m_ImageListPtr->BlockHandler(m_ImageListEventHandlerPtr);
}

void Browser::BrowserImpl::SetImageList(ImageListPtr imglist)
{
	m_ImageListPtr->RemoveEventHandler(m_ImageListEventHandlerPtr);
	
	m_ImageListPtr = imglist;
	
	m_ImageListPtr->AddEventHandler(m_ImageListEventHandlerPtr);
	
	if (0 == m_iMergedBrowserUI) // m_iMergedBrowserUI is related to GtkUIManager, this logic might need review
	{
		m_ImageListPtr->BlockHandler(m_ImageListEventHandlerPtr);
	}

	list<string> dirs = m_ImageListPtr->GetFolderList();
	m_FolderTreePtr->SetSelectedFolders(dirs);
	
	list<string> files = m_ImageListPtr->GetFileList();
	dirs.insert(dirs.end(), files.begin(), files.end());
	
	std::string selected;
	if (0 != m_ImageListPtr->GetSize())
	{
		selected = m_ImageListPtr->GetCurrent().GetURI();
	}
	m_BrowserHistory.Add(dirs, selected);
	
	UpdateUI(); // This was UIManager related, might need rework or removal
}


void Browser::BrowserImpl::SetImageIndex(int index, bool bDirectionForward, bool bFromIconView /* = false */)
{
	gint width=0, height=0;

	QuiverImageViewMode mode = quiver_image_view_get_view_mode_unmagnified(QUIVER_IMAGE_VIEW(m_pImageView));
	
	if (mode != QUIVER_IMAGE_VIEW_MODE_ACTUAL_SIZE && gtk_widget_get_realized(m_pImageView))
	{
		width = gtk_widget_get_width(m_pImageView);
		height = gtk_widget_get_height(m_pImageView);
	}

	m_ImageListPtr->BlockHandler(m_ImageListEventHandlerPtr);
	if (m_ImageListPtr->SetCurrentIndex(index))
	{
		QuiverFile f;
		f = m_ImageListPtr->GetCurrent();

		m_BrowserHistory.SetCurrentSelected(f.GetURI());

		if (!bFromIconView)
		{
			g_signal_handlers_block_by_func(m_pIconView,(gpointer)iconview_cursor_changed_cb, this);
			
			quiver_icon_view_set_cursor_cell( QUIVER_ICON_VIEW(m_pIconView),
			      m_ImageListPtr->GetCurrentIndex() );	
	
			g_signal_handlers_unblock_by_func(m_pIconView,(gpointer)iconview_cursor_changed_cb, this);
		}
		
		if (gtk_widget_get_mapped(m_pImageView)) // Check if widget is visible and part of a hierarchy
		{
			
			m_ImageLoader.LoadImageAtSize(f,width,height);
			
			if (bDirectionForward)
			{
				// cache the next image if there is one
				if (m_ImageListPtr->HasNext())
				{
					f = m_ImageListPtr->GetNext();
					m_ImageLoader.CacheImageAtSize(f,width,height);
				}
			}
			else
			{
				// cache the next image if there is one
				if (m_ImageListPtr->HasPrevious())
				{
					f = m_ImageListPtr->GetPrevious();
					m_ImageLoader.CacheImageAtSize(f, width, height);
				}
				
			}
		}	
	}
	else
	{
		quiver_image_view_set_pixbuf(QUIVER_IMAGE_VIEW(m_pImageView), NULL);
	}
	
	m_ImageListPtr->UnblockHandler(m_ImageListEventHandlerPtr);
	
	if (m_ImageListPtr->GetSize())
	{
		m_QuiverFileCurrent = m_ImageListPtr->GetCurrent();
	}
	else
	{
		QuiverFile f; // Create empty/invalid QuiverFile
		m_QuiverFileCurrent = f;
	}
	
	// update the toolbar / menu buttons - (un)set sensitive 
	//UpdateUI(); // GtkUIManager related, comment out for now
}


ImageListPtr Browser::BrowserImpl::GetImageList()
{
	return m_ImageListPtr;
}


//=============================================================================
// BrowswerImpl Callbacks
//=============================================================================

// #ifdef QUIVER_MAEMO // Hildon specific
// static int get_interpreted_thumb_size(gdouble value)
// {
// 	gint val;
// 	value = 20. + value * 21.51;
// 	val = (gint)ceil(value);
// 	val = std::min(val,256); // Use std::min
// 	return val;
// }
// static gdouble get_range_val_from_thumb_size(gint thumbsize)
// {
// 	gdouble value = floor( ((thumbsize - 20) / 21.51) + .5);
// 	return value;
// }
// #endif

static void icon_size_value_changed (GtkRange *range,gpointer  user_data)
{
	Browser::BrowserImpl* b = (Browser::BrowserImpl*)user_data;
	gdouble value = gtk_range_get_value (range);
// #ifndef QUIVER_MAEMO // Hildon specific
	quiver_icon_view_set_icon_size(QUIVER_ICON_VIEW(b->m_pIconView), (gint)value,(gint)value);
// #else
// 	// gint val = get_interpreted_thumb_size(value); // Hildon specific
//     gint val = (gint)value;
// 	quiver_icon_view_set_icon_size(QUIVER_ICON_VIEW(b->m_pIconView), val,val);
// #endif
}

static guint n_cells_callback(QuiverIconView *iconview, gpointer user_data)
{
	Browser::BrowserImpl* b = (Browser::BrowserImpl*)user_data;
	return b->m_ImageListPtr->GetSize();
}

static GdkPixbuf* icon_pixbuf_callback(QuiverIconView *iconview, guint cell,gpointer user_data)
{
	Browser::BrowserImpl* b = (Browser::BrowserImpl*)user_data;
	QuiverFile f = (*b->m_ImageListPtr)[cell];
	GdkPixbuf* pixbuf = NULL;

	guint width, height;
	quiver_icon_view_get_icon_size(iconview,&width, &height);

	gchar* icon_name = f.GetIconName();
	if (icon_name)
	{
		gchar cache_icon_name [256] = "";
		g_snprintf(cache_icon_name,256,"%s%u-%u",icon_name,width,height); // Use %u for guint
		pixbuf = b->m_IconCache.GetPixbuf(cache_icon_name);
		if (NULL == pixbuf)
		{
			pixbuf = f.GetIcon(width,height); // Assuming GetIcon can handle this or needs adjustment
			if (NULL != pixbuf)
			{
				b->m_IconCache.AddPixbuf(cache_icon_name,pixbuf);
                // pixbuf is owned by the cache now, or a ref is taken.
			}
		}
		g_free(icon_name);
	}

	return pixbuf; // The caller (QuiverIconView) should ref if it keeps the pixbuf.
}

static gboolean thumbnail_loader_update_list (gpointer data)
{
	Browser::BrowserImpl* b = (Browser::BrowserImpl*)data;
	// gdk_threads_enter(); // GTK4: UI updates must be on main thread.
	b->m_ThumbnailLoader.UpdateList();
	b->m_iTimeoutUpdateListID = 0;
	// gdk_threads_leave();
	return G_SOURCE_REMOVE; // Correct return for g_timeout_add
}

void Browser::BrowserImpl::QueueIconViewUpdate(int timeout /* = 50 ms */)
{
	if (!m_iTimeoutUpdateListID)
	{
		m_iTimeoutUpdateListID = g_timeout_add(timeout,thumbnail_loader_update_list,this);
	}
}


static GdkPixbuf* thumbnail_pixbuf_callback(QuiverIconView *iconview, guint cell, gint* actual_width, gint* actual_height, gpointer user_data)
{
	Browser::BrowserImpl* b = (Browser::BrowserImpl*)user_data;

	GdkPixbuf *pixbuf = NULL;
	gboolean need_new_thumb = TRUE;
	
	guint width, height;
	quiver_icon_view_get_icon_size(iconview,&width,&height);

	QuiverFile f = (*b->m_ImageListPtr)[cell];

	if (f.IsFolder())
	{
		gint mouse_x = 0, mouse_y = 0; // Renamed to avoid conflict with width/height
		quiver_icon_view_get_cell_mouse_position(iconview, cell, &mouse_x, &mouse_y);

		if (0 <= mouse_x && 0 <= mouse_y && mouse_x < (gint)width && mouse_y < (gint)height)
		{
			double percent = (width > 0) ? (double(mouse_x) / width) : 0.0;
			// FIXME: this should be optimized. creating a new list
			// every time the mouse moves can be quite slow.
			ImageListPtr lstPtr(new ImageList());
			lstPtr->SetImageList(f.GetURI());
			unsigned int listSize = lstPtr->GetSize();
			if (0 != listSize)
			{
				unsigned int index = (unsigned int)(listSize * percent);
				index = std::min(index, listSize - 1);
				pixbuf = (*lstPtr)[index].GetThumbnail(std::max(width, height));
				*actual_width = (*lstPtr)[index].GetWidth();
				*actual_height = (*lstPtr)[index].GetHeight();
				if (4 < (*lstPtr)[index].GetOrientation()) // Orientations 5-8 mean rotated 90/270 deg
				{
					std::swap(*actual_width,*actual_height); // Use std::swap
				}
				need_new_thumb = FALSE;
			}
		}
	}
	else
	{
		pixbuf = b->m_ThumbnailCache.GetPixbuf(f.GetURI());

		if (pixbuf)
		{
			*actual_width = f.GetWidth();
			*actual_height = f.GetHeight();

			if (4 < f.GetOrientation())
			{
				std::swap(*actual_width,*actual_height); // Use std::swap
			}

			guint thumb_width, thumb_height;
			thumb_width = gdk_pixbuf_get_width(pixbuf);
			thumb_height = gdk_pixbuf_get_height(pixbuf);

			guint bound_width, bound_height;
			bound_width = *actual_width; // Start with actual image dimensions
			bound_height = *actual_height;
			// Scale actual dimensions down to fit icon_view's cell size (width, height)
			quiver_rect_get_bound_size(width,height, &bound_width,&bound_height,FALSE);


			if (bound_width == thumb_width && bound_height == thumb_height)
			{
				need_new_thumb = FALSE;
			} else {
                // Cached thumbnail is not the right size, need a new one
                // g_object_unref(pixbuf); // Do not unref here, cache owns it.
                pixbuf = NULL; // Signal to load a new one
            }
		}
	}
	
	if (need_new_thumb && !f.IsFolder()) // Only queue for files, folder preview is synchronous
	{
		b->QueueIconViewUpdate();
        if (pixbuf) { // If we had a pixbuf but it was wrong size.
            // g_object_unref(pixbuf); // The one from cache shouldn't be unreffed here.
            pixbuf = NULL; // Don't return the wrong-sized one.
        }
	}
	
	return pixbuf; // QuiverIconView should ref if it keeps it.
}

static GdkPixbuf* 
overlay_pixbuf_callback(QuiverIconView* iconview, guint cell, QuiverIconOverlayType type, gpointer user_data)
{
	GdkPixbuf* pixbuf = NULL;
	Browser::BrowserImpl* b = (Browser::BrowserImpl*)user_data;
	QuiverFile f = (*b->m_ImageListPtr)[cell];
	if (type == QUIVER_ICON_OVERLAY_ICON && f.IsFolder())
	{
		gchar* icon_name = f.GetIconName();
		if (icon_name)
		{
			pixbuf = b->m_IconOverlayCache.GetPixbuf(icon_name);
			if (NULL == pixbuf)
			{
				pixbuf = f.GetIcon(32,32); // Assuming GetIcon can handle this
				if (NULL != pixbuf)
				{
					b->m_IconOverlayCache.AddPixbuf(icon_name,pixbuf);
                    // pixbuf owned by cache
				}
			}
			g_free(icon_name);
		}
	}

	return pixbuf; // QuiverIconView should ref if it keeps it.
}

static void iconview_cell_activated_cb(QuiverIconView *iconview, guint cell, gpointer user_data)
{
	Browser::BrowserImpl* b = (Browser::BrowserImpl*)user_data;
	b->m_BrowserParent->EmitItemActivatedEvent();
}

static void iconview_cursor_changed_cb(QuiverIconView *iconview, guint cell, gpointer user_data)
{
	Browser::BrowserImpl* b = (Browser::BrowserImpl*)user_data;
	
	b->SetImageIndex(cell,true,true);
}

static void iconview_selection_changed_cb(QuiverIconView *iconview, gpointer user_data)
{
	Browser::BrowserImpl* b = (Browser::BrowserImpl*)user_data;

	// // GtkAction *action = gtk_ui_manager_get_action(b->m_pUIManager,"/ui/ToolbarMain/Trash/BrowserTrash"); // GtkUIManager is deprecated
	// // if (NULL != action)
	// // {
	// // 	GList *selection;
	// // 	selection = quiver_icon_view_get_selection(iconview);
	// // 	if (NULL == selection)
	// // 	{
	// // 		gtk_action_set_sensitive(action,FALSE); // GtkAction is deprecated
	// // 	}
	// // 	else
	// // 	{
	// // 		gtk_action_set_sensitive(action,TRUE); // GtkAction is deprecated
	// // 		g_list_free(selection);
	// // 	}
	// // }
	b->m_BrowserParent->EmitSelectionChangedEvent();
}

// static gboolean iconview_motion_notify(GtkWidget *widget, GdkEventMotion *event, gpointer user_data) // GdkEventMotion is GTK3
static void iconview_motion_notify_cb(GtkEventControllerMotion* controller, gdouble x, gdouble y, gpointer user_data)
{
	Browser::BrowserImpl* b = (Browser::BrowserImpl*)user_data;
	QuiverIconView *iconview = QUIVER_ICON_VIEW(gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(controller)));

	gulong cell =  quiver_icon_view_get_cell_for_xy(iconview, (gint)x, (gint)y);

	if (G_MAXULONG == cell) // Or some other invalid cell indicator from quiver_icon_view
		return;

	guint icon_width, icon_height;
	quiver_icon_view_get_icon_size(iconview,&icon_width,&icon_height);

	QuiverFile f = (*b->m_ImageListPtr)[cell];

	bool bClearMap = true;
	if (f.IsFolder())
	{
		gint cell_x = 0, cell_y = 0;
		quiver_icon_view_get_cell_mouse_position(iconview, cell, &cell_x, &cell_y);

		if (0 <= cell_x && 0 <= cell_y && cell_x < (gint)icon_width && cell_y < (gint)icon_height)
		{
			double percent = (icon_width > 0) ? (double(cell_x) / icon_width) : 0.0;
			ImageListPtr lstPtr(new ImageList());
			lstPtr->SetImageList(f.GetURI());
			unsigned int listSize = lstPtr->GetSize();
			if (0 != listSize)
			{
				unsigned int index = (unsigned int)(listSize * percent);
				index = std::min(index, listSize - 1);

				QuiverFile child = (*lstPtr)[index];

				std::string uri_old;
                auto it = b->m_mapFolderToFile.find(f.GetURI());
                if (it != b->m_mapFolderToFile.end()) {
                    uri_old = it->second;
                }
				std::string uri_new = child.GetURI();

				if (uri_new != uri_old)
				{
					quiver_icon_view_invalidate_cell(iconview,cell);
					b->m_mapFolderToFile[f.GetURI()] = uri_new;
				}
				bClearMap = false;
			}
		}
	}

	if (bClearMap)
	{
		b->m_mapFolderToFile.clear();
	}
}


// static gboolean entry_key_press (GtkWidget   *widget, GdkEventKey *event, gpointer user_data) // GdkEventKey is GTK3
static gboolean entry_key_press_cb (GtkEventControllerKey* controller, guint keyval, guint keycode, GdkModifierType state, gpointer user_data)
{
    GtkWidget* widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(controller));
	switch(keyval)
	{
		case GDK_KEY_Escape:
			gtk_widget_set_visible(widget, FALSE);
			return TRUE; // Event handled
	}
	return FALSE; // Event not handled
}

/* // Context menu logic needs complete rework for GtkPopoverMenu
static gboolean browser_popup_menu_cb (GtkWidget *widget, gpointer userdata)
{
	// browser_show_context_menu(NULL, userdata); // GtkMenu based popup is deprecated
	return TRUE; 
}

static void browser_button_press_cb(GtkGestureClick* gesture, int n_press, double x, double y, gpointer user_data)
{
    if (gtk_gesture_get_current_button(GTK_GESTURE(gesture)) == GDK_BUTTON_SECONDARY)
    {
        // browser_show_context_menu(gesture, n_press, x, y, user_data);
    }
} 

static void browser_show_context_menu(GtkGestureClick* gesture, int n_press, double x, double y, gpointer userdata)
{
	// Browser::BrowserImpl* b = (Browser::BrowserImpl*)userdata;

	// // if (NULL != b->m_pUIManager) // GtkUIManager is deprecated
	// // {
	// // 	GtkWidget *menu_model_widget; // This would be a GtkMenu built from a GMenuModel
	// // 	// menu_model_widget = gtk_ui_manager_get_widget (b->m_pUIManager,"/ui/ContextMenuMain"); // GtkUIManager is deprecated

    // //  GtkWidget* popover = gtk_popover_menu_new_from_model(G_MENU_MODEL(menu_model_widget)); // If menu_model_widget was a GMenuModel
    // //  gtk_popover_set_pointing_to(GTK_POPOVER(popover), &GRAPHENE_RECT_INIT((float)x, (float)y, 1, 1) ); // Point to click
    // //  gtk_popover_popup(GTK_POPOVER(popover));
	// // }
}
*/


static void entry_activate(GtkEntry *entry, gpointer user_data)
{
	Browser::BrowserImpl* b = (Browser::BrowserImpl*)user_data;
    const char* entry_text_const = gtk_editable_get_text(GTK_EDITABLE(entry));
    string entry_text = entry_text_const ? entry_text_const : "";

	list<string> file_list;
	file_list.push_back(entry_text);
	b->m_ImageListPtr->SetImageList(&file_list); // This might need a path if entry_text is not a URI
    gtk_widget_set_visible(GTK_WIDGET(entry), FALSE); // Hide entry after activation
	
}

static void browser_imageview_magnification_changed(QuiverImageView *imageview,gpointer data)
{
	Browser::BrowserImpl* pBrowserImpl = (Browser::BrowserImpl*)data;
	
	double mag = quiver_image_view_get_magnification(QUIVER_IMAGE_VIEW(pBrowserImpl->m_pImageView));
    if (pBrowserImpl->m_StatusbarPtr) {
	    pBrowserImpl->m_StatusbarPtr->SetMagnification((int)(mag*100+.5));
    }
}

static void browser_imageview_reload(QuiverImageView *imageview,gpointer data)
{
	Browser::BrowserImpl* pBrowserImpl = (Browser::BrowserImpl*)data;

	if (!pBrowserImpl->m_ImageListPtr->GetSize())
		return;

	ImageLoader::LoadParams params = {0}; // Initialize struct

	params.orientation = pBrowserImpl->m_ImageListPtr->GetCurrent().GetOrientation();
	params.reload = true;
	params.fullsize = true;
	params.no_thumb_preview = true;
	params.state = ImageLoader::LOAD;

	pBrowserImpl->m_ImageLoader.LoadImage(pBrowserImpl->m_ImageListPtr->GetCurrent(),params);
}

static void browser_toggle_action_handler_cb(GAction *action, GVariant *state, gpointer user_data)
{
    Browser::BrowserImpl *pBrowserImpl = (Browser::BrowserImpl *)user_data;
    const gchar *name = g_action_get_name(action);
    gboolean is_active = g_variant_get_boolean(state);

    if (strcmp(name, ACTION_BROWSER_VIEW_SIDEBAR) == 0)
    {
        gtk_widget_set_visible(pBrowserImpl->vpaned, is_active);
        Preferences::GetInstance()->SetBoolean(QUIVER_PREFS_BROWSER, QUIVER_PREFS_BROWSER_FOLDERTREE_SHOW, is_active);
    }
    else if (strcmp(name, ACTION_BROWSER_VIEW_PREVIEW) == 0)
    {
        gtk_widget_set_visible(pBrowserImpl->m_pImageView, is_active);
        Preferences::GetInstance()->SetBoolean(QUIVER_PREFS_BROWSER, QUIVER_PREFS_BROWSER_PREVIEW_SHOW, is_active);
    }

    g_action_change_state(action, state);
}

static void browser_action_handler_cb(GAction *action, GVariant *parameter, gpointer user_data)
{
    Browser::BrowserImpl *pBrowserImpl = (Browser::BrowserImpl *)user_data;
    const gchar *name = g_action_get_name(action);

    if (strcmp(name, ACTION_BROWSER_OPEN_LOCATION) == 0)
    {
        gtk_widget_set_visible(pBrowserImpl->m_pLocationEntry, TRUE);
        gtk_widget_grab_focus(pBrowserImpl->m_pLocationEntry);
    }
    else if (strcmp(name, ACTION_BROWSER_HISTORY_BACK) == 0)
    {
        pBrowserImpl->m_bBrowserHistoryEvent = true;
        if (pBrowserImpl->m_BrowserHistory.GoBack())
        {
            const list<string>& folders = pBrowserImpl->m_BrowserHistory.GetCurrentFiles();
            pBrowserImpl->m_ImageListPtr->UpdateImageList(&folders);
        }
        pBrowserImpl->m_bBrowserHistoryEvent = false;
    }
    else if (strcmp(name, ACTION_BROWSER_HISTORY_FORWARD) == 0)
    {
        pBrowserImpl->m_bBrowserHistoryEvent = true;
        if (pBrowserImpl->m_BrowserHistory.GoForward())
        {
            const list<string>& folders = pBrowserImpl->m_BrowserHistory.GetCurrentFiles();
            pBrowserImpl->m_ImageListPtr->UpdateImageList(&folders);
        }
        pBrowserImpl->m_bBrowserHistoryEvent = false;
    }
    else if (strcmp(name, ACTION_BROWSER_RELOAD) == 0)
    {
        pBrowserImpl->m_ImageListPtr->Reload();
    }
    else if (strcmp(name, ACTION_BROWSER_SELECT_ALL) == 0)
    {
        gtk_icon_view_select_all(GTK_ICON_VIEW(pBrowserImpl->m_pIconView));
    }
    else if (strcmp(name, ACTION_BROWSER_ZOOM_IN) == 0)
    {
        gdouble value = gtk_range_get_value(GTK_RANGE(pBrowserImpl->hscale));
        gtk_range_set_value(GTK_RANGE(pBrowserImpl->hscale), value + 10);
    }
    else if (strcmp(name, ACTION_BROWSER_ZOOM_OUT) == 0)
    {
        gdouble value = gtk_range_get_value(GTK_RANGE(pBrowserImpl->hscale));
        gtk_range_set_value(GTK_RANGE(pBrowserImpl->hscale), value - 10);
    }
    else if (strcmp(name, ACTION_BROWSER_CUT) == 0)
    {
        g_clipboard.clear();
        GList *selection = quiver_icon_view_get_selection(QUIVER_ICON_VIEW(pBrowserImpl->m_pIconView));
        for (GList *item = selection; item != NULL; item = g_list_next(item))
        {
            QuiverFile f = (*pBrowserImpl->m_ImageListPtr)[GPOINTER_TO_UINT(item->data)];
            g_clipboard.push_back(f.GetURI());
        }
        g_list_free(selection);
        g_is_cut = true;
    }
    else if (strcmp(name, ACTION_BROWSER_COPY) == 0)
    {
        g_clipboard.clear();
        GList *selection = quiver_icon_view_get_selection(QUIVER_ICON_VIEW(pBrowserImpl->m_pIconView));
        for (GList *item = selection; item != NULL; item = g_list_next(item))
        {
            QuiverFile f = (*pBrowserImpl->m_ImageListPtr)[GPOINTER_TO_UINT(item->data)];
            g_clipboard.push_back(f.GetURI());
        }
        g_list_free(selection);
        g_is_cut = false;
    }
    else if (strcmp(name, ACTION_BROWSER_PASTE) == 0)
    {
        const std::list<std::string>& dest_folder_uri_list = pBrowserImpl->m_BrowserHistory.GetCurrentFiles();
        if (!dest_folder_uri_list.empty())
        {
            std::string dest_folder_uri = dest_folder_uri_list.front();
            QuiverFile dest_folder(dest_folder_uri.c_str());
            for (const auto& src_uri : g_clipboard)
            {
                QuiverFile src_file(src_uri.c_str());
                if (g_is_cut)
                {
                    QuiverFileOps::MoveFile(src_file, dest_folder);
                }
                else
                {
                    QuiverFileOps::CopyFile(src_file, dest_folder);
                }
            }
            if (g_is_cut)
            {
                g_clipboard.clear();
            }
            pBrowserImpl->m_ImageListPtr->Reload();
        }
    }
    else if (strcmp(name, ACTION_BROWSER_TRASH) == 0)
    {
        GList *selection = quiver_icon_view_get_selection(QUIVER_ICON_VIEW(pBrowserImpl->m_pIconView));
        for (GList *item = selection; item != NULL; item = g_list_next(item))
        {
            QuiverFile f = (*pBrowserImpl->m_ImageListPtr)[GPOINTER_TO_UINT(item->data)];
            QuiverFileOps::MoveToTrash(f);
        }
        g_list_free(selection);
        pBrowserImpl->m_ImageListPtr->Reload();
    }
}

/*
static void browser_action_handler_cb(GtkAction *action, gpointer data) // GtkAction is deprecated
{
	// // Browser::BrowserImpl* pBrowserImpl;
	// // pBrowserImpl = (Browser::BrowserImpl*)data;
	// // PreferencesPtr prefsPtr = Preferences::GetInstance();
	
	// // //printf("Browser Action: %s\n",gtk_action_get_name(action));
	
	// // const gchar * szAction = gtk_action_get_name(action);
	
    // // ... (entire body commented out as it relies on GtkAction and GtkUIManager) ...
    // // This needs to be reimplemented using GAction / GMenuModel if the actions are still needed.
}
*/

//=============================================================================
// private browser implementation nested classes:
//=============================================================================
void Browser::BrowserImpl::ImageListEventHandler::HandleContentsChanged(ImageListEventPtr event)
{
	// get the list of files and folders in the image list
	list<string> dirs  = parent->m_ImageListPtr->GetFolderList();
	list<string> files = parent->m_ImageListPtr->GetFileList();

	// add new history event
	if (!parent->m_bBrowserHistoryEvent)
	{
		std::string selected;
		if (0 != parent->m_ImageListPtr->GetSize())
		{
			selected = parent->m_ImageListPtr->GetCurrent().GetURI();
		}
		list<string> history_items = dirs; // Create a copy for history
		history_items.insert(history_items.end(), files.begin(), files.end());
		parent->m_BrowserHistory.Add(history_items, selected);
	}

	// refresh the list
	parent->SetImageIndex(parent->m_ImageListPtr->GetCurrentIndex(),true);
			
	quiver_icon_view_invalidate_window(QUIVER_ICON_VIEW(parent->m_pIconView)); // This might be quiver_icon_view_queue_draw or similar
	parent->m_ThumbnailLoader.UpdateList(true);	
	
	if (1 == dirs.size() && 0 == files.size())
	{
		GFile* file = g_file_new_for_uri(dirs.front().c_str()); 
		char* local_path = g_file_get_path(file);
		if (NULL != local_path)
		{
            gtk_editable_set_text(GTK_EDITABLE(parent->m_pLocationEntry),local_path);
			g_free(local_path);
		}
		else
		{
            gtk_editable_set_text(GTK_EDITABLE(parent->m_pLocationEntry),dirs.front().c_str());
		}
		g_object_unref(file);
	}
	else if (0 == dirs.size() && 1 == files.size())
	{
        gtk_editable_set_text(GTK_EDITABLE(parent->m_pLocationEntry),files.front().c_str());
	}
	else if (0 == dirs.size() && 0 == files.size())
	{
        gtk_editable_set_text(GTK_EDITABLE(parent->m_pLocationEntry),"");
	}
	else
	{
		gchar szText[256] = "";
		if (0 == dirs.size())
		{
			g_snprintf(szText,256,"%zu files", files.size());
		}
		else if (0 == files.size())
		{
			g_snprintf(szText,256,"%zu folders", dirs.size());
		}
		else
		{
			g_snprintf(szText,256,"%zu folders, %zu files", dirs.size(), files.size());
		}
        gtk_editable_set_text(GTK_EDITABLE(parent->m_pLocationEntry),szText);
		
	}
	
	if (!parent->m_bFolderTreeEvent)
	{
		parent->m_FolderTreePtr->SetSelectedFolders(dirs);
	}
	
	parent->UpdateUI(); // UIManager related, might need removal/rework
}

void Browser::BrowserImpl::ImageListEventHandler::HandleCurrentIndexChanged(ImageListEventPtr event) 
{
	parent->SetImageIndex(event->GetIndex(),true);
	
	parent->m_BrowserParent->EmitCursorChangedEvent();
}

void Browser::BrowserImpl::ImageListEventHandler::HandleItemAdded(ImageListEventPtr event)
{
	// refresh the list
	parent->SetImageIndex(parent->m_ImageListPtr->GetCurrentIndex(),true);
	parent->m_ThumbnailLoader.UpdateList(true);
}

void Browser::BrowserImpl::ImageListEventHandler::HandleItemRemoved(ImageListEventPtr event)
{
	parent->SetImageIndex(parent->m_ImageListPtr->GetCurrentIndex(),true);
	parent->m_ThumbnailLoader.UpdateList(true);
}

void Browser::BrowserImpl::ImageListEventHandler::HandleItemChanged(ImageListEventPtr event)
{
	QuiverFile f = parent->m_ImageListPtr->Get(event->GetIndex());
	//printf ("image list item changed %d: %s\n",event->GetIndex() , f.GetURI());

	parent->m_ThumbnailCache.RemovePixbuf(f.GetURI());
		
	parent->SetImageIndex(parent->m_ImageListPtr->GetCurrentIndex(),true);
	// refresh the list
	parent->m_ThumbnailLoader.UpdateList(true);	
	
}

void Browser::BrowserImpl::PreferencesEventHandler::HandlePreferenceChanged(PreferencesEventPtr event)
{
	PreferencesPtr prefsPtr = Preferences::GetInstance();
	if (QUIVER_PREFS_APP == event->GetSection() )
	{
		if (QUIVER_PREFS_APP_USE_THEME_COLOR == event->GetKey() )
		{
			if (event->GetNewBoolean())
			{
				// use theme color
				gtk_style_context_remove_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(parent->m_pCssProvider));
			}
			else
			{
				string strBGColorThumb = prefsPtr->GetString(QUIVER_PREFS_APP,QUIVER_PREFS_APP_BG_ICONVIEW);						
				string strBGColorImg   = prefsPtr->GetString(QUIVER_PREFS_APP,QUIVER_PREFS_APP_BG_IMAGEVIEW);
				std::string strCSS =  "QuiverIconView { background-color:" + strBGColorThumb + ";}\n";
				strCSS += "QuiverImageView { background-color:" + strBGColorImg + ";}\n";
                gtk_css_provider_load_from_string(parent->m_pCssProvider, strCSS.c_str());
				gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(parent->m_pCssProvider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
				
			}
		}
		else if (QUIVER_PREFS_APP_BG_IMAGEVIEW == event->GetKey() || QUIVER_PREFS_APP_BG_ICONVIEW == event->GetKey() )
		{
			if ( !prefsPtr->GetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_USE_THEME_COLOR,true) )
			{
				
				string strBGColorThumb = prefsPtr->GetString(QUIVER_PREFS_APP,QUIVER_PREFS_APP_BG_ICONVIEW);						
				string strBGColorImg   = prefsPtr->GetString(QUIVER_PREFS_APP,QUIVER_PREFS_APP_BG_IMAGEVIEW);
				std::string strCSS =  "QuiverIconView { background-color:" + strBGColorThumb + ";}\n";
				strCSS += "QuiverImageView { background-color:" + strBGColorImg + ";}\n";
                gtk_css_provider_load_from_string(parent->m_pCssProvider, strCSS.c_str());
				// gtk_style_context_add_provider_for_display is not needed here again if already added and provider is just reloaded.
                // However, to be safe, ensure it's present or re-add if necessary.
                // For GTK4, it's better to load new data into the existing provider.
			}
		}
		else if (QUIVER_PREFS_APP_WINDOW_FULLSCREEN == event->GetKey() )
		{
			parent->UpdateUI(); // UIManager related
		}
	}
}


void Browser::BrowserImpl::FolderTreeEventHandler::HandleSelectionChanged(FolderTreeEventPtr event)
{
	list<string> listFolders = parent->m_FolderTreePtr->GetSelectedFolders();
	// list<string>::iterator itr; // Not used

	parent->m_bFolderTreeEvent = true;
	parent->m_ImageListPtr->UpdateImageList(&listFolders);
	parent->m_bFolderTreeEvent = false;
}



void Browser::BrowserImpl::BrowserThumbLoader::LoadThumbnail(const ThumbLoaderItem &item, guint uiWidth, guint uiHeight)
{

	if (gtk_widget_get_mapped(m_pBrowserImpl->m_pIconView) && 
		item.m_ulIndex < m_pBrowserImpl->m_ImageListPtr->GetSize())
	{
		QuiverFile f(item.m_QuiverFile);

		GdkPixbuf *pixbuf = NULL;
		pixbuf = m_pBrowserImpl->m_ThumbnailCache.GetPixbuf(f.GetURI());				
	
		if (NULL != pixbuf)
		{
			// check if the thumbnail is the correct size
			guint thumb_width, thumb_height;
			guint bound_width_check = f.GetWidth(); // Use a different name
			guint bound_height_check = f.GetHeight();

			if (4 < f.GetOrientation())
			{
				std::swap(bound_width_check,bound_height_check); // Use std::swap
			}

			thumb_width = gdk_pixbuf_get_width(pixbuf);
			thumb_height = gdk_pixbuf_get_height(pixbuf);
			
			// Calculate the target size for the thumbnail based on icon view cell dimensions
            guint target_bound_width = f.GetWidth();
            guint target_bound_height = f.GetHeight();
            if (4 < f.GetOrientation()) {
                std::swap(target_bound_width, target_bound_height);
            }
			quiver_rect_get_bound_size(uiWidth,uiHeight, &target_bound_width, &target_bound_height,FALSE);

			if (thumb_width != target_bound_width || thumb_height != target_bound_height)
			{
				// need a new thumbnail because the current cached size
				// is not the same as the size needed
				// g_object_unref(pixbuf); // Do not unref, cache owns it.
				pixbuf = NULL; // Signal to reload
			}
				
		}

		if (NULL == pixbuf) // If not in cache or wrong size
		{
			pixbuf = f.GetThumbnail(std::max(uiWidth,uiHeight)); // Request a new one
		}

		if (NULL != pixbuf) // If we got a thumbnail (either from cache initially or fresh)
		{
			guint current_thumb_width = gdk_pixbuf_get_width(pixbuf);
			guint current_thumb_height = gdk_pixbuf_get_height(pixbuf);

			guint target_bound_width_final = f.GetWidth();
			guint target_bound_height_final = f.GetHeight();
			
			if (4 < f.GetOrientation())
			{
				std::swap(target_bound_width_final,target_bound_height_final); // Use std::swap
			}
			quiver_rect_get_bound_size(uiWidth,uiHeight, &target_bound_width_final,&target_bound_height_final,FALSE);

			if (current_thumb_width != target_bound_width_final || current_thumb_height != target_bound_height_final)
			{
				GdkPixbuf* newpixbuf = gdk_pixbuf_scale_simple (
								pixbuf,
								target_bound_width_final,
								target_bound_height_final,
								GDK_INTERP_BILINEAR); // GDK_INTERP_NEAREST or GDK_INTERP_BILINEAR
				g_object_unref(pixbuf); // Unref the old one (either freshly loaded or from cache if it was wrong size)
				pixbuf = newpixbuf; // This is the correctly scaled one
			}

			m_pBrowserImpl->m_ThumbnailCache.AddPixbuf(f.GetURI(),pixbuf); // Add/replace in cache
			// gdk_threads_enter(); // GTK4: UI updates must be on main thread.
			// quiver_icon_view_invalidate_cell might need to become quiver_icon_view_queue_draw_cell or similar
			quiver_icon_view_invalidate_cell(QUIVER_ICON_VIEW(m_pBrowserImpl->m_pIconView),item.m_ulIndex);
			// gdk_threads_leave();
            g_object_unref(pixbuf); // Cache took a ref, so unref our copy.
		}
	}
}

void Browser::BrowserImpl::BrowserThumbLoader::GetVisibleRange(gulong* pulStart, gulong* pulEnd)
{
	quiver_icon_view_get_visible_range(QUIVER_ICON_VIEW(m_pBrowserImpl->m_pIconView),pulStart, pulEnd);
}


void Browser::BrowserImpl::BrowserThumbLoader::GetIconSize(guint* puiWidth, guint* puiHeight)
{
	quiver_icon_view_get_icon_size(QUIVER_ICON_VIEW(m_pBrowserImpl->m_pIconView), puiWidth, puiHeight);
}

gulong Browser::BrowserImpl::BrowserThumbLoader::GetNumItems()
{
	return m_pBrowserImpl->m_ImageListPtr->GetSize();
}

QuiverFile Browser::BrowserImpl::BrowserThumbLoader::GetQuiverFile(gulong index)
{
	if (index < m_pBrowserImpl->m_ImageListPtr->GetSize())
	{
		return (*m_pBrowserImpl->m_ImageListPtr)[index];
	}
	return QuiverFile(); // Return default/invalid QuiverFile
}

void Browser::BrowserImpl::BrowserThumbLoader::SetIsRunning(bool bIsRunning)
{
	if (m_pBrowserImpl->m_StatusbarPtr.get())
	{
		// gdk_threads_enter(); // GTK4: UI updates must be on main thread.
		if (bIsRunning)
		{
			m_pBrowserImpl->m_StatusbarPtr->StartProgressPulse();
		}
		else
		{
			m_pBrowserImpl->m_StatusbarPtr->StopProgressPulse();
		}
		// gdk_threads_leave();
	}
	
}

void Browser::BrowserImpl::BrowserThumbLoader::SetCacheSize(guint uiCacheSize)
{
	m_pBrowserImpl->m_ThumbnailCache.SetSize(uiCacheSize);
}
