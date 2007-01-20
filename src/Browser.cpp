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


// ============================================================================
// Browser::BrowserImpl: private implementation (hidden from header file)
// ============================================================================
class ThumbnailLoader;

typedef struct _thread_data
{
	ThumbnailLoader *parent;
	int id;
} thread_data;

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
			quiver_image_view_set_pixbuf(m_pImageView,pixbuf);
		};
	virtual void SetPixbufAtSize(GdkPixbuf *pixbuf, gint width, gint height, bool bResetViewMode = true ){
		gboolean bReset = FALSE;
		if (bResetViewMode) bReset = TRUE;
		quiver_image_view_set_pixbuf_at_size_ex(m_pImageView,pixbuf,width,height,bReset);
	};
	virtual void SignalBytesRead(long bytes_read,long total){};
private:
	QuiverImageView *m_pImageView;
};


class ThumbnailLoader
{
public:
	ThumbnailLoader(Browser::BrowserImpl *b,int nThreads);
	~ThumbnailLoader();
	void Start();
	void UpdateList(bool bForce = false);
	void Run(int id);
	static void* run(void *data);
	
private:
	list<unsigned int> m_Files;
	Browser::BrowserImpl *b;
	
	pthread_t *m_pThreadIDs;

	pthread_cond_t  *m_pConditions;
	pthread_mutex_t *m_pConditionMutexes;
	pthread_mutex_t  m_ListMutex;
	
	thread_data     *m_pThreadData;

	guint m_iRangeStart, m_iRangeEnd;
	guint m_bLargeThumbs;
	int m_nThreads;
	
	bool m_bStopThreads;
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

/* member variables */
	FolderTree m_FolderTree;
	bool m_bFolderTreeEvent;
	
	GtkWidget *m_pIconView;

	GtkWidget *m_pBrowserWidget;
	
	//GtkWidget *hpaned;
	GtkWidget *vpaned;
	GtkWidget *notebook;
	GtkWidget *m_pImageView;

	GtkWidget *m_pLocationEntry;
	GtkWidget *hscale;
	
	GtkUIManager *m_pUIManager;
	guint m_iMergedBrowserUI;
	
	StatusbarPtr m_StatusbarPtr;
	
	ImageList m_QuiverFiles;
	QuiverFile m_QuiverFileLast;
	ImageCache m_ThumbnailCacheNormal;
	ImageCache m_ThumbnailCacheLarge;
	
	guint timeout_thumbnail_id;
	gint timeout_thumbnail_current;
	
	Browser *m_BrowserParent;
	ThumbnailLoader m_ThumbnailLoader;
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

	
	IImageListEventHandlerPtr    m_ImageListEventHandlerPtr;
	IPreferencesEventHandlerPtr  m_PreferencesEventHandlerPtr;
	IFolderTreeEventHandlerPtr m_FolderTreeEventHandlerPtr;
};
// ============================================================================



ThumbnailLoader::ThumbnailLoader(Browser::BrowserImpl *b,int nThreads)
{
	m_bStopThreads = false;
	m_iRangeStart = 0;
	m_iRangeEnd   = 0;
	this->b       = b;
	m_bLargeThumbs = false;
	
	m_nThreads = nThreads;
	
	m_pConditions       = new pthread_cond_t[m_nThreads];
	m_pConditionMutexes = new pthread_mutex_t[m_nThreads];
	m_pThreadIDs        = new pthread_t[m_nThreads];
	
	m_pThreadData       = new thread_data[m_nThreads];
	
	
	int i;
	for (i = 0 ; i < m_nThreads; i++)
	{
		pthread_cond_init(&m_pConditions[i],NULL);
		pthread_mutex_init(&m_pConditionMutexes[i], NULL);
		m_pThreadData[i].parent = this;
		m_pThreadData[i].id = i;
		
	}
	pthread_mutex_init(&m_ListMutex, NULL);
	
	for (i = 0 ; i < m_nThreads; i++)
	{
		pthread_create(&m_pThreadIDs[i], NULL, run, &m_pThreadData[i]);
	}
}
ThumbnailLoader::~ThumbnailLoader()
{
	m_bStopThreads = true;
	
	int i;
	for (i = 0 ; i < m_nThreads; i++)
	{
		// this call to gdk_threads_leave is made to make sure we dont get into
		// a deadlock between this thread(gui) and the ThumbnailLoader thread which calls
		// gdk_threads_enter 
		gdk_threads_leave();

		pthread_mutex_lock (&m_pConditionMutexes[i]);
		pthread_cond_signal(&m_pConditions[i]);
		pthread_mutex_unlock (&m_pConditionMutexes[i]);

		pthread_join(m_pThreadIDs[i], NULL);
	}
	
	for (i = 0 ; i < m_nThreads; i++)
	{
		pthread_cond_destroy(&m_pConditions[i]);
		pthread_mutex_destroy(&m_pConditionMutexes[i]);
	}
	delete [] m_pConditions;
	delete [] m_pConditionMutexes;
	delete [] m_pThreadIDs;
	delete [] m_pThreadData;

	pthread_mutex_destroy(&m_ListMutex);
}


