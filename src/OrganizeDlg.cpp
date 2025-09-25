#include <config.h>
#include "OrganizeDlg.h"
#include "OrganizeTask.h"
#include "RenameTask.h"

#include "QuiverPrefs.h"
#include "Preferences.h"

#include "QuiverStockIcons.h"
#include "MessageBox.h"

#include <gio/gio.h>
#include <gtk/gtk.h>

class OrganizeDlg::OrganizeDlgPriv
{
public:
// constructor, destructor
	OrganizeDlgPriv(OrganizeDlg *parent, GtkWindow* pParent);
	~OrganizeDlgPriv();
	
// methods
	void LoadWidgets();
	void UpdateUI();
	void ConnectSignals();
	
	bool ValidateInput();

	bool GetRenameFiles() const;

    void OnSourceFolderClicked();
    void OnDestFolderClicked();
    void OnFolderChooserResponse(int response_id, GtkFileChooser* chooser);

// variables
	OrganizeDlg*         m_pOrganizeDlg;
	GtkBuilder*            m_pGtkBuilder;
	bool m_bLoadedDlg;
	

	// dlg widgets
	GtkDialog*              m_pDialogOrganize;

	GtkWidget*              m_pBtnOK;

	GtkDropDown*        m_pComboTemplateFolder;
	GtkEntry*               m_pEntryTemplateFile;
	GtkButton*   m_pBtnSourceFolder;
	GtkButton*   m_pBtnDestFolder;
	GtkToggleButton*        m_pTglBtnSubfolders;
	GtkToggleButton*        m_pTglBtnRenameFiles;
	GtkSpinButton*          m_pSpinExtension;

	GtkEntry*               m_pEntryFolderName;
	GtkLabel*               m_pLabelExample;

    GFile* m_pSourceFolderFile = nullptr;
    GFile* m_pDestFolderFile = nullptr;

    GtkWindow*              m_pParent;
    bool m_isChoosingSource = false;
};

// prototypes
static void  on_clicked (GtkButton *button, gpointer   user_data);
static void  on_toggled (GtkToggleButton *togglebutton, gpointer user_data);
static void on_editable_changed (GtkEditable *editable, gpointer user_data);
static void combo_changed (GtkDropDown *widget, GParamSpec* pspec, gpointer user_data);


OrganizeDlg::OrganizeDlg(GtkWindow* parent) : m_PrivPtr(new OrganizeDlgPriv(this, parent))
{
}

OrganizeDlg::~OrganizeDlg()
{
}

GtkWidget* OrganizeDlg::GetWidget() const
{
	  return GTK_WIDGET(m_PrivPtr->m_pDialogOrganize);
}


void OrganizeDlg::Run()
{
	if (m_PrivPtr->m_bLoadedDlg)
	{
        g_signal_connect(m_PrivPtr->m_pDialogOrganize, "response", G_CALLBACK(+[](GtkDialog* dialog, int response, gpointer user_data){
            if (response == GTK_RESPONSE_OK) {
                // Here you would extract the data from the dialog
            }
            gtk_window_destroy(GTK_WINDOW(dialog));
        }), this);
		gtk_widget_set_visible(GTK_WIDGET(m_PrivPtr->m_pDialogOrganize), TRUE);
	}
}

std::string OrganizeDlg::GetFolderTemplate() const
{
    GObject* item = G_OBJECT(gtk_drop_down_get_selected_item(m_PrivPtr->m_pComboTemplateFolder));
    if (item) {
        return gtk_string_object_get_string(GTK_STRING_OBJECT(item));
    }
	return "";
}

std::string OrganizeDlg::GetFileTemplate() const
{
	return gtk_editable_get_text(GTK_EDITABLE(m_PrivPtr->m_pEntryTemplateFile));
}

std::string OrganizeDlg::GetOutputFolder() const
{
	std::string strDir;

	if (m_PrivPtr->m_pDestFolderFile) {
        gchar* uri = g_file_get_uri(m_PrivPtr->m_pDestFolderFile);
        strDir = uri;
        g_free(uri);
    }

	return strDir;
}

std::string OrganizeDlg::GetAppendedText() const
{
	return gtk_editable_get_text(GTK_EDITABLE(m_PrivPtr->m_pEntryFolderName));
}

std::string OrganizeDlg::GetInputFolder() const
{
	std::string strDir;

	if (m_PrivPtr->m_pSourceFolderFile) {
        gchar* uri = g_file_get_uri(m_PrivPtr->m_pSourceFolderFile);
        strDir = uri;
        g_free(uri);
    }

	return strDir;
}

void OrganizeDlg::SetInputFolder(std::string dir)
{
    if (m_PrivPtr->m_pSourceFolderFile) {
        g_object_unref(m_PrivPtr->m_pSourceFolderFile);
    }
    m_PrivPtr->m_pSourceFolderFile = g_file_new_for_uri(dir.c_str());
    gtk_button_set_label(m_PrivPtr->m_pBtnSourceFolder, g_file_get_path(m_PrivPtr->m_pSourceFolderFile));
}


