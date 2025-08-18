#include <config.h>

#include "ExternalToolAddEditDlg.h"
#include "QuiverStockIcons.h"

#include <list>
#include <vector>

#ifdef QUIVER_MAEMO
#ifdef HAVE_HILDON_FM_2
#include <hildon/hildon-file-chooser-dialog.h>
#else
#include <hildon-widgets/hildon-file-chooser-dialog.h>
#endif
#endif


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
	GtkToggleButton*       m_pToggleMultiple;
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
		gtk_window_present(GTK_WINDOW(m_PrivPtr->m_pWidget));
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


	m_pGtkBuilder = gtk_builder_new_from_file(QUIVER_DATADIR "/" "quiver.ui");
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

		m_pButtonCancel          = (GtkButton*)gtk_button_new_from_icon_name("window-close");
		m_pButtonOk              = (GtkButton*)gtk_button_new_from_icon_name("dialog-ok");


		gtk_widget_set_visible(GTK_WIDGET(m_pButtonCancel), true);
		gtk_widget_set_visible(GTK_WIDGET(m_pButtonOk), true);

		if (m_pWidget)
		{
			gtk_box_append(GTK_BOX(gtk_window_get_child(GTK_WINDOW(m_pWidget))),GTK_WIDGET(m_pButtonCancel));
			gtk_box_append(GTK_BOX(gtk_window_get_child(GTK_WINDOW(m_pWidget))),GTK_WIDGET(m_pButtonOk));
		}

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
		gtk_window_destroy(GTK_WINDOW(priv->m_pWidget));
	}
	else if (button == priv->m_pButtonCancel)
	{
		gtk_window_destroy(GTK_WINDOW(priv->m_pWidget));
	}
}
