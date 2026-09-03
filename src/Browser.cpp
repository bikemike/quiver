#include <config.h>

#include <pthread.h>

#include <gtk/gtk.h>
#include <string.h>
#include <list>
#include <map>
#include <set>

#include <gdk/gdkkeysyms.h>

#include <gio/gio.h>

#include <libquiver/quiver-icon-view.h>
#include <libquiver/quiver-image-view.h>
#include <libquiver/quiver-pixbuf-utils.h>

#include "Browser.h"
#include "ThreadUtil.h"
#include "FolderTree.h"
#include "ImageList.h"
#include "ImageCache.h"
#include "ImageLoader.h"
#include "IPixbufLoaderObserver.h"
#include "QuiverUtils.h"
#include "QuiverPrefs.h"
#include "QuiverFileOps.h"
#include "BrowserHistory.h"

#include "Statusbar.h"

#include "IImageListEventHandler.h"
#include "IPreferencesEventHandler.h"
#include "IFolderTreeEventHandler.h"
#include "IconViewThumbLoader.h"

#include "QuiverStockIcons.h"

using namespace std;

#if (GLIB_MAJOR_VERSION < 2) || (GLIB_MAJOR_VERSION == 2 && GLIB_MINOR_VERSION < 10)
#define g_object_ref_sink(o) G_STMT_START{	\
	  g_object_ref (o);				\
	  gtk_object_sink ((GtkObject*)o);		\
}G_STMT_END
#endif



// ============================================================================
// Browser::BrowserImpl: private implementation (hidden from header file)
// ============================================================================

typedef boost::shared_ptr<IPixbufLoaderObserver> IPixbufLoaderObserverPtr;


/* Shared, ref-counted handle to the browser's preview image-view widget.
 * Both the loader observer and pixbuf idles posted from the loader thread hold
 * a reference; "destroy" NULLs the pointer so a queued idle cannot write into
 * a freed widget after component teardown. */
struct PixbufTarget {
	QuiverImageView *pImageView;
	int iRefs;
};

static void pixbuf_target_destroyed(GtkWidget *widget, gpointer data)
{
	(void)widget;
	((PixbufTarget*)data)->pImageView = NULL;
}

static PixbufTarget* pixbuf_target_new(QuiverImageView *pImageView)
{
	PixbufTarget *t = new PixbufTarget{pImageView, 1};
	g_signal_connect(G_OBJECT(pImageView), "destroy", G_CALLBACK(pixbuf_target_destroyed), t);
	return t;
}

static void pixbuf_target_ref(PixbufTarget *t)
{
	g_atomic_int_inc(&t->iRefs);
}

static void pixbuf_target_unref(PixbufTarget *t)
{
	if (g_atomic_int_dec_and_test(&t->iRefs))
		delete t;
}

struct AsyncPixbufData {
	PixbufTarget *pTarget;
	GdkPixbuf *pixbuf;
	gint width, height;
	gboolean bReset;
	bool bAtSize;
};

static gboolean idle_set_pixbuf_b(gpointer data) {
	AsyncPixbufData *p = (AsyncPixbufData*)data;
	/* the preview image view may already be gone (queued before teardown) */
	if (p->pTarget->pImageView != NULL)
	{
		if (p->bAtSize) {
			quiver_image_view_set_pixbuf_at_size_ex(p->pTarget->pImageView, p->pixbuf, p->width, p->height, p->bReset);
		} else {
			quiver_image_view_set_pixbuf(p->pTarget->pImageView, p->pixbuf);
		}
	}
	if (p->pixbuf) g_object_unref(p->pixbuf);
	pixbuf_target_unref(p->pTarget);
	delete p;
	return FALSE;
}

class ImageViewPixbufLoaderObserver : public IPixbufLoaderObserver
{
public:
	ImageViewPixbufLoaderObserver(QuiverImageView *imageview){m_pTarget = pixbuf_target_new(imageview);};
	virtual ~ImageViewPixbufLoaderObserver(){ pixbuf_target_unref(m_pTarget); };

	virtual void ConnectSignals(GdkPixbufLoader *loader){
			quiver_image_view_connect_pixbuf_loader_signals(m_pTarget->pImageView,loader);
		};
	virtual void ConnectSignalSizePrepared(GdkPixbufLoader * loader){
			quiver_image_view_connect_pixbuf_size_prepared_signal(m_pTarget->pImageView,loader);
		};

	// custom calls
	virtual void SetPixbuf(GdkPixbuf * pixbuf){
		if (ThreadUtil::IsGUIThread()) {
			quiver_image_view_set_pixbuf(m_pTarget->pImageView,pixbuf);
		} else {
			if (pixbuf) g_object_ref(pixbuf);
			AsyncPixbufData *data = new AsyncPixbufData{m_pTarget, pixbuf, 0, 0, FALSE, false};
			pixbuf_target_ref(m_pTarget);
			g_idle_add_full(G_PRIORITY_HIGH, idle_set_pixbuf_b, data, NULL);
		}
	};
	virtual void SetPixbufAtSize(GdkPixbuf *pixbuf, gint width, gint height, bool bResetViewMode = true ){
		gboolean bReset = bResetViewMode ? TRUE : FALSE;
		if (ThreadUtil::IsGUIThread()) {
			quiver_image_view_set_pixbuf_at_size_ex(m_pTarget->pImageView,pixbuf,width,height,bReset);
		} else {
			if (pixbuf) g_object_ref(pixbuf);
			AsyncPixbufData *data = new AsyncPixbufData{m_pTarget, pixbuf, width, height, bReset, true};
			pixbuf_target_ref(m_pTarget);
			g_idle_add_full(G_PRIORITY_HIGH, idle_set_pixbuf_b, data, NULL);
		}
	};
	virtual void SignalBytesRead(long bytes_read,long total){ (void)total;  (void)bytes_read; };
private:
	PixbufTarget *m_pTarget;
};


class Browser::BrowserImpl
{
public:
/* constructors and destructor */
	BrowserImpl(Browser *parent);
	~BrowserImpl();
	
/* member functions */

	void RegisterActions();
	void SetToolbar(GtkWidget *toolbar);
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
	
	//GtkWidget *hpaned;
	GtkWidget *vpaned;
	GtkWidget *m_pNotebook;
	GtkWidget *m_pSWFolderTree;
	
	GtkWidget *m_pImageView;

	GtkWidget *m_pLocationEntry;
	GtkWidget *hscale;

	GtkWidget *m_pToolItemThumbSizer;
	
	GtkWidget *m_pToolbar;

	GtkWidget *m_pContextMenuPopover;
	
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
	
};
// ============================================================================


static void browser_action_handler_cb(GSimpleAction *action, GVariant *parameter, gpointer data);

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
		selection_list.push_back((uintptr_t)item->data);
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



void
Browser::SetStatusbar(StatusbarPtr statusbarPtr)
{
	m_BrowserImplPtr->m_ImageLoader.RemovePixbufLoaderObserver(m_BrowserImplPtr->m_StatusbarPtr.get());
	
	m_BrowserImplPtr->m_StatusbarPtr = statusbarPtr;
	
	m_BrowserImplPtr->m_ImageLoader.AddPixbufLoaderObserver(m_BrowserImplPtr->m_StatusbarPtr.get());

}

void 
Browser::RegisterActions()
{
	m_BrowserImplPtr->RegisterActions();
}