int OrganizeDlg::GetDayExtention() const
{
	return gtk_spin_button_get_value_as_int (m_PrivPtr->m_pSpinExtension);
}

bool OrganizeDlg::GetIncludeSubfolders() const
{
	return gtk_toggle_button_get_active(m_PrivPtr->m_pTglBtnSubfolders);
}

bool OrganizeDlg::GetRenameFiles() const
{
	return m_PrivPtr->GetRenameFiles();
}

OrganizeDlg::OrganizeDlgPriv::OrganizeDlgPriv(OrganizeDlg *parent, GtkWindow* pParent) :
        m_pOrganizeDlg(parent), m_pParent(pParent)
{
	m_pDialogOrganize = NULL;
	m_pGtkBuilder = gtk_builder_new();

    gtk_builder_add_from_file(m_pGtkBuilder, QUIVER_DATADIR "/quiver.ui", NULL);

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

    if (m_pSourceFolderFile) g_object_unref(m_pSourceFolderFile);
    if (m_pDestFolderFile) g_object_unref(m_pDestFolderFile);
}


void OrganizeDlg::OrganizeDlgPriv::LoadWidgets()
{
	m_pDialogOrganize = GTK_DIALOG(gtk_builder_get_object (m_pGtkBuilder, "OrganizeDialog"));
    gtk_window_set_transient_for(GTK_WINDOW(m_pDialogOrganize), m_pParent);
    gtk_window_set_modal(GTK_WINDOW(m_pDialogOrganize), TRUE);

    m_pBtnOK = GTK_WIDGET(gtk_builder_get_object (m_pGtkBuilder, "organize_ok_button"));


	m_pComboTemplateFolder = GTK_DROP_DOWN( gtk_builder_get_object(m_pGtkBuilder, "organize_combo_template") );
	m_pEntryTemplateFile = GTK_ENTRY( gtk_builder_get_object(m_pGtkBuilder, "organize_entry_filename_template") );
	m_pSpinExtension = GTK_SPIN_BUTTON( gtk_builder_get_object(m_pGtkBuilder, "organize_spinbutton_day_offset") );
	m_pTglBtnSubfolders = GTK_TOGGLE_BUTTON( gtk_builder_get_object(m_pGtkBuilder, "organize_cb_subfolders") );
	m_pTglBtnRenameFiles = GTK_TOGGLE_BUTTON( gtk_builder_get_object(m_pGtkBuilder, "organize_cb_rename_files") );

    GtkGrid* grid = GTK_GRID(gtk_builder_get_object(m_pGtkBuilder, "table3"));

    m_pBtnSourceFolder = GTK_BUTTON(gtk_button_new_with_label("Choose Source Folder"));
    gtk_widget_set_hexpand(GTK_WIDGET(m_pBtnSourceFolder), TRUE);
    gtk_grid_attach(grid, GTK_WIDGET(m_pBtnSourceFolder), 0, 1, 2, 1);

    m_pBtnDestFolder = GTK_BUTTON(gtk_button_new_with_label("Choose Destination Folder"));
    gtk_widget_set_hexpand(GTK_WIDGET(m_pBtnDestFolder), TRUE);
    gtk_grid_attach(grid, GTK_WIDGET(m_pBtnDestFolder), 1, 4, 1, 1);

	m_pEntryFolderName = GTK_ENTRY( gtk_builder_get_object(m_pGtkBuilder, "organize_entry_folder_name") );
	m_pLabelExample = GTK_LABEL( gtk_builder_get_object(m_pGtkBuilder, "organize_label_example_output") );

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

		gtk_drop_down_set_selected(m_pComboTemplateFolder, 0);

		PreferencesPtr prefs = Preferences::GetInstance();
		std::string strPhotoLibrary = prefs->GetString(QUIVER_PREFS_APP,QUIVER_PREFS_APP_PHOTO_LIBRARY);
		if (!strPhotoLibrary.empty())
		{
            if (m_pDestFolderFile) g_object_unref(m_pDestFolderFile);
            m_pDestFolderFile = g_file_new_for_uri(strPhotoLibrary.c_str());
            gtk_button_set_label(m_pBtnDestFolder, g_file_get_path(m_pDestFolderFile));
		}
	}  
}

void OrganizeDlg::OrganizeDlgPriv::UpdateUI()
{
	if (m_bLoadedDlg)
	{
		std::string strLabel;
        if (m_pDestFolderFile) {
            char* path = g_file_get_path(m_pDestFolderFile);
            strLabel = path;
            g_free(path);
        }

		GDateTime* time = g_date_time_new_now_local();

		strLabel += G_DIR_SEPARATOR_S;
        GObject* selected_item = G_OBJECT(gtk_drop_down_get_selected_item(m_pComboTemplateFolder));
        if (selected_item) {
            strLabel += gtk_string_object_get_string(GTK_STRING_OBJECT(selected_item));
        }
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
		g_signal_connect(m_pBtnSourceFolder, "clicked", G_CALLBACK(+[](GtkButton* button, gpointer user_data){
            static_cast<OrganizeDlg::OrganizeDlgPriv*>(user_data)->OnSourceFolderClicked();
        }), this);
		g_signal_connect(m_pBtnDestFolder, "clicked", G_CALLBACK(+[](GtkButton* button, gpointer user_data){
            static_cast<OrganizeDlg::OrganizeDlgPriv*>(user_data)->OnDestFolderClicked();
        }), this);
		g_signal_connect(m_pBtnOK, "clicked", G_CALLBACK(on_clicked), this);
		g_signal_connect(m_pTglBtnRenameFiles, "toggled", G_CALLBACK(on_toggled),this);
		g_signal_connect(m_pEntryFolderName, "changed", G_CALLBACK(on_editable_changed),this);
		g_signal_connect(m_pComboTemplateFolder, "notify::selected-item", G_CALLBACK(combo_changed),this);
		g_signal_connect(m_pEntryTemplateFile, "changed", G_CALLBACK(on_editable_changed),this);
	}
}

