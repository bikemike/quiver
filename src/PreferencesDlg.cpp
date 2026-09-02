#include <config.h>

#include "PreferencesDlg.h"

#include "QuiverPrefs.h"
#include "IPreferencesEventHandler.h"

extern GtkApplication *g_pApp;

class PreferencesDlg::PreferencesDlgPriv
{
public:
// constructor, destructor
	PreferencesDlgPriv(PreferencesDlg *parent);
	~PreferencesDlgPriv();
	
// methods
	void LoadWidgets();
	void UpdateUI();
	void ConnectSignals();

// variables
	PreferencesDlg*     m_pPreferencesDlg;
	GtkBuilder*           m_pGtkBuilder;
	bool m_bLoadedDlg;
	
	// dlg widgets
	GtkWidget*  m_pFCBtnPhotoLibrary;

	GtkDropDown*           m_pComboFilmstripPos;
	GtkDropDown*           m_pComboDefaultViewMode;
	
	GtkCheckButton*        m_pToggleAskBeforeDelete;
	GtkCheckButton*        m_pToggleUseThemeColor;
	GtkCheckButton*	   m_pToggleSlideShowLoop;
	GtkCheckButton*	   m_pToggleSlideShowFS;
	GtkCheckButton*	   m_pToggleStartFS;
	GtkCheckButton*	   m_pToggleQuickPreview;
	GtkCheckButton*	   m_pToggleViewerHideScrollbars;
	GtkCheckButton*	   m_pToggleBrowserHideFolderTreeFS;

	GtkCheckButton*        m_pToggleGIFAnimation;
	GtkCheckButton*        m_pToggleSlideShowTransition;
	GtkCheckButton*        m_pToggleSlideShowHideFilmStrip;
	GtkCheckButton*        m_pToggleSlideShowRotateToMaximize;
	GtkCheckButton*        m_pToggleSlideShowRandomOrder;
	GtkCheckButton*        m_pToggleFilmstripOverlay;
	GtkCheckButton*        m_pToggleViewerHideFilmstripFS;
	
	GtkRange*              m_pRangeSlideDuration;
	GtkRange*              m_pRangeFilmstripSize;
	
	GtkColorDialogButton*  m_pClrBtnBrowser;
	GtkColorDialogButton*  m_pClrBtnViewer;
	
	GtkLabel*              m_pLblBrowserColor;
	GtkLabel*              m_pLblViewerColor;
	
// nested classes
	class PreferencesEventHandler : public IPreferencesEventHandler
	{
	public:
		PreferencesEventHandler(PreferencesDlgPriv* parent) {this->parent = parent;};
		virtual void HandlePreferenceChanged(PreferencesEventPtr event);
	private:
		PreferencesDlgPriv* parent;
	};
	IPreferencesEventHandlerPtr m_PreferencesEventHandler;
	
};


PreferencesDlg::PreferencesDlg() : m_PrivPtr(new PreferencesDlg::PreferencesDlgPriv(this))
{
	
}


GtkWidget* PreferencesDlg::GetWidget()
{
	  return NULL;
}


static gboolean preferences_dlg_delete_idle(gpointer user_data)
{
	PreferencesDlg *dlg = static_cast<PreferencesDlg*>(user_data);
	delete dlg;
	return G_SOURCE_REMOVE;
}

static void preferences_dlg_destroy_cb(GtkWidget *widget, gpointer user_data)
{
	(void)widget;
	/* The dialog is heap-allocated (see the ACTION_QUIVER_PREFERENCES
	 * handler) so that its signal handlers outlive the show-and-return
	 * Run().  Delete it only after the destroy emission has finished. */
	g_idle_add(preferences_dlg_delete_idle, user_data);
}

void PreferencesDlg::Run()
{
	if (m_PrivPtr->m_bLoadedDlg)
	{
		GtkWidget *prefDlg = GTK_WIDGET(gtk_builder_get_object (m_PrivPtr->m_pGtkBuilder, "QuiverPreferencesDialog"));
		GtkWindow *mainWin = gtk_application_get_active_window(g_pApp);
		if (mainWin != NULL)
			gtk_window_set_transient_for(GTK_WINDOW(prefDlg), mainWin);
		g_signal_connect_swapped(prefDlg, "response", G_CALLBACK(gtk_window_destroy), prefDlg);
		g_signal_connect(prefDlg, "destroy", G_CALLBACK(preferences_dlg_destroy_cb), this);
		gtk_widget_set_visible(prefDlg, TRUE);
	}
}

