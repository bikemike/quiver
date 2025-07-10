#include <gtk/gtk.h>
#include "PreferencesDlg.h"
#include "Preferences.h"
#include "QuiverPrefs.h" // Added for preference key string definitions
#include "Quiver.h" // For QuiverApp
// #include "QuiverFrame.h" // Removed as it does not exist and was not used correctly
#include "QuiverUtils.h"
#include "config.h"


// Preferences Dialog Private Implementation
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
	GtkComboBox*           m_pComboFilmstripPos; // Will need migration to GtkDropDown
	GtkListStore*          m_pStoreFilmstripPos; // Will need migration for GtkDropDown model
	GtkListStore*          m_pStoreSlideShowTransitions; // Will need migration
	// GtkFileChooserButton*  m_pFCBtnPhotoLibrary; // Deprecated
	GtkColorButton*        m_pClrBtnBrowser;     // Needs GdkRGBA update
	GtkColorButton*        m_pClrBtnViewer;      // Needs GdkRGBA update
	PreferencesPtr         m_prefPtr;
};

// Signal handler prototypes
static void on_response(GtkDialog *dialog, gint response_id, gpointer user_data);
static void on_folder_change(GtkFileChooser *chooser, gpointer user_data);
static void on_viewer_film_strip_pos_changed(GtkComboBox *widget, gpointer user_data);
static void on_color_set(GtkWidget *widget, gpointer user_data);


PreferencesDlg::PreferencesDlg() : m_PrivPtr (new PreferencesDlgPriv(this) )
{
}

PreferencesDlg::~PreferencesDlg()
{
}

void PreferencesDlg::Run()
{
	m_PrivPtr->UpdateUI();
	// gtk_dialog_run is deprecated. Modal dialogs are handled differently in GTK4.
	// Typically, you create a GtkWindow, set it modal, connect to "response" or button signals,
	// and then present it.
	// For now, we'll just show it non-modally to get it on screen.
    if (m_PrivPtr->m_pWindow) {
	    // gtk_window_set_modal(GTK_WINDOW(m_PrivPtr->m_pWindow), TRUE); // If it were a GtkWindow
        gtk_widget_show(m_PrivPtr->m_pWindow); // gtk_widget_show is deprecated, use gtk_widget_set_visible
    }
	// gint result = gtk_dialog_run(GTK_DIALOG(m_PrivPtr->m_pWindow));
	// if (m_PrivPtr->m_pWindow) gtk_widget_destroy(m_PrivPtr->m_pWindow); // gtk_widget_destroy is deprecated
}

PreferencesDlg::PreferencesDlgPriv::PreferencesDlgPriv(PreferencesDlg* pPublic)
{
	m_pPublic = pPublic;
	m_prefPtr = Preferences::GetInstance();

	m_pGtkBuilder = gtk_builder_new ();
    GError *error = NULL;
    // Use G_FILE_TEST_IS_REGULAR to check if file exists before loading
	if (!gtk_builder_add_from_file (m_pGtkBuilder, QUIVER_DATADIR "/" "quiver.ui", &error)) {
        g_warning ("Couldn't load builder file: %s", error->message);
        g_error_free (error);
    }

	gchar *objectids[] = {
		"QuiverPreferencesDialog",
		"adjustment1",
		"adjustment2",
		"liststore1",
		"liststore2",
		NULL
	};
	// gtk_builder_add_objects_from_file (m_pGtkBuilder, QUIVER_DATADIR "/" "quiver.ui", objectids, NULL); // Problematic call
    // Instead, get objects one by one or ensure UI file is compatible with GtkBuilder's current API

	m_pWindow = GTK_WIDGET(gtk_builder_get_object (m_pGtkBuilder, "QuiverPreferencesDialog"));

	LoadWidgets();
	ConnectSignals();

	g_object_unref(G_OBJECT(m_pGtkBuilder)); // Builder no longer needed after getting objects
}

PreferencesDlg::PreferencesDlgPriv::~PreferencesDlgPriv() {}

