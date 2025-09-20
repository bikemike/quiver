#include <gtk/gtk.h>
#include "PreferencesDlg.h"
#include "Preferences.h"
#include "QuiverPrefs.h"
#include "Quiver.h"
#include "QuiverUtils.h"
#include "config.h"

class PreferencesDlg::PreferencesDlgPriv
{
public:
	PreferencesDlgPriv(PreferencesDlg* pPublic);
	~PreferencesDlgPriv();
	
	void LoadWidgets();
	void ConnectSignals();
	void UpdateUI();
	
	PreferencesDlg*        m_pPublic;
	GtkBuilder*            m_pGtkBuilder;
	GtkWidget*             m_pWindow;
	GtkAdjustment*         m_pAdjFilmStripSize;
	GtkAdjustment*         m_pAdjSlideShowSize;
	GtkDropDown*           m_pDropDownFilmstripPos;
	GtkStringList*         m_pStoreFilmstripPos;
	GtkColorDialogButton*  m_pClrBtnBrowser;
	GtkColorDialogButton*  m_pClrBtnViewer;
	PreferencesPtr         m_prefPtr;
};

static void on_response(GtkDialog *dialog, gint response_id, gpointer user_data);
static void on_folder_change(GtkFileChooser *chooser, gpointer user_data);
static void on_viewer_film_strip_pos_changed(GtkDropDown *widget, GParamSpec* pspec, gpointer user_data);
static void on_color_changed(GtkColorDialogButton *widget, GParamSpec* pspec, gpointer user_data);


PreferencesDlg::PreferencesDlg() : m_PrivPtr (new PreferencesDlgPriv(this) )
{
}

PreferencesDlg::~PreferencesDlg()
{
}

void PreferencesDlg::Run()
{
	m_PrivPtr->UpdateUI();
    if (m_PrivPtr->m_pWindow) {
        gtk_widget_set_visible(m_PrivPtr->m_pWindow, TRUE);
    }
}

PreferencesDlg::PreferencesDlgPriv::PreferencesDlgPriv(PreferencesDlg* pPublic)
{
	m_pPublic = pPublic;
	m_prefPtr = Preferences::GetInstance();

	m_pGtkBuilder = gtk_builder_new ();
    GError *error = NULL;
	if (!gtk_builder_add_from_file (m_pGtkBuilder, QUIVER_DATADIR "/" "quiver.ui", &error)) {
        g_warning ("Couldn't load builder file: %s", error->message);
        g_error_free (error);
    }

	m_pWindow = GTK_WIDGET(gtk_builder_get_object (m_pGtkBuilder, "QuiverPreferencesDialog"));

	LoadWidgets();
	ConnectSignals();

	g_object_unref(G_OBJECT(m_pGtkBuilder));
}

PreferencesDlg::PreferencesDlgPriv::~PreferencesDlgPriv() {}

void PreferencesDlg::PreferencesDlgPriv::LoadWidgets()
{
	m_pAdjFilmStripSize = GTK_ADJUSTMENT(gtk_builder_get_object (m_pGtkBuilder, "adjustment1"));
	m_pAdjSlideShowSize = GTK_ADJUSTMENT(gtk_builder_get_object (m_pGtkBuilder, "adjustment2"));
	m_pDropDownFilmstripPos = GTK_DROP_DOWN(gtk_builder_get_object (m_pGtkBuilder, "combobox1"));
	
	const char* pos[] = {"Bottom", "Top", "Left", "Right", NULL};
	m_pStoreFilmstripPos = gtk_string_list_new(pos);
	gtk_drop_down_set_model(m_pDropDownFilmstripPos, G_LIST_MODEL(m_pStoreFilmstripPos));
	
	m_pClrBtnBrowser = GTK_COLOR_DIALOG_BUTTON(gtk_builder_get_object (m_pGtkBuilder, "clrbtn_general_bg_browser"));
	m_pClrBtnViewer = GTK_COLOR_DIALOG_BUTTON(gtk_builder_get_object (m_pGtkBuilder, "clrbtn_general_bg_viewer"));
}

void PreferencesDlg::PreferencesDlgPriv::ConnectSignals()
{
	g_signal_connect (G_OBJECT (m_pWindow), "response", G_CALLBACK (on_response), this);
	g_signal_connect(m_pDropDownFilmstripPos, "notify::selected", G_CALLBACK (on_viewer_film_strip_pos_changed), this);
	g_signal_connect(m_pClrBtnBrowser, "notify::rgba", G_CALLBACK (on_color_changed), this);
	g_signal_connect(m_pClrBtnViewer, "notify::rgba", G_CALLBACK (on_color_changed), this);
}

