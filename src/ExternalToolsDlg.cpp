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
	GtkWidget*             m_pTreeViewExternalTools;
	GListStore*            m_pListStoreExternalTools;
	GtkMultiSelection*     m_pSelectionExternalTools;
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


// row item type for the external tools column view
typedef struct {
	GObject  parent_instance;
	int      id;
	gchar*   icon;
	gchar*   name;
} ExternalToolItem;

typedef struct {
	GObjectClass parent_class;
} ExternalToolItemClass;

#define EXTERNALTOOL_ITEM_TYPE (externaltool_item_get_type())
#define EXTERNALTOOL_ITEM(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST((obj), EXTERNALTOOL_ITEM_TYPE, ExternalToolItem))

G_DEFINE_TYPE(ExternalToolItem, externaltool_item, G_TYPE_OBJECT)

static void externaltool_item_finalize (GObject* object)
{
	ExternalToolItem* item = EXTERNALTOOL_ITEM(object);
	g_free(item->icon);
	g_free(item->name);
	G_OBJECT_CLASS(g_type_class_peek_parent(
		G_OBJECT_GET_CLASS(object)))->finalize(object);
}

static void externaltool_item_class_init (ExternalToolItemClass* klass)
{
	G_OBJECT_CLASS(klass)->finalize = externaltool_item_finalize;
}

static void externaltool_item_init (ExternalToolItem* item)
{
	item->id = 0;
	item->icon = NULL;
	item->name = NULL;
}

static ExternalToolItem* externaltool_item_new (int id, const gchar* icon, const gchar* name)
{
	ExternalToolItem* item = static_cast<ExternalToolItem*>(
		g_object_new(EXTERNALTOOL_ITEM_TYPE, NULL));
	item->id = id;
	item->icon = g_strdup(icon);
	item->name = g_strdup(name);
	return item;
}

static void externaltool_icon_setup (GtkListItem* list_item, gpointer user_data)
{ (void)user_data;
	GtkWidget* image = gtk_image_new();
	gtk_image_set_icon_size(GTK_IMAGE(image), GTK_ICON_SIZE_NORMAL);
	gtk_widget_set_margin_start(image, 6);
	gtk_widget_set_margin_end(image, 6);
	gtk_list_item_set_child(list_item, image);
}

static void externaltool_icon_bind (GtkListItem* list_item, gpointer user_data)
{ (void)user_data;
	ExternalToolItem* item = EXTERNALTOOL_ITEM(gtk_list_item_get_item(list_item));
	GtkWidget* image = gtk_list_item_get_child(list_item);
	gtk_image_set_from_icon_name(GTK_IMAGE(image), item->icon);
}

static void externaltool_name_edited (GtkEditableLabel* editable, GParamSpec* pspec,
	ExternalToolsDlg::ExternalToolsDlgPriv* priv);
static void externaltool_name_setup (GtkListItem* list_item, gpointer user_data)
{
	ExternalToolsDlg::ExternalToolsDlgPriv* priv =
		static_cast<ExternalToolsDlg::ExternalToolsDlgPriv*>(user_data);
	GtkWidget* editable = gtk_editable_label_new(NULL);
	gtk_widget_set_hexpand(editable, TRUE);
	g_signal_connect(editable, "notify::text",
		G_CALLBACK(externaltool_name_edited), priv);
	gtk_list_item_set_child(list_item, editable);
}

static void externaltool_name_bind (GtkListItem* list_item, gpointer user_data)
{ (void)user_data;
	ExternalToolItem* item = EXTERNALTOOL_ITEM(gtk_list_item_get_item(list_item));
	GtkWidget* editable = gtk_list_item_get_child(list_item);
	g_object_set_data(G_OBJECT(editable), "externaltool-id",
		GINT_TO_POINTER(item->id));
	gtk_editable_set_text(GTK_EDITABLE(editable), item->name);
}

static void externaltool_name_edited (GtkEditableLabel* editable, GParamSpec* pspec,
	ExternalToolsDlg::ExternalToolsDlgPriv* priv)
{ (void)pspec;
	int id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(editable), "externaltool-id"));
	const ExternalTool* ext = priv->m_ExternalToolsPtr->GetExternalTool(id);
	if (NULL != ext)
	{
		ExternalTool modified = *ext;
		modified.SetName(gtk_editable_get_text(GTK_EDITABLE(editable)));
		priv->m_ExternalToolsPtr->UpdateExternalTool(modified);
	}
}