void PreferencesDlg::PreferencesDlgPriv::LoadWidgets()
{
	m_pAdjFilmStripSize = GTK_ADJUSTMENT(gtk_builder_get_object (m_pGtkBuilder, "adjustment1"));
	m_pAdjSlideShowSize = GTK_ADJUSTMENT(gtk_builder_get_object (m_pGtkBuilder, "adjustment2"));
	m_pComboFilmstripPos = GTK_COMBO_BOX(gtk_builder_get_object (m_pGtkBuilder, "combobox1"));
	m_pStoreFilmstripPos = GTK_LIST_STORE(gtk_builder_get_object (m_pGtkBuilder, "liststore1"));
	m_pStoreSlideShowTransitions = GTK_LIST_STORE(gtk_builder_get_object (m_pGtkBuilder, "liststore2"));
	
	// m_pFCBtnPhotoLibrary = GTK_FILE_CHOOSER_BUTTON(gtk_file_chooser_button_new ("Choose Photo Library Directory", GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER));
    // GtkWidget *vbox = GTK_WIDGET(gtk_builder_get_object (m_pGtkBuilder, "vboxPreferences"));
    // gtk_box_pack_start (GTK_BOX (vbox), GTK_WIDGET(m_pFCBtnPhotoLibrary), FALSE, FALSE, 0);
    // gtk_widget_show(GTK_WIDGET(m_pFCBtnPhotoLibrary));
	
	m_pClrBtnBrowser = GTK_COLOR_BUTTON(gtk_builder_get_object (m_pGtkBuilder, "clrbtnBrowser"));
	m_pClrBtnViewer = GTK_COLOR_BUTTON(gtk_builder_get_object (m_pGtkBuilder, "clrbtnViewer"));
}

void PreferencesDlg::PreferencesDlgPriv::ConnectSignals()
{
	g_signal_connect (G_OBJECT (m_pWindow), "response", G_CALLBACK (on_response), this);
	// g_signal_connect(m_pFCBtnPhotoLibrary, "file-set", G_CALLBACK (on_folder_change), this);
	g_signal_connect(m_pComboFilmstripPos, "changed", G_CALLBACK (on_viewer_film_strip_pos_changed), this);
	g_signal_connect(m_pClrBtnBrowser, "color-set", G_CALLBACK (on_color_set), this);
	g_signal_connect(m_pClrBtnViewer, "color-set", G_CALLBACK (on_color_set), this);
}

void PreferencesDlg::PreferencesDlgPriv::UpdateUI()
{
	// Filmstrip Thumbnail Size
	gint iFilmstripSize = m_prefPtr->GetInteger(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_FILMSTRIP_SIZE);
	gtk_adjustment_set_value(m_pAdjFilmStripSize, iFilmstripSize);

	// Filmstrip Position
	std::string strFilmstripPos = m_prefPtr->GetString(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_FILMSTRIP_POSITION);
	gint iFilmstripPos = 0;
	if (strFilmstripPos == "Bottom")
	{
		iFilmstripPos = 0;
	}
	else if (strFilmstripPos == "Top")
	{
		iFilmstripPos = 1;
	}
	else if (strFilmstripPos == "Left")
	{
		iFilmstripPos = 2;
	}
	else if (strFilmstripPos == "Right")
	{
		iFilmstripPos = 3;
	}
	gtk_combo_box_set_active(m_pComboFilmstripPos, iFilmstripPos); // GtkComboBox deprecated

	// Slideshow
	gint iSlideShowDelay = m_prefPtr->GetInteger(QUIVER_PREFS_SLIDESHOW, QUIVER_PREFS_SLIDESHOW_DURATION);
	gtk_adjustment_set_value(m_pAdjSlideShowSize, iSlideShowDelay);

	// Photo Library
	std::string strPhotoLibrary = m_prefPtr->GetString(QUIVER_PREFS_APP, QUIVER_PREFS_APP_PHOTO_LIBRARY);
	/*
	if (NULL != strPhotoLibrary.c_str())
	{
		gtk_file_chooser_set_current_folder_uri (
								GTK_FILE_CHOOSER (m_pFCBtnPhotoLibrary),
								strPhotoLibrary.c_str());
	}
    */

	// Browser BG Color
	std::string strClrBrowser = m_prefPtr->GetString(QUIVER_PREFS_APP, QUIVER_PREFS_APP_BG_ICONVIEW);
	if (strClrBrowser.length())
	{
		GdkRGBA clrBrowser = {0};
		gdk_rgba_parse(&clrBrowser, strClrBrowser.c_str());
		// gtk_color_button_set_color(m_pClrBtnBrowser,&clrBrowser); // GdkColor is deprecated
        // gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(m_pClrBtnBrowser), &clrBrowser); // Correct for GTK4
	}

	// Viewer BG Color
	std::string strClrViewer = m_prefPtr->GetString(QUIVER_PREFS_APP, QUIVER_PREFS_APP_BG_IMAGEVIEW);
	if (strClrViewer.length())
	{
		GdkRGBA clrViewer = {0};
		gdk_rgba_parse(&clrViewer, strClrViewer.c_str());
		// gtk_color_button_set_color(m_pClrBtnViewer,&clrViewer); // GdkColor is deprecated
        // gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(m_pClrBtnViewer), &clrViewer); // Correct for GTK4
	}
}

