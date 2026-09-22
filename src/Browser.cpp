#include <config.h>

#include <pthread.h>
#include <memory>
#include <atomic>

#include <gtk/gtk.h>
#include <pango/pangocairo.h>
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
#include "RenameDlg.h"
#include "RenameTask.h"

#include "QuiverClipboard.h"
#include "TaskManager.h"

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
	if (NULL == t)
		return;
	if (g_atomic_int_dec_and_test(&t->iRefs))
	{
		if (t->pImageView && G_IS_OBJECT(t->pImageView))
		{
			g_signal_handlers_disconnect_by_func(t->pImageView, (gpointer)pixbuf_target_destroyed, t);
		}
		delete t;
	}
}

#if HAVE_GDK_PIXBUF
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
#endif

struct AsyncTextureData {
	PixbufTarget *pTarget;
	GdkTexture *texture;
	gint width, height;
	gboolean bReset;
	bool bAtSize;
};

static gboolean idle_set_texture_b(gpointer data) {
	AsyncTextureData *p = (AsyncTextureData*)data;
	if (p->pTarget->pImageView != NULL)
	{
		if (p->bAtSize) {
			quiver_image_view_set_texture_at_size_ex(p->pTarget->pImageView, p->texture, p->width, p->height, p->bReset);
		} else {
			quiver_image_view_set_texture(p->pTarget->pImageView, p->texture);
		}
	}
	if (p->texture) g_object_unref(p->texture);
	pixbuf_target_unref(p->pTarget);
	delete p;
	return FALSE;
}

class ImageViewPixbufLoaderObserver : public IPixbufLoaderObserver
{
public:
	ImageViewPixbufLoaderObserver(QuiverImageView *imageview){m_pTarget = pixbuf_target_new(imageview);};
	virtual ~ImageViewPixbufLoaderObserver(){
		if (m_pTarget && m_pTarget->pImageView && G_IS_OBJECT(m_pTarget->pImageView))
		{
			g_signal_handlers_disconnect_by_func(m_pTarget->pImageView, (gpointer)pixbuf_target_destroyed, m_pTarget);
			m_pTarget->pImageView = NULL;
		}
		pixbuf_target_unref(m_pTarget);
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
#endif
	virtual void SetTexture(GdkTexture * texture){
		if (ThreadUtil::IsGUIThread()) {
			quiver_image_view_set_texture(m_pTarget->pImageView, texture);
		} else {
			if (texture) g_object_ref(texture);
			AsyncTextureData *data = new AsyncTextureData{m_pTarget, texture, 0, 0, FALSE, false};
			pixbuf_target_ref(m_pTarget);
			g_idle_add_full(G_PRIORITY_HIGH, idle_set_texture_b, data, NULL);
		}
	};
	virtual void SetTextureAtSize(GdkTexture *texture, gint width, gint height, bool bResetViewMode = true ){
		gboolean bReset = bResetViewMode ? TRUE : FALSE;
		if (ThreadUtil::IsGUIThread()) {
			quiver_image_view_set_texture_at_size_ex(m_pTarget->pImageView, texture, width, height, bReset);
		} else {
			if (texture) g_object_ref(texture);
			AsyncTextureData *data = new AsyncTextureData{m_pTarget, texture, width, height, bReset, true};
			pixbuf_target_ref(m_pTarget);
			g_idle_add_full(G_PRIORITY_HIGH, idle_set_texture_b, data, NULL);
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

	/* A right-click on an icon sets this while the button is held; the
	 * context-menu popover is only built and shown on the matching release,
	 * because popping up a menu mid-press makes the autohide grab pop it
	 * straight back down (the press is still in flight). */
	bool m_bContextMenuPending;

	/* When the pending right-click came down in the icon view's empty area
	 * (no cell under the pointer), the release builds the reduced empty-area
	 * menu (paste / undo-delete) instead of the item menu. */
	bool m_bContextMenuOnEmptyArea;

	/* Drag-and-drop: set in the drag-motion handler; used in the drop
	 * handler to know whether the external source offered MOVE or COPY. */
	GdkDragAction m_eDropAction;

	StatusbarPtr m_StatusbarPtr;
	
	ImageListPtr m_ImageListPtr;
	QuiverFile m_QuiverFileCurrent;
	ImageCache m_ThumbnailCache;
	ImageCache m_IconCache;
	ImageCache m_IconOverlayCache;
	ImageCache m_FilmstripCache;
	
	guint m_iTimeoutUpdateListID;
	guint m_iTimeoutHideLocationID;

	Browser *m_BrowserParent;

	ImageLoader m_ImageLoader;
	IPixbufLoaderObserverPtr m_ImageViewPixbufLoaderObserverPtr;

	map<string, string> m_mapFolderToFile;

	std::string m_strPeekFolderURI;
	ImageListPtr m_pPeekImageList;
	GThreadPool* m_pFolderPeekThreadPool;
	std::set<std::string> m_setFolderPeeksInFlight;
	std::mutex m_mutexFolderPeeks;

	void RequestFolderPeekAsync(QuiverIconView* iconview, gulong cell, const std::string& uri, int target_size);

	/* True when the browser is currently showing the trash folder
	 * (trash:///).  Delete then permanently removes items instead of moving
	 * them to trash. */
	bool IsTrashMode() const;

	/* Context-menu state that depends on the current folder (trash vs.
	 * normal), updated in browser_show_context_menu(). */
	GtkWidget *m_pContextMenuTrashBtn;
	GtkWidget *m_pContextMenuRestoreBtn;
	/* In the trash: title row showing the original name and location of the
	 * item the restore action would bring back (menu child at the top). */
	GtkWidget *m_pContextMenuTitleLabel;
	/* Overlay wrapping the icon view (see GetIconViewOverlay()). */
	GtkWidget *m_pIconViewOverlay;
	
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
		BrowserThumbLoader(BrowserImpl* pBrowserImpl, guint iNumThreads, std::shared_ptr<bool> spAlive)  :
			IconViewThumbLoader(iNumThreads, false),
			m_pBrowserImpl(pBrowserImpl),
			m_spAlive(spAlive)
		{
			m_bMapped.store(false, std::memory_order_relaxed);
			m_uiThumbWidth.store(96, std::memory_order_relaxed);
			m_uiThumbHeight.store(96, std::memory_order_relaxed);
			Start();
		}
		
		~BrowserThumbLoader(){}

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
		virtual void GetVisibleRange(gulong* pulStart, gulong* pulEnd);
		virtual void GetIconSize(guint* puiWidth, guint* puiHeight);
		virtual gulong GetNumItems();
		virtual QuiverFile GetQuiverFile(gulong index);
		virtual void SetIsRunning(bool bIsRunning);
		virtual void SetCacheSize(guint uiCacheSize);
	
		
	private:
		BrowserImpl* m_pBrowserImpl; 
		std::shared_ptr<bool> m_spAlive;
		std::atomic<bool> m_bMapped;
		std::atomic<guint> m_uiThumbWidth;
		std::atomic<guint> m_uiThumbHeight;
	};


	IImageListEventHandlerPtr    m_ImageListEventHandlerPtr;
	IPreferencesEventHandlerPtr  m_PreferencesEventHandlerPtr;
	IFolderTreeEventHandlerPtr m_FolderTreeEventHandlerPtr;
	
	std::shared_ptr<bool> m_spAlive;
	BrowserThumbLoader m_ThumbnailLoader;
	
};
// ============================================================================


static void browser_action_handler_cb(GSimpleAction *action, GVariant *parameter, gpointer data);
static void browser_icon_view_map_cb(GtkWidget *widget, gpointer user_data);
static void browser_icon_view_unmap_cb(GtkWidget *widget, gpointer user_data);

#define ACTION_BROWSER_OPEN_LOCATION                      "BrowserOpenLocation"
#define ACTION_BROWSER_HISTORY_BACK                       "BrowserHistoryBack"
#define ACTION_BROWSER_HISTORY_FORWARD                    "BrowserHistoryForward"
#define ACTION_BROWSER_CUT                                "BrowserCut"
#define ACTION_BROWSER_COPY                               "BrowserCopy"
#define ACTION_BROWSER_PASTE                              "BrowserPaste"
#define ACTION_BROWSER_NEW_FOLDER                         "BrowserNewFolder"
#define ACTION_BROWSER_RENAME                             "BrowserRename"
#define ACTION_BROWSER_SELECT_ALL                         "BrowserSelectAll"
#define ACTION_BROWSER_TRASH                              "BrowserTrash"
#define ACTION_BROWSER_TRASH_FORCE                        "BrowserTrashForce"
#define ACTION_BROWSER_RESTORE                            "BrowserRestore"
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

FolderTreePtr Browser::GetFolderTree()
{
	return m_BrowserImplPtr->m_FolderTreePtr;
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
	QuiverUtils::GrabFocusForWidget (m_BrowserImplPtr->m_pIconView);
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

GtkWidget* Browser::GetIconViewOverlay()
{
	return m_BrowserImplPtr->m_pIconViewOverlay;
}


//=============================================================================
//=============================================================================
// private browser implementation:
//=============================================================================


//=============================================================================
// BrowswerImpl Callback Prototypes
//=============================================================================

#if HAVE_GDK_PIXBUF
static GdkPixbuf* icon_pixbuf_callback(QuiverIconView *iconview, gulong cell,gpointer user_data);
static GdkPixbuf* thumbnail_pixbuf_callback(QuiverIconView *iconview, gulong cell, gint* actual_width, gint* actual_height, gpointer user_data);
static GdkPixbuf* overlay_pixbuf_callback(QuiverIconView* iconview, gulong cell, QuiverIconOverlayType type, gpointer user_data);
#endif
static GdkTexture* icon_texture_callback(QuiverIconView *iconview, gulong cell, gpointer user_data);
static GdkTexture* thumbnail_texture_callback(QuiverIconView *iconview, gulong cell, gint* actual_width, gint* actual_height, gpointer user_data);
static GdkTexture* overlay_texture_callback(QuiverIconView* iconview, gulong cell, QuiverIconOverlayType type, gpointer user_data);
static GdkTexture* filmstrip_texture_callback(QuiverIconView* iconview, gulong cell,
	gint thumb_natural_w, gint thumb_natural_h,
	gint thumb_drawn_w, gint thumb_drawn_h,
	QuiverIconViewFilmstripSide side, gpointer user_data);
static gchar* text_pixbuf_callback(QuiverIconView *iconview, gulong cell,gpointer user_data);
static gulong n_cells_callback(QuiverIconView *iconview, gpointer user_data);
static void icon_size_value_changed (GtkRange *range,gpointer  user_data);

static void iconview_cell_activated_cb(QuiverIconView *iconview, guint cell, gpointer user_data);
static void iconview_cursor_changed_cb(QuiverIconView *iconview, guint cell, gpointer user_data);
static void iconview_selection_changed_cb(QuiverIconView *iconview, gpointer user_data);
	static void iconview_motion_notify(GtkEventControllerMotion *controller, gdouble x, gdouble y, gpointer user_data);
	static void iconview_leave_notify(GtkEventControllerMotion *controller, gpointer user_data);

static void browser_button_press_cb(GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data); 
static void browser_button_release_cb(GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data); 
static void browser_show_context_menu(GtkWidget *widget, gdouble x, gdouble y, gpointer userdata);
static gboolean iconview_key_press_cb(GtkEventControllerKey *controller, guint keyval, guint keycode, GdkModifierType state, gpointer user_data);

static GdkContentProvider* browser_drag_source_prepare(GtkDragSource *source, gdouble x, gdouble y, gpointer user_data);
static void browser_drag_source_begin(GtkDragSource *source, GdkDrag *drag, gpointer user_data);
static gboolean browser_drop_motion_cb(GtkDropTarget *target, gdouble x, gdouble y, gpointer user_data);
static void browser_drop_leave_cb(GtkDropTarget *target, gpointer user_data);
static gboolean browser_drop_cb(GtkDropTarget *target, const GValue *value, gdouble x, gdouble y, gpointer user_data);

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
	m_FilmstripCache(8),
	m_ImageListEventHandlerPtr( new ImageListEventHandler(this) ),
	m_PreferencesEventHandlerPtr(new PreferencesEventHandler(this) ),
	m_FolderTreeEventHandlerPtr( new FolderTreeEventHandler(this) ),
	m_spAlive(std::make_shared<bool>(true)),
	m_ThumbnailLoader(this, 4, m_spAlive)
{
	PreferencesPtr prefsPtr = Preferences::GetInstance();
	prefsPtr->AddEventHandler( m_PreferencesEventHandlerPtr );
	m_FolderTreePtr->AddEventHandler(m_FolderTreeEventHandlerPtr);

	m_BrowserParent = parent;
	m_pToolbar = NULL;
	m_bFolderTreeEvent = false;
	m_bBrowserHistoryEvent = false;
	m_pFolderPeekThreadPool = NULL;

	m_iTimeoutUpdateListID = 0;
	m_iTimeoutHideLocationID = 0;
	m_pContextMenuTrashBtn = NULL;
	m_pContextMenuRestoreBtn = NULL;
	m_pContextMenuTitleLabel = NULL;
	m_pContextMenuPopover = NULL;
	m_bContextMenuPending = false;
	m_bContextMenuOnEmptyArea = false;
	m_eDropAction = GDK_ACTION_COPY;
	m_pIconViewOverlay = NULL;
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

	hscale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,20,256,1);
	gtk_range_set_value(GTK_RANGE(hscale),128);
	gtk_scale_set_value_pos (GTK_SCALE(hscale),GTK_POS_LEFT);
	gtk_scale_set_draw_value(GTK_SCALE(hscale),FALSE);
	gtk_widget_set_tooltip_text(hscale, "Thumbnail Size");
	gtk_widget_set_focus_on_click(hscale, FALSE);
	gtk_widget_set_focusable(hscale, FALSE);

	gtk_widget_set_size_request(hscale,100,-1);
	m_pToolItemThumbSizer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_set_hexpand(m_pToolItemThumbSizer, TRUE);

	gtk_widget_set_halign(hscale, GTK_ALIGN_END);
	gtk_widget_set_valign(hscale, GTK_ALIGN_CENTER);
	gtk_widget_set_margin_start(hscale, 4);
	gtk_widget_set_margin_end(hscale, 4);
	
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
	m_ThumbnailLoader.SetMapped(gtk_widget_get_mapped(m_pIconView));
	g_signal_connect(G_OBJECT(m_pIconView), "map", G_CALLBACK(browser_icon_view_map_cb), this);
	g_signal_connect(G_OBJECT(m_pIconView), "unmap", G_CALLBACK(browser_icon_view_unmap_cb), this);

	/* Drag & drop: drag the whole selection out; drop file URIs in.  The
	 * drop target accepts a G_TYPE_STRING, which the uri-list/gome-copied-files
	 * deserializers (QuiverClipboard) feed with the incoming file list. */
	{
		GtkDragSource *drag_source = gtk_drag_source_new();
		gtk_drag_source_set_actions(drag_source, (GdkDragAction)(GDK_ACTION_MOVE | GDK_ACTION_COPY));
		g_signal_connect(drag_source, "prepare",
			G_CALLBACK(browser_drag_source_prepare), this);
		g_signal_connect(drag_source, "drag-begin",
			G_CALLBACK(browser_drag_source_begin), this);
		gtk_widget_add_controller(GTK_WIDGET(m_pIconView),
			GTK_EVENT_CONTROLLER(drag_source));

		GtkDropTarget *drop_target = gtk_drop_target_new(G_TYPE_STRING,
			(GdkDragAction)(GDK_ACTION_COPY | GDK_ACTION_MOVE));
		g_signal_connect(drop_target, "motion",
			G_CALLBACK(browser_drop_motion_cb), this);
		g_signal_connect(drop_target, "leave",
			G_CALLBACK(browser_drop_leave_cb), this);
		g_signal_connect(drop_target, "drop",
			G_CALLBACK(browser_drop_cb), this);
		gtk_widget_add_controller(GTK_WIDGET(m_pIconView),
			GTK_EVENT_CONTROLLER(drop_target));
	}
	m_pImageView = quiver_image_view_new();

	bool bShowPreview = prefsPtr->GetBoolean(QUIVER_PREFS_BROWSER,QUIVER_PREFS_BROWSER_PREVIEW_SHOW,true);
	if (bShowPreview)
	{
		gtk_widget_set_visible(m_pImageView, TRUE);
	}

	scrolled_window = gtk_scrolled_window_new();
	
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),GTK_POLICY_AUTOMATIC,GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled_window),m_pIconView);
	
	/* wrap the icon-view scroller in an overlay so floating chrome (i.e. the
	 * undo-delete toast) can be placed over the top-right of the image grid
	 * without disturbing the location-entry row above. */
	m_pIconViewOverlay = gtk_overlay_new();
	gtk_overlay_set_child(GTK_OVERLAY(m_pIconViewOverlay), scrolled_window);
	gtk_widget_set_hexpand(m_pIconViewOverlay, TRUE);
	gtk_widget_set_vexpand(m_pIconViewOverlay, TRUE);
	
	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL,0);
	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL,0);
	
	gtk_box_append (GTK_BOX (hbox), m_pLocationEntry);
	//gtk_box_append (GTK_BOX (hbox), hscale);
	gtk_widget_set_hexpand(hbox, TRUE);
	gtk_widget_set_vexpand(vbox, TRUE);
	gtk_box_append (GTK_BOX (vbox), hbox);
	gtk_box_append (GTK_BOX (vbox), m_pIconViewOverlay);
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
	
	GtkWidget *pFolderTree = m_FolderTreePtr->GetWidget();
	m_pSWFolderTree = pFolderTree;
	gtk_widget_set_visible(pFolderTree, TRUE);
	gtk_notebook_append_page(GTK_NOTEBOOK(m_pNotebook), pFolderTree, gtk_label_new("Folders"));	
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
		g_signal_connect(gesture, "released", G_CALLBACK(browser_button_release_cb), this);
		gtk_widget_add_controller(m_pImageView, GTK_EVENT_CONTROLLER(gesture));
	}
	//g_signal_connect(G_OBJECT(m_pImageView), "popup-menu", G_CALLBACK(browser_popup_menu_cb), this);

	quiver_icon_view_set_scroll_type(QUIVER_ICON_VIEW(m_pIconView),QUIVER_ICON_VIEW_SCROLL_SMOOTH);
	quiver_icon_view_set_n_items_func(QUIVER_ICON_VIEW(m_pIconView),(QuiverIconViewGetNItemsFunc)n_cells_callback,this,NULL);
