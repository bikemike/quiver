#include <config.h>

#include "DonateDlg.h"
#include "QuiverStockIcons.h"

#ifdef QUIVER_MAEMO
#include <libosso.h>
#ifdef HAVE_HILDON_MIME
#include <hildon-mime.h>
#else
#include <osso-mime.h>
#endif
extern osso_context_t* osso_context;
#endif

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
	gchar* objectids[] = {
		"DonateDialog",
		NULL};
	gtk_builder_add_objects_from_file (m_pGtkBuilder, QUIVER_DATADIR "/" "quiver.ui", objectids, NULL);
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
		m_pButtonClose              = GTK_BUTTON( gtk_button_new_from_stock(QUIVER_STOCK_CLOSE) );


		gtk_widget_show(GTK_WIDGET(m_pButtonDonate));
		gtk_widget_show(GTK_WIDGET(m_pButtonClose));

		if (m_pWidget)
		{
			gtk_box_pack_start(GTK_BOX(gtk_dialog_get_action_area(GTK_DIALOG(m_pWidget))),GTK_WIDGET(m_pButtonDonate),TRUE,TRUE,5);
			gtk_box_pack_start(GTK_BOX(gtk_dialog_get_action_area(GTK_DIALOG(m_pWidget))),GTK_WIDGET(m_pButtonClose),FALSE,TRUE,5);
			gtk_button_box_set_layout  (GTK_BUTTON_BOX(gtk_dialog_get_action_area(GTK_DIALOG(m_pWidget))), GTK_BUTTONBOX_EDGE);
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


void DonateDlg::DonateDlgPriv::ConnectSignals()
{
	if (m_bLoadedDlg)
	{
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
#ifdef QUIVER_MAEMO
		DBusConnection *con = (DBusConnection*)osso_get_dbus_connection(osso_context);
#ifdef HAVE_HILDON_MIME
		hildon_mime_open_file_with_mime_type (con,
			DONATION_URL,
			DONATION_URL_MIME);
#else
		osso_mime_open_file_with_mime_type (con,
			DONATION_URL,
			DONATION_URL_MIME);
#endif
#else
	gchar* contentType =
			g_content_type_from_mime_type("text/html");
	gboolean launched = g_app_info_launch_default_for_uri(DONATION_URL, NULL, NULL);
	g_free(contentType);

#endif
	}
}