void 
Browser::SetToolbar(GtkWidget *toolbar)
{
	m_BrowserImplPtr->SetToolbar(toolbar);
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


//=============================================================================
//=============================================================================
// private browser implementation:
//=============================================================================


//=============================================================================
// BrowswerImpl Callback Prototypes
//=============================================================================

static GdkPixbuf* icon_pixbuf_callback(QuiverIconView *iconview, gulong cell,gpointer user_data);
static gchar* text_pixbuf_callback(QuiverIconView *iconview, gulong cell,gpointer user_data);
static GdkPixbuf* thumbnail_pixbuf_callback(QuiverIconView *iconview, gulong cell, gint* actual_width, gint* actual_height, gpointer user_data);
static GdkPixbuf* overlay_pixbuf_callback(QuiverIconView* iconview, gulong cell, QuiverIconOverlayType type, gpointer user_data);
static gulong n_cells_callback(QuiverIconView *iconview, gpointer user_data);
static void icon_size_value_changed (GtkRange *range,gpointer  user_data);

static void iconview_cell_activated_cb(QuiverIconView *iconview, guint cell, gpointer user_data);
static void iconview_cursor_changed_cb(QuiverIconView *iconview, guint cell, gpointer user_data);
static void iconview_selection_changed_cb(QuiverIconView *iconview, gpointer user_data);
	static void iconview_motion_notify(GtkEventControllerMotion *controller, gdouble x, gdouble y, gpointer user_data);

static void browser_button_press_cb(GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data); 
static void browser_show_context_menu(GtkWidget *widget, gdouble x, gdouble y, gpointer userdata);

static void entry_activate(GtkEntry *entry, gpointer user_data);
static gboolean entry_key_press (GtkEventControllerKey *controller, guint keyval, guint keycode, GdkModifierType state, gpointer user_data);

static void browser_imageview_magnification_changed(QuiverImageView *imageview,gpointer data);
static void browser_imageview_reload(QuiverImageView *imageview,gpointer data);

static void entry_focus_in ( GtkEventControllerFocus *controller, gpointer user_data)
{ (void)controller; 
	Browser::BrowserImpl *pBrowserImpl = (Browser::BrowserImpl*)user_data;

	QuiverUtils::DisconnectUnmodifiedAccelerators();
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
	return FALSE;
}

static void entry_focus_out ( GtkEventControllerFocus *controller, gpointer user_data)
{ (void)controller; 
	Browser::BrowserImpl *pBrowserImpl = (Browser::BrowserImpl*)user_data;

	QuiverUtils::ConnectUnmodifiedAccelerators();

	if (0 == pBrowserImpl->m_iTimeoutHideLocationID)
	{
		pBrowserImpl->m_iTimeoutHideLocationID = g_timeout_add(10,timeout_hide_location,pBrowserImpl->m_pLocationEntry);
	}
}

static void pane_position_changed (GObject* widget, GParamSpec* pspec, gpointer user_data)
{ (void)user_data; (void)pspec; (void)widget;
	PreferencesPtr prefsPtr = Preferences::GetInstance();
	if (GTK_IS_PANED(widget))
	{
		if (gtk_orientable_get_orientation(GTK_ORIENTABLE(widget)) == GTK_ORIENTATION_HORIZONTAL)
		{
			prefsPtr->SetInteger(QUIVER_PREFS_BROWSER,QUIVER_PREFS_BROWSER_FOLDER_HPANE,gtk_paned_get_position(GTK_PANED(widget)));
		}
		else
		{
			prefsPtr->SetInteger(QUIVER_PREFS_BROWSER,QUIVER_PREFS_BROWSER_FOLDER_VPANE,gtk_paned_get_position(GTK_PANED(widget)));
		}
	}
}

/*
	bool visible = false;
	GList* vchildren = gtk_container_get_children(GTK_CONTAINER(pBrowserImpl->vpaned));
	GList* vchild = vchildren;
	while (NULL != vchild)
	{
		if (GTK_WIDGET_VISIBLE(GTK_WIDGET(vchild->data)))
		{
			visible = true;
			break;
		}
		vchild  = g_list_next(vchild);
	}
	if (NULL != vchildren)
	{
		g_list_free(vchildren);
	}
	if (!visible)
	{
		gtk_widget_set_visible(pBrowserImpl->vpaned, FALSE);Initialize
	}
*/

void notebook_page_added  (GtkNotebook *notebook, 
	GtkWidget *child, guint page_num, gpointer user_data)
{ (void)page_num;  (void)child;  (void)user_data;

	gtk_notebook_set_show_tabs(notebook, 1 > gtk_notebook_get_n_pages(notebook));

}

void notebook_page_removed  (GtkNotebook *notebook, 
	GtkWidget *child, guint page_num, gpointer     user_data)
{ (void)page_num;  (void)child; 
	Browser::BrowserImpl *pBrowserImpl = (Browser::BrowserImpl*)user_data;
	
	gtk_notebook_set_show_tabs(notebook, 1 <= gtk_notebook_get_n_pages(notebook));
	if (0 == gtk_notebook_get_n_pages(notebook))
	{
		gtk_widget_set_visible(GTK_WIDGET(notebook), FALSE);
		if (!gtk_widget_get_visible(pBrowserImpl->m_pImageView))
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
	m_pToolbar = NULL;
	m_bFolderTreeEvent = false;
	m_bBrowserHistoryEvent = false;

	m_iTimeoutUpdateListID = 0;
	m_iTimeoutHideLocationID = 0;
	/*
	 * layout for the browser gui:
	 * hpaned
	 *   -> vpaned
	 *     -> notebook
	 *     -> imageview
	 *   -> vbox
	 *     -> hbox
	 *       -> gtkentry
	 *       -> gtkhscale
	 *     -> scrolled window
	 *       -> icon view
	 */
	GtkWidget *hpaned;
	GtkWidget *scrolled_window;
	GtkWidget *hbox,*vbox;
	
	m_pCssProvider = gtk_css_provider_new();

	hscale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,20,256,1);
	gtk_range_set_value(GTK_RANGE(hscale),128);
	gtk_scale_set_value_pos (GTK_SCALE(hscale),GTK_POS_LEFT);
	gtk_scale_set_draw_value(GTK_SCALE(hscale),FALSE);

	gtk_widget_set_size_request(hscale,100,-1);
	m_pToolItemThumbSizer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_set_hexpand(m_pToolItemThumbSizer, TRUE);

	gtk_widget_set_halign(hscale, GTK_ALIGN_END);
	gtk_widget_set_valign(hscale, GTK_ALIGN_CENTER);
	
	gtk_box_append(GTK_BOX(m_pToolItemThumbSizer), hscale);

	g_object_ref(m_pToolItemThumbSizer);
	
	m_pLocationEntry = gtk_entry_new();
	gtk_widget_set_visible(m_pLocationEntry, FALSE);
	gtk_widget_set_hexpand(m_pLocationEntry, TRUE);
	
	hpaned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
	vpaned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
	m_pNotebook = gtk_notebook_new();
	
#if (GTK_MAJOR_VERSION > 2) || (GTK_MAJOR_VERSION == 2 && GTK_MINOR_VERSION >= 10)
	g_signal_connect (G_OBJECT (m_pNotebook), "page-added",
	      G_CALLBACK (notebook_page_added), this);
	g_signal_connect (G_OBJECT (m_pNotebook), "page-removed",
	      G_CALLBACK (notebook_page_removed), this);
#endif
	
	m_pIconView = quiver_icon_view_new();
	m_pImageView = quiver_image_view_new();

	bool bShowPreview = prefsPtr->GetBoolean(QUIVER_PREFS_BROWSER,QUIVER_PREFS_BROWSER_PREVIEW_SHOW,true);
	if (bShowPreview)
	{
		gtk_widget_set_visible(m_pImageView, TRUE);
	}

	scrolled_window = gtk_scrolled_window_new();
	
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),GTK_POLICY_AUTOMATIC,GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled_window),m_pIconView);
	
	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL,0);
	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL,0);
	
	gtk_box_append (GTK_BOX (hbox), m_pLocationEntry);
	//gtk_box_append (GTK_BOX (hbox), hscale);
	gtk_widget_set_hexpand(hbox, TRUE);
	gtk_widget_set_vexpand(vbox, TRUE);
	gtk_widget_set_hexpand(scrolled_window, TRUE);
	gtk_widget_set_vexpand(scrolled_window, TRUE);
	gtk_box_append (GTK_BOX (vbox), hbox);
	gtk_box_append (GTK_BOX (vbox), scrolled_window);
	gtk_widget_set_vexpand(m_pImageView, TRUE);
	
	gtk_paned_set_start_child(GTK_PANED(vpaned),m_pNotebook);
	gtk_paned_set_end_child(GTK_PANED(vpaned),m_pImageView);
	
	gtk_paned_set_start_child(GTK_PANED(hpaned),vpaned);
	gtk_paned_set_end_child(GTK_PANED(hpaned),vbox);

	// in GTK4, paned children must be explicitly allowed to resize/shrink
	gtk_paned_set_resize_start_child(GTK_PANED(vpaned), TRUE);
	gtk_paned_set_resize_end_child(GTK_PANED(vpaned), TRUE);
	gtk_paned_set_shrink_start_child(GTK_PANED(vpaned), TRUE);
	gtk_paned_set_shrink_end_child(GTK_PANED(vpaned), TRUE);
	gtk_paned_set_resize_start_child(GTK_PANED(hpaned), TRUE);
	gtk_paned_set_resize_end_child(GTK_PANED(hpaned), TRUE);
	gtk_paned_set_shrink_start_child(GTK_PANED(hpaned), TRUE);
	gtk_paned_set_shrink_end_child(GTK_PANED(hpaned), TRUE);
	
	gtk_widget_set_hexpand(vpaned, TRUE);
	gtk_widget_set_vexpand(vpaned, TRUE);
	gtk_widget_set_hexpand(vbox, TRUE);
	gtk_widget_set_vexpand(vbox, TRUE);
	
	// set the size of the hpane and vpane
	int hpaned_pos = prefsPtr->GetInteger(QUIVER_PREFS_BROWSER,QUIVER_PREFS_BROWSER_FOLDER_HPANE,200);
	gtk_paned_set_position(GTK_PANED(hpaned),hpaned_pos);

	int vpaned_pos = prefsPtr->GetInteger(QUIVER_PREFS_BROWSER,QUIVER_PREFS_BROWSER_FOLDER_VPANE,300);
	gtk_paned_set_position(GTK_PANED(vpaned),vpaned_pos);
	
	
	g_signal_connect (G_OBJECT (hpaned), "notify::position",
	      G_CALLBACK (pane_position_changed), this);
	g_signal_connect (G_OBJECT (vpaned), "notify::position",
	      G_CALLBACK (pane_position_changed), this);
	
	m_pBrowserWidget = hpaned;
	
	m_pSWFolderTree = gtk_scrolled_window_new();
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(m_pSWFolderTree),GTK_POLICY_AUTOMATIC,GTK_POLICY_AUTOMATIC);
	GtkWidget *pFolderTree = m_FolderTreePtr->GetWidget();
	
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(m_pSWFolderTree),pFolderTree);
	gtk_widget_set_visible(m_pSWFolderTree, TRUE);
	gtk_notebook_append_page(GTK_NOTEBOOK(m_pNotebook), m_pSWFolderTree,gtk_label_new("Folders"));	
	gtk_widget_set_visible(m_pNotebook, TRUE);

	if (!prefsPtr->GetBoolean(QUIVER_PREFS_BROWSER,QUIVER_PREFS_BROWSER_FOLDERTREE_SHOW,true))
	{	
		gtk_widget_set_visible(vpaned, FALSE);
	}

	gtk_notebook_popup_enable(GTK_NOTEBOOK(m_pNotebook));
	gtk_notebook_set_scrollable (GTK_NOTEBOOK(m_pNotebook),TRUE);

	
