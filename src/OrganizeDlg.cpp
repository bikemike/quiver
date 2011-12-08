#include <config.h>
#include "OrganizeDlg.h"

#include "QuiverPrefs.h"
#include "Preferences.h"

#include "QuiverStockIcons.h"

#include <gio/gio.h>

#ifdef QUIVER_MAEMO
#ifdef HAVE_HILDON_FM_2
#include <hildon/hildon-file-chooser-dialog.h>
#else
#include <hildon-widgets/hildon-file-chooser-dialog.h>
#endif
#endif

class OrganizeDlg::OrganizeDlgPriv
{
public:
// constructor, destructor
	OrganizeDlgPriv(OrganizeDlg *parent);
	~OrganizeDlgPriv();
	
// methods
	void LoadWidgets();
	void UpdateUI();
	void ConnectSignals();
	
	bool ValidateInput();

// variables
	OrganizeDlg*         m_pOrganizeDlg;
	GtkBuilder*            m_pGtkBuilder;
	bool m_bLoadedDlg;
	

	// dlg widgets
	GtkDialog*              m_pDialogOrganize;

	GtkWidget*              m_pBtnOK;

	GtkComboBox*            m_pComboTemplate;
#ifdef QUIVER_MAEMO
	GtkButton*              m_pBtnSourceFolder;
	GtkButton*              m_pBtnDestFolder;
#else
	GtkFileChooserButton*   m_pFCBtnSourceFolder;
	GtkFileChooserButton*   m_pFCBtnDestFolder;
#endif
	GtkToggleButton*        m_pTglBtnSubfolders;
	//GtkToggleButton*        m_pTglBtnCurrentSelection;
	//GtkToggleButton*        m_pTglBtnFolder;
	GtkSpinButton*          m_pSpinExtension;

	GtkEntry*               m_pEntryFolderName;
	GtkLabel*               m_pLabelExample;

	//GtkToggleButton*        m_pTglBtnCopy;
	//GtkToggleButton*        m_pTglBtnMove;
};


OrganizeDlg::OrganizeDlg() : m_PrivPtr(new OrganizeDlg::OrganizeDlgPriv(this))
{
	
}


GtkWidget* OrganizeDlg::GetWidget() const
{
	  return NULL;
}


bool OrganizeDlg::Run()
{
	if (m_PrivPtr->m_bLoadedDlg)
	{
		gint result = gtk_dialog_run(GTK_DIALOG(m_PrivPtr->m_pDialogOrganize));
		return (GTK_RESPONSE_OK == result);
	}
	return false;
}

std::string OrganizeDlg::GetDateTemplate() const
{
	return gtk_combo_box_get_active_text(m_PrivPtr->m_pComboTemplate);
}

std::string OrganizeDlg::GetOutputFolder() const
{
	std::string strDir;

#ifdef QUIVER_MAEMO
	const gchar* dir = gtk_button_get_label ( GTK_BUTTON (m_PrivPtr->m_pBtnDestFolder));
	strDir = dir;
#else
	gchar* dir = gtk_file_chooser_get_uri (
				GTK_FILE_CHOOSER (m_PrivPtr->m_pFCBtnDestFolder));
	if (NULL != dir)
	{
		strDir = dir;
		g_free(dir);
	}
#endif

	return strDir;
}

std::string OrganizeDlg::GetAppendedText() const
{
	return gtk_entry_get_text(m_PrivPtr->m_pEntryFolderName);
}

std::string OrganizeDlg::GetInputFolder() const
{
	std::string strDir;

#ifdef QUIVER_MAEMO
	const gchar* dir = gtk_button_get_label ( GTK_BUTTON (m_PrivPtr->m_pBtnSourceFolder));
	strDir = dir;
#else
	gchar* dir = gtk_file_chooser_get_uri (
				GTK_FILE_CHOOSER (m_PrivPtr->m_pFCBtnSourceFolder));
	if (NULL != dir)
	{
		strDir = dir;
		g_free(dir);
	}
#endif

	return strDir;

}

