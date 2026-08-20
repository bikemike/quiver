#include <config.h>
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "RenameDlg.h"
#include "RenameTask.h"

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

	GtkFileChooserButton*   m_pFCBtnSourceFolder;
	GtkEntry*               m_pEntryTemplate;
	GtkLabel*               m_pLabelExample;

};


RenameDlg::RenameDlg() : m_PrivPtr(new RenameDlg::RenameDlgPriv(this))
{
	
}


GtkWidget* RenameDlg::GetWidget() const
{
	  return NULL;
}


bool RenameDlg::Run()
{
	if (m_PrivPtr->m_bLoadedDlg)
	{
		gint result = gtk_dialog_run(GTK_DIALOG(m_PrivPtr->m_pDialogRename));
		return (GTK_RESPONSE_OK == result);
	}
	return false;
}

std::string RenameDlg::GetTemplate() const
{
	return gtk_entry_get_text(m_PrivPtr->m_pEntryTemplate);
}

std::string RenameDlg::GetInputFolder() const
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

void RenameDlg::SetInputFolder(std::string folder)
{
	gtk_file_chooser_set_current_folder_uri(GTK_FILE_CHOOSER(m_PrivPtr->m_pFCBtnSourceFolder), folder.c_str());

}


// private stuff


// prototypes
static void on_clicked (GtkButton *button, gpointer user_data);
static void on_folder_change (GtkFileChooser *chooser, gpointer user_data);
static void on_editable_changed (GtkEditable *editable, gpointer user_data);
[[maybe_unused]] static void __attribute__((unused)) combo_changed (GtkComboBox *widget, gpointer user_data);


RenameDlg::RenameDlgPriv::RenameDlgPriv(RenameDlg *parent) :
        m_pRenameDlg(parent)
{
	m_pDialogRename = NULL;
	m_pGtkBuilder = gtk_builder_new();
	gchar* objectids[] = {
		(gchar*)"RenameDialog",
		NULL};
	gtk_builder_add_objects_from_file(m_pGtkBuilder, QUIVER_DATADIR "/" "quiver.ui", objectids, NULL);

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
		gtk_widget_destroy(GTK_WIDGET(m_pDialogRename));
		m_pDialogRename = NULL;
	}
}


void RenameDlg::RenameDlgPriv::LoadWidgets()
{
	m_pDialogRename         = GTK_DIALOG(gtk_builder_get_object (m_pGtkBuilder, (gchar*)"RenameDialog"));

	m_pBtnOK = gtk_dialog_add_button(m_pDialogRename, "_OK", GTK_RESPONSE_OK);


	GtkContainer* src_cont = GTK_CONTAINER( gtk_builder_get_object(m_pGtkBuilder, "rename_align_source_folder") );
		m_pFCBtnSourceFolder = GTK_FILE_CHOOSER_BUTTON(gtk_file_chooser_button_new ("Choose Source Folder", GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER));
		gtk_file_chooser_set_local_only(GTK_FILE_CHOOSER(m_pFCBtnSourceFolder), FALSE);
		gtk_widget_show(GTK_WIDGET(m_pFCBtnSourceFolder));
		
		gtk_container_add(src_cont, GTK_WIDGET(m_pFCBtnSourceFolder));
	m_pEntryTemplate        = GTK_ENTRY( gtk_builder_get_object(m_pGtkBuilder, "rename_entry_template") );

	m_pLabelExample           = GTK_LABEL( gtk_builder_get_object(m_pGtkBuilder, "rename_label_example") );

	m_bLoadedDlg = (
		NULL != m_pDialogRename        &&
		NULL != m_pFCBtnSourceFolder     &&
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
		std::string strTemplate = gtk_entry_get_text(m_pEntryTemplate);
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
		g_signal_connect(m_pFCBtnSourceFolder,
			"current-folder-changed",(GCallback)on_folder_change,this);

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

	// make sure source and dest directories are 
	// in separate locations
	
	gchar* src_uri = gtk_file_chooser_get_uri (
				GTK_FILE_CHOOSER (m_pFCBtnSourceFolder));


	if (NULL != src_uri)
	{
		GFile* file_src = g_file_new_for_uri(src_uri);

		// check if child file is valid
		std::string strTemplate = gtk_entry_get_text(m_pEntryTemplate);
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

	if (NULL != src_uri)
	{
		g_free(src_uri);
	}

	if (!bIsValid)
	{
		GtkWidget* dialog = gtk_message_dialog_new (GTK_WINDOW(m_pDialogRename),
			GTK_DIALOG_DESTROY_WITH_PARENT,
			GTK_MESSAGE_ERROR,
			GTK_BUTTONS_CLOSE,
			"%s", strMsg.c_str());
		gtk_window_set_title(GTK_WINDOW(dialog), strTitle.c_str());
		gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (dialog);
	}

	return bIsValid;
}

static void on_clicked (GtkButton *button, gpointer user_data)
{
	RenameDlg::RenameDlgPriv *priv = static_cast<RenameDlg::RenameDlgPriv*>(user_data);
	if (GTK_BUTTON(priv->m_pBtnOK) == button)
	{
		if (priv->ValidateInput())
		{
			gtk_dialog_response(priv->m_pDialogRename, GTK_RESPONSE_OK);
		}
	}
}

void on_folder_change (GtkFileChooser *chooser, gpointer user_data)
{
	RenameDlg::RenameDlgPriv *priv = static_cast<RenameDlg::RenameDlgPriv*>(user_data);
	
	if (GTK_FILE_CHOOSER(priv->m_pFCBtnSourceFolder) == chooser)
	{
		priv->UpdateUI();
	}
}


static void on_editable_changed (GtkEditable *editable, gpointer user_data)
{
	std::string invalid_chars = "\\/:*?\"<>|";
	RenameDlg::RenameDlgPriv *priv = static_cast<RenameDlg::RenameDlgPriv*>(user_data);

	std::string strTemplate = gtk_entry_get_text(priv->m_pEntryTemplate);
	std::string strNewTemplate = strTemplate;
   
	std::string::iterator itr = 
		std::remove_if(strNewTemplate.begin(), strNewTemplate.end(), boost::is_any_of(invalid_chars));
	strNewTemplate.erase(itr, strNewTemplate.end());

	if (strNewTemplate != strTemplate)
	{
		gtk_entry_set_text(priv->m_pEntryTemplate, strNewTemplate.c_str());
	}

	priv->UpdateUI();
}

void __attribute__((unused)) combo_changed (GtkComboBox *widget, gpointer user_data)
{
	RenameDlg::RenameDlgPriv *priv = static_cast<RenameDlg::RenameDlgPriv*>(user_data);
	priv->UpdateUI();
}