void ThumbnailLoader::UpdateList(bool bForce/* = false*/)
{
	bool bLargeThumbs = true;
	gulong start,end;
	quiver_icon_view_get_visible_range(QUIVER_ICON_VIEW(b->m_pIconView),&start,&end);

	guint width, height;
	quiver_icon_view_get_icon_size(QUIVER_ICON_VIEW(b->m_pIconView),&width,&height);

	if (width <= 128 && height <= 128)
	{
		bLargeThumbs = false;
	}
	
	if (bForce || m_iRangeStart != start || m_iRangeEnd != end || bLargeThumbs != m_bLargeThumbs)
	{
		pthread_mutex_lock (&m_ListMutex);
		// view is different, we'll need to create a new list of items to cache
		
		m_iRangeStart = start;
		m_iRangeEnd   = end;
		m_bLargeThumbs = bLargeThumbs;
		
		m_Files.clear();
		
		guint n_cells = b->m_QuiverFiles.GetSize();
		guint n_visible = end - start;
	
		guint cache_size;
		cache_size = n_visible * 13; // set the size to 10 pages			

		if (m_bLargeThumbs)
		{
			b->m_ThumbnailCacheLarge.SetSize(cache_size);
		}
		else
		{
			b->m_ThumbnailCacheNormal.SetSize(cache_size);
		}
		
	
		guint i;
		for (i = start;i < end;i++)
		{
			m_Files.push_back(i);
		}

		guint loop_size = (cache_size - n_visible*2 ) /2;
	
		// FIXME: This algorithm is quite
		// horrible and needs to be rewritten
		for (i = 0;i < loop_size; i++)
		{
			// now add cells not in view
			gint cell_before = start - i -1;
			gint cell_after = start + n_visible + i;
			if (0 <= cell_before)
			{
				// add before cell
				m_Files.push_back(cell_before);
				//printf("offscreen cell: %d - cell_before\n",cell_before);
			}
			else
			{
				gint cell_after2 = start + n_visible + loop_size + i;
				if ((gint)n_cells > cell_after2)
				{
					// add after2 cell
					m_Files.push_back(cell_after2);
					//printf("offscreen cell: %d - cell_after2\n",cell_after2);
				}
			}
			if (cell_after < (gint)n_cells)
			{
				// add after cell
				m_Files.push_back(cell_after);
				//printf("offscreen cell: %d - cell_after\n",cell_after);
			}
			else
			{
				gint cell_before2 = start - n_visible - loop_size -i;
				if (0 <= cell_before2)
				{
					// add before2 cell
					m_Files.push_back(cell_before2);
					//printf("offscreen cell: %d - cell_before2\n",cell_before2);
				}
			}
		}
		
		for (i = start;i < end;i++)
		{
			m_Files.push_back(i);
		}

		int j;
		for (j = 0; j < m_nThreads; j++)
		{
			pthread_mutex_lock (&m_pConditionMutexes[j]);
			pthread_cond_signal(&m_pConditions[j]);
			pthread_mutex_unlock (&m_pConditionMutexes[j]);
		}
		pthread_mutex_unlock (&m_ListMutex);
	}


}

