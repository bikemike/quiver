#include <config.h>

#include <pthread.h>

#include "FolderTree.h"

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <libgnomevfs/gnome-vfs.h>

#ifndef QUIVER_MAEMO
#include <libgnomeui/gnome-icon-lookup.h>
#else
#include <hildon-widgets/hildon-file-system-model.h>
#endif

#include <set>

#define QUIVER_TREE_COLUMN_TOGGLE      "column_toggle"
#define QUIVER_FOLDER_TREE_ROOT_NAME   "Filesystem"


#ifdef QUIVER_MAEMO

#define ICON_MAEMO_DEVICE "qgn_list_filesys_divc_cls"
#define ICON_MAEMO_AUDIO "qgn_list_filesys_audio_fldr"
#define ICON_MAEMO_DOCUMENTS "qgn_list_filesys_doc_fldr"
#define ICON_MAEMO_GAMES "qgn_list_filesys_games_fldr"
#define ICON_MAEMO_IMAGES "qgn_list_filesys_image_fldr"
#define ICON_MAEMO_VIDEO "qgn_list_filesys_video_fldr"
#define ICON_MAEMO_MMC "qgn_list_filesys_mmc_root"

// folder expanders
#define ICON_MAEMO_EXPAND "qgn_list_gene_fldr_exp"
#define ICON_MAEMO_COLAPSE "qgn_list_gene_fldr_clp"

// folder open / closed
#define ICON_MAEMO_FOLDER_CLOSED "qgn_list_gene_fldr_cls"
#define ICON_MAEMO_FOLDER_OPEN "qgn_list_gene_fldr_opn"
#endif

/*GtkTreeStore *store;*/

enum
{
	FILE_TREE_COLUMN_CHECKBOX,
	FILE_TREE_COLUMN_ICON,
	FILE_TREE_COLUMN_DISPLAY_NAME,
	FILE_TREE_COLUMN_URI,
	FILE_TREE_COLUMN_SEPARATOR,
	FILE_TREE_COLUMN_COUNT,
};

static gint sort_func (GtkTreeModel *model,GtkTreeIter *a, GtkTreeIter *b, gpointer user_data);
static gboolean folder_tree_is_separator (GtkTreeModel *model,GtkTreeIter *iter,gpointer data);
static void folder_tree_selection_changed (GtkTreeSelection *treeselection, gpointer user_data);
static void folder_tree_cursor_changed (GtkTreeView *treeview, gpointer user_data);
static gboolean view_onButtonPressed (GtkWidget *treeview, GdkEventButton *event, gpointer userdata);
static gboolean view_on_key_press(GtkWidget *treeview, GdkEventKey *event, gpointer userdata);
static gboolean view_onPopupMenu (GtkWidget *treeview, gpointer userdata);
static void view_onRowActivated (GtkTreeView *treeview, GtkTreePath *path, GtkTreeViewColumn *col, gpointer userdata);
static void signal_folder_tree_row_expanded(GtkTreeView *treeview, GtkTreeIter *iter_parent, GtkTreePath *treepath, gpointer data);

static GtkTreeIter* folder_tree_add_subdir(GtkTreeModel* model, GtkTreeIter *iter_parent, const gchar* name, gboolean duplicate_check);
static void folder_tree_clear_all_checkboxes(GtkTreeModel *model);

class FolderTree::FolderTreeImpl
{
public:
// constructor / destructor
	FolderTreeImpl(FolderTree *pFolderTree);
	~FolderTreeImpl();


// methods
	void CreateWidget();
	void PopulateTreeModel(GtkTreeStore *store);

	void SetSelectedFolders(std::list<std::string> &uris);
	std::list<std::string> GetSelectedFolders() const;

// member variables
	GtkWidget*      m_pWidget;
	FolderTree*     m_pFolderTree;
	GHashTable*     m_pHashRootNodeOrder;
	GtkTreeIter*    m_pTreeIterScrollTo;
	std::set<guint> m_setFolderThreads;
	guint           m_iTimeoutScrollToCell;
};


FolderTree::FolderTree() : m_FolderTreeImplPtr(new FolderTreeImpl(this))
{
	
}

FolderTree::~FolderTree()
{
}

GtkWidget* FolderTree::GetWidget() const
{
	return m_FolderTreeImplPtr->m_pWidget;
}

void FolderTree::SetUIManager(GtkUIManager* pUIManager)
{
}

std::list<std::string> FolderTree::GetSelectedFolders() const
{
	return m_FolderTreeImplPtr->GetSelectedFolders();	
}

void FolderTree::SetSelectedFolders(std::list<std::string> &uris)
{
	m_FolderTreeImplPtr->SetSelectedFolders(uris);
}



FolderTree::FolderTreeImpl::FolderTreeImpl(FolderTree *parent)
{
	m_pFolderTree = parent;
	
	m_pTreeIterScrollTo = NULL;
	
	CreateWidget();
}

FolderTree::FolderTreeImpl::~FolderTreeImpl()
{
	if (NULL != m_pHashRootNodeOrder)
	{
		g_hash_table_destroy(m_pHashRootNodeOrder);
	}
}

static gboolean folder_tree_add_checked_to_list (GtkTreeModel* model, GtkTreePath* path, GtkTreeIter *iter, gpointer user_data)
{
	std::list<std::string>* pListSelectedFolders = (std::list<std::string>*)user_data ;
	gboolean value;
	gtk_tree_model_get (model,iter,FILE_TREE_COLUMN_CHECKBOX,&value,-1);
	if (value)
	{
		gchar* uri;
		gtk_tree_model_get(model, iter, FILE_TREE_COLUMN_URI, &uri, -1);
		pListSelectedFolders->push_back(uri);
		g_free(uri);
	}
	return FALSE;
}

GtkTreeIter* add_uri_to_tree(GtkTreeView *treeview, GtkTreeIter* iter, const gchar* uri)
{
	GtkTreeIter* iter_added = NULL;
	
	GtkTreeModel* model = gtk_tree_view_get_model (treeview);
	if (NULL != uri)
	{
		gchar* base_uri;
		gtk_tree_model_get(model, iter, FILE_TREE_COLUMN_URI, &base_uri, -1);
		
		GnomeVFSURI* vfs_tmp;
		GnomeVFSURI* vfs_uri = gnome_vfs_uri_new(uri);
		GnomeVFSURI* vfs_base = gnome_vfs_uri_new(base_uri);
		
		bool bValid = false;
		
		std::list<std::string> strDirsToAdd;
		
		do
		{
			if (gnome_vfs_uri_equal (vfs_uri, vfs_base))
			{
				gnome_vfs_uri_unref(vfs_uri);
				bValid = true;
				break;
			}

			gchar* path_name_escaped = gnome_vfs_uri_extract_short_path_name (vfs_uri);
			gchar* path_name = gnome_vfs_unescape_string(path_name_escaped,NULL);
							
			strDirsToAdd.push_front(path_name);	
			
			g_free(path_name);
			g_free(path_name_escaped);
			
			vfs_tmp = gnome_vfs_uri_get_parent(vfs_uri);
			gnome_vfs_uri_unref(vfs_uri);
			vfs_uri = vfs_tmp;
		
		} while (NULL != vfs_uri);
		
		gnome_vfs_uri_unref(vfs_base);
		
		if (bValid)
		{
			std::list<std::string>::iterator itr;
			
			iter = gtk_tree_iter_copy(iter);
			
			for (itr = strDirsToAdd.begin(); strDirsToAdd.end() != itr; ++itr)
			{
				GtkTreeIter* iter_child = folder_tree_add_subdir(model, iter, itr->c_str(), TRUE);
				
				gtk_tree_iter_free(iter);
				iter = iter_child;
				
			}

			
			gtk_tree_store_set (GTK_TREE_STORE(model),iter,FILE_TREE_COLUMN_CHECKBOX, TRUE, -1);

			iter_added = gtk_tree_iter_copy(iter);
			
			gtk_tree_iter_free(iter);
		}
	}
	
	return iter_added;
}

