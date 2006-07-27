#include <config.h>

#include <pthread.h>

#include <gtk/gtk.h>
#include <string.h>
#include <list>
#include <map>
using namespace std;

#include <libquiver/quiver-icon-view.h>
#include <libquiver/quiver-image-view.h>

#include "Browser.h"
#include "ImageList.h"
#include "ImageCache.h"
#include "ImageLoader.h"
#include "IPixbufLoaderObserver.h"

// ============================================================================
// Browser::BrowserImpl: private implementation (hidden from header file)
// ============================================================================
class ThumbnailLoader;

typedef struct _thread_data
{
	ThumbnailLoader *parent;
	int id;
} thread_data;

class ImageViewPixbufLoaderObserver : public IPixbufLoaderObserver
{
public:
	ImageViewPixbufLoaderObserver(QuiverImageView *imageview){m_ImageView = imageview;};
	virtual ~ImageViewPixbufLoaderObserver(){};

	virtual void ConnectSignals(GdkPixbufLoader *loader){
			quiver_image_view_connect_pixbuf_loader_signals(m_ImageView,loader);
		};
	virtual void ConnectSignalSizePrepared(GdkPixbufLoader * loader){
			quiver_image_view_connect_pixbuf_size_prepared_signal(m_ImageView,loader);
		};

	// custom calls
	virtual void SetPixbuf(GdkPixbuf * pixbuf){
			quiver_image_view_set_pixbuf(m_ImageView,pixbuf);
		};
	virtual void SetPixbufAtSize(GdkPixbuf *pixbuf, gint width, gint height){
		quiver_image_view_set_pixbuf_at_size(m_ImageView,pixbuf,width,height);
		};
	// the image that will be displayed immediately
	virtual void SetQuiverFile(QuiverFile quiverFile){};
	
	// the image that is being cached for future use
	virtual void SetCacheQuiverFile(QuiverFile quiverFile){};
	virtual void SignalBytesRead(long bytes_read,long total){};
private:
	QuiverImageView *m_ImageView;
};

class ThumbnailLoader
{
public:
	ThumbnailLoader(Browser::BrowserImpl *b,int nThreads);
	~ThumbnailLoader();
	void Start();
	void UpdateList();
	void Run(int id);
	static void* run(void *data);
	
private:
	list<int> m_Files;
	Browser::BrowserImpl *b;
	
	pthread_t *m_pThreadIDs;

	pthread_cond_t  *m_pConditions;
	pthread_mutex_t *m_pConditionMutexes;
	pthread_mutex_t  m_ListMutex;
	
	thread_data     *m_pThreadData;

	guint m_iRangeStart, m_iRangeEnd;
	guint m_bLargeThumbs;
	int m_nThreads;

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
	GtkWidget *iconview;
	
	GtkWidget *m_pBrowserWidget;
	
	//GtkWidget *hpaned;
	GtkWidget *vpaned;
	GtkWidget *notebook;
	GtkWidget *imageview;

	GtkWidget *entry;
	GtkWidget *hscale;
	
	GtkUIManager *m_pUIManager;
	guint m_iMergedBrowserUI;
	
	ImageList m_QuiverFiles;
	ImageCache m_ThumbnailCacheNormal;
	ImageCache m_ThumbnailCacheLarge;
	
	guint timeout_thumbnail_id;
	gint timeout_thumbnail_current;
	
	Browser *m_BrowserParent;
	ThumbnailLoader m_ThumbnailLoader;
	ImageLoader m_ImageLoader;
	ImageViewPixbufLoaderObserver *m_pImageViewPixbufLoaderObserver;
};
// ============================================================================