void PreferencesDlg::PreferencesDlgPriv::UpdateUI()
{
	// Filmstrip Thumbnail Size
	gint iFilmstripSize = m_prefPtr->GetInteger(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_FILMSTRIP_SIZE);
	gtk_adjustment_set_value(m_pAdjFilmStripSize, iFilmstripSize);

	// Filmstrip Position
	std::string strFilmstripPos = m_prefPtr->GetString(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_FILMSTRIP_POSITION);
	gint iFilmstripPos = 0;
	if (strFilmstripPos == "Top") iFilmstripPos = 1;
	else if (strFilmstripPos == "Left") iFilmstripPos = 2;
	else if (strFilmstripPos == "Right") iFilmstripPos = 3;
	gtk_drop_down_set_selected(m_pDropDownFilmstripPos, iFilmstripPos);

	// Slideshow
	gint iSlideShowDelay = m_prefPtr->GetInteger(QUIVER_PREFS_SLIDESHOW, QUIVER_PREFS_SLIDESHOW_DURATION);
	gtk_adjustment_set_value(m_pAdjSlideShowSize, iSlideShowDelay);

	// Browser BG Color
	std::string strClrBrowser = m_prefPtr->GetString(QUIVER_PREFS_APP, QUIVER_PREFS_APP_BG_ICONVIEW);
	if (strClrBrowser.length())
	{
		GdkRGBA clrBrowser = {0};
		gdk_rgba_parse(&clrBrowser, strClrBrowser.c_str());
        gtk_color_dialog_button_set_rgba(m_pClrBtnBrowser, &clrBrowser);
	}

	// Viewer BG Color
	std::string strClrViewer = m_prefPtr->GetString(QUIVER_PREFS_APP, QUIVER_PREFS_APP_BG_IMAGEVIEW);
	if (strClrViewer.length())
	{
		GdkRGBA clrViewer = {0};
		gdk_rgba_parse(&clrViewer, strClrViewer.c_str());
        gtk_color_dialog_button_set_rgba(m_pClrBtnViewer, &clrViewer);
	}
}

static void on_response(GtkDialog *dialog, gint response_id, gpointer user_data)
{
    PreferencesDlg::PreferencesDlgPriv* priv = static_cast<PreferencesDlg::PreferencesDlgPriv*>(user_data);
    if (response_id == GTK_RESPONSE_APPLY || response_id == GTK_RESPONSE_OK || response_id == GTK_RESPONSE_ACCEPT)
    {
        gint iFilmstripSize = (gint)gtk_adjustment_get_value(priv->m_pAdjFilmStripSize);
        priv->m_prefPtr->SetInteger(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_FILMSTRIP_SIZE, iFilmstripSize);

        guint iFilmstripPos = gtk_drop_down_get_selected(priv->m_pDropDownFilmstripPos);
        const char* strFilmstripPos = gtk_string_list_get_string(priv->m_pStoreFilmstripPos, iFilmstripPos);
        priv->m_prefPtr->SetString(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_FILMSTRIP_POSITION, strFilmstripPos);

        gint iSlideShowDelay = (gint)gtk_adjustment_get_value(priv->m_pAdjSlideShowSize);
        priv->m_prefPtr->SetInteger(QUIVER_PREFS_SLIDESHOW, QUIVER_PREFS_SLIDESHOW_DURATION, iSlideShowDelay);
    }
    if (GTK_IS_WINDOW(dialog)) {
        gtk_window_destroy(GTK_WINDOW(dialog));
    }
}

static void on_folder_change(GtkFileChooser *chooser, gpointer user_data)
{
}

void on_viewer_film_strip_pos_changed(GtkDropDown *widget, GParamSpec* pspec, gpointer user_data)
{
    PreferencesDlg::PreferencesDlgPriv* priv = static_cast<PreferencesDlg::PreferencesDlgPriv*>(user_data);
    if (priv) {
        guint iFilmstripPos = gtk_drop_down_get_selected(widget);
        const char* strFilmstripPos = gtk_string_list_get_string(priv->m_pStoreFilmstripPos, iFilmstripPos);
        priv->m_prefPtr->SetString(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_FILMSTRIP_POSITION, strFilmstripPos);
    }
}

void on_color_changed(GtkColorDialogButton *widget, GParamSpec* pspec, gpointer user_data)
{
    PreferencesDlg::PreferencesDlgPriv* priv = static_cast<PreferencesDlg::PreferencesDlgPriv*>(user_data);
    if (priv) {
        const GdkRGBA* rgba = gtk_color_dialog_button_get_rgba(widget);
        gchar *rgba_str = gdk_rgba_to_string(rgba);

        if (widget == priv->m_pClrBtnBrowser) {
            priv->m_prefPtr->SetString(QUIVER_PREFS_APP, QUIVER_PREFS_APP_BG_ICONVIEW, rgba_str);
        } else if (widget == priv->m_pClrBtnViewer) {
            priv->m_prefPtr->SetString(QUIVER_PREFS_APP, QUIVER_PREFS_APP_BG_IMAGEVIEW, rgba_str);
        }
        g_free(rgba_str);
    }
}