static gboolean timeout_folder_tree_scroll_to_cell(gpointer data)
{
	FolderTree::FolderTreeImpl* pFolderTreeImpl = (FolderTree::FolderTreeImpl*)data;
	gboolean rval = FALSE;
	
	gdk_threads_enter();
	// wait untill all the idle functions have finished
	if (pFolderTreeImpl->m_setFolderThreads.size() || NULL == pFolderTreeImpl->m_pTreeIterScrollTo)
	{
		rval = TRUE;
	}
	else
	{
		// do processing
		GtkTreeView* treeview = GTK_TREE_VIEW(pFolderTreeImpl->m_pWidget);
		GtkTreeModel* model = gtk_tree_view_get_model (treeview);
		GtkTreePath* path =  gtk_tree_model_get_path(model, pFolderTreeImpl->m_pTreeIterScrollTo);

		gtk_tree_view_expand_to_path(treeview, path);
		gtk_tree_view_scroll_to_cell(treeview, path, NULL, TRUE, .125, 0.);

		GtkTreeSelection* selection;
		selection = gtk_tree_view_get_selection(treeview);
		
		gtk_tree_selection_unselect_all(selection);
		gtk_tree_selection_select_iter(selection, pFolderTreeImpl->m_pTreeIterScrollTo);
				
		gtk_tree_path_free(path);
		
		pFolderTreeImpl->m_iTimeoutScrollToCell = 0;

		gtk_tree_iter_free(pFolderTreeImpl->m_pTreeIterScrollTo);
		pFolderTreeImpl->m_pTreeIterScrollTo = NULL;
		
		rval =  FALSE;
	}
	gdk_threads_leave();
	return rval;
}

void  FolderTree::FolderTreeImpl::SetSelectedFolders(std::list<std::string> &uris)
{
	GtkTreeModel *model;
	
	GtkTreeIter* iter_parent = NULL;
	GtkTreeIter iter_child = {0};
	GtkTreeIter iter_match = {0};

	model = gtk_tree_view_get_model (GTK_TREE_VIEW(m_pWidget));
	
	folder_tree_clear_all_checkboxes(gtk_tree_view_get_model(GTK_TREE_VIEW(m_pWidget)));
	
	std::list<std::string>::iterator itr;
	for (itr = uris.begin(); uris.end() != itr; ++itr)
	{
		// here we go
		//printf("selecting folder: %s\n", itr->c_str());
		gint i = 0;
		std::string strLongestURI;
		bool found_match = false;
		
		gint longest_match = -1;
		
		while ( gtk_tree_model_iter_nth_child (model,&iter_child, iter_parent, i) )
		{
			i++;
			gchar* uri;
			gtk_tree_model_get(model,&iter_child, FILE_TREE_COLUMN_URI, &uri, -1);
			
			
			if (NULL != uri)
			{
				std::string strURI = uri;
				if (std::string::npos != itr->find(strURI))
				{
					GnomeVFSURI* vfs_uri, *vfs_parent;
					vfs_uri = gnome_vfs_uri_new (uri);
					
					gint n_parents = 0;
					while (NULL != (vfs_parent = gnome_vfs_uri_get_parent(vfs_uri)))
					{
						gnome_vfs_uri_unref(vfs_uri);
						vfs_uri = vfs_parent;
						n_parents++;	
					}
					
					if (longest_match < n_parents)
					{
						strLongestURI = strURI;
						longest_match = n_parents;
						iter_match = iter_child;
						found_match = true;
					}
					
					gnome_vfs_uri_unref(vfs_uri);
					
				}
				g_free(uri);
			}
			
		}

		if (found_match)
		{
			GtkTreeIter* iter = add_uri_to_tree(GTK_TREE_VIEW(m_pWidget), &iter_match, itr->c_str());

			// we need a timeout here because if the list is still changing we need to wait
			// before we scroll to the cell
			if (NULL != m_pTreeIterScrollTo)
			{
				gtk_tree_iter_free(m_pTreeIterScrollTo);
			}
			m_pTreeIterScrollTo = iter;
			
			if (0 != m_iTimeoutScrollToCell)
			{
				g_source_remove(m_iTimeoutScrollToCell);
			}
			
			GtkTreePath* path =  gtk_tree_model_get_path(model, iter);
	
			gtk_tree_view_expand_to_path(GTK_TREE_VIEW(m_pWidget), path);
			
			gtk_tree_path_free(path);	
		
			m_iTimeoutScrollToCell = g_timeout_add(50, timeout_folder_tree_scroll_to_cell, this);
		}

	}

}

std::list<std::string> FolderTree::FolderTreeImpl::GetSelectedFolders() const
{
	std::list<std::string> listSelectedFolders;

	GtkTreeModel* model = gtk_tree_view_get_model(GTK_TREE_VIEW(m_pWidget));
	gtk_tree_model_foreach(model,folder_tree_add_checked_to_list,&listSelectedFolders);

	return listSelectedFolders;
}

void FolderTree::FolderTreeImpl::CreateWidget()
{
	m_pHashRootNodeOrder = g_hash_table_new(g_str_hash,g_str_equal);


	//printf("quiver_folder_tree_new start\n");
	GtkTreeViewColumn *column;
	GtkCellRenderer *renderer;
	GtkTreeSelection *selection;
	GtkTreeStore *store;

	
	/* Create a model.  We are using the store model for now, though we
	* could use any other GtkTreeModel */
	store = gtk_tree_store_new (FILE_TREE_COLUMN_COUNT,
	 G_TYPE_BOOLEAN,
	 G_TYPE_STRING,
	 G_TYPE_STRING,
 	 G_TYPE_STRING,
	 G_TYPE_BOOLEAN
	 );
	
	/* custom function to fill the model with data */

	// it appears the sort column must be set after the sort function
	//gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(store), FILE_TREE_COLUMN_DISPLAY_NAME, sort_func,NULL, NULL);
	//gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(store), FILE_TREE_COLUMN_DISPLAY_NAME, GTK_SORT_ASCENDING);
	
	/* Create a view */
	m_pWidget = gtk_tree_view_new();

	PopulateTreeModel (store);

	gtk_tree_sortable_set_default_sort_func(GTK_TREE_SORTABLE(store), sort_func,this, NULL);
	gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(store), GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID, GTK_SORT_ASCENDING);
 	
	gtk_tree_view_set_model(GTK_TREE_VIEW(m_pWidget), GTK_TREE_MODEL (store));
	
	GtkTreeIter iter;
	if ( gtk_tree_model_get_iter_first (GTK_TREE_MODEL(store), &iter) )
	{
		GtkTreePath* first =  gtk_tree_model_get_path(GTK_TREE_MODEL(store), &iter);

		gtk_tree_view_set_cursor (GTK_TREE_VIEW(m_pWidget),
		                             first,
		                             NULL,
		                             FALSE);
		gtk_tree_path_free(first);
	}
			                            
#if GTK_MAJOR_VERSION == 2  &&  GTK_MINOR_VERSION >= 10 || GTK_MAJOR_VERSION > 2
	gtk_tree_view_set_rubber_banding(GTK_TREE_VIEW(m_pWidget),TRUE);