ThumbnailLoader::ThumbnailLoader(Browser::BrowserImpl *b,int nThreads)
{
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
}
ThumbnailLoader::~ThumbnailLoader()
{
	int i;
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

void ThumbnailLoader::Start()
{
	int i;
	for (i = 0 ; i < m_nThreads; i++)
	{
		pthread_create(&m_pThreadIDs[i], NULL, run, &m_pThreadData[i]);
		pthread_detach(m_pThreadIDs[i]);
	}
}
void ThumbnailLoader::UpdateList()
{

	bool bLargeThumbs = true;
	guint start,end;
	quiver_icon_view_get_visible_range(QUIVER_ICON_VIEW(b->iconview),&start,&end);

	guint width, height;
	quiver_icon_view_get_icon_size(QUIVER_ICON_VIEW(b->iconview),&width,&height);

	if (width <= 128 && height <= 128)
	{
		bLargeThumbs = false;
	}
	if (m_iRangeStart != start || m_iRangeEnd != end || bLargeThumbs != m_bLargeThumbs)
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

		if (0 == m_Files.size())
		{
			// unlock mutex
			pthread_mutex_unlock (&m_ListMutex);
			
			pthread_mutex_lock (&m_pConditionMutexes[id]);
			pthread_cond_wait(&m_pConditions[id],&m_pConditionMutexes[id]);
			pthread_mutex_unlock (&m_pConditionMutexes[id]);

		}
		else
		{
			pthread_mutex_unlock (&m_ListMutex);
		}
		
		while (true)
		{
			//printf("thread %d\n",id); 
			pthread_mutex_lock (&m_ListMutex);
			if (0 == m_Files.size())
			{
				pthread_mutex_unlock (&m_ListMutex);
				break;
			}
			
			int n = m_Files.front();
			QuiverFile f = b->m_QuiverFiles[n];
			m_Files.pop_front();
			pthread_mutex_unlock (&m_ListMutex);
			
			//printf("Loading : %s\n",f.GetFilePath().c_str());
			guint width, height;
			quiver_icon_view_get_icon_size(QUIVER_ICON_VIEW(b->iconview),&width,&height);

			GdkPixbuf *pixbuf = NULL;
			
			if (width <= 128 && height <= 128)
			{
				pixbuf = b->m_ThumbnailCacheNormal.GetPixbuf(f.GetFilePath());				
			}
			else
			{
				pixbuf = b->m_ThumbnailCacheLarge.GetPixbuf(f.GetFilePath());	
			}


			if (NULL == pixbuf)
			{
				//printf ("[%d] loading thumb : %s\n",id,f.GetFilePath().c_str());

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
						b->m_ThumbnailCacheNormal.AddPixbuf(f.GetFilePath(),pixbuf);
					}
					else
					{
						b->m_ThumbnailCacheLarge.AddPixbuf(f.GetFilePath(),pixbuf);
					}
					
	
					g_object_unref(pixbuf);
					
					gdk_threads_enter();
					
					quiver_icon_view_invalidate_cell(QUIVER_ICON_VIEW(b->iconview),n);
					
					gdk_threads_leave();
					//if (n % 15 == 0)
					{
						pthread_yield();
					}
				}
				//usleep(1000);
			}
			else
			{
				g_object_unref(pixbuf);
			}

			
		}
		

	}
}

void* ThumbnailLoader::run(void * data)
{
	thread_data *tdata = ((thread_data*)data);
	tdata->parent->Run(tdata->id);
	return 0;
}


static char *ui_browser =
"<ui>"
"	<menubar name='MenubarMain'>"
"		<menu action='MenuEdit'>"
"			<placeholder name='CopyPaste'>"
"				<menuitem action='Cut'/>"
"				<menuitem action='Copy'/>"
"				<menuitem action='Paste'/>"
"			</placeholder>"
"			<placeholder name='Selection'>"
"				<menuitem action='SelectAll'/>"
"			</placeholder>"
"			<placeholder name='Trash'>"
"				<menuitem action='ImageTrash'/>"
"			</placeholder>"
"		</menu>"
"		<menu action='MenuView'>"
"			<placeholder name='UIItems'>"
"				<menuitem action='ViewPreview'/>"
"			</placeholder>"
"		</menu>"
"	</menubar>"
"	<toolbar name='ToolbarMain'>"
"		<separator/>"
"	</toolbar>"
"</ui>";

static  GtkToggleActionEntry action_entries_toggle[] = {
	{ "ViewPreview", GTK_STOCK_PROPERTIES,"Preview", "<Control><Shift>p", "Show/Hide Image Preview", G_CALLBACK(NULL),TRUE},
};

static GtkActionEntry action_entries[] = {
	
//	{ "MenuFile2", NULL, "_File" }, 
	{ "BrowserStuff", GTK_STOCK_PROPERTIES, "what is this?", "", "Menu Tool Item Test", G_CALLBACK(NULL)},
	
	{ "Cut", GTK_STOCK_CUT, "_Cut", "<Control>X", "Cut image", G_CALLBACK(NULL)},
	{ "Copy", GTK_STOCK_COPY, "Copy", "<Control>C", "Copy image", G_CALLBACK(NULL)},
	{ "Paste", GTK_STOCK_PASTE, "Paste", "<Control>V", "Paste image", G_CALLBACK(NULL)},
	{ "SelectAll", NULL, "_Select All", "<Control>A", "Select all", G_CALLBACK(NULL)},
	{ "ImageTrash", GTK_STOCK_DELETE, "_Move To Trash", "Delete", "Move image to the Trash", G_CALLBACK(NULL)},
	
};





