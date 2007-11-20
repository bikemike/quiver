#include <config.h>

#include <pthread.h>

#include <gtk/gtk.h>
#include <string.h>
#include <list>
#include <map>
#include <set>

using namespace std;

#include <libquiver/quiver-icon-view.h>
#include <libquiver/quiver-image-view.h>
#include <libquiver/quiver-pixbuf-utils.h>

#include "Browser.h"
#include "FolderTree.h"
#include "ImageList.h"
#include "ImageCache.h"
#include "ImageLoader.h"
#include "IPixbufLoaderObserver.h"
#include "QuiverUtils.h"
#include "QuiverPrefs.h"
#include "QuiverFileOps.h"

#include "Statusbar.h"

#include "IImageListEventHandler.h"
#include "IPreferencesEventHandler.h"
#include "IFolderTreeEventHandler.h"
#include "IconViewThumbLoader.h"

#ifdef QUIVER_MAEMO
#ifdef HAVE_HILDON_1
#include <hildon/hildon-controlbar.h>
#else
#include <hildon-widgets/hildon-controlbar.h>
#endif
#include <math.h>
#endif


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
			gdk_threads_enter();
			quiver_image_view_set_pixbuf(m_pImageView,pixbuf);
			gdk_threads_leave();
		};
	virtual void SetPixbufAtSize(GdkPixbuf *pixbuf, gint width, gint height, bool bResetViewMode = true ){
		gdk_threads_enter();
		gboolean bReset = FALSE;
		if (bResetViewMode) bReset = TRUE;
		quiver_image_view_set_pixbuf_at_size_ex(m_pImageView,pixbuf,width,height,bReset);
		gdk_threads_leave();
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

	void SetUIManager(GtkUIManager *ui_manager);
	void Show();
	void Hide();

	GtkWidget* GetWidget(){return m_pBrowserWidget;};
	
	ImageList GetImageList();
	
	void SetImageList(ImageList list);
	
	void SetImageIndex(int index, bool bDirectionForward, bool bFromIconView = false);

/* member variables */
	FolderTree m_FolderTree;
	bool m_bFolderTreeEvent;
	
	GtkWidget *m_pIconView;

	GtkWidget *m_pBrowserWidget;
	
	//GtkWidget *hpaned;
	GtkWidget *vpaned;
	GtkWidget *m_pNotebook;
	GtkWidget *m_pSWFolderTree;
	
	GtkWidget *m_pImageView;

	GtkWidget *m_pLocationEntry;
	GtkWidget *hscale;

	GtkToolItem *m_pToolItemThumbSizer;
	
	GtkUIManager *m_pUIManager;
	guint m_iMergedBrowserUI;
	
	StatusbarPtr m_StatusbarPtr;
	
	ImageList m_ImageList;
	QuiverFile m_QuiverFileCurrent;
	ImageCache m_ThumbnailCache;
	ImageCache m_IconCache;
	
	guint m_iTimeoutUpdateListID;

	Browser *m_BrowserParent;

	ImageLoader m_ImageLoader;
	IPixbufLoaderObserverPtr m_ImageViewPixbufLoaderObserverPtr;
	
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
		
		virtual void LoadThumbnail(gulong ulIndex, guint uiWidth, guint uiHeight);
		virtual void GetVisibleRange(gulong* pulStart, gulong* pulEnd);
		virtual void GetIconSize(guint* puiWidth, guint* puiHeight);
		virtual gulong GetNumItems();
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


static void browser_action_handler_cb(GtkAction *action, gpointer data);

#define ACTION_BROWSER_OPEN_LOCATION                      "BrowserOpenLocation"
#define ACTION_BROWSER_CUT                                "BrowserCut"
#define ACTION_BROWSER_COPY                               "BrowserCopy"
#define ACTION_BROWSER_PASTE                              "BrowserPaste"
#define ACTION_BROWSER_SELECT_ALL                         "BrowserSelectAll"
#define ACTION_BROWSER_TRASH                              "BrowserTrash"
#define ACTION_BROWSER_RELOAD                             "BrowserReload"
#define ACTION_BROWSER_VIEW_PREVIEW                       "BrowserViewPreview"
#define ACTION_BROWSER_VIEW_FOLDERTREE                    "BrowserViewFolderTree"
#define ACTION_BROWSER_ZOOM_IN                            "BrowserZoomIn"
#define ACTION_BROWSER_ZOOM_OUT                           "BrowserZoomOut"

#ifdef QUIVER_MAEMO
#define ACTION_BROWSER_ZOOM_IN_MAEMO                      ACTION_BROWSER_ZOOM_IN"_MAEMO"
#define ACTION_BROWSER_ZOOM_OUT_MAEMO                     ACTION_BROWSER_ZOOM_OUT"_MAEMO"
#endif

static char *ui_browser =
"<ui>"
#ifdef QUIVER_MAEMO
"	<popup name='MenubarMain'>"
#else
"	<menubar name='MenubarMain'>"
#endif
"		<menu action='MenuFile'>"
"			<placeholder action='FileOpenItems'>"
#ifndef QUIVER_MAEMO
"				<menuitem action='"ACTION_BROWSER_OPEN_LOCATION"'/>"
#endif
"			</placeholder>"
"		</menu>"
"		<menu action='MenuEdit'>"
"			<placeholder name='CopyPaste'>"
#ifdef FIXME_DISABLED
"				<menuitem action='"ACTION_BROWSER_CUT"'/>"
#endif
"				<menuitem action='"ACTION_BROWSER_COPY"'/>"
#ifdef FIXME_DISABLED
"				<menuitem action='"ACTION_BROWSER_PASTE"'/>"
#endif
"			</placeholder>"
"			<placeholder name='Selection'>"
#ifdef FIXME_DISABLED
"				<menuitem action='"ACTION_BROWSER_SELECT_ALL"'/>"
#endif
"			</placeholder>"
"			<placeholder name='Trash'>"
"				<menuitem action='"ACTION_BROWSER_TRASH"'/>"
"			</placeholder>"
"		</menu>"
"		<menu action='MenuView'>"
"			<placeholder name='UIItems'>"
"				<menuitem action='"ACTION_BROWSER_VIEW_FOLDERTREE"'/>"
"				<menuitem action='"ACTION_BROWSER_VIEW_PREVIEW"'/>"
"			</placeholder>"
"			<menuitem action='"ACTION_BROWSER_RELOAD"'/>"
"		</menu>"
#ifdef QUIVER_MAEMO
"	</popup>"
#else
"	</menubar>"
#endif
"	<toolbar name='ToolbarMain'>"
"		<placeholder name='NavToolItems'>"
//"			<separator/>"
//"			<toolitem action='"ACTION_BROWSER_RELOAD"'/>"
//"			<separator/>"
"		</placeholder>"
"		<placeholder name='Trash'>"
"			<toolitem action='"ACTION_BROWSER_TRASH"'/>"
"		</placeholder>"
"	</toolbar>"
#ifdef QUIVER_MAEMO
"	<accelerator action='"ACTION_BROWSER_ZOOM_IN_MAEMO"'/>"
"	<accelerator action='"ACTION_BROWSER_ZOOM_OUT_MAEMO"'/>"
#endif
"</ui>";

static  GtkToggleActionEntry action_entries_toggle[] = {
	{ ACTION_BROWSER_VIEW_FOLDERTREE, NULL, "Folder Tree", "<Control><Shift>F", "Show/Hide Folder Tree", G_CALLBACK(browser_action_handler_cb),TRUE},
	{ ACTION_BROWSER_VIEW_PREVIEW, GTK_STOCK_PROPERTIES, "Preview", "<Control><Shift>p", "Show/Hide Image Preview", G_CALLBACK(browser_action_handler_cb),TRUE},
};

static GtkActionEntry action_entries[] = {
	{ ACTION_BROWSER_OPEN_LOCATION, NULL, "Open _Location", "<Control>l", "Open a Location", G_CALLBACK( browser_action_handler_cb )},
	{ ACTION_BROWSER_CUT, GTK_STOCK_CUT, "_Cut", "<Control>X", "Cut image", G_CALLBACK(browser_action_handler_cb)},
	{ ACTION_BROWSER_COPY, GTK_STOCK_COPY, "Copy", "<Control>C", "Copy image", G_CALLBACK(browser_action_handler_cb)},
	{ ACTION_BROWSER_PASTE, GTK_STOCK_PASTE, "Paste", "<Control>V", "Paste image", G_CALLBACK(browser_action_handler_cb)},
	{ ACTION_BROWSER_SELECT_ALL, NULL, "_Select All", "<Control>A", "Select all", G_CALLBACK(browser_action_handler_cb)},
	{ ACTION_BROWSER_TRASH, GTK_STOCK_DELETE, "_Move To Trash", "Delete", "Move image to the Trash", G_CALLBACK(browser_action_handler_cb)},
	{ ACTION_BROWSER_RELOAD, GTK_STOCK_REFRESH, "_Reload", "<Control>R", "Refresh the Current View", G_CALLBACK(browser_action_handler_cb)},
#ifdef QUIVER_MAEMO
	{ ACTION_BROWSER_ZOOM_IN_MAEMO, NULL, NULL, "F7", NULL, G_CALLBACK(browser_action_handler_cb)},
	{ ACTION_BROWSER_ZOOM_OUT_MAEMO, NULL, NULL, "F8", NULL, G_CALLBACK(browser_action_handler_cb)},
#endif
};





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
		selection_list.push_back((int)item->data);
		item = g_list_next(item);
	}
	g_list_free(selection);
	return selection_list;
}

