#include <config.h>

#include "PreferencesDlg.h"

#include <glade/glade.h>

#include "QuiverPrefs.h"
#include "IPreferencesEventHandler.h"


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
	GladeXML*           m_pGladeXML;
	
	// dlg widgets
	GtkFileChooserButton*  m_pFCBtnPhotoLibrary;
	
	GtkComboBox*           m_pComboFilmstripPos;
	GtkComboBox*           m_pComboDefaultViewMode;
	
	GtkToggleButton*       m_pToggleAskBeforeDelete;
	GtkToggleButton*       m_pToggleUseThemeColor;
	GtkToggleButton*	   m_pToggleSlideShowLoop;
	GtkToggleButton*	   m_pToggleSlideShowFS;
	GtkToggleButton*	   m_pToggleQuickPreview;
	GtkToggleButton*	   m_pToggleViewerHideScrollbars;

	GtkToggleButton*       m_pToggleGIFAnimation;
	GtkToggleButton*       m_pToggleSlideShowTransition;
	GtkToggleButton*       m_pToggleSlideShowHideFilmStrip;
	
	GtkRange*              m_pRangeSlideDuration;
	GtkRange*              m_pRangeFilmstripSize;
	
	GtkColorButton*        m_pClrBtnBrowser;
	GtkColorButton*        m_pClrBtnViewer;
	
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


void PreferencesDlg::Run()
{
	 GtkWidget *prefDlg = glade_xml_get_widget (m_PrivPtr->m_pGladeXML, "QuiverPreferencesDialog");
	 gtk_dialog_run(GTK_DIALOG(prefDlg));
	 gtk_widget_destroy(prefDlg);
}

// private stuff


// prototypes
static void  on_toggled (GtkToggleButton *togglebutton, gpointer user_data);
static void  on_general_theme_color_toggled (GtkToggleButton *togglebutton, gpointer user_data);
static void  on_viewer_film_strip_pos_changed  (GtkComboBox *widget, gpointer user_data);
static void  on_value_changed(GtkRange *range, gpointer user_data);

static void  on_color_set(GtkWidget* widget, gpointer user_data);

PreferencesDlg::PreferencesDlgPriv::PreferencesDlgPriv(PreferencesDlg *parent) :
        m_pPreferencesDlg(parent),
        m_PreferencesEventHandler( new PreferencesEventHandler(this) )
{
	m_pGladeXML = glade_xml_new (QUIVER_GLADEDIR "/" "quiver.glade", "QuiverPreferencesDialog", NULL);

	LoadWidgets();
	UpdateUI();
	ConnectSignals();
}

PreferencesDlg::PreferencesDlgPriv::~PreferencesDlgPriv()
{
	if (NULL != m_pGladeXML)
	{
		g_object_unref(m_pGladeXML);
		m_pGladeXML = NULL;
	}
}


