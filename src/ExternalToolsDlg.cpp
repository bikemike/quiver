#include <config.h>

#include "ExternalToolsDlg.h"

#include <list>
#include <vector>

#include "ExternalTools.h"
#include "IExternalToolsEventHandler.h"
#include "ExternalToolAddEditDlg.h"

#include "QuiverStockIcons.h"

using namespace std;

enum 
{
	COLUMN_ID,
	COLUMN_ICON,
	COLUMN_NAME,
	COLUMN_COUNT
};

class ExternalToolsDlg::ExternalToolsDlgPriv
{
public:
// constructor, destructor
	ExternalToolsDlgPriv(ExternalToolsDlg *parent);
	~ExternalToolsDlgPriv();
	
// methods
	void LoadWidgets();
	void UpdateUI();
	void SelectionChanged();
	void ConnectSignals();

// variables
	ExternalToolsDlg*     m_pExternalToolsDlg;
	GtkBuilder*         m_pGtkBuilder;
	ExternalToolsPtr      m_ExternalToolsPtr;

	bool m_bLoadedDlg;
	
	// dlg widgets
	GtkWidget*             m_pWidget;
	GtkTreeView*           m_pTreeViewExternalTools;
	GtkButton*             m_pButtonMoveUp;
	GtkButton*             m_pButtonMoveDown;
	GtkButton*             m_pButtonAdd;
	GtkButton*             m_pButtonEdit;
	GtkButton*             m_pButtonRemove;
	GtkButton*             m_pButtonClose;

// nested classes
	class ExternalToolsEventHandler : public IExternalToolsEventHandler
	{
	public:
		ExternalToolsEventHandler(ExternalToolsDlgPriv* parent) {this->parent = parent;};
		virtual void HandleExternalToolChanged(ExternalToolsEventPtr event);
	private:
		ExternalToolsDlgPriv* parent;
	};
	IExternalToolsEventHandlerPtr m_ExternalToolsEventHandler;
	
};


ExternalToolsDlg::ExternalToolsDlg() : m_PrivPtr(new ExternalToolsDlg::ExternalToolsDlgPriv(this))
{
	
}


GtkWidget* ExternalToolsDlg::GetWidget()
{
	  return NULL;
}


void ExternalToolsDlg::Run()
{
	if (m_PrivPtr->m_bLoadedDlg)
	{
		gtk_dialog_run(GTK_DIALOG(m_PrivPtr->m_pWidget));
		gtk_widget_destroy(m_PrivPtr->m_pWidget);
	}
}

// private stuff


// prototypes
static void  on_clicked (GtkButton *button, gpointer user_data);
static void selection_changed (GtkTreeSelection *treeselection, gpointer user_data);
static void cell_edited_callback (GtkCellRendererText *cell, gchar *path_string, gchar *new_text, gpointer user_data);


ExternalToolsDlg::ExternalToolsDlgPriv::ExternalToolsDlgPriv(ExternalToolsDlg *parent) :
        m_pExternalToolsDlg(parent),
        m_ExternalToolsEventHandler( new ExternalToolsEventHandler(this) )
{
	m_ExternalToolsPtr = ExternalTools::GetInstance();
	m_ExternalToolsPtr->AddEventHandler(m_ExternalToolsEventHandler);
	m_bLoadedDlg = false;

	m_pGtkBuilder = gtk_builder_new();
	gchar* objectids[] = {
		"ExternalToolsDialog",
		NULL};
	gtk_builder_add_objects_from_file (m_pGtkBuilder, QUIVER_DATADIR "/" "quiver.ui", objectids, NULL);
	LoadWidgets();
	UpdateUI();
	ConnectSignals();
}

ExternalToolsDlg::ExternalToolsDlgPriv::~ExternalToolsDlgPriv()
{
	m_ExternalToolsPtr->RemoveEventHandler(m_ExternalToolsEventHandler);
	if (NULL != m_pGtkBuilder)
	{
		g_object_unref(m_pGtkBuilder);
		m_pGtkBuilder = NULL;
	}
}


