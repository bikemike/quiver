#include <config.h>

#include "BookmarkAddEditDlg.h"

#ifdef HAVE_LIBGLADE
#include <glade/glade.h>
#endif

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
#ifdef HAVE_LIBGLADE
	GladeXML*         m_pGladeXML;
#endif
	Bookmark m_Bookmark;
	vector<string> m_vectURIs;
	bool m_bCancelled;

	bool m_bLoadedDlg;
	
	// dlg widgets
	GtkFileChooserButton*  m_pFCBtnPhotoLibrary;
	
	GtkWidget*             m_pWidget;
	GtkTreeView*           m_pTreeViewLocations;
	GtkEntry*              m_pEntryName;
	GtkEntry*              m_pEntryDescription;
	GtkEntry*              m_pEntryIcon;
	GtkButton*             m_pButtonAdd;
	GtkButton*             m_pButtonRemove;
	GtkButton*             m_pButtonOk;
	GtkButton*             m_pButtonCancel;
	GtkToggleButton*       m_pToggleRecursive;
};


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
#ifdef HAVE_LIBGLADE
	if (m_PrivPtr->m_bLoadedDlg)
	{
		gint result = gtk_dialog_run(GTK_DIALOG(m_PrivPtr->m_pWidget));
		if (GTK_RESPONSE_OK == result)
		{
			m_PrivPtr->m_bCancelled = false;

			m_PrivPtr->m_Bookmark.SetName( gtk_entry_get_text(m_PrivPtr->m_pEntryName) );
			m_PrivPtr->m_Bookmark.SetDescription( gtk_entry_get_text(m_PrivPtr->m_pEntryDescription) );
			m_PrivPtr->m_Bookmark.SetIcon( gtk_entry_get_text(m_PrivPtr->m_pEntryIcon) );
			m_PrivPtr->m_Bookmark.SetRecursive( gtk_toggle_button_get_active(m_PrivPtr->m_pToggleRecursive) ? true : false );
			list<string> uris(m_PrivPtr->m_vectURIs.begin(), m_PrivPtr->m_vectURIs.end());
			m_PrivPtr->m_Bookmark.SetURIs(uris);
		}
		gtk_widget_destroy(m_PrivPtr->m_pWidget);
	}
#endif
}

bool BookmarkAddEditDlg::Cancelled() const
{
	return m_PrivPtr->m_bCancelled;
}

// private stuff


// prototypes
static void  on_clicked (GtkButton *button, gpointer user_data);
static void  on_toggled (GtkToggleButton *button, gpointer user_data);
static void selection_changed (GtkTreeSelection *treeselection, gpointer user_data);
static void cell_edited_callback (GtkCellRendererText *cell, gchar *path_string, gchar *new_text, gpointer user_data);


BookmarkAddEditDlg::BookmarkAddEditDlgPriv::BookmarkAddEditDlgPriv(Bookmark b, BookmarkAddEditDlg *parent) :
        m_pBookmarkAddEditDlg(parent), m_Bookmark(b)
{
	m_bLoadedDlg = false;
	m_bCancelled = true;


	list<string> uris = m_Bookmark.GetURIs();
	m_vectURIs = vector<string>(uris.begin(), uris.end());
#ifdef HAVE_LIBGLADE
	m_pGladeXML = glade_xml_new (QUIVER_GLADEDIR "/" "quiver.glade", "BookmarkAddEditDialog", NULL);
	if (NULL != m_pGladeXML)
	{
		LoadWidgets();
		UpdateUI();
		ConnectSignals();
	}
#endif
}

BookmarkAddEditDlg::BookmarkAddEditDlgPriv::~BookmarkAddEditDlgPriv()
{
#ifdef HAVE_LIBGLADE
	if (NULL != m_pGladeXML)
	{
		g_object_unref(m_pGladeXML);
		m_pGladeXML = NULL;
	}
#endif
}