#if HAVE_GDK_PIXBUF
	quiver_icon_view_set_thumbnail_pixbuf_func(QUIVER_ICON_VIEW(m_pIconView),(QuiverIconViewGetThumbnailPixbufFunc)thumbnail_pixbuf_callback,this,NULL);
	quiver_icon_view_set_icon_pixbuf_func(QUIVER_ICON_VIEW(m_pIconView),(QuiverIconViewGetIconPixbufFunc)icon_pixbuf_callback,this,NULL);
	quiver_icon_view_set_overlay_pixbuf_func(QUIVER_ICON_VIEW(m_pIconView),overlay_pixbuf_callback,this,NULL);
#endif
	quiver_icon_view_set_thumbnail_texture_func(QUIVER_ICON_VIEW(m_pIconView),thumbnail_texture_callback,this,NULL);
	quiver_icon_view_set_icon_texture_func(QUIVER_ICON_VIEW(m_pIconView),icon_texture_callback,this,NULL);
	quiver_icon_view_set_overlay_texture_func(QUIVER_ICON_VIEW(m_pIconView),overlay_texture_callback,this,NULL);
	quiver_icon_view_set_text_func(QUIVER_ICON_VIEW(m_pIconView),(QuiverIconViewGetTextFunc)text_pixbuf_callback,this,NULL);
	quiver_icon_view_set_get_filmstrip_texture_func(QUIVER_ICON_VIEW(m_pIconView),filmstrip_texture_callback,this,NULL);

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
		g_signal_connect(gesture, "released", G_CALLBACK(browser_button_release_cb), this);
		gtk_widget_add_controller(m_pIconView, GTK_EVENT_CONTROLLER(gesture));
	}
	{
		GtkEventController *key_ctrl = gtk_event_controller_key_new();
		g_signal_connect(key_ctrl, "key-pressed", G_CALLBACK(iconview_key_press_cb), this);
		gtk_widget_add_controller(m_pIconView, key_ctrl);
	}
	{
		GtkEventController *motion = gtk_event_controller_motion_new();
		g_signal_connect(motion, "motion", G_CALLBACK(iconview_motion_notify), this);
		g_signal_connect(motion, "leave", G_CALLBACK(iconview_leave_notify), this);
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
		GdkRGBA color;
		if (gdk_rgba_parse(&color, strBGColorThumb.c_str()))
			QuiverUtils::SetWidgetBgColor(m_pIconView, &color);
		if (gdk_rgba_parse(&color, strBGColorImg.c_str()))
			QuiverUtils::SetWidgetBgColor(m_pImageView, &color);
	}

	quiver_icon_view_set_overlay_texture_func(QUIVER_ICON_VIEW(m_pIconView),(QuiverIconViewGetOverlayTextureFunc)overlay_texture_callback,this,NULL);
#if HAVE_GDK_PIXBUF
	quiver_icon_view_set_overlay_pixbuf_func(QUIVER_ICON_VIEW(m_pIconView),(QuiverIconViewGetOverlayPixbufFunc)overlay_pixbuf_callback,this,NULL);
#endif

	IPixbufLoaderObserverPtr tmp ( new ImageViewPixbufLoaderObserver(QUIVER_IMAGE_VIEW(m_pImageView)) );
	m_ImageViewPixbufLoaderObserverPtr = tmp;
	m_ImageLoader.AddPixbufLoaderObserver(m_ImageViewPixbufLoaderObserverPtr.get());

	gtk_widget_set_visible(m_pBrowserWidget, TRUE);
	gtk_widget_set_visible(m_pBrowserWidget, FALSE);

	// The context menu is a real menu model (no buttons); item set depends on
	// the current folder/trash mode, so the popover and its model are built
	// fresh in browser_show_context_menu() on every right-click.  Only the
	// trash-mode title row is long-lived.
	m_pContextMenuTitleLabel = QuiverUtils::MakeMenuTitleLabel("", "");
	gtk_widget_set_visible(m_pContextMenuTitleLabel, FALSE);

	/* The popover is parented to the icon view on each right-click.  If the
	 * icon view is destroyed directly (window teardown), the popover dies with
	 * it, so drop the pointer here -- the destructor must not unparent it then. */
	g_signal_connect(m_pIconView, "destroy",
		G_CALLBACK(+[](GtkWidget*, gpointer user_data) {
			Browser::BrowserImpl* b = static_cast<Browser::BrowserImpl*>(user_data);
			b->m_pContextMenuPopover = NULL;
		}), this);


	gdouble thumb_size = (gdouble)prefsPtr->GetInteger(QUIVER_PREFS_BROWSER,QUIVER_PREFS_BROWSER_THUMB_SIZE);	

	if (thumb_size < 20. || 256. < thumb_size)
	{
		thumb_size = 128.;
	}
	gtk_range_set_value(GTK_RANGE(hscale),thumb_size);
	m_ThumbnailLoader.SetIconDimensions((guint)thumb_size, (guint)thumb_size);

	bool bThumbsSquare = prefsPtr->GetBoolean(QUIVER_PREFS_BROWSER,QUIVER_PREFS_BROWSER_THUMBS_SQUARE, false);
	quiver_icon_view_set_thumbnails_square(QUIVER_ICON_VIEW(m_pIconView), bThumbsSquare);

	bool bFilmstrip = prefsPtr->GetBoolean(QUIVER_PREFS_BROWSER,QUIVER_PREFS_BROWSER_THUMBS_FILMSTRIP, true);
	quiver_icon_view_set_filmstrip_enabled(QUIVER_ICON_VIEW(m_pIconView), bFilmstrip);

}