#endif
	//gtk_tree_view_set_enable_tree_lines(GTK_TREE_VIEW(m_pWidget),TRUE);
	//gtk_tree_view_set_grid_lines(GTK_TREE_VIEW(m_pWidget),GTK_TREE_VIEW_GRID_LINES_BOTH);
	gtk_widget_show(m_pWidget);
	
	
	GdkColor highlight_color;
	gdk_color_parse("#ccc",&highlight_color);
	/* = m_pWidget->style->bg[GTK_STATE_SELECTED];*/

	/*
	highlight_color.red =  m_pWidget->style->bg[GTK_STATE_SELECTED].red/2 + m_pWidget->style->bg[GTK_STATE_NORMAL].red/2;
	highlight_color.green =  m_pWidget->style->bg[GTK_STATE_SELECTED].green/2 + m_pWidget->style->bg[GTK_STATE_NORMAL].green/2;
	highlight_color.blue =  m_pWidget->style->bg[GTK_STATE_SELECTED].blue/2 + m_pWidget->style->bg[GTK_STATE_NORMAL].blue/2;
	*/
	
	/* The view now holds a reference.  We can get rid of our own
	* reference */
	g_object_unref (G_OBJECT (store));
	
	renderer = gtk_cell_renderer_toggle_new ();
	g_object_set (G_OBJECT (renderer),  "mode", GTK_CELL_RENDERER_MODE_INERT,  NULL);
	g_object_set (G_OBJECT (renderer),  "cell-background-gdk", &highlight_color,  NULL);    
	column = gtk_tree_view_column_new_with_attributes (QUIVER_TREE_COLUMN_TOGGLE,
	  renderer,
	  "active", FILE_TREE_COLUMN_CHECKBOX,
	  "cell-background-set",FILE_TREE_COLUMN_CHECKBOX,
	NULL);
	gtk_tree_view_append_column (GTK_TREE_VIEW (m_pWidget), column);

	/* Second column.. title of the book. */
	column = gtk_tree_view_column_new();
	gtk_tree_view_column_set_title(column,"Folder");

	renderer = gtk_cell_renderer_pixbuf_new ();
	g_object_set (G_OBJECT (renderer),  "mode", GTK_CELL_RENDERER_MODE_INERT,  NULL);
	g_object_set (G_OBJECT (renderer),  "cell-background-gdk", &highlight_color,  NULL);    

	gtk_tree_view_column_pack_start (column,renderer,FALSE);
	gtk_tree_view_column_set_attributes(column,
	  renderer,
	  "icon_name", FILE_TREE_COLUMN_ICON,
  	  "cell-background-set",FILE_TREE_COLUMN_CHECKBOX,
	NULL);
	g_object_set (G_OBJECT (renderer),  "stock-size", 3,  NULL);

	renderer = gtk_cell_renderer_text_new ();
	g_object_set (G_OBJECT (renderer),  "mode", GTK_CELL_RENDERER_MODE_INERT,  NULL);
	g_object_set (G_OBJECT (renderer),  "cell-background-gdk", &highlight_color,  NULL);    	
	gtk_tree_view_column_pack_end (column,renderer,TRUE);

	gtk_tree_view_column_add_attribute(column,renderer,"text",FILE_TREE_COLUMN_DISPLAY_NAME);
	gtk_tree_view_column_add_attribute(column,renderer,"cell-background-set",FILE_TREE_COLUMN_CHECKBOX);

	gtk_tree_view_append_column (GTK_TREE_VIEW (m_pWidget), column);
	gtk_tree_view_set_expander_column(GTK_TREE_VIEW (m_pWidget),column);
	
	gtk_tree_view_set_row_separator_func(GTK_TREE_VIEW (m_pWidget),folder_tree_is_separator,NULL,NULL);
	/*GtkDestroyNotify destroy);*/
	
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW (m_pWidget),FALSE);
	
	gtk_tree_view_set_search_column(GTK_TREE_VIEW (m_pWidget),FILE_TREE_COLUMN_DISPLAY_NAME);
	
	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW (m_pWidget));
	
	gtk_tree_selection_set_mode(selection,GTK_SELECTION_MULTIPLE);
	
	g_signal_connect(G_OBJECT(selection),"changed",G_CALLBACK(folder_tree_selection_changed),this);
	g_signal_connect(m_pWidget, "cursor-changed",G_CALLBACK(folder_tree_cursor_changed),this);
	g_signal_connect(m_pWidget, "button-press-event", (GCallback) view_onButtonPressed, this);
	g_signal_connect(m_pWidget, "key-press-event", (GCallback) view_on_key_press, this);
	g_signal_connect(m_pWidget, "popup-menu", (GCallback) view_onPopupMenu, this);
	g_signal_connect(m_pWidget, "row-activated", (GCallback) view_onRowActivated, this);
	g_signal_connect(m_pWidget, "row-expanded", (GCallback) signal_folder_tree_row_expanded,this);
	
	//printf("quiver_folder_tree_new end\n");
}


static char* folder_tree_get_icon_name(const char* uri);

static gboolean from_mouse = FALSE;


static gboolean folder_tree_clear_checkbox(GtkTreeModel* model, GtkTreePath* path, GtkTreeIter *iter, gpointer user_data)
{
	gtk_tree_store_set (GTK_TREE_STORE(model),iter,FILE_TREE_COLUMN_CHECKBOX,FALSE,-1);
	return FALSE;
}

static void folder_tree_clear_all_checkboxes(GtkTreeModel *model)
{
	gtk_tree_model_foreach(model,folder_tree_clear_checkbox,NULL);
}

static void folder_tree_cursor_changed (GtkTreeView *treeview, gpointer user_data)
{
	FolderTree::FolderTreeImpl* pFolderTreeImpl = (FolderTree::FolderTreeImpl*)user_data;

	GtkTreePath* path = NULL;
	GtkTreeModel* model = gtk_tree_view_get_model(treeview);
	
	if (from_mouse)
	{
		gtk_tree_view_get_cursor(treeview,&path,NULL);
		if (NULL != path)
		{
			GtkTreeIter iter;
			gtk_tree_model_get_iter(model,&iter,path);
			folder_tree_clear_all_checkboxes(model);
			gtk_tree_store_set (GTK_TREE_STORE(model),&iter,FILE_TREE_COLUMN_CHECKBOX,TRUE,-1);

			gtk_tree_path_free(path);
			pFolderTreeImpl->m_pFolderTree->EmitSelectionChangedEvent();
		}
	}
}

static void folder_tree_set_selected_checkbox_value(GtkTreeView* treeview,gboolean value)
{
	GList* paths;
	GList* path_itr;
	GtkTreePath *path;
	GtkTreeModel *model;
	GtkTreeSelection* selection;
	GtkTreeIter iter;
	
	model = gtk_tree_view_get_model(treeview);
	selection = gtk_tree_view_get_selection(treeview);


	paths = gtk_tree_selection_get_selected_rows(selection,&model);
	path_itr = paths;
	while (NULL != path_itr)
	{
		path = (GtkTreePath*)path_itr->data;
		gtk_tree_model_get_iter(model,&iter,path);

		gtk_tree_store_set (GTK_TREE_STORE(model),&iter,FILE_TREE_COLUMN_CHECKBOX,value,-1);
		
		path_itr = g_list_next(path_itr);
	}	
	g_list_foreach (paths, (GFunc)gtk_tree_path_free, NULL);
	g_list_free (paths);
	
	//FIXME: emit event
}

static void
signal_check_selected (GtkWidget *menuitem, gpointer userdata)
{
	GtkTreeView *treeview = GTK_TREE_VIEW(userdata);
	folder_tree_set_selected_checkbox_value(treeview,TRUE);
	//g_print ("Do something!\n");
}

static void
signal_uncheck_selected (GtkWidget *menuitem, gpointer userdata)
{
	GtkTreeView *treeview = GTK_TREE_VIEW(userdata);
	folder_tree_set_selected_checkbox_value(treeview,FALSE);
	//g_print ("Do something!\n");
}

void view_popup_menu (GtkWidget *treeview, GdkEventButton *event, gpointer userdata)
{
	GtkWidget *menu, *menuitem;
	
	menu = gtk_menu_new();
	
	menuitem = gtk_menu_item_new_with_label("Check Selected Item(s)");
	g_signal_connect(menuitem, "activate",
	                 (GCallback) signal_check_selected, treeview);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);

	menuitem = gtk_menu_item_new_with_label("Uncheck Selected Item(s)");
	g_signal_connect(menuitem, "activate",
	                 (GCallback) signal_uncheck_selected, treeview);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);

	menuitem = gtk_menu_item_new();

	gtk_widget_show_all(menu);
	
	gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL,
	                  (event != NULL) ? event->button : 0,
	                  gdk_event_get_time((GdkEvent*)event));

}


static void
view_onRowActivated (GtkTreeView        *treeview,
                       GtkTreePath        *path,
                       GtkTreeViewColumn  *col,
                       gpointer            userdata)
{
	if ( gtk_tree_view_row_expanded(GTK_TREE_VIEW(treeview),path) )
	{
		gtk_tree_view_collapse_row(GTK_TREE_VIEW(treeview),path);
	}
	else
	{
		gtk_tree_view_expand_row(GTK_TREE_VIEW(treeview),path,FALSE);
	}
}