/*	
	quiver_icon_view_set_text_func(QUIVER_ICON_VIEW(real_iconview),(QuiverIconViewGetTextFunc)text_callback,user_data,NULL);
*/
	quiver_image_view_set_enable_transitions(QUIVER_IMAGE_VIEW(m_pImageView), true);
	quiver_image_view_set_magnification_mode(QUIVER_IMAGE_VIEW(m_pImageView),QUIVER_IMAGE_VIEW_MAGNIFICATION_MODE_SMOOTH);

    g_signal_connect (G_OBJECT (m_pImageView), "magnification-changed",
    			G_CALLBACK (browser_imageview_magnification_changed), this);
	
    g_signal_connect (G_OBJECT (m_pImageView), "reload",
    			G_CALLBACK (browser_imageview_reload), this);

	//popup menu stuff
	{
		GtkGesture *gesture = gtk_gesture_click_new();
		gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), 0);
		g_signal_connect(gesture, "pressed", G_CALLBACK(browser_button_press_cb), this);
		gtk_widget_add_controller(m_pImageView, GTK_EVENT_CONTROLLER(gesture));
	}
	//g_signal_connect(G_OBJECT(m_pImageView), "popup-menu", G_CALLBACK(browser_popup_menu_cb), this);

	quiver_icon_view_set_scroll_type(QUIVER_ICON_VIEW(m_pIconView),QUIVER_ICON_VIEW_SCROLL_SMOOTH);
	quiver_icon_view_set_n_items_func(QUIVER_ICON_VIEW(m_pIconView),(QuiverIconViewGetNItemsFunc)n_cells_callback,this,NULL);
	quiver_icon_view_set_thumbnail_pixbuf_func(QUIVER_ICON_VIEW(m_pIconView),(QuiverIconViewGetThumbnailPixbufFunc)thumbnail_pixbuf_callback,this,NULL);
	quiver_icon_view_set_icon_pixbuf_func(QUIVER_ICON_VIEW(m_pIconView),(QuiverIconViewGetIconPixbufFunc)icon_pixbuf_callback,this,NULL);
	quiver_icon_view_set_text_func(QUIVER_ICON_VIEW(m_pIconView),(QuiverIconViewGetTextFunc)text_pixbuf_callback,this,NULL);

	g_signal_connect (G_OBJECT (hscale), "value_changed",
	      G_CALLBACK (icon_size_value_changed), this);

	g_signal_connect(G_OBJECT(m_pIconView),"cell_activated",G_CALLBACK(iconview_cell_activated_cb),this);
	g_signal_connect(G_OBJECT(m_pIconView),"cursor_changed",G_CALLBACK(iconview_cursor_changed_cb),this);
	g_signal_connect(G_OBJECT(m_pIconView),"selection_changed",G_CALLBACK(iconview_selection_changed_cb),this);

	// popup menu stuff
	{
		GtkGesture *gesture = gtk_gesture_click_new();
		gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), 0);
		g_signal_connect(gesture, "pressed", G_CALLBACK(browser_button_press_cb), this);
		gtk_widget_add_controller(m_pIconView, GTK_EVENT_CONTROLLER(gesture));
	}
	{
		GtkEventController *motion = gtk_event_controller_motion_new();
		g_signal_connect(motion, "motion", G_CALLBACK(iconview_motion_notify), this);
		gtk_widget_add_controller(m_pIconView, motion);
	}
	g_signal_connect(G_OBJECT(m_pLocationEntry),"activate",G_CALLBACK(entry_activate),this);
	{
		GtkEventController *key_ctrl = gtk_event_controller_key_new();
		g_signal_connect(key_ctrl, "key-pressed", G_CALLBACK(entry_key_press), this);
		gtk_widget_add_controller(m_pLocationEntry, key_ctrl);
	}


	{
		GtkEventController *focus_ctrl = gtk_event_controller_focus_new();
		g_signal_connect(focus_ctrl, "enter", G_CALLBACK(entry_focus_in), this);
		gtk_widget_add_controller(m_pLocationEntry, focus_ctrl);
	}
	{
		GtkEventController *focus_ctrl = gtk_event_controller_focus_new();
		g_signal_connect(focus_ctrl, "leave", G_CALLBACK(entry_focus_out), this);
		gtk_widget_add_controller(m_pLocationEntry, focus_ctrl);
	}

	string strBGColorImg   = prefsPtr->GetString(QUIVER_PREFS_APP,QUIVER_PREFS_APP_BG_IMAGEVIEW, "#000");
	string strBGColorThumb = prefsPtr->GetString(QUIVER_PREFS_APP,QUIVER_PREFS_APP_BG_ICONVIEW, "#444");

	if (!prefsPtr->GetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_USE_THEME_COLOR,true))
	{
		std::string strCSS =  "QuiverIconView { background-color:" + strBGColorThumb + ";}\n";
		strCSS += "QuiverImageView { background-color:" + strBGColorImg + ";}\n";
		gtk_css_provider_load_from_string(m_pCssProvider, strCSS.c_str());
		gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(m_pCssProvider), GTK_STYLE_PROVIDER_PRIORITY_THEME);
	}

	quiver_icon_view_set_overlay_pixbuf_func(QUIVER_ICON_VIEW(m_pIconView),(QuiverIconViewGetOverlayPixbufFunc)overlay_pixbuf_callback,this,NULL);

	IPixbufLoaderObserverPtr tmp ( new ImageViewPixbufLoaderObserver(QUIVER_IMAGE_VIEW(m_pImageView)) );
	m_ImageViewPixbufLoaderObserverPtr = tmp;
	m_ImageLoader.AddPixbufLoaderObserver(m_ImageViewPixbufLoaderObserverPtr.get());

	gtk_widget_set_visible(m_pBrowserWidget, TRUE);
	gtk_widget_set_visible(m_pBrowserWidget, FALSE);

	// build context menu popover (follows FolderTree.cpp pattern)
	m_pContextMenuPopover = gtk_popover_new();
	{
		GtkWidget* menu_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

		GtkWidget* menuitem = gtk_button_new_with_label("Copy");
		gtk_widget_set_halign(menuitem, GTK_ALIGN_FILL);
		gtk_actionable_set_action_name(GTK_ACTIONABLE(menuitem), "quiver." ACTION_BROWSER_COPY);
		gtk_box_append(GTK_BOX(menu_box), menuitem);

		menuitem = gtk_button_new_with_label("Move To Trash");
		gtk_widget_set_halign(menuitem, GTK_ALIGN_FILL);
		gtk_actionable_set_action_name(GTK_ACTIONABLE(menuitem), "quiver." ACTION_BROWSER_TRASH);
		gtk_box_append(GTK_BOX(menu_box), menuitem);

		gtk_popover_set_child(GTK_POPOVER(m_pContextMenuPopover), menu_box);
	}
	gtk_widget_insert_action_group(m_pContextMenuPopover, "quiver", G_ACTION_GROUP(QuiverUtils::GetActionGroup()));


	gdouble thumb_size = (gdouble)prefsPtr->GetInteger(QUIVER_PREFS_BROWSER,QUIVER_PREFS_BROWSER_THUMB_SIZE);	

	if (thumb_size < 20. || 256. < thumb_size)
	{
		thumb_size = 128.;
	}
	gtk_range_set_value(GTK_RANGE(hscale),thumb_size);

}

