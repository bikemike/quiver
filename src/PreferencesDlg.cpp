#include <config.h>

#include "PreferencesDlg.h"
#include "ShortcutManager.h"

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
	void PopulateShortcutsList();
	void ShowKeyCaptureDialog(const std::string &action_name);

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
	
	// Shortcuts widgets
	GtkSearchEntry*        m_pSearchShortcuts;
	GtkDropDown*           m_pDropdownShortcutCategory;
	GtkListBox*            m_pListboxShortcuts;
	GtkButton*             m_pBtnResetAllShortcuts;
	
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

/* The single live PreferencesDlg instance, or NULL.  Prevents the user from
 * opening several Preferences windows at once. */
static PreferencesDlg *g_pPreferencesDlg = NULL;

static void preferences_dlg_destroy_cb(GtkWidget *widget, gpointer user_data)
{
	(void)widget;
	PreferencesDlg *dlg = static_cast<PreferencesDlg*>(user_data);
	if (g_pPreferencesDlg == dlg)
		g_pPreferencesDlg = NULL;
	/* The dialog is heap-allocated (see ACTION_QUIVER_PREFERENCES) so that its
	 * signal handlers outlive the show-and-return Run().  Delete it only after
	 * the destroy emission has finished. */
	g_idle_add(preferences_dlg_delete_idle, dlg);
}

void PreferencesDlg::ShowDialog()
{
	/* Reuse the existing instance if one is already alive: bring it to the
	 * front instead of stacking a duplicate window.  The window is hidden (not
	 * destroyed) when the user dismisses it, so presenting it again merely
	 * re-shows the same dialog. */
	if (g_pPreferencesDlg != NULL)
	{
		GtkWidget *dlg = GTK_WIDGET(gtk_builder_get_object(
			g_pPreferencesDlg->m_PrivPtr->m_pGtkBuilder, "QuiverPreferencesDialog"));
		if (dlg != NULL)
		{
			gtk_window_present(GTK_WINDOW(dlg));
			return;
		}
		g_pPreferencesDlg = NULL;
	}

	g_pPreferencesDlg = new PreferencesDlg();
	g_pPreferencesDlg->Run();
}

static void preferences_dlg_response_cb(GtkDialog *dialog, gint response_id, gpointer user_data)
{
	(void)response_id;
	(void)user_data;
	gtk_widget_set_visible(GTK_WIDGET(dialog), FALSE);
}

static gboolean preferences_dlg_close_request_cb(GtkWidget *widget, gpointer user_data)
{
	(void)user_data;
	gtk_widget_set_visible(widget, FALSE);
	return TRUE;
}