static void on_response(GtkDialog *dialog, gint response_id, gpointer user_data)
{
    PreferencesDlg::PreferencesDlgPriv* priv = static_cast<PreferencesDlg::PreferencesDlgPriv*>(user_data);
    if (response_id == GTK_RESPONSE_APPLY || response_id == GTK_RESPONSE_OK || response_id == GTK_RESPONSE_ACCEPT) // GTK_RESPONSE_APPLY might not exist in GTK4 dialogs
    {
        // Filmstrip Thumbnail Size
        gint iFilmstripSize = (gint)gtk_adjustment_get_value(priv->m_pAdjFilmStripSize);
        priv->m_prefPtr->SetInteger(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_FILMSTRIP_SIZE, iFilmstripSize);

        // Filmstrip Position
        gint iFilmstripPos = gtk_combo_box_get_active(priv->m_pComboFilmstripPos); // Deprecated
        std::string strFilmstripPos = "Bottom";
        if (iFilmstripPos == 1) strFilmstripPos = "Top";
        else if (iFilmstripPos == 2) strFilmstripPos = "Left";
        else if (iFilmstripPos == 3) strFilmstripPos = "Right";
        priv->m_prefPtr->SetString(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_FILMSTRIP_POSITION, strFilmstripPos);

        // Slideshow Delay
        gint iSlideShowDelay = (gint)gtk_adjustment_get_value(priv->m_pAdjSlideShowSize);
        priv->m_prefPtr->SetInteger(QUIVER_PREFS_SLIDESHOW, QUIVER_PREFS_SLIDESHOW_DURATION, iSlideShowDelay);

        // Browser BG Color
        GdkRGBA clrBrowser;
        // gtk_color_button_get_color(priv->m_pClrBtnBrowser,&clrBrowser); // Deprecated
        // gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(priv->m_pClrBtnBrowser), &clrBrowser); // Correct for GTK4
        // priv->m_prefPtr->SetString(PREFERENCES_BROWSER,PREFERENCES_BROWSER_BACKGROUNDCOLOR,gdk_color_to_string(&clrBrowser)); // gdk_color_to_string is deprecated
        // gchar *rgba_str_browser = gdk_rgba_to_string(&clrBrowser);
        // priv->m_prefPtr->SetString(PREFERENCES_BROWSER,PREFERENCES_BROWSER_BACKGROUNDCOLOR,rgba_str_browser);
        // g_free(rgba_str_browser);


        // Viewer BG Color
        GdkRGBA clrViewer;
        // gtk_color_button_get_color(priv->m_pClrBtnViewer,&clrViewer); // Deprecated
        // gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(priv->m_pClrBtnViewer), &clrViewer); // Correct for GTK4
        // priv->m_prefPtr->SetString(PREFERENCES_VIEWER,PREFERENCES_VIEWER_BACKGROUNDCOLOR,gdk_color_to_string(&clrViewer)); // gdk_color_to_string is deprecated
        // gchar *rgba_str_viewer = gdk_rgba_to_string(&clrViewer);
        // priv->m_prefPtr->SetString(PREFERENCES_VIEWER,PREFERENCES_VIEWER_BACKGROUNDCOLOR,rgba_str_viewer);
        // g_free(rgba_str_viewer);

        // priv->m_prefPtr->Save(); // Removed: Preferences are saved by its destructor when m_bModified is true.
        // QuiverApp::GetApp()->GetQuiverFrame()->HandlePreferenceChanged(); // Removed: Redundant, Preferences::Set* methods already emit events.
    }
    // gtk_widget_destroy(GTK_WIDGET(dialog)); // Deprecated, window management is different
    if (GTK_IS_WINDOW(dialog)) { // Check if it's a window before trying to destroy
        gtk_window_destroy(GTK_WINDOW(dialog));
    } else {
        gtk_widget_unparent(GTK_WIDGET(dialog)); // Fallback for non-window dialogs if any
    }
}

