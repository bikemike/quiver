#include <config.h>

#include "BookmarkAddEditDlg.h"
#include "QuiverStockIcons.h"

extern GtkApplication *g_pApp;

#include <list>
#include <vector>




using namespace std;

enum 
{
	COLUMN_ID,
	COLUMN_ICON,
	COLUMN_NAME,
	COLUMN_COUNT
};

class BookmarkAddEditDlg::BookmarkAddEditDlgPriv
{
public:
// constructor, destructor
	BookmarkAddEditDlgPriv(Bookmark b, BookmarkAddEditDlg *parent);
	~BookmarkAddEditDlgPriv();
	
// methods
	void LoadWidgets();
	void UpdateUI();
	void SelectionChanged();
	void ConnectSignals();

// variables
	BookmarkAddEditDlg*     m_pBookmarkAddEditDlg;
	GtkBuilder*         m_pGtkBuilder;
	Bookmark m_Bookmark;
	vector<string> m_vectURIs;
	bool m_bCancelled;

	bool m_bLoadedDlg;
	
	// dlg widgets
	GtkWidget*             m_pWidget;
	GtkWidget*             m_pTreeViewLocations;
	GListStore*            m_pListStoreLocations;
	GtkMultiSelection*     m_pSelectionLocations;
	GtkEntry*              m_pEntryName;
	GtkEntry*              m_pEntryDescription;
	GtkEntry*              m_pEntryIcon;
	GtkButton*             m_pButtonAdd;
	GtkButton*             m_pButtonRemove;
	GtkButton*             m_pButtonOk;
	GtkButton*             m_pButtonCancel;
	GtkCheckButton*        m_pToggleRecursive;
	bool m_bRunDone;
	gint m_iRunResponse;
};


// row item type for the locations column view
typedef struct {
	GObject  parent_instance;
	int      index;
	gchar*   uri;
} LocationItem;

typedef struct {
	GObjectClass parent_class;
} LocationItemClass;

#define LOCATION_ITEM_TYPE (location_item_get_type())
#define LOCATION_ITEM(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST((obj), LOCATION_ITEM_TYPE, LocationItem))

G_DEFINE_TYPE(LocationItem, location_item, G_TYPE_OBJECT)

static void location_item_finalize (GObject* object)
{
	LocationItem* item = LOCATION_ITEM(object);
	g_free(item->uri);
	G_OBJECT_CLASS(location_item_parent_class)->finalize(object);
}

static void location_item_class_init (LocationItemClass* klass)
{
	G_OBJECT_CLASS(klass)->finalize = location_item_finalize;
}

static void location_item_init (LocationItem* item)
{
	item->index = 0;
	item->uri = NULL;
}

static LocationItem* location_item_new (int index, const gchar* uri)
{
	LocationItem* item = static_cast<LocationItem*>(
		g_object_new(LOCATION_ITEM_TYPE, NULL));
	item->index = index;
	item->uri = g_strdup(uri);
	return item;
}

static void location_text_setup (GtkSignalListItemFactory* factory, GtkListItem* list_item, gpointer user_data)
{
	(void)factory; (void)user_data;
	GtkWidget* label = gtk_label_new(NULL);
	gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
	gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_MIDDLE);
	gtk_widget_set_hexpand(label, TRUE);
	gtk_list_item_set_child(list_item, label);
}

static void location_text_bind (GtkSignalListItemFactory* factory, GtkListItem* list_item, gpointer user_data)
{
	(void)factory; (void)user_data;
	LocationItem* item = LOCATION_ITEM(gtk_list_item_get_item(list_item));
	GtkWidget* label = gtk_list_item_get_child(list_item);
	if (!item)
	{
		gtk_label_set_text(GTK_LABEL(label), "");
		return;
	}
	gtk_label_set_text(GTK_LABEL(label), item->uri ? item->uri : "");
}