// private stuff


// prototypes
static void on_photo_library_folder_selected(GObject *source, GAsyncResult *res, gpointer user_data);
static void  on_toggled (GtkCheckButton *togglebutton, gpointer user_data);
static void  on_viewer_film_strip_pos_changed  (GObject *widget, gpointer user_data);
static void  on_value_changed(GtkRange *range, gpointer user_data);

static void  on_color_set(GObject* object, GParamSpec* pspec, gpointer user_data);

PreferencesDlg::PreferencesDlgPriv::PreferencesDlgPriv(PreferencesDlg *parent) :
        m_pPreferencesDlg(parent),
        m_PreferencesEventHandler( new PreferencesEventHandler(this) )
{
	m_bLoadedDlg = false;
	m_pGtkBuilder = gtk_builder_new();
	const gchar* objectids[] = {
		"QuiverPreferencesDialog",
		"adjustment1",
		"adjustment2",
		NULL
	};
	gtk_builder_add_objects_from_file (m_pGtkBuilder, QUIVER_DATADIR "/" "quiver.ui", (const char**)objectids, NULL);

	LoadWidgets();
	UpdateUI();
	ConnectSignals();
}

PreferencesDlg::PreferencesDlgPriv::~PreferencesDlgPriv()
{
	if (NULL != m_pGtkBuilder)
	{
		g_object_unref(m_pGtkBuilder);
		m_pGtkBuilder = NULL;
	}
}


