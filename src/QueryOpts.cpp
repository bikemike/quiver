#include "QueryOpts.h"

QueryOpts::QueryOpts()
{
}

QueryOpts::~QueryOpts()
{
}

GtkWidget* QueryOpts::GetWidget() const
{
	return NULL;//m_QueryOptsImplPtr->m_pWidget;
}

void QueryOpts::SetUIManager(GtkUIManager *ui_manager)
{
}

class QueryOpts::QueryOptsImpl
{
public:
// constructor / destructor
	QueryOptsImpl(QueryOpts *pQueryOpts);
	~QueryOptsImpl();


// methods
	void CreateWidget();

// member variables
	GtkWidget*       m_pWidget;
	QueryOpts*       m_pQueryOpts;
};

QueryOpts::QueryOptsImpl::QueryOptsImpl(QueryOpts *parent)
{
	m_pQueryOpts = parent;
	
	CreateWidget();
}

QueryOpts::QueryOptsImpl::~QueryOptsImpl()
{
//	if (NULL != m_pHashRootNodeOrder)
//	{
//		g_hash_table_destroy(m_pHashRootNodeOrder);
//	}
}

void QueryOpts::QueryOptsImpl::CreateWidget()
{
//	m_pHashRootNodeOrder = g_hash_table_new(g_str_hash,g_str_equal);

	//printf("quiver_folder_tree_new start\n");
//	GtkTreeViewColumn *column;
//	GtkCellRenderer *renderer;
//	GtkTreeSelection *selection;
//	GtkTreeStore *store;

	
//	/* Create a model.  We are using the store model for now, though we
//	* could use any other GtkTreeModel */
//	store = gtk_tree_store_new (FILE_TREE_COLUMN_COUNT,
//	 G_TYPE_BOOLEAN,
//	 G_TYPE_STRING,
//	 G_TYPE_STRING,
// 	 G_TYPE_STRING,
//	 G_TYPE_BOOLEAN,
//	 G_TYPE_BOOLEAN,
//	 G_TYPE_BOOLEAN
//	 );
//	
//	/* custom function to fill the model with data */
//
//	// it appears the sort column must be set after the sort function
//	//gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(store), FILE_TREE_COLUMN_DISPLAY_NAME, sort_func,NULL, NULL);
//	//gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(store), FILE_TREE_COLUMN_DISPLAY_NAME, GTK_SORT_ASCENDING);
//	
//	/* Create a view */
//	m_pWidget = gtk_tree_view_new();
//
//	PopulateTreeModel (store);
//
//	gtk_tree_sortable_set_default_sort_func(GTK_TREE_SORTABLE(store), sort_func,this, NULL);
//	gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(store), GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID, GTK_SORT_ASCENDING);
// 	
//	gtk_tree_view_set_model(GTK_TREE_VIEW(m_pWidget), GTK_TREE_MODEL (store));
//	
//	GtkTreeIter iter;
//	if ( gtk_tree_model_get_iter_first (GTK_TREE_MODEL(store), &iter) )
//	{
//		GtkTreePath* first =  gtk_tree_model_get_path(GTK_TREE_MODEL(store), &iter);
//
//		gtk_tree_view_set_cursor (GTK_TREE_VIEW(m_pWidget),
//		                             first,
//		                             NULL,
//		                             FALSE);
//		gtk_tree_path_free(first);
//	}
//			                            
//#if GTK_MAJOR_VERSION == 2  &&  GTK_MINOR_VERSION >= 10 || GTK_MAJOR_VERSION > 2
//	gtk_tree_view_set_rubber_banding(GTK_TREE_VIEW(m_pWidget),TRUE);
//#endif
//	//gtk_tree_view_set_enable_tree_lines(GTK_TREE_VIEW(m_pWidget),TRUE);
//	//gtk_tree_view_set_grid_lines(GTK_TREE_VIEW(m_pWidget),GTK_TREE_VIEW_GRID_LINES_BOTH);
//	//gtk_widget_show(m_pWidget);
//	
//	
//	GdkColor highlight_color;
//	gdk_color_parse("#ccc",&highlight_color);
//	/* = m_pWidget->style->bg[GTK_STATE_SELECTED];*/
//
//	/*
//	highlight_color.red =  m_pWidget->style->bg[GTK_STATE_SELECTED].red/2 + m_pWidget->style->bg[GTK_STATE_NORMAL].red/2;
//	highlight_color.green =  m_pWidget->style->bg[GTK_STATE_SELECTED].green/2 + m_pWidget->style->bg[GTK_STATE_NORMAL].green/2;
//	highlight_color.blue =  m_pWidget->style->bg[GTK_STATE_SELECTED].blue/2 + m_pWidget->style->bg[GTK_STATE_NORMAL].blue/2;
//	*/
//	
//	/* The view now holds a reference.  We can get rid of our own
//	* reference */
//	g_object_unref (G_OBJECT (store));
//	
//	renderer = gtk_cell_renderer_toggle_new ();
//	g_object_set (G_OBJECT (renderer),  "mode", GTK_CELL_RENDERER_MODE_INERT,  NULL);
//	g_object_set (G_OBJECT (renderer),  "cell-background-gdk", &highlight_color,  NULL);    
//	column = gtk_tree_view_column_new_with_attributes (QUIVER_TREE_COLUMN_TOGGLE,
//	  renderer,
//	  "active", FILE_TREE_COLUMN_CHECKBOX,
//	  "cell-background-set",FILE_TREE_COLUMN_CHECKBOX,
//	NULL);
//	gtk_tree_view_append_column (GTK_TREE_VIEW (m_pWidget), column);
//
//	/* Second column.. title of the book. */
//	column = gtk_tree_view_column_new();
//	gtk_tree_view_column_set_title(column,"Folder");
//
//	m_pCellRendererPixbuf = gtk_cell_renderer_pixbuf_new ();
//	g_object_set (G_OBJECT (m_pCellRendererPixbuf),  "mode", GTK_CELL_RENDERER_MODE_INERT,  NULL);
//	g_object_set (G_OBJECT (m_pCellRendererPixbuf),  "cell-background-gdk", &highlight_color,  NULL);    
//
//	gtk_tree_view_column_pack_start (column,m_pCellRendererPixbuf,FALSE);
//	gtk_tree_view_column_set_attributes(column,
//	  m_pCellRendererPixbuf,
//	  "icon_name", FILE_TREE_COLUMN_ICON,
//  	  "cell-background-set",FILE_TREE_COLUMN_CHECKBOX,
//	NULL);
//	g_object_set (G_OBJECT (m_pCellRendererPixbuf),  "stock-size", 3,  NULL);
//
//	renderer = gtk_cell_renderer_text_new ();
//	g_object_set (G_OBJECT (renderer),  "mode", GTK_CELL_RENDERER_MODE_INERT,  NULL);
//	g_object_set (G_OBJECT (renderer),  "cell-background-gdk", &highlight_color,  NULL);    	
//	gtk_tree_view_column_pack_end (column,renderer,TRUE);
//
//	gtk_tree_view_column_add_attribute(column,renderer,"text",FILE_TREE_COLUMN_DISPLAY_NAME);
//	gtk_tree_view_column_add_attribute(column,renderer,"cell-background-set",FILE_TREE_COLUMN_CHECKBOX);
//
//	gtk_tree_view_append_column (GTK_TREE_VIEW (m_pWidget), column);
//	gtk_tree_view_set_expander_column(GTK_TREE_VIEW (m_pWidget),column);
//	
//	gtk_tree_view_set_row_separator_func(GTK_TREE_VIEW (m_pWidget),folder_tree_is_separator,NULL,NULL);
//	/*GtkDestroyNotify destroy);*/
//	
//	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW (m_pWidget),FALSE);
//	
//	gtk_tree_view_set_search_column(GTK_TREE_VIEW (m_pWidget),FILE_TREE_COLUMN_DISPLAY_NAME);
//	
//	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW (m_pWidget));
//	
//	gtk_tree_selection_set_mode(selection,GTK_SELECTION_MULTIPLE);
//	
//	g_signal_connect(G_OBJECT(selection),"changed",G_CALLBACK(folder_tree_selection_changed),this);
//	g_signal_connect(m_pWidget, "cursor-changed",G_CALLBACK(folder_tree_cursor_changed),this);
//	g_signal_connect(m_pWidget, "button-press-event", (GCallback) view_onButtonPressed, this);
//	g_signal_connect(m_pWidget, "key-press-event", (GCallback) view_on_key_press, this);
//	g_signal_connect(m_pWidget, "popup-menu", (GCallback) view_onPopupMenu, this);
//	g_signal_connect(m_pWidget, "row-activated", (GCallback) view_onRowActivated, this);
//	g_signal_connect(m_pWidget, "row-expanded", (GCallback) signal_folder_tree_row_expanded,this);
//	g_signal_connect(m_pWidget, "row-collapsed", (GCallback) signal_folder_tree_row_collapsed,this);
	
	//printf("quiver_folder_tree_new end\n");
}