int OrganizeDlg::GetDayExtention() const
{
	return gtk_spin_button_get_value_as_int (m_PrivPtr->m_pSpinExtension);
}

bool OrganizeDlg::GetIncludeSubfolders() const
{
	return (TRUE == gtk_toggle_button_get_active(m_PrivPtr->m_pTglBtnSubfolders));
}


// private stuff


// prototypes
static void  on_clicked (GtkButton *button, gpointer   user_data);
#ifndef QUIVER_MAEMO
static void on_folder_change (GtkFileChooser *chooser, gpointer user_data);
#endif
static void  on_toggled (GtkToggleButton *togglebutton, gpointer user_data);
static void on_editable_changed (GtkEditable *editable, gpointer user_data);
static void combo_changed (GtkComboBox *widget, gpointer user_data);


OrganizeDlg::OrganizeDlgPriv::OrganizeDlgPriv(OrganizeDlg *parent) :
        m_pOrganizeDlg(parent)
{
	m_pDialogOrganize = NULL;
	m_pGtkBuilder = gtk_builder_new();
	gchar* objectids[] = {
		"OrganizeDialog",
		"adjustment8",
		"liststore3",
		NULL};
	gtk_builder_add_objects_from_file(m_pGtkBuilder, QUIVER_DATADIR "/" "quiver.ui", objectids, NULL);

	LoadWidgets();
	UpdateUI();
	ConnectSignals();
}

OrganizeDlg::OrganizeDlgPriv::~OrganizeDlgPriv()
{
	if (NULL != m_pGtkBuilder)
	{
		g_object_unref(m_pGtkBuilder);
		m_pGtkBuilder = NULL;
	}

	if (NULL != m_pDialogOrganize)
	{
		gtk_widget_destroy(GTK_WIDGET(m_pDialogOrganize));
		m_pDialogOrganize = NULL;
	}
}