void BookmarkAddEditDlg::BookmarkAddEditDlgPriv::LoadWidgets()
{

#ifdef HAVE_LIBGLADE
	if (NULL != m_pGladeXML)
	{
		m_pWidget                = glade_xml_get_widget (m_pGladeXML, "BookmarkAddEditDialog");
		m_pTreeViewLocations     = GTK_TREE_VIEW(     glade_xml_get_widget (m_pGladeXML, "treeview_locations") );

		m_pButtonCancel          = GTK_BUTTON( gtk_button_new_from_stock(GTK_STOCK_CANCEL) );
		m_pButtonOk              = GTK_BUTTON( gtk_button_new_from_stock(GTK_STOCK_OK) );


		gtk_widget_show(GTK_WIDGET(m_pButtonCancel));
		gtk_widget_show(GTK_WIDGET(m_pButtonOk));

		if (m_pWidget)
		{
			gtk_box_pack_start(GTK_BOX(GTK_DIALOG(m_pWidget)->action_area),GTK_WIDGET(m_pButtonCancel),FALSE,TRUE,5);
			gtk_box_pack_start(GTK_BOX(GTK_DIALOG(m_pWidget)->action_area),GTK_WIDGET(m_pButtonOk),FALSE,TRUE,5);
		}

		if (m_pTreeViewLocations)
		{
			GtkTreeViewColumn*column;
			GtkCellRenderer* renderer;

			renderer = gtk_cell_renderer_text_new ();
			g_object_set (G_OBJECT (renderer),  "mode", GTK_CELL_RENDERER_MODE_INERT,  NULL);
			g_object_set (G_OBJECT (renderer),  "editable", TRUE,  NULL);
			g_signal_connect(renderer, "edited", (GCallback) cell_edited_callback, this);

			column = gtk_tree_view_column_new_with_attributes ("bookmark",
			  renderer,
			  "text",COLUMN_NAME,
			NULL);

			gtk_tree_view_append_column (m_pTreeViewLocations, column);

#if GTK_MAJOR_VERSION == 2  &&  GTK_MINOR_VERSION >= 12 || GTK_MAJOR_VERSION > 2
			g_object_set(G_OBJECT(m_pTreeViewLocations),"show-expanders",FALSE,NULL);
#endif
	
			gtk_tree_view_set_search_column (m_pTreeViewLocations,COLUMN_NAME);
			GtkTreeSelection* selection = gtk_tree_view_get_selection(m_pTreeViewLocations);
			gtk_tree_selection_set_mode(selection,GTK_SELECTION_MULTIPLE);
		}

		m_pButtonAdd             = GTK_BUTTON       ( glade_xml_get_widget (m_pGladeXML, "button_add") );
		m_pButtonRemove          = GTK_BUTTON       ( glade_xml_get_widget (m_pGladeXML, "button_remove") );
		m_pToggleRecursive       = GTK_TOGGLE_BUTTON( glade_xml_get_widget(m_pGladeXML, "checkbutton_recursive"));
		m_pEntryName             = GTK_ENTRY        ( glade_xml_get_widget(m_pGladeXML, "entry_name"));
		m_pEntryDescription      = GTK_ENTRY        ( glade_xml_get_widget(m_pGladeXML, "entry_description"));
		m_pEntryIcon             = GTK_ENTRY        ( glade_xml_get_widget(m_pGladeXML, "entry_icon"));

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

	
		gtk_entry_set_text(m_pEntryName, m_Bookmark.GetName().c_str());
		gtk_entry_set_text(m_pEntryDescription, m_Bookmark.GetDescription().c_str());
		gtk_entry_set_text(m_pEntryIcon, m_Bookmark.GetIcon().c_str());

		gtk_toggle_button_set_active(m_pToggleRecursive, m_Bookmark.GetRecursive() ? TRUE : FALSE );

	}
#endif
}

void BookmarkAddEditDlg::BookmarkAddEditDlgPriv::SelectionChanged()
{
	GList* paths;
	GList* path_itr;
	GtkTreeModel *model;
	GtkTreeSelection* selection;
	int selection_count = 0;
	
	GtkTreePath *path;
	GtkTreeIter iter;

	model = gtk_tree_view_get_model(m_pTreeViewLocations);
	selection = gtk_tree_view_get_selection(m_pTreeViewLocations);

	bool bTop = false, bBottom = false;
	GtkTreeIter iter2;
	GtkTreePath *path_top = NULL;
	GtkTreePath *path_bottom = NULL;
	int n_children = gtk_tree_model_iter_n_children(model,NULL);
	if (n_children)
	{
		gtk_tree_model_iter_nth_child(model, &iter2, NULL,0);
		path_top = gtk_tree_model_get_path(model,&iter2);
		gtk_tree_model_iter_nth_child(model, &iter2, NULL,n_children-1);
		path_bottom = gtk_tree_model_get_path(model,&iter2);
	}

	paths = gtk_tree_selection_get_selected_rows(selection,&model);
	path_itr = paths;
	while (NULL != path_itr)
	{
		path = (GtkTreePath*)path_itr->data;
		gtk_tree_model_get_iter(model,&iter,path);

		if (0 == gtk_tree_path_compare(path, path_top))
		{
			bTop = true;
		}

		if (0 == gtk_tree_path_compare(path, path_bottom))
		{
			bBottom = true;
		}
		
		path_itr = g_list_next(path_itr);
		++selection_count;
	}

	g_list_foreach (paths, (GFunc)gtk_tree_path_free, NULL);
	g_list_free (paths);

	if (n_children)
	{
		gtk_tree_path_free(path_top);
		gtk_tree_path_free(path_bottom);
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
		
		//m_pTreeViewLocations;
		GtkTreeStore *store;
		store = gtk_tree_store_new(COLUMN_COUNT, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING);
		vector<string>::iterator itr;

		for (itr = m_vectURIs.begin(); m_vectURIs.end() != itr; ++itr)
		{
			GtkTreeIter iter1 = {0};
			gtk_tree_store_append (store, &iter1, NULL);  
			gtk_tree_store_set (store, &iter1,
				COLUMN_ID, itr - m_vectURIs.begin(), 
				COLUMN_NAME, itr->c_str(),
				-1);
		}
		gtk_tree_view_set_model(m_pTreeViewLocations, GTK_TREE_MODEL (store));
		g_object_unref(store);

		SelectionChanged();

	}	
}


