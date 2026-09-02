#include <config.h>

#include "ExternalToolAddEditDlg.h"
#include "QuiverStockIcons.h"

#include <list>
#include <vector>




using namespace std;

class ExternalToolAddEditDlg::ExternalToolAddEditDlgPriv
{
public:
// constructor, destructor
	ExternalToolAddEditDlgPriv(ExternalTool b, ExternalToolAddEditDlg *parent);
	~ExternalToolAddEditDlgPriv();
	
// methods
	void LoadWidgets();
	void UpdateUI();
	void ConnectSignals();

// variables
	ExternalToolAddEditDlg*     m_pExternalToolAddEditDlg;
	GtkBuilder*         m_pGtkBuilder;
	ExternalTool m_ExternalTool;
	bool m_bCancelled;

	bool m_bLoadedDlg;
	
	// dlg widgets
	GtkWidget*             m_pWidget;
	GtkEntry*              m_pEntryName;
	GtkEntry*              m_pEntryTooltip;
	GtkEntry*              m_pEntryCmd;
	GtkEntry*              m_pEntryIcon;
	GtkButton*             m_pButtonOk;
	GtkButton*             m_pButtonCancel;
	GtkCheckButton*        m_pToggleMultiple;
	GMainLoop*             m_pRunLoop;
	gint                   m_iRunResponse;
};


ExternalToolAddEditDlg::ExternalToolAddEditDlg() : m_PrivPtr(new ExternalToolAddEditDlg::ExternalToolAddEditDlgPriv(ExternalTool(),this))
{
}
ExternalToolAddEditDlg::ExternalToolAddEditDlg(ExternalTool b) : m_PrivPtr(new ExternalToolAddEditDlg::ExternalToolAddEditDlgPriv(b,this))
{
}


ExternalTool ExternalToolAddEditDlg::GetExternalTool() const
{
	  return m_PrivPtr->m_ExternalTool;
}

GtkWidget* ExternalToolAddEditDlg::GetWidget() const
{
	  return NULL;
}

void ExternalToolAddEditDlg::Run()
{
	if (m_PrivPtr->m_bLoadedDlg)
	{
		m_PrivPtr->m_pRunLoop = g_main_loop_new(NULL, FALSE);
		m_PrivPtr->m_iRunResponse = 0;
		gtk_window_set_modal(GTK_WINDOW(m_PrivPtr->m_pWidget), TRUE);
		gtk_widget_set_visible(m_PrivPtr->m_pWidget, TRUE);
		g_main_loop_run(m_PrivPtr->m_pRunLoop);
		g_main_loop_unref(m_PrivPtr->m_pRunLoop);
		m_PrivPtr->m_pRunLoop = NULL;

		if (GTK_RESPONSE_OK == m_PrivPtr->m_iRunResponse)
		{
			m_PrivPtr->m_bCancelled = false;

			m_PrivPtr->m_ExternalTool.SetName( gtk_editable_get_text(GTK_EDITABLE(m_PrivPtr->m_pEntryName)) );
			std::string tooltip = gtk_editable_get_text(GTK_EDITABLE(m_PrivPtr->m_pEntryTooltip));
			if (tooltip.empty())
			{
				tooltip = gtk_editable_get_text(GTK_EDITABLE(m_PrivPtr->m_pEntryName));
			}
			m_PrivPtr->m_ExternalTool.SetTooltip( tooltip );
			m_PrivPtr->m_ExternalTool.SetIcon( gtk_editable_get_text(GTK_EDITABLE(m_PrivPtr->m_pEntryIcon)) );
			m_PrivPtr->m_ExternalTool.SetCmd( gtk_editable_get_text(GTK_EDITABLE(m_PrivPtr->m_pEntryCmd)) );
			m_PrivPtr->m_ExternalTool.SetSupportsMultiple( gtk_check_button_get_active(m_PrivPtr->m_pToggleMultiple) ? true : false );
		}
		gtk_window_destroy(GTK_WINDOW(m_PrivPtr->m_pWidget));
	}
}

bool ExternalToolAddEditDlg::Cancelled() const
{
	return m_PrivPtr->m_bCancelled;
}
// private stuff


// prototypes
static void  on_clicked (GtkButton *button, gpointer user_data);
//static void  on_toggled (GtkToggleButton *button, gpointer user_data);

ExternalToolAddEditDlg::ExternalToolAddEditDlgPriv::ExternalToolAddEditDlgPriv(ExternalTool b, ExternalToolAddEditDlg *parent) :
        m_pExternalToolAddEditDlg(parent), m_ExternalTool(b)
{
	m_bLoadedDlg = false;
	m_bCancelled = true;


	m_pGtkBuilder = gtk_builder_new();
	const gchar* objectids[] = {
		"ExternalToolAddEditDialog",
		NULL};
	gtk_builder_add_objects_from_file (m_pGtkBuilder, QUIVER_DATADIR "/" "quiver.ui", objectids, NULL);
	LoadWidgets();
	UpdateUI();
	ConnectSignals();
}

ExternalToolAddEditDlg::ExternalToolAddEditDlgPriv::~ExternalToolAddEditDlgPriv()
{
	if (NULL != m_pGtkBuilder)
	{
		g_object_unref(m_pGtkBuilder);
		m_pGtkBuilder = NULL;
	}
}