void ExternalToolsDlg::ExternalToolsDlgPriv::LoadWidgets()
{

	if (NULL != m_pGtkBuilder)
	{
		m_pWidget                = GTK_WIDGET(gtk_builder_get_object (m_pGtkBuilder, "ExternalToolsDialog"));
		m_pTreeViewExternalTools     = GTK_TREE_VIEW(     gtk_builder_get_object (m_pGtkBuilder, "externaltools_treeview") );

		m_pButtonClose           = GTK_BUTTON( gtk_button_new_from_stock(QUIVER_STOCK_CLOSE) );
		/*
		m_pButtonAdd             = GTK_BUTTON( gtk_button_new_from_stock(QUIVER_STOCK_ADD) );
		m_pButtonEdit            = GTK_BUTTON( gtk_button_new_from_stock(QUIVER_STOCK_EDIT) );
		m_pButtonRemove          = GTK_BUTTON( gtk_button_new_from_stock(QUIVER_STOCK_REMOVE) );


		gtk_widget_show(GTK_WIDGET(m_pButtonAdd));
		gtk_widget_show(GTK_WIDGET(m_pButtonEdit));
		gtk_widget_show(GTK_WIDGET(m_pButtonRemove));
		*/
		gtk_widget_show(GTK_WIDGET(m_pButtonClose));

		if (m_pWidget)
		{
			/*
			gtk_box_pack_start(GTK_BOX(GTK_DIALOG(m_pWidget)->action_area),GTK_WIDGET(m_pButtonAdd),FALSE,TRUE,5);
			gtk_box_pack_start(GTK_BOX(GTK_DIALOG(m_pWidget)->action_area),GTK_WIDGET(m_pButtonEdit),FALSE,TRUE,5);
			gtk_box_pack_start(GTK_BOX(GTK_DIALOG(m_pWidget)->action_area),GTK_WIDGET(m_pButtonRemove),FALSE,TRUE,5);
			*/
			gtk_box_pack_start(GTK_BOX(GTK_DIALOG(m_pWidget)->action_area),GTK_WIDGET(m_pButtonClose),FALSE,TRUE,5);
		}
		if (m_pTreeViewExternalTools)
		{
			GtkTreeViewColumn*column;

			GtkCellRenderer* renderer = gtk_cell_renderer_pixbuf_new ();
			g_object_set (G_OBJECT (renderer),  "mode", GTK_CELL_RENDERER_MODE_INERT,  NULL);
			g_object_set (G_OBJECT (renderer),  "stock-size", 3,  NULL);

			column = gtk_tree_view_column_new_with_attributes ("icon",
			  renderer,
			  "icon_name", COLUMN_ICON,
			NULL);


			gtk_tree_view_append_column (m_pTreeViewExternalTools, column);

			renderer = gtk_cell_renderer_text_new ();
			g_object_set (G_OBJECT (renderer),  "mode", GTK_CELL_RENDERER_MODE_INERT,  NULL);
			g_object_set (G_OBJECT (renderer),  "editable", TRUE,  NULL);
			g_signal_connect(renderer, "edited", (GCallback) cell_edited_callback, this);

			column = gtk_tree_view_column_new_with_attributes ("externaltool",
			  renderer,
			  "text",COLUMN_NAME,
			NULL);

			gtk_tree_view_append_column (m_pTreeViewExternalTools, column);

#if GTK_MAJOR_VERSION == 2  &&  GTK_MINOR_VERSION >= 12 || GTK_MAJOR_VERSION > 2
			g_object_set(G_OBJECT(m_pTreeViewExternalTools),"show-expanders",FALSE,NULL);
#endif
	
			gtk_tree_view_set_search_column (m_pTreeViewExternalTools,COLUMN_NAME);
			GtkTreeSelection* selection = gtk_tree_view_get_selection(m_pTreeViewExternalTools);
			gtk_tree_selection_set_mode(selection,GTK_SELECTION_MULTIPLE);
		}

		m_pButtonMoveUp          = GTK_BUTTON(     gtk_builder_get_object (m_pGtkBuilder, "externaltools_button_move_up") );
		m_pButtonMoveDown        = GTK_BUTTON(     gtk_builder_get_object (m_pGtkBuilder, "externaltools_button_move_down") );
		m_pButtonAdd             = GTK_BUTTON(     gtk_builder_get_object (m_pGtkBuilder, "externaltools_button_add") );
		m_pButtonEdit            = GTK_BUTTON(     gtk_builder_get_object (m_pGtkBuilder, "externaltools_button_edit") );
		m_pButtonRemove          = GTK_BUTTON(     gtk_builder_get_object (m_pGtkBuilder, "externaltools_button_remove") );
		//m_pButtonClose           = GTK_BUTTON(     gtk_builder_get_object (m_pGtkBuilder, "button_close") );

		m_bLoadedDlg = (
				NULL != m_pWidget && 
				NULL != m_pTreeViewExternalTools && 
				NULL != m_pButtonMoveDown && 
				NULL != m_pButtonMoveUp && 
				NULL != m_pButtonClose && 
				NULL != m_pButtonRemove && 
				NULL != m_pButtonEdit && 
				NULL != m_pButtonAdd); 
	}
}