void ThumbnailLoader::Run(int id)
{
	while (true)
	{

		pthread_mutex_lock (&m_ListMutex);

		if (0 == m_Files.size() && !m_bStopThreads)
		{
			// unlock mutex
			pthread_mutex_unlock (&m_ListMutex);
			
			if (b->m_StatusbarPtr.get())
			{
				gdk_threads_enter();
				b->m_StatusbarPtr->StopProgressPulse();
				gdk_threads_leave();
			}
			
			pthread_mutex_lock (&m_pConditionMutexes[id]);
			pthread_cond_wait(&m_pConditions[id],&m_pConditionMutexes[id]);
			pthread_mutex_unlock (&m_pConditionMutexes[id]);
			
			if (b->m_StatusbarPtr.get())
			{
				gdk_threads_enter();
				b->m_StatusbarPtr->StartProgressPulse();
				gdk_threads_leave();
			}

		}
		else
		{
			pthread_mutex_unlock (&m_ListMutex);
		}
		
		if (m_bStopThreads)
		{
			break;
		}
		
		while (true)
		{
			if (m_bStopThreads)
			{
				break;
			}
			//printf("thread %d\n",id); 
			pthread_mutex_lock (&m_ListMutex);
			/*
			list<unsigned int>::iterator itr;
			for (itr = m_Files.begin() ; m_Files.end() != itr; ++itr)
			{
				printf("%d ",*itr);
			}
			printf("\n");
			*/
			if (0 == m_Files.size())
			{
				pthread_mutex_unlock (&m_ListMutex);
				break;
			}
			
			unsigned int n = m_Files.front();
			
			if (n >= b->m_QuiverFiles.GetSize())
			{
				m_Files.clear();
				pthread_mutex_unlock (&m_ListMutex);
				break;
			}
			QuiverFile f = b->m_QuiverFiles[n];
			m_Files.pop_front();
			pthread_mutex_unlock (&m_ListMutex);
			
			//printf("Loading : %s\n",f.GetFilePath().c_str());
			guint width, height;

			quiver_icon_view_get_icon_size(QUIVER_ICON_VIEW(b->m_pIconView),&width,&height);

			GdkPixbuf *pixbuf = NULL;
			
			if (width <= 128 && height <= 128)
			{
				pixbuf = b->m_ThumbnailCacheNormal.GetPixbuf(f.GetURI());				
			}
			else
			{
				pixbuf = b->m_ThumbnailCacheLarge.GetPixbuf(f.GetURI());	
			}


			if (NULL == pixbuf)
			{
				//printf ("[%d] loading thumb : %s\n",id,f.GetURI());

				if (width <= 128 && height <= 128)
				{
					pixbuf = f.GetThumbnail();
				}
				else
				{
					pixbuf = f.GetThumbnail(true);
				}
				
				
				if (NULL != pixbuf)
				{
					if (width <= 128 && height <= 128)
					{
						b->m_ThumbnailCacheNormal.AddPixbuf(f.GetURI(),pixbuf);
					}
					else
					{
						b->m_ThumbnailCacheLarge.AddPixbuf(f.GetURI(),pixbuf);
					}
					
	
					g_object_unref(pixbuf);
					
					if (m_bStopThreads)
					{
						break;
					}

					gdk_threads_enter();
					
					quiver_icon_view_invalidate_cell(QUIVER_ICON_VIEW(b->m_pIconView),n);
					
					gdk_threads_leave();

					pthread_yield();
				}

			}
			else
			{
				g_object_unref(pixbuf);
			}

			
		}

		if (m_bStopThreads)
		{
			break;
		}
		

	}
}

void* ThumbnailLoader::run(void * data)
{
	thread_data *tdata = ((thread_data*)data);
	tdata->parent->Run(tdata->id);
	return 0;
}


static void browser_action_handler_cb(GtkAction *action, gpointer data);

static char *ui_browser =
"<ui>"
"	<menubar name='MenubarMain'>"
"		<menu action='MenuEdit'>"
"			<placeholder name='CopyPaste'>"
"				<menuitem action='BrowserCut'/>"
"				<menuitem action='BrowserCopy'/>"
"				<menuitem action='BrowserPaste'/>"
"			</placeholder>"
"			<placeholder name='Selection'>"
"				<menuitem action='BrowserSelectAll'/>"
"			</placeholder>"
"			<placeholder name='Trash'>"
"				<menuitem action='BrowserTrash'/>"
"			</placeholder>"
"		</menu>"
"		<menu action='MenuView'>"
"			<placeholder name='UIItems'>"
"				<menuitem action='BrowserViewPreview'/>"
"			</placeholder>"
"			<menuitem action='BrowserReload'/>"
"		</menu>"
"	</menubar>"
"	<toolbar name='ToolbarMain'>"
"		<placeholder name='NavToolItems'>"
"			<separator/>"
"			<toolitem action='BrowserReload'/>"
"			<separator/>"
"		</placeholder>"
"		<placeholder name='Trash'>"
"			<toolitem action='BrowserTrash'/>"
"		</placeholder>"
"	</toolbar>"
"</ui>";

static  GtkToggleActionEntry action_entries_toggle[] = {
	{ "BrowserViewPreview", GTK_STOCK_PROPERTIES,"Preview", "<Control><Shift>p", "Show/Hide Image Preview", G_CALLBACK(browser_action_handler_cb),TRUE},
};