Browser::BrowserImpl::~BrowserImpl()
{
	m_ImageLoader.RemovePixbufLoaderObserver(m_StatusbarPtr.get());
	m_ImageLoader.RemovePixbufLoaderObserver(m_ImageViewPixbufLoaderObserverPtr.get());
	
	PreferencesPtr prefsPtr = Preferences::GetInstance();
	gdouble value = gtk_range_get_value (GTK_RANGE(hscale));

	gint val;

	val = (int)value;

	prefsPtr->SetInteger(QUIVER_PREFS_BROWSER,QUIVER_PREFS_BROWSER_THUMB_SIZE,val);

	prefsPtr->RemoveEventHandler( m_PreferencesEventHandlerPtr );
	m_ImageListPtr->RemoveEventHandler(m_ImageListEventHandlerPtr);

	/* The icon view (and its folder-tree sibling, notebook and preview pane)
	 * live inside m_pBrowserWidget, which is owned by the window tree.  The
	 * window destroy at the end of ~QuiverImpl runs AFTER BrowserImpl has
	 * been freed, and unmapping the window there fires the icon view's
	 * leave/unmap controller, which calls back into this C++ object
	 * (n_cells_callback -> freed BrowserImpl) and crashes.
	 *
	 * The whole widget subtree must therefore be unparented HERE, while `this`
	 * is still alive, so the widgets are torn down before BrowserImpl is. */

	/* disconnect signal handlers on the notebook while it still exists */
	g_signal_handlers_disconnect_matched(
		m_pNotebook,
		G_SIGNAL_MATCH_DATA,
		0,
		0,
		NULL,
		NULL,
		this);

	/* cancel the icon-view refresh timer before the widgets go away */
	if (0 != m_iTimeoutUpdateListID)
	{
		g_source_remove(m_iTimeoutUpdateListID);
		m_iTimeoutUpdateListID = 0;
	}

	/* 1. the context-menu popover is parented to the icon view on demand;
	 *    unparent it before the icon view is destroyed */
	if (m_pContextMenuPopover)
	{
		if (gtk_widget_get_parent(m_pContextMenuPopover))
		{
			gtk_widget_unparent(m_pContextMenuPopover);
		}
		m_pContextMenuPopover = NULL;
	}

	/* 2. tear down the FolderTree component while its widget subtree is still
	 *    alive, so ~FolderTreeImpl can unparent its own menu popover */
	m_FolderTreePtr.reset();

	/* 3. the thumb-sizer floats between the app toolbar and the browser; it is
	 *    NOT part of m_pBrowserWidget, release it while both still exist */
	if (m_pToolItemThumbSizer)
	{
		if (gtk_widget_get_parent(m_pToolItemThumbSizer))
		{
			gtk_widget_unparent(m_pToolItemThumbSizer);
		}
		g_object_unref(m_pToolItemThumbSizer);
		m_pToolItemThumbSizer = NULL;
	}

	/* 4. unparent the whole browser widget subtree: since it is owned through
	 *    the window tree, this destroys it (icon view, folder tree scrolled
	 *    window, notebook and preview pane) while `this` is still valid */
	if (m_pBrowserWidget && gtk_widget_get_parent(m_pBrowserWidget))
	{
		gtk_widget_unparent(m_pBrowserWidget);
	}
	m_pBrowserWidget = NULL;
	m_pSWFolderTree = NULL;

	gtk_style_context_remove_provider_for_display(gdk_display_get_default(),GTK_STYLE_PROVIDER(m_pCssProvider));
	g_object_unref(m_pCssProvider);

}

void Browser::BrowserImpl::RegisterActions()
{
	PreferencesPtr prefsPtr = Preferences::GetInstance();

	/* Browser simple actions */
	QuiverUtils::AddSimpleAction(ACTION_BROWSER_OPEN_LOCATION, "<Control>l", browser_action_handler_cb, this);
	QuiverUtils::AddSimpleAction(ACTION_BROWSER_HISTORY_BACK, "<Alt>Left", browser_action_handler_cb, this);
	QuiverUtils::AddSimpleAction(ACTION_BROWSER_HISTORY_FORWARD, "<Alt>Right", browser_action_handler_cb, this);

	QuiverUtils::AddSimpleAction(ACTION_BROWSER_CUT, "<Control>X", browser_action_handler_cb, this);
	QuiverUtils::AddSimpleAction(ACTION_BROWSER_COPY, "<Control>C", browser_action_handler_cb, this);
	QuiverUtils::AddSimpleAction(ACTION_BROWSER_PASTE, "<Control>V", browser_action_handler_cb, this);
	QuiverUtils::AddSimpleAction(ACTION_BROWSER_SELECT_ALL, "<Control>A", browser_action_handler_cb, this);
	QuiverUtils::AddSimpleAction(ACTION_BROWSER_TRASH, "Delete", browser_action_handler_cb, this);
	QuiverUtils::AddSimpleAction(ACTION_BROWSER_RELOAD, "<Control>R", browser_action_handler_cb, this);
	/* Browser toggle actions */
	QuiverUtils::AddToggleAction(ACTION_BROWSER_VIEW_SIDEBAR, "<Control><Shift>F", TRUE, browser_action_handler_cb, this);
	QuiverUtils::AddToggleAction(ACTION_BROWSER_VIEW_PREVIEW, "<Control><Shift>p", TRUE, browser_action_handler_cb, this);

	/* initial toggle state from preferences */
	bool bShowPreview = prefsPtr->GetBoolean(QUIVER_PREFS_BROWSER,QUIVER_PREFS_BROWSER_PREVIEW_SHOW);
	QuiverUtils::ToggleActionSetActive(ACTION_BROWSER_VIEW_PREVIEW, bShowPreview ? TRUE : FALSE);

	bool bShowFolderTree = prefsPtr->GetBoolean(QUIVER_PREFS_BROWSER,QUIVER_PREFS_BROWSER_FOLDERTREE_SHOW);
	QuiverUtils::ToggleActionSetActive(ACTION_BROWSER_VIEW_SIDEBAR, bShowFolderTree ? TRUE : FALSE);
}

void Browser::BrowserImpl::SetToolbar(GtkWidget *toolbar)
{
	m_pToolbar = toolbar;
}