void 
Browser::SetUIManager(GtkUIManager *ui_manager)
{
	m_BrowserImplPtr->SetUIManager(ui_manager);
}

void
Browser::SetStatusbar(StatusbarPtr statusbarPtr)
{
	m_BrowserImplPtr->m_ImageLoader.RemovePixbufLoaderObserver(m_BrowserImplPtr->m_StatusbarPtr.get());
	
	m_BrowserImplPtr->m_StatusbarPtr = statusbarPtr;
	
	m_BrowserImplPtr->m_ImageLoader.AddPixbufLoaderObserver(m_BrowserImplPtr->m_StatusbarPtr.get());

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
Browser::SetImageList(ImageList list)
{
	m_BrowserImplPtr->SetImageList(list);
}


ImageList 
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

static GdkPixbuf* icon_pixbuf_callback(QuiverIconView *iconview, guint cell,gpointer user_data);
static GdkPixbuf* thumbnail_pixbuf_callback(QuiverIconView *iconview, guint cell, gint* actual_width, gint* actual_height, gpointer user_data);
static guint n_cells_callback(QuiverIconView *iconview, gpointer user_data);
static void icon_size_value_changed (GtkRange *range,gpointer  user_data);

static void iconview_cell_activated_cb(QuiverIconView *iconview, guint cell, gpointer user_data);
static void iconview_cursor_changed_cb(QuiverIconView *iconview, guint cell, gpointer user_data);
static void iconview_selection_changed_cb(QuiverIconView *iconview, gpointer user_data);

static void entry_activate(GtkEntry *entry, gpointer user_data);

static void browser_imageview_magnification_changed(QuiverImageView *imageview,gpointer data);
static void browser_imageview_reload(QuiverImageView *imageview,gpointer data);

#ifdef QUIVER_MAEMO
static int get_interpreted_thumb_size(gdouble value);
static gdouble get_range_val_from_thumb_size(gint thumbsize);
#endif

static gboolean entry_focus_in ( GtkWidget *widget, GdkEventFocus *event, gpointer user_data)
{
	Browser::BrowserImpl *pBrowserImpl = (Browser::BrowserImpl*)user_data;

	QuiverUtils::DisconnectUnmodifiedAccelerators(pBrowserImpl->m_pUIManager);
		
	//printf("focus in! %s\n", );

	return FALSE;
}

static gboolean entry_focus_out ( GtkWidget *widget, GdkEventFocus *event, gpointer user_data)
{
	Browser::BrowserImpl *pBrowserImpl = (Browser::BrowserImpl*)user_data;

	QuiverUtils::ConnectUnmodifiedAccelerators(pBrowserImpl->m_pUIManager);
	
	return FALSE;
}

static void pane_size_allocate (GtkWidget* widget, GtkAllocation *allocation, gpointer user_data)
{
	PreferencesPtr prefsPtr = Preferences::GetInstance();
	if (GTK_IS_HPANED(widget))
	{
		prefsPtr->SetInteger(QUIVER_PREFS_BROWSER,QUIVER_PREFS_BROWSER_FOLDER_HPANE,gtk_paned_get_position(GTK_PANED(widget)));
	}
	else if (GTK_IS_VPANED(widget))
	{
		prefsPtr->SetInteger(QUIVER_PREFS_BROWSER,QUIVER_PREFS_BROWSER_FOLDER_VPANE,gtk_paned_get_position(GTK_PANED(widget)));
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
		gtk_widget_hide(pBrowserImpl->vpaned);Initialize
	}
*/

void notebook_page_added  (GtkNotebook *notebook, 
	GtkWidget *child, guint page_num, gpointer user_data)
{
	Browser::BrowserImpl *pBrowserImpl = (Browser::BrowserImpl*)user_data;

	gtk_notebook_set_show_tabs(notebook, 1 > gtk_notebook_get_n_pages(notebook));
	gtk_widget_show(GTK_WIDGET(notebook));

	gtk_widget_show(pBrowserImpl->vpaned);
}

void notebook_page_removed  (GtkNotebook *notebook, 
	GtkWidget *child, guint page_num, gpointer     user_data)
{
	Browser::BrowserImpl *pBrowserImpl = (Browser::BrowserImpl*)user_data;
	
	gtk_notebook_set_show_tabs(notebook, 1 <= gtk_notebook_get_n_pages(notebook));
	if (0 == gtk_notebook_get_n_pages(notebook))
	{
		gtk_widget_hide(GTK_WIDGET(notebook));
		if (!GTK_WIDGET_VISIBLE(pBrowserImpl->m_pImageView))
		{
			gtk_widget_hide(pBrowserImpl->vpaned);
		}
	}
}


Browser::BrowserImpl::BrowserImpl(Browser *parent) : m_ThumbnailCache(100),
                                       m_IconCache(100),
                                       m_ImageListEventHandlerPtr( new ImageListEventHandler(this) ),
                                       m_PreferencesEventHandlerPtr(new PreferencesEventHandler(this) ),
                                       m_FolderTreeEventHandlerPtr( new FolderTreeEventHandler(this) ),
#ifdef QUIVER_MAEMO
                                       m_ThumbnailLoader(this,2)
#else
                                       m_ThumbnailLoader(this,4)
#endif
{
	PreferencesPtr prefsPtr = Preferences::GetInstance();
	prefsPtr->AddEventHandler( m_PreferencesEventHandlerPtr );
	m_FolderTree.AddEventHandler(m_FolderTreeEventHandlerPtr);

	m_BrowserParent = parent;
	m_pUIManager = NULL;
	m_bFolderTreeEvent = false;

	m_iTimeoutUpdateListID = 0;
	m_iMergedBrowserUI = 0;
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
	

#ifndef QUIVER_MAEMO
	hscale = gtk_hscale_new_with_range(20,256,1);
	gtk_range_set_value(GTK_RANGE(hscale),128);
	gtk_scale_set_value_pos (GTK_SCALE(hscale),GTK_POS_LEFT);
	gtk_scale_set_draw_value(GTK_SCALE(hscale),FALSE);

	gtk_widget_set_size_request(hscale,100,-1);
#else
	hscale = hildon_controlbar_new();
	hildon_controlbar_set_range(HILDON_CONTROLBAR(hscale),0,11);
	hildon_controlbar_set_value(HILDON_CONTROLBAR(hscale),5);
	/*
	GtkRequisition requisition = {0};
	gtk_widget_size_request(hscale,&requisition);
	gtk_widget_set_size_request(hscale, 200, requisition.height);
	*/
#endif
	m_pToolItemThumbSizer = gtk_tool_item_new();
	gtk_tool_item_set_expand(m_pToolItemThumbSizer, TRUE);

	GtkWidget* alignment = gtk_alignment_new(1.,5.,0.,1.);
	gtk_container_add(GTK_CONTAINER(alignment), hscale);
	gtk_container_add(GTK_CONTAINER(m_pToolItemThumbSizer), alignment);

	gtk_widget_show_all(GTK_WIDGET(m_pToolItemThumbSizer));
	g_object_ref(m_pToolItemThumbSizer);
	
	m_pLocationEntry = gtk_entry_new();
	
	hpaned = gtk_hpaned_new();
	vpaned = gtk_vpaned_new();
	m_pNotebook = gtk_notebook_new();
	
#if (GTK_MAJOR_VERSION > 2) || (GTK_MAJOR_VERSION == 2 && GTK_MINOR_VERSION >= 10)
	gtk_signal_connect (GTK_OBJECT (m_pNotebook), "page-added",
	      GTK_SIGNAL_FUNC (notebook_page_added), this);
	gtk_signal_connect (GTK_OBJECT (m_pNotebook), "page-removed",
	      GTK_SIGNAL_FUNC (notebook_page_removed), this);
#endif
	
	m_pIconView = quiver_icon_view_new();
	m_pImageView = quiver_image_view_new();

	gtk_widget_set_no_show_all(m_pImageView, TRUE);

#ifdef QUIVER_MAEMO
	bool bShowPreview = prefsPtr->GetBoolean(QUIVER_PREFS_BROWSER,QUIVER_PREFS_BROWSER_PREVIEW_SHOW,false);
#else
	bool bShowPreview = prefsPtr->GetBoolean(QUIVER_PREFS_BROWSER,QUIVER_PREFS_BROWSER_PREVIEW_SHOW,true);
#endif
	if (bShowPreview)
	{
		gtk_widget_show(m_pImageView);
	}

	scrolled_window = gtk_scrolled_window_new(NULL,NULL);
	
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),GTK_POLICY_AUTOMATIC,GTK_POLICY_AUTOMATIC);
	gtk_container_add(GTK_CONTAINER(scrolled_window),m_pIconView);
	
	hbox = gtk_hbox_new(FALSE,0);
	vbox = gtk_vbox_new(FALSE,0);
	
#ifndef QUIVER_MAEMO
	gtk_box_pack_start (GTK_BOX (hbox), m_pLocationEntry, TRUE, TRUE, 0);
	//gtk_box_pack_start (GTK_BOX (hbox), hscale, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);
#endif
	gtk_box_pack_start (GTK_BOX (vbox), scrolled_window, TRUE, TRUE, 0);
	
	gtk_paned_pack1(GTK_PANED(vpaned),m_pNotebook,TRUE,TRUE);
	gtk_paned_pack2(GTK_PANED(vpaned),m_pImageView,FALSE,FALSE);
	
	gtk_paned_pack1(GTK_PANED(hpaned),vpaned,FALSE,FALSE);
	gtk_paned_pack2(GTK_PANED(hpaned),vbox,TRUE,TRUE);
	
	// set the size of the hpane and vpane
	int hpaned_pos = prefsPtr->GetInteger(QUIVER_PREFS_BROWSER,QUIVER_PREFS_BROWSER_FOLDER_HPANE,200);
	gtk_paned_set_position(GTK_PANED(hpaned),hpaned_pos);

	int vpaned_pos = prefsPtr->GetInteger(QUIVER_PREFS_BROWSER,QUIVER_PREFS_BROWSER_FOLDER_VPANE,300);
	gtk_paned_set_position(GTK_PANED(vpaned),vpaned_pos);
	
	
	gtk_signal_connect (GTK_OBJECT (hpaned), "size_allocate",
	      GTK_SIGNAL_FUNC (pane_size_allocate), this);
	gtk_signal_connect (GTK_OBJECT (vpaned), "size_allocate",
	      GTK_SIGNAL_FUNC (pane_size_allocate), this);
	
	m_pBrowserWidget = hpaned;
	
	m_pSWFolderTree = gtk_scrolled_window_new(NULL,NULL);
	g_object_ref_sink(m_pSWFolderTree);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(m_pSWFolderTree),GTK_POLICY_AUTOMATIC,GTK_POLICY_AUTOMATIC);
	GtkWidget *pFolderTree = m_FolderTree.GetWidget();
	
	//gtk_widget_set_no_show_all(pFolderTree, TRUE);
	gtk_container_add(GTK_CONTAINER(m_pSWFolderTree),pFolderTree);
	gtk_widget_show_all(m_pSWFolderTree);
		
	gtk_widget_show_all(m_pNotebook);
	gtk_widget_set_no_show_all(m_pNotebook, TRUE);

	if (prefsPtr->GetBoolean(QUIVER_PREFS_BROWSER,QUIVER_PREFS_BROWSER_FOLDERTREE_SHOW,true))
	{	
		gtk_notebook_append_page(GTK_NOTEBOOK(m_pNotebook), m_pSWFolderTree,gtk_label_new("Folders"));	
	}
	
	if (0 == gtk_notebook_get_n_pages(GTK_NOTEBOOK(m_pNotebook)))
	{
		gtk_widget_hide(GTK_WIDGET(m_pNotebook));
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

#ifdef QUIVER_MAEMO
	quiver_icon_view_set_drag_behavior(QUIVER_ICON_VIEW(m_pIconView),QUIVER_ICON_VIEW_DRAG_BEHAVIOR_SCROLL);
#endif

	quiver_icon_view_set_scroll_type(QUIVER_ICON_VIEW(m_pIconView),QUIVER_ICON_VIEW_SCROLL_SMOOTH);
	quiver_icon_view_set_n_items_func(QUIVER_ICON_VIEW(m_pIconView),(QuiverIconViewGetNItemsFunc)n_cells_callback,this,NULL);
	quiver_icon_view_set_thumbnail_pixbuf_func(QUIVER_ICON_VIEW(m_pIconView),(QuiverIconViewGetThumbnailPixbufFunc)thumbnail_pixbuf_callback,this,NULL);
	quiver_icon_view_set_icon_pixbuf_func(QUIVER_ICON_VIEW(m_pIconView),(QuiverIconViewGetIconPixbufFunc)icon_pixbuf_callback,this,NULL);

	gtk_signal_connect (GTK_OBJECT (hscale), "value_changed",
	      GTK_SIGNAL_FUNC (icon_size_value_changed), this);

#ifdef QUIVER_MAEMO
	// a single click on a selected thumbnail will trigger this signal
	g_signal_connect(G_OBJECT(m_pIconView),"cell_clicked",G_CALLBACK(iconview_cell_activated_cb),this);
#endif
	g_signal_connect(G_OBJECT(m_pIconView),"cell_activated",G_CALLBACK(iconview_cell_activated_cb),this);
	g_signal_connect(G_OBJECT(m_pIconView),"cursor_changed",G_CALLBACK(iconview_cursor_changed_cb),this);
	g_signal_connect(G_OBJECT(m_pIconView),"selection_changed",G_CALLBACK(iconview_selection_changed_cb),this);	
	
	g_signal_connect(G_OBJECT(m_pLocationEntry),"activate",G_CALLBACK(entry_activate),this);


    g_signal_connect (G_OBJECT (m_pLocationEntry), "focus-in-event",
    			G_CALLBACK (entry_focus_in), this);
    g_signal_connect (G_OBJECT (m_pLocationEntry), "focus-out-event",
    			G_CALLBACK (entry_focus_out), this);

	string strBGColorImg   = prefsPtr->GetString(QUIVER_PREFS_APP,QUIVER_PREFS_APP_BG_IMAGEVIEW, "#000");
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
/*
	quiver_icon_view_set_overlay_pixbuf_func(QUIVER_ICON_VIEW(real_iconview),(QuiverIconViewGetOverlayPixbufFunc)overlay_pixbuf_callback,user_data,NULL);
*/

	IPixbufLoaderObserverPtr tmp ( new ImageViewPixbufLoaderObserver(QUIVER_IMAGE_VIEW(m_pImageView)) );
	m_ImageViewPixbufLoaderObserverPtr = tmp;
	m_ImageLoader.AddPixbufLoaderObserver(m_ImageViewPixbufLoaderObserverPtr.get());

	gtk_widget_show_all(m_pBrowserWidget);
	gtk_widget_hide(m_pBrowserWidget);
	gtk_widget_set_no_show_all(m_pBrowserWidget,TRUE);


	gdouble thumb_size = (gdouble)prefsPtr->GetInteger(QUIVER_PREFS_BROWSER,QUIVER_PREFS_BROWSER_THUMB_SIZE);	

	if (thumb_size < 20. || 256. < thumb_size)
	{
		thumb_size = 128.;
	}
#ifndef QUIVER_MAEMO
	gtk_range_set_value(GTK_RANGE(hscale),thumb_size);
#else
	gtk_range_set_value(GTK_RANGE(hscale),get_range_val_from_thumb_size((gint)thumb_size));
#endif

}