Browser::Browser() : m_BrowserImplPtr( new BrowserImpl(this) )
{

}


Browser::~Browser()
{

}

list<int> Browser::GetSelection()
{
	list<int> selection_list;
	GList *selection = quiver_icon_view_get_selection(QUIVER_ICON_VIEW(m_BrowserImplPtr->iconview));
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

static GdkPixbuf* icon_pixbuf_callback(QuiverIconView *iconview, guint cell,gpointer user_data);
static GdkPixbuf* thumbnail_pixbuf_callback(QuiverIconView *iconview, guint cell,gpointer user_data);
static guint n_cells_callback(QuiverIconView *iconview, gpointer user_data);
static void icon_size_value_changed (GtkRange *range,gpointer  user_data);

static void iconview_cell_activated_cb(QuiverIconView *iconview, guint cell, gpointer user_data);
static void iconview_cursor_changed_cb(QuiverIconView *iconview, guint cell, gpointer user_data);
static void iconview_selection_changed_cb(QuiverIconView *iconview, gpointer user_data);

static void entry_activate(GtkEntry *entry, gpointer user_data);


//=============================================================================
// BrowswerImpl Callback Prototypes
//=============================================================================

Browser::BrowserImpl::BrowserImpl(Browser *parent) : m_ThumbnailCacheNormal(100),m_ThumbnailCacheLarge(50), m_ThumbnailLoader(this,4)
{
	m_ThumbnailLoader.Start();
	m_BrowserParent = parent;
	m_pUIManager = NULL;
	timeout_thumbnail_current = -1;
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
	

	hscale = gtk_hscale_new_with_range(20,256,1);
	gtk_range_set_value(GTK_RANGE(hscale),128.);
	gtk_scale_set_value_pos (GTK_SCALE(hscale),GTK_POS_LEFT);
	gtk_scale_set_draw_value(GTK_SCALE(hscale),FALSE);

	gtk_widget_set_size_request(hscale,100,-1);

	
	entry = gtk_entry_new();
	
	hpaned = gtk_hpaned_new();
	vpaned = gtk_vpaned_new();
	notebook = gtk_notebook_new();
	
	iconview = quiver_icon_view_new();
	imageview = quiver_image_view_new();
	scrolled_window = gtk_scrolled_window_new(NULL,NULL);
	
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),GTK_POLICY_AUTOMATIC,GTK_POLICY_AUTOMATIC);
	gtk_container_add(GTK_CONTAINER(scrolled_window),iconview);
	
	hbox = gtk_hbox_new(FALSE,0);
	vbox = gtk_vbox_new(FALSE,0);
	
	gtk_box_pack_start (GTK_BOX (hbox), entry, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (hbox), hscale, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), scrolled_window, TRUE, TRUE, 0);
	
	gtk_paned_pack1(GTK_PANED(vpaned),notebook,TRUE,TRUE);
	gtk_paned_pack2(GTK_PANED(vpaned),imageview,FALSE,FALSE);
	
	gtk_paned_pack1(GTK_PANED(hpaned),vpaned,FALSE,FALSE);
	gtk_paned_pack2(GTK_PANED(hpaned),vbox,TRUE,TRUE);
	
	m_pBrowserWidget = hpaned;
	
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook),gtk_drawing_area_new(),gtk_label_new("Folders"));
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook),gtk_drawing_area_new(),gtk_label_new("Calendar"));
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook),gtk_drawing_area_new(),gtk_label_new("Categories"));

	gtk_notebook_popup_enable(GTK_NOTEBOOK(notebook));
	gtk_notebook_set_scrollable (GTK_NOTEBOOK(notebook),TRUE);

	