static GtkActionEntry action_entries[] = {

	{ "BrowserCut", GTK_STOCK_CUT, "_Cut", "<Control>X", "Cut image", G_CALLBACK(browser_action_handler_cb)},
	{ "BrowserCopy", GTK_STOCK_COPY, "Copy", "<Control>C", "Copy image", G_CALLBACK(browser_action_handler_cb)},
	{ "BrowserPaste", GTK_STOCK_PASTE, "Paste", "<Control>V", "Paste image", G_CALLBACK(browser_action_handler_cb)},
	{ "BrowserSelectAll", NULL, "_Select All", "<Control>A", "Select all", G_CALLBACK(browser_action_handler_cb)},
	{ "BrowserTrash", GTK_STOCK_DELETE, "_Move To Trash", "Delete", "Move image to the Trash", G_CALLBACK(browser_action_handler_cb)},
	{ "BrowserReload", GTK_STOCK_REFRESH, "_Reload", "<Control>R", "Refresh the Current View", G_CALLBACK(browser_action_handler_cb)},
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
static GdkPixbuf* thumbnail_pixbuf_callback(QuiverIconView *iconview, guint cell,gpointer user_data);
static guint n_cells_callback(QuiverIconView *iconview, gpointer user_data);
static void icon_size_value_changed (GtkRange *range,gpointer  user_data);

static void iconview_cell_activated_cb(QuiverIconView *iconview, guint cell, gpointer user_data);
static void iconview_cursor_changed_cb(QuiverIconView *iconview, guint cell, gpointer user_data);
static void iconview_selection_changed_cb(QuiverIconView *iconview, gpointer user_data);

static void entry_activate(GtkEntry *entry, gpointer user_data);

static void browser_imageview_magnification_changed(QuiverImageView *imageview,gpointer data);
static void browser_imageview_reload(QuiverImageView *imageview,gpointer data);

static GtkAction* GetAction(GtkUIManager* ui,const char * action_name)
{
	GList * action_groups = gtk_ui_manager_get_action_groups(ui);
	GtkAction * action = NULL;
	while (NULL != action_groups)
	{
		action = gtk_action_group_get_action (GTK_ACTION_GROUP(action_groups->data),action_name);
		if (NULL != action)
		{
			break;
		}                      
		action_groups = g_list_next(action_groups);
	}

	return action;
}

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


Browser::BrowserImpl::BrowserImpl(Browser *parent) : m_ThumbnailCacheNormal(100),
                                       m_ThumbnailCacheLarge(50),
                                       m_ThumbnailLoader(this,4),
                                       m_ImageListEventHandlerPtr( new ImageListEventHandler(this) ),
                                       m_PreferencesEventHandlerPtr(new PreferencesEventHandler(this) ),
                                       m_FolderTreeEventHandlerPtr( new FolderTreeEventHandler(this) )
{
	PreferencesPtr prefsPtr = Preferences::GetInstance();
	prefsPtr->AddEventHandler( m_PreferencesEventHandlerPtr );
	m_FolderTree.AddEventHandler(m_FolderTreeEventHandlerPtr);

	m_BrowserParent = parent;
	m_pUIManager = NULL;
	m_bFolderTreeEvent = false;

	timeout_thumbnail_current = -1;
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
	GtkWidget *scrolled_window, *sw_folders;
	GtkWidget *hbox,*vbox;
	

	hscale = gtk_hscale_new_with_range(20,256,1);
	gtk_range_set_value(GTK_RANGE(hscale),128);
	gtk_scale_set_value_pos (GTK_SCALE(hscale),GTK_POS_LEFT);
	gtk_scale_set_draw_value(GTK_SCALE(hscale),FALSE);

	gtk_widget_set_size_request(hscale,100,-1);

	
	m_pLocationEntry = gtk_entry_new();
	
	hpaned = gtk_hpaned_new();
	vpaned = gtk_vpaned_new();
	notebook = gtk_notebook_new();
	
	m_pIconView = quiver_icon_view_new();
	m_pImageView = quiver_image_view_new();
	scrolled_window = gtk_scrolled_window_new(NULL,NULL);
	
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),GTK_POLICY_AUTOMATIC,GTK_POLICY_AUTOMATIC);
	gtk_container_add(GTK_CONTAINER(scrolled_window),m_pIconView);
	
	hbox = gtk_hbox_new(FALSE,0);
	vbox = gtk_vbox_new(FALSE,0);
	
	gtk_box_pack_start (GTK_BOX (hbox), m_pLocationEntry, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (hbox), hscale, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), scrolled_window, TRUE, TRUE, 0);
	
	gtk_paned_pack1(GTK_PANED(vpaned),notebook,TRUE,TRUE);
	gtk_paned_pack2(GTK_PANED(vpaned),m_pImageView,FALSE,FALSE);
	
	gtk_paned_pack1(GTK_PANED(hpaned),vpaned,FALSE,FALSE);
	gtk_paned_pack2(GTK_PANED(hpaned),vbox,TRUE,TRUE);
	
	// set the size of the hpane and vpane
	int hpaned_pos = 200;
	if ( prefsPtr->HasKey(QUIVER_PREFS_BROWSER, QUIVER_PREFS_BROWSER_FOLDER_HPANE) )
	{
		hpaned_pos = prefsPtr->GetInteger(QUIVER_PREFS_BROWSER,QUIVER_PREFS_BROWSER_FOLDER_HPANE);
	}
	gtk_paned_set_position(GTK_PANED(hpaned),hpaned_pos);

	if ( prefsPtr->HasKey(QUIVER_PREFS_BROWSER,QUIVER_PREFS_BROWSER_FOLDER_VPANE) )
	{
		int vpaned_pos = prefsPtr->GetInteger(QUIVER_PREFS_BROWSER,QUIVER_PREFS_BROWSER_FOLDER_VPANE);
		gtk_paned_set_position(GTK_PANED(vpaned),vpaned_pos);
	}
	
	gtk_signal_connect (GTK_OBJECT (hpaned), "size_allocate",
	      GTK_SIGNAL_FUNC (pane_size_allocate), this);
	gtk_signal_connect (GTK_OBJECT (vpaned), "size_allocate",
	      GTK_SIGNAL_FUNC (pane_size_allocate), this);
	
	m_pBrowserWidget = hpaned;
	
	sw_folders = gtk_scrolled_window_new(NULL,NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw_folders),GTK_POLICY_AUTOMATIC,GTK_POLICY_AUTOMATIC);
	gtk_container_add(GTK_CONTAINER(sw_folders),m_FolderTree.GetWidget());
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), sw_folders,gtk_label_new("Folders"));

	gtk_notebook_popup_enable(GTK_NOTEBOOK(notebook));
	gtk_notebook_set_scrollable (GTK_NOTEBOOK(notebook),TRUE);

	