static gboolean view_on_key_press(GtkWidget *treeview, GdkEventKey *event, gpointer userdata)
{
	FolderTree::FolderTreeImpl* pFolderTreeImpl = (FolderTree::FolderTreeImpl*)userdata;

	GtkTreePath *path;
	gtk_tree_view_get_cursor        (GTK_TREE_VIEW(treeview),
                                             &path,
                                             NULL);
	from_mouse = FALSE;

	GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(treeview));
	gboolean rval = FALSE;
	if (GDK_Left  == event->keyval)
	{
		if ( gtk_tree_view_row_expanded (GTK_TREE_VIEW(treeview),path) )
		{
			gtk_tree_view_collapse_row(GTK_TREE_VIEW(treeview),path);
		}
		else
		{
			GtkTreeIter parent;
			GtkTreeIter child;
			gtk_tree_model_get_iter(model,&child,path);
			
			gboolean has_parent;
			has_parent = gtk_tree_model_iter_parent      (model,
                                             &parent,
                                             &child);
			if (has_parent)
			{
				GtkTreePath* parent_path =  gtk_tree_model_get_path(model,
                                            &parent);
				GtkTreeSelection* selection;
				selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));
				gtk_tree_selection_unselect_all(selection);
				gtk_tree_selection_select_path  (selection,
	                                             parent_path);

				gtk_tree_view_set_cursor (GTK_TREE_VIEW(treeview),
                                             parent_path,
                                             NULL,
                                             FALSE);

				gtk_tree_path_free(parent_path);
			}	                                            
			rval = TRUE;			
		}
	}
	else if (GDK_Right == event->keyval)
	{
		if ( gtk_tree_view_row_expanded (GTK_TREE_VIEW(treeview),path) )
		{
			GtkTreeIter parent;
			GtkTreeIter child;
			gtk_tree_model_get_iter(GTK_TREE_MODEL(model),&parent,path);
			
			gboolean has_child;
							
			has_child = gtk_tree_model_iter_nth_child (model,
                                             &child,
                                             &parent,0);
			if (has_child)
			{
				gchar* uri;
				gtk_tree_model_get(model, &child, FILE_TREE_COLUMN_URI, &uri, -1);
				if (NULL != uri)
				{
					g_free(uri);

					GtkTreePath* child_path =  gtk_tree_model_get_path(model,
												&child);
					GtkTreeSelection* selection;
					selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));
					gtk_tree_selection_unselect_all(selection);
					gtk_tree_selection_select_path  (selection,
													 child_path);

					gtk_tree_view_set_cursor (GTK_TREE_VIEW(treeview),
												 child_path,
												 NULL,
												 FALSE);

					gtk_tree_path_free(child_path);
				}
			}

		}
		else
		{
			gboolean expandable;
			expandable  = gtk_tree_view_expand_row(GTK_TREE_VIEW(treeview),path,FALSE);
			if (!expandable)
			{
				GtkTreeIter parent;
	
				GtkTreeIter sibling;
				GtkTreePath* sibling_path;
				gboolean has_next;
	
				gtk_tree_model_get_iter(GTK_TREE_MODEL(model),&parent,path);
	
				gchar* uri;
				gtk_tree_model_get(model, &parent, FILE_TREE_COLUMN_URI, &uri, -1);

				sibling = parent;
				has_next = gtk_tree_model_iter_next (model,&sibling);
				if (has_next)
				{
					sibling_path =  gtk_tree_model_get_path(model,
	                                            &sibling);
					GtkTreeSelection* selection;
					selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));
					gtk_tree_selection_unselect_all(selection);
					gtk_tree_selection_select_path  (selection,
		                                             sibling_path);
	
					gtk_tree_view_set_cursor (GTK_TREE_VIEW(treeview),
	                                             sibling_path,
	                                             NULL,
	                                             FALSE);
					
					gtk_tree_path_free(sibling_path);
				}
	
			}
		}
		rval = TRUE;
		
	}
	else if (GDK_Return == event->keyval)
	{
		GtkTreePath* path = NULL;
		gtk_tree_view_get_cursor (GTK_TREE_VIEW(pFolderTreeImpl->m_pWidget), &path, NULL);
		
		GtkTreeModel* model = gtk_tree_view_get_model(GTK_TREE_VIEW(pFolderTreeImpl->m_pWidget));

		if (NULL != path)
		{
			GtkTreeIter iter;
			gtk_tree_model_get_iter(model,&iter,path);
			folder_tree_clear_all_checkboxes(model);
			gtk_tree_store_set (GTK_TREE_STORE(model),&iter,FILE_TREE_COLUMN_CHECKBOX,TRUE,-1);

			gtk_tree_path_free(path);
			pFolderTreeImpl->m_pFolderTree->EmitSelectionChangedEvent();
			rval = TRUE;
		}
	}
	
	if (GDK_space == event->keyval &&  !( event->state & (GDK_CONTROL_MASK/*|GDK_SHIFT_MASK*/) ))
	{
		GtkTreeIter iter;
		gtk_tree_model_get_iter(model,&iter,path);
		gboolean value;
		gtk_tree_model_get (model,&iter,FILE_TREE_COLUMN_CHECKBOX,&value,-1);
		folder_tree_set_selected_checkbox_value(GTK_TREE_VIEW(treeview),!value);
		rval = TRUE;

		pFolderTreeImpl->m_pFolderTree->EmitSelectionChangedEvent();
	}
		
	return rval;
}

static gboolean
view_onButtonPressed (GtkWidget *treeview, GdkEventButton *event, gpointer userdata)
{
	FolderTree::FolderTreeImpl* pFolderTreeImpl = (FolderTree::FolderTreeImpl*)userdata;

	GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(treeview));

	from_mouse = TRUE;
	
	if (event->button == 2)
	{
		GtkTreePath *path;
		GtkTreeViewColumn *column;

		if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(treeview),
	                         (gint) event->x, 
	                         (gint) event->y,
	                         &path, &column, NULL, NULL))
		{
			if (event->type != GDK_2BUTTON_PRESS && event->type != GDK_3BUTTON_PRESS)
			{
				GtkTreeIter iter;
				gtk_tree_model_get_iter(model,&iter,path);
				gboolean value;
				gtk_tree_model_get (model,&iter,FILE_TREE_COLUMN_CHECKBOX,&value,-1);
				gtk_tree_store_set (GTK_TREE_STORE(model),&iter,FILE_TREE_COLUMN_CHECKBOX,!value,-1);
				pFolderTreeImpl->m_pFolderTree->EmitSelectionChangedEvent();
				return TRUE;
			}
		}
	}
	else if (event->button == 1)
	{
		GtkTreePath *path;
		GtkTreeViewColumn *column;

		if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(treeview),
	                         (gint) event->x, 
	                         (gint) event->y,
	                         &path, &column, NULL, NULL))
		{
			const gchar *column_name;
			column_name = gtk_tree_view_column_get_title(column);

			if (0 == strcmp(QUIVER_TREE_COLUMN_TOGGLE,column_name))
			{
				if (event->type != GDK_2BUTTON_PRESS && event->type != GDK_3BUTTON_PRESS)
				{
					GtkTreeIter iter;
					gtk_tree_model_get_iter(model,&iter,path);
					gboolean value;
					gtk_tree_model_get (model,&iter,FILE_TREE_COLUMN_CHECKBOX,&value,-1);
					gtk_tree_store_set (GTK_TREE_STORE(model),&iter,FILE_TREE_COLUMN_CHECKBOX,!value,-1);
					pFolderTreeImpl->m_pFolderTree->EmitSelectionChangedEvent();
					return TRUE;
				}
			}
		}
	}

	// right click
	if (event->type == GDK_BUTTON_PRESS  &&  event->button == 3)
	{
		GtkTreeSelection *selection;
		selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));
		
		if (gtk_tree_selection_count_selected_rows(selection)  <= 1)
		{
			GtkTreePath *path;

			GtkTreeViewColumn *column;
			if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(treeview),
									 (gint) event->x, 
									 (gint) event->y,
									 &path, &column, NULL, NULL))
			{
				gtk_tree_selection_unselect_all(selection);
				gtk_tree_selection_select_path(selection, path);
				gtk_tree_path_free(path);
			}
		}
		
		view_popup_menu(treeview, event, userdata);
		return TRUE; 
	}

	return FALSE; 
}

