#include <config.h>
#include "RenameDlg.h"
#include "RenameTask.h"

#include "QuiverPrefs.h"
#include "Preferences.h"

#include "QuiverStockIcons.h"

#include <gio/gio.h>
#include <boost/algorithm/string.hpp>

#ifdef QUIVER_MAEMO
#ifdef HAVE_HILDON_FM_2
#include <hildon/hildon-file-chooser-dialog.h>
#else
#include <hildon-widgets/hildon-file-chooser-dialog.h>
#endif
#endif

class RenameDlg::RenameDlgPriv
{
public:
// constructor, destructor
	RenameDlgPriv(RenameDlg *parent);
	~RenameDlgPriv();
	
// methods
	void LoadWidgets();
	void UpdateUI();
	void ConnectSignals();
	
	bool ValidateInput();

// variables
	RenameDlg*         m_pRenameDlg;
	GtkBuilder*            m_pGtkBuilder;
	bool m_bLoadedDlg;
	

	// dlg widgets
	GtkDialog*              m_pDialogRename;

	GtkWidget*              m_pBtnOK;

	GtkButton*              m_pBtnSourceFolder;
	GtkLabel*               m_pLabelSourceFolder;
	GtkEntry*               m_pEntryTemplate;
	GtkLabel*               m_pLabelExample;

};


RenameDlg::RenameDlg() : m_PrivPtr(new RenameDlg::RenameDlgPriv(this))
{
	
}


GtkWidget* RenameDlg::GetWidget() const
{
	  return GTK_WIDGET(m_PrivPtr->m_pDialogRename);
}


bool RenameDlg::Run()
{
	if (m_PrivPtr->m_bLoadedDlg)
	{
		gtk_window_present(GTK_WINDOW(m_PrivPtr->m_pDialogRename));
        return true; // Placeholder
	}
	return false;
}

std::string RenameDlg::GetTemplate() const
{
	return gtk_editable_get_text(GTK_EDITABLE(m_PrivPtr->m_pEntryTemplate));
}

std::string RenameDlg::GetInputFolder() const
{
	return gtk_label_get_text(m_PrivPtr->m_pLabelSourceFolder);
}

void RenameDlg::SetInputFolder(std::string folder)
{
	gtk_label_set_text(m_PrivPtr->m_pLabelSourceFolder, folder.c_str());
}


// private stuff


// prototypes
static void  on_clicked (GtkButton *button, gpointer   user_data);
static void on_folder_button_clicked(GtkButton* button, gpointer user_data);
static void on_folder_selected(GtkFileChooser* chooser, gint response_id, gpointer user_data);
static void on_editable_changed (GtkEditable *editable, gpointer user_data);


RenameDlg::RenameDlgPriv::RenameDlgPriv(RenameDlg *parent) :
        m_pRenameDlg(parent)
{
	m_pDialogRename = NULL;
	m_pGtkBuilder = gtk_builder_new_from_file(QUIVER_DATADIR "/" "quiver.ui");

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

	if (NULL != m_pDialogRename)
	{
		gtk_window_destroy(GTK_WINDOW(m_pDialogRename));
		m_pDialogRename = NULL;
	}
}


void RenameDlg::RenameDlgPriv::LoadWidgets()
{
	m_pDialogRename         = GTK_DIALOG(gtk_builder_get_object (m_pGtkBuilder, "RenameDialog"));

	m_pBtnOK               = gtk_button_new_with_label("OK");
    gtk_button_set_icon_name(GTK_BUTTON(m_pBtnOK), "dialog-ok");
	gtk_dialog_add_buttons(GTK_DIALOG(m_pDialogRename), "_OK", GTK_RESPONSE_OK, "_Cancel", GTK_RESPONSE_CANCEL, NULL);


	GtkBox* src_cont = GTK_BOX( gtk_builder_get_object(m_pGtkBuilder, "rename_align_source_folder") );
    m_pBtnSourceFolder = (GtkButton*)gtk_button_new_with_label("Choose Source Folder");
    m_pLabelSourceFolder = (GtkLabel*)gtk_label_new("");
    gtk_box_append(src_cont, GTK_WIDGET(m_pBtnSourceFolder));
    gtk_box_append(src_cont, GTK_WIDGET(m_pLabelSourceFolder));
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
		g_signal_connect(m_pBtnSourceFolder,
			"clicked",(GCallback)on_folder_button_clicked,this);

		g_signal_connect(m_pBtnOK,
			"clicked",(GCallback)on_clicked,this);

		g_signal_connect(m_pEntryTemplate,
			"changed",(GCallback)on_editable_changed,this);

	}
	
}