bool Browser::BrowserImpl::IsTrashMode() const
{
	list<string> dirs = m_ImageListPtr->GetFolderList();
	return (1 == dirs.size() && QuiverFileOps::IsTrashURI(dirs.front().c_str()));
}

Browser::BrowserImpl::~BrowserImpl()
{
	if (m_spAlive)
	{
		*m_spAlive = false;
	}
	m_ThumbnailLoader.Stop();

	if (m_pIconView && QUIVER_IS_ICON_VIEW(m_pIconView))
	{
		g_signal_handlers_disconnect_by_func(m_pIconView, (gpointer)browser_icon_view_map_cb, this);
		g_signal_handlers_disconnect_by_func(m_pIconView, (gpointer)browser_icon_view_unmap_cb, this);
		quiver_icon_view_set_n_items_func(QUIVER_ICON_VIEW(m_pIconView), NULL, NULL, NULL);
#if HAVE_GDK_PIXBUF
		quiver_icon_view_set_thumbnail_pixbuf_func(QUIVER_ICON_VIEW(m_pIconView), NULL, NULL, NULL);
		quiver_icon_view_set_icon_pixbuf_func(QUIVER_ICON_VIEW(m_pIconView), NULL, NULL, NULL);
		quiver_icon_view_set_overlay_pixbuf_func(QUIVER_ICON_VIEW(m_pIconView), NULL, NULL, NULL);
#endif
		quiver_icon_view_set_thumbnail_texture_func(QUIVER_ICON_VIEW(m_pIconView), NULL, NULL, NULL);
		quiver_icon_view_set_icon_texture_func(QUIVER_ICON_VIEW(m_pIconView), NULL, NULL, NULL);
		quiver_icon_view_set_overlay_texture_func(QUIVER_ICON_VIEW(m_pIconView), NULL, NULL, NULL);
		quiver_icon_view_set_text_func(QUIVER_ICON_VIEW(m_pIconView), NULL, NULL, NULL);
		quiver_icon_view_set_get_filmstrip_texture_func(QUIVER_ICON_VIEW(m_pIconView), NULL, NULL, NULL);
	}

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

	if (m_pFolderPeekThreadPool)
	{
		g_thread_pool_free(m_pFolderPeekThreadPool, TRUE, TRUE);
		m_pFolderPeekThreadPool = NULL;
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

	/* Disconnect all GObject signal handlers that captured `this` */
	if (m_pImageView && G_IS_OBJECT(m_pImageView))
	{
		g_signal_handlers_disconnect_by_data(m_pImageView, this);
	}
	if (m_pIconView && G_IS_OBJECT(m_pIconView))
	{
		g_signal_handlers_disconnect_by_data(m_pIconView, this);
	}
	if (m_pLocationEntry && G_IS_OBJECT(m_pLocationEntry))
	{
		g_signal_handlers_disconnect_by_data(m_pLocationEntry, this);
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
	QuiverUtils::AddSimpleAction(ACTION_BROWSER_NEW_FOLDER, "<Control><Shift>N", browser_action_handler_cb, this);
	QuiverUtils::AddSimpleAction(ACTION_BROWSER_RENAME, "F2", browser_action_handler_cb, this);
	QuiverUtils::AddSimpleAction(ACTION_BROWSER_SELECT_ALL, "<Control>A", browser_action_handler_cb, this);
	QuiverUtils::AddSimpleAction(ACTION_BROWSER_TRASH, "Delete", browser_action_handler_cb, this);
	QuiverUtils::AddSimpleAction(ACTION_BROWSER_TRASH_FORCE, "<Shift>Delete", browser_action_handler_cb, this);
	QuiverUtils::AddSimpleAction(ACTION_BROWSER_RESTORE, NULL, browser_action_handler_cb, this);
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
	if (m_pToolItemThumbSizer)
	{
		if (toolbar && GTK_IS_HEADER_BAR(toolbar))
		{
			gtk_widget_set_hexpand(m_pToolItemThumbSizer, FALSE);
		}
		else
		{
			gtk_widget_set_hexpand(m_pToolItemThumbSizer, TRUE);
		}
	}
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
		if (GTK_IS_HEADER_BAR(m_pToolbar))
		{
			gtk_header_bar_pack_end(GTK_HEADER_BAR(m_pToolbar), m_pToolItemThumbSizer);
		}
		else if (GTK_IS_BOX(m_pToolbar))
		{
			gtk_box_append(GTK_BOX(m_pToolbar), m_pToolItemThumbSizer);
		}
	}

 	if (0 != m_ImageListPtr->GetSize())
	{
		quiver_icon_view_set_cursor_cell( QUIVER_ICON_VIEW(m_pIconView),
			m_ImageListPtr->GetCurrentIndex() );
	}
	
	gint cursor_cell = quiver_icon_view_get_cursor_cell(QUIVER_ICON_VIEW(m_pIconView));
 
	if (0 == m_ImageListPtr->GetSize() || m_QuiverFileCurrent != m_ImageListPtr->GetCurrent())
	{
		quiver_image_view_set_texture(QUIVER_IMAGE_VIEW(m_pImageView),NULL);
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
		if (GTK_IS_HEADER_BAR(m_pToolbar))
		{
			gtk_header_bar_remove(GTK_HEADER_BAR(m_pToolbar), m_pToolItemThumbSizer);
		}
		else if (GTK_IS_BOX(m_pToolbar))
		{
			gtk_box_remove(GTK_BOX(m_pToolbar), m_pToolItemThumbSizer);
		}
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
	if (0 != m_ImageListPtr->GetSize() && m_ImageListPtr->SetCurrentIndex(index))
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
		quiver_image_view_set_texture(QUIVER_IMAGE_VIEW(m_pImageView), NULL);
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
	b->m_ThumbnailLoader.SetIconDimensions((guint)value, (guint)value);
}

static void browser_icon_view_map_cb(GtkWidget *widget, gpointer user_data)
{
	(void)widget;
	Browser::BrowserImpl* b = (Browser::BrowserImpl*)user_data;
	b->m_ThumbnailLoader.SetMapped(true);
	/* The first UpdateList(true) often runs at startup before the
	 * icon view is mapped, so every thumbnail was skipped.  Re-queue
	 * the visible range now that the widget is mapped (and allocated). */
	b->m_ThumbnailLoader.UpdateList(true);
}

static void browser_icon_view_unmap_cb(GtkWidget *widget, gpointer user_data)
{
	(void)widget;
	Browser::BrowserImpl* b = (Browser::BrowserImpl*)user_data;
	b->m_ThumbnailLoader.SetMapped(false);
}

static gulong n_cells_callback(QuiverIconView *iconview, gpointer user_data)
{ (void)iconview; 
	Browser::BrowserImpl* b = (Browser::BrowserImpl*)user_data;
	return b->m_ImageListPtr->GetSize();
}

#if HAVE_GDK_PIXBUF
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
#endif

static GdkTexture* icon_texture_callback(QuiverIconView *iconview, gulong cell, gpointer user_data)
{
	Browser::BrowserImpl* b = (Browser::BrowserImpl*)user_data;
	QuiverFile f = (*b->m_ImageListPtr)[cell];
	GdkTexture* texture = NULL;

	guint width, height;
	quiver_icon_view_get_icon_size(iconview, &width, &height);

	gchar* icon_name = f.GetIconName();
	if (icon_name)
	{
		gchar cache_icon_name [256] = "";
		g_snprintf(cache_icon_name, 256, "%s%d-%d", icon_name, width, height);
		texture = b->m_IconCache.GetTexture(cache_icon_name);
		if (NULL == texture)
		{
			texture = f.GetIconTexture(width, height);
			if (NULL != texture)
			{
				b->m_IconCache.AddTexture(cache_icon_name, texture);
			}
		}
		g_free(icon_name);
	}

	return texture;
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


struct FolderPeekTaskData {
	Browser::BrowserImpl* browser;
	QuiverIconView* iconview;
	gulong cell;
	std::string uri;
	int target_size;
	std::weak_ptr<bool> aliveToken;
};

static void FolderPeekWorker(gpointer data, gpointer user_data)
{
	(void)user_data;
	std::unique_ptr<FolderPeekTaskData> task(static_cast<FolderPeekTaskData*>(data));

	auto alive = task->aliveToken.lock();
	if (!alive || !*alive)
		return;

	QuiverFile child(task->uri.c_str());
	GdkTexture* texture = child.GetThumbnailTexture(task->target_size);

	struct PeekResultData {
		Browser::BrowserImpl* browser;
		QuiverIconView* iconview;
		gulong cell;
		std::string uri;
		GdkTexture* texture;
		std::weak_ptr<bool> aliveToken;
	};

	PeekResultData* result = new PeekResultData{
		task->browser,
		task->iconview,
		task->cell,
		task->uri,
		texture,
		task->aliveToken
	};

	g_idle_add([](gpointer d) -> gboolean {
		std::unique_ptr<PeekResultData> res(static_cast<PeekResultData*>(d));
		auto alive = res->aliveToken.lock();
		if (alive && *alive)
		{
			{
				std::lock_guard<std::mutex> lock(res->browser->m_mutexFolderPeeks);
				res->browser->m_setFolderPeeksInFlight.erase(res->uri);
			}
			if (res->texture)
			{
				res->browser->m_ThumbnailCache.AddTexture(res->uri, res->texture);

				if (res->iconview && QUIVER_IS_ICON_VIEW(res->iconview))
				{
					quiver_icon_view_invalidate_cell(res->iconview, res->cell);
				}
			}
			else
			{
				res->browser->m_ThumbnailCache.AddFailure(res->uri);
			}
		}
		if (res->texture)
		{
			g_object_unref(res->texture);
		}
		return G_SOURCE_REMOVE;
	}, result);
}

void Browser::BrowserImpl::RequestFolderPeekAsync(QuiverIconView* iconview, gulong cell, const std::string& uri, int target_size)
{
	if (m_ThumbnailCache.HasFailed(uri))
		return;

	{
		std::lock_guard<std::mutex> lock(m_mutexFolderPeeks);
		if (m_setFolderPeeksInFlight.find(uri) != m_setFolderPeeksInFlight.end())
			return;
		m_setFolderPeeksInFlight.insert(uri);
	}

	if (!m_pFolderPeekThreadPool)
	{
		m_pFolderPeekThreadPool = g_thread_pool_new(FolderPeekWorker, this, 2, FALSE, NULL);
	}

	FolderPeekTaskData* task = new FolderPeekTaskData{this, iconview, cell, uri, target_size, m_spAlive};
	g_thread_pool_push(m_pFolderPeekThreadPool, task, NULL);
}

#if HAVE_GDK_PIXBUF
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
		gulong prelight = quiver_icon_view_get_prelight_cell(iconview);
		if (prelight == cell)
		{
			gint x = 0, y = 0;
			quiver_icon_view_get_cell_mouse_position(iconview, cell, &x, &y);

			if (0 <= x && 0 <= y && x < gint(width) && y < gint(height))
			{
				double percent = double(x) / width;
				if (b->m_strPeekFolderURI != f.GetURI() || !b->m_pPeekImageList)
				{
					b->m_strPeekFolderURI = f.GetURI();
					b->m_pPeekImageList.reset(new ImageList());
					b->m_pPeekImageList->SetImageList(b->m_strPeekFolderURI.c_str());
				}

				unsigned int listSize = b->m_pPeekImageList ? b->m_pPeekImageList->GetSize() : 0;
				if (0 != listSize)
				{
					unsigned int index = (unsigned int)(listSize * percent);
					index = std::min(index, listSize - 1);
					QuiverFile child = (*b->m_pPeekImageList)[index];
					std::string child_uri = child.GetURI();

					pixbuf = b->m_ThumbnailCache.GetPixbuf(child_uri);
					if (pixbuf)
					{
						if (child.IsWidthHeightSet())
						{
							*actual_width = child.GetWidth();
							*actual_height = child.GetHeight();
							if (4 < child.GetOrientation())
							{
								swap(*actual_width,*actual_height);
							}
						}
						else
						{
							*actual_width = gdk_pixbuf_get_width(pixbuf);
							*actual_height = gdk_pixbuf_get_height(pixbuf);
						}
					}
					else
					{
						b->RequestFolderPeekAsync(iconview, cell, child_uri, std::max(width, height));
					}
				}
			}
		}
		need_new_thumb = FALSE;
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
			else if (thumb_width >= bound_width && thumb_height >= bound_height)
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
#endif

static GdkTexture* thumbnail_texture_callback(QuiverIconView *iconview, gulong cell, gint* actual_width, gint* actual_height, gpointer user_data)
{
	Browser::BrowserImpl* b = (Browser::BrowserImpl*)user_data;

	GdkTexture *texture = NULL;
	gboolean need_new_thumb = TRUE;
	
	guint width, height;
	quiver_icon_view_get_icon_size(iconview,&width,&height);

	QuiverFile f = (*b->m_ImageListPtr)[cell];

	if (f.IsFolder())
	{
		gulong prelight = quiver_icon_view_get_prelight_cell(iconview);
		if (prelight == cell)
		{
			gint x = 0, y = 0;
			quiver_icon_view_get_cell_mouse_position(iconview, cell, &x, &y);

			if (0 <= x && 0 <= y && x < gint(width) && y < gint(height))
			{
				double percent = double(x) / width;
				if (b->m_strPeekFolderURI != f.GetURI() || !b->m_pPeekImageList)
				{
					b->m_strPeekFolderURI = f.GetURI();
					b->m_pPeekImageList.reset(new ImageList());
					b->m_pPeekImageList->SetImageList(b->m_strPeekFolderURI.c_str());
				}

				unsigned int listSize = b->m_pPeekImageList ? b->m_pPeekImageList->GetSize() : 0;
				if (0 != listSize)
				{
					unsigned int index = (unsigned int)(listSize * percent);
					index = std::min(index, listSize - 1);
					QuiverFile child = (*b->m_pPeekImageList)[index];
					std::string child_uri = child.GetURI();

					texture = b->m_ThumbnailCache.GetTexture(child_uri);
					if (texture)
					{
						if (child.IsWidthHeightSet())
						{
							*actual_width = child.GetWidth();
							*actual_height = child.GetHeight();
							if (4 < child.GetOrientation())
							{
								swap(*actual_width,*actual_height);
							}
						}
						else
						{
							*actual_width = gdk_texture_get_width(texture);
							*actual_height = gdk_texture_get_height(texture);
						}
					}
					else
					{
						b->RequestFolderPeekAsync(iconview, cell, child_uri, std::max(width, height));
					}
				}
			}
		}
		need_new_thumb = FALSE;
	}
	else
	{
		texture = b->m_ThumbnailCache.GetTexture(f.GetURI());

		if (texture)
		{
			*actual_width = f.GetWidth();
			*actual_height = f.GetHeight();

			if (4 < f.GetOrientation())
			{
				swap(*actual_width,*actual_height);
			}

			guint thumb_width, thumb_height;
			thumb_width = gdk_texture_get_width(texture);
			thumb_height = gdk_texture_get_height(texture);

			guint bound_width, bound_height;
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
	}
	
	if (need_new_thumb)
	{
		// add a timeout
		b->QueueIconViewUpdate();
	}
	
	return texture;
}

#if HAVE_GDK_PIXBUF
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
#endif

static GdkTexture* overlay_texture_callback(QuiverIconView* iconview, gulong cell, QuiverIconOverlayType type, gpointer user_data)
{ (void)iconview; 
	GdkTexture* texture = NULL;
	Browser::BrowserImpl* b = (Browser::BrowserImpl*)user_data;
	QuiverFile f = (*b->m_ImageListPtr)[cell];
	if (type == QUIVER_ICON_OVERLAY_ICON && f.IsFolder())
	{
		gchar* icon_name = f.GetIconName();
		if (icon_name)
		{
			texture = b->m_IconOverlayCache.GetTexture(icon_name);
			if (NULL == texture)
			{
				texture = f.GetIconTexture(32, 32);
				if (NULL != texture)
				{
					b->m_IconOverlayCache.AddTexture(icon_name, texture);
				}
			}
			g_free(icon_name);
		}
	}
	else if (type == QUIVER_ICON_OVERLAY_LINK && f.IsVideo())
	{
		gchar* icon_name = f.GetIconName();
		if (icon_name)
		{
			texture = b->m_IconOverlayCache.GetTexture(icon_name);
			if (NULL == texture)
			{
				texture = f.GetIconTexture(32, 32);
				if (NULL != texture)
				{
					b->m_IconOverlayCache.AddTexture(icon_name, texture);
				}
			}
			g_free(icon_name);
		}
	}

	return texture;
}

static GdkTexture* filmstrip_texture_callback(QuiverIconView* iconview, gulong cell,
	gint thumb_natural_w, gint thumb_natural_h,
	gint thumb_drawn_w, gint thumb_drawn_h,
	QuiverIconViewFilmstripSide side, gpointer user_data)
{ (void)iconview; (void)thumb_drawn_w; (void)thumb_drawn_h; (void)side;
	Browser::BrowserImpl* b = (Browser::BrowserImpl*)user_data;
	QuiverFile f = (*b->m_ImageListPtr)[cell];
	if (!f.IsVideo())
		return NULL;

	std::string path = QuiverUtils::GetFilmstripPath(MAX(thumb_natural_w, thumb_natural_h));
	GdkTexture* texture = b->m_FilmstripCache.GetTexture(path);
	if (NULL == texture)
	{
		texture = gdk_texture_new_from_filename(path.c_str(), NULL);
		if (NULL != texture)
		{
			b->m_FilmstripCache.AddTexture(path, texture);
		}
	}

	return texture;
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
	{
		if (!b->m_mapFolderToFile.empty())
		{
			b->m_mapFolderToFile.clear();
			gtk_widget_queue_draw(GTK_WIDGET(iconview));
		}
		return;
	}

	guint width, height;
	quiver_icon_view_get_icon_size(iconview,&width,&height);

	QuiverFile f = (*b->m_ImageListPtr)[cell];

	bool bClearMap = true;
	if (f.IsFolder())
	{
		/* If switching to a different folder, clear old preview mappings */
		if (!b->m_mapFolderToFile.empty() && b->m_mapFolderToFile.find(f.GetURI()) == b->m_mapFolderToFile.end())
		{
			b->m_mapFolderToFile.clear();
		}

		gint cx = 0, cy = 0;
		quiver_icon_view_get_cell_mouse_position(iconview, cell, &cx, &cy);

		if (0 <= cx && 0 <= cy && cx < (gint)width && cy < (gint)height)
		{
			double percent = double(cx) / width;
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
		if (!b->m_mapFolderToFile.empty())
		{
			b->m_mapFolderToFile.clear();
			quiver_icon_view_invalidate_cell(iconview, cell);
		}
	}
}

static void iconview_leave_notify(GtkEventControllerMotion *controller, gpointer user_data)
{
	(void)controller;
	Browser::BrowserImpl* b = (Browser::BrowserImpl*)user_data;
	if (!b->m_mapFolderToFile.empty())
	{
		b->m_mapFolderToFile.clear();
		if (b->m_pIconView)
		{
			gtk_widget_queue_draw(b->m_pIconView);
		}
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
{ (void)n_press;
	GtkWidget *widget = GTK_WIDGET(gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture)));
	guint button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));
	if (2 == button)
	{
		GAction *fs = QuiverUtils::GetAction("FullScreen");
		if (fs != NULL)
		{
			g_action_activate(fs, NULL);
		}
		return;
	}
	if (3 != button)
	{
		return;
	}

	Browser::BrowserImpl *b = static_cast<Browser::BrowserImpl*>(user_data);

	/* A right-click in the icon view's empty area opens the reduced
	 * empty-area menu (paste / undo delete) on release instead of acting on an
	 * item; one over an icon selects exactly that icon (deselecting the rest)
	 * so the menu actions act on the item that was clicked. */
	if (widget == GTK_WIDGET(b->m_pIconView))
	{
		gulong cell = quiver_icon_view_get_cell_for_xy(
			QUIVER_ICON_VIEW(widget), (gint)x, (gint)y);
		if (G_MAXULONG == cell)
		{
			b->m_bContextMenuPending = true;
			b->m_bContextMenuOnEmptyArea = true;
			return;
		}

		b->m_bContextMenuPending = true;
		b->m_bContextMenuOnEmptyArea = false;

		GList *sel = quiver_icon_view_get_selection(QUIVER_ICON_VIEW(widget));
		gboolean already = FALSE;
		for (const GList *it = sel; it != NULL; it = it->next)
		{
			if ((gulong)(uintptr_t)it->data == cell)
			{
				already = TRUE;
				break;
			}
		}
		if (!already)
		{
			GList *single = g_list_append(NULL, (gpointer)(uintptr_t)cell);
			quiver_icon_view_set_selection(QUIVER_ICON_VIEW(widget), single);
			g_list_free(single);
		}

		/* Pin the icon view's cursor to the right-clicked cell so the view's
		 * scroll-to-cursor bookkeeping (size-allocate / adjustment-changed /
		 * set_cursor_cell) targets the cell already under the pointer instead
		 * of snapping back toward a previously selected cell. */
		quiver_icon_view_set_cursor_cell_silent(QUIVER_ICON_VIEW(widget), cell);
		g_list_free(sel);
	}
}

static void browser_button_release_cb(GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data)
{ (void)n_press;
	Browser::BrowserImpl *pBrowserImpl = static_cast<Browser::BrowserImpl*>(user_data);
	GtkWidget *widget = GTK_WIDGET(gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture)));
	guint button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));
	if (3 != button)
		return;
	if (!pBrowserImpl->m_bContextMenuPending)
		return;
	pBrowserImpl->m_bContextMenuPending = false;

	browser_show_context_menu(widget, x, y, user_data);
}