void BookmarkAddEditDlg::BookmarkAddEditDlgPriv::ConnectSignals()
{
	if (m_bLoadedDlg)
	{
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

		GtkTreeSelection* selection = gtk_tree_view_get_selection(m_pTreeViewLocations);
		g_signal_connect(G_OBJECT(selection),
			"changed",G_CALLBACK(selection_changed),this);
	}
}


static void  on_toggled (GtkToggleButton *togglebutton, gpointer user_data)
{
	BookmarkAddEditDlg::BookmarkAddEditDlgPriv *priv = static_cast<BookmarkAddEditDlg::BookmarkAddEditDlgPriv*>(user_data);
	if (priv->m_pToggleRecursive == togglebutton)
	{ 
		gboolean bRecursive = gtk_toggle_button_get_active(togglebutton);
	}
}

static void  on_clicked (GtkButton *button, gpointer user_data)
{
	BookmarkAddEditDlg::BookmarkAddEditDlgPriv *priv = static_cast<BookmarkAddEditDlg::BookmarkAddEditDlgPriv*>(user_data);
	
	list<int> values;

	// move the bookmark one up in the row
	GList* paths;
	GList* path_itr;
	GtkTreePath *path;
	GtkTreeModel *model;
	GtkTreeSelection* selection;
	GtkTreeIter iter;
	int value;
	
	model = gtk_tree_view_get_model(priv->m_pTreeViewLocations);
	selection = gtk_tree_view_get_selection(priv->m_pTreeViewLocations);


	paths = gtk_tree_selection_get_selected_rows(selection,&model);
	path_itr = paths;
	while (NULL != path_itr)
	{
		path = (GtkTreePath*)path_itr->data;
		gtk_tree_model_get_iter(model,&iter,path);

		gtk_tree_model_get (model,&iter,COLUMN_ID,&value,-1);
		values.push_back(value);
		path_itr = g_list_next(path_itr);
	}	
	g_list_foreach (paths, (GFunc)gtk_tree_path_free, NULL);
	g_list_free (paths);
	// have to remove after iterating the model because
	// removing modifiees the model
	if (button == priv->m_pButtonAdd)
	{
		GtkWidget* widget = gtk_file_chooser_dialog_new ("Select Folder",
			NULL,
			GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
			GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
			GTK_STOCK_OK, GTK_RESPONSE_OK, 
		   	NULL);

		gint rval  = gtk_dialog_run(GTK_DIALOG(widget));
		if (GTK_RESPONSE_OK == rval)
		{
			char* uri = gtk_file_chooser_get_uri(GTK_FILE_CHOOSER(widget));
			priv->m_vectURIs.push_back(uri);
			g_free(uri);
			priv->UpdateUI();
		}
		gtk_widget_destroy(widget);
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
		gtk_dialog_response(GTK_DIALOG(priv->m_pWidget), GTK_RESPONSE_OK);
	}
	else if (button == priv->m_pButtonCancel)
	{
		gtk_dialog_response(GTK_DIALOG(priv->m_pWidget), GTK_RESPONSE_CANCEL);
	}
}

static void selection_changed (GtkTreeSelection *treeselection, gpointer user_data)
{
	BookmarkAddEditDlg::BookmarkAddEditDlgPriv *priv = static_cast<BookmarkAddEditDlg::BookmarkAddEditDlgPriv*>(user_data);
	priv->SelectionChanged();
}

static void
cell_edited_callback (GtkCellRendererText *cell, gchar *path_string, gchar *new_text, gpointer user_data)
{
	BookmarkAddEditDlg::BookmarkAddEditDlgPriv *priv = static_cast<BookmarkAddEditDlg::BookmarkAddEditDlgPriv*>(user_data);

	GtkTreePath *path;
	GtkTreeIter child = {0};
	GtkTreeIter parent = {0};

	unsigned int value;

	GtkTreeModel *pTreeModel = gtk_tree_view_get_model(priv->m_pTreeViewLocations);

	path = gtk_tree_path_new_from_string(path_string);
	gtk_tree_model_get_iter(pTreeModel,&child,path);

	gtk_tree_model_iter_parent(pTreeModel,&parent, &child);
	
	gtk_tree_model_get (pTreeModel,&child,COLUMN_ID,&value,-1);

	if (value < priv->m_vectURIs.size() )
	{
		priv->m_vectURIs[value] = new_text;
	}
	priv->UpdateUI();
}