void Browser::BrowserImpl::UpdateUI()
{	
	PreferencesPtr prefsPtr = Preferences::GetInstance();
	GAction* action;
	action = QuiverUtils::GetAction(ACTION_BROWSER_HISTORY_FORWARD);
	if (NULL != action)
	{
		g_simple_action_set_enabled(G_SIMPLE_ACTION(action),m_BrowserHistory.CanGoForward() ? TRUE : FALSE);
	}
	action = QuiverUtils::GetAction(ACTION_BROWSER_HISTORY_BACK);
	if (NULL != action)
	{
		g_simple_action_set_enabled(G_SIMPLE_ACTION(action),m_BrowserHistory.CanGoBack() ? TRUE : FALSE);
	}

	bool bFullscreen = prefsPtr->GetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_WINDOW_FULLSCREEN);
	if (bFullscreen)
	{
		if (prefsPtr->GetBoolean(QUIVER_PREFS_BROWSER,QUIVER_PREFS_BROWSER_FOLDERTREE_SHOW,true))
		{	
			if (prefsPtr->GetBoolean(QUIVER_PREFS_BROWSER,QUIVER_PREFS_BROWSER_FOLDERTREE_HIDE_FS,true))
			{
				QuiverUtils::ToggleActionSetActive(ACTION_BROWSER_VIEW_SIDEBAR, FALSE);
			}
		}
	}
	else
	{
		if (prefsPtr->GetBoolean(QUIVER_PREFS_BROWSER,QUIVER_PREFS_BROWSER_FOLDERTREE_SHOW,true))
		{	
			if (prefsPtr->GetBoolean(QUIVER_PREFS_BROWSER,QUIVER_PREFS_BROWSER_FOLDERTREE_HIDE_FS,true))
			{
				QuiverUtils::ToggleActionSetActive(ACTION_BROWSER_VIEW_SIDEBAR, TRUE);
			}
		}
	}
}

void Browser::BrowserImpl::Show()
{
	if (NULL != m_pToolbar && NULL == gtk_widget_get_parent(GTK_WIDGET(m_pToolItemThumbSizer)))
	{
		gtk_box_append(GTK_BOX(m_pToolbar), m_pToolItemThumbSizer);
	}

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
	if (NULL != m_pToolbar && NULL != gtk_widget_get_parent(GTK_WIDGET(m_pToolItemThumbSizer)))
	{
		gtk_box_remove(GTK_BOX(m_pToolbar), m_pToolItemThumbSizer);
	}
	
	m_ImageListPtr->BlockHandler(m_ImageListEventHandlerPtr);
}

void Browser::BrowserImpl::SetImageList(ImageListPtr imglist)
{
	m_ImageListPtr->RemoveEventHandler(m_ImageListEventHandlerPtr);
	
	m_ImageListPtr = imglist;
	
	m_ImageListPtr->AddEventHandler(m_ImageListEventHandlerPtr);
	
	if (FALSE == gtk_widget_get_visible(m_pBrowserWidget))
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
	
	UpdateUI();
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
		
		if (gtk_widget_get_mapped(m_pImageView))
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
		QuiverFile f;
		m_QuiverFileCurrent = f;
	}
	
	// update the toolbar / menu buttons - (un)set sensitive 
	//UpdateUI();
}


ImageListPtr Browser::BrowserImpl::GetImageList()
{
	return m_ImageListPtr;
}


//=============================================================================
// BrowswerImpl Callbacks
//=============================================================================

static void icon_size_value_changed (GtkRange *range,gpointer  user_data)
{
	Browser::BrowserImpl* b = (Browser::BrowserImpl*)user_data;
	gdouble value = gtk_range_get_value (range);
	quiver_icon_view_set_icon_size(QUIVER_ICON_VIEW(b->m_pIconView), (gint)value,(gint)value);
}

static gulong n_cells_callback(QuiverIconView *iconview, gpointer user_data)
{ (void)iconview; 
	Browser::BrowserImpl* b = (Browser::BrowserImpl*)user_data;
	return b->m_ImageListPtr->GetSize();
}

static GdkPixbuf* icon_pixbuf_callback(QuiverIconView *iconview, gulong cell,gpointer user_data)
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
		g_snprintf(cache_icon_name,256,"%s%d-%d",icon_name,width,height);
		pixbuf = b->m_IconCache.GetPixbuf(cache_icon_name);
		if (NULL == pixbuf)
		{
			pixbuf = f.GetIcon(width,height);
			if (NULL != pixbuf)
			{
				b->m_IconCache.AddPixbuf(cache_icon_name,pixbuf);
			}
		}
		g_free(icon_name);
	}

	return pixbuf;
}

static gchar* text_pixbuf_callback(QuiverIconView *iconview, gulong cell,gpointer user_data)
{ (void)iconview;
	Browser::BrowserImpl* b = (Browser::BrowserImpl*)user_data;

	if (cell >= b->m_ImageListPtr->GetSize())
		return NULL;

	QuiverFile f = (*b->m_ImageListPtr)[cell];
	if (!f.IsFolder())
		return NULL;

	const gchar* uri = f.GetURI();
	gchar* path = g_filename_from_uri(uri,NULL,NULL);
	gchar* name;
	if (path)
	{
		name = g_filename_display_basename(path);
		g_free(path);
	}
	else
	{
		name = g_strdup("");
	}
	return name;
}

static gboolean thumbnail_loader_update_list (gpointer data)
{
	Browser::BrowserImpl* b = (Browser::BrowserImpl*)data;
	b->m_ThumbnailLoader.UpdateList();
	b->m_iTimeoutUpdateListID = 0;
	return FALSE;
}

void Browser::BrowserImpl::QueueIconViewUpdate(int timeout)
{
	if (!m_iTimeoutUpdateListID)
	{
		m_iTimeoutUpdateListID = g_timeout_add(timeout,thumbnail_loader_update_list,this);
	}
}


static GdkPixbuf* thumbnail_pixbuf_callback(QuiverIconView *iconview, gulong cell, gint* actual_width, gint* actual_height, gpointer user_data)
{
	Browser::BrowserImpl* b = (Browser::BrowserImpl*)user_data;

	GdkPixbuf *pixbuf = NULL;
	gboolean need_new_thumb = TRUE;
	
	guint width, height;
	quiver_icon_view_get_icon_size(iconview,&width,&height);

	QuiverFile f = (*b->m_ImageListPtr)[cell];

	if (f.IsFolder())
	{
		gint x = 0, y = 0;
		quiver_icon_view_get_cell_mouse_position(iconview, cell, &x, &y);

		if (0 <= x && 0 <= y && x < gint(width) && y < gint(height))
		{
			double percent = double(x) / width;
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
				if (4 < (*lstPtr)[index].GetOrientation())
				{
					swap(*actual_width,*actual_height);
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
				swap(*actual_width,*actual_height);
			}

			guint thumb_width, thumb_height;
			thumb_width = gdk_pixbuf_get_width(pixbuf);
			thumb_height = gdk_pixbuf_get_height(pixbuf);

			guint bound_width, bound_height;
			bound_width = *actual_width;
			bound_height = *actual_height;
			quiver_rect_get_bound_size(width,height, &bound_width,&bound_height,FALSE);

			if (bound_width == thumb_width && bound_height == thumb_height)
			{
				need_new_thumb = FALSE;
			}
		}
	}
	
	if (need_new_thumb)
	{
		// add a timeout
		b->QueueIconViewUpdate();
	}
	
	return pixbuf;
}

static GdkPixbuf* overlay_pixbuf_callback(QuiverIconView* iconview, gulong cell, QuiverIconOverlayType type, gpointer user_data)
{ (void)iconview; 
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
				pixbuf = f.GetIcon(32,32);
				if (NULL != pixbuf)
				{
					b->m_IconOverlayCache.AddPixbuf(icon_name,pixbuf);
				}
			}
			g_free(icon_name);
		}
	}
	else if (type == QUIVER_ICON_OVERLAY_LINK && f.IsVideo())
	{
		// show the default icon for the video mime-type, like folders do
		gchar* icon_name = f.GetIconName();
		if (icon_name)
		{
			pixbuf = b->m_IconOverlayCache.GetPixbuf(icon_name);
			if (NULL == pixbuf)
			{
				pixbuf = f.GetIcon(32,32);
				if (NULL != pixbuf)
				{
					b->m_IconOverlayCache.AddPixbuf(icon_name,pixbuf);
				}
			}
			g_free(icon_name);
		}
	}

	return pixbuf;
}

static void iconview_cell_activated_cb(QuiverIconView *iconview, guint cell, gpointer user_data)
{ (void)cell;  (void)iconview; 
	Browser::BrowserImpl* b = (Browser::BrowserImpl*)user_data;
	b->m_BrowserParent->EmitItemActivatedEvent();
}

static void iconview_cursor_changed_cb(QuiverIconView *iconview, guint cell, gpointer user_data)
{ (void)iconview; 
	Browser::BrowserImpl* b = (Browser::BrowserImpl*)user_data;
	
	b->SetImageIndex(cell,true,true);
}

