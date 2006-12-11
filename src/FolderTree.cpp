#include <config.h>

#include "FolderTree.h"
#include <gdk/gdkkeysyms.h>
#include <libgnomevfs/gnome-vfs.h>
#include <libgnomeui/libgnomeui.h>

#define QUIVER_TREE_COLUMN_TOGGLE      "column_toggle"
#define QUIVER_FOLDER_TREE_ROOT_NAME   "Filesystem"

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

gint sort_func (GtkTreeModel *model,GtkTreeIter *a, GtkTreeIter *b, gpointer user_data);
static gboolean folder_tree_is_separator (GtkTreeModel *model,GtkTreeIter *iter,gpointer data);
void folder_tree_selection_changed (GtkTreeSelection *treeselection, gpointer user_data);
static void folder_tree_cursor_changed (GtkTreeView *treeview, gpointer user_data);
gboolean view_onButtonPressed (GtkWidget *treeview, GdkEventButton *event, gpointer userdata);
gboolean view_on_key_press(GtkWidget *treeview, GdkEventKey *event, gpointer userdata);
gboolean view_onPopupMenu (GtkWidget *treeview, gpointer userdata);
void view_onRowActivated (GtkTreeView *treeview, GtkTreePath *path, GtkTreeViewColumn *col, gpointer userdata);
void signal_folder_tree_row_expanded(GtkTreeView *treeview, GtkTreeIter *iter_parent, GtkTreePath *treepath, gpointer data);

class FolderTree::FolderTreeImpl
{
public:
// constructor / destructor
	FolderTreeImpl(FolderTree *pFolderTree);
	~FolderTreeImpl();


// methods
	void CreateWidget();
	void PopulateTreeModel(GtkTreeStore *store);

	std::list<std::string> GetSelectedFolders();

// member variables
	GtkWidget* m_pWidget;
	FolderTree* m_pFolderTree;
	GHashTable* m_pHashRootNodeOrder;

};


FolderTree::FolderTree() : m_FolderTreeImplPtr(new FolderTreeImpl(this))
{
	
}

FolderTree::~FolderTree()
{
}

GtkWidget* FolderTree::GetWidget()
{
	return m_FolderTreeImplPtr->m_pWidget;
}

void FolderTree::SetUIManager(GtkUIManager* pUIManager)
{
}

std::list<std::string> FolderTree::GetSelectedFolders()
{
	return m_FolderTreeImplPtr->GetSelectedFolders();	
}



FolderTree::FolderTreeImpl::FolderTreeImpl(FolderTree *parent)
{
	m_pFolderTree = parent;
	CreateWidget();
}

FolderTree::FolderTreeImpl::~FolderTreeImpl()
{
	if (NULL != m_pHashRootNodeOrder)
	{
		g_hash_table_unref(m_pHashRootNodeOrder);
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

std::list<std::string> FolderTree::FolderTreeImpl::GetSelectedFolders()
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
	PopulateTreeModel (store);

	// it appears the sort column must be set after the sort function
	gtk_tree_sortable_set_default_sort_func(GTK_TREE_SORTABLE(store), sort_func,this, NULL);
	gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(store), GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID, GTK_SORT_ASCENDING);
	//gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(store), FILE_TREE_COLUMN_DISPLAY_NAME, sort_func,NULL, NULL);
	//gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(store), FILE_TREE_COLUMN_DISPLAY_NAME, GTK_SORT_ASCENDING);
	
	/* Create a view */
	m_pWidget = gtk_tree_view_new_with_model (GTK_TREE_MODEL (store));
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
	g_object_set (G_OBJECT (renderer),  "stock-size", 1,  NULL);

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


char* folder_tree_get_icon_name(const char* uri);

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

void folder_tree_set_selected_checkbox_value(GtkTreeView* treeview,gboolean value)
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

void
signal_check_selected (GtkWidget *menuitem, gpointer userdata)
{
	GtkTreeView *treeview = GTK_TREE_VIEW(userdata);
	folder_tree_set_selected_checkbox_value(treeview,TRUE);
	//g_print ("Do something!\n");
}

void
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


void
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

gboolean view_on_key_press(GtkWidget *treeview, GdkEventKey *event, gpointer userdata)
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
	if (GDK_Right == event->keyval)
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

gboolean
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

gboolean view_onPopupMenu (GtkWidget *treeview, gpointer userdata)
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

void folder_tree_selection_changed (GtkTreeSelection *treeselection, gpointer user_data)
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
	GtkTreeModel* model;
	GtkTreeIter* iter_parent;
	GtkTreeIter* iter_child;
	GnomeVFSDirectoryHandle* dir_handle;
	GHashTable* hash_table;
	MyIdleState state;
	gboolean has_subdirs;
	int i;

} MyDataStruct;