void ExternalToolsDlg::ExternalToolsDlgPriv::SelectionChanged()
{
	GList* paths;
	GList* path_itr;
	GtkTreeModel *model;
	GtkTreeSelection* selection;
	int selection_count = 0;
	
	GtkTreePath *path;
	GtkTreeIter iter;

	model = gtk_tree_view_get_model(m_pTreeViewExternalTools);
	selection = gtk_tree_view_get_selection(m_pTreeViewExternalTools);

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
		// disable edit, remove
		gtk_widget_set_sensitive(GTK_WIDGET(m_pButtonRemove),FALSE);
		gtk_widget_set_sensitive(GTK_WIDGET(m_pButtonEdit),FALSE);
		gtk_widget_set_sensitive(GTK_WIDGET(m_pButtonMoveUp),FALSE);
		gtk_widget_set_sensitive(GTK_WIDGET(m_pButtonMoveDown),FALSE);
	}
	else if (1 == selection_count)
	{
		if (bTop)
		{
			gtk_widget_set_sensitive(GTK_WIDGET(m_pButtonMoveUp),FALSE);
		}
		else
		{
			gtk_widget_set_sensitive(GTK_WIDGET(m_pButtonMoveUp),TRUE);
		}
		if (bBottom)
		{
			gtk_widget_set_sensitive(GTK_WIDGET(m_pButtonMoveDown),FALSE);
		}
		else
		{
			gtk_widget_set_sensitive(GTK_WIDGET(m_pButtonMoveDown),TRUE);
		}

		// enable edit,remove
		gtk_widget_set_sensitive(GTK_WIDGET(m_pButtonRemove),TRUE);
		gtk_widget_set_sensitive(GTK_WIDGET(m_pButtonEdit),TRUE);
	}
	else
	{
		// disable edit, enable remove
		gtk_widget_set_sensitive(GTK_WIDGET(m_pButtonRemove),TRUE);
		gtk_widget_set_sensitive(GTK_WIDGET(m_pButtonEdit),FALSE);
		gtk_widget_set_sensitive(GTK_WIDGET(m_pButtonMoveUp),FALSE);
		gtk_widget_set_sensitive(GTK_WIDGET(m_pButtonMoveDown),FALSE);
	}
}

void ExternalToolsDlg::ExternalToolsDlgPriv::UpdateUI()
{
	if (m_bLoadedDlg)
	{
		ExternalToolsPtr externaltoolsPtr = m_ExternalToolsPtr;
		
		//m_pTreeViewExternalTools;
		GtkTreeStore *store;
		store = gtk_tree_store_new(COLUMN_COUNT, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING);
		vector<ExternalTool> externaltools = externaltoolsPtr->GetExternalTools();
		vector<ExternalTool>::iterator itr;

		for (itr = externaltools.begin(); externaltools.end() != itr; ++itr)
		{
			GtkTreeIter iter1 = {0};
			gtk_tree_store_append (store, &iter1, NULL);  
			gtk_tree_store_set (store, &iter1,
				COLUMN_ID, itr->GetID(),
				COLUMN_NAME, itr->GetName().c_str(),
				COLUMN_ICON, itr->GetIcon().c_str(),
				-1);

		}
		gtk_tree_view_set_model(m_pTreeViewExternalTools, GTK_TREE_MODEL (store));
		g_object_unref(store);

		SelectionChanged();

	}	

}


void ExternalToolsDlg::ExternalToolsDlgPriv::ConnectSignals()
{
	if (m_bLoadedDlg)
	{
		g_signal_connect(m_pButtonMoveUp,
			"clicked",(GCallback)on_clicked,this);
		g_signal_connect(m_pButtonMoveDown,
			"clicked",(GCallback)on_clicked,this);
		g_signal_connect(m_pButtonAdd,
			"clicked",(GCallback)on_clicked,this);
		g_signal_connect(m_pButtonEdit,
			"clicked",(GCallback)on_clicked,this);
		g_signal_connect(m_pButtonRemove,
			"clicked",(GCallback)on_clicked,this);
		g_signal_connect(m_pButtonClose,
			"clicked",(GCallback)on_clicked,this);

		GtkTreeSelection* selection = gtk_tree_view_get_selection(m_pTreeViewExternalTools);
		g_signal_connect(G_OBJECT(selection),
			"changed",G_CALLBACK(selection_changed),this);
	}
}


