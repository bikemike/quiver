#include <config.h>
#include "OrganizeDlg.h"
#include "OrganizeTask.h"
#include "RenameTask.h"

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
	GtkButton*              m_pBtnSourceFolder;
	GtkLabel*               m_pLabelSourceFolder;
	GtkButton*              m_pBtnDestFolder;
	GtkLabel*               m_pLabelDestFolder;
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
		// In GTK4, dialogs are non-blocking. You'd connect to the "response"
        // signal and handle the result there. The Run() method can't
        // return the result directly in the same way.
        // For now, we'll just show the dialog.
        gtk_window_present(GTK_WINDOW(m_PrivPtr->m_pDialogOrganize));
        return true; // Placeholder
	}
	return false;
}

std::string OrganizeDlg::GetFolderTemplate() const
{
	return gtk_combo_box_get_active_id(GTK_COMBO_BOX(m_PrivPtr->m_pComboTemplateFolder));
}

std::string OrganizeDlg::GetFileTemplate() const
{
	return gtk_editable_get_text(GTK_EDITABLE(m_PrivPtr->m_pEntryTemplateFile));
}

std::string OrganizeDlg::GetOutputFolder() const
{
	return gtk_label_get_text(m_PrivPtr->m_pLabelDestFolder);
}

std::string OrganizeDlg::GetAppendedText() const
{
	return gtk_editable_get_text(GTK_EDITABLE(m_PrivPtr->m_pEntryFolderName));
}

std::string OrganizeDlg::GetInputFolder() const
{
	return gtk_label_get_text(m_PrivPtr->m_pLabelSourceFolder);
}

void OrganizeDlg::SetInputFolder(std::string dir)
{
	gtk_label_set_text(m_PrivPtr->m_pLabelSourceFolder, dir.c_str());
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
static void on_folder_button_clicked(GtkButton* button, gpointer user_data);
static void on_editable_changed (GtkEditable *editable, gpointer user_data);
static void combo_changed (GtkComboBox *widget, gpointer user_data);


OrganizeDlg::OrganizeDlgPriv::OrganizeDlgPriv(OrganizeDlg *parent) :
        m_pOrganizeDlg(parent)
{
	m_pDialogOrganize = NULL;
	m_pGtkBuilder = gtk_builder_new_from_file(QUIVER_DATADIR "/" "quiver.ui");

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
		gtk_window_destroy(GTK_WINDOW(m_pDialogOrganize));
		m_pDialogOrganize = NULL;
	}
}


void OrganizeDlg::OrganizeDlgPriv::LoadWidgets()
{
	m_pDialogOrganize         = GTK_DIALOG(gtk_builder_get_object (m_pGtkBuilder, "OrganizeDialog"));

	m_pBtnOK               = GTK_WIDGET(gtk_button_new_with_label("OK"));
    gtk_button_set_icon_name(GTK_BUTTON(m_pBtnOK), "dialog-ok");
	gtk_dialog_add_buttons(GTK_DIALOG(m_pDialogOrganize), "_OK", GTK_RESPONSE_OK, "_Cancel", GTK_RESPONSE_CANCEL, NULL);

	m_pComboTemplateFolder       = GTK_COMBO_BOX_TEXT( gtk_builder_get_object(m_pGtkBuilder, "organize_combo_template") );
	m_pEntryTemplateFile       = GTK_ENTRY( gtk_builder_get_object(m_pGtkBuilder, "organize_entry_filename_template") );
	m_pSpinExtension            = GTK_SPIN_BUTTON( gtk_builder_get_object(m_pGtkBuilder, "organize_spinbutton_day_offset") );

	m_pTglBtnSubfolders       = GTK_TOGGLE_BUTTON( gtk_builder_get_object(m_pGtkBuilder, "organize_cb_subfolders") );
	m_pTglBtnRenameFiles      = GTK_TOGGLE_BUTTON( gtk_builder_get_object(m_pGtkBuilder, "organize_cb_rename_files") );

	GtkBox* src_cont = GTK_BOX( gtk_builder_get_object(m_pGtkBuilder, "organize_align_source_folder") );
	GtkBox* dst_cont = GTK_BOX( gtk_builder_get_object(m_pGtkBuilder, "organize_align_dest_folder") );
    m_pBtnSourceFolder = (GtkButton*)gtk_button_new_with_label("Choose Source Folder");
    m_pLabelSourceFolder = (GtkLabel*)gtk_label_new("");
    m_pBtnDestFolder = (GtkButton*)gtk_button_new_with_label("Choose Destination Folder");
    m_pLabelDestFolder = (GtkLabel*)gtk_label_new("");

    gtk_box_append(src_cont, (GtkWidget*)m_pBtnSourceFolder);
    gtk_box_append(src_cont, (GtkWidget*)m_pLabelSourceFolder);
    gtk_box_append(dst_cont, (GtkWidget*)m_pBtnDestFolder);
    gtk_box_append(dst_cont, (GtkWidget*)m_pLabelDestFolder);
	m_pEntryFolderName        = GTK_ENTRY( gtk_builder_get_object(m_pGtkBuilder, "organize_entry_folder_name") );

	m_pLabelExample           = GTK_LABEL( gtk_builder_get_object(m_pGtkBuilder, "organize_label_example_output") );

	m_bLoadedDlg = (
		NULL != m_pDialogOrganize        &&
		NULL != m_pComboTemplateFolder   &&
		NULL != m_pEntryTemplateFile     &&
		NULL != m_pSpinExtension         &&
		NULL != m_pTglBtnSubfolders      &&
		NULL != m_pTglBtnRenameFiles     &&
		NULL != m_pBtnSourceFolder     &&
		NULL != m_pBtnDestFolder       &&
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


#ifdef QUIVER_MAEMO
		char* cwd = g_get_current_dir();
		gtk_button_set_label(m_pBtnSourceFolder, cwd);
		g_free(cwd);
#endif

		PreferencesPtr prefs = Preferences::GetInstance();
		std::string strPhotoLibrary = prefs->GetString(QUIVER_PREFS_APP,QUIVER_PREFS_APP_PHOTO_LIBRARY);
		if (!strPhotoLibrary.empty())
		{
			GFile *file = g_file_new_for_path(strPhotoLibrary.c_str());
			gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(m_pBtnDestFolder), file, NULL);
			g_object_unref(file);
		}
	}  
}