void PreferencesDlg::PreferencesDlgPriv::LoadWidgets()
{

	if (NULL != m_pGtkBuilder)
	{
		GtkBox* hbox_photo_library = GTK_BOX( gtk_builder_get_object (m_pGtkBuilder, "hbox_photo_library") );
		m_pFCBtnPhotoLibrary = gtk_button_new_with_label("Choose Photo Library Directory");
		
		gtk_box_append(hbox_photo_library, GTK_WIDGET(m_pFCBtnPhotoLibrary));


		//m_pFCBtnPhotoLibrary     = GTK_FILE_CHOOSER_BUTTON(     gtk_builder_get_object (m_pGtkBuilder, "fcb_general_photo_library") );
				
		m_pComboFilmstripPos     = GTK_DROP_DOWN(    gtk_builder_get_object (m_pGtkBuilder, "cbox_viewer_filmstrip_position") );
		m_pComboDefaultViewMode  = GTK_DROP_DOWN(    gtk_builder_get_object (m_pGtkBuilder, "cbox_viewer_default_viewmode") );

		if (NULL != m_pComboDefaultViewMode)
		{
			const char* viewmodes[] = {
				"Fit Image",
				"Fit Image to Window",
				"Fit Image to Window and Stretch",
				"Actual Size",
				"Fill Screen",
				NULL};
			GtkStringList* model = gtk_string_list_new(viewmodes);
			gtk_drop_down_set_model(m_pComboDefaultViewMode, G_LIST_MODEL(model));
			g_object_unref(model);
		}
		if (NULL != m_pComboFilmstripPos)
		{
			const char* positions[] = {
				"Top",
				"Left",
				"Bottom",
				"Right",
				NULL};
			GtkStringList* model = gtk_string_list_new(positions);
			gtk_drop_down_set_model(m_pComboFilmstripPos, G_LIST_MODEL(model));
			g_object_unref(model);
		}
		
		
		m_pToggleAskBeforeDelete = GTK_CHECK_BUTTON( gtk_builder_get_object (m_pGtkBuilder, "chkbtn_general_ask_before_delete") );
		m_pToggleStartFS     = GTK_CHECK_BUTTON( gtk_builder_get_object (m_pGtkBuilder, "chkbtn_general_start_fullscreen") );
		m_pToggleUseThemeColor   = GTK_CHECK_BUTTON( gtk_builder_get_object (m_pGtkBuilder, "chkbtn_general_theme_color") );
		m_pToggleQuickPreview    = GTK_CHECK_BUTTON( gtk_builder_get_object (m_pGtkBuilder, "chkbtn_viewer_quickpreview") );
		m_pToggleViewerHideScrollbars    = GTK_CHECK_BUTTON( gtk_builder_get_object (m_pGtkBuilder, "chkbtn_viewer_hide_scrollbars") );

		m_pToggleBrowserHideFolderTreeFS    = GTK_CHECK_BUTTON( gtk_builder_get_object (m_pGtkBuilder, "chkbtn_browser_hide_foldertree_fullscreen") );

		m_pToggleSlideShowLoop   = GTK_CHECK_BUTTON( gtk_builder_get_object (m_pGtkBuilder, "chkbtn_slideshow_loop") );
		m_pToggleSlideShowFS     = GTK_CHECK_BUTTON( gtk_builder_get_object (m_pGtkBuilder, "chkbtn_slideshow_fullscreen") );
		m_pToggleSlideShowTransition     = GTK_CHECK_BUTTON( gtk_builder_get_object (m_pGtkBuilder, "chkbtn_slideshow_transition") );
		m_pToggleSlideShowHideFilmStrip  = GTK_CHECK_BUTTON( gtk_builder_get_object (m_pGtkBuilder, "chkbtn_slideshow_hide_filmstrip") );
		
		m_pToggleGIFAnimation      = GTK_CHECK_BUTTON( gtk_builder_get_object (m_pGtkBuilder, "chkbtn_viewer_enable_gif_anim") );

		m_pToggleSlideShowRotateToMaximize  = GTK_CHECK_BUTTON( gtk_builder_get_object (m_pGtkBuilder, "chkbtn_slideshow_rotate_to_maximize") );

		m_pToggleSlideShowRandomOrder  = GTK_CHECK_BUTTON( gtk_builder_get_object (m_pGtkBuilder, "chkbtn_slideshow_random_order") );
		m_pToggleFilmstripOverlay = GTK_CHECK_BUTTON( gtk_builder_get_object (m_pGtkBuilder, "chkbtn_viewer_filmstrip_overlay") );
		m_pToggleViewerHideFilmstripFS = GTK_CHECK_BUTTON( gtk_builder_get_object (m_pGtkBuilder, "chkbtn_viewer_filmstrip_hide_fs") );
		
		
		m_pRangeSlideDuration    = GTK_RANGE        ( gtk_builder_get_object (m_pGtkBuilder, "hscale_slideshow_duration") );
		m_pRangeFilmstripSize    = GTK_RANGE        ( gtk_builder_get_object (m_pGtkBuilder, "hscale_viewer_filmstrip_size") );
		
		m_pClrBtnBrowser         = GTK_COLOR_DIALOG_BUTTON ( gtk_builder_get_object (m_pGtkBuilder, "clrbtn_general_bg_browser") );
		m_pClrBtnViewer          = GTK_COLOR_DIALOG_BUTTON ( gtk_builder_get_object (m_pGtkBuilder, "clrbtn_general_bg_viewer") );

		m_pLblBrowserColor       = GTK_LABEL ( gtk_builder_get_object(m_pGtkBuilder,"label_general_bg_browser") );
		m_pLblViewerColor        = GTK_LABEL ( gtk_builder_get_object(m_pGtkBuilder,"label_general_bg_viewer") );

		m_bLoadedDlg = (
			NULL != m_pFCBtnPhotoLibrary &&
			NULL != m_pComboFilmstripPos && 
			NULL != m_pComboDefaultViewMode && 
			NULL != m_pToggleAskBeforeDelete && 
			NULL != m_pToggleStartFS && 
			NULL != m_pToggleUseThemeColor && 
			NULL != m_pToggleQuickPreview && 
			NULL != m_pToggleViewerHideScrollbars && 
			NULL != m_pToggleBrowserHideFolderTreeFS && 
			NULL != m_pToggleSlideShowLoop && 
			NULL != m_pToggleSlideShowFS && 
			NULL != m_pToggleSlideShowTransition && 
			NULL != m_pToggleSlideShowHideFilmStrip && 
			NULL != m_pToggleGIFAnimation && 
			NULL != m_pToggleSlideShowRotateToMaximize && 
			NULL != m_pToggleSlideShowRandomOrder && 
			NULL != m_pToggleFilmstripOverlay && 
			NULL != m_pToggleViewerHideFilmstripFS && 
			NULL != m_pRangeSlideDuration && 
			NULL != m_pRangeFilmstripSize && 
			NULL != m_pClrBtnBrowser && 
			NULL != m_pClrBtnViewer && 
			NULL != m_pLblBrowserColor && 
			NULL != m_pLblViewerColor
			); 
	}
}