Browser::BrowserImpl::~BrowserImpl()
{
	m_ImageLoader.RemovePixbufLoaderObserver(m_StatusbarPtr.get());
	m_ImageLoader.RemovePixbufLoaderObserver(m_ImageViewPixbufLoaderObserverPtr.get());
	
	PreferencesPtr prefsPtr = Preferences::GetInstance();
	gdouble value = gtk_range_get_value (GTK_RANGE(hscale));

	gint val;

#ifdef QUIVER_MAEMO
	g_object_unref(m_pToolItemThumbSizer);
	val = get_interpreted_thumb_size(value);
#else
	val = (int)value;
#endif

	prefsPtr->SetInteger(QUIVER_PREFS_BROWSER,QUIVER_PREFS_BROWSER_THUMB_SIZE,val);

	prefsPtr->RemoveEventHandler( m_PreferencesEventHandlerPtr );
	m_ImageList.RemoveEventHandler(m_ImageListEventHandlerPtr);
	

	if (m_pUIManager)
	{
		g_object_unref(m_pUIManager);
	}
	g_object_unref(m_pSWFolderTree);
}

void Browser::BrowserImpl::SetUIManager(GtkUIManager *ui_manager)
{
	PreferencesPtr prefsPtr = Preferences::GetInstance();
	GError *tmp_error;
	tmp_error = NULL;
	
	if (m_pUIManager)
	{
		g_object_unref(m_pUIManager);
	}

	m_pUIManager = ui_manager;
	
	g_object_ref(m_pUIManager);


	guint n_entries = G_N_ELEMENTS (action_entries);
	GtkActionGroup* actions = gtk_action_group_new ("BrowserActions");
	gtk_action_group_add_actions(actions, action_entries, n_entries, this);

	gtk_action_group_add_toggle_actions(actions,
										action_entries_toggle, 
										G_N_ELEMENTS (action_entries_toggle),
										this);
	gtk_ui_manager_insert_action_group (m_pUIManager,actions,0);	

		
	GtkAction* action = QuiverUtils::GetAction(m_pUIManager,ACTION_BROWSER_VIEW_PREVIEW);
	if (NULL != action)
	{
		bool bShowPreview = prefsPtr->GetBoolean(QUIVER_PREFS_BROWSER,QUIVER_PREFS_BROWSER_PREVIEW_SHOW);
		gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), bShowPreview ? TRUE : FALSE);
	}

	action = QuiverUtils::GetAction(m_pUIManager,ACTION_BROWSER_VIEW_FOLDERTREE);
	if (NULL != action)
	{
		bool bShowFolderTree = prefsPtr->GetBoolean(QUIVER_PREFS_BROWSER,QUIVER_PREFS_BROWSER_FOLDERTREE_SHOW);
		gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), bShowFolderTree ? TRUE : FALSE);
	}

}