void OrganizeDlg::OrganizeDlgPriv::UpdateUI()
{
	if (m_bLoadedDlg)
	{
		std::string strLabel = gtk_label_get_text(m_pLabelDestFolder);
		GDateTime* time = g_date_time_new_now_local();

		strLabel += G_DIR_SEPARATOR_S;
		strLabel += gtk_combo_box_get_active_id(GTK_COMBO_BOX(m_pComboTemplateFolder));
		strLabel += gtk_editable_get_text(GTK_EDITABLE(m_pEntryFolderName));
		strLabel = OrganizeTask::DoVariableSubstitution(strLabel, time);
		if (GetRenameFiles())
		{
			strLabel += G_DIR_SEPARATOR_S;
			std::string strFileName = gtk_editable_get_text(GTK_EDITABLE(m_pEntryTemplateFile));
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
		g_signal_connect(m_pBtnSourceFolder,
			"clicked",(GCallback)on_folder_button_clicked,this);
		g_signal_connect(m_pBtnDestFolder,
			"clicked",(GCallback)on_folder_button_clicked,this);

		g_signal_connect(m_pBtnOK,
			"clicked",(GCallback)on_clicked,this);

		g_signal_connect(m_pTglBtnRenameFiles,
			"clicked",(GCallback)on_clicked,this);

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
	
	const gchar* src_uri = gtk_label_get_text(m_pLabelSourceFolder);
	const gchar* dst_uri = gtk_label_get_text(m_pLabelDestFolder);

	if (NULL != src_uri && NULL != dst_uri && strlen(src_uri) > 0 && strlen(dst_uri) > 0)
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

            GtkWidget* dialog = gtk_message_dialog_new(GTK_WINDOW(m_pDialogOrganize),
                GTK_DIALOG_DESTROY_WITH_PARENT,
                GTK_MESSAGE_ERROR,
                GTK_BUTTONS_CLOSE,
                "Source and Destination folders overlap. Please choose a different destination folder.");
            gtk_window_set_title(GTK_WINDOW(dialog), "Folder Conflict");
            g_signal_connect (dialog, "response", G_CALLBACK (gtk_window_destroy), NULL);
            gtk_window_present(GTK_WINDOW(dialog));
		}
	}

	return bIsValid;
}

static void on_folder_button_clicked(GtkButton* button, gpointer user_data);
static void on_folder_selected(GtkFileChooser* chooser, int response_id, gpointer user_data);

static void on_folder_button_clicked(GtkButton* button, gpointer user_data)
{
    OrganizeDlg::OrganizeDlgPriv *priv = static_cast<OrganizeDlg::OrganizeDlgPriv*>(user_data);
    GtkFileChooser* dialog = GTK_FILE_CHOOSER(gtk_file_chooser_dialog_new("Select a folder",
        GTK_WINDOW(priv->m_pDialogOrganize),
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Select", GTK_RESPONSE_ACCEPT,
        NULL));

    gpointer callback_data = NULL;
    if (button == priv->m_pBtnSourceFolder)
    {
        callback_data = priv->m_pLabelSourceFolder;
    }
    else
    {
        callback_data = priv->m_pLabelDestFolder;
    }

    g_signal_connect(dialog, "response", G_CALLBACK(on_folder_selected), callback_data);
    gtk_widget_show(GTK_WIDGET(dialog));
}

static void on_folder_selected(GtkFileChooser* chooser, int response_id, gpointer user_data)
{
    if (response_id == GTK_RESPONSE_ACCEPT)
    {
        GtkLabel* label = GTK_LABEL(user_data);
        GFile* file = gtk_file_chooser_get_file(chooser);
        char* uri = g_file_get_uri(file);
        gtk_label_set_text(label, uri);
        g_free(uri);
        g_object_unref(file);
    }
    gtk_window_destroy(GTK_WINDOW(chooser));
}

bool OrganizeDlg::OrganizeDlgPriv::GetRenameFiles() const
{
	return (TRUE == gtk_toggle_button_get_active(m_pTglBtnRenameFiles));
}

static void  on_clicked (GtkButton *button, gpointer   user_data)
{
	OrganizeDlg::OrganizeDlgPriv *priv = static_cast<OrganizeDlg::OrganizeDlgPriv*>(user_data);
	if (GTK_BUTTON(priv->m_pBtnOK) == button)
	{
		if (priv->ValidateInput())
		{
			gtk_window_destroy(GTK_WINDOW(priv->m_pDialogOrganize));
		}
	}
	else if (button == GTK_BUTTON(priv->m_pTglBtnRenameFiles))
	{
		priv->UpdateUI();
	}
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




