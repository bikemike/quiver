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
		gtk_window_present(GTK_WINDOW(m_PrivPtr->m_pWidget));
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

	m_pGtkBuilder = gtk_builder_new_from_file(QUIVER_DATADIR "/" "quiver.ui");
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

		m_pButtonClose           = (GtkButton*)gtk_button_new_from_icon_name("window-close");
		gtk_widget_show(GTK_WIDGET(m_pButtonClose));

		if (m_pWidget)
		{
			gtk_box_append(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(m_pWidget))),GTK_WIDGET(m_pButtonClose));
		}
		if (m_pTreeViewExternalTools)
		{
			GtkTreeViewColumn*column;

			GtkCellRenderer* renderer = gtk_cell_renderer_pixbuf_new ();
			column = gtk_tree_view_column_new_with_attributes ("icon",
			  renderer,
			  "icon-name", COLUMN_ICON,
			NULL);
			gtk_tree_view_append_column (m_pTreeViewExternalTools, column);

			renderer = gtk_cell_renderer_text_new ();
			g_object_set (G_OBJECT (renderer),  "editable", TRUE,  NULL);
			g_signal_connect(renderer, "edited", (GCallback) cell_edited_callback, this);

			column = gtk_tree_view_column_new_with_attributes ("externaltool",
			  renderer,
			  "text",COLUMN_NAME,
			NULL);

			gtk_tree_view_append_column (m_pTreeViewExternalTools, column);

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
	GtkTreeModel *model;
	GtkTreeSelection* selection;
	int selection_count = 0;
	
	model = gtk_tree_view_get_model(m_pTreeViewExternalTools);
	selection = gtk_tree_view_get_selection(m_pTreeViewExternalTools);

	selection_count = gtk_tree_selection_count_selected_rows(selection);

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
		// enable edit,remove
		gtk_widget_set_sensitive(GTK_WIDGET(m_pButtonRemove),TRUE);
		gtk_widget_set_sensitive(GTK_WIDGET(m_pButtonEdit),TRUE);
		gtk_widget_set_sensitive(GTK_WIDGET(m_pButtonMoveUp),TRUE);
		gtk_widget_set_sensitive(GTK_WIDGET(m_pButtonMoveDown),TRUE);
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
		
		GtkListStore *store;
		store = gtk_list_store_new(COLUMN_COUNT, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING);
		vector<ExternalTool> externaltools = externaltoolsPtr->GetExternalTools();
		vector<ExternalTool>::iterator itr;

		for (itr = externaltools.begin(); externaltools.end() != itr; ++itr)
		{
			GtkTreeIter iter1 = {0};
			gtk_list_store_append (store, &iter1);
			gtk_list_store_set (store, &iter1,
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
	
	GtkTreeSelection* selection = gtk_tree_view_get_selection(priv->m_pTreeViewExternalTools);
	GtkTreeModel* model;
	GList* rows = gtk_tree_selection_get_selected_rows(selection, &model);
	GList* l;

	for (l = rows; l != NULL; l = l->next)
	{
		GtkTreePath* path = (GtkTreePath*)l->data;
		GtkTreeIter iter;
		gtk_tree_model_get_iter(gtk_tree_view_get_model(priv->m_pTreeViewExternalTools), &iter, path);
		int id;
		gtk_tree_model_get(gtk_tree_view_get_model(priv->m_pTreeViewExternalTools), &iter, COLUMN_ID, &id, -1);

		if (button == priv->m_pButtonMoveUp)
		{
			priv->m_ExternalToolsPtr->MoveUp(id);
		}
		else if (button == priv->m_pButtonMoveDown)
		{
			priv->m_ExternalToolsPtr->MoveDown(id);
		}
		else if (button == priv->m_pButtonRemove)
		{
			priv->m_ExternalToolsPtr->Remove(id);
		}
		else if (button == priv->m_pButtonEdit)
		{
			const ExternalTool* ext = priv->m_ExternalToolsPtr->GetExternalTool(id);
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
	g_list_free_full(rows, (GDestroyNotify)gtk_tree_path_free);

	if (button == priv->m_pButtonAdd)
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
	else if (button == priv->m_pButtonClose)
	{
		gtk_window_destroy(GTK_WINDOW(priv->m_pWidget));
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

	GtkTreePath *path = gtk_tree_path_new_from_string(path_string);
	GtkTreeIter iter;

	GtkTreeModel *pTreeModel = gtk_tree_view_get_model(priv->m_pTreeViewExternalTools);
	gtk_tree_model_get_iter(pTreeModel, &iter, path);

	int value;
	gtk_tree_model_get(pTreeModel, &iter, COLUMN_ID, &value, -1);

	const ExternalTool* ext = priv->m_ExternalToolsPtr->GetExternalTool(value);
	if (NULL != ext)
	{
		ExternalTool modified = *ext;
		modified.SetName(new_text);
		priv->m_ExternalToolsPtr->UpdateExternalTool(modified);
	}
	gtk_tree_path_free(path);
}

// nested class

void ExternalToolsDlg::ExternalToolsDlgPriv::ExternalToolsEventHandler::HandleExternalToolChanged(ExternalToolsEventPtr event)
{
	if (parent->m_bLoadedDlg)
	{
		parent->UpdateUI();
	}
}