void Browser::BrowserImpl::Show()
{
	GError *tmp_error;
	tmp_error = NULL;

	if (m_pUIManager && 0 == m_iMergedBrowserUI)
	{
		m_iMergedBrowserUI = gtk_ui_manager_add_ui_from_string(m_pUIManager,
				ui_browser,
				strlen(ui_browser),
				&tmp_error);
		gtk_ui_manager_ensure_update(m_pUIManager);

		GtkWidget* toolbar = gtk_ui_manager_get_widget(m_pUIManager,"/ui/ToolbarMain/");
		if (NULL != toolbar)
		{
			gtk_toolbar_insert(GTK_TOOLBAR(toolbar),m_pToolItemThumbSizer,-1);
		}

		if (NULL != tmp_error)
		{
			g_warning("Browser::Show() Error: %s\n",tmp_error->message);
		}
	}

 	if (0 != m_ImageList.GetSize())
	{
		quiver_icon_view_set_cursor_cell( QUIVER_ICON_VIEW(m_pIconView),
			m_ImageList.GetCurrentIndex() );
	}
	
	gint cursor_cell = quiver_icon_view_get_cursor_cell(QUIVER_ICON_VIEW(m_pIconView));
 
	if (0 == m_ImageList.GetSize() || m_QuiverFileCurrent != m_ImageList.GetCurrent())
	{
		quiver_image_view_set_pixbuf(QUIVER_IMAGE_VIEW(m_pImageView),NULL);
	}
	else if (0 != m_ImageList.GetSize())
	{
		if ( (gint)m_ImageList.GetCurrentIndex() != cursor_cell  )
		{
		
			g_signal_handlers_block_by_func(m_pIconView,(gpointer)iconview_cursor_changed_cb,this);
	
			quiver_icon_view_set_cursor_cell(
				QUIVER_ICON_VIEW(m_pIconView),
				m_ImageList.GetCurrentIndex() );
			
			g_signal_handlers_unblock_by_func(m_pIconView,(gpointer)iconview_cursor_changed_cb,this);
		}
	
	}

	gtk_widget_show(m_pBrowserWidget);
	
	if (m_ImageList.GetSize() && m_QuiverFileCurrent != m_ImageList.GetCurrent() )
	{
		SetImageIndex(m_ImageList.GetCurrentIndex(), true);
	}
	m_ImageList.UnblockHandler(m_ImageListEventHandlerPtr);
}