void PreferencesDlg::PreferencesDlgPriv::LoadWidgets()
{

	m_pFCBtnPhotoLibrary     = GTK_FILE_CHOOSER_BUTTON(     glade_xml_get_widget (m_pGladeXML, "fcb_general_photo_library") );
			
	m_pComboFilmstripPos     = GTK_COMBO_BOX(     glade_xml_get_widget (m_pGladeXML, "cbox_viewer_filmstrip_position") );
	m_pComboDefaultViewMode  = GTK_COMBO_BOX(     glade_xml_get_widget (m_pGladeXML, "cbox_viewer_default_viewmode") );
	
	
	m_pToggleAskBeforeDelete = GTK_TOGGLE_BUTTON( glade_xml_get_widget (m_pGladeXML, "chkbtn_general_ask_before_delete") );
	m_pToggleUseThemeColor   = GTK_TOGGLE_BUTTON( glade_xml_get_widget (m_pGladeXML, "chkbtn_general_theme_color") );
	m_pToggleQuickPreview    = GTK_TOGGLE_BUTTON( glade_xml_get_widget (m_pGladeXML, "chkbtn_viewer_quickpreview") );
	m_pToggleViewerHideScrollbars    = GTK_TOGGLE_BUTTON( glade_xml_get_widget (m_pGladeXML, "chkbtn_viewer_hide_scrollbars") );

	m_pToggleSlideShowLoop   = GTK_TOGGLE_BUTTON( glade_xml_get_widget (m_pGladeXML, "chkbtn_slideshow_loop") );
	m_pToggleSlideShowFS     = GTK_TOGGLE_BUTTON( glade_xml_get_widget (m_pGladeXML, "chkbtn_slideshow_fullscreen") );
	m_pToggleSlideShowTransition     = GTK_TOGGLE_BUTTON( glade_xml_get_widget (m_pGladeXML, "chkbtn_slideshow_transition") );
	m_pToggleSlideShowHideFilmStrip  = GTK_TOGGLE_BUTTON( glade_xml_get_widget (m_pGladeXML, "chkbtn_slideshow_hide_filmstrip") );
	
	m_pToggleGIFAnimation    = GTK_TOGGLE_BUTTON( glade_xml_get_widget (m_pGladeXML, "chkbtn_viewer_enable_gif_anim") );
	
	
	m_pRangeSlideDuration    = GTK_RANGE        ( glade_xml_get_widget (m_pGladeXML, "hscale_slideshow_duration") );
	m_pRangeFilmstripSize    = GTK_RANGE        ( glade_xml_get_widget (m_pGladeXML, "hscale_viewer_filmstrip_size") );
	
	m_pClrBtnBrowser         = GTK_COLOR_BUTTON ( glade_xml_get_widget (m_pGladeXML, "clrbtn_general_bg_browser") );
	m_pClrBtnViewer          = GTK_COLOR_BUTTON ( glade_xml_get_widget (m_pGladeXML, "clrbtn_general_bg_viewer") );

	m_pLblBrowserColor       = GTK_LABEL ( glade_xml_get_widget(m_pGladeXML,"label_general_bg_browser") );
	m_pLblViewerColor        = GTK_LABEL ( glade_xml_get_widget(m_pGladeXML,"label_general_bg_viewer") );
}

void PreferencesDlg::PreferencesDlgPriv::UpdateUI()
{
	PreferencesPtr prefs = Preferences::GetInstance();
	
	// sync the ui to the pref items
		
	int iFilmstripPos = prefs->GetInteger(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_FILMSTRIP_POSITION, FSTRIP_POS_LEFT);
	gtk_combo_box_set_active(m_pComboFilmstripPos, iFilmstripPos);

	gboolean bLoopSlideshow = (gboolean)prefs->GetBoolean(QUIVER_PREFS_SLIDESHOW, QUIVER_PREFS_SLIDESHOW_LOOP, true);
	gtk_toggle_button_set_active(m_pToggleSlideShowLoop, bLoopSlideshow);	

	gboolean bUseThemeColor = (gboolean)prefs->GetBoolean(QUIVER_PREFS_APP, QUIVER_PREFS_APP_USE_THEME_COLOR, true);
	gtk_toggle_button_set_active(m_pToggleUseThemeColor, bUseThemeColor);
	gtk_widget_set_sensitive(GTK_WIDGET(m_pLblBrowserColor),!bUseThemeColor);
	gtk_widget_set_sensitive(GTK_WIDGET(m_pLblViewerColor),!bUseThemeColor);
	gtk_widget_set_sensitive(GTK_WIDGET(m_pClrBtnBrowser),!bUseThemeColor);
	gtk_widget_set_sensitive(GTK_WIDGET(m_pClrBtnViewer),!bUseThemeColor);

	gboolean bQuickPreview = (gboolean)prefs->GetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_QUICK_PREVIEW, true);
	gtk_toggle_button_set_active(m_pToggleQuickPreview, bQuickPreview);

	gboolean bTransition = (gboolean)prefs->GetBoolean(QUIVER_PREFS_SLIDESHOW, QUIVER_PREFS_SLIDESHOW_TRANSITION, true);
	gtk_toggle_button_set_active(m_pToggleSlideShowTransition, bTransition);

	gboolean bValue = (gboolean)prefs->GetBoolean(QUIVER_PREFS_SLIDESHOW, QUIVER_PREFS_SLIDESHOW_FULLSCREEN, false);
	gtk_toggle_button_set_active(m_pToggleSlideShowFS, bValue);

	bValue = (gboolean)prefs->GetBoolean(QUIVER_PREFS_SLIDESHOW, QUIVER_PREFS_SLIDESHOW_FILMSTRIP_HIDE, false);
	gtk_toggle_button_set_active(m_pToggleSlideShowHideFilmStrip, bValue);	

	bValue = (gboolean)prefs->GetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_SCROLLBARS_HIDE, false);
	gtk_toggle_button_set_active(m_pToggleViewerHideScrollbars, bValue);

	std::string strClrViewer = prefs->GetString(QUIVER_PREFS_APP,QUIVER_PREFS_APP_BG_IMAGEVIEW,"#000000");
	std::string strClrBrowser = prefs->GetString(QUIVER_PREFS_APP,QUIVER_PREFS_APP_BG_ICONVIEW,"#444444");

	std::string strPhotoLibrary = prefs->GetString(QUIVER_PREFS_APP,QUIVER_PREFS_APP_PHOTO_LIBRARY);
	if (!strPhotoLibrary.empty())
	{
		gtk_file_chooser_set_current_folder (
			GTK_FILE_CHOOSER (m_pFCBtnPhotoLibrary),
			strPhotoLibrary.c_str());
	}  

	GdkColor clrBrowser = {0};
	gdk_color_parse(strClrBrowser.c_str(), &clrBrowser);
	gtk_color_button_set_color(m_pClrBtnBrowser,&clrBrowser);

	GdkColor clrViewer = {0};
	gdk_color_parse(strClrViewer.c_str(), &clrViewer);
	gtk_color_button_set_color(m_pClrBtnViewer,&clrViewer);

	gdouble value;
	value = prefs->GetInteger(QUIVER_PREFS_SLIDESHOW, QUIVER_PREFS_SLIDESHOW_DURATION, 2000);	
	value /= 1000.; // convert to seconds;
	gtk_range_set_value(m_pRangeSlideDuration,value);
	
	value = prefs->GetInteger(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_FILMSTRIP_SIZE, 128);
	gtk_range_set_value(m_pRangeFilmstripSize,value);
	

}

