#include <config.h>
#include "RenameDlg.h"
#include <glade/glade.h>

#include "QuiverPrefs.h"
#include "Preferences.h"

#include "QuiverStockIcons.h"

#include <libgnomevfs/gnome-vfs.h>

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
#ifdef HAVE_LIBGLADE
	GladeXML*            m_pGladeXML;
#endif
	bool m_bLoadedDlg;
	

	// dlg widgets
	GtkDialog*              m_pDialogRename;

	GtkWidget*              m_pBtnOK;

#ifdef QUIVER_MAEMO
	GtkButton*              m_pBtnSourceFolder;
	GtkButton*              m_pBtnDestFolder;
#else
	GtkFileChooserButton*   m_pFCBtnSourceFolder;
	GtkFileChooserButton*   m_pFCBtnDestFolder;
#endif
	GtkToggleButton*        m_pTglBtnSubfolders;

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
#ifdef HAVE_LIBGLADE
	if (m_PrivPtr->m_bLoadedDlg)
	{
		gint result = gtk_dialog_run(GTK_DIALOG(m_PrivPtr->m_pDialogRename));
		return (GTK_RESPONSE_OK == result);
	}
#endif
	return false;
}

std::string RenameDlg::GetTemplate() const
{
	return gtk_entry_get_text(m_PrivPtr->m_pEntryTemplate);
}

std::string RenameDlg::GetOutputFolder() const
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

std::string RenameDlg::GetInputFolder() const
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

bool RenameDlg::GetIncludeSubfolders() const
{
	return (TRUE == gtk_toggle_button_get_active(m_PrivPtr->m_pTglBtnSubfolders));
}


// private stuff


// prototypes
static void  on_clicked (GtkButton *button, gpointer   user_data);
#ifndef QUIVER_MAEMO
static void on_folder_change (GtkFileChooser *chooser, gpointer user_data);
#endif
static void on_editable_changed (GtkEditable *editable, gpointer user_data);
static void combo_changed (GtkComboBox *widget, gpointer user_data);


RenameDlg::RenameDlgPriv::RenameDlgPriv(RenameDlg *parent) :
        m_pRenameDlg(parent)
{
	m_pDialogRename = NULL;
#ifdef HAVE_LIBGLADE
	m_pGladeXML = glade_xml_new (QUIVER_GLADEDIR "/" "quiver.glade", "RenameDialog", NULL);

	LoadWidgets();
	UpdateUI();
	ConnectSignals();
#endif
}

RenameDlg::RenameDlgPriv::~RenameDlgPriv()
{
	if (NULL != m_pGladeXML)
	{
		g_object_unref(m_pGladeXML);
		m_pGladeXML = NULL;
	}

	if (NULL != m_pDialogRename)
	{
		gtk_widget_destroy(GTK_WIDGET(m_pDialogRename));
		m_pDialogRename = NULL;
	}
}


void RenameDlg::RenameDlgPriv::LoadWidgets()
{
	m_pDialogRename         = GTK_DIALOG(glade_xml_get_widget (m_pGladeXML, "RenameDialog"));

	m_pBtnOK               = gtk_button_new_from_stock(QUIVER_STOCK_OK);
	gtk_widget_show(m_pBtnOK);
	gtk_container_add(GTK_CONTAINER(m_pDialogRename->action_area),m_pBtnOK);

	m_pTglBtnSubfolders       = GTK_TOGGLE_BUTTON( glade_xml_get_widget(m_pGladeXML, "rename_cb_subfolders") );

	GtkContainer* src_cont = GTK_CONTAINER( glade_xml_get_widget(m_pGladeXML, "rename_align_source_folder") );
	GtkContainer* dst_cont = GTK_CONTAINER( glade_xml_get_widget(m_pGladeXML, "rename_align_dest_folder") );
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
	m_pEntryTemplate        = GTK_ENTRY( glade_xml_get_widget(m_pGladeXML, "rename_entry_template") );

	m_pLabelExample           = GTK_LABEL( glade_xml_get_widget(m_pGladeXML, "rename_label_example") );

	m_bLoadedDlg = (
		NULL != m_pDialogRename        &&
		//NULL != m_pTglBtnCurrentSelection&&
		//NULL != m_pTglBtnFolder          &&
		//NULL != m_pTglBtnCopy            &&
		//NULL != m_pTglBtnMove            &&
		NULL != m_pTglBtnSubfolders      &&
#ifdef QUIVER_MAEMO
		NULL != m_pBtnSourceFolder     &&
		NULL != m_pBtnDestFolder       &&
#else
		NULL != m_pFCBtnSourceFolder     &&
		NULL != m_pFCBtnDestFolder       &&
#endif
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

void RenameDlg::RenameDlgPriv::UpdateUI()
{
	if (m_bLoadedDlg)
	{
#ifdef QUIVER_MAEMO
		const gchar* dir = gtk_button_get_label ( GTK_BUTTON (m_pBtnDestFolder));
		std::string strLabel = dir;
		strLabel += G_DIR_SEPARATOR_S;
		strLabel += gtk_entry_get_text(m_pEntryTemplate);
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
			// template ####
			strLabel += gtk_entry_get_text(m_pEntryTemplate);
			strLabel += "0001.jpg";
			gtk_label_set_text(m_pLabelExample, strLabel.c_str());
			g_free(dir);
		}
#endif
	}
}




void RenameDlg::RenameDlgPriv::ConnectSignals()
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

		g_signal_connect(m_pEntryTemplate,
			"changed",(GCallback)on_editable_changed,this);

	}
	
}

bool RenameDlg::RenameDlgPriv::ValidateInput()
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
		GnomeVFSURI* vuri_src = gnome_vfs_uri_new(src_uri);
		GnomeVFSURI* vuri_dst = gnome_vfs_uri_new(dst_uri);

		gboolean source_is_parent = 
			gnome_vfs_uri_is_parent (vuri_src, vuri_dst, TRUE);

		gboolean source_is_child = 
			gnome_vfs_uri_is_parent (vuri_dst, vuri_src, TRUE);

		gboolean source_is_dst = 
			gnome_vfs_uris_match(src_uri, dst_uri);

		gnome_vfs_uri_unref(vuri_src);
		gnome_vfs_uri_unref(vuri_dst);
		
		if ( (source_is_parent && m_pRenameDlg->GetIncludeSubfolders()) || source_is_child || source_is_dst)
		{
			bIsValid =false;
 
			GtkWidget* dialog = gtk_message_dialog_new (GTK_WINDOW(m_pDialogRename),
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
	RenameDlg::RenameDlgPriv *priv = static_cast<RenameDlg::RenameDlgPriv*>(user_data);
	if (GTK_BUTTON(priv->m_pBtnOK) == button)
	{
		if (priv->ValidateInput())
		{
			gtk_dialog_response(priv->m_pDialogRename, GTK_RESPONSE_OK);
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
	RenameDlg::RenameDlgPriv *priv = static_cast<RenameDlg::RenameDlgPriv*>(user_data);
	
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


static void on_editable_changed (GtkEditable *editable, gpointer user_data)
{
	RenameDlg::RenameDlgPriv *priv = static_cast<RenameDlg::RenameDlgPriv*>(user_data);
	priv->UpdateUI();
}

void combo_changed (GtkComboBox *widget, gpointer user_data)
{
	RenameDlg::RenameDlgPriv *priv = static_cast<RenameDlg::RenameDlgPriv*>(user_data);
	priv->UpdateUI();
}




