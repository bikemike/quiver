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

#include "Query.h"
#include "QueryOpts.h"
#include "ImageList.h"
#include "ImageCache.h"
#include "ImageLoader.h"
#include "IPixbufLoaderObserver.h"
#include "QuiverUtils.h"
#include "QuiverPrefs.h"
#include "QuiverFileOps.h"
#include "Database.h"

#include "Statusbar.h"

#include "IImageListEventHandler.h"
#include "IPreferencesEventHandler.h"
#include "IQueryOptsEventHandler.h"
#include "IconViewThumbLoader.h"

#ifdef QUIVER_MAEMO
#include <hildon-widgets/hildon-controlbar.h>
#include <math.h>
#endif


#if (GLIB_MAJOR_VERSION < 2) || (GLIB_MAJOR_VERSION == 2 && GLIB_MINOR_VERSION < 10)
#define g_object_ref_sink(o) G_STMT_START{	\
	  g_object_ref (o);				\
	  gtk_object_sink ((GtkObject*)o);		\
}G_STMT_END
#endif



// ============================================================================
// Query::QueryImpl: private implementation (hidden from header file)
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


class Query::QueryImpl
{
public:
/* constructors and destructor */
	QueryImpl(Query *parent);
	~QueryImpl();
	
/* member functions */

	void SetUIManager(GtkUIManager *ui_manager);
	void Show();
	void Hide();

	GtkWidget* GetWidget(){return m_pQueryWidget;};
	
	ImageList GetImageList();
	
	void SetImageList(std::list<std::string> *file_list);
	
	void SetImageIndex(int index, bool bDirectionForward, bool bFromIconView = false);

/* member variables */
	QueryOpts m_QueryOpts;
	bool m_bQueryOptsEvent;
	
	GtkWidget *m_pIconView;

	GtkWidget *m_pQueryWidget;
	
	//GtkWidget *hpaned;
	GtkWidget *vpaned;
	GtkWidget *m_pNotebook;
	GtkWidget *m_pSWQueryOpts;
	
	GtkWidget *m_pImageView;

	GtkWidget *m_pLocationEntry;
	GtkWidget *hscale;

	GtkToolItem *m_pToolItemThumbSizer;
	
	GtkUIManager *m_pUIManager;
	guint m_iMergedQueryUI;
	
	StatusbarPtr m_StatusbarPtr;
	
	ImageList m_ResultList;
	QuiverFile m_QuiverFileCurrent;
	ImageCache m_ThumbnailCache;
	ImageCache m_IconCache;
	
	guint m_iTimeoutUpdateListID;

	Query *m_QueryParent;

	ImageLoader m_ImageLoader;
	IPixbufLoaderObserverPtr m_ImageViewPixbufLoaderObserverPtr;
	
/* nested classes */
	//class ViewerEventHandler;
	class ImageListEventHandler : public IImageListEventHandler
	{
	public:
		ImageListEventHandler(Query::QueryImpl *parent){this->parent = parent;};
		virtual void HandleContentsChanged(ImageListEventPtr event);
		virtual void HandleCurrentIndexChanged(ImageListEventPtr event) ;
		virtual void HandleItemAdded(ImageListEventPtr event);
		virtual void HandleItemRemoved(ImageListEventPtr event);
		virtual void HandleItemChanged(ImageListEventPtr event);
	private:
		Query::QueryImpl *parent;
	};
	
	class PreferencesEventHandler : public IPreferencesEventHandler
	{
	public:
		PreferencesEventHandler(QueryImpl* parent) {this->parent = parent;};
		virtual void HandlePreferenceChanged(PreferencesEventPtr event);
	private:
		QueryImpl* parent;
	};
	

	class QueryOptsEventHandler : public IQueryOptsEventHandler
	{
	public:
		QueryOptsEventHandler(QueryImpl* pQueryImpl){this->parent = pQueryImpl;};
		//virtual void HandleSelectionChanged(QueryOptsEventPtr event);
		virtual ~QueryOptsEventHandler(){};
	private:
		QueryImpl* parent;
	};

	class QueryThumbLoader : public IconViewThumbLoader
	{
	public:
		QueryThumbLoader(QueryImpl* pQueryImpl, guint iNumThreads)  : IconViewThumbLoader(iNumThreads)
		{
			m_pQueryImpl = pQueryImpl;
		}
		
		~QueryThumbLoader(){}
		
	protected:
		
		virtual void LoadThumbnail(gulong ulIndex, guint uiWidth, guint uiHeight);
		virtual void GetVisibleRange(gulong* pulStart, gulong* pulEnd);
		virtual void GetIconSize(guint* puiWidth, guint* puiHeight);
		virtual gulong GetNumItems();
		virtual void SetIsRunning(bool bIsRunning);
		virtual void SetCacheSize(guint uiCacheSize);
	
		
	private:
		QueryImpl* m_pQueryImpl; 
		
	};


	IImageListEventHandlerPtr    m_ImageListEventHandlerPtr;
	IPreferencesEventHandlerPtr  m_PreferencesEventHandlerPtr;
	IQueryOptsEventHandlerPtr m_QueryOptsEventHandlerPtr;
	
	QueryThumbLoader m_ThumbnailLoader;
	
};
// ============================================================================


static void query_action_handler_cb(GtkAction *action, gpointer data);

#define ACTION_QUERY_OPEN_LOCATION                      "QueryOpenLocation"
#define ACTION_QUERY_CUT                                "QueryCut"
#define ACTION_QUERY_COPY                               "QueryCopy"
#define ACTION_QUERY_PASTE                              "QueryPaste"
#define ACTION_QUERY_SELECT_ALL                         "QuerySelectAll"
#define ACTION_QUERY_TRASH                              "QueryTrash"
#define ACTION_QUERY_RELOAD                             "QueryReload"
#define ACTION_QUERY_VIEW_PREVIEW                       "QueryViewPreview"
#define ACTION_QUERY_VIEW_QUERYOPTS                     "QueryViewQueryOpts"
#define ACTION_QUERY_ZOOM_IN                            "QueryZoomIn"
#define ACTION_QUERY_ZOOM_OUT                           "QueryZoomOut"
#define ACTION_QUERY_CBIR_QUERY                         "QueryQuery"