bool RenameDlg::RenameDlgPriv::ValidateInput()
{
	bool bIsValid = true;
	std::string strMsg, strTitle;

	const gchar* src_uri = gtk_label_get_text(m_pLabelSourceFolder);

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
		GFile* file = g_file_get_child_for_display_name(parent_dir, strFileName.c_str(), &error);
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

	if (!bIsValid)
	{
		GtkWidget* dialog = gtk_message_dialog_new (GTK_WINDOW(m_pDialogRename),
			GTK_DIALOG_DESTROY_WITH_PARENT,
			GTK_MESSAGE_ERROR,
			GTK_BUTTONS_CLOSE,
			"%s",
			strMsg.c_str());
		gtk_window_set_title(GTK_WINDOW(dialog), strTitle.c_str());
		gtk_window_present(GTK_WINDOW(dialog));
	}

	return bIsValid;
}

static void  on_clicked (GtkButton *button, gpointer   user_data)
{
	RenameDlg::RenameDlgPriv *priv = static_cast<RenameDlg::RenameDlgPriv*>(user_data);
	if (GTK_BUTTON(priv->m_pBtnOK) == button)
	{
		if (priv->ValidateInput())
		{
			gtk_window_destroy(GTK_WINDOW(priv->m_pDialogRename));
		}
	}
}

static void on_folder_button_clicked(GtkButton* button, gpointer user_data)
{
    RenameDlg::RenameDlgPriv *priv = static_cast<RenameDlg::RenameDlgPriv*>(user_data);
    GtkWidget* dialog = gtk_file_chooser_dialog_new("Select a folder",
        GTK_WINDOW(priv->m_pDialogRename),
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Select", GTK_RESPONSE_ACCEPT,
        NULL);

    g_signal_connect(dialog, "response", G_CALLBACK(on_folder_selected), priv);
    gtk_widget_show(GTK_WIDGET(dialog));
}

static void on_folder_selected(GtkFileChooser* chooser, gint response_id, gpointer user_data)
{
    if (response_id == GTK_RESPONSE_ACCEPT)
    {
        RenameDlg::RenameDlgPriv *priv = static_cast<RenameDlg::RenameDlgPriv*>(user_data);
        GFile* file = gtk_file_chooser_get_file(chooser);
        char* uri = g_file_get_uri(file);
		gtk_label_set_text(priv->m_pLabelSourceFolder, uri);
        g_free(uri);
        g_object_unref(file);
    }
    gtk_window_destroy(GTK_WINDOW(chooser));
}

static void on_editable_changed (GtkEditable *editable, gpointer user_data)
{
	std::string invalid_chars = "\\/:*?\"<>|";
	RenameDlg::RenameDlgPriv *priv = static_cast<RenameDlg::RenameDlgPriv*>(user_data);

	GtkEntryBuffer* buffer = gtk_entry_get_buffer(GTK_ENTRY(editable));
	const gchar* text = gtk_entry_buffer_get_text(buffer);
	std::string strTemplate(text);
	std::string strNewTemplate = strTemplate;
   
	strNewTemplate.erase(std::remove_if(strNewTemplate.begin(), strNewTemplate.end(),
		[&invalid_chars](char c) {
			return invalid_chars.find(c) != std::string::npos;
		}), strNewTemplate.end());

	if (strNewTemplate != strTemplate)
	{
		gtk_entry_buffer_set_text(buffer, strNewTemplate.c_str(), -1);
	}

	priv->UpdateUI();
}