static GtkListItemFactory* location_column_factory ()
{
	GtkListItemFactory* factory = gtk_signal_list_item_factory_new();
	g_signal_connect(factory, "setup", G_CALLBACK(location_text_setup), NULL);
	g_signal_connect(factory, "bind", G_CALLBACK(location_text_bind), NULL);
	return factory;
}


BookmarkAddEditDlg::BookmarkAddEditDlg() : m_PrivPtr(new BookmarkAddEditDlg::BookmarkAddEditDlgPriv(Bookmark(),this))
{
}
BookmarkAddEditDlg::BookmarkAddEditDlg(Bookmark b) : m_PrivPtr(new BookmarkAddEditDlg::BookmarkAddEditDlgPriv(b,this))
{
}


Bookmark BookmarkAddEditDlg::GetBookmark() const
{
	  return m_PrivPtr->m_Bookmark;
}

GtkWidget* BookmarkAddEditDlg::GetWidget() const
{
	  return NULL;
}


void BookmarkAddEditDlg::Run()
{
	if (m_PrivPtr->m_bLoadedDlg)
	{
		m_PrivPtr->m_bRunDone = false;
		m_PrivPtr->m_iRunResponse = GTK_RESPONSE_NONE;
		/* Keep the dialog above its parent window without grabbing input. */
		if (NULL != g_pApp)
		{
			GtkWindow *mainWin = gtk_application_get_active_window(g_pApp);
			if (mainWin != NULL)
				gtk_window_set_transient_for(GTK_WINDOW(m_PrivPtr->m_pWidget), mainWin);
		}
		gtk_widget_set_visible(m_PrivPtr->m_pWidget, TRUE);

		GMainContext* ctx = g_main_context_default();
		while (!m_PrivPtr->m_bRunDone)
		{
			g_main_context_iteration(ctx, TRUE);
		}

		if (GTK_RESPONSE_OK == m_PrivPtr->m_iRunResponse)
		{
			m_PrivPtr->m_bCancelled = false;

			m_PrivPtr->m_Bookmark.SetName( gtk_editable_get_text(GTK_EDITABLE(m_PrivPtr->m_pEntryName)) );
			m_PrivPtr->m_Bookmark.SetDescription( gtk_editable_get_text(GTK_EDITABLE(m_PrivPtr->m_pEntryDescription)) );
			m_PrivPtr->m_Bookmark.SetIcon( gtk_editable_get_text(GTK_EDITABLE(m_PrivPtr->m_pEntryIcon)) );
			m_PrivPtr->m_Bookmark.SetRecursive( gtk_check_button_get_active(m_PrivPtr->m_pToggleRecursive) ? true : false );
			list<string> uris(m_PrivPtr->m_vectURIs.begin(), m_PrivPtr->m_vectURIs.end());
			m_PrivPtr->m_Bookmark.SetURIs(uris);
		}
	}
}

bool BookmarkAddEditDlg::Cancelled() const
{
	return m_PrivPtr->m_bCancelled;
}

// private stuff


// prototypes
static void  on_clicked (GtkButton *button, gpointer user_data);
static void  on_toggled (GtkCheckButton *button, gpointer user_data);
static void selection_changed (GtkSelectionModel* selection, guint position, guint n_items, gpointer user_data);


BookmarkAddEditDlg::BookmarkAddEditDlgPriv::BookmarkAddEditDlgPriv(Bookmark b, BookmarkAddEditDlg *parent) :
        m_pBookmarkAddEditDlg(parent), m_Bookmark(b)
{
	m_bLoadedDlg = false;
	m_bCancelled = true;


	list<string> uris = m_Bookmark.GetURIs();
	m_vectURIs = vector<string>(uris.begin(), uris.end());
	m_pListStoreLocations = NULL;
	m_pSelectionLocations = NULL;

	m_pGtkBuilder = gtk_builder_new();
	const gchar* objectids[] = {
		"BookmarkAddEditDialog", 
		NULL};
	gtk_builder_add_objects_from_file(m_pGtkBuilder, QUIVER_DATADIR "/" "quiver.ui", (const char**)objectids, NULL);

	LoadWidgets();
	UpdateUI();
	ConnectSignals();
}

