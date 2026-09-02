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
		GMainLoop *loop = g_main_loop_new(NULL, FALSE);
		g_object_set_data(G_OBJECT(m_PrivPtr->m_pWidget), "donate-loop", loop);
		gtk_window_set_modal(GTK_WINDOW(m_PrivPtr->m_pWidget), TRUE);
		gtk_window_present(GTK_WINDOW(m_PrivPtr->m_pWidget));
		g_main_loop_run(loop);
		g_main_loop_unref(loop);
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
	gtk_builder_add_objects_from_file (m_pGtkBuilder, QUIVER_DATADIR "/" "quiver.ui", (const char**)objectids, NULL);
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



		if (m_pWidget)
		{
			/* pack the action buttons into a header bar titlebar */
			GtkHeaderBar* hbar = GTK_HEADER_BAR(gtk_header_bar_new());
			gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(hbar), FALSE);
			gtk_header_bar_pack_end(hbar, GTK_WIDGET(m_pButtonDonate));
			gtk_header_bar_pack_end(hbar, GTK_WIDGET(m_pButtonClose));
			gtk_window_set_titlebar(GTK_WINDOW(m_pWidget), GTK_WIDGET(hbar));
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
		g_signal_connect(m_pWidget, "close-request",
			G_CALLBACK(+[](GtkWidget* widget, gpointer) -> gboolean {
				GMainLoop *loop = (GMainLoop*)g_object_get_data(G_OBJECT(widget), "donate-loop");
				if (loop)
					g_main_loop_quit(loop);
				return FALSE;
			}), NULL);

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
		GMainLoop *loop = (GMainLoop*)g_object_get_data(G_OBJECT(priv->m_pWidget), "donate-loop");
		if (loop)
			g_main_loop_quit(loop);
		gtk_window_destroy(GTK_WINDOW(priv->m_pWidget));
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


