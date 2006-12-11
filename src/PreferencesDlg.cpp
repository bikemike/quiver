#include "PreferencesDlg.h"

#include <glade/glade.h>

#include "Preferences.h"


class PreferencesDlg::PreferencesDlgPriv
{
public:
	PreferencesDlgPriv(PreferencesDlg *parent);
	~PreferencesDlgPriv();

	PreferencesDlg *m_pPreferencesDlg;
	GladeXML *m_pGladeXML;
	
};


PreferencesDlg::PreferencesDlg() : m_PrivPtr(new PreferencesDlg::PreferencesDlgPriv(this))
{
	
}


GtkWidget* PreferencesDlg::GetWidget()
{
	  return NULL;
}


void PreferencesDlg::Run()
{
	 GtkWidget *prefDlg = glade_xml_get_widget (m_PrivPtr->m_pGladeXML, "QuiverPreferencesDialog");
	 gtk_dialog_run(GTK_DIALOG(prefDlg));
	 gtk_widget_destroy(prefDlg);
}

// private stuff


// prototypes
static void  on_general_theme_color_toggled (GtkToggleButton *togglebutton, gpointer user_data);


PreferencesDlg::PreferencesDlgPriv::PreferencesDlgPriv(PreferencesDlg *parent) : m_pPreferencesDlg(parent)
{
	PreferencesPtr prefs = Preferences::GetInstance();

	m_pGladeXML = glade_xml_new (QUIVER_GLADEDIR "/" "quiver.glade", "QuiverPreferencesDialog", NULL);
	
	g_signal_connect(glade_xml_get_widget (m_pGladeXML, "chkbtn_general_theme_color")
		,"toggled",(GCallback)on_general_theme_color_toggled,this);	
	
	GtkWidget *clrbtn_bg_browser = glade_xml_get_widget (m_pGladeXML, "clrbtn_general_bg_browser");
	GtkWidget *clrbtn_bg_viewer = glade_xml_get_widget (m_pGladeXML, "clrbtn_general_bg_viewer");
	
	std::string strClrViewer = prefs->GetString("application","bgcolor_imageview");
	std::string strClrBrowser = prefs->GetString("application","bgcolor_iconview");

	GdkColor clrBrowser = {0};
	gdk_color_parse(strClrBrowser.c_str(), &clrBrowser);
	gtk_color_button_set_color(GTK_COLOR_BUTTON(clrbtn_bg_browser),&clrBrowser);

	GdkColor clrViewer = {0};
	gdk_color_parse(strClrViewer.c_str(), &clrViewer);
	gtk_color_button_set_color(GTK_COLOR_BUTTON(clrbtn_bg_viewer),&clrViewer);


	//GtkWidget *chkbtn_general_loop = glade_xml_get_widget (m_pGladeXML, "clrbtn_general_bg_viewer");
	

}


PreferencesDlg::PreferencesDlgPriv::~PreferencesDlgPriv()
{
	if (NULL != m_pGladeXML)
	{
		g_object_unref(m_pGladeXML);
		m_pGladeXML = NULL;
	}
}


static void  on_general_theme_color_toggled (GtkToggleButton *togglebutton, gpointer user_data)
{
	PreferencesDlg::PreferencesDlgPriv *priv = static_cast<PreferencesDlg::PreferencesDlgPriv*>(user_data);
	
	GtkWidget *label_browser = glade_xml_get_widget(priv->m_pGladeXML,"label_general_bg_browser");
	GtkWidget *label_viewer = glade_xml_get_widget(priv->m_pGladeXML,"label_general_bg_viewer");
	GtkWidget *clrbtn_browser = glade_xml_get_widget(priv->m_pGladeXML,"clrbtn_general_bg_browser");
	GtkWidget *clrbtn_viewer = glade_xml_get_widget(priv->m_pGladeXML,"clrbtn_general_bg_viewer");

	gboolean sensitive = !gtk_toggle_button_get_active(togglebutton);

	gtk_widget_set_sensitive(label_browser,sensitive);
	gtk_widget_set_sensitive(label_viewer,sensitive);
	gtk_widget_set_sensitive(clrbtn_browser,sensitive);
	gtk_widget_set_sensitive(clrbtn_viewer,sensitive);
	
}
 

