#include <config.h>
#include "RenameDlg.h"
#include "RenameTask.h"
#include "MessageBox.h"

#include "QuiverPrefs.h"
#include "Preferences.h"

#include "QuiverStockIcons.h"

#include <gio/gio.h>

#include <algorithm>
#include <boost/algorithm/string/classification.hpp>


class RenameDlg::RenameDlgPriv
{
public:
// constructor, destructor
	RenameDlgPriv(RenameDlg *parent, GtkWindow* pParent);
	~RenameDlgPriv();
	
// methods
	void LoadWidgets();
	void UpdateUI();
	void ConnectSignals();
	
	bool ValidateInput();
    void OnFolderClicked();
    void OnFolderChooserResponse(int response_id, GtkFileChooser* chooser);

// variables
	RenameDlg*         m_pRenameDlg;
	GtkBuilder*            m_pGtkBuilder;
	bool m_bLoadedDlg;
	

	// dlg widgets
	GtkDialog*              m_pDialogRename;

	GtkWidget*              m_pBtnOK;

	GtkButton*   m_pBtnSourceFolder;
	GtkEntry*               m_pEntryTemplate;
	GtkLabel*               m_pLabelExample;
    GtkWindow*              m_pParent;
    GFile*                  m_pSourceFolderFile = nullptr;
    GMainLoop*              m_loop = nullptr;
};


RenameDlg::RenameDlg(GtkWindow* parent) : m_PrivPtr(new RenameDlg::RenameDlgPriv(this, parent))
{
	
}

RenameDlg::~RenameDlg()
{
}

GtkWidget* RenameDlg::GetWidget() const
{
	  return GTK_WIDGET(m_PrivPtr->m_pDialogRename);
}


void RenameDlg::Run()
{
	if (m_PrivPtr->m_bLoadedDlg)
	{
		gtk_widget_set_visible(GTK_WIDGET(m_PrivPtr->m_pDialogRename), TRUE);
        m_PrivPtr->m_loop = g_main_loop_new(NULL, FALSE);
        g_main_loop_run(m_PrivPtr->m_loop);
        g_main_loop_unref(m_PrivPtr->m_loop);
        m_PrivPtr->m_loop = nullptr;
	}
}

std::string RenameDlg::GetTemplate() const
{
	return gtk_editable_get_text(GTK_EDITABLE(m_PrivPtr->m_pEntryTemplate));
}

std::string RenameDlg::GetInputFolder() const
{
	std::string strDir;

	if (m_PrivPtr->m_pSourceFolderFile) {
        gchar* uri = g_file_get_uri(m_PrivPtr->m_pSourceFolderFile);
        strDir = uri;
        g_free(uri);
    }

	return strDir;

}

void RenameDlg::SetInputFolder(std::string folder)
{
    if (m_PrivPtr->m_pSourceFolderFile) {
        g_object_unref(m_PrivPtr->m_pSourceFolderFile);
    }
    m_PrivPtr->m_pSourceFolderFile = g_file_new_for_uri(folder.c_str());
    gtk_button_set_label(m_PrivPtr->m_pBtnSourceFolder, g_file_get_path(m_PrivPtr->m_pSourceFolderFile));
}


// private stuff


// prototypes
static void  on_clicked (GtkButton *button, gpointer   user_data);
static void on_editable_changed (GtkEditable *editable, gpointer user_data);


RenameDlg::RenameDlgPriv::RenameDlgPriv(RenameDlg *parent, GtkWindow* pParent) :
        m_pRenameDlg(parent), m_pParent(pParent)
{
	m_pDialogRename = NULL;
	m_pGtkBuilder = gtk_builder_new();
	const gchar* objectids[] = {
		"RenameDialog",
		NULL};
	gtk_builder_add_objects_from_file(m_pGtkBuilder, QUIVER_DATADIR "/" "quiver.ui", (const char**)objectids, NULL);

	LoadWidgets();
	UpdateUI();
	ConnectSignals();
}

RenameDlg::RenameDlgPriv::~RenameDlgPriv()
{
	if (NULL != m_pGtkBuilder)
	{
		g_object_unref(m_pGtkBuilder);
		m_pGtkBuilder = NULL;
	}
    if (m_pSourceFolderFile) g_object_unref(m_pSourceFolderFile);
}


void RenameDlg::RenameDlgPriv::LoadWidgets()
{
	m_pDialogRename         = GTK_DIALOG(gtk_builder_get_object (m_pGtkBuilder, "RenameDialog"));
    gtk_window_set_transient_for(GTK_WINDOW(m_pDialogRename), m_pParent);
    gtk_window_set_modal(GTK_WINDOW(m_pDialogRename), TRUE);


    m_pBtnOK = GTK_WIDGET(gtk_builder_get_object(m_pGtkBuilder, "rename_ok_button"));

    GtkGrid* grid = GTK_GRID(gtk_builder_get_object(m_pGtkBuilder, "table4"));
	m_pBtnSourceFolder = GTK_BUTTON(gtk_button_new_with_label ("Choose Source Folder"));
    gtk_grid_attach(grid, GTK_WIDGET(m_pBtnSourceFolder), 0, 0, 2, 1);

	m_pEntryTemplate        = GTK_ENTRY( gtk_builder_get_object(m_pGtkBuilder, "rename_entry_template") );
	m_pLabelExample           = GTK_LABEL( gtk_builder_get_object(m_pGtkBuilder, "rename_label_example") );

	m_bLoadedDlg = (
		NULL != m_pDialogRename        &&
		NULL != m_pBtnSourceFolder     &&
		NULL != m_pEntryTemplate       &&
		NULL != m_pLabelExample         ); 

	if (m_bLoadedDlg)
	{
		PangoAttrList* attrs = pango_attr_list_new();
		PangoAttribute* attr = pango_attr_scale_new (PANGO_SCALE_SMALL);
		pango_attr_list_insert(attrs,attr);

		gtk_label_set_attributes(m_pLabelExample, attrs);
		pango_attr_list_unref(attrs);

		gtk_window_set_default_size(GTK_WINDOW(m_pDialogRename), 400,-1);

	}  
}

