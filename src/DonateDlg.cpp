#include <config.h>

#include "DonateDlg.h"
#include "QuiverStockIcons.h"



#define DONATION_URL_MIME "text/html"
#define DONATION_URL "http://mike.yi.org/donations/quiver/redirect/"

using namespace std;

class DonateDlg::DonateDlgPriv
{
public:
// constructor, destructor
	DonateDlgPriv(DonateDlg *parent);
	~DonateDlgPriv();
	
// methods
	void LoadWidgets();
	void UpdateUI();
	void ConnectSignals();

// variables
	DonateDlg*     m_pDonateDlg;
	GtkBuilder*         m_pGtkBuilder;
	bool m_bLoadedDlg;
	GtkWidget*             m_pWidget;
	GtkButton*             m_pButtonClose;
	GtkButton*             m_pButtonDonate;
};


DonateDlg::DonateDlg() : m_PrivPtr(new DonateDlg::DonateDlgPriv(this))
{
}


GtkWidget* DonateDlg::GetWidget() const
{
	  return NULL;
}


void DonateDlg::Run()
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


DonateDlg::DonateDlgPriv::DonateDlgPriv(DonateDlg *parent) :
        m_pDonateDlg(parent)
{
	m_bLoadedDlg = false;

	m_pGtkBuilder = gtk_builder_new();
	const gchar* objectids[] = {
		"DonateDialog",
		NULL};
	gtk_builder_add_objects_from_file (m_pGtkBuilder, QUIVER_DATADIR "/" "quiver.ui", (gchar**)objectids, NULL);
	LoadWidgets();
	UpdateUI();
	ConnectSignals();
}

DonateDlg::DonateDlgPriv::~DonateDlgPriv()
{
	if (NULL != m_pGtkBuilder)
	{
		g_object_unref(m_pGtkBuilder);
		m_pGtkBuilder = NULL;
	}
}


void DonateDlg::DonateDlgPriv::LoadWidgets()
{

	if (NULL != m_pGtkBuilder)
	{
		m_pWidget                = GTK_WIDGET(gtk_builder_get_object (m_pGtkBuilder, "DonateDialog"));

		m_pButtonDonate          = GTK_BUTTON( gtk_button_new_with_label("Donate to Quiver"));
		m_pButtonClose              = GTK_BUTTON( gtk_button_new_with_mnemonic("_Close") );
		gtk_button_set_image(GTK_BUTTON(m_pButtonClose), gtk_image_new_from_icon_name(GTK_STOCK_CLOSE, GTK_ICON_SIZE_BUTTON));


		gtk_widget_show(GTK_WIDGET(m_pButtonDonate));
		gtk_widget_show(GTK_WIDGET(m_pButtonClose));

		if (m_pWidget)
		{
			gtk_dialog_add_action_widget(GTK_DIALOG(m_pWidget),GTK_WIDGET(m_pButtonDonate),GTK_RESPONSE_NONE);
			gtk_dialog_add_action_widget(GTK_DIALOG(m_pWidget),GTK_WIDGET(m_pButtonClose),GTK_RESPONSE_NONE);
			gtk_button_box_set_layout  (GTK_BUTTON_BOX(gtk_builder_get_object(m_pGtkBuilder, "dialog-action_area7")), GTK_BUTTONBOX_EDGE);
		}

		m_bLoadedDlg = (
				NULL != m_pWidget && 
				NULL != m_pButtonClose && 
				NULL != m_pButtonDonate 
				); 
	}
}

void DonateDlg::DonateDlgPriv::UpdateUI()
{
	if (m_bLoadedDlg)
	{
	}	
}


static void on_dialog_response (GtkDialog *dlg, gint response, gpointer data)
{ (void)data; 
	if (GTK_RESPONSE_NONE == response)
	{
		g_signal_stop_emission_by_name (dlg, "response");
	}
}

void DonateDlg::DonateDlgPriv::ConnectSignals()
{
	if (m_bLoadedDlg)
	{
		g_signal_connect(m_pWidget,
			"response",(GCallback)on_dialog_response,this);

		g_signal_connect(m_pButtonClose,
			"clicked",(GCallback)on_clicked,this);
		g_signal_connect(m_pButtonDonate,
			"clicked",(GCallback)on_clicked,this);

	}
}


static void  on_clicked (GtkButton *button, gpointer user_data)
{
	DonateDlg::DonateDlgPriv *priv = static_cast<DonateDlg::DonateDlgPriv*>(user_data);
	
	if (button == priv->m_pButtonClose)
	{
		gtk_dialog_response(GTK_DIALOG(priv->m_pWidget), GTK_RESPONSE_CLOSE);
	}
	else if (button == priv->m_pButtonDonate)
	{
	gchar* contentType =
			g_content_type_from_mime_type("text/html");
	gboolean launched = g_app_info_launch_default_for_uri(DONATION_URL, NULL, NULL);
 (void)launched;
	g_free(contentType);

	}
}


