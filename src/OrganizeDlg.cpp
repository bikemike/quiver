#include <config.h>
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "OrganizeDlg.h"
#include "OrganizeTask.h"
#include "RenameTask.h"

#include "QuiverPrefs.h"
#include "Preferences.h"

#include "QuiverStockIcons.h"

#include <gio/gio.h>

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

	bool GetRenameFiles() const;

// variables
	OrganizeDlg*         m_pOrganizeDlg;
	GtkBuilder*            m_pGtkBuilder;
	bool m_bLoadedDlg;
	

	// dlg widgets
	GtkDialog*              m_pDialogOrganize;

	GtkWidget*              m_pBtnOK;

	GtkComboBoxText*        m_pComboTemplateFolder;
	GtkEntry*               m_pEntryTemplateFile;
	GtkFileChooserButton*   m_pFCBtnSourceFolder;
	GtkFileChooserButton*   m_pFCBtnDestFolder;
	GtkToggleButton*        m_pTglBtnSubfolders;
	GtkToggleButton*        m_pTglBtnRenameFiles;
	//GtkToggleButton*        m_pTglBtnCurrentSelection;
	//GtkToggleButton*        m_pTglBtnFolder;
	GtkSpinButton*          m_pSpinExtension;

	GtkEntry*               m_pEntryFolderName;
	GtkLabel*               m_pLabelExample;

	//GtkToggleButton*        m_pTglBtnCopy;
	//GtkToggleButton*        m_pTglBtnMove;
};