/*	
	quiver_icon_view_set_text_func(QUIVER_ICON_VIEW(real_iconview),(QuiverIconViewGetTextFunc)text_callback,user_data,NULL);
*/
	//quiver_image_view_set_transition_type(QUIVER_IMAGE_VIEW(m_pImageView),QUIVER_IMAGE_VIEW_TRANSITION_TYPE_CROSSFADE);
	quiver_image_view_set_magnification_mode(QUIVER_IMAGE_VIEW(m_pImageView),QUIVER_IMAGE_VIEW_MAGNIFICATION_MODE_SMOOTH);

    g_signal_connect (G_OBJECT (m_pImageView), "magnification-changed",
    			G_CALLBACK (browser_imageview_magnification_changed), this);
	
    g_signal_connect (G_OBJECT (m_pImageView), "reload",
    			G_CALLBACK (browser_imageview_reload), this);


//	quiver_icon_view_set_smooth_scroll(QUIVER_ICON_VIEW(m_pIconView), TRUE);
	quiver_icon_view_set_n_items_func(QUIVER_ICON_VIEW(m_pIconView),(QuiverIconViewGetNItemsFunc)n_cells_callback,this,NULL);
	quiver_icon_view_set_thumbnail_pixbuf_func(QUIVER_ICON_VIEW(m_pIconView),(QuiverIconViewGetThumbnailPixbufFunc)thumbnail_pixbuf_callback,this,NULL);
	quiver_icon_view_set_icon_pixbuf_func(QUIVER_ICON_VIEW(m_pIconView),(QuiverIconViewGetThumbnailPixbufFunc)icon_pixbuf_callback,this,NULL);

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

	string strBGColorImg   = prefsPtr->GetString(QUIVER_PREFS_APP,QUIVER_PREFS_APP_BG_IMAGEVIEW);
	string strBGColorThumb = prefsPtr->GetString(QUIVER_PREFS_APP,QUIVER_PREFS_APP_BG_ICONVIEW);

	if (!prefsPtr->GetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_USE_THEME_COLOR,true))
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
	gtk_range_set_value(GTK_RANGE(hscale),thumb_size);

	// temporarily hide the folder tree until we
	// get the code written	
	//gtk_widget_hide(vpaned);
}