void Browser::BrowserImpl::Hide()
{
	gtk_widget_hide(m_pBrowserWidget);
	if (m_pUIManager && 0 != m_iMergedBrowserUI)
	{	
		gtk_ui_manager_remove_ui(m_pUIManager, m_iMergedBrowserUI);
		m_iMergedBrowserUI = 0;
		gtk_ui_manager_ensure_update(m_pUIManager);

		GtkWidget* toolbar = gtk_ui_manager_get_widget(m_pUIManager,"/ui/ToolbarMain/");
		if (NULL != toolbar)
		{
			gtk_container_remove(GTK_CONTAINER(toolbar),GTK_WIDGET(m_pToolItemThumbSizer));
		}
	}
	
	m_ImageList.BlockHandler(m_ImageListEventHandlerPtr);
}

void Browser::BrowserImpl::SetImageList(ImageList imglist)
{
	m_ImageList.RemoveEventHandler(m_ImageListEventHandlerPtr);
	
	m_ImageList = imglist;
	
	m_ImageList.AddEventHandler(m_ImageListEventHandlerPtr);
	
	if (0 == m_iMergedBrowserUI)
	{
		m_ImageList.BlockHandler(m_ImageListEventHandlerPtr);
	}

	list<string> dirs = m_ImageList.GetFolderList();
	m_FolderTree.SetSelectedFolders(dirs);
}


void Browser::BrowserImpl::SetImageIndex(int index, bool bDirectionForward, bool bFromIconView /* = false */)
{
	gint width=0, height=0;

	QuiverImageViewMode mode = quiver_image_view_get_view_mode_unmagnified(QUIVER_IMAGE_VIEW(m_pImageView));
	
	if (mode != QUIVER_IMAGE_VIEW_MODE_ACTUAL_SIZE && GTK_WIDGET_REALIZED(m_pImageView))
	{

		width = m_pImageView->allocation.width;
		height = m_pImageView->allocation.height;
	}

	m_ImageList.BlockHandler(m_ImageListEventHandlerPtr);
	
	if (m_ImageList.SetCurrentIndex(index))
	{
		if (!bFromIconView)
		{
			g_signal_handlers_block_by_func(m_pIconView,(gpointer)iconview_cursor_changed_cb, this);
			
			quiver_icon_view_set_cursor_cell( QUIVER_ICON_VIEW(m_pIconView),
			      m_ImageList.GetCurrentIndex() );	
	
			g_signal_handlers_unblock_by_func(m_pIconView,(gpointer)iconview_cursor_changed_cb, this);
		}
		
		if (GTK_WIDGET_MAPPED(m_pImageView))
		{
			
			QuiverFile f;
			f = m_ImageList.GetCurrent();
			m_ImageLoader.LoadImageAtSize(f,width,height);
			
			if (bDirectionForward)
			{
				// cache the next image if there is one
				if (m_ImageList.HasNext())
				{
					f = m_ImageList.GetNext();
					m_ImageLoader.CacheImageAtSize(f,width,height);
				}
			}
			else
			{
				// cache the next image if there is one
				if (m_ImageList.HasPrevious())
				{
					f = m_ImageList.GetPrevious();
					m_ImageLoader.CacheImageAtSize(f, width, height);
				}
				
			}
		}	
	}
	
	m_ImageList.UnblockHandler(m_ImageListEventHandlerPtr);
	
	if (m_ImageList.GetSize())
	{
		m_QuiverFileCurrent = m_ImageList.GetCurrent();
	}
	else
	{
		QuiverFile f;
		m_QuiverFileCurrent = f;
	}
	
	// update the toolbar / menu buttons - (un)set sensitive 
	//UpdateUI();
}