static void iconview_selection_changed_cb(QuiverIconView *iconview, gpointer user_data)
{
	Browser::BrowserImpl* b = (Browser::BrowserImpl*)user_data;

	GAction *action = QuiverUtils::GetAction(ACTION_BROWSER_TRASH);
	if (NULL != action && G_IS_SIMPLE_ACTION(action))
	{
		GList *selection;
		selection = quiver_icon_view_get_selection(iconview);
		if (NULL == selection)
		{
			g_simple_action_set_enabled(G_SIMPLE_ACTION(action),FALSE);
		}
		else
		{
			g_simple_action_set_enabled(G_SIMPLE_ACTION(action),TRUE);
			g_list_free(selection);
		}
	}
	b->m_BrowserParent->EmitSelectionChangedEvent();
}

static void iconview_motion_notify(GtkEventControllerMotion *controller, gdouble x, gdouble y, gpointer user_data)
{
	Browser::BrowserImpl* b = (Browser::BrowserImpl*)user_data;
	QuiverIconView *iconview = QUIVER_ICON_VIEW(gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(controller)));

	gint ix = (gint)x;
	gint iy = (gint)y;

	gulong cell =  quiver_icon_view_get_cell_for_xy(iconview, ix, iy);

	if (G_MAXULONG == cell)
		return;

	guint width, height;
	quiver_icon_view_get_icon_size(iconview,&width,&height);

	QuiverFile f = (*b->m_ImageListPtr)[cell];

	bool bClearMap = true;
	if (f.IsFolder())
	{
		gint x = 0, y = 0;
		quiver_icon_view_get_cell_mouse_position(iconview, cell, &x, &y);

		if (0 <= x && 0 <= y && x < (gint)width && y < (gint)height)
		{
			double percent = double(x) / width;
			ImageListPtr lstPtr(new ImageList());
			lstPtr->SetImageList(f.GetURI());
			unsigned int listSize = lstPtr->GetSize();
			if (0 != listSize)
			{
				unsigned int index = (unsigned int)(listSize * percent);
				index = std::min(index, listSize - 1);

				QuiverFile child = (*lstPtr)[index];

				std::string uri_old = b->m_mapFolderToFile[f.GetURI()];
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


static gboolean
entry_key_press (GtkEventControllerKey *controller, guint keyval, guint keycode, GdkModifierType state, gpointer user_data)
{ (void)controller; (void)state; (void)user_data; (void)keycode;
 GtkWidget *widget = GTK_WIDGET(gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(controller)));
	switch(keyval)
	{
		case GDK_KEY_Escape:
			gtk_widget_set_visible(widget, FALSE);
			break;
	}
	return FALSE;
}

static void 
browser_button_press_cb(GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data)
{ (void)n_press; (void)x; (void)y;
	GtkWidget *widget = GTK_WIDGET(gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture)));
	guint button = gtk_gesture_single_get_button(GTK_GESTURE_SINGLE(gesture));
	if (3 == button)
	{
		browser_show_context_menu(widget, x, y, user_data);
	}
}

static void browser_show_context_menu(GtkWidget *widget, gdouble x, gdouble y, gpointer userdata)
{
	Browser::BrowserImpl *pBrowserImpl = (Browser::BrowserImpl*)userdata;
	(void)widget;

	if (NULL != pBrowserImpl->m_pContextMenuPopover)
	{
		if (x >= 0 && y >= 0)
		{
			GdkRectangle rect;
			rect.x = (int)x;
			rect.y = (int)y;
			rect.width = 1;
			rect.height = 1;
			gtk_popover_set_pointing_to(GTK_POPOVER(pBrowserImpl->m_pContextMenuPopover), &rect);
		}
		gtk_popover_popup(GTK_POPOVER(pBrowserImpl->m_pContextMenuPopover));
	}
}



static void entry_activate(GtkEntry *entry, gpointer user_data)
{
	Browser::BrowserImpl* b = (Browser::BrowserImpl*)user_data;
	string entry_text = gtk_editable_get_text(GTK_EDITABLE(entry));
	list<string> file_list;
	file_list.push_back(entry_text);
	b->m_ImageListPtr->SetImageList(&file_list);
	
}

static void browser_imageview_magnification_changed(QuiverImageView *imageview,gpointer data)
{ (void)imageview; 
	Browser::BrowserImpl* pBrowserImpl = (Browser::BrowserImpl*)data;
	
	double mag = quiver_image_view_get_magnification(QUIVER_IMAGE_VIEW(pBrowserImpl->m_pImageView));
	pBrowserImpl->m_StatusbarPtr->SetMagnification((int)(mag*100+.5));
}

static void browser_imageview_reload(QuiverImageView *imageview,gpointer data)
{ (void)imageview; 
	//printf("#### got a reload message from the imageview\n");
	Browser::BrowserImpl* pBrowserImpl = (Browser::BrowserImpl*)data;

	if (!pBrowserImpl->m_ImageListPtr->GetSize())
		return;

	ImageLoader::LoadParams params = {};

	params.orientation = pBrowserImpl->m_ImageListPtr->GetCurrent().GetOrientation();
	params.reload = true;
	params.fullsize = true;
	params.no_thumb_preview = true;
	params.state = ImageLoader::LOAD;

	pBrowserImpl->m_ImageLoader.LoadImage(pBrowserImpl->m_ImageListPtr->GetCurrent(),params);
}