void OrganizeDlg::OrganizeDlgPriv::LoadWidgets()
{
	m_pDialogOrganize         = GTK_DIALOG(gtk_builder_get_object (m_pGtkBuilder, "OrganizeDialog"));

	m_pBtnOK               = gtk_button_new_from_stock(QUIVER_STOCK_OK);
	gtk_widget_show(m_pBtnOK);
	gtk_container_add(GTK_CONTAINER(m_pDialogOrganize->action_area),m_pBtnOK);

	m_pComboTemplate          = GTK_COMBO_BOX( gtk_builder_get_object(m_pGtkBuilder, "organize_combo_template") );
	//m_pTglBtnCurrentSelection = GTK_TOGGLE_BUTTON( gtk_builder_get_object(m_pGtkBuilder, "organize_rb_current_selection") );
	//m_pTglBtnFolder           = GTK_TOGGLE_BUTTON( gtk_builder_get_object(m_pGtkBuilder, "organize_rb_folder") );
	//m_pTglBtnCopy             = GTK_TOGGLE_BUTTON( gtk_builder_get_object(m_pGtkBuilder, "organize_rb_copy") );
	//m_pTglBtnMove             = GTK_TOGGLE_BUTTON( gtk_builder_get_object(m_pGtkBuilder, "organize_rb_move") );
	m_pSpinExtension            = GTK_SPIN_BUTTON( gtk_builder_get_object(m_pGtkBuilder, "organize_spinbutton_day_offset") );

	m_pTglBtnSubfolders       = GTK_TOGGLE_BUTTON( gtk_builder_get_object(m_pGtkBuilder, "organize_cb_subfolders") );

	GtkContainer* src_cont = GTK_CONTAINER( gtk_builder_get_object(m_pGtkBuilder, "organize_align_source_folder") );
	GtkContainer* dst_cont = GTK_CONTAINER( gtk_builder_get_object(m_pGtkBuilder, "organize_align_dest_folder") );
#ifdef QUIVER_MAEMO
		m_pBtnSourceFolder = GTK_BUTTON( gtk_button_new() );
		m_pBtnDestFolder = GTK_BUTTON( gtk_button_new() );
		gtk_widget_show(GTK_WIDGET(m_pBtnSourceFolder));
		gtk_widget_show(GTK_WIDGET(m_pBtnDestFolder));
		gtk_container_add(src_cont, GTK_WIDGET(m_pBtnSourceFolder));
		gtk_container_add(dst_cont, GTK_WIDGET(m_pBtnDestFolder));
#else
		m_pFCBtnSourceFolder = GTK_FILE_CHOOSER_BUTTON(gtk_file_chooser_button_new ("Choose Source Folder", GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER));
		m_pFCBtnDestFolder = GTK_FILE_CHOOSER_BUTTON(gtk_file_chooser_button_new ("Choose Destination Folder", GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER));
		gtk_widget_show(GTK_WIDGET(m_pFCBtnSourceFolder));
		gtk_widget_show(GTK_WIDGET(m_pFCBtnDestFolder));
		
		gtk_container_add(src_cont, GTK_WIDGET(m_pFCBtnSourceFolder));
		gtk_container_add(dst_cont, GTK_WIDGET(m_pFCBtnDestFolder));
#endif
	m_pEntryFolderName        = GTK_ENTRY( gtk_builder_get_object(m_pGtkBuilder, "organize_entry_folder_name") );

	m_pLabelExample           = GTK_LABEL( gtk_builder_get_object(m_pGtkBuilder, "organize_label_example_output") );

	m_bLoadedDlg = (
		NULL != m_pDialogOrganize        &&
		NULL != m_pComboTemplate         &&
		//NULL != m_pTglBtnCurrentSelection&&
		//NULL != m_pTglBtnFolder          &&
		//NULL != m_pTglBtnCopy            &&
		//NULL != m_pTglBtnMove            &&
		NULL != m_pSpinExtension         &&
		NULL != m_pTglBtnSubfolders      &&
#ifdef QUIVER_MAEMO
		NULL != m_pBtnSourceFolder     &&
		NULL != m_pBtnDestFolder       &&
#else
		NULL != m_pFCBtnSourceFolder     &&
		NULL != m_pFCBtnDestFolder       &&
#endif
		NULL != m_pEntryFolderName       &&
		NULL != m_pLabelExample         ); 

	if (m_bLoadedDlg)
	{
		PangoAttrList* attrs = pango_attr_list_new();
		PangoAttribute* attr = pango_attr_scale_new (PANGO_SCALE_SMALL);
		pango_attr_list_insert(attrs,attr);

		gtk_label_set_attributes(m_pLabelExample, attrs);
		pango_attr_list_unref(attrs);

		gtk_window_set_default_size(GTK_WINDOW(m_pDialogOrganize), 400,-1);

		gtk_combo_box_set_active(m_pComboTemplate, 0);


#ifdef QUIVER_MAEMO
		char* cwd = g_get_current_dir();
		gtk_button_set_label(m_pBtnSourceFolder, cwd);
		g_free(cwd);
#endif

		PreferencesPtr prefs = Preferences::GetInstance();
		std::string strPhotoLibrary = prefs->GetString(QUIVER_PREFS_APP,QUIVER_PREFS_APP_PHOTO_LIBRARY);
		if (!strPhotoLibrary.empty())
		{
#ifdef QUIVER_MAEMO
			GnomeVFSURI* vuri= gnome_vfs_uri_new(strPhotoLibrary.c_str());
			if (gnome_vfs_uri_is_local(vuri))
			{
				char* localdir = gnome_vfs_get_local_path_from_uri (strPhotoLibrary.c_str());
				if (NULL != localdir)
				{
					strPhotoLibrary = localdir;
					g_free(localdir);
				}
			}
			gnome_vfs_uri_unref(vuri);
			gtk_button_set_label(m_pBtnDestFolder, strPhotoLibrary.c_str());
#else
			gtk_file_chooser_set_current_folder_uri (
				GTK_FILE_CHOOSER (m_pFCBtnDestFolder),
				strPhotoLibrary.c_str());
#endif
		}
	}  
}