static gboolean view_onPopupMenu (GtkWidget *treeview, gpointer userdata)
{
	view_popup_menu(treeview, NULL, userdata);
	return TRUE; 
}

static gboolean folder_tree_is_separator (GtkTreeModel *model,GtkTreeIter *iter,gpointer data)
{
	gboolean value;
	gtk_tree_model_get (model,iter,FILE_TREE_COLUMN_SEPARATOR,&value,-1);
	return value;
}

static void folder_tree_selection_changed (GtkTreeSelection *treeselection, gpointer user_data)
{
	//printf("changed\n");
}


typedef enum _MyIdleState
{
	TRAVERSE_TREE,
	OPEN_DIR,
	TRAVERSE_DIR,
	SYNC_TREE,
} MyIdleState;

typedef struct _MyDataStruct
{
	FolderTree::FolderTreeImpl* pFolderTreeImpl;
	GtkTreeModel* model;
	GtkTreeIter* iter_parent;
	GtkTreeIter* iter_child;
	GnomeVFSDirectoryHandle* dir_handle;
	GHashTable* hash_table;
	MyIdleState state;
	gboolean has_subdirs;
	int i;
} MyDataStruct;


static GtkTreeIter* folder_tree_add_subdir(GtkTreeModel* model, GtkTreeIter *iter_parent, const gchar* name, gboolean duplicate_check)
{
	g_return_val_if_fail(NULL != name,NULL);
	g_return_val_if_fail(NULL != iter_parent,NULL);
	g_return_val_if_fail(NULL != model,NULL);
	
	GtkTreeIter iter_child = {0};

	gboolean found_duplicate = FALSE;	
	
	if (duplicate_check)
	{
		gint n_nodes = gtk_tree_model_iter_n_children  (model, iter_parent);
		gint i;

		// check to see if the folder is already in the tree
		for (i = 0 ; i < n_nodes && !found_duplicate; i++)
		{
			if ( gtk_tree_model_iter_nth_child(model, &iter_child, iter_parent, i) )
			{
				gchar* folder_name = NULL;
				gtk_tree_model_get(model, &iter_child, FILE_TREE_COLUMN_DISPLAY_NAME, &folder_name, -1);

				if (NULL != name && 0 == strcmp(name,folder_name))
				{
					found_duplicate = TRUE;
				}
				
				g_free(folder_name);
			}
		}
	}
	
	if (!found_duplicate)
	{ 
	
		gtk_tree_store_append (GTK_TREE_STORE(model), &iter_child, iter_parent);  
	
		gchar* uri;
		gtk_tree_model_get(model,iter_parent, FILE_TREE_COLUMN_URI, &uri, -1);
		
		GnomeVFSURI * vfs_uri_parent = gnome_vfs_uri_new(uri);
		GnomeVFSURI * vfs_uri_child = gnome_vfs_uri_append_path(vfs_uri_parent, name);
		
		gchar* uri_child = gnome_vfs_uri_to_string (vfs_uri_child, GNOME_VFS_URI_HIDE_NONE);
		
		char* icon_name = folder_tree_get_icon_name(uri_child);
	
		gtk_tree_store_set (GTK_TREE_STORE(model), &iter_child,
				FILE_TREE_COLUMN_CHECKBOX, FALSE,
				FILE_TREE_COLUMN_ICON, icon_name,
				FILE_TREE_COLUMN_DISPLAY_NAME, name,
				FILE_TREE_COLUMN_SEPARATOR,FALSE,
				FILE_TREE_COLUMN_URI,uri_child,
				-1);
		free(icon_name);
		
		g_free(uri_child);
		
		gnome_vfs_uri_unref(vfs_uri_child);
		gnome_vfs_uri_unref(vfs_uri_parent);
		
		g_free(uri);
		
		
	}
	
	return gtk_tree_iter_copy(&iter_child);

}



static void hash_foreach_sync_add(gpointer key, gpointer value, gpointer user_data)
{
	gchar* folder_name = (gchar*)key;

	if (NULL != folder_name && '.' != folder_name[0])
	{

		MyDataStruct* data = (MyDataStruct*)user_data;
		GtkTreeModel *model = data->model;

		folder_tree_add_subdir(model, data->iter_child, folder_name, FALSE);
	}
}

static void* thread_check_for_subdirs(void* user_data)
{
	MyDataStruct* data = (MyDataStruct*)user_data;
	GtkTreeModel *model = data->model;
	GtkTreeIter* iter_parent = NULL;

	gboolean finished = FALSE;
	
	while (!finished)
	{
		iter_parent = data->iter_parent;
		char* uri = NULL;
		GnomeVFSResult result;
	
		switch (data->state)
		{
			case SYNC_TREE:
				{
					gdk_threads_enter();
					if ( 0 == g_hash_table_size(data->hash_table) )
					{
						// dir has no children so remove any existing ones
						GtkTreeIter iter =  {0};
						gint n_nodes = gtk_tree_model_iter_n_children  (model, data->iter_child);
						gint i;
						for (i = 0 ; i < n_nodes ; i++)
						{
							if ( gtk_tree_model_iter_nth_child(model, &iter, NULL, i) )
							{
								gtk_tree_store_remove(GTK_TREE_STORE(model),&iter);
							}
						}
					}
					else
					{
						// sync the directories
						GtkTreeIter iter =  {0};
						gint n_nodes = gtk_tree_model_iter_n_children  (model, data->iter_child);
						gint i;
						// remove items no longer in the tree
						for (i = 0 ; i < n_nodes ; i++)
						{
							if ( gtk_tree_model_iter_nth_child(model, &iter, data->iter_child, i) )
							{
								gchar* uri = NULL;
								gtk_tree_model_get(model, &iter, FILE_TREE_COLUMN_URI, &uri, -1);
	
								gchar* local_filename = g_filename_from_uri(uri,NULL, NULL);
								gchar* dir_name = g_path_get_basename(local_filename);
	
	
								gpointer* orig_key = NULL;
								gpointer* value    = NULL;
	
								if (g_hash_table_lookup_extended(data->hash_table, dir_name, orig_key, value))
								{
									// if the key exists, remove it from the hash
									g_hash_table_remove(data->hash_table, dir_name);
								}
								else
								{
									// if the key does not exist, remove the iterator
									gtk_tree_store_remove(GTK_TREE_STORE(model),&iter);
									// one less item now so i much be adjusted
									i--;
								}
								g_free(dir_name);
								g_free(local_filename);
								g_free(uri);
							}
						}
						// add new items
						g_hash_table_foreach(data->hash_table, hash_foreach_sync_add, data);
	
					}
	
					gtk_tree_iter_free(data->iter_child);
					gdk_threads_leave();
					data->iter_child = NULL;
					data->state = TRAVERSE_TREE;
	
					if (NULL != data->hash_table)
					{
						g_hash_table_destroy( data->hash_table );
						data->hash_table = NULL;
					}
						
				}
				break;
			case TRAVERSE_DIR:
				{
					gdk_threads_enter();
					
					gtk_tree_model_get(model,data->iter_child, FILE_TREE_COLUMN_URI, &uri, -1);
					GnomeVFSURI * vfs_uri_dir = gnome_vfs_uri_new(uri);
					GnomeVFSFileInfo *info = gnome_vfs_file_info_new ();;
					gdk_threads_leave();
					
					result = gnome_vfs_directory_read_next(data->dir_handle,info);
						
					if (GNOME_VFS_OK == result )
					{
						GnomeVFSURI * vfs_uri_file = gnome_vfs_uri_append_path(vfs_uri_dir,info->name);
						gchar *str_uri_file = gnome_vfs_uri_to_string (vfs_uri_file,GNOME_VFS_URI_HIDE_NONE);
						
						if ( ( 0 == strstr(uri,str_uri_file)) &&
							(GNOME_VFS_FILE_INFO_FIELDS_TYPE  & info->valid_fields) &&
							 (info->type == GNOME_VFS_FILE_TYPE_DIRECTORY) )
						{
							/*
							if ('.' == info->name[0])
							{
								// FIXME: show hidden?
							}
							else
							*/
							{
								g_hash_table_insert(data->hash_table,g_strdup(info->name),NULL);
							}
						}
						g_free (str_uri_file);
						gnome_vfs_uri_unref(vfs_uri_file);
					}
					else
					{
						gnome_vfs_directory_close (data->dir_handle);
						data->state = SYNC_TREE;
	
					}
	
					gnome_vfs_file_info_unref (info);
					gnome_vfs_uri_unref(vfs_uri_dir);
	
					if (NULL != uri)
					{
						g_free(uri);
					}
				}
				break;
			case OPEN_DIR:
				{
					gdk_threads_enter();
					gtk_tree_model_get(model,data->iter_child, FILE_TREE_COLUMN_URI, &uri, -1);
					gdk_threads_leave();
					
					GnomeVFSResult result;
					GnomeVFSDirectoryHandle *dir_handle;
					result = gnome_vfs_directory_open (&dir_handle,uri,
							(GnomeVFSFileInfoOptions)(GNOME_VFS_FILE_INFO_DEFAULT|
								GNOME_VFS_FILE_INFO_FORCE_FAST_MIME_TYPE|
								GNOME_VFS_FILE_INFO_GET_MIME_TYPE|
								GNOME_VFS_FILE_INFO_FOLLOW_LINKS));
					if ( GNOME_VFS_OK == result )
					{
						data->dir_handle  = dir_handle;
						data->state       = TRAVERSE_DIR;
						data->has_subdirs = FALSE;
						data->hash_table  = g_hash_table_new(g_str_hash, g_str_equal);
					}
					else
					{
						data->state = TRAVERSE_TREE;
	
						if (GNOME_VFS_ERROR_NOT_FOUND == result)
						{
							gdk_threads_enter();
							gtk_tree_store_remove (GTK_TREE_STORE(model),data->iter_child);
							gdk_threads_leave();		
							data->i--;
						}
						gdk_threads_enter();
						gtk_tree_iter_free(data->iter_child);
						data->iter_child = NULL;
						gdk_threads_leave();
	
					}
					if (NULL != uri)
					{
						g_free(uri);
					}
				}
				break;
			case TRAVERSE_TREE:
				{
					GtkTreeIter iter_child =  {0};
					gdk_threads_enter();
					if ( gtk_tree_model_iter_nth_child (model,&iter_child, iter_parent, data->i) )
					{
						data->iter_child = gtk_tree_iter_copy(&iter_child);
						data->state = OPEN_DIR;
					}
					else
					{
						finished = TRUE;
					}
					gdk_threads_leave();
					data->i++;
				}
				break;
			default:
				break;
		}
		pthread_yield();
	}

	if (NULL != data->pFolderTreeImpl)
	{
		pthread_t thread_id = pthread_self();
		data->pFolderTreeImpl->m_setFolderThreads.erase(thread_id);
	}
	
	if (NULL != data->iter_parent)
	{
		gdk_threads_enter();
		gtk_tree_iter_free(data->iter_parent);
		data->iter_parent = NULL;
		gdk_threads_leave();
	}

	if (NULL != data->iter_child)
	{
		gdk_threads_enter();
		gtk_tree_iter_free(data->iter_child);
		data->iter_child = NULL;
		gdk_threads_leave();
	}

	if (NULL != data->hash_table)
	{
		g_hash_table_destroy( data->hash_table );
	}
	g_free(data);
	
	return NULL;

}