static void on_folder_change(GtkFileChooser *chooser, gpointer user_data)
{
    /*
    PreferencesDlg::PreferencesDlgPriv* priv = static_cast<PreferencesDlg::PreferencesDlgPriv*>(user_data);
    if (GTK_FILE_CHOOSER(priv->m_pFCBtnPhotoLibrary) == chooser)
    {
        gchar* dir = gtk_file_chooser_get_uri (
                        GTK_FILE_CHOOSER (priv->m_pFCBtnPhotoLibrary));
        priv->m_prefPtr->SetString(PREFERENCES_GENERAL,PREFERENCES_GENERAL_PHOTOLIBRARY,dir);
        g_free (dir);
    }
    else
    {
        gchar* dir = gtk_file_chooser_get_filename (
                                        GTK_FILE_CHOOSER (priv->m_pFCBtnPhotoLibrary)
                                        );
        priv->m_prefPtr->SetString(PREFERENCES_GENERAL,PREFERENCES_GENERAL_PHOTOLIBRARY,dir);

        g_free (dir);
    }
    */
}

// Callbacks
void on_viewer_film_strip_pos_changed(GtkComboBox *widget, gpointer user_data)
{
    // This function will need to be adapted if GtkComboBox is replaced by GtkDropDown
    // For now, keeping the logic but noting the deprecation
    PreferencesDlg::PreferencesDlgPriv* priv = static_cast<PreferencesDlg::PreferencesDlgPriv*>(user_data);
    if (priv) {
        gint iFilmstripPos = gtk_combo_box_get_active(widget); // Deprecated
        std::string strFilmstripPos = "Bottom";
        if (iFilmstripPos == 1) strFilmstripPos = "Top";
        else if (iFilmstripPos == 2) strFilmstripPos = "Left";
        else if (iFilmstripPos == 3) strFilmstripPos = "Right";
        priv->m_prefPtr->SetString(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_FILMSTRIP_POSITION, strFilmstripPos);
    }
}

void on_color_set(GtkWidget *widget, gpointer user_data)
{
    PreferencesDlg::PreferencesDlgPriv* priv = static_cast<PreferencesDlg::PreferencesDlgPriv*>(user_data);
    if (priv) {
        GtkColorButton *button = GTK_COLOR_BUTTON(widget);
        GdkRGBA rgba;
        gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(button), &rgba);
        gchar *rgba_str = gdk_rgba_to_string(&rgba);

        if (button == priv->m_pClrBtnBrowser) {
            priv->m_prefPtr->SetString(QUIVER_PREFS_APP, QUIVER_PREFS_APP_BG_ICONVIEW, rgba_str);
        } else if (button == priv->m_pClrBtnViewer) {
            priv->m_prefPtr->SetString(QUIVER_PREFS_APP, QUIVER_PREFS_APP_BG_IMAGEVIEW, rgba_str);
        }
        g_free(rgba_str);
    }
}