void PreferencesDlg::PreferencesDlgPriv::UpdateUI()
{
	if (m_bLoadedDlg)
	{
		PreferencesPtr prefs = Preferences::GetInstance();
		
		// sync the ui to the pref items
			
		int iFilmstripPos = prefs->GetInteger(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_FILMSTRIP_POSITION, FSTRIP_POS_LEFT);
		gtk_drop_down_set_selected(m_pComboFilmstripPos, (guint)iFilmstripPos);

		gboolean bLoopSlideshow = (gboolean)prefs->GetBoolean(QUIVER_PREFS_SLIDESHOW, QUIVER_PREFS_SLIDESHOW_LOOP, true);
		gtk_check_button_set_active(m_pToggleSlideShowLoop, bLoopSlideshow);	

		gboolean bUseThemeColor = (gboolean)prefs->GetBoolean(QUIVER_PREFS_APP, QUIVER_PREFS_APP_USE_THEME_COLOR, true);
		gtk_check_button_set_active(m_pToggleUseThemeColor, bUseThemeColor);
		gtk_widget_set_sensitive(GTK_WIDGET(m_pLblBrowserColor),!bUseThemeColor);
		gtk_widget_set_sensitive(GTK_WIDGET(m_pLblViewerColor),!bUseThemeColor);
		gtk_widget_set_sensitive(GTK_WIDGET(m_pClrBtnBrowser),!bUseThemeColor);
		gtk_widget_set_sensitive(GTK_WIDGET(m_pClrBtnViewer),!bUseThemeColor);

		gboolean bQuickPreview = (gboolean)prefs->GetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_QUICK_PREVIEW, true);
		gtk_check_button_set_active(m_pToggleQuickPreview, bQuickPreview);

		gboolean bTransition = (gboolean)prefs->GetBoolean(QUIVER_PREFS_SLIDESHOW, QUIVER_PREFS_SLIDESHOW_TRANSITION, true);
		gtk_check_button_set_active(m_pToggleSlideShowTransition, bTransition);

		gboolean bValue = (gboolean)prefs->GetBoolean(QUIVER_PREFS_SLIDESHOW, QUIVER_PREFS_SLIDESHOW_FULLSCREEN, true);
		gtk_check_button_set_active(m_pToggleSlideShowFS, bValue);

		bValue = (gboolean)prefs->GetBoolean(QUIVER_PREFS_APP, QUIVER_PREFS_APP_START_FULLSCREEN, false);
		gtk_check_button_set_active(m_pToggleStartFS, bValue);

		bValue = (gboolean)prefs->GetBoolean(QUIVER_PREFS_SLIDESHOW, QUIVER_PREFS_SLIDESHOW_FILMSTRIP_HIDE, true);
		gtk_check_button_set_active(m_pToggleSlideShowHideFilmStrip, bValue);	

		bValue = (gboolean)prefs->GetBoolean(QUIVER_PREFS_SLIDESHOW, QUIVER_PREFS_SLIDESHOW_ROTATE_FOR_BEST_FIT, false);
		gtk_check_button_set_active(m_pToggleSlideShowRotateToMaximize, bValue);

		bValue = (gboolean)prefs->GetBoolean(QUIVER_PREFS_SLIDESHOW, QUIVER_PREFS_SLIDESHOW_RANDOM_ORDER, false);
		gtk_check_button_set_active(m_pToggleSlideShowRandomOrder, bValue);

		bValue = (gboolean)prefs->GetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_SCROLLBARS_HIDE, false);
		gtk_check_button_set_active(m_pToggleViewerHideScrollbars, bValue);

		bValue = (gboolean)prefs->GetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_FILMSTRIP_OVERLAY, true);
		gtk_check_button_set_active(m_pToggleFilmstripOverlay, bValue);

		bValue = (gboolean)prefs->GetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_FILMSTRIP_HIDE_FS, true);
		gtk_check_button_set_active(m_pToggleViewerHideFilmstripFS, bValue);

		bValue = (gboolean)prefs->GetBoolean(QUIVER_PREFS_BROWSER, QUIVER_PREFS_BROWSER_FOLDERTREE_HIDE_FS, true);
		gtk_check_button_set_active(m_pToggleBrowserHideFolderTreeFS, bValue);

		std::string strClrViewer = prefs->GetString(QUIVER_PREFS_APP,QUIVER_PREFS_APP_BG_IMAGEVIEW,"#000000");
		std::string strClrBrowser = prefs->GetString(QUIVER_PREFS_APP,QUIVER_PREFS_APP_BG_ICONVIEW,"#444444");

		GdkRGBA clrBrowser;
		gdk_rgba_parse(&clrBrowser, strClrBrowser.c_str());
		gtk_color_dialog_button_set_rgba(m_pClrBtnBrowser, &clrBrowser);

		GdkRGBA clrViewer;
		gdk_rgba_parse(&clrViewer, strClrViewer.c_str());
		gtk_color_dialog_button_set_rgba(m_pClrBtnViewer, &clrViewer);

		gdouble value;
		value = prefs->GetInteger(QUIVER_PREFS_SLIDESHOW, QUIVER_PREFS_SLIDESHOW_DURATION, 2000);	
		value /= 1000.; // convert to seconds;
		gtk_range_set_value(m_pRangeSlideDuration,value);
		
		value = prefs->GetInteger(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_FILMSTRIP_SIZE, 128);
		gtk_range_set_value(m_pRangeFilmstripSize,value);
	}

}
static void on_photo_library_clicked(GtkButton *button, gpointer user_data) {
	(void)button;
	PreferencesDlg::PreferencesDlgPriv *priv = static_cast<PreferencesDlg::PreferencesDlgPriv*>(user_data);
	GtkFileDialog *dialog = gtk_file_dialog_new();
	gtk_file_dialog_set_title(dialog, "Choose Photo Library Directory");
	PreferencesPtr prefs = Preferences::GetInstance();
	std::string strPhotoLibrary = prefs->GetString(QUIVER_PREFS_APP,QUIVER_PREFS_APP_PHOTO_LIBRARY);
	if (!strPhotoLibrary.empty())
	{
		GFile *folder = g_file_new_for_uri(strPhotoLibrary.c_str());
		gtk_file_dialog_set_initial_folder(dialog, folder);
		g_object_unref(folder);
	}
	gtk_file_dialog_select_folder(dialog, NULL, NULL, on_photo_library_folder_selected, g_object_ref(priv));
}