static void signal_folder_tree_row_expanded(GtkTreeView *treeview,
						GtkTreeIter *iter_parent,
						GtkTreePath *treepath,
						gpointer data)
{
	FolderTree::FolderTreeImpl* pFolderTreeImpl = (FolderTree::FolderTreeImpl*)data;
	
	GtkTreeModel *model = gtk_tree_view_get_model(treeview);
	MyDataStruct* mydata = (MyDataStruct*)g_malloc0(sizeof(MyDataStruct));
	mydata->model = model;
	mydata->pFolderTreeImpl = pFolderTreeImpl;

	gchar* uri;
	gtk_tree_model_get(model, iter_parent, FILE_TREE_COLUMN_URI, &uri, -1);
	g_free(uri);

	mydata->iter_parent = gtk_tree_iter_copy(iter_parent);
	pthread_t thread_id;
	pthread_create(&thread_id, NULL, thread_check_for_subdirs, mydata);
	pFolderTreeImpl->m_setFolderThreads.insert(thread_id);
}


static char* folder_tree_get_icon_name(const char* uri)
{
	GtkIconTheme* icon_theme = gtk_icon_theme_get_default();

	char* preferred_icon_name = NULL;
	const char* homedir = g_get_home_dir();
	char* desktop_path = g_build_filename (homedir, "Desktop", NULL);

	char* home_uri = gnome_vfs_get_uri_from_local_path (homedir);
	char* desktop_uri = gnome_vfs_get_uri_from_local_path (desktop_path);

#ifndef QUIVER_MAEMO
	if (gnome_vfs_uris_match (home_uri,uri))
	{
		preferred_icon_name = "gnome-fs-home";
	}
	else if (gnome_vfs_uris_match (desktop_uri,uri))
	{
		preferred_icon_name = "gnome-fs-desktop";
	}
	/*
	else if (gnome_vfs_uris_match ("trash://",uri))
	{
		preferred_icon_name = GNOME_STOCK_TRASH;
	}
	*/
#else
	preferred_icon_name = ICON_MAEMO_FOLDER_CLOSED;
#endif

	free(desktop_path);
	free(desktop_uri);
	free(home_uri);

	char* icon_name;
#ifndef QUIVER_MAEMO
	GnomeIconLookupResultFlags lookup_result;
	icon_name = gnome_icon_lookup_sync (icon_theme,
										 NULL,
										 uri,
										 preferred_icon_name,
										 GNOME_ICON_LOOKUP_FLAGS_NONE,
										 &lookup_result);
#else
	icon_name = g_strdup(preferred_icon_name);
#endif
	return icon_name;

}