static GtkListItemFactory* externaltool_column_factory (int iCol,
	ExternalToolsDlg::ExternalToolsDlgPriv* priv)
{
	GtkListItemFactory* factory = gtk_signal_list_item_factory_new();
	if (COLUMN_ICON == iCol)
	{
		g_signal_connect(factory, "setup", G_CALLBACK(externaltool_icon_setup), NULL);
		g_signal_connect(factory, "bind", G_CALLBACK(externaltool_icon_bind), NULL);
	}
	else
	{
		g_signal_connect(factory, "setup", G_CALLBACK(externaltool_name_setup), priv);
		g_signal_connect(factory, "bind", G_CALLBACK(externaltool_name_bind), NULL);
	}
	return factory;
}


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
		GMainLoop *loop = g_main_loop_new(NULL, FALSE);
		g_object_set_data(G_OBJECT(m_PrivPtr->m_pWidget), "tools-loop", loop);
		gtk_window_set_modal(GTK_WINDOW(m_PrivPtr->m_pWidget), TRUE);
		gtk_widget_set_visible(m_PrivPtr->m_pWidget, TRUE);
		g_main_loop_run(loop);
		g_main_loop_unref(loop);
		gtk_window_destroy(GTK_WINDOW(m_PrivPtr->m_pWidget));
	}
}

// private stuff


// prototypes
static void  on_clicked (GtkButton *button, gpointer user_data);
static void selection_changed (GtkSelectionModel* selection, gpointer user_data);


ExternalToolsDlg::ExternalToolsDlgPriv::ExternalToolsDlgPriv(ExternalToolsDlg *parent) :
        m_pExternalToolsDlg(parent),
        m_ExternalToolsEventHandler( new ExternalToolsEventHandler(this) )
{
	m_ExternalToolsPtr = ExternalTools::GetInstance();
	m_ExternalToolsPtr->AddEventHandler(m_ExternalToolsEventHandler);
	m_bLoadedDlg = false;
	m_pListStoreExternalTools = NULL;
	m_pSelectionExternalTools = NULL;

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
	if (NULL != m_pSelectionExternalTools)
	{
		g_object_unref(m_pSelectionExternalTools);
		m_pSelectionExternalTools = NULL;
	}
	if (NULL != m_pListStoreExternalTools)
	{
		g_object_unref(m_pListStoreExternalTools);
		m_pListStoreExternalTools = NULL;
	}
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
		m_pTreeViewExternalTools     = GTK_WIDGET(     gtk_builder_get_object (m_pGtkBuilder, "externaltools_treeview") );

		m_pButtonClose           = GTK_BUTTON( gtk_button_new_with_mnemonic("_Close") );

		if (m_pWidget)
		{
			GtkHeaderBar* hbar = GTK_HEADER_BAR(gtk_header_bar_new());
			gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(hbar), TRUE);
			gtk_header_bar_pack_end(hbar, GTK_WIDGET(m_pButtonClose));
			gtk_window_set_titlebar(GTK_WINDOW(m_pWidget), GTK_WIDGET(hbar));
		}
		if (m_pTreeViewExternalTools)
		{
			m_pListStoreExternalTools = g_list_store_new(EXTERNALTOOL_ITEM_TYPE);
			m_pSelectionExternalTools = gtk_multi_selection_new(
				G_LIST_MODEL(m_pListStoreExternalTools));
			gtk_column_view_set_model(GTK_COLUMN_VIEW(m_pTreeViewExternalTools),
				GTK_SELECTION_MODEL(m_pSelectionExternalTools));

			GtkColumnViewColumn* column =
				gtk_column_view_column_new("icon",
					externaltool_column_factory(COLUMN_ICON, this));
			gtk_column_view_append_column(GTK_COLUMN_VIEW(m_pTreeViewExternalTools), column);

			column = gtk_column_view_column_new("externaltool",
				externaltool_column_factory(COLUMN_NAME, this));
			gtk_column_view_append_column(GTK_COLUMN_VIEW(m_pTreeViewExternalTools), column);
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
	GtkSelectionModel* sel = GTK_SELECTION_MODEL(m_pSelectionExternalTools);
	guint n = g_list_model_get_n_items(G_LIST_MODEL(m_pListStoreExternalTools));
	guint selection_count = 0;
	bool bTop = false, bBottom = false;

	if (n)
	{
		bBottom = gtk_selection_model_is_selected(sel, n - 1);
	}
	for (guint i = 0 ; i < n ; i++)
	{
		if (gtk_selection_model_is_selected(sel, i))
		{
			selection_count++;
			if (0 == i)
				bTop = true;
		}
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
		
		g_list_store_remove_all(m_pListStoreExternalTools);
		vector<ExternalTool> externaltools = externaltoolsPtr->GetExternalTools();
		vector<ExternalTool>::iterator itr;

		for (itr = externaltools.begin(); externaltools.end() != itr; ++itr)
		{
			ExternalToolItem* item = externaltool_item_new(
				itr->GetID(),
				itr->GetIcon().c_str(),
				itr->GetName().c_str());
			g_list_store_append(m_pListStoreExternalTools, G_OBJECT(item));
			g_object_unref(item);
		}

		SelectionChanged();

	}	

}