OrganizeDlg::OrganizeDlg()
{
	OrganizeDlgPrivPtr ptr(new OrganizeDlgPriv(this));
	m_PrivPtr = ptr;
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

std::string OrganizeDlg::GetFolderTemplate() const
{
	return gtk_combo_box_text_get_active_text(m_PrivPtr->m_pComboTemplateFolder);
}

std::string OrganizeDlg::GetFileTemplate() const
{
	return gtk_entry_get_text(m_PrivPtr->m_pEntryTemplateFile);
}

std::string OrganizeDlg::GetOutputFolder() const
{
	std::string strDir;

	gchar* dir = gtk_file_chooser_get_uri (
				GTK_FILE_CHOOSER (m_PrivPtr->m_pFCBtnDestFolder));
	if (NULL != dir)
	{
		strDir = dir;
		g_free(dir);
	}

	return strDir;
}

std::string OrganizeDlg::GetAppendedText() const
{
	return gtk_entry_get_text(m_PrivPtr->m_pEntryFolderName);
}

std::string OrganizeDlg::GetInputFolder() const
{
	std::string strDir;

	gchar* dir = gtk_file_chooser_get_uri (
				GTK_FILE_CHOOSER (m_PrivPtr->m_pFCBtnSourceFolder));
	if (NULL != dir)
	{
		strDir = dir;
		g_free(dir);
	}

	return strDir;

}

void OrganizeDlg::SetInputFolder(std::string dir)
{
	gtk_file_chooser_set_current_folder_uri(GTK_FILE_CHOOSER(m_PrivPtr->m_pFCBtnSourceFolder), dir.c_str());
}


int OrganizeDlg::GetDayExtention() const
{
	return gtk_spin_button_get_value_as_int (m_PrivPtr->m_pSpinExtension);
}

bool OrganizeDlg::GetIncludeSubfolders() const
{
	return (TRUE == gtk_toggle_button_get_active(m_PrivPtr->m_pTglBtnSubfolders));
}

bool OrganizeDlg::GetRenameFiles() const
{
	return m_PrivPtr->GetRenameFiles();
}



// private stuff


// prototypes
static void  on_clicked (GtkButton *button, gpointer   user_data);
static void on_folder_change (GtkFileChooser *chooser, gpointer user_data);
static void __attribute__((unused))  on_toggled (GtkToggleButton *togglebutton, gpointer user_data);
static void on_editable_changed (GtkEditable *editable, gpointer user_data);
static void combo_changed (GtkComboBox *widget, gpointer user_data);


OrganizeDlg::OrganizeDlgPriv::OrganizeDlgPriv(OrganizeDlg *parent) :
        m_pOrganizeDlg(parent)
{
	m_pDialogOrganize = NULL;
	m_pGtkBuilder = gtk_builder_new();
	gchar* objectids[] = {
		(gchar*)"OrganizeDialog",
		(gchar*)"adjustment8",
		(gchar*)"liststore3",
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

	m_pBtnOK = gtk_dialog_add_button(m_pDialogOrganize, "_OK", GTK_RESPONSE_OK);

	m_pComboTemplateFolder       = GTK_COMBO_BOX_TEXT( gtk_builder_get_object(m_pGtkBuilder, "organize_combo_template") );
	m_pEntryTemplateFile       = GTK_ENTRY( gtk_builder_get_object(m_pGtkBuilder, "organize_entry_filename_template") );
	//m_pTglBtnCurrentSelection = GTK_TOGGLE_BUTTON( gtk_builder_get_object(m_pGtkBuilder, "organize_rb_current_selection") );
	//m_pTglBtnFolder           = GTK_TOGGLE_BUTTON( gtk_builder_get_object(m_pGtkBuilder, "organize_rb_folder") );
	//m_pTglBtnCopy             = GTK_TOGGLE_BUTTON( gtk_builder_get_object(m_pGtkBuilder, "organize_rb_copy") );
	//m_pTglBtnMove             = GTK_TOGGLE_BUTTON( gtk_builder_get_object(m_pGtkBuilder, "organize_rb_move") );
	m_pSpinExtension            = GTK_SPIN_BUTTON( gtk_builder_get_object(m_pGtkBuilder, "organize_spinbutton_day_offset") );

	m_pTglBtnSubfolders       = GTK_TOGGLE_BUTTON( gtk_builder_get_object(m_pGtkBuilder, "organize_cb_subfolders") );
	m_pTglBtnRenameFiles      = GTK_TOGGLE_BUTTON( gtk_builder_get_object(m_pGtkBuilder, "organize_cb_rename_files") );

	GtkContainer* src_cont = GTK_CONTAINER( gtk_builder_get_object(m_pGtkBuilder, "organize_align_source_folder") );
	GtkContainer* dst_cont = GTK_CONTAINER( gtk_builder_get_object(m_pGtkBuilder, "organize_align_dest_folder") );
		m_pFCBtnSourceFolder = GTK_FILE_CHOOSER_BUTTON(gtk_file_chooser_button_new ("Choose Source Folder", GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER));
		gtk_file_chooser_set_local_only(GTK_FILE_CHOOSER(m_pFCBtnSourceFolder), FALSE);
		m_pFCBtnDestFolder = GTK_FILE_CHOOSER_BUTTON(gtk_file_chooser_button_new ("Choose Destination Folder", GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER));
		gtk_file_chooser_set_local_only(GTK_FILE_CHOOSER(m_pFCBtnDestFolder), FALSE);
		gtk_widget_show(GTK_WIDGET(m_pFCBtnSourceFolder));
		gtk_widget_show(GTK_WIDGET(m_pFCBtnDestFolder));
		
		gtk_container_add(src_cont, GTK_WIDGET(m_pFCBtnSourceFolder));
		gtk_container_add(dst_cont, GTK_WIDGET(m_pFCBtnDestFolder));
	m_pEntryFolderName        = GTK_ENTRY( gtk_builder_get_object(m_pGtkBuilder, "organize_entry_folder_name") );

	m_pLabelExample           = GTK_LABEL( gtk_builder_get_object(m_pGtkBuilder, "organize_label_example_output") );

	m_bLoadedDlg = (
		NULL != m_pDialogOrganize        &&
		NULL != m_pComboTemplateFolder   &&
		NULL != m_pEntryTemplateFile     &&
		//NULL != m_pTglBtnCurrentSelection&&
		//NULL != m_pTglBtnFolder          &&
		//NULL != m_pTglBtnCopy            &&
		//NULL != m_pTglBtnMove            &&
		NULL != m_pSpinExtension         &&
		NULL != m_pTglBtnSubfolders      &&
		NULL != m_pTglBtnRenameFiles     &&
		NULL != m_pFCBtnSourceFolder     &&
		NULL != m_pFCBtnDestFolder       &&
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

		gtk_combo_box_set_active(GTK_COMBO_BOX(m_pComboTemplateFolder), 0);


		PreferencesPtr prefs = Preferences::GetInstance();
		std::string strPhotoLibrary = prefs->GetString(QUIVER_PREFS_APP,QUIVER_PREFS_APP_PHOTO_LIBRARY);
		if (!strPhotoLibrary.empty())
		{
			gtk_file_chooser_set_current_folder_uri (
				GTK_FILE_CHOOSER (m_pFCBtnDestFolder),
				strPhotoLibrary.c_str());
		}
	}  
}

void OrganizeDlg::OrganizeDlgPriv::UpdateUI()
{
	if (m_bLoadedDlg)
	{
		std::string strLabel;
		gchar* dir = gtk_file_chooser_get_uri (
				GTK_FILE_CHOOSER (m_pFCBtnDestFolder));

		if (NULL != dir)
		{
			// directory name
			strLabel = dir;
			g_free(dir);
		}
		GDateTime* time = g_date_time_new_now_local();

		strLabel += G_DIR_SEPARATOR_S;
		strLabel += gtk_combo_box_text_get_active_text(m_pComboTemplateFolder);
		strLabel += gtk_entry_get_text(m_pEntryFolderName);
		strLabel = OrganizeTask::DoVariableSubstitution(strLabel, time);
		if (GetRenameFiles())
		{
			strLabel += G_DIR_SEPARATOR_S;
			std::string strFileName = gtk_entry_get_text(m_pEntryTemplateFile);
			strFileName = RenameTask::DoVariableSubstitution(strFileName, time, 1);
			strLabel += strFileName;
			strLabel += ".jpg";
		}
		g_date_time_unref(time);
		gtk_label_set_text(m_pLabelExample, strLabel.c_str());

		gtk_widget_set_sensitive(GTK_WIDGET(m_pEntryTemplateFile), GetRenameFiles() ? TRUE : FALSE);

	}
}




void OrganizeDlg::OrganizeDlgPriv::ConnectSignals()
{
	if (m_bLoadedDlg)
	{
		g_signal_connect(m_pFCBtnSourceFolder,
			"current-folder-changed",(GCallback)on_folder_change,this);
		g_signal_connect(m_pFCBtnDestFolder,
			"current-folder-changed",(GCallback)on_folder_change,this);

		g_signal_connect(m_pBtnOK,
			"clicked",(GCallback)on_clicked,this);

		g_signal_connect(m_pTglBtnRenameFiles,
			"clicked",(GCallback)on_clicked,this);

		/*
		g_signal_connect(m_pTglBtnCurrentSelection,
			"toggled",(GCallback)on_toggled,this);

		g_signal_connect(m_pTglBtnFolder,
			"toggled",(GCallback)on_toggled,this);
			*/

		g_signal_connect(m_pEntryFolderName,
			"changed",(GCallback)on_editable_changed,this);

		g_signal_connect(m_pComboTemplateFolder,
			"changed",(GCallback)combo_changed,this);

		g_signal_connect(m_pEntryTemplateFile,
			"changed",(GCallback)on_editable_changed,this);

	}
	
}

bool OrganizeDlg::OrganizeDlgPriv::ValidateInput()
{
	bool bIsValid = true;

	// make sure source and dest directories are 
	// in separate locations
	
	gchar* src_uri = gtk_file_chooser_get_uri (
				GTK_FILE_CHOOSER (m_pFCBtnSourceFolder));

	gchar* dst_uri = gtk_file_chooser_get_uri (
				GTK_FILE_CHOOSER (m_pFCBtnDestFolder));

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

	if (NULL != src_uri)
	{
		g_free(src_uri);
	}
	if (NULL != dst_uri)
	{
		g_free(dst_uri);
	}

	return bIsValid;
}

bool OrganizeDlg::OrganizeDlgPriv::GetRenameFiles() const
{
	return (TRUE == gtk_toggle_button_get_active(m_pTglBtnRenameFiles));
}

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
	else if (button == GTK_BUTTON(priv->m_pTglBtnRenameFiles))
	{
		priv->UpdateUI();
	}
}


static void __attribute__((unused))  on_toggled (GtkToggleButton *togglebutton, gpointer user_data)
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