BookmarkAddEditDlg::BookmarkAddEditDlgPriv::~BookmarkAddEditDlgPriv()
{
	if (NULL != m_pWidget)
	{
		gtk_window_destroy(GTK_WINDOW(m_pWidget));
		m_pWidget = NULL;
	}
	if (NULL != m_pSelectionLocations)
	{
		g_object_unref(m_pSelectionLocations);
		m_pSelectionLocations = NULL;
	}
	m_pListStoreLocations = NULL;
	if (NULL != m_pGtkBuilder)
	{
		g_object_unref(m_pGtkBuilder);
		m_pGtkBuilder = NULL;
	}
}


void BookmarkAddEditDlg::BookmarkAddEditDlgPriv::LoadWidgets()
{

	if (NULL != m_pGtkBuilder)
	{
		m_pWidget                = GTK_WIDGET(gtk_builder_get_object(m_pGtkBuilder, "BookmarkAddEditDialog"));
		m_pTreeViewLocations     = GTK_WIDGET(     gtk_builder_get_object (m_pGtkBuilder, "bookmark_treeview_locations") );

		m_pButtonCancel          = GTK_BUTTON( gtk_button_new_with_mnemonic("_Cancel") );
		m_pButtonOk              = GTK_BUTTON( gtk_button_new_with_mnemonic("_OK") );
		gtk_widget_add_css_class(GTK_WIDGET(m_pButtonOk), "suggested-action");

		if (m_pWidget)
		{
			GtkHeaderBar* hbar = GTK_HEADER_BAR(gtk_header_bar_new());
			gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(hbar), TRUE);
			gtk_header_bar_pack_end(hbar, GTK_WIDGET(m_pButtonCancel));
			gtk_header_bar_pack_end(hbar, GTK_WIDGET(m_pButtonOk));
			gtk_window_set_titlebar(GTK_WINDOW(m_pWidget), GTK_WIDGET(hbar));
		}

		if (m_pTreeViewLocations)
		{
			m_pListStoreLocations = g_list_store_new(LOCATION_ITEM_TYPE);
			m_pSelectionLocations = gtk_multi_selection_new(
				G_LIST_MODEL(m_pListStoreLocations));
			gtk_column_view_set_model(GTK_COLUMN_VIEW(m_pTreeViewLocations),
				GTK_SELECTION_MODEL(m_pSelectionLocations));

			GtkColumnViewColumn* column =
				gtk_column_view_column_new("bookmark",
					location_column_factory());
			gtk_column_view_column_set_expand(column, TRUE);
			gtk_column_view_append_column(GTK_COLUMN_VIEW(m_pTreeViewLocations), column);
		}

		m_pButtonAdd             = GTK_BUTTON       ( gtk_builder_get_object (m_pGtkBuilder, "bookmark_button_add") );
		m_pButtonRemove          = GTK_BUTTON       ( gtk_builder_get_object (m_pGtkBuilder, "bookmark_button_remove") );
		m_pToggleRecursive       = GTK_CHECK_BUTTON( gtk_builder_get_object(m_pGtkBuilder, "bookmark_checkbutton_recursive"));
		m_pEntryName             = GTK_ENTRY        ( gtk_builder_get_object(m_pGtkBuilder, "bookmark_entry_name"));
		m_pEntryDescription      = GTK_ENTRY        ( gtk_builder_get_object(m_pGtkBuilder, "bookmark_entry_description"));
		m_pEntryIcon             = GTK_ENTRY        ( gtk_builder_get_object(m_pGtkBuilder, "bookmark_entry_icon"));

		m_bLoadedDlg = (
				NULL != m_pWidget && 
				NULL != m_pTreeViewLocations && 
				NULL != m_pButtonAdd && 
				NULL != m_pButtonRemove && 
				NULL != m_pButtonOk && 
				NULL != m_pButtonCancel && 
				NULL != m_pEntryName && 
				NULL != m_pEntryDescription && 
				NULL != m_pEntryIcon && 
				NULL != m_pToggleRecursive
				); 
	
		gtk_editable_set_text(GTK_EDITABLE(m_pEntryName), m_Bookmark.GetName().c_str());
		gtk_editable_set_text(GTK_EDITABLE(m_pEntryDescription), m_Bookmark.GetDescription().c_str());
		gtk_editable_set_text(GTK_EDITABLE(m_pEntryIcon), m_Bookmark.GetIcon().c_str());

		gtk_check_button_set_active(m_pToggleRecursive, m_Bookmark.GetRecursive() ? TRUE : FALSE );

	}
}