ImageList Browser::BrowserImpl::GetImageList()
{
	return m_ImageList;
}


//=============================================================================
// BrowswerImpl Callbacks
//=============================================================================

#ifdef QUIVER_MAEMO
static int get_interpreted_thumb_size(gdouble value)
{
	gint val;
	value = 20. + value * 21.51;
	val = (gint)ceil(value);
	val = min(val,256);
	return val;
}
static gdouble get_range_val_from_thumb_size(gint thumbsize)
{
	gdouble value = floor( ((thumbsize - 20) / 21.51) + .5);
	return value;
}
#endif

static void icon_size_value_changed (GtkRange *range,gpointer  user_data)
{
	Browser::BrowserImpl* b = (Browser::BrowserImpl*)user_data;
	gdouble value = gtk_range_get_value (range);
#ifndef QUIVER_MAEMO
	quiver_icon_view_set_icon_size(QUIVER_ICON_VIEW(b->m_pIconView), (gint)value,(gint)value);
#else
	gint val = get_interpreted_thumb_size(value);
	quiver_icon_view_set_icon_size(QUIVER_ICON_VIEW(b->m_pIconView), val,val);
#endif
}

static guint n_cells_callback(QuiverIconView *iconview, gpointer user_data)
{
	Browser::BrowserImpl* b = (Browser::BrowserImpl*)user_data;
	return b->m_ImageList.GetSize();
}

static GdkPixbuf* icon_pixbuf_callback(QuiverIconView *iconview, guint cell,gpointer user_data)
{
	Browser::BrowserImpl* b = (Browser::BrowserImpl*)user_data;
	QuiverFile f = b->m_ImageList[cell];
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
			b->m_IconCache.AddPixbuf(cache_icon_name,pixbuf);
		}
		g_free(icon_name);
	}

	return pixbuf;
}

static gboolean thumbnail_loader_udpate_list (gpointer data)
{
	Browser::BrowserImpl* b = (Browser::BrowserImpl*)data;
	gdk_threads_enter();
	b->m_ThumbnailLoader.UpdateList();
	gdk_threads_leave();
	return FALSE;
}

static GdkPixbuf* thumbnail_pixbuf_callback(QuiverIconView *iconview, guint cell, gint* actual_width, gint* actual_height, gpointer user_data)
{
	Browser::BrowserImpl* b = (Browser::BrowserImpl*)user_data;

	GdkPixbuf *pixbuf = NULL;
	gboolean need_new_thumb = TRUE;
	
	guint width, height;
	guint thumb_width, thumb_height;
	guint bound_width, bound_height;
	quiver_icon_view_get_icon_size(iconview,&width,&height);
	

	pixbuf = b->m_ThumbnailCache.GetPixbuf(b->m_ImageList[cell].GetURI());

	if (pixbuf)
	{
		*actual_width = b->m_ImageList[cell].GetWidth();
		*actual_height = b->m_ImageList[cell].GetHeight();

		if (4 < b->m_ImageList[cell].GetOrientation())
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
		if (b->m_iTimeoutUpdateListID)
		{
			g_source_remove (b->m_iTimeoutUpdateListID);
		}
		b->m_iTimeoutUpdateListID = g_timeout_add(20,thumbnail_loader_udpate_list,b);
	}
	
	return pixbuf;
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

	GtkAction *action = gtk_ui_manager_get_action(b->m_pUIManager,"/ui/ToolbarMain/Trash/BrowserTrash");
	if (NULL != action)
	{
		GList *selection;
		selection = quiver_icon_view_get_selection(iconview);
		if (NULL == selection)
		{
			gtk_action_set_sensitive(action,FALSE);
		}
		else
		{
			gtk_action_set_sensitive(action,TRUE);
			g_list_free(selection);
		}
	}
	b->m_BrowserParent->EmitSelectionChangedEvent();
}



static void entry_activate(GtkEntry *entry, gpointer user_data)
{
	Browser::BrowserImpl* b = (Browser::BrowserImpl*)user_data;
	string entry_text = gtk_entry_get_text(entry);
	list<string> file_list;
	file_list.push_back(entry_text);
	b->m_ImageList.SetImageList(&file_list);
	
}

static void browser_imageview_magnification_changed(QuiverImageView *imageview,gpointer data)
{
	Browser::BrowserImpl* pBrowserImpl = (Browser::BrowserImpl*)data;
	
	double mag = quiver_image_view_get_magnification(QUIVER_IMAGE_VIEW(pBrowserImpl->m_pImageView));
	pBrowserImpl->m_StatusbarPtr->SetMagnification((int)(mag*100+.5));
}

static void browser_imageview_reload(QuiverImageView *imageview,gpointer data)
{
	//printf("#### got a reload message from the imageview\n");
	Browser::BrowserImpl* pBrowserImpl = (Browser::BrowserImpl*)data;

	if (!pBrowserImpl->m_ImageList.GetSize())
		return;

	ImageLoader::LoadParams params = {0};

	params.orientation = pBrowserImpl->m_ImageList.GetCurrent().GetOrientation();
	params.reload = true;
	params.fullsize = true;
	params.no_thumb_preview = true;
	params.state = ImageLoader::LOAD;

	pBrowserImpl->m_ImageLoader.LoadImage(pBrowserImpl->m_ImageList.GetCurrent(),params);
}