Browser::BrowserImpl::~BrowserImpl()
{
	m_ImageLoader.RemovePixbufLoaderObserver(m_StatusbarPtr.get());
	m_ImageLoader.RemovePixbufLoaderObserver(m_ImageViewPixbufLoaderObserverPtr.get());
	
	PreferencesPtr prefsPtr = Preferences::GetInstance();
	gdouble value = gtk_range_get_value (GTK_RANGE(hscale));
	
	prefsPtr->SetInteger(QUIVER_PREFS_BROWSER,QUIVER_PREFS_BROWSER_THUMB_SIZE,(int)value);

	prefsPtr->RemoveEventHandler( m_PreferencesEventHandlerPtr );
	m_QuiverFiles.RemoveEventHandler(m_ImageListEventHandlerPtr);
	
	if (m_pUIManager)
	{
		g_object_unref(m_pUIManager);
	}
}

void Browser::BrowserImpl::SetUIManager(GtkUIManager *ui_manager)
{
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

	/*
	GtkWidget *menuitem = gtk_ui_manager_get_widget(ui_manager,"/ui/MenubarMain/thefilemenu");

	GtkAction *action = gtk_ui_manager_get_action (ui_manager,"/ui/ToolbarMain/BrowserStuff");
	GtkToolItem * tool_button_menu = gtk_menu_tool_button_new(NULL,NULL);

	if (NULL == menuitem)
		printf("NULL!\n");
	gtk_menu_tool_button_set_menu(GTK_MENU_TOOL_BUTTON(tool_button_menu),gtk_menu_item_get_submenu(GTK_MENU_ITEM(menuitem)));
	
	gtk_action_connect_proxy(action,GTK_WIDGET(tool_button_menu));
	g_object_notify (G_OBJECT (action), "tooltip");
	*/
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
		if (NULL != tmp_error)
		{
			g_warning("Browser::Show() Error: %s\n",tmp_error->message);
		}
	}

 	if (0 != m_QuiverFiles.GetSize())
	{
		quiver_icon_view_set_cursor_cell( QUIVER_ICON_VIEW(m_pIconView),
			m_QuiverFiles.GetCurrentIndex() );
	}
	
	gint cursor_cell = quiver_icon_view_get_cursor_cell(QUIVER_ICON_VIEW(m_pIconView));
 
	if (0 == m_QuiverFiles.GetSize() || m_QuiverFileLast != m_QuiverFiles.GetCurrent())
	{
		quiver_image_view_set_pixbuf(QUIVER_IMAGE_VIEW(m_pImageView),NULL);
	}
	else if (0 != m_QuiverFiles.GetSize())
	{
		if ( (gint)m_QuiverFiles.GetCurrentIndex() != cursor_cell  )
		{
		
			g_signal_handlers_block_by_func(m_pIconView,(gpointer)iconview_cursor_changed_cb,this);
	
			quiver_icon_view_set_cursor_cell(
				QUIVER_ICON_VIEW(m_pIconView),
				m_QuiverFiles.GetCurrentIndex() );
			
			g_signal_handlers_unblock_by_func(m_pIconView,(gpointer)iconview_cursor_changed_cb,this);
		}
	
	}

	gtk_widget_show(m_pBrowserWidget);
	
	if (m_QuiverFiles.GetSize() && m_QuiverFileLast != m_QuiverFiles.GetCurrent() )
	{		
		gint width=0, height=0;
		QuiverImageViewMode mode = quiver_image_view_get_view_mode_unmagnified(QUIVER_IMAGE_VIEW(m_pImageView));
		
		if (mode != QUIVER_IMAGE_VIEW_MODE_ACTUAL_SIZE && GTK_WIDGET_REALIZED(m_pImageView))
		{
			width = m_pImageView->allocation.width;
			height = m_pImageView->allocation.height;
			if (width < 20 || height < 20)
			{
				width = height = 20;
			}
		}
		
		if (GTK_WIDGET_MAPPED(m_pImageView))
		{
			m_ImageLoader.LoadImageAtSize(m_QuiverFiles.GetCurrent(),width,height);
		}
	}
	
	m_QuiverFiles.UnblockHandler(m_ImageListEventHandlerPtr);

}

void Browser::BrowserImpl::Hide()
{
	gtk_widget_hide(m_pBrowserWidget);
	if (m_pUIManager)
	{	
		gtk_ui_manager_remove_ui(m_pUIManager,
			m_iMergedBrowserUI);
		m_iMergedBrowserUI = 0;
	}
	
	if (m_QuiverFiles.GetSize())
	{
		m_QuiverFileLast = m_QuiverFiles.GetCurrent();
	}
	else
	{
		QuiverFile f;
		m_QuiverFileLast = f;
	}
	
	m_QuiverFiles.BlockHandler(m_ImageListEventHandlerPtr);
}