void BookmarkAddEditDlg::BookmarkAddEditDlgPriv::SelectionChanged()
{
	if (!m_pSelectionLocations || !m_pListStoreLocations || !m_pButtonRemove)
		return;

	GtkSelectionModel* sel = GTK_SELECTION_MODEL(m_pSelectionLocations);
	guint n = g_list_model_get_n_items(G_LIST_MODEL(m_pListStoreLocations));
	guint selection_count = 0;

	for (guint i = 0 ; i < n ; i++)
	{
		if (gtk_selection_model_is_selected(sel, i))
			selection_count++;
	}

	if (0 == selection_count)
	{
		// disable remove
		gtk_widget_set_sensitive(GTK_WIDGET(m_pButtonRemove),FALSE);
	}
	else
	{
		gtk_widget_set_sensitive(GTK_WIDGET(m_pButtonRemove),TRUE);
	}
}

void BookmarkAddEditDlg::BookmarkAddEditDlgPriv::UpdateUI()
{
	if (m_bLoadedDlg)
	{
		
		g_list_store_remove_all(m_pListStoreLocations);
		vector<string>::iterator itr;
		int idx = 0;

		for (itr = m_vectURIs.begin(); m_vectURIs.end() != itr; ++itr)
		{
			LocationItem* item = location_item_new(idx, itr->c_str());
			g_list_store_append(m_pListStoreLocations, G_OBJECT(item));
			g_object_unref(item);
			++idx;
		}

		SelectionChanged();

	}	
}


void BookmarkAddEditDlg::BookmarkAddEditDlgPriv::ConnectSignals()
{
	if (m_bLoadedDlg)
	{
		g_signal_connect(m_pWidget, "close-request",
			G_CALLBACK(+[](GtkWidget* widget, gpointer user_data) -> gboolean {
				BookmarkAddEditDlg::BookmarkAddEditDlgPriv *priv =
					static_cast<BookmarkAddEditDlg::BookmarkAddEditDlgPriv*>(user_data);
				priv->m_iRunResponse = GTK_RESPONSE_CANCEL;
				priv->m_bRunDone = true;
				gtk_widget_set_visible(widget, FALSE);
				return TRUE;
			}), this);

		g_signal_connect(m_pToggleRecursive,
			"toggled",(GCallback)on_toggled,this);

		g_signal_connect(m_pButtonAdd,
			"clicked",(GCallback)on_clicked,this);
		g_signal_connect(m_pButtonRemove,
			"clicked",(GCallback)on_clicked,this);
		g_signal_connect(m_pButtonOk,
			"clicked",(GCallback)on_clicked,this);
		g_signal_connect(m_pButtonCancel,
			"clicked",(GCallback)on_clicked,this);

		g_signal_connect(G_OBJECT(m_pSelectionLocations),
			"selection-changed",G_CALLBACK(selection_changed),this);
	}
}


static void  on_toggled (GtkCheckButton *togglebutton, gpointer user_data)
{
	BookmarkAddEditDlg::BookmarkAddEditDlgPriv *priv = static_cast<BookmarkAddEditDlg::BookmarkAddEditDlgPriv*>(user_data);
	if (priv->m_pToggleRecursive == togglebutton)
	{ 
		gboolean bRecursive = gtk_check_button_get_active(togglebutton);
  (void)bRecursive;
	}
}