static void browser_iconview_scroll_context_menu_cb(QuiverIconView *iconview, gulong cell, gpointer user_data)
{
	Browser::BrowserImpl *pBrowserImpl = static_cast<Browser::BrowserImpl*>(user_data);
	if (!pBrowserImpl || !pBrowserImpl->m_pIconView || GTK_WIDGET(iconview) != pBrowserImpl->m_pIconView)
		return;

	GdkRectangle rect;
	if (quiver_icon_view_get_cell_rect(iconview, cell, &rect))
	{
		int w = gtk_widget_get_width(pBrowserImpl->m_pIconView);
		int h = gtk_widget_get_height(pBrowserImpl->m_pIconView);
		gdouble x = rect.x + rect.width / 2.0;
		gdouble y = rect.y + rect.height / 2.0;
		if (w > 0 && h > 0)
		{
			x = std::clamp(x, 0.0, (double)w);
			y = std::clamp(y, 0.0, (double)h);
		}
		pBrowserImpl->m_bContextMenuOnEmptyArea = false;
		browser_show_context_menu(pBrowserImpl->m_pIconView, x, y, pBrowserImpl);
	}
	else
	{
		pBrowserImpl->m_bContextMenuOnEmptyArea = false;
		browser_show_context_menu(pBrowserImpl->m_pIconView, -1, -1, pBrowserImpl);
	}
}