static void  on_clicked (GtkButton *button, gpointer user_data)
{
	ExternalToolsDlg::ExternalToolsDlgPriv *priv = static_cast<ExternalToolsDlg::ExternalToolsDlgPriv*>(user_data);
	ExternalToolsPtr externaltoolsPtr = priv->m_ExternalToolsPtr;
	
	list<int> values;

	// move the externaltool one up in the row
	GList* paths;
	GList* path_itr;
	GtkTreePath *path;
	GtkTreeModel *model;
	GtkTreeSelection* selection;
	GtkTreeIter iter;
	int value;
	
	model = gtk_tree_view_get_model(priv->m_pTreeViewExternalTools);
	selection = gtk_tree_view_get_selection(priv->m_pTreeViewExternalTools);


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
	if (button == priv->m_pButtonMoveUp)
	{ 
		for (list<int>::iterator itr = values.begin(); values.end() != itr; ++itr)
		{
			priv->m_ExternalToolsPtr->MoveUp(*itr);
			// make sure the item is selected again
			// because the model has changed
			model = gtk_tree_view_get_model(priv->m_pTreeViewExternalTools);
			selection = gtk_tree_view_get_selection(priv->m_pTreeViewExternalTools);
			int n_children = gtk_tree_model_iter_n_children(model,NULL);
			for (int i = 0; i < n_children; ++i)
			{
				gtk_tree_model_iter_nth_child(model, &iter, NULL,i);
				gtk_tree_model_get (model,&iter,COLUMN_ID,&value,-1);
				if (value == *itr)
				{
					gtk_tree_selection_select_iter(selection,&iter);
				}
			}
		}
	}
	else if (button == priv->m_pButtonMoveDown)
	{
		for (list<int>::iterator itr = values.begin(); values.end() != itr; ++itr)
		{
			priv->m_ExternalToolsPtr->MoveDown(*itr);
			// make sure the item is selected again
			// because the model has changed
			model = gtk_tree_view_get_model(priv->m_pTreeViewExternalTools);
			selection = gtk_tree_view_get_selection(priv->m_pTreeViewExternalTools);
			int n_children = gtk_tree_model_iter_n_children(model,NULL);
			for (int i = 0; i < n_children; ++i)
			{
				gtk_tree_model_iter_nth_child(model, &iter, NULL,i);
				gtk_tree_model_get (model,&iter,COLUMN_ID,&value,-1);
				if (value == *itr)
				{
					gtk_tree_selection_select_iter(selection,&iter);
				}
			}
		}
	}
	else if (button == priv->m_pButtonAdd)
	{
		ExternalToolAddEditDlg dlg;
		dlg.Run();
		if (!dlg.Cancelled())
		{
			ExternalTool newbm = dlg.GetExternalTool();
			if (!newbm.GetName().empty())
			{
				priv->m_ExternalToolsPtr->AddExternalTool(newbm);
			}
		}

	}
	else if (button == priv->m_pButtonEdit)
	{

		for (list<int>::iterator itr = values.begin(); values.end() != itr; ++itr)
		{
			const ExternalTool* ext = priv->m_ExternalToolsPtr->GetExternalTool(*itr);
			if (NULL != ext)
			{
				ExternalToolAddEditDlg dlg(*ext);
				dlg.Run();
				if (!dlg.Cancelled())
				{
					ExternalTool new_ext = dlg.GetExternalTool();
					if (!new_ext.GetName().empty())
					{
						priv->m_ExternalToolsPtr->UpdateExternalTool(new_ext);
					}
				}
			}
		}
	}
	else if (button == priv->m_pButtonRemove)
	{
		for (list<int>::iterator itr = values.begin(); values.end() != itr; ++itr)
		{
			priv->m_ExternalToolsPtr->Remove(*itr);
		}
	}
	else if (button == priv->m_pButtonClose)
	{
		gtk_dialog_response(GTK_DIALOG(priv->m_pWidget), GTK_RESPONSE_CLOSE);
	}
}

static void selection_changed (GtkTreeSelection *treeselection, gpointer user_data)
{
	ExternalToolsDlg::ExternalToolsDlgPriv *priv = static_cast<ExternalToolsDlg::ExternalToolsDlgPriv*>(user_data);
	priv->SelectionChanged();
}

static void
cell_edited_callback (GtkCellRendererText *cell, gchar *path_string, gchar *new_text, gpointer user_data)
{
	ExternalToolsDlg::ExternalToolsDlgPriv *priv = static_cast<ExternalToolsDlg::ExternalToolsDlgPriv*>(user_data);

	GtkTreePath *path;
	GtkTreeIter child = {0};
	GtkTreeIter parent = {0};

	int value;

	GtkTreeModel *pTreeModel = gtk_tree_view_get_model(priv->m_pTreeViewExternalTools);

	path = gtk_tree_path_new_from_string(path_string);
	gtk_tree_model_get_iter(pTreeModel,&child,path);

	gtk_tree_model_iter_parent(pTreeModel,&parent, &child);
	
	gtk_tree_model_get (pTreeModel,&child,COLUMN_ID,&value,-1);

	const ExternalTool* ext = priv->m_ExternalToolsPtr->GetExternalTool(value);
	if (NULL != ext)
	{
		ExternalTool modified = *ext;
		modified.SetName(new_text);
		priv->m_ExternalToolsPtr->UpdateExternalTool(modified);
	}
}

// nested class

void ExternalToolsDlg::ExternalToolsDlgPriv::ExternalToolsEventHandler::HandleExternalToolChanged(ExternalToolsEventPtr event)
{
	if (parent->m_bLoadedDlg)
	{
		parent->UpdateUI();
	}
}



