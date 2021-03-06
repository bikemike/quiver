#include <config.h>

#include "ExternalToolAddEditDlg.h"
#include "QuiverStockIcons.h"

#ifdef HAVE_LIBGLADE
#include <glade/glade.h>
#endif

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
#ifdef HAVE_LIBGLADE
	GladeXML*         m_pGladeXML;
#endif
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
#ifdef HAVE_LIBGLADE
	if (m_PrivPtr->m_bLoadedDlg)
	{
		gint result = gtk_dialog_run(GTK_DIALOG(m_PrivPtr->m_pWidget));
		if (GTK_RESPONSE_OK == result)
		{
			m_PrivPtr->m_bCancelled = false;

			m_PrivPtr->m_ExternalTool.SetName( gtk_entry_get_text(m_PrivPtr->m_pEntryName) );
			std::string tooltip = gtk_entry_get_text(m_PrivPtr->m_pEntryTooltip);
			if (tooltip.empty())
			{
				tooltip = gtk_entry_get_text(m_PrivPtr->m_pEntryName);
			}
			m_PrivPtr->m_ExternalTool.SetTooltip( tooltip );
			m_PrivPtr->m_ExternalTool.SetIcon( gtk_entry_get_text(m_PrivPtr->m_pEntryIcon) );
			m_PrivPtr->m_ExternalTool.SetCmd( gtk_entry_get_text(m_PrivPtr->m_pEntryCmd) );
			m_PrivPtr->m_ExternalTool.SetSupportsMultiple( gtk_toggle_button_get_active(m_PrivPtr->m_pToggleMultiple) ? true : false );
		}
		gtk_widget_destroy(m_PrivPtr->m_pWidget);
	}
#endif
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


#ifdef HAVE_LIBGLADE
	m_pGladeXML = glade_xml_new (QUIVER_GLADEDIR "/" "quiver.glade", "ExternalToolAddEditDialog", NULL);
	if (NULL != m_pGladeXML)
	{
		LoadWidgets();
		UpdateUI();
		ConnectSignals();
	}
#endif
}

ExternalToolAddEditDlg::ExternalToolAddEditDlgPriv::~ExternalToolAddEditDlgPriv()
{
#ifdef HAVE_LIBGLADE
	if (NULL != m_pGladeXML)
	{
		g_object_unref(m_pGladeXML);
		m_pGladeXML = NULL;
	}
#endif
}


void ExternalToolAddEditDlg::ExternalToolAddEditDlgPriv::LoadWidgets()
{

#ifdef HAVE_LIBGLADE
	if (NULL != m_pGladeXML)
	{
		m_pWidget                = glade_xml_get_widget (m_pGladeXML, "ExternalToolAddEditDialog");

		m_pButtonCancel          = GTK_BUTTON( gtk_button_new_from_stock(QUIVER_STOCK_CANCEL) );
		m_pButtonOk              = GTK_BUTTON( gtk_button_new_from_stock(QUIVER_STOCK_OK) );


		gtk_widget_show(GTK_WIDGET(m_pButtonCancel));
		gtk_widget_show(GTK_WIDGET(m_pButtonOk));

		if (m_pWidget)
		{
			gtk_box_pack_start(GTK_BOX(GTK_DIALOG(m_pWidget)->action_area),GTK_WIDGET(m_pButtonCancel),FALSE,TRUE,5);
			gtk_box_pack_start(GTK_BOX(GTK_DIALOG(m_pWidget)->action_area),GTK_WIDGET(m_pButtonOk),FALSE,TRUE,5);
		}

		m_pToggleMultiple       = GTK_TOGGLE_BUTTON( glade_xml_get_widget(m_pGladeXML, "external_tools_edit_multiple"));
		m_pEntryName             = GTK_ENTRY        ( glade_xml_get_widget(m_pGladeXML, "external_tools_edit_name"));
		m_pEntryCmd             = GTK_ENTRY        ( glade_xml_get_widget(m_pGladeXML, "external_tools_edit_cmd"));
		m_pEntryTooltip          = GTK_ENTRY        ( glade_xml_get_widget(m_pGladeXML, "external_tools_edit_tooltip"));
		m_pEntryIcon             = GTK_ENTRY        ( glade_xml_get_widget(m_pGladeXML, "external_tools_edit_icon"));

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

	
		gtk_entry_set_text(m_pEntryName, m_ExternalTool.GetName().c_str());
		gtk_entry_set_text(m_pEntryCmd, m_ExternalTool.GetCmd().c_str());
		gtk_entry_set_text(m_pEntryTooltip, m_ExternalTool.GetTooltip().c_str());
		gtk_entry_set_text(m_pEntryIcon, m_ExternalTool.GetIcon().c_str());

		gtk_toggle_button_set_active(m_pToggleMultiple, m_ExternalTool.GetSupportsMultiple() ? TRUE : FALSE );

	}
#endif
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
	
	list<int> values;

	if (button == priv->m_pButtonOk)
	{
		gtk_dialog_response(GTK_DIALOG(priv->m_pWidget), GTK_RESPONSE_OK);
	}
	else if (button == priv->m_pButtonCancel)
	{
		gtk_dialog_response(GTK_DIALOG(priv->m_pWidget), GTK_RESPONSE_CANCEL);
	}
}