static gboolean iconview_key_press_cb(GtkEventControllerKey *controller, guint keyval, guint keycode, GdkModifierType state, gpointer user_data)
{
	(void)controller;
	(void)keycode;
	Browser::BrowserImpl *pBrowserImpl = static_cast<Browser::BrowserImpl*>(user_data);
	if (!pBrowserImpl || !pBrowserImpl->m_pIconView)
		return FALSE;

	if (GDK_KEY_Menu == keyval || ((state & GDK_SHIFT_MASK) && (GDK_KEY_F10 == keyval)))
	{
		gulong cell = quiver_icon_view_get_cursor_cell(QUIVER_ICON_VIEW(pBrowserImpl->m_pIconView));
		gulong n_items = pBrowserImpl->m_ImageListPtr ? pBrowserImpl->m_ImageListPtr->GetSize() : 0;
		if (cell != G_MAXULONG && cell < n_items)
		{
			GList *sel = quiver_icon_view_get_selection(QUIVER_ICON_VIEW(pBrowserImpl->m_pIconView));
			if (NULL == sel)
			{
				GList *single = g_list_append(NULL, (gpointer)(uintptr_t)cell);
				quiver_icon_view_set_selection(QUIVER_ICON_VIEW(pBrowserImpl->m_pIconView), single);
				g_list_free(single);
			}
			else
			{
				g_list_free(sel);
			}
			pBrowserImpl->m_bContextMenuOnEmptyArea = false;

			if (quiver_icon_view_is_cell_visible(QUIVER_ICON_VIEW(pBrowserImpl->m_pIconView), cell))
			{
				GdkRectangle rect;
				if (quiver_icon_view_get_cell_rect(QUIVER_ICON_VIEW(pBrowserImpl->m_pIconView), cell, &rect))
				{
					int w = gtk_widget_get_width(pBrowserImpl->m_pIconView);
					int h = gtk_widget_get_height(pBrowserImpl->m_pIconView);
					gdouble x = rect.x + rect.width / 2.0;
					gdouble y = rect.y + rect.height / 2.0;
					if (w > 0 && h > 0)
					{
						x = std::clamp(x, 0.0, (double)w);
						y = std::clamp(y, 0.0, (double)h);
					}
					browser_show_context_menu(pBrowserImpl->m_pIconView, x, y, pBrowserImpl);
				}
				else
				{
					browser_show_context_menu(pBrowserImpl->m_pIconView, -1, -1, pBrowserImpl);
				}
			}
			else
			{
				/* Scroll to the cell (smoothly if enabled) and then show context menu on arrival */
				quiver_icon_view_scroll_to_cell_with_callback(
					QUIVER_ICON_VIEW(pBrowserImpl->m_pIconView),
					cell,
					browser_iconview_scroll_context_menu_cb,
					pBrowserImpl);
			}
		}
		else
		{
			pBrowserImpl->m_bContextMenuOnEmptyArea = true;
			browser_show_context_menu(pBrowserImpl->m_pIconView, -1, -1, pBrowserImpl);
		}
		return TRUE;
	}
	return FALSE;
}