#ifdef QUIVER_MAEMO
#define ACTION_QUERY_ZOOM_IN_MAEMO                      ACTION_QUERY_ZOOM_IN"_MAEMO"
#define ACTION_QUERY_ZOOM_OUT_MAEMO                     ACTION_QUERY_ZOOM_OUT"_MAEMO"
#endif

static char *ui_query =
"<ui>"
#ifdef QUIVER_MAEMO
"	<popup name='MenubarMain'>"
#else
"	<menubar name='MenubarMain'>"
#endif
"		<menu action='MenuFile'>"
"			<placeholder action='FileOpenItems'>"
#ifndef QUIVER_MAEMO
"				<menuitem action='"ACTION_QUERY_OPEN_LOCATION"'/>"
#endif
"			</placeholder>"
"		</menu>"
"		<menu action='MenuEdit'>"
"			<placeholder name='CopyPaste'>"
#ifdef FIXME_DISABLED
"				<menuitem action='"ACTION_QUERY_CUT"'/>"
#endif
"				<menuitem action='"ACTION_QUERY_COPY"'/>"
#ifdef FIXME_DISABLED
"				<menuitem action='"ACTION_QUERY_PASTE"'/>"
#endif
"			</placeholder>"
"			<placeholder name='Selection'>"
#ifdef FIXME_DISABLED
"				<menuitem action='"ACTION_QUERY_SELECT_ALL"'/>"
#endif
"			</placeholder>"
"			<placeholder name='Trash'>"
"				<menuitem action='"ACTION_QUERY_TRASH"'/>"
"			</placeholder>"
"		</menu>"
"		<menu action='MenuView'>"
"			<placeholder name='UIItems'>"
"				<menuitem action='"ACTION_QUERY_VIEW_QUERYOPTS"'/>"
"				<menuitem action='"ACTION_QUERY_VIEW_PREVIEW"'/>"
"			</placeholder>"
"			<menuitem action='"ACTION_QUERY_RELOAD"'/>"
"		</menu>"
"		<menu action='MenuQuery'>"
"			<placeholder name ='Query'>"
"				<menuitem action='"ACTION_QUERY_CBIR_QUERY"'/>"
"			</placeholder>"
"		</menu>"
#ifdef QUIVER_MAEMO
"	</popup>"
#else
"	</menubar>"
#endif
"	<toolbar name='ToolbarMain'>"
"		<placeholder name='NavToolItems'>"
//"			<separator/>"
//"			<toolitem action='"ACTION_QUERY_RELOAD"'/>"
//"			<separator/>"
"		</placeholder>"
"		<placeholder name='Trash'>"
"			<toolitem action='"ACTION_QUERY_TRASH"'/>"
"		</placeholder>"
"	</toolbar>"
#ifdef QUIVER_MAEMO
"	<accelerator action='"ACTION_QUERY_ZOOM_IN_MAEMO"'/>"
"	<accelerator action='"ACTION_QUERY_ZOOM_OUT_MAEMO"'/>"
#endif
"</ui>";

static  GtkToggleActionEntry action_entries_toggle[] = {
	{ ACTION_QUERY_VIEW_QUERYOPTS, NULL, "Folder Tree", "<Control><Shift>F", "Show/Hide Folder Tree", G_CALLBACK(query_action_handler_cb),TRUE},
	{ ACTION_QUERY_VIEW_PREVIEW, GTK_STOCK_PROPERTIES, "Preview", "<Control><Shift>p", "Show/Hide Image Preview", G_CALLBACK(query_action_handler_cb),TRUE},
};

static GtkActionEntry action_entries[] = {
	{ ACTION_QUERY_OPEN_LOCATION, NULL, "Open _Location", "<Control>l", "Open a Location", G_CALLBACK( query_action_handler_cb )},
	{ ACTION_QUERY_CUT, GTK_STOCK_CUT, "_Cut", "<Control>X", "Cut image", G_CALLBACK(query_action_handler_cb)},
	{ ACTION_QUERY_COPY, GTK_STOCK_COPY, "Copy", "<Control>C", "Copy image", G_CALLBACK(query_action_handler_cb)},
	{ ACTION_QUERY_PASTE, GTK_STOCK_PASTE, "Paste", "<Control>V", "Paste image", G_CALLBACK(query_action_handler_cb)},
	{ ACTION_QUERY_SELECT_ALL, NULL, "_Select All", "<Control>A", "Select all", G_CALLBACK(query_action_handler_cb)},
	{ ACTION_QUERY_TRASH, GTK_STOCK_DELETE, "_Move To Trash", "Delete", "Move image to the Trash", G_CALLBACK(query_action_handler_cb)},
	{ ACTION_QUERY_RELOAD, GTK_STOCK_REFRESH, "_Reload", "<Control>R", "Refresh the Current View", G_CALLBACK(query_action_handler_cb)},
	{ ACTION_QUERY_CBIR_QUERY, NULL, "_Query for similar images", "", "Query for similar images", G_CALLBACK(query_action_handler_cb)},
#ifdef QUIVER_MAEMO
	{ ACTION_QUERY_ZOOM_IN_MAEMO, NULL, NULL, "F7", NULL, G_CALLBACK(query_action_handler_cb)},
	{ ACTION_QUERY_ZOOM_OUT_MAEMO, NULL, NULL, "F8", NULL, G_CALLBACK(query_action_handler_cb)},
#endif
};





Query::Query() : m_QueryImplPtr( new QueryImpl(this) )
{

}


Query::~Query()
{

}