static void browser_action_handler_cb(GtkAction *action, gpointer data)
{
	Browser::BrowserImpl* pBrowserImpl;
	pBrowserImpl = (Browser::BrowserImpl*)data;
	PreferencesPtr prefsPtr = Preferences::GetInstance();
	
	//printf("Browser Action: %s\n",gtk_action_get_name(action));
	
	const gchar * szAction = gtk_action_get_name(action);
	
	if (0 == strcmp(szAction,ACTION_BROWSER_RELOAD))
	{
		// clear the caches
		pBrowserImpl->m_ImageList.Reload();

		pBrowserImpl->m_ThumbnailCache.Clear();
			
		pBrowserImpl->m_ThumbnailLoader.UpdateList(true);
		
	}
	else if (0 == strcmp(szAction,ACTION_BROWSER_VIEW_FOLDERTREE))
	{
		if( gtk_toggle_action_get_active(GTK_TOGGLE_ACTION(action)) )
		{

#if (GTK_MAJOR_VERSION < 2) || (GTK_MAJOR_VERSION == 2 && GTK_MINOR_VERSION < 10)
			gint page_num = 
#endif
			gtk_notebook_prepend_page(GTK_NOTEBOOK(pBrowserImpl->m_pNotebook), pBrowserImpl->m_pSWFolderTree,gtk_label_new("Folders"));
#if (GTK_MAJOR_VERSION < 2) || (GTK_MAJOR_VERSION == 2 && GTK_MINOR_VERSION < 10)
			notebook_page_added(GTK_NOTEBOOK(pBrowserImpl->m_pNotebook),pBrowserImpl->m_pSWFolderTree, page_num, pBrowserImpl);
#endif
			prefsPtr->SetBoolean(QUIVER_PREFS_BROWSER,QUIVER_PREFS_BROWSER_FOLDERTREE_SHOW,true);
		}
		else
		{
			gint page_num =  
				gtk_notebook_page_num(GTK_NOTEBOOK(pBrowserImpl->m_pNotebook),pBrowserImpl->m_pSWFolderTree);
			gtk_notebook_remove_page(GTK_NOTEBOOK(pBrowserImpl->m_pNotebook),page_num);
#if (GTK_MAJOR_VERSION < 2) || (GTK_MAJOR_VERSION == 2 && GTK_MINOR_VERSION < 10)
			notebook_page_removed(GTK_NOTEBOOK(pBrowserImpl->m_pNotebook),pBrowserImpl->m_pSWFolderTree, page_num, pBrowserImpl);
#endif
			prefsPtr->SetBoolean(QUIVER_PREFS_BROWSER,QUIVER_PREFS_BROWSER_FOLDERTREE_SHOW,false);
		}
	}
	else if (0 == strcmp(szAction,ACTION_BROWSER_VIEW_PREVIEW))
	{
		if( gtk_toggle_action_get_active(GTK_TOGGLE_ACTION(action)) )
		{
			gtk_widget_show(pBrowserImpl->m_pImageView);	
			prefsPtr->SetBoolean(QUIVER_PREFS_BROWSER,QUIVER_PREFS_BROWSER_PREVIEW_SHOW,true);
			gtk_widget_show(pBrowserImpl->vpaned);
		}
		else
		{
			gtk_widget_hide(pBrowserImpl->m_pImageView);	
			prefsPtr->SetBoolean(QUIVER_PREFS_BROWSER,QUIVER_PREFS_BROWSER_PREVIEW_SHOW,false);
			if (!GTK_WIDGET_VISIBLE(pBrowserImpl->m_pNotebook))
			{
				gtk_widget_hide(pBrowserImpl->vpaned);
			}
		}
	}
#ifdef QUIVER_MAEMO
	else if (0 == strcmp(szAction,ACTION_BROWSER_ZOOM_IN_MAEMO))
	{
		gtk_range_set_value (GTK_RANGE(pBrowserImpl->hscale), gtk_range_get_value (GTK_RANGE(pBrowserImpl->hscale)) + 1.);
	}
	else if (0 == strcmp(szAction,ACTION_BROWSER_ZOOM_OUT_MAEMO))
	{
		gtk_range_set_value (GTK_RANGE(pBrowserImpl->hscale), gtk_range_get_value (GTK_RANGE(pBrowserImpl->hscale)) - 1.);
	}
#endif
	else if (0 == strcmp(szAction,ACTION_BROWSER_OPEN_LOCATION))
	{
		gtk_widget_grab_focus(pBrowserImpl->m_pLocationEntry);
	}
	else if (0 == strcmp(szAction,ACTION_BROWSER_COPY))
	{
		GtkClipboard* clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
		
		string strClipText;
		
		GList *selection;
		selection = quiver_icon_view_get_selection(QUIVER_ICON_VIEW(pBrowserImpl->m_pIconView));
	
		if (NULL != selection)
		{
			// delete the items!
			GList *sel_itr = selection;

			while (NULL != sel_itr)
			{
				int item = (int)sel_itr->data;
				QuiverFile f = pBrowserImpl->m_ImageList[item];
				if (!strClipText.empty())
				{
					strClipText += "\n";
				}
				strClipText += f.GetURI();
				sel_itr = g_list_next(sel_itr);
			}
			g_list_free(selection);

			gtk_clipboard_set_text (clipboard, strClipText.c_str(), strClipText.length());

		}
			
		

			
		
	}
	else if (0 == strcmp(szAction, ACTION_BROWSER_TRASH))
	{
		gint rval = GTK_RESPONSE_YES;
		GtkWidget* dialog = gtk_message_dialog_new (/*FIXME*/NULL,GTK_DIALOG_MODAL,
								GTK_MESSAGE_QUESTION,GTK_BUTTONS_YES_NO,("Move the selected images to the trash?"));
		rval = gtk_dialog_run(GTK_DIALOG(dialog));
		gtk_widget_destroy(dialog);

		switch (rval)
		{
			case GTK_RESPONSE_YES:
			{
				GList *selection;
				selection = quiver_icon_view_get_selection(QUIVER_ICON_VIEW(pBrowserImpl->m_pIconView));
				if (NULL == selection)
				{
					// nothing to delete!
				}
				else
				{
					// delete the items!
					GList *sel_itr = selection;
					set<int> items;
					
					while (NULL != sel_itr)
					{
						items.insert((int)sel_itr->data);
						sel_itr = g_list_next(sel_itr);
					}
					g_list_free(selection);

					set<int>::reverse_iterator ritr;
					
					pBrowserImpl->m_ImageList.BlockHandler(pBrowserImpl->m_ImageListEventHandlerPtr);
					
					for (ritr = items.rbegin() ; items.rend() != ritr ; ++ritr)
					{
						//printf("delete: %d\n",*ritr);
						QuiverFile f = pBrowserImpl->m_ImageList[*ritr];
						
						
						
						if (QuiverFileOps::MoveToTrash(f))
						{
							pBrowserImpl->m_ImageList.Remove(*ritr);
						}

					}
					
					pBrowserImpl->m_ImageList.UnblockHandler(pBrowserImpl->m_ImageListEventHandlerPtr);	
					
					quiver_icon_view_set_cursor_cell(QUIVER_ICON_VIEW(pBrowserImpl->m_pIconView),pBrowserImpl->m_ImageList.GetCurrentIndex());					
					
					pBrowserImpl->m_ThumbnailLoader.UpdateList(true);
				}
				break;
			}
			case GTK_RESPONSE_NO:
				//fall through
			default:
				// do not delete
				// cout << "not trashing file : " << endl;//m_QuiverImplPtr->m_ImageList.GetCurrent().GetURI() << endl;
				break;
		}
	

	}
}