void OrganizeDlg::OrganizeDlgPriv::UpdateUI()
{
	if (m_bLoadedDlg)
	{
#ifdef QUIVER_MAEMO
		const gchar* dir = gtk_button_get_label ( GTK_BUTTON (m_pBtnDestFolder));
		std::string strLabel = dir;
		strLabel += G_DIR_SEPARATOR_S;
		strLabel += gtk_combo_box_get_active_text(m_pComboTemplate);
		strLabel += gtk_entry_get_text(m_pEntryFolderName);
		gtk_label_set_text(m_pLabelExample, strLabel.c_str());
#else
		gchar* dir = gtk_file_chooser_get_uri (
				GTK_FILE_CHOOSER (m_pFCBtnDestFolder));

		if (NULL != dir)
		{
			// FIXME - use proper glib functions to construct the 
			// directory name
			std::string strLabel = dir;
			strLabel += G_DIR_SEPARATOR_S;
			strLabel += gtk_combo_box_get_active_text(m_pComboTemplate);
			strLabel += gtk_entry_get_text(m_pEntryFolderName);
			gtk_label_set_text(m_pLabelExample, strLabel.c_str());
			g_free(dir);
		}
#endif
	}
}




void OrganizeDlg::OrganizeDlgPriv::ConnectSignals()
{
	if (m_bLoadedDlg)
	{
#ifdef QUIVER_MAEMO
		g_signal_connect(m_pBtnSourceFolder,
			"clicked",(GCallback)on_clicked,this);
		g_signal_connect(m_pBtnDestFolder,
			"clicked",(GCallback)on_clicked,this);
#else
		g_signal_connect(m_pFCBtnSourceFolder,
			"current-folder-changed",(GCallback)on_folder_change,this);
		g_signal_connect(m_pFCBtnDestFolder,
			"current-folder-changed",(GCallback)on_folder_change,this);
#endif

		g_signal_connect(m_pBtnOK,
			"clicked",(GCallback)on_clicked,this);

		/*
		g_signal_connect(m_pTglBtnCurrentSelection,
			"toggled",(GCallback)on_toggled,this);

		g_signal_connect(m_pTglBtnFolder,
			"toggled",(GCallback)on_toggled,this);
			*/

		g_signal_connect(m_pEntryFolderName,
			"changed",(GCallback)on_editable_changed,this);

		g_signal_connect(m_pComboTemplate,
			"changed",(GCallback)combo_changed,this);

	}
	
}

bool OrganizeDlg::OrganizeDlgPriv::ValidateInput()
{
	bool bIsValid = true;

	// make sure source and dest directories are 
	// in separate locations
	
#ifdef QUIVER_MAEMO
	const gchar* src_uri = gtk_button_get_label (m_pBtnSourceFolder);

	const gchar* dst_uri = gtk_button_get_label (m_pBtnDestFolder);
#else
	gchar* src_uri = gtk_file_chooser_get_uri (
				GTK_FILE_CHOOSER (m_pFCBtnSourceFolder));

	gchar* dst_uri = gtk_file_chooser_get_uri (
				GTK_FILE_CHOOSER (m_pFCBtnDestFolder));
#endif

	if (NULL != src_uri && NULL != dst_uri)
	{
		GFile* file_src = g_file_new_for_uri(src_uri);
		GFile* file_dst = g_file_new_for_uri(dst_uri);

		gboolean source_is_parent = 
			g_file_has_parent(file_dst, file_src);

		gboolean source_is_child = 
			g_file_has_parent (file_src, file_dst);

		gboolean source_is_dst = 
			g_file_equal(file_src, file_dst);

		g_object_unref(file_src);
		g_object_unref(file_dst);
		
		if ( (source_is_parent && m_pOrganizeDlg->GetIncludeSubfolders()) || source_is_child || source_is_dst)
		{
			bIsValid = false;

			GtkWidget* dialog = gtk_message_dialog_new (GTK_WINDOW(m_pDialogOrganize),
				GTK_DIALOG_DESTROY_WITH_PARENT,
				GTK_MESSAGE_ERROR,
				GTK_BUTTONS_CLOSE,
				"Source and Destination folders overlap. Please choose a different destination folder.");
			gtk_window_set_title(GTK_WINDOW(dialog), "Folder Conflict");
			gtk_dialog_run (GTK_DIALOG (dialog));
			gtk_widget_destroy (dialog);
		}
	}

#ifdef QUIVER_MAEMO
#else
	if (NULL != src_uri)
	{
		g_free(src_uri);
	}
	if (NULL != dst_uri)
	{
		g_free(dst_uri);
	}
#endif

	return bIsValid;
}