void folder_tree_add_subdir(GtkTreeModel* model, GtkTreeIter *iter_parent, const gchar* name, const gchar* uri)
{
	GtkTreeIter iter_child = {0};
	gtk_tree_store_append (GTK_TREE_STORE(model), &iter_child, iter_parent);  

	char* icon_name = folder_tree_get_icon_name(uri);

	gtk_tree_store_set (GTK_TREE_STORE(model), &iter_child,
			FILE_TREE_COLUMN_CHECKBOX, FALSE,
			FILE_TREE_COLUMN_ICON, icon_name,
			FILE_TREE_COLUMN_DISPLAY_NAME, name,
			FILE_TREE_COLUMN_SEPARATOR,FALSE,
			FILE_TREE_COLUMN_URI,uri,
			-1);
	free(icon_name);
}



static void hash_foreach_sync_add(gpointer key, gpointer value, gpointer user_data)
{
	gchar* folder_name = (gchar*)key;
	gchar* uri;

	MyDataStruct* data = (MyDataStruct*)user_data;
	GtkTreeModel *model = data->model;

	gtk_tree_model_get(model,data->iter_child, FILE_TREE_COLUMN_URI, &uri, -1);
	GnomeVFSURI * vfs_uri_dir = gnome_vfs_uri_new(uri);
	GnomeVFSURI * vfs_uri_file = gnome_vfs_uri_append_path(vfs_uri_dir,folder_name);
	gchar *str_uri_file = gnome_vfs_uri_to_string (vfs_uri_file,GNOME_VFS_URI_HIDE_NONE);

	folder_tree_add_subdir(model, data->iter_child, folder_name, str_uri_file);

	g_free (str_uri_file);
	gnome_vfs_uri_unref(vfs_uri_file);
	gnome_vfs_uri_unref(vfs_uri_dir);

	if (NULL != uri)
	{
		g_free(uri);
	}
					
}

static gboolean idle_check_for_subdirs(gpointer user_data)
{
	gdk_threads_enter();
	MyDataStruct* data = (MyDataStruct*)user_data;
	GtkTreeIter* iter_parent = data->iter_parent;

	GtkTreeModel *model = data->model;

	gboolean finished = FALSE;
	char* uri = NULL;
	GnomeVFSResult result;

	switch (data->state)
	{
		case SYNC_TREE:
			{
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
							gchar* name = NULL;
							gtk_tree_model_get(model, &iter, FILE_TREE_COLUMN_DISPLAY_NAME, &name, -1);


							gpointer* orig_key = NULL;
							gpointer* value    = NULL;

							if (g_hash_table_lookup_extended(data->hash_table, name, orig_key, value))
							{
								// if the key exists, remove it from the hash
								g_hash_table_remove(data->hash_table, name);
							}
							else
							{
								// if the key does not exist, remove the iterator
								gtk_tree_store_remove(GTK_TREE_STORE(model),&iter);
							}
							g_free(name);
						}
					}
					// add new items
					g_hash_table_foreach(data->hash_table, hash_foreach_sync_add, data);

				}

				gtk_tree_iter_free(data->iter_child);
				data->iter_child = NULL;
				data->state = TRAVERSE_TREE;

				if (NULL != data->hash_table)
				{
					g_hash_table_unref( data->hash_table );
					data->hash_table = NULL;
				}
					
			}
			break;
		case TRAVERSE_DIR:
			{
				gtk_tree_model_get(model,data->iter_child, FILE_TREE_COLUMN_URI, &uri, -1);
				GnomeVFSURI * vfs_uri_dir = gnome_vfs_uri_new(uri);
				GnomeVFSFileInfo *info = gnome_vfs_file_info_new ();;
				result = gnome_vfs_directory_read_next(data->dir_handle,info);
					
				if (GNOME_VFS_OK == result )
				{
					GnomeVFSURI * vfs_uri_file = gnome_vfs_uri_append_path(vfs_uri_dir,info->name);
					gchar *str_uri_file = gnome_vfs_uri_to_string (vfs_uri_file,GNOME_VFS_URI_HIDE_NONE);
					
					if ( ( 0 == strstr(uri,str_uri_file)) &&
						(GNOME_VFS_FILE_INFO_FIELDS_TYPE  & info->valid_fields) &&
						 (info->type == GNOME_VFS_FILE_TYPE_DIRECTORY) )
					{
						if ('.' == info->name[0])
						{
							// FIXME: show hidden?
						}
						else
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
				gtk_tree_model_get(model,data->iter_child, FILE_TREE_COLUMN_URI, &uri, -1);

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
						gtk_tree_store_remove (GTK_TREE_STORE(model),data->iter_child);		
						data->i--;
					}

					gtk_tree_iter_free(data->iter_child);
					data->iter_child = NULL;

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

				if ( gtk_tree_model_iter_nth_child (model,&iter_child, iter_parent, data->i) )
				{
					data->iter_child = gtk_tree_iter_copy(&iter_child);
					data->state = OPEN_DIR;
				}
				else
				{
					finished = TRUE;
				}
				data->i++;
			}
			break;
		default:
			break;
	}

	if (finished)
	{
		if (NULL != data->iter_parent)
		{
			gtk_tree_iter_free(data->iter_parent);
			data->iter_parent = NULL;
		}

		if (NULL != data->iter_child)
		{
			gtk_tree_iter_free(data->iter_child);
			data->iter_child = NULL;
		}

		if (NULL != data->hash_table)
		{
			g_hash_table_unref( data->hash_table );
		}
		g_free(data);
	}

	gdk_threads_leave();

	return !finished;

}