void FolderTree::FolderTreeImpl::PopulateTreeModel(GtkTreeStore *store)
{
	int iNodeOrder = 0;

	//printf("populate_tree_model start\n");
	GtkTreeIter iter1 = {0};  /* Parent iter */
	//GtkTreeIter iter2 = {0};  /* Child iter  */
	const char* homedir = g_get_home_dir();

	char* desktop_path = g_build_filename (homedir, "Desktop", NULL);
	char* pictures_path = g_build_filename (homedir, "Pictures", NULL);
	char* documents_path = g_build_filename (homedir, "Documents", NULL);

	char* home_uri = gnome_vfs_get_uri_from_local_path (homedir);

	char* desktop_uri = gnome_vfs_get_uri_from_local_path (desktop_path);
	char* pictures_uri = gnome_vfs_get_uri_from_local_path (pictures_path);
	char* documents_uri = gnome_vfs_get_uri_from_local_path (documents_path);
	char* root_uri = gnome_vfs_get_uri_from_local_path ("/");


	free(desktop_path);
	free(pictures_path);
	free(documents_path);

	/*
	 * Desktop
	 * Home
	 * Documents
	 * Pictures
	 * Filesystem
	 * storage
	 * usb...
	 * trash
	 */

	char* icon_name = NULL;


	GnomeVFSURI *vfsURI;
	GnomeVFSResult result;
	result = gnome_vfs_find_directory     (NULL,/*GnomeVFSURI *near_uri,*/
                                             GNOME_VFS_DIRECTORY_KIND_DESKTOP,
                                             &vfsURI,
                                             FALSE,
                                             FALSE,
                                             0);
#ifdef QUIVER_MAEMO

/*
	const gchar *env;
	env = g_getenv("MYDOCSDIR");
	if (env && env[0])
		return g_strdup(env);

	env = g_getenv("HOME");

	return g_build_path(G_DIR_SEPARATOR_S, 
		(env && env[0]) ? env : g_get_home_dir(), 
		"MyDocs", NULL);

".images"
".videos"
".sounds"
".documents"
".games"

*/

	gchar* docs_dir = NULL;
	const gchar *env;
	env = g_getenv("MYDOCSDIR");

	if (env && env[0])
	{
		docs_dir = g_strdup(env);
	}
	else
	{
		env = g_getenv("HOME");
		docs_dir = g_build_filename((env && env[0]) ? env : g_get_home_dir(), "MyDocs", NULL);
	}

	gchar* uri;
	
	printf("docs path: %s\n",docs_dir);
	uri = g_filename_to_uri(docs_dir, NULL, NULL);
	printf("docs path: %s\n",docs_dir);
	gtk_tree_store_append (store, &iter1, NULL);  
	gtk_tree_store_set (store, &iter1,
		FILE_TREE_COLUMN_CHECKBOX, FALSE,
		FILE_TREE_COLUMN_ICON, ICON_MAEMO_DEVICE,
			FILE_TREE_COLUMN_DISPLAY_NAME, "N800",
			FILE_TREE_COLUMN_SEPARATOR,FALSE,
			FILE_TREE_COLUMN_URI,uri,
			-1);
	printf("added uri: %s\n",uri);
	g_free(uri);
			
	g_hash_table_insert(m_pHashRootNodeOrder,g_strdup("N800"),(gpointer)iNodeOrder++);


	GtkTreeIter iter2 = {0};  /* Parent iter */
	gchar* subpath;
	
	// Audio clips
	subpath = g_build_filename(docs_dir,".sounds", NULL);
	uri = g_filename_to_uri(subpath, NULL, NULL);
	gtk_tree_store_append (store, &iter2, &iter1);  
	gtk_tree_store_set (store, &iter2,
		FILE_TREE_COLUMN_CHECKBOX, FALSE,
		FILE_TREE_COLUMN_ICON, ICON_MAEMO_AUDIO,
			FILE_TREE_COLUMN_DISPLAY_NAME, "Audio clips",
			FILE_TREE_COLUMN_SEPARATOR,FALSE,
			FILE_TREE_COLUMN_URI,uri,
			-1);
	g_free(uri);
	g_free(subpath);

	g_hash_table_insert(m_pHashRootNodeOrder,g_strdup("Audio clips"),(gpointer)iNodeOrder++);


	// Documents

	subpath = g_build_filename(docs_dir,".documents", NULL);
	uri = g_filename_to_uri(subpath, NULL, NULL);
	gtk_tree_store_append (store, &iter2, &iter1);  
	gtk_tree_store_set (store, &iter2,
		FILE_TREE_COLUMN_CHECKBOX, FALSE,
		FILE_TREE_COLUMN_ICON, ICON_MAEMO_DOCUMENTS,
			FILE_TREE_COLUMN_DISPLAY_NAME, "Documents",
			FILE_TREE_COLUMN_SEPARATOR,FALSE,
			FILE_TREE_COLUMN_URI,uri,
			-1);
			
	g_free(uri);
	g_free(subpath);
	g_hash_table_insert(m_pHashRootNodeOrder,g_strdup("Documents"),(gpointer)iNodeOrder++);

	// Games
	subpath = g_build_filename(docs_dir,".games", NULL);
	uri = g_filename_to_uri(subpath, NULL, NULL);
	gtk_tree_store_append (store, &iter2, &iter1);  
	gtk_tree_store_set (store, &iter2,
		FILE_TREE_COLUMN_CHECKBOX, FALSE,
		FILE_TREE_COLUMN_ICON, ICON_MAEMO_GAMES,
			FILE_TREE_COLUMN_DISPLAY_NAME, "Games",
			FILE_TREE_COLUMN_SEPARATOR,FALSE,
			FILE_TREE_COLUMN_URI,uri,
			-1);
	g_free(uri);
	g_free(subpath);
			
	g_hash_table_insert(m_pHashRootNodeOrder,g_strdup("Games"),(gpointer)iNodeOrder++);

	// Images
	subpath = g_build_filename(docs_dir,".images", NULL);
	uri = g_filename_to_uri(subpath, NULL, NULL);
	gtk_tree_store_append (store, &iter2, &iter1);  
	gtk_tree_store_set (store, &iter2,
		FILE_TREE_COLUMN_CHECKBOX, FALSE,
		FILE_TREE_COLUMN_ICON, ICON_MAEMO_IMAGES,
			FILE_TREE_COLUMN_DISPLAY_NAME, "Images",
			FILE_TREE_COLUMN_SEPARATOR,FALSE,
			FILE_TREE_COLUMN_URI,uri,
			-1);
	g_free(uri);
	g_free(subpath);
			
	g_hash_table_insert(m_pHashRootNodeOrder,g_strdup("Images"),(gpointer)iNodeOrder++);

	// Video clips
	subpath = g_build_filename(docs_dir,".videos", NULL);
	uri = g_filename_to_uri(subpath, NULL, NULL);
	gtk_tree_store_append (store, &iter2, &iter1);  
	gtk_tree_store_set (store, &iter2,
		FILE_TREE_COLUMN_CHECKBOX, FALSE,
		FILE_TREE_COLUMN_ICON, ICON_MAEMO_VIDEO,
			FILE_TREE_COLUMN_DISPLAY_NAME, "Video clips",
			FILE_TREE_COLUMN_SEPARATOR,FALSE,
			FILE_TREE_COLUMN_URI,uri,
			-1);
	g_free(uri);
	g_free(subpath);
			
	g_hash_table_insert(m_pHashRootNodeOrder,g_strdup("Video clips"),(gpointer)iNodeOrder++);

	// MMC 1
	const gchar* mmc_dir = g_getenv("INTERNAL_MMC_MOUNTPOINT");
	if (NULL != mmc_dir)
	{
		uri = g_filename_to_uri(mmc_dir, NULL, NULL);
		gtk_tree_store_append (store, &iter2, &iter1);  
		gtk_tree_store_set (store, &iter2,
			FILE_TREE_COLUMN_CHECKBOX, FALSE,
			FILE_TREE_COLUMN_ICON, ICON_MAEMO_MMC,
				FILE_TREE_COLUMN_DISPLAY_NAME, "Internal MMC",
				FILE_TREE_COLUMN_SEPARATOR,FALSE,
				FILE_TREE_COLUMN_URI,uri,
				-1);
		g_free(uri);
		g_hash_table_insert(m_pHashRootNodeOrder,g_strdup("Internal MMC"),(gpointer)iNodeOrder++);
	}

	// MMC 2
	mmc_dir = g_getenv("MMC_MOUNTPOINT");
	if (NULL != mmc_dir)
	{
		uri = g_filename_to_uri(mmc_dir, NULL, NULL);
		gtk_tree_store_append (store, &iter2, &iter1);  
		gtk_tree_store_set (store, &iter2,
			FILE_TREE_COLUMN_CHECKBOX, FALSE,
			FILE_TREE_COLUMN_ICON, ICON_MAEMO_MMC,
				FILE_TREE_COLUMN_DISPLAY_NAME, "External MMC",
				FILE_TREE_COLUMN_SEPARATOR,FALSE,
				FILE_TREE_COLUMN_URI,uri,
				-1);
		g_free(uri);
		g_hash_table_insert(m_pHashRootNodeOrder,g_strdup("External MMC"),(gpointer)iNodeOrder++);
	}

	g_free(docs_dir);
#else

	if (GNOME_VFS_OK == result)
	{
		gchar* uri = gnome_vfs_uri_to_string(vfsURI,(GnomeVFSURIHideOptions)0);
		icon_name = folder_tree_get_icon_name(uri);

		gtk_tree_store_append (store, &iter1, NULL);  
		gtk_tree_store_set (store, &iter1,
				FILE_TREE_COLUMN_CHECKBOX, FALSE,
			FILE_TREE_COLUMN_ICON, icon_name,
				FILE_TREE_COLUMN_DISPLAY_NAME, "Desktop",
				FILE_TREE_COLUMN_SEPARATOR,FALSE,
				FILE_TREE_COLUMN_URI,uri,
				-1);
				
		g_hash_table_insert(m_pHashRootNodeOrder,g_strdup("Desktop"),(gpointer)iNodeOrder++);

		free(icon_name);
		g_free(uri);

	}
	gnome_vfs_uri_unref(vfsURI);

	const char* name = g_get_real_name();

	gchar home_name[256];
	if (0 == strcmp(name,"Unknown"))
	{
		strcpy(home_name,"Home");
	}
	else
	{
		// get the first name
		gchar** tokens = g_strsplit(name," ",2);
		g_snprintf(home_name,256,"%s's Home",tokens[0]);
		g_strfreev(tokens);
	}
	

	icon_name = folder_tree_get_icon_name(home_uri);

	gtk_tree_store_append (store, &iter1, NULL);  
	gtk_tree_store_set (store, &iter1,
            FILE_TREE_COLUMN_CHECKBOX, FALSE,
			FILE_TREE_COLUMN_ICON, icon_name,
            FILE_TREE_COLUMN_DISPLAY_NAME, home_name,
            FILE_TREE_COLUMN_SEPARATOR,FALSE,
            FILE_TREE_COLUMN_URI,home_uri,
            -1);

	g_hash_table_insert(m_pHashRootNodeOrder,g_strdup(home_name),(gpointer)iNodeOrder++);
	free(icon_name);



	icon_name = folder_tree_get_icon_name(documents_uri);

	gtk_tree_store_append (store, &iter1, NULL);  
	gtk_tree_store_set (store, &iter1,
            FILE_TREE_COLUMN_CHECKBOX, FALSE,
			FILE_TREE_COLUMN_ICON, icon_name,
            FILE_TREE_COLUMN_DISPLAY_NAME, "Documents",
            FILE_TREE_COLUMN_SEPARATOR,FALSE,
            FILE_TREE_COLUMN_URI,documents_uri,
            -1);
            
	g_hash_table_insert(m_pHashRootNodeOrder,g_strdup("Documents"),(gpointer)iNodeOrder++);

	free(icon_name);

	icon_name = folder_tree_get_icon_name(pictures_uri);

	gtk_tree_store_append (store, &iter1, NULL);  
	gtk_tree_store_set (store, &iter1,
            FILE_TREE_COLUMN_CHECKBOX, FALSE,
        FILE_TREE_COLUMN_ICON, icon_name,
            FILE_TREE_COLUMN_DISPLAY_NAME, "Pictures",
            FILE_TREE_COLUMN_SEPARATOR,FALSE,
            FILE_TREE_COLUMN_URI, pictures_uri,
            -1);
            

	free(icon_name);

	g_hash_table_insert(m_pHashRootNodeOrder,g_strdup("Pictures"),(gpointer)iNodeOrder++);

#endif

	char* filesystem_path = "/";
	char* filesystem_uri = gnome_vfs_get_uri_from_local_path (filesystem_path);
	GnomeVFSVolumeMonitor* monitor = gnome_vfs_get_volume_monitor();
	GnomeVFSVolume* volume = gnome_vfs_volume_monitor_get_volume_for_path(monitor,filesystem_path);
	if (NULL != volume)
	{
#ifdef QUIVER_MAEMO
		char* icon2 = g_strdup(ICON_MAEMO_FOLDER_CLOSED);
#else
		char* icon2 = gnome_vfs_volume_get_icon(volume);
#endif

		gtk_tree_store_append (store, &iter1, NULL);  
		gtk_tree_store_set (store, &iter1,
				FILE_TREE_COLUMN_CHECKBOX, FALSE,
			FILE_TREE_COLUMN_ICON, icon2,
				FILE_TREE_COLUMN_DISPLAY_NAME, "Filesystem",
				FILE_TREE_COLUMN_SEPARATOR,FALSE,
				FILE_TREE_COLUMN_URI, filesystem_uri,
				-1);
				
		free(icon2);

		g_hash_table_insert(m_pHashRootNodeOrder,g_strdup("Filesystem"),(gpointer)iNodeOrder++);
	}

	GList *volumes = gnome_vfs_volume_monitor_get_mounted_volumes(monitor);
	GList *volume_itr = volumes;
	while (NULL != volume_itr)
	{
		GnomeVFSVolume *volume = GNOME_VFS_VOLUME(volume_itr->data);
		if (gnome_vfs_volume_is_user_visible(volume)  )
		{

			char* name = gnome_vfs_volume_get_display_name(volume);
			char* display_name = name;
			char* path = gnome_vfs_volume_get_device_path(volume);

#ifdef QUIVER_MAEMO
			char* icon2 = g_strdup(ICON_MAEMO_FOLDER_CLOSED);
#else
			char* icon2 = gnome_vfs_volume_get_icon(volume);
#endif
			char* uri  = gnome_vfs_volume_get_activation_uri(volume);
			char* type = gnome_vfs_volume_get_filesystem_type(volume);

			if (NULL == uri)
			{
				//printf("null uri \n");
				//printf("null uri for %s\n",name);
				continue;
			}
			
			if (0 == strcmp(uri,"file:///"))
			{
				display_name = QUIVER_FOLDER_TREE_ROOT_NAME;
			}
			
			{	
				/*
				 * FIXME: show type: GNOME_VFS_DEVICE_TYPE_HARDDRIVE first,
				 * then others sorted on uri
				 */
				gtk_tree_store_append (store, &iter1, NULL);  
				gtk_tree_store_set (store, &iter1,
	                    FILE_TREE_COLUMN_CHECKBOX, FALSE,
	                FILE_TREE_COLUMN_ICON, icon2,
	                    FILE_TREE_COLUMN_DISPLAY_NAME, display_name,
	                    FILE_TREE_COLUMN_SEPARATOR,FALSE,
	                    FILE_TREE_COLUMN_URI,uri,
	                    -1);

				g_hash_table_insert(m_pHashRootNodeOrder,g_strdup(display_name),(gpointer)iNodeOrder++);

			}
			
			
			free(name);
			free(path);
			free(icon2);
			free(uri);
			free(type);
		}
		gnome_vfs_volume_unref(volume);
		
		volume_itr = g_list_next(volume_itr);
	}
	g_list_free(volumes);

	MyDataStruct* mydata;
	mydata = (MyDataStruct*)g_malloc0(sizeof(MyDataStruct));
	mydata->model = GTK_TREE_MODEL(store);
	mydata->iter_parent = NULL; // special case
	pthread_t thread_id;
	pthread_create(&thread_id, NULL, thread_check_for_subdirs, mydata);
	m_setFolderThreads.insert(thread_id);

	free(home_uri);
	free(desktop_uri);
	free(pictures_uri);
	free(documents_uri);
	free(root_uri);
}