static void browser_menu_item(GMenu *menu, const char *label, const char *action_name,
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

static void browser_show_context_menu(GtkWidget *widget, gdouble x, gdouble y, gpointer userdata)
{
	Browser::BrowserImpl *pBrowserImpl = (Browser::BrowserImpl*)userdata;

	/* Build a fresh popover on every show: a GTK popover that has been
	 * through a popup()/popdown() cycle refuses to map its surface again on
	 * this stack, so reuse (rather than recycle) it. */
	if (pBrowserImpl->m_pContextMenuPopover)
	{
		if (gtk_widget_get_parent(pBrowserImpl->m_pContextMenuPopover))
		{
			gtk_widget_unparent(pBrowserImpl->m_pContextMenuPopover);
		}
		pBrowserImpl->m_pContextMenuPopover = NULL;
	}

	const bool bTrash = pBrowserImpl->IsTrashMode();
	const bool bEmptyArea = pBrowserImpl->m_bContextMenuOnEmptyArea;

	/* The icon view selection and the internal clipboard define which items
	 * should be usable.  Update the enabled states so the model menu greys
	 * out what cannot run right now. */
	GList *selection = quiver_icon_view_get_selection(
		QUIVER_ICON_VIEW(pBrowserImpl->m_pIconView));
	const gboolean bHasSelection = (NULL != selection);
	const gboolean bHasClipboard = QuiverFileOps::ClipboardHasItems() ? TRUE : FALSE;
	{
		GAction *a;
		if (NULL != (a = QuiverUtils::GetAction(ACTION_BROWSER_COPY)))
			g_simple_action_set_enabled(G_SIMPLE_ACTION(a), bHasSelection);
		if (NULL != (a = QuiverUtils::GetAction(ACTION_BROWSER_CUT)))
			g_simple_action_set_enabled(G_SIMPLE_ACTION(a), bHasSelection ? (bTrash ? FALSE : TRUE) : FALSE);
		if (NULL != (a = QuiverUtils::GetAction(ACTION_BROWSER_PASTE)))
			g_simple_action_set_enabled(G_SIMPLE_ACTION(a), bHasClipboard);
		if (NULL != (a = QuiverUtils::GetAction(ACTION_BROWSER_NEW_FOLDER)))
		{
			std::list<std::string> dirs = pBrowserImpl->m_ImageListPtr->GetFolderList();
			bool canCreate = !bTrash && (dirs.size() == 1) && QuiverUtils::IsDirectoryURI(dirs.front().c_str());
			g_simple_action_set_enabled(G_SIMPLE_ACTION(a), canCreate);
		}
		if (NULL != (a = QuiverUtils::GetAction(ACTION_BROWSER_RENAME)))
			g_simple_action_set_enabled(G_SIMPLE_ACTION(a), bHasSelection && !bTrash);
		if (NULL != (a = QuiverUtils::GetAction(ACTION_BROWSER_TRASH)))
			g_simple_action_set_enabled(G_SIMPLE_ACTION(a), bHasSelection);
		if (NULL != (a = QuiverUtils::GetAction(ACTION_BROWSER_RESTORE)))
			g_simple_action_set_enabled(G_SIMPLE_ACTION(a), bHasSelection && bTrash);
	}
	const guint nSel = g_list_length(selection);

	/* Trash title row: the original file name + location of the (first)
	 * selected trash item. */
	bool bShowTitle = false;
	if (bTrash && nSel > 0 && selection != NULL)
	{
		guint item = (guint)(uintptr_t)selection->data;
		if (item < (guint)pBrowserImpl->m_ImageListPtr->GetSize())
		{
			QuiverFile f = (*pBrowserImpl->m_ImageListPtr)[item];
			char *orig = QuiverFileOps::GetTrashItemOrigPath(f.GetURI());
			if (NULL != orig)
			{
				char *name = g_path_get_basename(orig);
				char *dir  = g_path_get_dirname(orig);
				gchar *markup = g_markup_escape_text(name ? name : orig, -1);
				gtk_label_set_markup(GTK_LABEL(pBrowserImpl->m_pContextMenuTitleLabel), markup);
				g_free(markup);
				g_free(dir);
				g_free(name);
				g_free(orig);
				bShowTitle = true;
			}
		}
	}
	gtk_widget_set_visible(pBrowserImpl->m_pContextMenuTitleLabel, bShowTitle);
	const char *title_id = bShowTitle ? "browser-trash-title" : NULL;

	/* Rebuild the model: items depend on trash vs. normal browsing, and on
	 * whether the click came down on an item or the empty area. */
	GMenu *menu = g_menu_new();
	if (bEmptyArea)
	{
		if (!bTrash)
		{
			std::list<std::string> dirs = pBrowserImpl->m_ImageListPtr->GetFolderList();
			if (dirs.size() == 1 && QuiverUtils::IsDirectoryURI(dirs.front().c_str()))
			{
				browser_menu_item(menu, "New Folder", "quiver." ACTION_BROWSER_NEW_FOLDER,
					"<Primary><Shift>n", NULL, "folder-new-symbolic");
			}
		}
		/* Empty-area menu: "Undo Delete" only makes sense here and only when
		 * there actually is a deletion to undo. */
		if (bHasClipboard)
			browser_menu_item(menu, "Paste", "quiver." ACTION_BROWSER_PASTE,
				"<Control>v", NULL, "edit-paste-symbolic");
		if (QuiverFileOps::UndoStackHasItems() && NULL != QuiverUtils::GetAction("UndoDelete"))
			browser_menu_item(menu, "Undo Delete", "quiver.UndoDelete", "<Control>z", NULL, "edit-undo-symbolic");
	}
	else if (bTrash)
	{
		browser_menu_item(menu, "Copy", "quiver." ACTION_BROWSER_COPY,
			"<Control>c", title_id, "edit-copy-symbolic");
		GMenu *trash_section = g_menu_new();
		browser_menu_item(trash_section, "Delete Permanently", "quiver." ACTION_BROWSER_TRASH,
			"Delete", NULL, "edit-delete-symbolic");
		browser_menu_item(trash_section, "Restore From Trash", "quiver." ACTION_BROWSER_RESTORE,
			NULL, NULL, "edit-undo-symbolic");
		g_menu_append_section(menu, NULL, G_MENU_MODEL(trash_section));
		g_object_unref(trash_section);
	}
	else
	{
		browser_menu_item(menu, "Copy", "quiver." ACTION_BROWSER_COPY,
			"<Control>c", NULL, "edit-copy-symbolic");
		browser_menu_item(menu, "Cut", "quiver." ACTION_BROWSER_CUT,
			"<Control>x", NULL, "edit-cut-symbolic");
		if (QuiverFileOps::ClipboardHasItems())
			browser_menu_item(menu, "Paste", "quiver." ACTION_BROWSER_PASTE,
				"<Control>v", NULL, "edit-paste-symbolic");
		browser_menu_item(menu, "Rename", "quiver." ACTION_BROWSER_RENAME,
			"F2", NULL, "document-edit-symbolic");
		GMenu *actions_section = g_menu_new();
		browser_menu_item(actions_section, "Move To Trash", "quiver." ACTION_BROWSER_TRASH,
			"Delete", NULL, "user-trash-symbolic");
		g_menu_append_section(menu, NULL, G_MENU_MODEL(actions_section));
		g_object_unref(actions_section);
	}
	g_list_free(selection);

	pBrowserImpl->m_pContextMenuPopover = gtk_popover_menu_new_from_model(G_MENU_MODEL(menu));
	g_object_unref(menu);
	if (bShowTitle)
	{
		gtk_popover_menu_add_child(GTK_POPOVER_MENU(pBrowserImpl->m_pContextMenuPopover),
			pBrowserImpl->m_pContextMenuTitleLabel, "browser-trash-title");
	}
	gtk_widget_insert_action_group(pBrowserImpl->m_pContextMenuPopover, "quiver",
		G_ACTION_GROUP(QuiverUtils::GetActionGroup()));
	gtk_widget_set_parent(pBrowserImpl->m_pContextMenuPopover, widget);

	QuiverUtils::ShowContextMenuAt(
		GTK_POPOVER(pBrowserImpl->m_pContextMenuPopover), widget, x, y);
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
	if (pBrowserImpl->m_StatusbarPtr)
	{
		pBrowserImpl->m_StatusbarPtr->SetMagnification((int)(mag*100+.5));
	}
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

/* Modal "Permanently delete?" confirmation for trash browsing. */
static bool browser_confirm_permanent_delete(const std::string& strDlgText)
{
	return QuiverUtils::ConfirmDialog("Delete Permanently?",
		strDlgText, "Delete Permanently", "Cancel");
}

/* URIs of the currently selected icon-view items, newest first. */
static std::list<std::string> browser_selected_uris(Browser::BrowserImpl *b)
{
	std::list<std::string> uris;
	GList *selection = quiver_icon_view_get_selection(QUIVER_ICON_VIEW(b->m_pIconView));
	if (selection != NULL)
	{
		for (GList *it = selection; it != NULL; it = it->next)
		{
			guint item = (guint)(uintptr_t)it->data;
			if (item < (guint)b->m_ImageListPtr->GetSize())
				uris.push_back((*b->m_ImageListPtr)[item].GetURI());
		}
		g_list_free(selection);
	}
	return uris;
}

/* Paste / drop conflict resolution: prompt once per collision, remember the
 * choice when the user ticks "do this for all remaining conflicts". */
struct BrowserPasteConflictUI
{
	bool bApplyToAll = false;
	QuiverFileOps::PasteConflictAction lastAction = QuiverFileOps::PASTE_SKIP;
};

static QuiverFileOps::PasteConflictAction browser_paste_conflict_cb(
	const gchar *src_uri, const gchar *dest_uri, gpointer user_data)
{
	BrowserPasteConflictUI *ui = (BrowserPasteConflictUI*)user_data;
	if (ui->bApplyToAll)
		return ui->lastAction;
	QuiverFileOps::PasteConflictAction action =
		QuiverUtils::ResolvePasteConflict(src_uri, dest_uri, ui->bApplyToAll);
	ui->lastAction = action;
	return action;
}

/* Drag icon: creates a paintable snapshot of the selected icon view cells,
 * masking out unselected cells with transparency. */
static void browser_drag_source_begin(GtkDragSource *source, GdkDrag *drag, gpointer user_data)
{ (void)drag;
	Browser::BrowserImpl *b = (Browser::BrowserImpl*)user_data;
	gint hot_x = 0, hot_y = 0;
	GdkPaintable *icon = quiver_icon_view_create_drag_icon(
		QUIVER_ICON_VIEW(b->m_pIconView), &hot_x, &hot_y);
	if (NULL != icon)
	{
		gtk_drag_source_set_icon(source, icon, hot_x, hot_y);
		g_object_unref(icon);
	}
	else
	{
		gtk_drag_source_set_icon(source, NULL, 0, 0);
	}
}

/* Drag the whole selection out of the icon view.  Copy-only, so dropping
 * elsewhere can never destroy the sources (a MOVE would require the external
 * target to move files it only sees as URIs). */
static GdkContentProvider* browser_drag_source_prepare(GtkDragSource *source, gdouble x, gdouble y, gpointer user_data)
{ (void)source;
	Browser::BrowserImpl *b = (Browser::BrowserImpl*)user_data;
	QuiverIconView *iconview = QUIVER_ICON_VIEW(b->m_pIconView);
	gulong cell = quiver_icon_view_get_cell_for_xy(iconview, (gint)x, (gint)y);
	if (G_MAXULONG == cell)
		return NULL;
	GdkModifierType state = (GdkModifierType)0;
	GdkEvent *ev = gtk_event_controller_get_current_event(GTK_EVENT_CONTROLLER(source));
	if (ev != NULL)
		state = gdk_event_get_modifier_state(ev);
	GList *sel = quiver_icon_view_get_selection(iconview);
	gboolean bInSel = FALSE;
	for (const GList *it = sel; it != NULL; it = it->next)
	{
		if ((gulong)(uintptr_t)it->data == cell)
		{
			bInSel = TRUE;
			break;
		}
	}
	if (!bInSel)
	{
		GList *subject = NULL;
		if (0 == (state & GDK_CONTROL_MASK))
			subject = g_list_append(NULL, (gpointer)(uintptr_t)cell);
		else
			subject = g_list_append(g_list_copy(sel), (gpointer)(uintptr_t)cell);
		quiver_icon_view_set_selection(iconview, subject);
		g_list_free(subject);
	}
	g_list_free(sel);
	std::list<std::string> uris = browser_selected_uris(b);
	if (uris.empty())
		return NULL;
	/* Alt-drag offers plain file-path text only (no file list), so drops
	 * into text editors/terminals insert the paths instead of the target
	 * treating the drag as files to open. */
	bool bCutDrag = (0 == (state & GDK_CONTROL_MASK));
	return QuiverClipboard::MakeContentProvider(uris, bCutDrag,
		0 != (state & GDK_ALT_MASK));
}

static gboolean browser_drop_motion_cb(GtkDropTarget *target, gdouble x, gdouble y, gpointer user_data)
{
	Browser::BrowserImpl *b = (Browser::BrowserImpl*)user_data;
	GdkDrop *drop = gtk_drop_target_get_current_drop(target);
	if (drop == NULL || b == NULL || !b->m_pIconView || !QUIVER_IS_ICON_VIEW(b->m_pIconView))
		return GDK_EVENT_PROPAGATE;

	gulong cell = quiver_icon_view_get_cell_for_xy(QUIVER_ICON_VIEW(b->m_pIconView), (gint)x, (gint)y);
	if (cell == G_MAXULONG || !b->m_ImageListPtr || cell >= b->m_ImageListPtr->GetSize())
	{
		quiver_icon_view_set_drop_cell(QUIVER_ICON_VIEW(b->m_pIconView), G_MAXULONG);
		gdk_drop_status(drop, (GdkDragAction)0, (GdkDragAction)0);
		return GDK_EVENT_PROPAGATE;
	}

	QuiverFile f = (*b->m_ImageListPtr)[cell];
	if (!f.IsFolder())
	{
		quiver_icon_view_set_drop_cell(QUIVER_ICON_VIEW(b->m_pIconView), G_MAXULONG);
		gdk_drop_status(drop, (GdkDragAction)0, (GdkDragAction)0);
		return GDK_EVENT_PROPAGATE;
	}

	quiver_icon_view_set_drop_cell(QUIVER_ICON_VIEW(b->m_pIconView), cell);
	GdkDragAction actions = gdk_drop_get_actions(drop);
	GdkDragAction chosen = (actions & GDK_ACTION_MOVE) ? GDK_ACTION_MOVE : GDK_ACTION_COPY;
	b->m_eDropAction = chosen;
	gdk_drop_status(drop, chosen, chosen);
	return GDK_EVENT_STOP;
}

static void browser_drop_leave_cb(GtkDropTarget *target, gpointer user_data)
{
	(void)target;
	Browser::BrowserImpl *b = (Browser::BrowserImpl*)user_data;
	if (b != NULL && b->m_pIconView && QUIVER_IS_ICON_VIEW(b->m_pIconView))
	{
		quiver_icon_view_set_drop_cell(QUIVER_ICON_VIEW(b->m_pIconView), G_MAXULONG);
	}
}

struct BrowserDropData
{
	Browser::BrowserImpl *browser;
	std::shared_ptr<bool> alive;
	std::list<std::string> uris;
	std::string target_uri;
	bool bMove;
};

static gboolean browser_drop_idle_cb(gpointer user_data)
{
	BrowserDropData *data = static_cast<BrowserDropData*>(user_data);
	if (!data->alive || !*data->alive)
	{
		delete data;
		return G_SOURCE_REMOVE;
	}

	BrowserPasteConflictUI ui;
	std::list<std::string> moved;
	int transferred = QuiverFileOps::TransferFiles(data->uris, data->bMove, data->target_uri.c_str(),
		browser_paste_conflict_cb, &ui, &moved);
	if (data->bMove)
		QuiverFileOps::ClipboardRemoveURIs(moved);

	if (transferred > 0 && data->alive && *data->alive)
	{
		data->browser->m_ImageListPtr->Reload();
		data->browser->m_ThumbnailCache.Clear();
		data->browser->m_ThumbnailLoader.UpdateList(true);
		if (data->browser->m_StatusbarPtr)
		{
			gchar msg[64];
			g_snprintf(msg, sizeof(msg), "%s %d file(s)",
				data->bMove ? "Moved" : "Copied", transferred);
			data->browser->m_StatusbarPtr->PushText(msg);
		}
	}

	delete data;
	return G_SOURCE_REMOVE;
}

static gboolean browser_drop_cb(GtkDropTarget *target, const GValue *value, gdouble x, gdouble y, gpointer user_data)
{
	Browser::BrowserImpl *b = (Browser::BrowserImpl*)user_data;
	if (b != NULL && b->m_pIconView && QUIVER_IS_ICON_VIEW(b->m_pIconView))
	{
		quiver_icon_view_set_drop_cell(QUIVER_ICON_VIEW(b->m_pIconView), G_MAXULONG);
	}
	if (value == NULL || !G_VALUE_HOLDS_STRING(value) || b == NULL || !b->m_pIconView || !QUIVER_IS_ICON_VIEW(b->m_pIconView))
		return GDK_EVENT_PROPAGATE;

	gulong cell = quiver_icon_view_get_cell_for_xy(QUIVER_ICON_VIEW(b->m_pIconView), (gint)x, (gint)y);
	if (cell == G_MAXULONG || !b->m_ImageListPtr || cell >= b->m_ImageListPtr->GetSize())
		return GDK_EVENT_PROPAGATE;

	QuiverFile target_file = (*b->m_ImageListPtr)[cell];
	if (!target_file.IsFolder())
		return GDK_EVENT_PROPAGATE;

	const char *target_uri = target_file.GetURI();
	if (target_uri == NULL || '\0' == target_uri[0])
		return GDK_EVENT_PROPAGATE;

	std::list<std::string> uris;
	bool bCut = false;
	const char *text = g_value_get_string(value);
	if (!QuiverClipboard::ParseClipboardText(text ? text : "", uris, bCut) || uris.empty())
		return GDK_EVENT_PROPAGATE;

	for (const auto &u : uris)
	{
		if (u == target_uri)
			return GDK_EVENT_PROPAGATE;
	}

	/* Default to MOVE unless explicit copy was requested (e.g. Ctrl held) */
	GdkDrop *drop = gtk_drop_target_get_current_drop(target);
	bool bMove = true;
	if (drop != NULL)
	{
		GdkDragAction actions = gdk_drop_get_actions(drop);
		if (actions == GDK_ACTION_COPY && !bCut)
			bMove = false;
		else if (b->m_eDropAction == GDK_ACTION_COPY && !bCut)
			bMove = false;
	}
	else
		bMove = bCut;

	/* Defer the file transfer to an idle callback so the drop event finishes
	 * in GTK immediately. Otherwise, resolving paste conflicts with a modal
	 * dialog inside the drop callback spins a nested main loop while the drop
	 * is active, causing gtk_drop_begin_event assertion failure. */
	BrowserDropData *data = new BrowserDropData();
	data->browser = b;
	data->alive = b->m_spAlive;
	data->uris = uris;
	data->target_uri = target_uri;
	data->bMove = bMove;
	g_idle_add(browser_drop_idle_cb, data);

	return GDK_EVENT_STOP;
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
			gtk_widget_set_visible(pBrowserImpl->vpaned, TRUE);
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
		/* Feed both the system clipboard (uri-list / gnome-copied-files /
		 * plain text, so file managers AND text editors can paste the URLs)
		 * and the internal quiver clipboard (so Paste inside quiver can
		 * duplicate files). */
		std::list<std::string> uris = browser_selected_uris(pBrowserImpl);
		if (uris.empty())
			return;
		QuiverFileOps::ClipboardSet(uris, false);
		QuiverClipboard::SetClipboard(uris, false);
		if (pBrowserImpl->m_StatusbarPtr)
			pBrowserImpl->m_StatusbarPtr->PushText("Copied to clipboard");
	}
	else if (0 == strcmp(szAction,ACTION_BROWSER_CUT))
	{
		std::list<std::string> uris = browser_selected_uris(pBrowserImpl);
		if (uris.empty())
			return;
		QuiverFileOps::ClipboardSet(uris, true);
		QuiverClipboard::SetClipboard(uris, true);
		if (pBrowserImpl->m_StatusbarPtr)
			pBrowserImpl->m_StatusbarPtr->PushText("Cut to clipboard");
	}
	else if (0 == strcmp(szAction,ACTION_BROWSER_PASTE))
	{
		std::list<std::string> dirs = pBrowserImpl->m_ImageListPtr->GetFolderList();
		if (1 != dirs.size())
			return; /* single-folder view only */

		/* The internal clipboard is authoritative for in-app cut/copy.  When
		 * it is empty, best-effort read of the system clipboard so files
		 * copied in an external file manager can be pasted here too. */
		std::list<std::string> uris = *QuiverFileOps::ClipboardGetUris();
		bool bCut = QuiverFileOps::ClipboardIsCut();
		if (uris.empty())
		{
			if (!QuiverClipboard::GetClipboardUris(uris, bCut))
				return;
			bCut = false; /* cut state is only trustworthy on the internal clipboard */
		}

		BrowserPasteConflictUI ui;
		std::list<std::string> moved;
		int transferred = QuiverFileOps::TransferFiles(uris, bCut,
			dirs.front().c_str(), browser_paste_conflict_cb, &ui, &moved);
		if (bCut)
			QuiverFileOps::ClipboardRemoveURIs(moved);
		if (transferred <= 0)
			return;

		pBrowserImpl->m_ImageListPtr->Reload();
		pBrowserImpl->m_ThumbnailCache.Clear();
		pBrowserImpl->m_ThumbnailLoader.UpdateList(true);
		if (pBrowserImpl->m_StatusbarPtr)
		{
			gchar msg[64];
			g_snprintf(msg, sizeof(msg), "%s %d file(s)",
				bCut ? "Moved" : "Copied", transferred);
			pBrowserImpl->m_StatusbarPtr->PushText(msg);
		}
	}
	else if (0 == strcmp(szAction, ACTION_BROWSER_NEW_FOLDER))
	{
		std::list<std::string> dirs = pBrowserImpl->m_ImageListPtr->GetFolderList();
		if (dirs.size() != 1 || pBrowserImpl->IsTrashMode() || !QuiverUtils::IsDirectoryURI(dirs.front().c_str()))
			return;

		GFile *parent = QuiverUtils::FileFromURIOrPath(dirs.front().c_str());
		if (NULL == parent)
			return;

		std::string initial_name = QuiverUtils::GetUniqueFolderName(parent, "New Folder");
		char *folder_name = QuiverUtils::PromptForString("New Folder", "Folder name:", initial_name.c_str(), "Create");
		if (NULL == folder_name || '\0' == folder_name[0])
		{
			if (folder_name)
				g_free(folder_name);
			g_object_unref(parent);
			return;
		}

		GFile *child = g_file_get_child(parent, folder_name);
		GError *error = NULL;
		gboolean ok = g_file_make_directory(child, NULL, &error);
		if (!ok)
		{
			QuiverUtils::ConfirmDialog("Create Folder Failed",
				error && error->message ? error->message : "The folder could not be created.",
				"OK", "Close");
			if (error)
				g_error_free(error);
		}
		else
		{
			char *child_uri = g_file_get_uri(child);
			pBrowserImpl->m_ImageListPtr->Reload();
			pBrowserImpl->m_ThumbnailCache.Clear();
			pBrowserImpl->m_ThumbnailLoader.UpdateList(true);

			if (NULL != child_uri)
			{
				pBrowserImpl->m_ImageListPtr->SetCurrentFile(child_uri);
				guint idx = pBrowserImpl->m_ImageListPtr->GetCurrentIndex();
				quiver_icon_view_set_cursor_cell(
					QUIVER_ICON_VIEW(pBrowserImpl->m_pIconView), idx);
				GList *single = g_list_append(NULL, (gpointer)(uintptr_t)idx);
				quiver_icon_view_set_selection(
					QUIVER_ICON_VIEW(pBrowserImpl->m_pIconView), single);
				g_list_free(single);

				pBrowserImpl->m_BrowserHistory.SetCurrentSelected(child_uri);

				if (pBrowserImpl->m_FolderTreePtr)
				{
					char *parent_uri = g_file_get_uri(parent);
					pBrowserImpl->m_FolderTreePtr->AddChildFolder(
						parent_uri ? parent_uri : dirs.front().c_str(),
						child_uri, folder_name);
					g_free(parent_uri);
				}
				QuiverFileOps::UndoStackRecordNewFolder(child_uri);
				g_free(child_uri);
			}
		}
		g_object_unref(child);
		g_object_unref(parent);
		g_free(folder_name);
	}
	else if (0 == strcmp(szAction,ACTION_BROWSER_RENAME))
	{
		GList *selection = quiver_icon_view_get_selection(QUIVER_ICON_VIEW(pBrowserImpl->m_pIconView));
		if (NULL == selection)
			return;

		const guint nSelected = g_list_length(selection);
		if (0 == nSelected)
		{
			g_list_free(selection);
			return;
		}

		/* Multi-select: hand the batch over to the rename task dialog
		 * (template + numbering), not the single-item prompt. */
		if (nSelected > 1)
		{
			std::vector<QuiverFile> files;
			for (const GList *it = selection; NULL != it; it = it->next)
			{
				guint idx = (guint)(uintptr_t)it->data;
				if (idx < (guint)pBrowserImpl->m_ImageListPtr->GetSize())
					files.push_back((*pBrowserImpl->m_ImageListPtr)[idx]);
			}
			g_list_free(selection);
			if (files.empty())
				return;

			/* Rename exactly the selected items; each keeps its own folder.
			 * A parent-folder default is preset so the dialog's Folder mode
			 * works, and a folder-first selection falls back to renaming
			 * the whole folder containing the first item. */
			RenameDlg dlg;
			if (!files.empty())
			{
				const gchar* uri0 = files.front().GetURI();
				if (NULL != uri0)
				{
					GFile* file = g_file_new_for_uri(uri0);
					GFile* parent = g_file_get_parent(file);
					g_object_unref(file);
					if (NULL != parent)
					{
						gchar* parent_uri = g_file_get_uri(parent);
						if (NULL != parent_uri)
						{
							dlg.SetInputFolder(parent_uri);
							g_free(parent_uri);
						}
						g_object_unref(parent);
					}
				}
			}
			dlg.SetFiles(files);

			if (dlg.Run())
			{
				RenameTaskPtr renameTaskPtr(new RenameTask());
				renameTaskPtr->SetTemplate(dlg.GetTemplate());
				if (dlg.GetFilesMode())
				{
					renameTaskPtr->AddFiles(dlg.GetFiles());
				}
				else
				{
					renameTaskPtr->SetInputFolder(dlg.GetInputFolder());
				}
				TaskManager::GetInstance()->AddTask(renameTaskPtr);
			}
			return;
		}

		guint item = (guint)(uintptr_t)selection->data;
		g_list_free(selection);
		if (item >= (guint)pBrowserImpl->m_ImageListPtr->GetSize())
			return;

		QuiverFile f = (*pBrowserImpl->m_ImageListPtr)[item];
		char *old_name = g_path_get_basename(f.GetURI());
		char *new_name = QuiverUtils::PromptForString(
			"Rename", "Enter the new name for this item:", old_name);

		if (NULL == new_name)
		{
			g_free(old_name);
			return;
		}

		gboolean renamed = FALSE;
		char *new_uri = NULL;
		GFile *src = g_file_new_for_uri(f.GetURI());
		GError *error = NULL;
		GFile *dst = g_file_set_display_name(src, new_name, NULL, &error);
		if (NULL == dst)
		{
			QuiverUtils::ConfirmDialog("Rename failed",
				error && error->message ? error->message
					: "The item could not be renamed.",
				"OK", "Close");
			if (error)
				g_error_free(error);
		}
		else
		{
			renamed = TRUE;
			new_uri = g_file_get_uri(dst);
			g_object_unref(dst);
		}
		g_object_unref(src);
		g_free(new_name);
		g_free(old_name);

		if (renamed)
		{
			pBrowserImpl->m_ImageListPtr->Reload();
			pBrowserImpl->m_ThumbnailCache.Clear();
			pBrowserImpl->m_ThumbnailLoader.UpdateList(true);

			if (NULL != new_uri)
			{
				/* Position the list on the renamed item.  SetCurrentFile
				 * refreshes the view downstream when the rename shifted the
				 * item's index, but a rename that keeps the list order fires
				 * nothing, so re-seat the cursor cell and the selection
				 * explicitly under the item's new name. */
				pBrowserImpl->m_ImageListPtr->SetCurrentFile(new_uri);

				guint idx = pBrowserImpl->m_ImageListPtr->GetCurrentIndex();
				quiver_icon_view_set_cursor_cell(
					QUIVER_ICON_VIEW(pBrowserImpl->m_pIconView), idx);
				GList *single = g_list_append(NULL, (gpointer)(uintptr_t)idx);
				quiver_icon_view_set_selection(
					QUIVER_ICON_VIEW(pBrowserImpl->m_pIconView), single);
				g_list_free(single);

				pBrowserImpl->m_BrowserHistory.SetCurrentSelected(new_uri);
				g_free(new_uri);
			}
		}
	}
	else if (0 == strcmp(szAction, ACTION_BROWSER_TRASH)
			|| 0 == strcmp(szAction, ACTION_BROWSER_TRASH_FORCE))
	{
		bool bForce = (0 == strcmp(szAction, ACTION_BROWSER_TRASH_FORCE));

		GList *selection;
		selection = quiver_icon_view_get_selection(QUIVER_ICON_VIEW(pBrowserImpl->m_pIconView));
		set<int> items;
		if (NULL != selection)
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

		if (0 == items.size())
		{
			// nothing to delete
		}
		else if (pBrowserImpl->IsTrashMode())
		{
			/* Browsing the trash: Delete removes files for good.  Confirm
			 * unless the user held Shift (ACTION_BROWSER_TRASH_FORCE). */
			bool bProceed = bForce;
			if (!bProceed)
			{
				std::string strDlgText = (1 == items.size())
					? "Permanently delete this item from the trash?"
					: "Permanently delete these items from the trash?";
				bProceed = browser_confirm_permanent_delete(strDlgText);
			}

			if (bProceed)
			{
				pBrowserImpl->m_ImageListPtr->BlockHandler(pBrowserImpl->m_ImageListEventHandlerPtr);
				for (set<int>::reverse_iterator ritr = items.rbegin(); items.rend() != ritr; ++ritr)
				{
					QuiverFile f = (*pBrowserImpl->m_ImageListPtr)[*ritr];
					if (QuiverFileOps::PermanentlyDeleteTrashItem(f))
					{
						pBrowserImpl->m_ImageListPtr->Remove(*ritr);
					}
				}
				pBrowserImpl->m_ImageListPtr->UnblockHandler(pBrowserImpl->m_ImageListEventHandlerPtr);
				quiver_icon_view_set_cursor_cell(QUIVER_ICON_VIEW(pBrowserImpl->m_pIconView), pBrowserImpl->m_ImageListPtr->GetCurrentIndex());
				pBrowserImpl->m_ThumbnailLoader.UpdateList(true);
			}
		}
		else
		{
			/* Normal folder: move to trash without asking, then record the
			 * batch so Ctrl+Z / the undo button can restore it. */
			std::list<QuiverFile> trashed;

			pBrowserImpl->m_ImageListPtr->BlockHandler(pBrowserImpl->m_ImageListEventHandlerPtr);

			for (set<int>::reverse_iterator ritr = items.rbegin(); items.rend() != ritr; ++ritr)
			{
				QuiverFile f = (*pBrowserImpl->m_ImageListPtr)[*ritr];

				if (QuiverFileOps::MoveToTrash(f))
				{
					trashed.push_back(f);
					pBrowserImpl->m_ImageListPtr->Remove(*ritr);
				}
			}

			pBrowserImpl->m_ImageListPtr->UnblockHandler(pBrowserImpl->m_ImageListEventHandlerPtr);

			QuiverFileOps::UndoStackRecord(trashed);

			quiver_icon_view_set_cursor_cell(QUIVER_ICON_VIEW(pBrowserImpl->m_pIconView), pBrowserImpl->m_ImageListPtr->GetCurrentIndex());

			pBrowserImpl->m_ThumbnailLoader.UpdateList(true);
		}
	}
	else if (0 == strcmp(szAction, ACTION_BROWSER_RESTORE))
	{
		if (!pBrowserImpl->IsTrashMode())
		{
			/* 'r' in a normal folder is reserved; restore only makes sense
			 * while browsing the trash. */
			return;
		}

		GList *selection;
		selection = quiver_icon_view_get_selection(QUIVER_ICON_VIEW(pBrowserImpl->m_pIconView));
		set<int> items;
		if (NULL != selection)
		{
			GList *sel_itr = selection;
			while (NULL != sel_itr)
			{
				items.insert((uintptr_t)sel_itr->data);
				sel_itr = g_list_next(sel_itr);
			}
			g_list_free(selection);
		}

		if (0 == items.size())
		{
			return;
		}

		pBrowserImpl->m_ImageListPtr->BlockHandler(pBrowserImpl->m_ImageListEventHandlerPtr);
		for (set<int>::reverse_iterator ritr = items.rbegin(); items.rend() != ritr; ++ritr)
		{
			QuiverFile f = (*pBrowserImpl->m_ImageListPtr)[*ritr];
			if (QuiverFileOps::RestoreTrashItem(f))
			{
				pBrowserImpl->m_ImageListPtr->Remove(*ritr);
			}
		}
		pBrowserImpl->m_ImageListPtr->UnblockHandler(pBrowserImpl->m_ImageListEventHandlerPtr);
		quiver_icon_view_set_cursor_cell(QUIVER_ICON_VIEW(pBrowserImpl->m_pIconView), pBrowserImpl->m_ImageListPtr->GetCurrentIndex());
		pBrowserImpl->m_ThumbnailLoader.UpdateList(true);
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

	/* 'r' restores from trash only while browsing the trash; outside of it
	 * the reader's rotate action owns the plain-r key. */
	GAction *restoreAction = QuiverUtils::GetAction(ACTION_BROWSER_RESTORE);
	if (NULL != restoreAction && G_IS_SIMPLE_ACTION(restoreAction))
	{
		g_simple_action_set_enabled(G_SIMPLE_ACTION(restoreAction), parent->IsTrashMode());
	}

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
	if (0 != parent->m_ImageListPtr->GetSize())
	{
		parent->SetImageIndex(parent->m_ImageListPtr->GetCurrentIndex(),true);
	}
			
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

	parent->m_ThumbnailCache.RemoveTexture(f.GetURI());
		
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
				QuiverUtils::SetWidgetBgColor(parent->m_pIconView, NULL);
				QuiverUtils::SetWidgetBgColor(parent->m_pImageView, NULL);
			}
			else
			{
				string strBGColorThumb = prefsPtr->GetString(QUIVER_PREFS_APP,QUIVER_PREFS_APP_BG_ICONVIEW, "#444");						
				string strBGColorImg   = prefsPtr->GetString(QUIVER_PREFS_APP,QUIVER_PREFS_APP_BG_IMAGEVIEW, "#000");
				GdkRGBA color;
				if (gdk_rgba_parse(&color, strBGColorThumb.c_str()))
					QuiverUtils::SetWidgetBgColor(parent->m_pIconView, &color);
				if (gdk_rgba_parse(&color, strBGColorImg.c_str()))
					QuiverUtils::SetWidgetBgColor(parent->m_pImageView, &color);
			}
		}
		else if (QUIVER_PREFS_APP_BG_IMAGEVIEW == event->GetKey() )
		{
			if ( !prefsPtr->GetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_USE_THEME_COLOR,true) )
			{
				string strBGColorImg = prefsPtr->GetString(QUIVER_PREFS_APP,QUIVER_PREFS_APP_BG_IMAGEVIEW, "#000");
				GdkRGBA color;
				if (gdk_rgba_parse(&color, strBGColorImg.c_str()))
					QuiverUtils::SetWidgetBgColor(parent->m_pImageView, &color);
			}
		}
		else if (QUIVER_PREFS_APP_BG_ICONVIEW == event->GetKey() )
		{
			if ( !prefsPtr->GetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_USE_THEME_COLOR,true) )
			{
				string strBGColorThumb = prefsPtr->GetString(QUIVER_PREFS_APP,QUIVER_PREFS_APP_BG_ICONVIEW, "#444");
				GdkRGBA color;
				if (gdk_rgba_parse(&color, strBGColorThumb.c_str()))
					QuiverUtils::SetWidgetBgColor(parent->m_pIconView, &color);
			}
		}
		else if (QUIVER_PREFS_APP_WINDOW_FULLSCREEN == event->GetKey() )
		{
			parent->UpdateUI();
		}
	}
	else if ( QUIVER_PREFS_BROWSER == event->GetSection() )
	{
		if (QUIVER_PREFS_BROWSER_THUMBS_SQUARE == event->GetKey() )
		{
			quiver_icon_view_set_thumbnails_square(QUIVER_ICON_VIEW(parent->m_pIconView), event->GetNewBoolean());
			parent->m_ThumbnailLoader.UpdateList(true);
		}
		else if (QUIVER_PREFS_BROWSER_THUMBS_FILMSTRIP == event->GetKey() )
		{
			quiver_icon_view_set_filmstrip_enabled(QUIVER_ICON_VIEW(parent->m_pIconView), event->GetNewBoolean());
			quiver_icon_view_invalidate_window(QUIVER_ICON_VIEW(parent->m_pIconView));
		}
	}
}


