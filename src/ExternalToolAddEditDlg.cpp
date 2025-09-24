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
	ExternalToolAddEditDlgPriv(GtkWindow* pParent, ExternalTool b, ExternalToolAddEditDlg *parent);
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
    GtkWindow* m_pParent;
    GMainLoop* m_loop = nullptr;

	bool m_bLoadedDlg;
	
	// dlg widgets
	GtkWidget*             m_pWidget;
	GtkEntry*              m_pEntryName;
	GtkEntry*              m_pEntryTooltip;
	GtkEntry*              m_pEntryCmd;
	GtkEntry*              m_pEntryIcon;
	GtkButton*             m_pButtonOk;
	GtkButton*             m_pButtonCancel;
	GtkToggleButton*       m_pToggleMultiple;
};


ExternalToolAddEditDlg::ExternalToolAddEditDlg(GtkWindow* parent) : m_PrivPtr(new ExternalToolAddEditDlg::ExternalToolAddEditDlgPriv(parent, ExternalTool(),this))
{
}
ExternalToolAddEditDlg::ExternalToolAddEditDlg(GtkWindow* parent, ExternalTool b) : m_PrivPtr(new ExternalToolAddEditDlg::ExternalToolAddEditDlgPriv(parent, b,this))
{
}

ExternalToolAddEditDlg::~ExternalToolAddEditDlg()
{
}

ExternalTool ExternalToolAddEditDlg::GetExternalTool() const
{
	  return m_PrivPtr->m_ExternalTool;
}

GtkWidget* ExternalToolAddEditDlg::GetWidget() const
{
	  return m_PrivPtr->m_pWidget;
}


void ExternalToolAddEditDlg::Run()
{
	if (m_PrivPtr->m_bLoadedDlg)
	{
		gtk_widget_set_visible(m_PrivPtr->m_pWidget, TRUE);
        m_PrivPtr->m_loop = g_main_loop_new(NULL, FALSE);
        g_main_loop_run(m_PrivPtr->m_loop);
        g_main_loop_unref(m_PrivPtr->m_loop);
        m_PrivPtr->m_loop = nullptr;
	}
}

bool ExternalToolAddEditDlg::Cancelled() const
{
	return m_PrivPtr->m_bCancelled;
}

// private stuff


// prototypes
static void  on_clicked (GtkButton *button, gpointer user_data);


ExternalToolAddEditDlg::ExternalToolAddEditDlgPriv::ExternalToolAddEditDlgPriv(GtkWindow* pParent, ExternalTool b, ExternalToolAddEditDlg *parent) :
        m_pExternalToolAddEditDlg(parent), m_ExternalTool(b), m_pParent(pParent)
{
	m_bLoadedDlg = false;
	m_bCancelled = true;


	m_pGtkBuilder = gtk_builder_new();
	const gchar* objectids[] = {
		"ExternalToolAddEditDialog",
		NULL};
	gtk_builder_add_objects_from_file (m_pGtkBuilder, QUIVER_DATADIR "/" "quiver.ui", (const char**)objectids, NULL);
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
		m_pWidget = GTK_WIDGET( gtk_builder_get_object (m_pGtkBuilder, "ExternalToolAddEditDialog"));
        gtk_window_set_transient_for(GTK_WINDOW(m_pWidget), m_pParent);

        m_pButtonCancel = GTK_BUTTON(gtk_dialog_add_button(GTK_DIALOG(m_pWidget), "_Cancel", GTK_RESPONSE_CANCEL));
        m_pButtonOk = GTK_BUTTON(gtk_dialog_add_button(GTK_DIALOG(m_pWidget), "_OK", GTK_RESPONSE_OK));

		m_pToggleMultiple       = GTK_TOGGLE_BUTTON( gtk_builder_get_object(m_pGtkBuilder, "external_tools_edit_multiple"));
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

		gtk_toggle_button_set_active(m_pToggleMultiple, m_ExternalTool.GetSupportsMultiple() ? TRUE : FALSE );

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
		g_signal_connect(m_pButtonOk, "clicked",(GCallback)on_clicked,this);
		g_signal_connect(m_pButtonCancel, "clicked",(GCallback)on_clicked,this);
	}
}

static void  on_clicked (GtkButton *button, gpointer user_data)
{
	ExternalToolAddEditDlg::ExternalToolAddEditDlgPriv *priv = static_cast<ExternalToolAddEditDlg::ExternalToolAddEditDlgPriv*>(user_data);
	
	if (button == priv->m_pButtonOk)
	{
		priv->m_bCancelled = false;

		priv->m_ExternalTool.SetName( gtk_editable_get_text(GTK_EDITABLE(priv->m_pEntryName)) );
		std::string tooltip = gtk_editable_get_text(GTK_EDITABLE(priv->m_pEntryTooltip));
		if (tooltip.empty())
		{
			tooltip = gtk_editable_get_text(GTK_EDITABLE(priv->m_pEntryName));
		}
		priv->m_ExternalTool.SetTooltip( tooltip );
		priv->m_ExternalTool.SetIcon( gtk_editable_get_text(GTK_EDITABLE(priv->m_pEntryIcon)) );
		priv->m_ExternalTool.SetCmd( gtk_editable_get_text(GTK_EDITABLE(priv->m_pEntryCmd)) );
		priv->m_ExternalTool.SetSupportsMultiple( gtk_toggle_button_get_active(priv->m_pToggleMultiple) ? true : false );
	}

    if (priv->m_loop) {
        g_main_loop_quit(priv->m_loop);
    }
	gtk_window_destroy(GTK_WINDOW(priv->m_pWidget));
}