void ExternalToolsDlg::ExternalToolsDlgPriv::ConnectSignals()
{
	if (m_bLoadedDlg)
	{
		g_signal_connect(m_pWidget, "close-request",
			G_CALLBACK(+[](GtkWidget* widget, gpointer) -> gboolean {
				GMainLoop *loop = (GMainLoop*)g_object_get_data(G_OBJECT(widget), "tools-loop");
				if (loop)
					g_main_loop_quit(loop);
				return FALSE;
			}), NULL);

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

		g_signal_connect(G_OBJECT(m_pSelectionExternalTools),
			"selection-changed",G_CALLBACK(selection_changed),this);
	}
}


static void  on_clicked (GtkButton *button, gpointer user_data)
{
	ExternalToolsDlg::ExternalToolsDlgPriv *priv = static_cast<ExternalToolsDlg::ExternalToolsDlgPriv*>(user_data);
	ExternalToolsPtr externaltoolsPtr = priv->m_ExternalToolsPtr;
	
	list<int> values;

	GtkSelectionModel* sel = GTK_SELECTION_MODEL(priv->m_pSelectionExternalTools);
	guint n = g_list_model_get_n_items(G_LIST_MODEL(priv->m_pListStoreExternalTools));

	for (guint i = 0 ; i < n ; i++)
	{
		if (gtk_selection_model_is_selected(sel, i))
		{
			ExternalToolItem* item = EXTERNALTOOL_ITEM(
				g_list_model_get_item(G_LIST_MODEL(priv->m_pListStoreExternalTools), i));
			values.push_back(item->id);
			g_object_unref(item);
		}
	}
	// have to remove after iterating the model because
	// removing modifiees the model
	if (button == priv->m_pButtonMoveUp)
	{ 
		for (list<int>::iterator itr = values.begin(); values.end() != itr; ++itr)
		{
			priv->m_ExternalToolsPtr->MoveUp(*itr);
			// make sure the item is selected again
			// because the model has changed
			for (guint i = 0 ; i < n ; i++)
			{
				ExternalToolItem* item = EXTERNALTOOL_ITEM(
					g_list_model_get_item(G_LIST_MODEL(priv->m_pListStoreExternalTools), i));
				int id = item->id;
				g_object_unref(item);
				if (id == *itr)
				{
					gtk_selection_model_select_item(sel, i, FALSE);
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
			for (guint i = 0 ; i < n ; i++)
			{
				ExternalToolItem* item = EXTERNALTOOL_ITEM(
					g_list_model_get_item(G_LIST_MODEL(priv->m_pListStoreExternalTools), i));
				int id = item->id;
				g_object_unref(item);
				if (id == *itr)
				{
					gtk_selection_model_select_item(sel, i, FALSE);
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
		GMainLoop *loop = (GMainLoop*)g_object_get_data(G_OBJECT(priv->m_pWidget), "tools-loop");
		if (loop)
			g_main_loop_quit(loop);
	}
}

static void selection_changed (GtkSelectionModel* selection, gpointer user_data)
{ (void)selection; 
	ExternalToolsDlg::ExternalToolsDlgPriv *priv = static_cast<ExternalToolsDlg::ExternalToolsDlgPriv*>(user_data);
	priv->SelectionChanged();
}


// nested class

void ExternalToolsDlg::ExternalToolsDlgPriv::ExternalToolsEventHandler::HandleExternalToolChanged(ExternalToolsEventPtr event)
{ (void)event; 
	if (parent->m_bLoadedDlg)
	{
		parent->UpdateUI();
	}
}