void PreferencesDlg::Run()
{
	if (m_PrivPtr->m_bLoadedDlg)
	{
		GtkWidget *prefDlg = GTK_WIDGET(gtk_builder_get_object (m_PrivPtr->m_pGtkBuilder, "QuiverPreferencesDialog"));
		GtkWindow *mainWin = gtk_application_get_active_window(g_pApp);
		if (mainWin != NULL)
			gtk_window_set_transient_for(GTK_WINDOW(prefDlg), mainWin);
		/* Dismissing the dialog (a response, the window-closer, or Escape)
		 * hides it instead of destroying the window, so the singleton survives
		 * and can be re-presented by the next ShowDialog(). */
		g_signal_connect(prefDlg, "response", G_CALLBACK(preferences_dlg_response_cb), NULL);
		g_signal_connect(prefDlg, "close-request", G_CALLBACK(preferences_dlg_close_request_cb), NULL);
		g_signal_connect(prefDlg, "destroy", G_CALLBACK(preferences_dlg_destroy_cb), this);
		gtk_widget_set_visible(prefDlg, TRUE);
		gtk_window_present(GTK_WINDOW(prefDlg));
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

		m_pSearchShortcuts           = GTK_SEARCH_ENTRY        ( gtk_builder_get_object (m_pGtkBuilder, "search_shortcuts") );
		m_pDropdownShortcutCategory  = GTK_DROP_DOWN           ( gtk_builder_get_object (m_pGtkBuilder, "dropdown_shortcut_category") );
		m_pListboxShortcuts          = GTK_LIST_BOX            ( gtk_builder_get_object (m_pGtkBuilder, "listbox_shortcuts") );
		m_pBtnResetAllShortcuts      = GTK_BUTTON              ( gtk_builder_get_object (m_pGtkBuilder, "btn_reset_all_shortcuts") );

		if (NULL != m_pListboxShortcuts)
		{
			PopulateShortcutsList();
		}

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

struct KeyDeleteData {
	PreferencesDlg::PreferencesDlgPriv *priv;
	std::string action_name;
	std::string accel;
};

static void on_shortcut_delete_clicked(GtkButton *btn, gpointer user_data)
{
	(void)btn;
	KeyDeleteData *data = static_cast<KeyDeleteData*>(user_data);
	std::string act = data->action_name;
	std::string acc = data->accel;
	PreferencesDlg::PreferencesDlgPriv *priv = data->priv;
	ShortcutManager::GetInstance().RemoveAccelerator(act, acc);
	priv->PopulateShortcutsList();
}

struct KeyAddData {
	PreferencesDlg::PreferencesDlgPriv *priv;
	std::string action_name;
};

static void on_shortcut_add_clicked(GtkButton *btn, gpointer user_data)
{
	(void)btn;
	KeyAddData *data = static_cast<KeyAddData*>(user_data);
	std::string act = data->action_name;
	PreferencesDlg::PreferencesDlgPriv *priv = data->priv;
	priv->ShowKeyCaptureDialog(act);
}

struct KeyResetData {
	PreferencesDlg::PreferencesDlgPriv *priv;
	std::string action_name;
};

static void on_shortcut_reset_clicked(GtkButton *btn, gpointer user_data)
{
	(void)btn;
	KeyResetData *data = static_cast<KeyResetData*>(user_data);
	std::string act = data->action_name;
	PreferencesDlg::PreferencesDlgPriv *priv = data->priv;
	ShortcutManager::GetInstance().ResetToDefault(act);
	priv->PopulateShortcutsList();
}

struct KeyCaptureState {
	PreferencesDlg::PreferencesDlgPriv *priv;
	std::string action_name;
	std::string captured_accel;
	std::string conflicting_label;
	GtkWidget *dialog;
	GtkLabel *lbl_display;
	GtkLabel *lbl_conflict;
	GtkButton *btn_assign;
};

static gboolean on_capture_key_pressed(GtkEventControllerKey *controller, guint keyval, guint keycode, GdkModifierType state, gpointer user_data)
{
	(void)controller; (void)keycode;
	KeyCaptureState *data = static_cast<KeyCaptureState*>(user_data);

	if (keyval == GDK_KEY_Escape) {
		gtk_window_destroy(GTK_WINDOW(data->dialog));
		return TRUE;
	}

	if (keyval == GDK_KEY_Control_L || keyval == GDK_KEY_Control_R ||
		keyval == GDK_KEY_Shift_L || keyval == GDK_KEY_Shift_R ||
		keyval == GDK_KEY_Alt_L || keyval == GDK_KEY_Alt_R ||
		keyval == GDK_KEY_Super_L || keyval == GDK_KEY_Super_R ||
		keyval == GDK_KEY_Meta_L || keyval == GDK_KEY_Meta_R) {
		return TRUE;
	}

	std::string accel = ShortcutManager::KeyvalAndModsToAccelString(keyval, state);
	if (accel.empty()) return TRUE;

	std::string human = ShortcutManager::AccelStringToHumanLabel(accel);
	data->captured_accel = accel;

	gchar *markup = g_markup_printf_escaped("<span size='xx-large'><b>%s</b></span>", human.c_str());
	gtk_label_set_markup(data->lbl_display, markup);
	g_free(markup);

	std::string conflict = ShortcutManager::GetInstance().FindConflictingAction(accel, data->action_name);
	data->conflicting_label = conflict;

	if (!conflict.empty()) {
		gchar *c_markup = g_markup_printf_escaped("<span foreground='#e67e22'>⚠️ Already assigned to '<b>%s</b>'. Assigning will reassign it.</span>", conflict.c_str());
		gtk_label_set_markup(data->lbl_conflict, c_markup);
		g_free(c_markup);
		gtk_widget_set_visible(GTK_WIDGET(data->lbl_conflict), TRUE);
	} else {
		gtk_widget_set_visible(GTK_WIDGET(data->lbl_conflict), FALSE);
	}

	gtk_widget_set_sensitive(GTK_WIDGET(data->btn_assign), TRUE);
	return TRUE;
}

static void on_capture_assign_clicked(GtkButton *btn, gpointer user_data)
{
	(void)btn;
	KeyCaptureState *data = static_cast<KeyCaptureState*>(user_data);
	if (!data->captured_accel.empty()) {
		if (!data->conflicting_label.empty()) {
			for (const auto &act : ShortcutManager::GetInstance().GetActions()) {
				if (act.label == data->conflicting_label) {
					ShortcutManager::GetInstance().RemoveAccelerator(act.action_name, data->captured_accel);
				}
			}
		}
		ShortcutManager::GetInstance().AddAccelerator(data->action_name, data->captured_accel);
		data->priv->PopulateShortcutsList();
	}
	gtk_window_destroy(GTK_WINDOW(data->dialog));
}

static void on_capture_cancel_clicked(GtkButton *btn, gpointer user_data)
{
	(void)btn;
	KeyCaptureState *data = static_cast<KeyCaptureState*>(user_data);
	gtk_window_destroy(GTK_WINDOW(data->dialog));
}

static void on_capture_destroy(GtkWidget *widget, gpointer user_data)
{
	(void)widget;
	KeyCaptureState *data = static_cast<KeyCaptureState*>(user_data);
	delete data;
}

void PreferencesDlg::PreferencesDlgPriv::ShowKeyCaptureDialog(const std::string &action_name)
{
	const ShortcutActionDef *def = ShortcutManager::GetInstance().GetAction(action_name);
	if (!def) return;

	GtkWidget *prefDlg = GTK_WIDGET(gtk_builder_get_object(m_pGtkBuilder, "QuiverPreferencesDialog"));

	KeyCaptureState *data = new KeyCaptureState();
	data->priv = this;
	data->action_name = action_name;
	data->dialog = gtk_window_new();
	gtk_window_set_title(GTK_WINDOW(data->dialog), ("Assign Shortcut - " + def->label).c_str());
	gtk_window_set_modal(GTK_WINDOW(data->dialog), TRUE);
	if (prefDlg != NULL) {
		gtk_window_set_transient_for(GTK_WINDOW(data->dialog), GTK_WINDOW(prefDlg));
	}
	gtk_window_set_default_size(GTK_WINDOW(data->dialog), 380, 220);

	GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
	gtk_widget_set_margin_start(vbox, 20);
	gtk_widget_set_margin_end(vbox, 20);
	gtk_widget_set_margin_top(vbox, 20);
	gtk_widget_set_margin_bottom(vbox, 20);

	gchar *prompt = g_markup_printf_escaped("Press the shortcut key combination for <b>%s</b>:", def->label.c_str());
	GtkWidget *lbl_prompt = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(lbl_prompt), prompt);
	g_free(prompt);
	gtk_label_set_xalign(GTK_LABEL(lbl_prompt), 0.0);
	gtk_box_append(GTK_BOX(vbox), lbl_prompt);

	GtkWidget *frame = gtk_frame_new(NULL);
	data->lbl_display = GTK_LABEL(gtk_label_new(NULL));
	gtk_label_set_markup(data->lbl_display, "<span size='large' foreground='#888888'>Press any key...</span>");
	gtk_widget_set_margin_top(GTK_WIDGET(data->lbl_display), 16);
	gtk_widget_set_margin_bottom(GTK_WIDGET(data->lbl_display), 16);
	gtk_frame_set_child(GTK_FRAME(frame), GTK_WIDGET(data->lbl_display));
	gtk_box_append(GTK_BOX(vbox), frame);

	data->lbl_conflict = GTK_LABEL(gtk_label_new(NULL));
	gtk_label_set_wrap(data->lbl_conflict, TRUE);
	gtk_widget_set_visible(GTK_WIDGET(data->lbl_conflict), FALSE);
	gtk_box_append(GTK_BOX(vbox), GTK_WIDGET(data->lbl_conflict));

	GtkWidget *hbox_btns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_widget_set_halign(hbox_btns, GTK_ALIGN_END);

	GtkWidget *btn_cancel = gtk_button_new_with_label("Cancel");
	g_signal_connect(btn_cancel, "clicked", G_CALLBACK(on_capture_cancel_clicked), data);
	gtk_box_append(GTK_BOX(hbox_btns), btn_cancel);

	data->btn_assign = GTK_BUTTON(gtk_button_new_with_label("Assign"));
	gtk_widget_add_css_class(GTK_WIDGET(data->btn_assign), "suggested-action");
	gtk_widget_set_sensitive(GTK_WIDGET(data->btn_assign), FALSE);
	g_signal_connect(data->btn_assign, "clicked", G_CALLBACK(on_capture_assign_clicked), data);
	gtk_box_append(GTK_BOX(hbox_btns), GTK_WIDGET(data->btn_assign));

	gtk_box_append(GTK_BOX(vbox), hbox_btns);

	gtk_window_set_child(GTK_WINDOW(data->dialog), vbox);

	GtkEventController *key_ctrl = gtk_event_controller_key_new();
	g_signal_connect(key_ctrl, "key-pressed", G_CALLBACK(on_capture_key_pressed), data);
	gtk_widget_add_controller(data->dialog, key_ctrl);

	g_signal_connect(data->dialog, "destroy", G_CALLBACK(on_capture_destroy), data);

	gtk_widget_set_visible(data->dialog, TRUE);
}

void PreferencesDlg::PreferencesDlgPriv::PopulateShortcutsList()
{
	if (!m_pListboxShortcuts) return;

	GtkWidget *child = gtk_widget_get_first_child(GTK_WIDGET(m_pListboxShortcuts));
	while (child != NULL) {
		GtkWidget *next = gtk_widget_get_next_sibling(child);
		gtk_list_box_remove(m_pListboxShortcuts, child);
		child = next;
	}

	const auto &actions = ShortcutManager::GetInstance().GetActions();
	for (const auto &def : actions) {
		GtkWidget *row = gtk_list_box_row_new();
		gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), FALSE);
		gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(row), FALSE);

		g_object_set_data_full(G_OBJECT(row), "action-name", g_strdup(def.action_name.c_str()), g_free);
		g_object_set_data_full(G_OBJECT(row), "category", g_strdup(def.category.c_str()), g_free);
		g_object_set_data_full(G_OBJECT(row), "label", g_strdup(def.label.c_str()), g_free);

		GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
		gtk_widget_set_margin_start(hbox, 8);
		gtk_widget_set_margin_end(hbox, 8);
		gtk_widget_set_margin_top(hbox, 6);
		gtk_widget_set_margin_bottom(hbox, 6);

		// Left: Title and description
		GtkWidget *vbox_labels = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
		gtk_widget_set_hexpand(vbox_labels, TRUE);

		GtkWidget *lbl_title = gtk_label_new(NULL);
		gchar *markup = g_markup_printf_escaped("<b>%s</b> <small><span foreground='#888888'>(%s)</span></small>",
			def.label.c_str(), def.category.c_str());
		gtk_label_set_markup(GTK_LABEL(lbl_title), markup);
		g_free(markup);
		gtk_label_set_xalign(GTK_LABEL(lbl_title), 0.0);

		GtkWidget *lbl_desc = gtk_label_new(def.description.c_str());
		gtk_widget_add_css_class(lbl_desc, "dim-label");
		gtk_label_set_xalign(GTK_LABEL(lbl_desc), 0.0);

		gtk_box_append(GTK_BOX(vbox_labels), lbl_title);
		gtk_box_append(GTK_BOX(vbox_labels), lbl_desc);
		gtk_box_append(GTK_BOX(hbox), vbox_labels);

		// Right: Key badges and buttons
		GtkWidget *box_keys = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
		gtk_widget_set_valign(box_keys, GTK_ALIGN_CENTER);

		for (const auto &accel : def.current_accels) {
			GtkWidget *pill = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
			gtk_widget_add_css_class(pill, "card");

			std::string human = ShortcutManager::AccelStringToHumanLabel(accel);
			GtkWidget *lbl_k = gtk_label_new(human.c_str());
			gtk_widget_set_margin_start(lbl_k, 6);
			gtk_widget_set_margin_end(lbl_k, 2);
			gtk_box_append(GTK_BOX(pill), lbl_k);

			GtkWidget *btn_del = gtk_button_new_from_icon_name("window-close-symbolic");
			gtk_button_set_has_frame(GTK_BUTTON(btn_del), FALSE);
			gtk_widget_set_tooltip_text(btn_del, "Remove shortcut");

			KeyDeleteData *del_data = new KeyDeleteData();
			del_data->priv = this;
			del_data->action_name = def.action_name;
			del_data->accel = accel;
			g_object_set_data_full(G_OBJECT(btn_del), "del-data", del_data, [](gpointer d){ delete static_cast<KeyDeleteData*>(d); });
			g_signal_connect(btn_del, "clicked", G_CALLBACK(on_shortcut_delete_clicked), del_data);

			gtk_box_append(GTK_BOX(pill), btn_del);
			gtk_box_append(GTK_BOX(box_keys), pill);
		}

		// Add button
		GtkWidget *btn_add = gtk_button_new_from_icon_name("list-add-symbolic");
		gtk_widget_set_tooltip_text(btn_add, "Add shortcut");
		KeyAddData *add_data = new KeyAddData();
		add_data->priv = this;
		add_data->action_name = def.action_name;
		g_object_set_data_full(G_OBJECT(btn_add), "add-data", add_data, [](gpointer d){ delete static_cast<KeyAddData*>(d); });
		g_signal_connect(btn_add, "clicked", G_CALLBACK(on_shortcut_add_clicked), add_data);
		gtk_box_append(GTK_BOX(box_keys), btn_add);

		// Reset to default button (only if customized)
		if (def.current_accels != def.default_accels) {
			GtkWidget *btn_reset = gtk_button_new_from_icon_name("edit-undo-symbolic");
			gtk_widget_set_tooltip_text(btn_reset, "Reset to default");
			KeyResetData *reset_data = new KeyResetData();
			reset_data->priv = this;
			reset_data->action_name = def.action_name;
			g_object_set_data_full(G_OBJECT(btn_reset), "reset-data", reset_data, [](gpointer d){ delete static_cast<KeyResetData*>(d); });
			g_signal_connect(btn_reset, "clicked", G_CALLBACK(on_shortcut_reset_clicked), reset_data);
			gtk_box_append(GTK_BOX(box_keys), btn_reset);
		}

		gtk_box_append(GTK_BOX(hbox), box_keys);
		gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), hbox);
		gtk_list_box_append(m_pListboxShortcuts, row);
	}

	gtk_list_box_invalidate_filter(m_pListboxShortcuts);
}