void Browser::BrowserImpl::SetImageList(ImageList list)
{
	m_QuiverFiles.RemoveEventHandler(m_ImageListEventHandlerPtr);
	
	m_QuiverFiles = list;
	
	m_QuiverFiles.AddEventHandler(m_ImageListEventHandlerPtr);
	
	if (0 == m_iMergedBrowserUI)
	{
		m_QuiverFiles.BlockHandler(m_ImageListEventHandlerPtr);
	}
}

ImageList Browser::BrowserImpl::GetImageList()
{
	return m_QuiverFiles;
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

static guint n_cells_callback(QuiverIconView *iconview, gpointer user_data)
{
	Browser::BrowserImpl* b = (Browser::BrowserImpl*)user_data;
	return b->m_QuiverFiles.GetSize();
}

static GdkPixbuf* icon_pixbuf_callback(QuiverIconView *iconview, guint cell,gpointer user_data)
{
	Browser::BrowserImpl* b = (Browser::BrowserImpl*)user_data;
	QuiverFile f = b->m_QuiverFiles[cell];

	guint width, height;
	quiver_icon_view_get_icon_size(iconview,&width, &height);
	return f.GetIcon(width,height);
}

static GdkPixbuf* thumbnail_pixbuf_callback(QuiverIconView *iconview, guint cell,gpointer user_data)
{
	Browser::BrowserImpl* b = (Browser::BrowserImpl*)user_data;

	GdkPixbuf *pixbuf;
	
	guint width, height;
	quiver_icon_view_get_icon_size(iconview,&width,&height);
	
	if (width <= 128 && height <= 128)
	{
		pixbuf = b->m_ThumbnailCacheNormal.GetPixbuf(b->m_QuiverFiles[cell].GetURI());
	}
	else
	{
		pixbuf = b->m_ThumbnailCacheLarge.GetPixbuf(b->m_QuiverFiles[cell].GetURI());
		if (NULL == pixbuf)
		{
			pixbuf = b->m_ThumbnailCacheNormal.GetPixbuf(b->m_QuiverFiles[cell].GetURI());
		}
			
	}
	

	b->m_ThumbnailLoader.UpdateList();
	
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
	
	//b->m_QuiverFiles.BlockHandler(b->m_ImageListEventHandlerPtr);
	
	b->m_QuiverFiles.SetCurrentIndex(cell);
	
	//b->m_QuiverFiles.UnblockHandler(b->m_ImageListEventHandlerPtr);

}

static void iconview_selection_changed_cb(QuiverIconView *iconview, gpointer user_data)
{
	Browser::BrowserImpl* b = (Browser::BrowserImpl*)user_data;
//	printf("yes, the selection did change!\n");

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
	b->m_QuiverFiles.SetImageList(&file_list);
	
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

	if (!pBrowserImpl->m_QuiverFiles.GetSize())
		return;

	ImageLoader::LoadParams params = {0};

	params.orientation = pBrowserImpl->m_QuiverFiles.GetCurrent().GetOrientation();
	params.reload = true;
	params.fullsize = true;
	params.no_thumb_preview = true;
	params.state = ImageLoader::LOAD;

	pBrowserImpl->m_ImageLoader.LoadImage(pBrowserImpl->m_QuiverFiles.GetCurrent(),params);
}

static void browser_action_handler_cb(GtkAction *action, gpointer data)
{
	Browser::BrowserImpl* pBrowserImpl;
	pBrowserImpl = (Browser::BrowserImpl*)data;
	
	printf("Browser Action: %s\n",gtk_action_get_name(action));
	
	const gchar * szAction = gtk_action_get_name(action);
	
	if (0 == strcmp(szAction,"BrowserReload"))
	{
		// clear the caches
		pBrowserImpl->m_QuiverFiles.Reload();

		pBrowserImpl->m_ThumbnailCacheLarge.Clear();
		pBrowserImpl->m_ThumbnailCacheNormal.Clear();
			
		pBrowserImpl->m_ThumbnailLoader.UpdateList(true);
		
	}
	else if (0 == strcmp(szAction,"BrowserTrash"))
	{
		gint rval = GTK_RESPONSE_YES;
		if (0) // FIXME: add preference to display trash dialog
		{
		
			GtkWidget* dialog = gtk_message_dialog_new (/*FIXME*/NULL,GTK_DIALOG_MODAL,
									GTK_MESSAGE_QUESTION,GTK_BUTTONS_YES_NO,("Move the selected images to the trash?"));
			rval = gtk_dialog_run(GTK_DIALOG(dialog));
			gtk_widget_destroy(dialog);
		}

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
					
					pBrowserImpl->m_QuiverFiles.BlockHandler(pBrowserImpl->m_ImageListEventHandlerPtr);
					
					for (ritr = items.rbegin() ; items.rend() != ritr ; ++ritr)
					{
						//printf("delete: %d\n",*ritr);
						QuiverFile f = pBrowserImpl->m_QuiverFiles[*ritr];
						
						
						
						if (QuiverFileOps::MoveToTrash(f))
						{
							pBrowserImpl->m_QuiverFiles.Remove(*ritr);
						}

					}
					
					pBrowserImpl->m_QuiverFiles.UnblockHandler(pBrowserImpl->m_ImageListEventHandlerPtr);	
					
					quiver_icon_view_set_cursor_cell(QUIVER_ICON_VIEW(pBrowserImpl->m_pIconView),pBrowserImpl->m_QuiverFiles.GetCurrentIndex());					
					
					pBrowserImpl->m_ThumbnailLoader.UpdateList(true);
				}
				break;
			}
			case GTK_RESPONSE_NO:
				//fall through
			default:
				// do not delete
				cout << "not trashing file : " << endl;//m_QuiverImplPtr->m_ImageList.GetCurrent().GetURI() << endl;
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
	//printf("HandleContentsChanged\n");
	gint width=0, height=0;
	QuiverImageViewMode mode = quiver_image_view_get_view_mode_unmagnified(QUIVER_IMAGE_VIEW(parent->m_pImageView));
	
	if (mode != QUIVER_IMAGE_VIEW_MODE_ACTUAL_SIZE && GTK_WIDGET_REALIZED(parent->m_pImageView))
	{
		width = parent->m_pImageView->allocation.width;
		height = parent->m_pImageView->allocation.height;
	}
	
	if (parent->m_QuiverFiles.GetSize())
	{
		quiver_icon_view_set_cursor_cell(QUIVER_ICON_VIEW(parent->m_pIconView),parent->m_QuiverFiles.GetCurrentIndex());
		if (GTK_WIDGET_MAPPED(parent->m_pImageView))
		{
			parent->m_ImageLoader.LoadImageAtSize(parent->m_QuiverFiles.GetCurrent(),width,height);
		}

	}

	//parent->m_ThumbnailCacheLarge.Clear();
	//parent->m_ThumbnailCacheNormal.Clear();
	quiver_icon_view_invalidate_window(QUIVER_ICON_VIEW(parent->m_pIconView));
			
	parent->m_ThumbnailLoader.UpdateList(true);	
	
	list<string> dirs  = parent->m_QuiverFiles.GetFolderList();
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
	
	if (GTK_WIDGET_MAPPED(parent->m_pImageView))
	{
		parent->m_ImageLoader.LoadImageAtSize(parent->m_QuiverFiles[event->GetIndex()],width,height);
	}
	
	parent->m_BrowserParent->EmitCursorChangedEvent();
}
void Browser::BrowserImpl::ImageListEventHandler::HandleItemAdded(ImageListEventPtr event)
{
	// refresh the list
	parent->m_ThumbnailLoader.UpdateList(true);	
}
void Browser::BrowserImpl::ImageListEventHandler::HandleItemRemoved(ImageListEventPtr event)
{
	parent->m_ThumbnailLoader.UpdateList(true);
}
void Browser::BrowserImpl::ImageListEventHandler::HandleItemChanged(ImageListEventPtr event)
{
	QuiverFile f = parent->m_QuiverFiles.Get(event->GetIndex());
	//printf ("image list item changed %d: %s\n",event->GetIndex() , f.GetURI());

	parent->m_ThumbnailCacheLarge.RemovePixbuf(f.GetURI());
	parent->m_ThumbnailCacheNormal.RemovePixbuf(f.GetURI());
		
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
	parent->m_QuiverFiles.UpdateImageList(&listFolders);
	parent->m_bFolderTreeEvent = false;
	
	// get the list of files and folders in the image list
	list<string> dirs  = parent->m_QuiverFiles.GetFolderList();
	list<string> files = parent->m_QuiverFiles.GetFileList();
	if (1 == dirs.size() && 0 == files.size())
	{
		gtk_entry_set_text(GTK_ENTRY(parent->m_pLocationEntry),dirs.front().c_str());
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

}