bool OrganizeDlg::OrganizeDlgPriv::ValidateInput()
{
	bool bIsValid = true;

	if (!m_pSourceFolderFile || !m_pDestFolderFile) {
        MessageBox::Run(m_pParent, MessageBox::ICON_TYPE_ERROR, MessageBox::BUTTON_TYPE_OK, "Source and destination folders must be selected.", "");
        return false;
    }
	
	gchar* src_path = g_file_get_path(m_pSourceFolderFile);
	gchar* dst_path = g_file_get_path(m_pDestFolderFile);

	if (NULL != src_path && NULL != dst_path)
	{
		bool source_is_child = g_str_has_prefix(src_path, dst_path);
        bool dest_is_child = g_str_has_prefix(dst_path, src_path);

		if ( (dest_is_child && m_pOrganizeDlg->GetIncludeSubfolders()) || source_is_child)
		{
			bIsValid = false;
            MessageBox::Run(m_pParent, MessageBox::ICON_TYPE_ERROR, MessageBox::BUTTON_TYPE_CLOSE, "Source and Destination folders overlap.", "Please choose a different destination folder.");
		}
	}

	g_free(src_path);
	g_free(dst_path);

	return bIsValid;
}

bool OrganizeDlg::OrganizeDlgPriv::GetRenameFiles() const
{
	return gtk_toggle_button_get_active(m_pTglBtnRenameFiles);
}

static void select_folder_callback(GObject *source_object, GAsyncResult *res, gpointer user_data)
{
    OrganizeDlg::OrganizeDlgPriv* self = static_cast<OrganizeDlg::OrganizeDlgPriv*>(user_data);
    GtkFileDialog* dialog = GTK_FILE_DIALOG(source_object);
    GError* error = NULL;
    GFile* folder = gtk_file_dialog_select_folder_finish(dialog, res, &error);

    if (error) {
        g_warning("Error selecting folder: %s", error->message);
        g_error_free(error);
        self->UpdateUI();
        return;
    }

    if (folder) { // User selected a folder, NULL if cancelled
        if (self->m_isChoosingSource) {
            if (self->m_pSourceFolderFile) g_object_unref(self->m_pSourceFolderFile);
            self->m_pSourceFolderFile = folder; // takes ownership from finish function
            char* path = g_file_get_path(self->m_pSourceFolderFile);
            gtk_button_set_label(self->m_pBtnSourceFolder, path);
            g_free(path);
        } else {
            if (self->m_pDestFolderFile) g_object_unref(self->m_pDestFolderFile);
            self->m_pDestFolderFile = folder; // takes ownership
            char* path = g_file_get_path(self->m_pDestFolderFile);
            gtk_button_set_label(self->m_pBtnDestFolder, path);
            g_free(path);
        }
    }
    self->UpdateUI();
}

void OrganizeDlg::OrganizeDlgPriv::OnSourceFolderClicked() {
    m_isChoosingSource = true;
    GtkFileDialog* dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Choose Source Folder");
    gtk_file_dialog_select_folder(dialog, GTK_WINDOW(m_pDialogOrganize), NULL, select_folder_callback, this);
    g_object_unref(dialog);
}

void OrganizeDlg::OrganizeDlgPriv::OnDestFolderClicked() {
    m_isChoosingSource = false;
    GtkFileDialog* dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Choose Destination Folder");
    gtk_file_dialog_select_folder(dialog, GTK_WINDOW(m_pDialogOrganize), NULL, select_folder_callback, this);
    g_object_unref(dialog);
}


static void  on_clicked (GtkButton *button, gpointer   user_data)
{
	OrganizeDlg::OrganizeDlgPriv *priv = static_cast<OrganizeDlg::OrganizeDlgPriv*>(user_data);
	if (GTK_WIDGET(priv->m_pBtnOK) == GTK_WIDGET(button))
	{
		if (priv->ValidateInput())
		{
            g_signal_emit_by_name(priv->m_pDialogOrganize, "response", GTK_RESPONSE_OK);
		}
	}
}

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

static void combo_changed (GtkDropDown *widget, GParamSpec* pspec, gpointer user_data)
{
	OrganizeDlg::OrganizeDlgPriv *priv = static_cast<OrganizeDlg::OrganizeDlgPriv*>(user_data);
	priv->UpdateUI();
}