static void on_photo_library_folder_selected(GObject *source, GAsyncResult *res, gpointer user_data) {
	GtkFileDialog *dialog = GTK_FILE_DIALOG(source);
	GError *error = NULL;
	GFile *folder = gtk_file_dialog_select_folder_finish(dialog, res, &error);
	if (folder != NULL)
	{
		gchar *uri = g_file_get_uri(folder);
		PreferencesPtr prefs = Preferences::GetInstance();
		prefs->SetString(QUIVER_PREFS_APP, QUIVER_PREFS_APP_PHOTO_LIBRARY, uri);
		g_free(uri);
		g_object_unref(folder);
	}
	else if (error != NULL) { g_error_free(error); }
	g_object_unref(user_data);
}


void PreferencesDlg::PreferencesDlgPriv::ConnectSignals()
{
	if (m_bLoadedDlg)
	{
		g_signal_connect(m_pFCBtnPhotoLibrary,
			"clicked",(GCallback)on_photo_library_clicked,this);
		g_signal_connect(m_pComboFilmstripPos,
			"notify::selected",(GCallback)on_viewer_film_strip_pos_changed,this);

		g_signal_connect(m_pToggleUseThemeColor,
			"toggled",(GCallback)on_toggled,this);	

		g_signal_connect(m_pToggleQuickPreview,
			"toggled",(GCallback)on_toggled,this);

		g_signal_connect(m_pToggleSlideShowTransition,
			"toggled",(GCallback)on_toggled,this);

		g_signal_connect(m_pToggleSlideShowFS,
			"toggled",(GCallback)on_toggled,this);

		g_signal_connect(m_pToggleStartFS,
			"toggled",(GCallback)on_toggled,this);

		g_signal_connect(m_pToggleSlideShowHideFilmStrip,
			"toggled",(GCallback)on_toggled,this);

		g_signal_connect(m_pToggleViewerHideScrollbars,
			"toggled",(GCallback)on_toggled,this);

		g_signal_connect(m_pToggleBrowserHideFolderTreeFS,
			"toggled",(GCallback)on_toggled,this);

		g_signal_connect(m_pToggleSlideShowLoop,
			"toggled",(GCallback)on_toggled,this);	

		g_signal_connect(m_pToggleSlideShowRotateToMaximize,
			"toggled",(GCallback)on_toggled,this);	

		g_signal_connect(m_pToggleSlideShowRandomOrder,
			"toggled",(GCallback)on_toggled,this);	
		
		g_signal_connect(m_pRangeSlideDuration,
			"value-changed",(GCallback)on_value_changed,this);

		g_signal_connect(m_pRangeFilmstripSize,
			"value-changed",(GCallback)on_value_changed,this);

		g_signal_connect(m_pToggleFilmstripOverlay,
			"toggled",(GCallback)on_toggled,this);

		g_signal_connect(m_pToggleViewerHideFilmstripFS,
			"toggled",(GCallback)on_toggled,this);

		g_signal_connect(m_pClrBtnBrowser,
			"notify::rgba",(GCallback)on_color_set,this);

		g_signal_connect(m_pClrBtnViewer,
			"notify::rgba",(GCallback)on_color_set,this);
	}
}