void on_folder_change (GtkFileChooser *chooser, gpointer user_data)
{
	PreferencesDlg::PreferencesDlgPriv *priv = static_cast<PreferencesDlg::PreferencesDlgPriv*>(user_data);
	PreferencesPtr prefs = Preferences::GetInstance();
	
	g_signal_handlers_block_by_func(chooser, (gpointer)on_folder_change, user_data);
	
	if (GTK_FILE_CHOOSER(priv->m_pFCBtnPhotoLibrary) == chooser)
	{
		gchar* dir = gtk_file_chooser_get_current_folder (
			GTK_FILE_CHOOSER (priv->m_pFCBtnPhotoLibrary));
		if (NULL == dir)
		{
			std::string strPhotoLibrary = prefs->GetString(QUIVER_PREFS_APP,QUIVER_PREFS_APP_PHOTO_LIBRARY);
			if (!strPhotoLibrary.empty())
			{
				gtk_file_chooser_set_current_folder (
					GTK_FILE_CHOOSER (priv->m_pFCBtnPhotoLibrary),
					strPhotoLibrary.c_str());
			}
		}
		else
		{
			prefs->SetString(QUIVER_PREFS_APP,QUIVER_PREFS_APP_PHOTO_LIBRARY,dir);
			g_free(dir);
		}
	}
	
	g_signal_handlers_unblock_by_func(chooser, (gpointer)on_folder_change, user_data);
}
	


void PreferencesDlg::PreferencesDlgPriv::ConnectSignals()
{
	g_signal_connect(m_pFCBtnPhotoLibrary,
		"current-folder-changed",(GCallback)on_folder_change,this);

	g_signal_connect(m_pComboFilmstripPos,
		"changed",(GCallback)on_viewer_film_strip_pos_changed,this);

	g_signal_connect(m_pToggleUseThemeColor,
		"toggled",(GCallback)on_toggled,this);	

	g_signal_connect(m_pToggleQuickPreview,
		"toggled",(GCallback)on_toggled,this);

	g_signal_connect(m_pToggleSlideShowTransition,
		"toggled",(GCallback)on_toggled,this);

	g_signal_connect(m_pToggleSlideShowFS,
		"toggled",(GCallback)on_toggled,this);

	g_signal_connect(m_pToggleSlideShowHideFilmStrip,
		"toggled",(GCallback)on_toggled,this);

	g_signal_connect(m_pToggleViewerHideScrollbars,
		"toggled",(GCallback)on_toggled,this);

	g_signal_connect(m_pToggleSlideShowLoop,
		"toggled",(GCallback)on_toggled,this);	
	
	g_signal_connect(m_pRangeSlideDuration,
		"value-changed",(GCallback)on_value_changed,this);

	g_signal_connect(m_pRangeFilmstripSize,
		"value-changed",(GCallback)on_value_changed,this);

	g_signal_connect(m_pClrBtnBrowser,
		"color-set",(GCallback)on_color_set,this);

	g_signal_connect(m_pClrBtnViewer,
		"color-set",(GCallback)on_color_set,this);
}