static gint sort_func (GtkTreeModel *model,GtkTreeIter *a, GtkTreeIter *b, gpointer user_data)
{

	FolderTree::FolderTreeImpl* pFolderTreeImpl = (FolderTree::FolderTreeImpl*)user_data;
	GtkTreeIter parent;

	gchar* name_a;
	gchar* name_b;
	gtk_tree_model_get(model,a, FILE_TREE_COLUMN_DISPLAY_NAME, &name_a, -1);
	gtk_tree_model_get(model,b, FILE_TREE_COLUMN_DISPLAY_NAME, &name_b, -1);

	

	if (NULL == name_a && NULL == name_b)
	{
		return 0;
	}
	else if (NULL == name_a)
	{
		g_free(name_b);
		return -1;
	}
	else if (NULL == name_b)
	{
		g_free(name_a);
		return 1;
	}

	int rval = 0;

	if (!gtk_tree_model_iter_parent(model,&parent,a))
	{
		int a_int = (int)g_hash_table_lookup(pFolderTreeImpl->m_pHashRootNodeOrder, name_a);
		int b_int = (int)g_hash_table_lookup(pFolderTreeImpl->m_pHashRootNodeOrder, name_b);

		rval = a_int - b_int;
	}
	else
	{
		rval = g_ascii_strcasecmp(name_a,name_b);
		/*
		gchar* a_casefolded = g_utf8_casefold(name_a, strlen(name_a));
		gchar* b_casefolded = g_utf8_casefold(name_b, strlen(name_b));
		gchar* a_collated = g_utf8_collate_key(a_casefolded,strlen(a_casefolded));
		gchar* b_collated = g_utf8_collate_key(b_casefolded,strlen(b_casefolded));
		rval = strcmp(a_collated,b_collated);
		g_free(a_casefolded);
		g_free(a_collated);
		g_free(b_casefolded);
		g_free(b_collated);
		*/
	}


	g_free(name_a);
	g_free(name_b);

	return rval;

}