void ExternalToolAddEditDlg::ExternalToolAddEditDlgPriv::LoadWidgets()
{

	if (NULL != m_pGtkBuilder)
	{
		m_pWidget                = GTK_WIDGET( gtk_builder_get_object (m_pGtkBuilder, "ExternalToolAddEditDialog"));

		m_pButtonCancel          = GTK_BUTTON( gtk_button_new_with_mnemonic("_Cancel") );
		m_pButtonOk              = GTK_BUTTON( gtk_button_new_with_mnemonic("_OK") );



		if (m_pWidget)
		{
			GtkHeaderBar* hbar = GTK_HEADER_BAR(gtk_header_bar_new());
			gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(hbar), TRUE);
			gtk_header_bar_pack_end(hbar, GTK_WIDGET(m_pButtonCancel));
			gtk_header_bar_pack_end(hbar, GTK_WIDGET(m_pButtonOk));
			gtk_window_set_titlebar(GTK_WINDOW(m_pWidget), GTK_WIDGET(hbar));
		}

		m_pToggleMultiple       = GTK_CHECK_BUTTON( gtk_builder_get_object(m_pGtkBuilder, "external_tools_edit_multiple"));
		m_pEntryName             = GTK_ENTRY        ( gtk_builder_get_object(m_pGtkBuilder, "external_tools_edit_name"));
		m_pEntryCmd             = GTK_ENTRY        ( gtk_builder_get_object(m_pGtkBuilder, "external_tools_edit_cmd"));
		m_pEntryTooltip          = GTK_ENTRY        ( gtk_builder_get_object(m_pGtkBuilder, "external_tools_edit_tooltip"));
		m_pEntryIcon             = GTK_ENTRY        ( gtk_builder_get_object(m_pGtkBuilder, "external_tools_edit_icon"));

		m_bLoadedDlg = (
				NULL != m_pWidget && 
				NULL != m_pButtonOk && 
				NULL != m_pButtonCancel && 
				NULL != m_pEntryName && 
				NULL != m_pEntryCmd && 
				NULL != m_pEntryTooltip && 
				NULL != m_pEntryIcon && 
				NULL != m_pToggleMultiple
				); 

	
		gtk_editable_set_text(GTK_EDITABLE(m_pEntryName), m_ExternalTool.GetName().c_str());
		gtk_editable_set_text(GTK_EDITABLE(m_pEntryCmd), m_ExternalTool.GetCmd().c_str());
		gtk_editable_set_text(GTK_EDITABLE(m_pEntryTooltip), m_ExternalTool.GetTooltip().c_str());
		gtk_editable_set_text(GTK_EDITABLE(m_pEntryIcon), m_ExternalTool.GetIcon().c_str());

		gtk_check_button_set_active(m_pToggleMultiple, m_ExternalTool.GetSupportsMultiple() ? TRUE : FALSE );

	}
}

void ExternalToolAddEditDlg::ExternalToolAddEditDlgPriv::UpdateUI()
{
	if (m_bLoadedDlg)
	{
		
	}	
}


void ExternalToolAddEditDlg::ExternalToolAddEditDlgPriv::ConnectSignals()
{
	if (m_bLoadedDlg)
	{
		g_signal_connect(m_pWidget, "close-request",
			G_CALLBACK(+[](GtkWidget* widget, gpointer user_data) -> gboolean {
				(void)widget;
				ExternalToolAddEditDlg::ExternalToolAddEditDlgPriv *priv =
					static_cast<ExternalToolAddEditDlg::ExternalToolAddEditDlgPriv*>(user_data);
				priv->m_iRunResponse = GTK_RESPONSE_CANCEL;
				if (NULL != priv->m_pRunLoop)
					g_main_loop_quit(priv->m_pRunLoop);
				return FALSE;
			}), this);

		/*
		g_signal_connect(m_pToggleMultiple,
			"toggled",(GCallback)on_toggled,this);

		*/
		g_signal_connect(m_pButtonOk,
			"clicked",(GCallback)on_clicked,this);
		g_signal_connect(m_pButtonCancel,
			"clicked",(GCallback)on_clicked,this);
	}
}

/*

static void  on_toggled (GtkToggleButton *togglebutton, gpointer user_data)
{
	ExternalToolAddEditDlg::ExternalToolAddEditDlgPriv *priv = static_cast<ExternalToolAddEditDlg::ExternalToolAddEditDlgPriv*>(user_data);
	if (priv->m_pToggleMultiple == togglebutton)
	{ 
		gboolean bMultiple = gtk_toggle_button_get_active(togglebutton);
	}
}
*/

static void  on_clicked (GtkButton *button, gpointer user_data)
{
	ExternalToolAddEditDlg::ExternalToolAddEditDlgPriv *priv = static_cast<ExternalToolAddEditDlg::ExternalToolAddEditDlgPriv*>(user_data);
	
	if (button == priv->m_pButtonOk)
	{
		priv->m_iRunResponse = GTK_RESPONSE_OK;
		if (NULL != priv->m_pRunLoop)
			g_main_loop_quit(priv->m_pRunLoop);
	}
	else if (button == priv->m_pButtonCancel)
	{
		priv->m_iRunResponse = GTK_RESPONSE_CANCEL;
		if (NULL != priv->m_pRunLoop)
			g_main_loop_quit(priv->m_pRunLoop);
	}
}