static void  on_clicked (GtkButton *button, gpointer   user_data)
{
	OrganizeDlg::OrganizeDlgPriv *priv = static_cast<OrganizeDlg::OrganizeDlgPriv*>(user_data);
	if (GTK_BUTTON(priv->m_pBtnOK) == button)
	{
		if (priv->ValidateInput())
		{
			gtk_dialog_response(priv->m_pDialogOrganize, GTK_RESPONSE_OK);
		}
	}

#ifdef QUIVER_MAEMO
	if (priv->m_pBtnSourceFolder == button || priv->m_pBtnDestFolder == button)
	{ 
		// photo library
		GtkWidget* dlg = hildon_file_chooser_dialog_new(NULL, GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);
		gchar* uri = gnome_vfs_make_uri_from_input(
			gtk_button_get_label(button));
		gtk_file_chooser_set_current_folder_uri (
			GTK_FILE_CHOOSER (dlg),
			uri);
		g_free(uri);

		gint response = gtk_dialog_run(GTK_DIALOG(dlg));
		
		if (GTK_RESPONSE_OK == response)
		{
			gchar* dir = gtk_file_chooser_get_uri (
				GTK_FILE_CHOOSER (dlg));
			if (NULL != dir)
			{
				GnomeVFSURI* vuri= gnome_vfs_uri_new(dir);
				if (gnome_vfs_uri_is_local(vuri))
				{
					char* localdir = gnome_vfs_get_local_path_from_uri (dir);
					if (NULL != localdir)
					{
						g_free(dir);
						dir = localdir;
					}
				}
				
				gtk_button_set_label(button, dir);

				gnome_vfs_uri_unref(vuri);
				g_free(dir);
			}
		}

		gtk_widget_destroy(dlg);
	}
#endif
}

#ifndef QUIVER_MAEMO
void on_folder_change (GtkFileChooser *chooser, gpointer user_data)
{
	OrganizeDlg::OrganizeDlgPriv *priv = static_cast<OrganizeDlg::OrganizeDlgPriv*>(user_data);
	
	if (GTK_FILE_CHOOSER(priv->m_pFCBtnSourceFolder) == chooser)
	{
		priv->UpdateUI();
	}
	else if (GTK_FILE_CHOOSER(priv->m_pFCBtnDestFolder) == chooser)
	{
		priv->UpdateUI();
	}
}
#endif


static void  on_toggled (GtkToggleButton *togglebutton, gpointer user_data)
{
	OrganizeDlg::OrganizeDlgPriv *priv = static_cast<OrganizeDlg::OrganizeDlgPriv*>(user_data);
	priv->UpdateUI();
}

static void on_editable_changed (GtkEditable *editable, gpointer user_data)
{
	OrganizeDlg::OrganizeDlgPriv *priv = static_cast<OrganizeDlg::OrganizeDlgPriv*>(user_data);
	priv->UpdateUI();
}

void combo_changed (GtkComboBox *widget, gpointer user_data)
{
	OrganizeDlg::OrganizeDlgPriv *priv = static_cast<OrganizeDlg::OrganizeDlgPriv*>(user_data);
	priv->UpdateUI();
}