/*	
	quiver_icon_view_set_text_func(QUIVER_ICON_VIEW(real_iconview),(QuiverIconViewGetTextFunc)text_callback,user_data,NULL);
*/
	//quiver_image_view_set_transition_type(QUIVER_IMAGE_VIEW(imageview),QUIVER_IMAGE_VIEW_TRANSITION_TYPE_CROSSFADE);
	quiver_image_view_set_magnification_mode(QUIVER_IMAGE_VIEW(imageview),QUIVER_IMAGE_VIEW_MAGNIFICATION_MODE_SMOOTH);
	
	quiver_icon_view_set_smooth_scroll(QUIVER_ICON_VIEW(iconview), TRUE);
	quiver_icon_view_set_n_items_func(QUIVER_ICON_VIEW(iconview),(QuiverIconViewGetNItemsFunc)n_cells_callback,this,NULL);
	quiver_icon_view_set_thumbnail_pixbuf_func(QUIVER_ICON_VIEW(iconview),(QuiverIconViewGetThumbnailPixbufFunc)thumbnail_pixbuf_callback,this,NULL);
	quiver_icon_view_set_icon_pixbuf_func(QUIVER_ICON_VIEW(iconview),(QuiverIconViewGetThumbnailPixbufFunc)icon_pixbuf_callback,this,NULL);

	gtk_signal_connect (GTK_OBJECT (hscale), "value_changed",
	      GTK_SIGNAL_FUNC (icon_size_value_changed), this);	

	g_signal_connect(G_OBJECT(iconview),"cell_activated",G_CALLBACK(iconview_cell_activated_cb),this);
	g_signal_connect(G_OBJECT(iconview),"cursor_changed",G_CALLBACK(iconview_cursor_changed_cb),this);
	g_signal_connect(G_OBJECT(iconview),"selection_changed",G_CALLBACK(iconview_selection_changed_cb),this);	
	
	g_signal_connect(G_OBJECT(entry),"activate",G_CALLBACK(entry_activate),this);

	GdkColor dark_grey;
	gdk_color_parse("#444",&dark_grey);
	gtk_widget_modify_bg (iconview, GTK_STATE_NORMAL, &dark_grey );

	GdkColor black;
	gdk_color_parse("black",&black);
	gtk_widget_modify_bg (imageview, GTK_STATE_NORMAL, &black );
	
/*
	quiver_icon_view_set_overlay_pixbuf_func(QUIVER_ICON_VIEW(real_iconview),(QuiverIconViewGetOverlayPixbufFunc)overlay_pixbuf_callback,user_data,NULL);
*/

	m_pImageViewPixbufLoaderObserver = new ImageViewPixbufLoaderObserver(QUIVER_IMAGE_VIEW(imageview));
	m_ImageLoader.AddPixbufLoaderObserver(m_pImageViewPixbufLoaderObserver);
	m_ImageLoader.Start();

	gtk_widget_show_all(m_pBrowserWidget);
	gtk_widget_hide(m_pBrowserWidget);
	gtk_widget_set_no_show_all(m_pBrowserWidget,TRUE);


	// temporarily hide the folder tree until we
	// get the code written	
	gtk_widget_hide(vpaned);
}

Browser::BrowserImpl::~BrowserImpl()
{
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
	
	gtk_widget_show(m_pBrowserWidget);
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
}

void Browser::BrowserImpl::SetImageList(ImageList list)
{
	m_QuiverFiles = list;
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
	quiver_icon_view_set_icon_size(QUIVER_ICON_VIEW(b->iconview), (gint)value,(gint)value);
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
		pixbuf = b->m_ThumbnailCacheNormal.GetPixbuf(b->m_QuiverFiles[cell].GetFilePath());
	}
	else
	{
		pixbuf = b->m_ThumbnailCacheLarge.GetPixbuf(b->m_QuiverFiles[cell].GetFilePath());
		if (NULL == pixbuf)
		{
			pixbuf = b->m_ThumbnailCacheNormal.GetPixbuf(b->m_QuiverFiles[cell].GetFilePath());
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
	if (GTK_WIDGET_VISIBLE(b->iconview))
	{
		b->m_ImageLoader.LoadImage(b->m_QuiverFiles[cell]);
	
		b->m_QuiverFiles.SetCurrentIndex(cell);
	}

	b->m_BrowserParent->EmitCursorChangedEvent();
}

static void iconview_selection_changed_cb(QuiverIconView *iconview, gpointer user_data)
{
	Browser::BrowserImpl* b = (Browser::BrowserImpl*)user_data;
//	printf("yes, the selection did change!\n");

	GtkAction *action = gtk_ui_manager_get_action(b->m_pUIManager,"/ui/ToolbarMain/ImageTrash");
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
	b->m_BrowserParent->EmitSelectionChangedEvent();
}



static void entry_activate(GtkEntry *entry, gpointer user_data)
{
	Browser::BrowserImpl* b = (Browser::BrowserImpl*)user_data;
	string entry_text = gtk_entry_get_text(entry);
	list<string> file_list;
	file_list.push_back(entry_text);
	b->m_QuiverFiles.SetImageList(&file_list);
	printf("activated the entry box!\n");
	
}