static gboolean shortcut_filter_func(GtkListBoxRow *row, gpointer user_data)
{
	PreferencesDlg::PreferencesDlgPriv *priv = static_cast<PreferencesDlg::PreferencesDlgPriv*>(user_data);
	if (!priv->m_pDropdownShortcutCategory || !priv->m_pSearchShortcuts) return TRUE;

	const gchar *cat = (const gchar*)g_object_get_data(G_OBJECT(row), "category");
	const gchar *label = (const gchar*)g_object_get_data(G_OBJECT(row), "label");
	const gchar *action_name = (const gchar*)g_object_get_data(G_OBJECT(row), "action-name");

	guint selected_cat = gtk_drop_down_get_selected(priv->m_pDropdownShortcutCategory);
	if (selected_cat > 0) {
		const char *categories[] = {
			"All Categories",
			"Viewer Navigation",
			"Video Playback",
			"Image Manipulation",
			"Viewer Display",
			"File & Window",
			"Browser"
		};
		if (selected_cat < G_N_ELEMENTS(categories)) {
			if (cat == NULL || strcmp(cat, categories[selected_cat]) != 0) {
				return FALSE;
			}
		}
	}

	const gchar *query = gtk_editable_get_text(GTK_EDITABLE(priv->m_pSearchShortcuts));
	if (query != NULL && query[0] != '\0') {
		gchar *q_down = g_utf8_strdown(query, -1);
		gchar *l_down = label ? g_utf8_strdown(label, -1) : g_strdup("");
		gchar *c_down = cat ? g_utf8_strdown(cat, -1) : g_strdup("");
		gchar *a_down = action_name ? g_utf8_strdown(action_name, -1) : g_strdup("");

		gboolean match = (strstr(l_down, q_down) != NULL) ||
		                 (strstr(c_down, q_down) != NULL) ||
		                 (strstr(a_down, q_down) != NULL);
		g_free(q_down);
		g_free(l_down);
		g_free(c_down);
		g_free(a_down);
		if (!match) return FALSE;
	}

	return TRUE;
}