static void  on_toggled (GtkToggleButton *togglebutton, gpointer user_data)
{
	PreferencesPtr prefs = Preferences::GetInstance();
	PreferencesDlg::PreferencesDlgPriv *priv = static_cast<PreferencesDlg::PreferencesDlgPriv*>(user_data);
	
	if (priv->m_pToggleSlideShowLoop == togglebutton)
	{ 
		gboolean bLoopSlideshow = gtk_toggle_button_get_active(togglebutton);
		prefs->SetBoolean(QUIVER_PREFS_SLIDESHOW, QUIVER_PREFS_SLIDESHOW_LOOP, bool(bLoopSlideshow));
	}
	else if (priv->m_pToggleUseThemeColor == togglebutton)
	{
		gboolean bUseThemeColor = gtk_toggle_button_get_active(togglebutton);
		prefs->SetBoolean(QUIVER_PREFS_APP, QUIVER_PREFS_APP_USE_THEME_COLOR, bool(bUseThemeColor));
		priv->UpdateUI();
	}
	else if (priv->m_pToggleQuickPreview == togglebutton)
	{
		gboolean bBool = gtk_toggle_button_get_active(togglebutton);
		prefs->SetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_QUICK_PREVIEW, bool(bBool));
	}
	else if (priv->m_pToggleSlideShowTransition == togglebutton)
	{
		gboolean bBool = gtk_toggle_button_get_active(togglebutton);
		prefs->SetBoolean(QUIVER_PREFS_SLIDESHOW, QUIVER_PREFS_SLIDESHOW_TRANSITION, bool(bBool));
	}
	else if (priv->m_pToggleSlideShowFS == togglebutton)
	{
		gboolean bBool = gtk_toggle_button_get_active(togglebutton);
		prefs->SetBoolean(QUIVER_PREFS_SLIDESHOW, QUIVER_PREFS_SLIDESHOW_FULLSCREEN, bool(bBool));
	}
	else if (priv->m_pToggleSlideShowHideFilmStrip == togglebutton)
	{
		gboolean bBool = gtk_toggle_button_get_active(togglebutton);
		prefs->SetBoolean(QUIVER_PREFS_SLIDESHOW, QUIVER_PREFS_SLIDESHOW_FILMSTRIP_HIDE, bool(bBool));
	}
	else if (priv->m_pToggleViewerHideScrollbars == togglebutton)
	{
		gboolean bBool = gtk_toggle_button_get_active(togglebutton);
		prefs->SetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_SCROLLBARS_HIDE, bool(bBool));
	}
}


static void  on_viewer_film_strip_pos_changed  (GtkComboBox *widget, gpointer user_data)
{
	//PreferencesDlg::PreferencesDlgPriv *priv = static_cast<PreferencesDlg::PreferencesDlgPriv*>(user_data);

	PreferencesPtr prefs = Preferences::GetInstance();
	
	gint iFilmstripPos = gtk_combo_box_get_active(widget);
	prefs->SetInteger(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_FILMSTRIP_POSITION, iFilmstripPos);
}


static void  on_color_set(GtkWidget* widget, gpointer user_data)
{
	PreferencesDlg::PreferencesDlgPriv *priv = static_cast<PreferencesDlg::PreferencesDlgPriv*>(user_data);
	PreferencesPtr prefs = Preferences::GetInstance();
	if (GTK_IS_COLOR_BUTTON(widget))
	{
		GtkColorButton *button = GTK_COLOR_BUTTON(widget);
		GdkColor color;
		gtk_color_button_get_color (button, &color); 
		char szColor[10];
		g_snprintf (szColor,10,"#%02x%02x%02x",color.red/256,color.green/256,color.blue/256);
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
{
	parent->UpdateUI();
}