list<unsigned int> Query::GetSelection()
{
	list<unsigned int> selection_list;
	GList *selection = quiver_icon_view_get_selection(QUIVER_ICON_VIEW(m_QueryImplPtr->m_pIconView));
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
Query::SetUIManager(GtkUIManager *ui_manager)
{
	m_QueryImplPtr->SetUIManager(ui_manager);
}

void
Query::SetStatusbar(StatusbarPtr statusbarPtr)
{
	m_QueryImplPtr->m_ImageLoader.RemovePixbufLoaderObserver(m_QueryImplPtr->m_StatusbarPtr.get());
	
	m_QueryImplPtr->m_StatusbarPtr = statusbarPtr;
	
	m_QueryImplPtr->m_ImageLoader.AddPixbufLoaderObserver(m_QueryImplPtr->m_StatusbarPtr.get());

}

void 
Query::GrabFocus()
{
	gtk_widget_grab_focus (m_QueryImplPtr->m_pIconView);
}

void 
Query::Show()
{
	m_QueryImplPtr->Show();
}

void 
Query::Hide()
{
	m_QueryImplPtr->Hide();
}


void 
Query::SetImageList(std::list<std::string> *file_list)
{
	m_QueryImplPtr->SetImageList(file_list);
}


ImageList 
Query::GetImageList()
{
	return m_QueryImplPtr->GetImageList();
}

GtkWidget* 
Query::GetWidget()
{
	return m_QueryImplPtr->GetWidget();
};


//=============================================================================
//=============================================================================
// private query implementation:
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

static void query_imageview_magnification_changed(QuiverImageView *imageview,gpointer data);
static void query_imageview_reload(QuiverImageView *imageview,gpointer data);

#ifdef QUIVER_MAEMO
static int get_interpreted_thumb_size(gdouble value);
static gdouble get_range_val_from_thumb_size(gint thumbsize);
#endif

static gboolean entry_focus_in ( GtkWidget *widget, GdkEventFocus *event, gpointer user_data)
{
	Query::QueryImpl *pQueryImpl = (Query::QueryImpl*)user_data;

	QuiverUtils::DisconnectUnmodifiedAccelerators(pQueryImpl->m_pUIManager);
		
	//printf("focus in! %s\n", );

	return FALSE;
}

static gboolean entry_focus_out ( GtkWidget *widget, GdkEventFocus *event, gpointer user_data)
{
	Query::QueryImpl *pQueryImpl = (Query::QueryImpl*)user_data;

	QuiverUtils::ConnectUnmodifiedAccelerators(pQueryImpl->m_pUIManager);
	
	return FALSE;
}

static void pane_size_allocate (GtkWidget* widget, GtkAllocation *allocation, gpointer user_data)
{
	PreferencesPtr prefsPtr = Preferences::GetInstance();
	if (GTK_IS_HPANED(widget))
	{
		prefsPtr->SetInteger(QUIVER_PREFS_QUERY,QUIVER_PREFS_QUERY_FOLDER_HPANE,gtk_paned_get_position(GTK_PANED(widget)));
	}
	else if (GTK_IS_VPANED(widget))
	{
		prefsPtr->SetInteger(QUIVER_PREFS_QUERY,QUIVER_PREFS_QUERY_FOLDER_VPANE,gtk_paned_get_position(GTK_PANED(widget)));
	} 
}

/*
	bool visible = false;
	GList* vchildren = gtk_container_get_children(GTK_CONTAINER(pQueryImpl->vpaned));
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
		gtk_widget_hide(pQueryImpl->vpaned);Initialize
	}
*/

void notebook_page_added_q  (GtkNotebook *notebook, 
	GtkWidget *child, guint page_num, gpointer user_data)
{
	Query::QueryImpl *pQueryImpl = (Query::QueryImpl*)user_data;

	gtk_notebook_set_show_tabs(notebook, 1 > gtk_notebook_get_n_pages(notebook));
	gtk_widget_show(GTK_WIDGET(notebook));

	gtk_widget_show(pQueryImpl->vpaned);
}

void notebook_page_removed_q  (GtkNotebook *notebook, 
	GtkWidget *child, guint page_num, gpointer     user_data)
{
	Query::QueryImpl *pQueryImpl = (Query::QueryImpl*)user_data;
	
	gtk_notebook_set_show_tabs(notebook, 1 <= gtk_notebook_get_n_pages(notebook));
	if (0 == gtk_notebook_get_n_pages(notebook))
	{
		gtk_widget_hide(GTK_WIDGET(notebook));
		if (!GTK_WIDGET_VISIBLE(pQueryImpl->m_pImageView))
		{
			gtk_widget_hide(pQueryImpl->vpaned);
		}
	}
}


Query::QueryImpl::QueryImpl(Query *parent) : m_ThumbnailCache(100),
                                       m_IconCache(100),
                                       m_ImageListEventHandlerPtr( new ImageListEventHandler(this) ),
                                       m_PreferencesEventHandlerPtr(new PreferencesEventHandler(this) ),
                                       m_QueryOptsEventHandlerPtr( new QueryOptsEventHandler(this) ),
#ifdef QUIVER_MAEMO
                                       m_ThumbnailLoader(this,2)
#else
                                       m_ThumbnailLoader(this,4)
#endif
{
	PreferencesPtr prefsPtr = Preferences::GetInstance();
	prefsPtr->AddEventHandler( m_PreferencesEventHandlerPtr );
	m_QueryOpts.AddEventHandler(m_QueryOptsEventHandlerPtr);

	m_QueryParent = parent;
	m_pUIManager = NULL;
	m_bQueryOptsEvent = false;

	m_iTimeoutUpdateListID = 0;
	m_iMergedQueryUI = 0;
	/*
	 * layout for the query gui:
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
	      GTK_SIGNAL_FUNC (notebook_page_added_q), this);
	gtk_signal_connect (GTK_OBJECT (m_pNotebook), "page-removed",
	      GTK_SIGNAL_FUNC (notebook_page_removed_q), this);
#endif
	
	m_pIconView = quiver_icon_view_new();
	m_pImageView = quiver_image_view_new();

	gtk_widget_set_no_show_all(m_pImageView, TRUE);

#ifdef QUIVER_MAEMO
	bool bShowPreview = prefsPtr->GetBoolean(QUIVER_PREFS_QUERY,QUIVER_PREFS_QUERY_PREVIEW_SHOW,false);
#else
	bool bShowPreview = prefsPtr->GetBoolean(QUIVER_PREFS_QUERY,QUIVER_PREFS_QUERY_PREVIEW_SHOW,true);
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
	int hpaned_pos = prefsPtr->GetInteger(QUIVER_PREFS_QUERY,QUIVER_PREFS_QUERY_FOLDER_HPANE,200);
	gtk_paned_set_position(GTK_PANED(hpaned),hpaned_pos);

	int vpaned_pos = prefsPtr->GetInteger(QUIVER_PREFS_QUERY,QUIVER_PREFS_QUERY_FOLDER_VPANE,300);
	gtk_paned_set_position(GTK_PANED(vpaned),vpaned_pos);
	
	
	gtk_signal_connect (GTK_OBJECT (hpaned), "size_allocate",
	      GTK_SIGNAL_FUNC (pane_size_allocate), this);
	gtk_signal_connect (GTK_OBJECT (vpaned), "size_allocate",
	      GTK_SIGNAL_FUNC (pane_size_allocate), this);
	
	m_pQueryWidget = hpaned;
	
	m_pSWQueryOpts = gtk_scrolled_window_new(NULL,NULL);
	g_object_ref_sink(m_pSWQueryOpts);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(m_pSWQueryOpts),GTK_POLICY_AUTOMATIC,GTK_POLICY_AUTOMATIC);
	GtkWidget *pQueryOpts = m_QueryOpts.GetWidget();
	
	//gtk_widget_set_no_show_all(pQueryOpts, TRUE);
	gtk_container_add(GTK_CONTAINER(m_pSWQueryOpts),pQueryOpts);
	gtk_widget_show_all(m_pSWQueryOpts);
		
	gtk_widget_show_all(m_pNotebook);
	gtk_widget_set_no_show_all(m_pNotebook, TRUE);

	if (prefsPtr->GetBoolean(QUIVER_PREFS_QUERY,QUIVER_PREFS_QUERY_QUERYOPTS_SHOW,true))
	{	
		gtk_notebook_append_page(GTK_NOTEBOOK(m_pNotebook), m_pSWQueryOpts,gtk_label_new("Folders"));	
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
    			G_CALLBACK (query_imageview_magnification_changed), this);
	
    g_signal_connect (G_OBJECT (m_pImageView), "reload",
    			G_CALLBACK (query_imageview_reload), this);

#ifdef QUIVER_MAEMO
	quiver_icon_view_set_drag_behavior(QUIVER_ICON_VIEW(m_pIconView),QUIVER_ICON_VIEW_DRAG_BEHAVIOR_SCROLL);
#endif

//	quiver_icon_view_set_smooth_scroll(QUIVER_ICON_VIEW(m_pIconView), TRUE);
	quiver_icon_view_set_n_items_func(QUIVER_ICON_VIEW(m_pIconView),(QuiverIconViewGetNItemsFunc)n_cells_callback,this,NULL);
	quiver_icon_view_set_thumbnail_pixbuf_func(QUIVER_ICON_VIEW(m_pIconView),(QuiverIconViewGetThumbnailPixbufFunc)thumbnail_pixbuf_callback,this,NULL);
	quiver_icon_view_set_icon_pixbuf_func(QUIVER_ICON_VIEW(m_pIconView),(QuiverIconViewGetIconPixbufFunc)icon_pixbuf_callback,this,NULL);

	gtk_signal_connect (GTK_OBJECT (hscale), "value_changed",
	      GTK_SIGNAL_FUNC (icon_size_value_changed), this);

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

	gtk_widget_show_all(m_pQueryWidget);
	gtk_widget_hide(m_pQueryWidget);
	gtk_widget_set_no_show_all(m_pQueryWidget,TRUE);


	gdouble thumb_size = (gdouble)prefsPtr->GetInteger(QUIVER_PREFS_QUERY,QUIVER_PREFS_QUERY_THUMB_SIZE);	

	if (thumb_size < 20. || 256. < thumb_size)
	{
		thumb_size = 128.;
	}
#ifndef QUIVER_MAEMO
	gtk_range_set_value(GTK_RANGE(hscale),thumb_size);
#else
	gtk_range_set_value(GTK_RANGE(hscale),get_range_val_from_thumb_size((gint)thumb_size));
#endif

	m_ResultList.EnableDirectoryLookups(false);

}

Query::QueryImpl::~QueryImpl()
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

	prefsPtr->SetInteger(QUIVER_PREFS_QUERY,QUIVER_PREFS_QUERY_THUMB_SIZE,val);

	prefsPtr->RemoveEventHandler( m_PreferencesEventHandlerPtr );
	m_ResultList.RemoveEventHandler(m_ImageListEventHandlerPtr);
	

	if (m_pUIManager)
	{
		g_object_unref(m_pUIManager);
	}
	g_object_unref(m_pSWQueryOpts);
}

void Query::QueryImpl::SetUIManager(GtkUIManager *ui_manager)
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
	GtkActionGroup* actions = gtk_action_group_new ("QueryActions");
	gtk_action_group_add_actions(actions, action_entries, n_entries, this);

	gtk_action_group_add_toggle_actions(actions,
										action_entries_toggle, 
										G_N_ELEMENTS (action_entries_toggle),
										this);
	gtk_ui_manager_insert_action_group (m_pUIManager,actions,0);	

		
	GtkAction* action = QuiverUtils::GetAction(m_pUIManager,ACTION_QUERY_VIEW_PREVIEW);
	if (NULL != action)
	{
		bool bShowPreview = prefsPtr->GetBoolean(QUIVER_PREFS_QUERY,QUIVER_PREFS_QUERY_PREVIEW_SHOW);
		gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), bShowPreview ? TRUE : FALSE);
	}

	action = QuiverUtils::GetAction(m_pUIManager,ACTION_QUERY_VIEW_QUERYOPTS);
	if (NULL != action)
	{
		bool bShowQueryOpts = prefsPtr->GetBoolean(QUIVER_PREFS_QUERY,QUIVER_PREFS_QUERY_QUERYOPTS_SHOW);
		gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), bShowQueryOpts ? TRUE : FALSE);
	}

}

void Query::QueryImpl::Show()
{
	GError *tmp_error;
	tmp_error = NULL;

	if (m_pUIManager && 0 == m_iMergedQueryUI)
	{
		m_iMergedQueryUI = gtk_ui_manager_add_ui_from_string(m_pUIManager,
				ui_query,
				strlen(ui_query),
				&tmp_error);
		gtk_ui_manager_ensure_update(m_pUIManager);

		GtkWidget* toolbar = gtk_ui_manager_get_widget(m_pUIManager,"/ui/ToolbarMain/");
		if (NULL != toolbar)
		{
			gtk_toolbar_insert(GTK_TOOLBAR(toolbar),m_pToolItemThumbSizer,-1);
		}

		if (NULL != tmp_error)
		{
			g_warning("Query::Show() Error: %s\n",tmp_error->message);
		}
	}

 	if (0 != m_ResultList.GetSize())
	{
		quiver_icon_view_set_cursor_cell( QUIVER_ICON_VIEW(m_pIconView),
			m_ResultList.GetCurrentIndex() );
	}
	
	gint cursor_cell = quiver_icon_view_get_cursor_cell(QUIVER_ICON_VIEW(m_pIconView));
 
	if (0 == m_ResultList.GetSize() || m_QuiverFileCurrent != m_ResultList.GetCurrent())
	{
		quiver_image_view_set_pixbuf(QUIVER_IMAGE_VIEW(m_pImageView),NULL);
	}
	else if (0 != m_ResultList.GetSize())
	{
		if ( (gint)m_ResultList.GetCurrentIndex() != cursor_cell  )
		{
		
			g_signal_handlers_block_by_func(m_pIconView,(gpointer)iconview_cursor_changed_cb,this);
	
			quiver_icon_view_set_cursor_cell(
				QUIVER_ICON_VIEW(m_pIconView),
				m_ResultList.GetCurrentIndex() );
			
			g_signal_handlers_unblock_by_func(m_pIconView,(gpointer)iconview_cursor_changed_cb,this);
		}
	
	}

	gtk_widget_show(m_pQueryWidget);
	
	if (m_ResultList.GetSize() && m_QuiverFileCurrent != m_ResultList.GetCurrent() )
	{
		SetImageIndex(m_ResultList.GetCurrentIndex(), true);
	}
	m_ResultList.UnblockHandler(m_ImageListEventHandlerPtr);
}

void Query::QueryImpl::Hide()
{
	gtk_widget_hide(m_pQueryWidget);
	if (m_pUIManager && 0 != m_iMergedQueryUI)
	{	
		gtk_ui_manager_remove_ui(m_pUIManager, m_iMergedQueryUI);
		m_iMergedQueryUI = 0;
		gtk_ui_manager_ensure_update(m_pUIManager);

		GtkWidget* toolbar = gtk_ui_manager_get_widget(m_pUIManager,"/ui/ToolbarMain/");
		if (NULL != toolbar)
		{
			gtk_container_remove(GTK_CONTAINER(toolbar),GTK_WIDGET(m_pToolItemThumbSizer));
		}
	}
	
	m_ResultList.BlockHandler(m_ImageListEventHandlerPtr);
}

void Query::QueryImpl::SetImageList(std::list<std::string> *file_list)
{
	m_ResultList.SetImageList(file_list);
//	m_ResultList.RemoveEventHandler(m_ImageListEventHandlerPtr);
//	
//	m_ResultList = imglist;
//	
//	m_ResultList.AddEventHandler(m_ImageListEventHandlerPtr);
//	
//	if (0 == m_iMergedQueryUI)
//	{
//		m_ResultList.BlockHandler(m_ImageListEventHandlerPtr);
//	}
//
//	list<string> dirs = m_ResultList.GetFolderList();
//	//m_QueryOpts.SetSelectedFolders(dirs);
}


void Query::QueryImpl::SetImageIndex(int index, bool bDirectionForward, bool bFromIconView /* = false */)
{
	gint width=0, height=0;

	QuiverImageViewMode mode = quiver_image_view_get_view_mode_unmagnified(QUIVER_IMAGE_VIEW(m_pImageView));
	
	if (mode != QUIVER_IMAGE_VIEW_MODE_ACTUAL_SIZE && GTK_WIDGET_REALIZED(m_pImageView))
	{

		width = m_pImageView->allocation.width;
		height = m_pImageView->allocation.height;
	}

	m_ResultList.BlockHandler(m_ImageListEventHandlerPtr);
	
	if (m_ResultList.SetCurrentIndex(index))
	{
		if (!bFromIconView)
		{
			g_signal_handlers_block_by_func(m_pIconView,(gpointer)iconview_cursor_changed_cb, this);
			
			quiver_icon_view_set_cursor_cell( QUIVER_ICON_VIEW(m_pIconView),
			      m_ResultList.GetCurrentIndex() );	
	
			g_signal_handlers_unblock_by_func(m_pIconView,(gpointer)iconview_cursor_changed_cb, this);
		}
		
		if (GTK_WIDGET_MAPPED(m_pImageView))
		{
			
			QuiverFile f;
			f = m_ResultList.GetCurrent();
			m_ImageLoader.LoadImageAtSize(f,width,height);
			
			if (bDirectionForward)
			{
				// cache the next image if there is one
				if (m_ResultList.HasNext())
				{
					f = m_ResultList.GetNext();
					m_ImageLoader.CacheImageAtSize(f,width,height);
				}
			}
			else
			{
				// cache the next image if there is one
				if (m_ResultList.HasPrevious())
				{
					f = m_ResultList.GetPrevious();
					m_ImageLoader.CacheImageAtSize(f, width, height);
				}
				
			}
		}	
	}
	
	m_ResultList.UnblockHandler(m_ImageListEventHandlerPtr);
	
	if (m_ResultList.GetSize())
	{
		m_QuiverFileCurrent = m_ResultList.GetCurrent();
	}
	else
	{
		QuiverFile f;
		m_QuiverFileCurrent = f;
	}
	
	// update the toolbar / menu buttons - (un)set sensitive 
	//UpdateUI();
}


ImageList Query::QueryImpl::GetImageList()
{
	return m_ResultList;
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
	Query::QueryImpl* b = (Query::QueryImpl*)user_data;
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
	Query::QueryImpl* b = (Query::QueryImpl*)user_data;
	return b->m_ResultList.GetSize();
}

static GdkPixbuf* icon_pixbuf_callback(QuiverIconView *iconview, guint cell,gpointer user_data)
{
	Query::QueryImpl* b = (Query::QueryImpl*)user_data;
	QuiverFile f = b->m_ResultList[cell];
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
	Query::QueryImpl* b = (Query::QueryImpl*)data;
	gdk_threads_enter();
	b->m_ThumbnailLoader.UpdateList();
	gdk_threads_leave();
	return FALSE;
}

static GdkPixbuf* thumbnail_pixbuf_callback(QuiverIconView *iconview, guint cell, gint* actual_width, gint* actual_height, gpointer user_data)
{
	Query::QueryImpl* b = (Query::QueryImpl*)user_data;

	GdkPixbuf *pixbuf = NULL;
	gboolean need_new_thumb = TRUE;
	
	guint width, height;
	guint thumb_width, thumb_height;
	guint bound_width, bound_height;
	quiver_icon_view_get_icon_size(iconview,&width,&height);
	

	pixbuf = b->m_ThumbnailCache.GetPixbuf(b->m_ResultList[cell].GetURI());

	if (pixbuf)
	{
		*actual_width = b->m_ResultList[cell].GetWidth();
		*actual_height = b->m_ResultList[cell].GetHeight();

		if (4 < b->m_ResultList[cell].GetOrientation())
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
	Query::QueryImpl* b = (Query::QueryImpl*)user_data;
	b->m_QueryParent->EmitItemActivatedEvent();
}

static void iconview_cursor_changed_cb(QuiverIconView *iconview, guint cell, gpointer user_data)
{
	Query::QueryImpl* b = (Query::QueryImpl*)user_data;
	
	b->SetImageIndex(cell,true,true);
}

static void iconview_selection_changed_cb(QuiverIconView *iconview, gpointer user_data)
{
	Query::QueryImpl* b = (Query::QueryImpl*)user_data;

	GtkAction *action = gtk_ui_manager_get_action(b->m_pUIManager,"/ui/ToolbarMain/Trash/QueryTrash");
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
	b->m_QueryParent->EmitSelectionChangedEvent();
}



static void entry_activate(GtkEntry *entry, gpointer user_data)
{
	Query::QueryImpl* b = (Query::QueryImpl*)user_data;
	string entry_text = gtk_entry_get_text(entry);
	list<string> file_list;
	file_list.push_back(entry_text);
	b->m_ResultList.SetImageList(&file_list);
	
}

static void query_imageview_magnification_changed(QuiverImageView *imageview,gpointer data)
{
	Query::QueryImpl* pQueryImpl = (Query::QueryImpl*)data;
	
	double mag = quiver_image_view_get_magnification(QUIVER_IMAGE_VIEW(pQueryImpl->m_pImageView));
	pQueryImpl->m_StatusbarPtr->SetMagnification((int)(mag*100+.5));
}

static void query_imageview_reload(QuiverImageView *imageview,gpointer data)
{
	//printf("#### got a reload message from the imageview\n");
	Query::QueryImpl* pQueryImpl = (Query::QueryImpl*)data;

	if (!pQueryImpl->m_ResultList.GetSize())
		return;

	ImageLoader::LoadParams params = {0};

	params.orientation = pQueryImpl->m_ResultList.GetCurrent().GetOrientation();
	params.reload = true;
	params.fullsize = true;
	params.no_thumb_preview = true;
	params.state = ImageLoader::LOAD;

	pQueryImpl->m_ImageLoader.LoadImage(pQueryImpl->m_ResultList.GetCurrent(),params);
}

static void query_action_handler_cb(GtkAction *action, gpointer data)
{
	Query::QueryImpl* pQueryImpl;
	pQueryImpl = (Query::QueryImpl*)data;
	PreferencesPtr prefsPtr = Preferences::GetInstance();
	
	//printf("Query Action: %s\n",gtk_action_get_name(action));
	
	const gchar * szAction = gtk_action_get_name(action);
	
	if (0 == strcmp(szAction,ACTION_QUERY_RELOAD))
	{
		// clear the caches
		pQueryImpl->m_ResultList.Reload();

		pQueryImpl->m_ThumbnailCache.Clear();
			
		pQueryImpl->m_ThumbnailLoader.UpdateList(true);
		
	}
	else if (0 == strcmp(szAction,ACTION_QUERY_VIEW_QUERYOPTS))
	{
		if( gtk_toggle_action_get_active(GTK_TOGGLE_ACTION(action)) )
		{

#if (GTK_MAJOR_VERSION < 2) || (GTK_MAJOR_VERSION == 2 && GTK_MINOR_VERSION < 10)
			gint page_num = 
#endif
			gtk_notebook_prepend_page(GTK_NOTEBOOK(pQueryImpl->m_pNotebook), pQueryImpl->m_pSWQueryOpts,gtk_label_new("Folders"));
#if (GTK_MAJOR_VERSION < 2) || (GTK_MAJOR_VERSION == 2 && GTK_MINOR_VERSION < 10)
			notebook_page_added(GTK_NOTEBOOK(pQueryImpl->m_pNotebook),pQueryImpl->m_pSWQueryOpts, page_num, pQueryImpl);
#endif
			prefsPtr->SetBoolean(QUIVER_PREFS_QUERY,QUIVER_PREFS_QUERY_QUERYOPTS_SHOW,true);
		}
		else
		{
			gint page_num =  
				gtk_notebook_page_num(GTK_NOTEBOOK(pQueryImpl->m_pNotebook),pQueryImpl->m_pSWQueryOpts);
			gtk_notebook_remove_page(GTK_NOTEBOOK(pQueryImpl->m_pNotebook),page_num);
#if (GTK_MAJOR_VERSION < 2) || (GTK_MAJOR_VERSION == 2 && GTK_MINOR_VERSION < 10)
			notebook_page_removed(GTK_NOTEBOOK(pQueryImpl->m_pNotebook),pQueryImpl->m_pSWQueryOpts, page_num, pQueryImpl);
#endif
			prefsPtr->SetBoolean(QUIVER_PREFS_QUERY,QUIVER_PREFS_QUERY_QUERYOPTS_SHOW,false);
		}
	}
	else if (0 == strcmp(szAction,ACTION_QUERY_VIEW_PREVIEW))
	{
		if( gtk_toggle_action_get_active(GTK_TOGGLE_ACTION(action)) )
		{
			gtk_widget_show(pQueryImpl->m_pImageView);	
			prefsPtr->SetBoolean(QUIVER_PREFS_QUERY,QUIVER_PREFS_QUERY_PREVIEW_SHOW,true);
			gtk_widget_show(pQueryImpl->vpaned);
		}
		else
		{
			gtk_widget_hide(pQueryImpl->m_pImageView);	
			prefsPtr->SetBoolean(QUIVER_PREFS_QUERY,QUIVER_PREFS_QUERY_PREVIEW_SHOW,false);
			if (!GTK_WIDGET_VISIBLE(pQueryImpl->m_pNotebook))
			{
				gtk_widget_hide(pQueryImpl->vpaned);
			}
		}
	}
#ifdef QUIVER_MAEMO
	else if (0 == strcmp(szAction,ACTION_QUERY_ZOOM_IN_MAEMO))
	{
		gtk_range_set_value (GTK_RANGE(pQueryImpl->hscale), gtk_range_get_value (GTK_RANGE(pQueryImpl->hscale)) + 1.);
	}
	else if (0 == strcmp(szAction,ACTION_QUERY_ZOOM_OUT_MAEMO))
	{
		gtk_range_set_value (GTK_RANGE(pQueryImpl->hscale), gtk_range_get_value (GTK_RANGE(pQueryImpl->hscale)) - 1.);
	}
#endif
	else if (0 == strcmp(szAction,ACTION_QUERY_OPEN_LOCATION))
	{
		gtk_widget_grab_focus(pQueryImpl->m_pLocationEntry);
	}
	else if (0 == strcmp(szAction,ACTION_QUERY_COPY))
	{
		GtkClipboard* clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
		
		string strClipText;
		
		GList *selection;
		selection = quiver_icon_view_get_selection(QUIVER_ICON_VIEW(pQueryImpl->m_pIconView));
	
		if (NULL != selection)
		{
			// delete the items!
			GList *sel_itr = selection;

			while (NULL != sel_itr)
			{
				int item = (int)sel_itr->data;
				QuiverFile f = pQueryImpl->m_ResultList[item];
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
	else if (0 == strcmp(szAction, ACTION_QUERY_TRASH))
	{
		gint rval = GTK_RESPONSE_YES;
		GtkWidget* dialog = gtk_message_dialog_new (/*FIXME*/NULL,GTK_DIALOG_MODAL,
								GTK_MESSAGE_QUESTION,GTK_BUTTONS_YES_NO,("Move the selected images to the trash?"));
		gtk_dialog_set_default_response (GTK_DIALOG(dialog), GTK_RESPONSE_YES);
		rval = gtk_dialog_run(GTK_DIALOG(dialog));
		gtk_widget_destroy(dialog);

		switch (rval)
		{
			case GTK_RESPONSE_YES:
			{
				GList *selection;
				selection = quiver_icon_view_get_selection(QUIVER_ICON_VIEW(pQueryImpl->m_pIconView));
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
					
					pQueryImpl->m_ResultList.BlockHandler(pQueryImpl->m_ImageListEventHandlerPtr);
					
					for (ritr = items.rbegin() ; items.rend() != ritr ; ++ritr)
					{
						//printf("delete: %d\n",*ritr);
						QuiverFile f = pQueryImpl->m_ResultList[*ritr];
						
						
						
						if (QuiverFileOps::MoveToTrash(f))
						{
							pQueryImpl->m_ResultList.Remove(*ritr);
						}

					}
					
					pQueryImpl->m_ResultList.UnblockHandler(pQueryImpl->m_ImageListEventHandlerPtr);	
					
					quiver_icon_view_set_cursor_cell(QUIVER_ICON_VIEW(pQueryImpl->m_pIconView),pQueryImpl->m_ResultList.GetCurrentIndex());					
					
					pQueryImpl->m_ThumbnailLoader.UpdateList(true);
				}
				break;
			}
			case GTK_RESPONSE_NO:
				//fall through
			default:
				// do not delete
				// cout << "not trashing file : " << endl;//m_QuiverImplPtr->m_ResultList.GetCurrent().GetURI() << endl;
				break;
		}
	

	}
	else if(0 == strcmp(szAction, ACTION_QUERY_CBIR_QUERY))
	{
		list<unsigned int> selection = pQueryImpl->m_QueryParent->GetSelection();
		if(1 != selection.size())
		{
			printf("can only use one image as a query for now...\n");
			return;
		}
		
		list<unsigned int>::iterator itr = selection.begin();
		string file = pQueryImpl->m_ResultList[*itr].GetURI();
		
		list <string> result = Database::GetInstance()->GetClosestMatch(file);
		
		// display the result

		pQueryImpl->m_ResultList.SetImageList(&result);
	}
}

//=============================================================================
// private query implementation nested classes:
//=============================================================================
void Query::QueryImpl::ImageListEventHandler::HandleContentsChanged(ImageListEventPtr event)
{
	// refresh the list
	parent->SetImageIndex(parent->m_ResultList.GetCurrentIndex(),true);
			
	parent->m_ThumbnailLoader.UpdateList(true);	
	
	// get the list of files and folders in the image list
	list<string> dirs  = parent->m_ResultList.GetFolderList();
	list<string> files = parent->m_ResultList.GetFileList();
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
	
	if (!parent->m_bQueryOptsEvent)
	{
		//parent->m_QueryOpts.SetSelectedFolders(dirs);
	}

}
void Query::QueryImpl::ImageListEventHandler::HandleCurrentIndexChanged(ImageListEventPtr event) 
{
	gint width=0, height=0;
	QuiverImageViewMode mode = quiver_image_view_get_view_mode_unmagnified(QUIVER_IMAGE_VIEW(parent->m_pImageView));
	
	if (mode != QUIVER_IMAGE_VIEW_MODE_ACTUAL_SIZE && GTK_WIDGET_REALIZED(parent->m_pImageView))
	{
		width = parent->m_pImageView->allocation.width;
		height = parent->m_pImageView->allocation.height;
	}
	
	parent->SetImageIndex(event->GetIndex(),true);
	
	parent->m_QueryParent->EmitCursorChangedEvent();
}
void Query::QueryImpl::ImageListEventHandler::HandleItemAdded(ImageListEventPtr event)
{
	// refresh the list
	parent->SetImageIndex(parent->m_ResultList.GetCurrentIndex(),true);
	parent->m_ThumbnailLoader.UpdateList(true);	
}
void Query::QueryImpl::ImageListEventHandler::HandleItemRemoved(ImageListEventPtr event)
{
	parent->SetImageIndex(parent->m_ResultList.GetCurrentIndex(),true);
	parent->m_ThumbnailLoader.UpdateList(true);
}
void Query::QueryImpl::ImageListEventHandler::HandleItemChanged(ImageListEventPtr event)
{
	QuiverFile f = parent->m_ResultList.Get(event->GetIndex());
	//printf ("image list item changed %d: %s\n",event->GetIndex() , f.GetURI());

	parent->m_ThumbnailCache.RemovePixbuf(f.GetURI());
		
	parent->SetImageIndex(parent->m_ResultList.GetCurrentIndex(),true);
	// refresh the list
	parent->m_ThumbnailLoader.UpdateList(true);	
	
}

void Query::QueryImpl::PreferencesEventHandler::HandlePreferenceChanged(PreferencesEventPtr event)
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


//void Query::QueryImpl::QueryOptsEventHandler::HandleSelectionChanged(QueryOptsEventPtr event)
//{
//	list<string> listFolders = parent->m_QueryOpts.GetSelectedFolders();
//	list<string>::iterator itr;
//
//	parent->m_bQueryOptsEvent = true;
//	parent->m_ResultList.UpdateImageList(&listFolders);
//	parent->m_bQueryOptsEvent = false;
//}



void Query::QueryImpl::QueryThumbLoader::LoadThumbnail(gulong ulIndex, guint uiWidth, guint uiHeight)
{

	if (GTK_WIDGET_MAPPED(m_pQueryImpl->m_pIconView) && 
		ulIndex < m_pQueryImpl->m_ResultList.GetSize())
	{
		QuiverFile f = m_pQueryImpl->m_ResultList[ulIndex];
	
		GdkPixbuf *pixbuf = NULL;
		pixbuf = m_pQueryImpl->m_ThumbnailCache.GetPixbuf(f.GetURI());				
	
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

			m_pQueryImpl->m_ThumbnailCache.AddPixbuf(f.GetURI(),pixbuf);
			g_object_unref(pixbuf);

			gdk_threads_enter();
			
			quiver_icon_view_invalidate_cell(QUIVER_ICON_VIEW(m_pQueryImpl->m_pIconView),ulIndex);
			
			gdk_threads_leave();

			pthread_yield();
		}
	}
}

void Query::QueryImpl::QueryThumbLoader::GetVisibleRange(gulong* pulStart, gulong* pulEnd)
{
	quiver_icon_view_get_visible_range(QUIVER_ICON_VIEW(m_pQueryImpl->m_pIconView),pulStart, pulEnd);
}


void Query::QueryImpl::QueryThumbLoader::GetIconSize(guint* puiWidth, guint* puiHeight)
{
	quiver_icon_view_get_icon_size(QUIVER_ICON_VIEW(m_pQueryImpl->m_pIconView), puiWidth, puiHeight);
}

gulong Query::QueryImpl::QueryThumbLoader::GetNumItems()
{
	return m_pQueryImpl->m_ResultList.GetSize();
}

void Query::QueryImpl::QueryThumbLoader::SetIsRunning(bool bIsRunning)
{
	if (m_pQueryImpl->m_StatusbarPtr.get())
	{
		gdk_threads_enter();
		if (bIsRunning)
		{
			m_pQueryImpl->m_StatusbarPtr->StartProgressPulse();
		}
		else
		{
			m_pQueryImpl->m_StatusbarPtr->StopProgressPulse();
		}
		gdk_threads_leave();
	}
	
}

void Query::QueryImpl::QueryThumbLoader::SetCacheSize(guint uiCacheSize)
{
	m_pQueryImpl->m_ThumbnailCache.SetSize(uiCacheSize);
}