static void  on_toggled (GtkCheckButton *togglebutton, gpointer user_data)
{
	PreferencesPtr prefs = Preferences::GetInstance();
	PreferencesDlg::PreferencesDlgPriv *priv = static_cast<PreferencesDlg::PreferencesDlgPriv*>(user_data);
	
	if (priv->m_pToggleSlideShowLoop == togglebutton)
	{ 
		gboolean bLoopSlideshow = gtk_check_button_get_active(togglebutton);
		prefs->SetBoolean(QUIVER_PREFS_SLIDESHOW, QUIVER_PREFS_SLIDESHOW_LOOP, bool(bLoopSlideshow));
	}
	else if (priv->m_pToggleUseThemeColor == togglebutton)
	{
		gboolean bUseThemeColor = gtk_check_button_get_active(togglebutton);
		prefs->SetBoolean(QUIVER_PREFS_APP, QUIVER_PREFS_APP_USE_THEME_COLOR, bool(bUseThemeColor));
		priv->UpdateUI();
	}
	else if (priv->m_pToggleQuickPreview == togglebutton)
	{
		gboolean bBool = gtk_check_button_get_active(togglebutton);
		prefs->SetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_QUICK_PREVIEW, bool(bBool));
	}
	else if (priv->m_pToggleSlideShowTransition == togglebutton)
	{
		gboolean bBool = gtk_check_button_get_active(togglebutton);
		prefs->SetBoolean(QUIVER_PREFS_SLIDESHOW, QUIVER_PREFS_SLIDESHOW_TRANSITION, bool(bBool));
	}
	else if (priv->m_pToggleSlideShowFS == togglebutton)
	{
		gboolean bBool = gtk_check_button_get_active(togglebutton);
		prefs->SetBoolean(QUIVER_PREFS_SLIDESHOW, QUIVER_PREFS_SLIDESHOW_FULLSCREEN, bool(bBool));
	}
	else if (priv->m_pToggleSlideShowHideFilmStrip == togglebutton)
	{
		gboolean bBool = gtk_check_button_get_active(togglebutton);
		prefs->SetBoolean(QUIVER_PREFS_SLIDESHOW, QUIVER_PREFS_SLIDESHOW_FILMSTRIP_HIDE, bool(bBool));
	}
	else if (priv->m_pToggleViewerHideScrollbars == togglebutton)
	{
		gboolean bBool = gtk_check_button_get_active(togglebutton);
		prefs->SetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_SCROLLBARS_HIDE, bool(bBool));
	}
	else if (priv->m_pToggleBrowserHideFolderTreeFS == togglebutton)
	{
		gboolean bBool = gtk_check_button_get_active(togglebutton);
		prefs->SetBoolean(QUIVER_PREFS_BROWSER, QUIVER_PREFS_BROWSER_FOLDERTREE_HIDE_FS, bool(bBool));
	}
	else if (priv->m_pToggleStartFS == togglebutton)
	{
		gboolean bBool = gtk_check_button_get_active(togglebutton);
		prefs->SetBoolean(QUIVER_PREFS_APP, QUIVER_PREFS_APP_START_FULLSCREEN, bool(bBool));
	}
	else if (priv->m_pToggleSlideShowRotateToMaximize == togglebutton)
	{
		gboolean bBool = gtk_check_button_get_active(togglebutton);
		prefs->SetBoolean(QUIVER_PREFS_SLIDESHOW, QUIVER_PREFS_SLIDESHOW_ROTATE_FOR_BEST_FIT, bool(bBool));
	}
	else if (priv->m_pToggleSlideShowRandomOrder == togglebutton)
	{
		gboolean bBool = gtk_check_button_get_active(togglebutton);
		prefs->SetBoolean(QUIVER_PREFS_SLIDESHOW, QUIVER_PREFS_SLIDESHOW_RANDOM_ORDER, bool(bBool));
	}
	else if (priv->m_pToggleFilmstripOverlay == togglebutton)
	{
		gboolean bBool = gtk_check_button_get_active(togglebutton);
		prefs->SetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_FILMSTRIP_OVERLAY, bool(bBool));
	}
	else if (priv->m_pToggleViewerHideFilmstripFS == togglebutton)
	{
		gboolean bBool = gtk_check_button_get_active(togglebutton);
		prefs->SetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_FILMSTRIP_HIDE_FS, bool(bBool));
	}
}