static void on_shortcut_search_changed(GtkSearchEntry *entry, gpointer user_data)
{
	(void)entry;
	PreferencesDlg::PreferencesDlgPriv *priv = static_cast<PreferencesDlg::PreferencesDlgPriv*>(user_data);
	if (priv->m_pListboxShortcuts) {
		gtk_list_box_invalidate_filter(priv->m_pListboxShortcuts);
	}
}

static void on_shortcut_category_changed(GObject *dropdown, GParamSpec *pspec, gpointer user_data)
{
	(void)dropdown; (void)pspec;
	PreferencesDlg::PreferencesDlgPriv *priv = static_cast<PreferencesDlg::PreferencesDlgPriv*>(user_data);
	if (priv->m_pListboxShortcuts) {
		gtk_list_box_invalidate_filter(priv->m_pListboxShortcuts);
	}
}

static void on_reset_all_shortcuts_clicked(GtkButton *btn, gpointer user_data)
{
	(void)btn;
	PreferencesDlg::PreferencesDlgPriv *priv = static_cast<PreferencesDlg::PreferencesDlgPriv*>(user_data);
	ShortcutManager::GetInstance().ResetAllToDefaults();
	priv->PopulateShortcutsList();
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

		if (NULL != m_pListboxShortcuts)
		{
			gtk_list_box_set_filter_func(m_pListboxShortcuts, shortcut_filter_func, this, NULL);
		}
		if (NULL != m_pSearchShortcuts)
		{
			g_signal_connect(m_pSearchShortcuts, "search-changed", G_CALLBACK(on_shortcut_search_changed), this);
		}
		if (NULL != m_pDropdownShortcutCategory)
		{
			g_signal_connect(m_pDropdownShortcutCategory, "notify::selected", G_CALLBACK(on_shortcut_category_changed), this);
		}
		if (NULL != m_pBtnResetAllShortcuts)
		{
			g_signal_connect(m_pBtnResetAllShortcuts, "clicked", G_CALLBACK(on_reset_all_shortcuts_clicked), this);
		}
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