void Browser::BrowserImpl::FolderTreeEventHandler::HandleSelectionChanged(FolderTreeEventPtr event)
{
	parent->m_bFolderTreeEvent = true;

	if (event && event->HasBookmarkData())
	{
		const std::list<std::string>& uris = event->GetURIs();
		// landing on the first item mirrors the bookmark menu behavior
		parent->m_ImageListPtr->UpdateImageListAsync(&uris, event->GetRecursive(), true);
		return;
	}

	list<string> listFolders = parent->m_FolderTreePtr->GetSelectedFolders();
	parent->m_ImageListPtr->UpdateImageListAsync(&listFolders, false, true);
}




struct BrowserThumbLoaderSyncData {
	GtkWidget* iconview;
	Statusbar* statusbar;
	gulong index;
	bool is_running;
	std::weak_ptr<bool> aliveToken;
};

static gboolean idle_invalidate_cell(gpointer data) {
	BrowserThumbLoaderSyncData* pData = (BrowserThumbLoaderSyncData*)data;
	auto alive = pData->aliveToken.lock();
	if (alive && *alive && pData->iconview && QUIVER_IS_ICON_VIEW(pData->iconview))
	{
		quiver_icon_view_invalidate_cell(QUIVER_ICON_VIEW(pData->iconview), pData->index);
	}
	delete pData;
	return G_SOURCE_REMOVE;
}