static void  on_viewer_film_strip_pos_changed  (GObject *widget, gpointer user_data)
{ (void)user_data; 
	//PreferencesDlg::PreferencesDlgPriv *priv = static_cast<PreferencesDlg::PreferencesDlgPriv*>(user_data);

	PreferencesPtr prefs = Preferences::GetInstance();
	
	guint iFilmstripPos = gtk_drop_down_get_selected(GTK_DROP_DOWN(widget));
	prefs->SetInteger(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_FILMSTRIP_POSITION, (int)iFilmstripPos);
}


static void  on_color_set(GObject* object, GParamSpec* pspec, gpointer user_data)
{
	(void)pspec;
	PreferencesDlg::PreferencesDlgPriv *priv = static_cast<PreferencesDlg::PreferencesDlgPriv*>(user_data);
	PreferencesPtr prefs = Preferences::GetInstance();
	if (GTK_IS_COLOR_DIALOG_BUTTON(object))
	{
		GtkColorDialogButton *button = GTK_COLOR_DIALOG_BUTTON(object);
		const GdkRGBA *clr = gtk_color_dialog_button_get_rgba(button);
		char szColor[10];
		g_snprintf (szColor,10,"#%02x%02x%02x",(guint)(clr->red*255.999),(guint)(clr->green*255.999),(guint)(clr->blue*255.999));
		// update preferences
		
		if (button == priv->m_pClrBtnBrowser)
		{
			prefs->SetString(QUIVER_PREFS_APP,QUIVER_PREFS_APP_BG_ICONVIEW,szColor);
		}
		else if (button == priv->m_pClrBtnViewer)
		{
			prefs->SetString(QUIVER_PREFS_APP,QUIVER_PREFS_APP_BG_IMAGEVIEW,szColor);
		} 
	}
}

static void  on_value_changed(GtkRange *range, gpointer user_data)
{
	PreferencesDlg::PreferencesDlgPriv *priv = static_cast<PreferencesDlg::PreferencesDlgPriv*>(user_data);
	if (priv->m_pRangeFilmstripSize == range)
	{
		gdouble value = gtk_range_get_value(range);
		PreferencesPtr prefs = Preferences::GetInstance();
		prefs->SetInteger(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_FILMSTRIP_SIZE, (int)value);
	}
	else if (priv->m_pRangeSlideDuration == range)
	{
		gdouble value = gtk_range_get_value(range);
		value *= 1000; // convert to milliseconds
		
		PreferencesPtr prefs = Preferences::GetInstance();
		prefs->SetInteger(QUIVER_PREFS_SLIDESHOW, QUIVER_PREFS_SLIDESHOW_DURATION, (int)value);
	}

}


// nested class

void PreferencesDlg::PreferencesDlgPriv::PreferencesEventHandler::HandlePreferenceChanged(PreferencesEventPtr event)
{ (void)event; 
	parent->UpdateUI();
}