static void browser_action_handler_cb(GSimpleAction *action, GVariant *parameter, gpointer data)
{ (void)parameter; 
	Browser::BrowserImpl* pBrowserImpl;
	pBrowserImpl = (Browser::BrowserImpl*)data;
	PreferencesPtr prefsPtr = Preferences::GetInstance();
	
	//printf("Browser Action: %s\n",g_action_get_name(G_ACTION(action)));
	
	const gchar * szAction = g_action_get_name(G_ACTION(action));
	
	if (0 == strcmp(szAction,ACTION_BROWSER_RELOAD))
	{
		// clear the caches
		pBrowserImpl->m_ImageListPtr->Reload();

		pBrowserImpl->m_ThumbnailCache.Clear();
			
		pBrowserImpl->m_ThumbnailLoader.UpdateList(true);
		
	}
	else if (0 == strcmp(szAction,ACTION_BROWSER_VIEW_SIDEBAR))
	{
		if( QuiverUtils::ToggleActionGetActive(szAction) )
		{
			bool bFullscreen = prefsPtr->GetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_WINDOW_FULLSCREEN);
			if (!bFullscreen)
			{
				prefsPtr->SetBoolean(QUIVER_PREFS_BROWSER,QUIVER_PREFS_BROWSER_FOLDERTREE_SHOW,true);
			}
		}
		else
		{
			gtk_widget_set_visible(pBrowserImpl->vpaned, FALSE);
			bool bFullscreen = prefsPtr->GetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_WINDOW_FULLSCREEN);
			if (!bFullscreen)
			{
				prefsPtr->SetBoolean(QUIVER_PREFS_BROWSER,QUIVER_PREFS_BROWSER_FOLDERTREE_SHOW,false);
			}
		}
	}
	else if (0 == strcmp(szAction,ACTION_BROWSER_VIEW_PREVIEW))
	{
		if( QuiverUtils::ToggleActionGetActive(szAction) )
		{
			prefsPtr->SetBoolean(QUIVER_PREFS_BROWSER,QUIVER_PREFS_BROWSER_PREVIEW_SHOW,true);
		}
		else
		{
			gtk_widget_set_visible(pBrowserImpl->m_pImageView, FALSE);	
			prefsPtr->SetBoolean(QUIVER_PREFS_BROWSER,QUIVER_PREFS_BROWSER_PREVIEW_SHOW,false);
		}
	}
	else if (0 == strcmp(szAction,ACTION_BROWSER_OPEN_LOCATION))
	{
		gtk_widget_grab_focus(pBrowserImpl->m_pLocationEntry);
	}
	else if (0 == strcmp(szAction,ACTION_BROWSER_HISTORY_BACK))
	{
		if (pBrowserImpl->m_BrowserHistory.GoBack())
		{
			pBrowserImpl->m_bBrowserHistoryEvent = true;
			const list<string>& files = pBrowserImpl->m_BrowserHistory.GetCurrentFiles();
			string selected = pBrowserImpl->m_BrowserHistory.GetCurrentSelected();
			pBrowserImpl->m_ImageListPtr->SetImageList(&files);
			pBrowserImpl->m_ImageListPtr->SetCurrentFile(selected);
			pBrowserImpl->m_bBrowserHistoryEvent = false;
		}
	}
	else if (0 == strcmp(szAction,ACTION_BROWSER_HISTORY_FORWARD))
	{
		pBrowserImpl->m_bBrowserHistoryEvent = true;
		if (pBrowserImpl->m_BrowserHistory.GoForward())
		{
			pBrowserImpl->m_bBrowserHistoryEvent = true;
			const list<string>& files = pBrowserImpl->m_BrowserHistory.GetCurrentFiles();
			string selected = pBrowserImpl->m_BrowserHistory.GetCurrentSelected();
			pBrowserImpl->m_ImageListPtr->SetImageList(&files);
			pBrowserImpl->m_ImageListPtr->SetCurrentFile(selected);
			pBrowserImpl->m_bBrowserHistoryEvent = false;
		}
		pBrowserImpl->m_bBrowserHistoryEvent = false;
	}	
	else if (0 == strcmp(szAction,ACTION_BROWSER_COPY))
	{
		GdkClipboard* clipboard = gdk_display_get_clipboard(gdk_display_get_default());
		
		string strClipText;
		
		GList *selection;
		selection = quiver_icon_view_get_selection(QUIVER_ICON_VIEW(pBrowserImpl->m_pIconView));
	
		if (NULL != selection)
		{
			// delete the items!
			GList *sel_itr = selection;

			while (NULL != sel_itr)
			{
				int item = (uintptr_t)sel_itr->data;
				QuiverFile f = (*pBrowserImpl->m_ImageListPtr)[item];
				if (!strClipText.empty())
				{
					strClipText += "\n";
				}
				strClipText += f.GetURI();
				sel_itr = g_list_next(sel_itr);
			}
			g_list_free(selection);

			gdk_clipboard_set_text (clipboard, strClipText.c_str());

		}
			
		

			
		
	}
	else if (0 == strcmp(szAction, ACTION_BROWSER_TRASH))
	{
		gint rval = GTK_RESPONSE_NO;
		GList *selection;
		selection = quiver_icon_view_get_selection(QUIVER_ICON_VIEW(pBrowserImpl->m_pIconView));
		set<int> items;
		if (NULL == selection)
		{
			// nothing to delete!
		}
		else
		{
			// delete the items!
			GList *sel_itr = selection;
			
			while (NULL != sel_itr)
			{
				items.insert((uintptr_t)sel_itr->data);
				sel_itr = g_list_next(sel_itr);
			}
			g_list_free(selection);
		}

		if (0 != items.size())
		{
			string strDlgText;
			if (1 == items.size())
			{
			strDlgText = "Move the selected image to the trash?";
			}
			else
			{
			strDlgText = "Move the selected images to the trash?";
			}
		GtkWidget* dialog = gtk_window_new();
		gtk_window_set_title(GTK_WINDOW(dialog), "Confirm");
		gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
		gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);
		GtkWidget* dlgBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
		GtkWidget* dlgLabel = gtk_label_new(strDlgText.c_str());
		gtk_label_set_wrap(GTK_LABEL(dlgLabel), TRUE);
		gtk_widget_set_margin_start(dlgLabel, 12);
		gtk_widget_set_margin_end(dlgLabel, 12);
		gtk_widget_set_margin_top(dlgLabel, 12);
		gtk_widget_set_margin_bottom(dlgLabel, 4);
		GtkWidget* btnBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
		gtk_widget_set_halign(btnBox, GTK_ALIGN_END);
		gtk_widget_set_margin_start(btnBox, 12);
		gtk_widget_set_margin_end(btnBox, 12);
		gtk_widget_set_margin_bottom(btnBox, 12);
		gtk_box_append(GTK_BOX(dlgBox), dlgLabel);
		gtk_box_append(GTK_BOX(dlgBox), btnBox);
		gtk_window_set_child(GTK_WINDOW(dialog), dlgBox);
		GtkWidget* btnNo = gtk_button_new_with_label("No");
		GtkWidget* btnYes = gtk_button_new_with_label("Yes");
		gtk_box_append(GTK_BOX(btnBox), btnNo);
		gtk_box_append(GTK_BOX(btnBox), btnYes);
			{
				GMainLoop *loop = g_main_loop_new(NULL, FALSE);
				struct { gint resp; GMainLoop *loop; } d = { GTK_RESPONSE_NO, loop };
				gulong no_handler = g_signal_connect(btnNo, "clicked",
					G_CALLBACK(+[](GtkWidget*, gpointer user_data) {
						auto *d2 = (decltype(&d))user_data;
						d2->resp = GTK_RESPONSE_NO;
						g_main_loop_quit(d2->loop);
					}), &d);
				gulong yes_handler = g_signal_connect(btnYes, "clicked",
					G_CALLBACK(+[](GtkWidget*, gpointer user_data) {
						auto *d2 = (decltype(&d))user_data;
						d2->resp = GTK_RESPONSE_YES;
						g_main_loop_quit(d2->loop);
					}), &d);
				gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
				gtk_window_present(GTK_WINDOW(dialog));
				g_main_loop_run(loop);
				g_signal_handler_disconnect(btnNo, no_handler);
				g_signal_handler_disconnect(btnYes, yes_handler);
				rval = d.resp;
				g_main_loop_unref(loop);
			}
			gtk_window_destroy(GTK_WINDOW(dialog));
		}

		switch (rval)
		{
			case GTK_RESPONSE_YES:
			{
				set<int>::reverse_iterator ritr;
				
				pBrowserImpl->m_ImageListPtr->BlockHandler(pBrowserImpl->m_ImageListEventHandlerPtr);
				
				for (ritr = items.rbegin() ; items.rend() != ritr ; ++ritr)
				{
					//printf("delete: %d\n",*ritr);
					QuiverFile f = (*pBrowserImpl->m_ImageListPtr)[*ritr];
					
					
					
					if (QuiverFileOps::MoveToTrash(f))
					{
						pBrowserImpl->m_ImageListPtr->Remove(*ritr);
					}

				}
				
				pBrowserImpl->m_ImageListPtr->UnblockHandler(pBrowserImpl->m_ImageListEventHandlerPtr);	
				
				quiver_icon_view_set_cursor_cell(QUIVER_ICON_VIEW(pBrowserImpl->m_pIconView),pBrowserImpl->m_ImageListPtr->GetCurrentIndex());					
				
				pBrowserImpl->m_ThumbnailLoader.UpdateList(true);
				break;
			}
			case GTK_RESPONSE_NO:
				//fall through
			default:
				// do not delete
				// cout << "not trashing file : " << endl;//m_QuiverImplPtr->m_ImageListPtr->GetCurrent().GetURI() << endl;
				break;
		}
	

	}
}

//=============================================================================
// private browser implementation nested classes:
//=============================================================================
void Browser::BrowserImpl::ImageListEventHandler::HandleContentsChanged(ImageListEventPtr event)
{ (void)event; 
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
		dirs.insert(dirs.end(), files.begin(), files.end());
		parent->m_BrowserHistory.Add(dirs, selected);
	}

	// refresh the list
	parent->SetImageIndex(parent->m_ImageListPtr->GetCurrentIndex(),true);
			
	quiver_icon_view_invalidate_window(QUIVER_ICON_VIEW(parent->m_pIconView));
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
	parent->m_bFolderTreeEvent = false;
	parent->UpdateUI();
}

void Browser::BrowserImpl::ImageListEventHandler::HandleCurrentIndexChanged(ImageListEventPtr event) 
{
	gint width=0, height=0;
 (void)width;
 (void)height;
	QuiverImageViewMode mode = quiver_image_view_get_view_mode_unmagnified(QUIVER_IMAGE_VIEW(parent->m_pImageView));
	
	if (mode != QUIVER_IMAGE_VIEW_MODE_ACTUAL_SIZE && gtk_widget_get_realized(parent->m_pImageView))
	{
		width = gtk_widget_get_width(parent->m_pImageView);
		height = gtk_widget_get_height(parent->m_pImageView);
	}
	
	parent->SetImageIndex(event->GetIndex(),true);
	
	parent->m_BrowserParent->EmitCursorChangedEvent();
}

void Browser::BrowserImpl::ImageListEventHandler::HandleItemAdded(ImageListEventPtr event)
{ (void)event; 
	// refresh the list
	parent->SetImageIndex(parent->m_ImageListPtr->GetCurrentIndex(),true);
	parent->m_ThumbnailLoader.UpdateList(true);
}