//=============================================================================
// private browser implementation nested classes:
//=============================================================================
void Browser::BrowserImpl::ImageListEventHandler::HandleContentsChanged(ImageListEventPtr event)
{
	// refresh the list
	parent->SetImageIndex(parent->m_ImageList.GetCurrentIndex(),true);
			
	parent->m_ThumbnailLoader.UpdateList(true);	
	
	// get the list of files and folders in the image list
	list<string> dirs  = parent->m_ImageList.GetFolderList();
	list<string> files = parent->m_ImageList.GetFileList();
	if (1 == dirs.size() && 0 == files.size())
	{
		
		GnomeVFSURI* vfs_uri = gnome_vfs_uri_new(dirs.front().c_str());
		if (gnome_vfs_uri_is_local(vfs_uri))
		{
			char* uri = gnome_vfs_get_local_path_from_uri (dirs.front().c_str());
			gtk_entry_set_text(GTK_ENTRY(parent->m_pLocationEntry),uri);
			g_free(uri);
		}
		else
		{
			gtk_entry_set_text(GTK_ENTRY(parent->m_pLocationEntry),dirs.front().c_str());
		}
		gnome_vfs_uri_unref(vfs_uri);
	}
	else if (0 == dirs.size() && 1 == files.size())
	{
		gtk_entry_set_text(GTK_ENTRY(parent->m_pLocationEntry),files.front().c_str());
	}
	else if (0 == dirs.size() && 0 == files.size())
	{
		gtk_entry_set_text(GTK_ENTRY(parent->m_pLocationEntry),"");
	}
	else
	{
		gchar szText[256] = "";
		if (0 == dirs.size())
		{
			g_snprintf(szText,256,"%d files", files.size());
		}
		else if (0 == files.size())
		{
			g_snprintf(szText,256,"%d folders", dirs.size());
		}
		else
		{
			g_snprintf(szText,256,"%d folders, %d files", dirs.size(), files.size());
		}
		gtk_entry_set_text(GTK_ENTRY(parent->m_pLocationEntry),szText);
		
	}
	
	if (!parent->m_bFolderTreeEvent)
	{
		parent->m_FolderTree.SetSelectedFolders(dirs);
	}

}
void Browser::BrowserImpl::ImageListEventHandler::HandleCurrentIndexChanged(ImageListEventPtr event) 
{
	gint width=0, height=0;
	QuiverImageViewMode mode = quiver_image_view_get_view_mode_unmagnified(QUIVER_IMAGE_VIEW(parent->m_pImageView));
	
	if (mode != QUIVER_IMAGE_VIEW_MODE_ACTUAL_SIZE && GTK_WIDGET_REALIZED(parent->m_pImageView))
	{
		width = parent->m_pImageView->allocation.width;
		height = parent->m_pImageView->allocation.height;
	}
	
	parent->SetImageIndex(event->GetIndex(),true);
	
	parent->m_BrowserParent->EmitCursorChangedEvent();
}
void Browser::BrowserImpl::ImageListEventHandler::HandleItemAdded(ImageListEventPtr event)
{
	// refresh the list
	parent->SetImageIndex(parent->m_ImageList.GetCurrentIndex(),true);
	parent->m_ThumbnailLoader.UpdateList(true);	
}
void Browser::BrowserImpl::ImageListEventHandler::HandleItemRemoved(ImageListEventPtr event)
{
	parent->SetImageIndex(parent->m_ImageList.GetCurrentIndex(),true);
	parent->m_ThumbnailLoader.UpdateList(true);
}
void Browser::BrowserImpl::ImageListEventHandler::HandleItemChanged(ImageListEventPtr event)
{
	QuiverFile f = parent->m_ImageList.Get(event->GetIndex());
	//printf ("image list item changed %d: %s\n",event->GetIndex() , f.GetURI());

	parent->m_ThumbnailCache.RemovePixbuf(f.GetURI());
		
	parent->SetImageIndex(parent->m_ImageList.GetCurrentIndex(),true);
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
}


void Browser::BrowserImpl::FolderTreeEventHandler::HandleSelectionChanged(FolderTreeEventPtr event)
{
	list<string> listFolders = parent->m_FolderTree.GetSelectedFolders();
	list<string>::iterator itr;

	parent->m_bFolderTreeEvent = true;
	parent->m_ImageList.UpdateImageList(&listFolders);
	parent->m_bFolderTreeEvent = false;
}



void Browser::BrowserImpl::BrowserThumbLoader::LoadThumbnail(gulong ulIndex, guint uiWidth, guint uiHeight)
{

	if (GTK_WIDGET_MAPPED(m_pBrowserImpl->m_pIconView) && 
		ulIndex < m_pBrowserImpl->m_ImageList.GetSize())
	{
		QuiverFile f = m_pBrowserImpl->m_ImageList[ulIndex];
	
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

			m_pBrowserImpl->m_ThumbnailCache.AddPixbuf(f.GetURI(),pixbuf);
			g_object_unref(pixbuf);

			gdk_threads_enter();
			
			quiver_icon_view_invalidate_cell(QUIVER_ICON_VIEW(m_pBrowserImpl->m_pIconView),ulIndex);
			
			gdk_threads_leave();

			pthread_yield();
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
	return m_pBrowserImpl->m_ImageList.GetSize();
}

void Browser::BrowserImpl::BrowserThumbLoader::SetIsRunning(bool bIsRunning)
{
	if (m_pBrowserImpl->m_StatusbarPtr.get())
	{
		gdk_threads_enter();
		if (bIsRunning)
		{
			m_pBrowserImpl->m_StatusbarPtr->StartProgressPulse();
		}
		else
		{
			m_pBrowserImpl->m_StatusbarPtr->StopProgressPulse();
		}
		gdk_threads_leave();
	}
	
}

void Browser::BrowserImpl::BrowserThumbLoader::SetCacheSize(guint uiCacheSize)
{
	m_pBrowserImpl->m_ThumbnailCache.SetSize(uiCacheSize);
}