static GMainLoop *s_bookmark_loop = NULL;

static void on_folder_selected(GObject *source, GAsyncResult *result, gpointer user_data)
{
	BookmarkAddEditDlg::BookmarkAddEditDlgPriv *priv =
		static_cast<BookmarkAddEditDlg::BookmarkAddEditDlgPriv*>(user_data);
	GtkFileDialog *filedialog = GTK_FILE_DIALOG(source);
	GError *error = NULL;
	GFile *folder = gtk_file_dialog_select_folder_finish(filedialog, result, &error);
	if (folder != NULL)
	{
		char* uri = g_file_get_uri(folder);
		priv->m_vectURIs.push_back(std::string(uri));
		g_free(uri);
		priv->UpdateUI();
		g_object_unref(folder);
	}
	else if (error != NULL)
	{
		g_error_free(error);
	}
	if (s_bookmark_loop != NULL)
	{
		g_main_loop_quit(s_bookmark_loop);
	}
}

static void  on_clicked (GtkButton *button, gpointer user_data)
{
	BookmarkAddEditDlg::BookmarkAddEditDlgPriv *priv = static_cast<BookmarkAddEditDlg::BookmarkAddEditDlgPriv*>(user_data);
	
	list<int> values;

	GtkSelectionModel* sel = GTK_SELECTION_MODEL(priv->m_pSelectionLocations);
	guint n = g_list_model_get_n_items(G_LIST_MODEL(priv->m_pListStoreLocations));

	for (guint i = 0 ; i < n ; i++)
	{
		if (gtk_selection_model_is_selected(sel, i))
		{
			LocationItem* item = LOCATION_ITEM(
				g_list_model_get_item(G_LIST_MODEL(priv->m_pListStoreLocations), i));
			values.push_back(item->index);
			g_object_unref(item);
		}
	}
	if (button == priv->m_pButtonAdd)
	{
		GtkFileDialog *filedialog = gtk_file_dialog_new();
		gtk_file_dialog_set_title(filedialog, "Select Folder");

		s_bookmark_loop = g_main_loop_new(NULL, FALSE);
		gtk_file_dialog_select_folder(filedialog, GTK_WINDOW(priv->m_pWidget), NULL,
			on_folder_selected, priv);
		g_main_loop_run(s_bookmark_loop);
		g_main_loop_unref(s_bookmark_loop);
		s_bookmark_loop = NULL;
		g_object_unref(filedialog);
	}
	else if (button == priv->m_pButtonRemove)
	{
		values.sort();
		values.reverse();
		for (list<int>::iterator itr = values.begin(); values.end() != itr; ++itr)
		{
			if (*itr < (int)priv->m_vectURIs.size())
			{
				priv->m_vectURIs.erase(priv->m_vectURIs.begin() + *itr);
			}
			priv->UpdateUI();
		}
	}
	else if (button == priv->m_pButtonOk)
	{
		priv->m_iRunResponse = GTK_RESPONSE_OK;
		priv->m_bRunDone = true;
		gtk_widget_set_visible(priv->m_pWidget, FALSE);
	}
	else if (button == priv->m_pButtonCancel)
	{
		priv->m_iRunResponse = GTK_RESPONSE_CANCEL;
		priv->m_bRunDone = true;
		gtk_widget_set_visible(priv->m_pWidget, FALSE);
	}
}

static void selection_changed (GtkSelectionModel* selection, guint position, guint n_items, gpointer user_data)
{
	(void)selection;
	(void)position;
	(void)n_items;
	BookmarkAddEditDlg::BookmarkAddEditDlgPriv *priv = static_cast<BookmarkAddEditDlg::BookmarkAddEditDlgPriv*>(user_data);
	if (priv)
	{
		priv->SelectionChanged();
	}
}