static gboolean idle_set_is_running(gpointer data) {
	BrowserThumbLoaderSyncData* pData = (BrowserThumbLoaderSyncData*)data;
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

void Browser::BrowserImpl::BrowserThumbLoader::LoadThumbnail(const ThumbLoaderItem &item, guint uiWidth, guint uiHeight)
{
	if (IsStopped() || !m_spAlive || !*m_spAlive || !m_pBrowserImpl)
		return;

	bool is_mapped = m_bMapped.load(std::memory_order_relaxed);

	if (is_mapped && m_pBrowserImpl->m_ImageListPtr && item.m_ulIndex < m_pBrowserImpl->m_ImageListPtr->GetSize())
	{
		QuiverFile f((*m_pBrowserImpl->m_ImageListPtr)[item.m_ulIndex]);
		if (NULL == f.GetURI())
			return;

		bool bSquare = Preferences::GetInstance()->GetBoolean(QUIVER_PREFS_BROWSER,QUIVER_PREFS_BROWSER_THUMBS_SQUARE, false);

		/* In square mode the icon view center-crops thumbnails at draw time, so
		 * fetch and cache an extra 2x source to keep that crop sharp for images
		 * whose aspect ratio is far from the square cell.  Thumbnails always
		 * stay proportional (aspect-ratio preserving), both here and in the
		 * freedesktop.org thumbnail cache. */
		guint uiLoadW = bSquare ? uiWidth * 2 : uiWidth;
		guint uiLoadH = bSquare ? uiHeight * 2 : uiHeight;

		GdkTexture *texture = NULL;
		texture = m_pBrowserImpl->m_ThumbnailCache.GetTexture(f.GetURI());				
	
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
			
			quiver_rect_get_bound_size(uiLoadW,uiLoadH, &bound_width,&bound_height,FALSE);
			if (thumb_width != bound_width || thumb_height != bound_height)
			{
				// need a new thumbnail because the current cached size
				// is not the same as the size needed
				g_object_unref(texture);
				texture = NULL;
			}
				
		}

		if (NULL == texture)
		{
			guint iMaxSide = std::max(uiLoadW,uiLoadH);
			texture = f.GetThumbnailTexture(iMaxSide);
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

			quiver_rect_get_bound_size(uiLoadW,uiLoadH, &bound_width,&bound_height,FALSE);

			if (bound_width > 0 && bound_height > 0 && (thumb_width != bound_width || thumb_height != bound_height))
			{
				GdkTexture* scaled = QuiverUtils::ScaleTexture(texture, bound_width, bound_height);
				g_object_unref(texture);
				texture = scaled;
			}

			if (NULL != texture)
			{
				m_pBrowserImpl->m_ThumbnailCache.AddTexture(f.GetURI(), texture);
				g_object_unref(texture);
			}

			BrowserThumbLoaderSyncData* pInvData = new BrowserThumbLoaderSyncData();
			pInvData->iconview = m_pBrowserImpl->m_pIconView;
			pInvData->index = item.m_ulIndex;
			pInvData->aliveToken = m_spAlive;
			if (!ThreadUtil::IsGUIThread()) { g_idle_add_full(G_PRIORITY_HIGH, idle_invalidate_cell, pInvData, NULL); } else { idle_invalidate_cell(pInvData); }
		}
	}
}

void Browser::BrowserImpl::BrowserThumbLoader::GetVisibleRange(gulong* pulStart, gulong* pulEnd)
{
	if (pulStart) *pulStart = 0;
	if (pulEnd) *pulEnd = 0;
	if (IsStopped() || !m_spAlive || !*m_spAlive || !m_pBrowserImpl)
	{
		return;
	}
	if (m_pBrowserImpl->m_pIconView && QUIVER_IS_ICON_VIEW(m_pBrowserImpl->m_pIconView))
	{
		quiver_icon_view_get_visible_range(QUIVER_ICON_VIEW(m_pBrowserImpl->m_pIconView), pulStart, pulEnd);
	}
}

void Browser::BrowserImpl::BrowserThumbLoader::GetIconSize(guint* puiWidth, guint* puiHeight)
{
	if (puiWidth) *puiWidth = m_uiThumbWidth.load(std::memory_order_relaxed);
	if (puiHeight) *puiHeight = m_uiThumbHeight.load(std::memory_order_relaxed);
}

gulong Browser::BrowserImpl::BrowserThumbLoader::GetNumItems()
{
	if (IsStopped() || !m_spAlive || !*m_spAlive || !m_pBrowserImpl || !m_pBrowserImpl->m_ImageListPtr)
		return 0;
	return m_pBrowserImpl->m_ImageListPtr->GetSize();
}

QuiverFile Browser::BrowserImpl::BrowserThumbLoader::GetQuiverFile(gulong index)
{
	if (IsStopped() || !m_spAlive || !*m_spAlive || !m_pBrowserImpl || !m_pBrowserImpl->m_ImageListPtr)
		return QuiverFile();
	if (index < m_pBrowserImpl->m_ImageListPtr->GetSize())
	{
		return (*m_pBrowserImpl->m_ImageListPtr)[index];
	}
	return QuiverFile();
}

void Browser::BrowserImpl::BrowserThumbLoader::SetIsRunning(bool bIsRunning)
{
	if (IsStopped() || !m_spAlive || !*m_spAlive || !m_pBrowserImpl)
		return;
	BrowserThumbLoaderSyncData* pData = new BrowserThumbLoaderSyncData();
	pData->statusbar = m_pBrowserImpl->m_StatusbarPtr.get();
	pData->is_running = bIsRunning;
	pData->aliveToken = m_spAlive;
	if (!ThreadUtil::IsGUIThread()) { g_idle_add_full(G_PRIORITY_HIGH, idle_set_is_running, pData, NULL); } else { idle_set_is_running(pData); }
}

void Browser::BrowserImpl::BrowserThumbLoader::SetCacheSize(guint uiCacheSize)
{
	if (IsStopped() || !m_spAlive || !*m_spAlive || !m_pBrowserImpl)
		return;
	m_pBrowserImpl->m_ThumbnailCache.SetSize(uiCacheSize);
}