void signal_folder_tree_row_expanded(GtkTreeView *treeview,
						GtkTreeIter *iter_parent,
						GtkTreePath *treepath,
						gpointer data)
{
	GtkTreeModel *model = gtk_tree_view_get_model(treeview);
	MyDataStruct* mydata = (MyDataStruct*)g_malloc0(sizeof(MyDataStruct));
	mydata->model = model;

	gchar* uri;
	gtk_tree_model_get(model, iter_parent, FILE_TREE_COLUMN_URI, &uri, -1);
	g_free(uri);

	mydata->iter_parent = gtk_tree_iter_copy(iter_parent);
	g_idle_add(idle_check_for_subdirs,mydata);
}


char* folder_tree_get_icon_name(const char* uri)
{
	GnomeIconLookupResultFlags lookup_result;
	GtkIconTheme* icon_theme = gtk_icon_theme_get_default();

	char* preferred_icon_name = NULL;
	const char* homedir = g_get_home_dir();
	char* desktop_path = g_build_filename (homedir, "Desktop", NULL);

	char* home_uri = gnome_vfs_get_uri_from_local_path (homedir);
	char* desktop_uri = gnome_vfs_get_uri_from_local_path (desktop_path);

	if (gnome_vfs_uris_match (home_uri,uri))
	{
		preferred_icon_name = "gnome-fs-home";
	}
	else if (gnome_vfs_uris_match (desktop_uri,uri))
	{
		preferred_icon_name = "gnome-fs-desktop";
	}
	else if (gnome_vfs_uris_match ("trash://",uri))
	{
		preferred_icon_name = GNOME_STOCK_TRASH;
	}

	free(desktop_path);
	free(desktop_uri);
	free(home_uri);

	char* icon_name = gnome_icon_lookup_sync (icon_theme,
										 NULL,
										 uri,
										 preferred_icon_name,
										 GNOME_ICON_LOOKUP_FLAGS_NONE,
										 &lookup_result);
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

	// get the first name
	gchar** tokens = g_strsplit(name," ",2);
	gchar home_name[256];
	g_snprintf(home_name,256,"%s's Home",tokens[0]);
	g_strfreev(tokens);
	

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


	char* filesystem_path = "/";
	char* filesystem_uri = gnome_vfs_get_uri_from_local_path (filesystem_path);
	GnomeVFSVolumeMonitor* monitor = gnome_vfs_get_volume_monitor();
	GnomeVFSVolume* volume = gnome_vfs_volume_monitor_get_volume_for_path(monitor,filesystem_path);
	char* icon2 = gnome_vfs_volume_get_icon(volume);

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

			char* icon2 = gnome_vfs_volume_get_icon(volume);
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
	g_idle_add(idle_check_for_subdirs,mydata);

	free(home_uri);
	free(desktop_uri);
	free(pictures_uri);
	free(documents_uri);
	free(root_uri);
}


gint sort_func (GtkTreeModel *model,GtkTreeIter *a, GtkTreeIter *b, gpointer user_data)
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

		int a_int = (int)g_hash_table_lookup(pFolderTreeImpl->m_pHashRootNodeOrder,name_a);
		int b_int = (int)g_hash_table_lookup(pFolderTreeImpl->m_pHashRootNodeOrder,name_b);
		rval = a_int - b_int;
	}
	else
	{
		gchar* a_casefolded = g_utf8_casefold(name_a, strlen(name_a));
		gchar* b_casefolded = g_utf8_casefold(name_b, strlen(name_b));
		gchar* a_collated = g_utf8_collate_key(a_casefolded,strlen(a_casefolded));
		gchar* b_collated = g_utf8_collate_key(b_casefolded,strlen(b_casefolded));
		rval = strcmp(a_collated,b_collated);
		g_free(a_casefolded);
		g_free(a_collated);
		g_free(b_casefolded);
		g_free(b_collated);
	}


	g_free(name_a);
	g_free(name_b);

	return rval;

}


