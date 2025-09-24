#include <config.h>
#include "ExternalToolsDlg.h"
#include <list>
#include <vector>
#include "ExternalTools.h"
#include "IExternalToolsEventHandler.h"
#include "ExternalToolAddEditDlg.h"
#include "QuiverStockIcons.h"
#include "QuiverExternalTool.h"
#include <gtk/gtk.h>

using namespace std;

class ExternalToolsDlg::ExternalToolsDlgPriv
{
public:
	ExternalToolsDlgPriv(ExternalToolsDlg *parent, GtkWindow* pParent);
	~ExternalToolsDlgPriv();
	
	void LoadWidgets();
	void UpdateUI();
	void SelectionChanged();
	void ConnectSignals();

	ExternalToolsDlg*     m_pExternalToolsDlg;
	GtkBuilder*         m_pGtkBuilder;
	ExternalToolsPtr      m_ExternalToolsPtr;
    GtkWindow*          m_pParent;

	bool m_bLoadedDlg;
	
	GtkWidget*             m_pWidget;
	GtkColumnView*         m_pColumnViewExternalTools;
	GtkButton*             m_pButtonMoveUp;
	GtkButton*             m_pButtonMoveDown;
	GtkButton*             m_pButtonAdd;
	GtkButton*             m_pButtonEdit;
	GtkButton*             m_pButtonRemove;
	GtkButton*             m_pButtonClose;

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

static void on_clicked (GtkButton *button, gpointer user_data);
static void selection_changed (GObject* selection, GParamSpec* pspec, gpointer user_data);
static void setup_icon_factory(GtkListItemFactory* factory, GtkListItem* list_item);
static void setup_name_factory(GtkListItemFactory* factory, GtkListItem* list_item);
static void bind_icon_factory(GtkListItemFactory* factory, GtkListItem* list_item);
static void bind_name_factory(GtkListItemFactory* factory, GtkListItem* list_item);


ExternalToolsDlg::ExternalToolsDlg(GtkWindow* parent) : m_PrivPtr(new ExternalToolsDlg::ExternalToolsDlgPriv(this, parent))
{
}

ExternalToolsDlg::~ExternalToolsDlg()
{
}

GtkWidget* ExternalToolsDlg::GetWidget()
{
	  return m_PrivPtr->m_pWidget;
}

void ExternalToolsDlg::Run()
{
	if (m_PrivPtr->m_bLoadedDlg)
	{
		gtk_widget_set_visible(m_PrivPtr->m_pWidget, TRUE);
	}
}

ExternalToolsDlg::ExternalToolsDlgPriv::ExternalToolsDlgPriv(ExternalToolsDlg *parent, GtkWindow* pParent) :
        m_pExternalToolsDlg(parent),
        m_pParent(pParent),
        m_ExternalToolsEventHandler( new ExternalToolsEventHandler(this) )
{
	m_ExternalToolsPtr = ExternalTools::GetInstance();
	m_ExternalToolsPtr->AddEventHandler(m_ExternalToolsEventHandler);
	m_bLoadedDlg = false;

	m_pGtkBuilder = gtk_builder_new();
	const gchar* objectids[] = {
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
		m_pWidget = GTK_WIDGET(gtk_builder_get_object (m_pGtkBuilder, "ExternalToolsDialog"));
        gtk_window_set_transient_for(GTK_WINDOW(m_pWidget), m_pParent);
		m_pColumnViewExternalTools = GTK_COLUMN_VIEW(gtk_builder_get_object (m_pGtkBuilder, "externaltools_treeview") );

		m_pButtonClose = GTK_BUTTON( gtk_button_new_from_icon_name("window-close-symbolic") );
        gtk_dialog_add_button(GTK_DIALOG(m_pWidget), "Close", GTK_RESPONSE_CLOSE);


		if (m_pColumnViewExternalTools)
		{
            GListModel* columns = gtk_column_view_get_columns(m_pColumnViewExternalTools);
            GtkColumnViewColumn* icon_column = GTK_COLUMN_VIEW_COLUMN(g_list_model_get_item(columns, 0));
            GtkColumnViewColumn* name_column = GTK_COLUMN_VIEW_COLUMN(g_list_model_get_item(columns, 1));

            GtkListItemFactory* icon_factory = gtk_signal_list_item_factory_new();
            g_signal_connect(icon_factory, "setup", G_CALLBACK(setup_icon_factory), NULL);
            g_signal_connect(icon_factory, "bind", G_CALLBACK(bind_icon_factory), NULL);
            gtk_column_view_column_set_factory(icon_column, icon_factory);

            GtkListItemFactory* name_factory = gtk_signal_list_item_factory_new();
            g_signal_connect(name_factory, "setup", G_CALLBACK(setup_name_factory), NULL);
            g_signal_connect(name_factory, "bind", G_CALLBACK(bind_name_factory), NULL);
            gtk_column_view_column_set_factory(name_column, name_factory);

            g_object_unref(icon_column);
            g_object_unref(name_column);
            g_object_unref(columns);
		}

		m_pButtonMoveUp          = GTK_BUTTON(     gtk_builder_get_object (m_pGtkBuilder, "externaltools_button_move_up") );
		m_pButtonMoveDown        = GTK_BUTTON(     gtk_builder_get_object (m_pGtkBuilder, "externaltools_button_move_down") );
		m_pButtonAdd             = GTK_BUTTON(     gtk_builder_get_object (m_pGtkBuilder, "externaltools_button_add") );
		m_pButtonEdit            = GTK_BUTTON(     gtk_builder_get_object (m_pGtkBuilder, "externaltools_button_edit") );
		m_pButtonRemove          = GTK_BUTTON(     gtk_builder_get_object (m_pGtkBuilder, "externaltools_button_remove") );

		m_bLoadedDlg = (
				NULL != m_pWidget && 
				NULL != m_pColumnViewExternalTools &&
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
    GtkSelectionModel* selection = gtk_column_view_get_model(m_pColumnViewExternalTools);
    if (!selection) return;

    GtkBitset* selected_bitset = gtk_selection_model_get_selection(selection);
    guint selection_count = gtk_bitset_get_size(selected_bitset);
    gtk_bitset_unref(selected_bitset);

	if (0 == selection_count)
	{
		gtk_widget_set_sensitive(GTK_WIDGET(m_pButtonRemove),FALSE);
		gtk_widget_set_sensitive(GTK_WIDGET(m_pButtonEdit),FALSE);
		gtk_widget_set_sensitive(GTK_WIDGET(m_pButtonMoveUp),FALSE);
		gtk_widget_set_sensitive(GTK_WIDGET(m_pButtonMoveDown),FALSE);
	}
	else if (1 == selection_count)
	{
		gtk_widget_set_sensitive(GTK_WIDGET(m_pButtonMoveUp),TRUE);
		gtk_widget_set_sensitive(GTK_WIDGET(m_pButtonMoveDown),TRUE);
		gtk_widget_set_sensitive(GTK_WIDGET(m_pButtonRemove),TRUE);
		gtk_widget_set_sensitive(GTK_WIDGET(m_pButtonEdit),TRUE);
	}
	else
	{
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
		
        GListStore* store = g_list_store_new(QUIVER_TYPE_EXTERNAL_TOOL);
		vector<ExternalTool> externaltools = externaltoolsPtr->GetExternalTools();
		for (const auto& tool : externaltools)
		{
            g_list_store_append(store, quiver_external_tool_new(tool));
		}

        GtkMultiSelection* selection = gtk_multi_selection_new(G_LIST_MODEL(store));
		gtk_column_view_set_model(m_pColumnViewExternalTools, GTK_SELECTION_MODEL(selection));
		g_object_unref(store);

		SelectionChanged();
	}	
}

void ExternalToolsDlg::ExternalToolsDlgPriv::ConnectSignals()
{
	if (m_bLoadedDlg)
	{
		g_signal_connect(m_pButtonMoveUp, "clicked",(GCallback)on_clicked,this);
		g_signal_connect(m_pButtonMoveDown, "clicked",(GCallback)on_clicked,this);
		g_signal_connect(m_pButtonAdd, "clicked",(GCallback)on_clicked,this);
		g_signal_connect(m_pButtonEdit, "clicked",(GCallback)on_clicked,this);
		g_signal_connect(m_pButtonRemove, "clicked",(GCallback)on_clicked,this);
		g_signal_connect(m_pButtonClose, "clicked",(GCallback)on_clicked,this);

        GtkSelectionModel* selection = gtk_column_view_get_model(m_pColumnViewExternalTools);
		g_signal_connect(G_OBJECT(selection), "selection-changed",G_CALLBACK(selection_changed),this);
	}
}

static void on_clicked (GtkButton *button, gpointer user_data)
{
	ExternalToolsDlg::ExternalToolsDlgPriv *priv = static_cast<ExternalToolsDlg::ExternalToolsDlgPriv*>(user_data);
	
    GtkSelectionModel* selection_model = gtk_column_view_get_model(priv->m_pColumnViewExternalTools);
    if (!selection_model) return;

    GtkBitset* selected_bitset = gtk_selection_model_get_selection(selection_model);

    list<int> values;
    GtkBitsetIter iter;
    guint position;
    if (gtk_bitset_iter_init_first(&iter, selected_bitset, &position))
    {
        do {
            QuiverExternalTool* tool_obj = (QuiverExternalTool*)g_list_model_get_item(G_LIST_MODEL(selection_model), position);
            const ExternalTool& tool = quiver_external_tool_get_tool(tool_obj);
            values.push_back(tool.GetID());
            g_object_unref(tool_obj);
        } while (gtk_bitset_iter_next(&iter, &position));
    }
    gtk_bitset_unref(selected_bitset);

	if (button == priv->m_pButtonMoveUp)
	{
		for (int id : values)
		{
			priv->m_ExternalToolsPtr->MoveUp(id);
		}
	}
	else if (button == priv->m_pButtonMoveDown)
	{
		for (int id : values)
		{
			priv->m_ExternalToolsPtr->MoveDown(id);
		}
	}
	else if (button == priv->m_pButtonAdd)
	{
		ExternalToolAddEditDlg dlg(priv->m_pParent);
		dlg.Run();
	}
	else if (button == priv->m_pButtonEdit)
	{
		for (int id : values)
		{
			const ExternalTool* ext = priv->m_ExternalToolsPtr->GetExternalTool(id);
			if (NULL != ext)
			{
				ExternalToolAddEditDlg dlg(priv->m_pParent, *ext);
				dlg.Run();
			}
		}
	}
	else if (button == priv->m_pButtonRemove)
	{
		for (int id : values)
		{
			priv->m_ExternalToolsPtr->Remove(id);
		}
	}
	else if (button == priv->m_pButtonClose)
	{
		gtk_window_destroy(GTK_WINDOW(priv->m_pWidget));
	}
}

static void selection_changed (GObject* selection, GParamSpec* pspec, gpointer user_data)
{
	ExternalToolsDlg::ExternalToolsDlgPriv *priv = static_cast<ExternalToolsDlg::ExternalToolsDlgPriv*>(user_data);
	priv->SelectionChanged();
}

static void setup_icon_factory(GtkListItemFactory* factory, GtkListItem* list_item)
{
    GtkImage* image = GTK_IMAGE(gtk_image_new());
    gtk_list_item_set_child(list_item, GTK_WIDGET(image));
}

static void setup_name_factory(GtkListItemFactory* factory, GtkListItem* list_item)
{
    GtkLabel* label = GTK_LABEL(gtk_label_new(""));
    gtk_list_item_set_child(list_item, GTK_WIDGET(label));
}

static void bind_icon_factory(GtkListItemFactory* factory, GtkListItem* list_item)
{
    GtkImage* image = GTK_IMAGE(gtk_list_item_get_child(list_item));
    QuiverExternalTool* tool = (QuiverExternalTool*)gtk_list_item_get_item(list_item);
    if (tool) {
        const gchar* icon_name = quiver_external_tool_get_tool(tool).GetIcon().c_str();
        gtk_image_set_from_icon_name(image, icon_name);
    }
}

static void bind_name_factory(GtkListItemFactory* factory, GtkListItem* list_item)
{
    GtkLabel* label = GTK_LABEL(gtk_list_item_get_child(list_item));
    QuiverExternalTool* tool = (QuiverExternalTool*)gtk_list_item_get_item(list_item);
    if (tool) {
        const gchar* name = quiver_external_tool_get_tool(tool).GetName().c_str();
        gtk_label_set_text(label, name);
    }
}

void ExternalToolsDlg::ExternalToolsDlgPriv::ExternalToolsEventHandler::HandleExternalToolChanged(ExternalToolsEventPtr event)
{
	if (parent->m_bLoadedDlg)
	{
		parent->UpdateUI();
	}
}