void RenameDlg::RenameDlgPriv::UpdateUI()
{
	if (m_bLoadedDlg)
	{
		std::string strTemplate = gtk_editable_get_text(GTK_EDITABLE(m_pEntryTemplate));
		GDateTime* time = g_date_time_new_now_local();
		std::string strFileName = RenameTask::DoVariableSubstitution(strTemplate, time, 1);
		g_date_time_unref(time);
		std::string strLabel = strFileName + ".jpg";
		gtk_label_set_text(m_pLabelExample, strLabel.c_str());
	}
}

void RenameDlg::RenameDlgPriv::ConnectSignals()
{
	if (m_bLoadedDlg)
	{
		g_signal_connect(m_pBtnSourceFolder, "clicked", G_CALLBACK(+[](GtkButton* button, gpointer user_data){
            static_cast<RenameDlg::RenameDlgPriv*>(user_data)->OnFolderClicked();
        }), this);

		g_signal_connect(m_pBtnOK, "clicked",(GCallback)on_clicked,this);

		g_signal_connect(m_pEntryTemplate, "changed",(GCallback)on_editable_changed,this);
	}
}

bool RenameDlg::RenameDlgPriv::ValidateInput()
{
	bool bIsValid = true;
	std::string strMsg, strTitle;

	if (!m_pSourceFolderFile) {
        MessageBox::Run(m_pParent, MessageBox::ICON_TYPE_ERROR, MessageBox::BUTTON_TYPE_OK, "Source folder must be selected.", "");
        return false;
    }

	gchar* src_uri = g_file_get_uri(m_pSourceFolderFile);

	if (NULL != src_uri)
	{
		GFile* file_src = g_file_new_for_uri(src_uri);

		// check if child file is valid
		std::string strTemplate = gtk_editable_get_text(GTK_EDITABLE(m_pEntryTemplate));
		GDateTime* time = g_date_time_new_now_local();
		std::string strFileName = RenameTask::DoVariableSubstitution(strTemplate, time, 1);
		g_date_time_unref(time);

		GFile* parent_dir = g_file_get_parent(file_src);
		GError* error = NULL;
		GFile* file = g_file_get_child(parent_dir, strFileName.c_str());
		g_object_unref(parent_dir);
		if (NULL != file)
		{
			g_object_unref(file);
		}

		if (NULL != error)
		{
			bIsValid =false;
			strTitle = "File Error";
 			strMsg = error->message;
			g_error_free(error);
		}
		
		g_object_unref(file_src);
	}
	else
	{
		bIsValid = false;
	}

	if (NULL != src_uri)
	{
		g_free(src_uri);
	}

	if (!bIsValid)
	{
        MessageBox::Run(m_pParent, MessageBox::ICON_TYPE_ERROR, MessageBox::BUTTON_TYPE_CLOSE, strTitle, strMsg);
	}

	return bIsValid;
}

static void select_folder_callback(GObject *source_object, GAsyncResult *res, gpointer user_data)
{
    RenameDlg::RenameDlgPriv* self = static_cast<RenameDlg::RenameDlgPriv*>(user_data);
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
        if (self->m_pSourceFolderFile) g_object_unref(self->m_pSourceFolderFile);
        self->m_pSourceFolderFile = folder; // takes ownership from finish function
        char* path = g_file_get_path(self->m_pSourceFolderFile);
        gtk_button_set_label(self->m_pBtnSourceFolder, path);
        g_free(path);
    }
    self->UpdateUI();
}

void RenameDlg::RenameDlgPriv::OnFolderClicked() {
    GtkFileDialog* dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Choose Source Folder");
    gtk_file_dialog_select_folder(dialog, GTK_WINDOW(m_pDialogRename), NULL, select_folder_callback, this);
    g_object_unref(dialog);
}


static void  on_clicked (GtkButton *button, gpointer   user_data)
{
	RenameDlg::RenameDlgPriv *priv = static_cast<RenameDlg::RenameDlgPriv*>(user_data);
	if (GTK_WIDGET(priv->m_pBtnOK) == GTK_WIDGET(button))
	{
		if (priv->ValidateInput())
		{
            g_signal_emit_by_name(priv->m_pDialogRename, "response", GTK_RESPONSE_OK);
		}
	}
}

static void on_editable_changed (GtkEditable *editable, gpointer user_data)
{
	std::string invalid_chars = "\\/:*?\"<>|";
	RenameDlg::RenameDlgPriv *priv = static_cast<RenameDlg::RenameDlgPriv*>(user_data);

	std::string strTemplate = gtk_editable_get_text(GTK_EDITABLE(priv->m_pEntryTemplate));
	std::string strNewTemplate = strTemplate;
   
	std::string::iterator itr =
		std::remove_if(strNewTemplate.begin(), strNewTemplate.end(), boost::is_any_of(invalid_chars));
	strNewTemplate.erase(itr, strNewTemplate.end());

	if (strNewTemplate != strTemplate)
	{
		gtk_editable_set_text(GTK_EDITABLE(priv->m_pEntryTemplate), strNewTemplate.c_str());
	}

	priv->UpdateUI();
}