void Browser::BrowserImpl::ImageListEventHandler::HandleItemRemoved(ImageListEventPtr event)
{ (void)event; 
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
				gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(parent->m_pCssProvider), GTK_STYLE_PROVIDER_PRIORITY_THEME);
				
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
				gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(parent->m_pCssProvider), GTK_STYLE_PROVIDER_PRIORITY_THEME);
			}
		}
		else if (QUIVER_PREFS_APP_WINDOW_FULLSCREEN == event->GetKey() )
		{
			parent->UpdateUI();
		}
	}
}


void Browser::BrowserImpl::FolderTreeEventHandler::HandleSelectionChanged(FolderTreeEventPtr event)
{ (void)event; 
	list<string> listFolders = parent->m_FolderTreePtr->GetSelectedFolders();

	parent->m_bFolderTreeEvent = true;
	parent->m_ImageListPtr->UpdateImageListAsync(&listFolders);
}




struct BrowserThumbLoaderSyncData {
	GtkWidget* iconview;
	Statusbar* statusbar;
	gulong index;
	guint width;
	guint height;
	gulong start;
	gulong end;
	bool is_mapped;
	GMutex mutex;
	GCond cond;
	bool done;
};

static gboolean idle_invalidate_cell(gpointer data) {
	BrowserThumbLoaderSyncData* pData = (BrowserThumbLoaderSyncData*)data;
	quiver_icon_view_invalidate_cell(QUIVER_ICON_VIEW(pData->iconview), pData->index);
	delete pData;
	return G_SOURCE_REMOVE;
}

static gboolean idle_get_visible_range(gpointer data) {
	BrowserThumbLoaderSyncData* pData = (BrowserThumbLoaderSyncData*)data;
	quiver_icon_view_get_visible_range(QUIVER_ICON_VIEW(pData->iconview), &pData->start, &pData->end);
	g_mutex_lock(&pData->mutex);
	pData->done = true;
	g_cond_signal(&pData->cond);
	g_mutex_unlock(&pData->mutex);
	return G_SOURCE_REMOVE;
}

static gboolean idle_get_icon_size(gpointer data) {
	BrowserThumbLoaderSyncData* pData = (BrowserThumbLoaderSyncData*)data;
	quiver_icon_view_get_icon_size(QUIVER_ICON_VIEW(pData->iconview), &pData->width, &pData->height);
	g_mutex_lock(&pData->mutex);
	pData->done = true;
	g_cond_signal(&pData->cond);
	g_mutex_unlock(&pData->mutex);
	return G_SOURCE_REMOVE;
}

static gboolean idle_is_mapped(gpointer data) {
	BrowserThumbLoaderSyncData* pData = (BrowserThumbLoaderSyncData*)data;
	pData->is_mapped = gtk_widget_get_mapped(pData->iconview) ? true : false;
	g_mutex_lock(&pData->mutex);
	pData->done = true;
	g_cond_signal(&pData->cond);
	g_mutex_unlock(&pData->mutex);
	return G_SOURCE_REMOVE;
}

static gboolean idle_set_is_running(gpointer data) {
	BrowserThumbLoaderSyncData* pData = (BrowserThumbLoaderSyncData*)data;
	if (pData->statusbar) {
		if (pData->is_mapped) { // repurpose is_mapped for bIsRunning
			pData->statusbar->StartProgressPulse();
		} else {
			pData->statusbar->StopProgressPulse();
		}
	}
	delete pData;
	return G_SOURCE_REMOVE;
}
void Browser::BrowserImpl::BrowserThumbLoader::LoadThumbnail(const ThumbLoaderItem &item, guint uiWidth, guint uiHeight)
{

	BrowserThumbLoaderSyncData syncData;
	syncData.iconview = m_pBrowserImpl->m_pIconView;
	syncData.done = false;
	g_mutex_init(&syncData.mutex);
	g_cond_init(&syncData.cond);
	if (!ThreadUtil::IsGUIThread()) { g_idle_add_full(G_PRIORITY_HIGH, idle_is_mapped, &syncData, NULL);
	g_mutex_lock(&syncData.mutex);
	while(!syncData.done) g_cond_wait(&syncData.cond, &syncData.mutex);
	g_mutex_unlock(&syncData.mutex); } else { idle_is_mapped(&syncData); }
	bool is_mapped = syncData.is_mapped;
	g_mutex_clear(&syncData.mutex);
	g_cond_clear(&syncData.cond);

	if (is_mapped && item.m_ulIndex < m_pBrowserImpl->m_ImageListPtr->GetSize())
	{
		QuiverFile f(item.m_QuiverFile);

		GdkPixbuf *pixbuf = NULL;
		pixbuf = m_pBrowserImpl->m_ThumbnailCache.GetPixbuf(f.GetURI());				
	
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
			pixbuf = f.GetThumbnail(std::max(uiWidth,uiHeight));
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
				if (0 == bound_width || 0 == bound_height ||
					0 == gdk_pixbuf_get_width(pixbuf) ||
					0 == gdk_pixbuf_get_height(pixbuf))
				{
					g_object_unref(pixbuf);
					pixbuf = NULL;
				}
				else
				{
					GdkPixbuf* newpixbuf = gdk_pixbuf_scale_simple (
									pixbuf,
									bound_width,
									bound_height,
									GDK_INTERP_BILINEAR);
					g_object_unref(pixbuf);
					pixbuf = newpixbuf;
				}
			}

			if (NULL != pixbuf)			m_pBrowserImpl->m_ThumbnailCache.AddPixbuf(f.GetURI(),pixbuf);
			g_object_unref(pixbuf);

			BrowserThumbLoaderSyncData* pInvData = new BrowserThumbLoaderSyncData();
			pInvData->iconview = m_pBrowserImpl->m_pIconView;
			pInvData->index = item.m_ulIndex;
			if (!ThreadUtil::IsGUIThread()) { g_idle_add_full(G_PRIORITY_HIGH, idle_invalidate_cell, pInvData, NULL); } else { idle_invalidate_cell(pInvData); }
		}
	}
}

void Browser::BrowserImpl::BrowserThumbLoader::GetVisibleRange(gulong* pulStart, gulong* pulEnd)
{
	BrowserThumbLoaderSyncData syncData;
	syncData.iconview = m_pBrowserImpl->m_pIconView;
	syncData.done = false;
	g_mutex_init(&syncData.mutex);
	g_cond_init(&syncData.cond);
	if (!ThreadUtil::IsGUIThread()) { g_idle_add_full(G_PRIORITY_HIGH, idle_get_visible_range, &syncData, NULL);
	g_mutex_lock(&syncData.mutex);
	while(!syncData.done) g_cond_wait(&syncData.cond, &syncData.mutex);
	g_mutex_unlock(&syncData.mutex); } else { idle_get_visible_range(&syncData); }
	*pulStart = syncData.start;
	*pulEnd = syncData.end;
	g_mutex_clear(&syncData.mutex);
	g_cond_clear(&syncData.cond);
}


void Browser::BrowserImpl::BrowserThumbLoader::GetIconSize(guint* puiWidth, guint* puiHeight)
{
	BrowserThumbLoaderSyncData syncData;
	syncData.iconview = m_pBrowserImpl->m_pIconView;
	syncData.done = false;
	g_mutex_init(&syncData.mutex);
	g_cond_init(&syncData.cond);
	if (!ThreadUtil::IsGUIThread()) { g_idle_add_full(G_PRIORITY_HIGH, idle_get_icon_size, &syncData, NULL);
	g_mutex_lock(&syncData.mutex);
	while(!syncData.done) g_cond_wait(&syncData.cond, &syncData.mutex);
	g_mutex_unlock(&syncData.mutex); } else { idle_get_icon_size(&syncData); }
	*puiWidth = syncData.width;
	*puiHeight = syncData.height;
	g_mutex_clear(&syncData.mutex);
	g_cond_clear(&syncData.cond);
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
	return QuiverFile();
}

void Browser::BrowserImpl::BrowserThumbLoader::SetIsRunning(bool bIsRunning)
{
	BrowserThumbLoaderSyncData* pData = new BrowserThumbLoaderSyncData();
	pData->statusbar = m_pBrowserImpl->m_StatusbarPtr.get();
	pData->is_mapped = bIsRunning;
	if (!ThreadUtil::IsGUIThread()) { g_idle_add_full(G_PRIORITY_HIGH, idle_set_is_running, pData, NULL); } else { idle_set_is_running(pData); }
}

void Browser::BrowserImpl::BrowserThumbLoader::SetCacheSize(guint uiCacheSize)
{
	m_pBrowserImpl->m_ThumbnailCache.SetSize(uiCacheSize);
}

