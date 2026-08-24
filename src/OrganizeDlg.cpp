#include <config.h>
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "OrganizeDlg.h"
#include "OrganizeTask.h"
#include "RenameTask.h"

#include "QuiverPrefs.h"
#include "Preferences.h"

#include "QuiverStockIcons.h"

#include <gio/gio.h>
#include <gtk/gtk.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

class OrganizeDlg::OrganizeDlgPriv
{
public:
// state shared with detached conflict-check workers; workers never
// touch the dialog object itself, so it can be destroyed anytime
	struct ConflictShared
	{
		std::mutex                    mutex;
		bool                          bDone;
		bool                          bFound;
		double                        dProgress;
		FileConflictCheck::ResultList vectResults;

		ConflictShared() : bDone(false), bFound(false), dProgress(0.) {}
	};

// constructor, destructor
	OrganizeDlgPriv(OrganizeDlg *parent);
	~OrganizeDlgPriv();

// methods
	void LoadWidgets();
	void UpdateUI();
	void ConnectSignals();

	bool ValidateInput();
	void StartConflictCheck();
	void CancelConflictCheck();
	void ApplyConflictResults(ConflictShared& state);
	std::string GetConflictInputKey() const;

	bool GetRenameFiles() const;

	static bool CollectAndCheck(const OrganizeTask::Options& opts,
			GCancellable* pCancellable,
			FileConflictCheck::ProgressFn fnProgress,
			gpointer pUserData,
			FileConflictCheck::ResultList& vectResults);

	static void ConflictProgressCb(double fraction, gpointer user_data);

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
	GtkWidget*              m_pLabelWarning;
	GtkProgressBar*         m_pProgressBar;
	GtkWidget*              m_pBtnViewConflicts;

	guint                   m_iConflictCheckID;
	guint                   m_iConflictPollID;
	GCancellable*           m_pConflictCancel;
	std::shared_ptr<ConflictShared>          m_pConflict;
	std::shared_ptr<std::atomic<int> >       m_pConflictGeneration;

	bool                    m_bConflictFound;
	FileConflictCheck::ResultList m_vectConflicts;
	std::string             m_strConflictKey;
	bool                    m_bConflictKeyValid;

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
static gboolean conflict_check_timeout_cb (gpointer user_data);
static gboolean conflict_poll_cb (gpointer user_data);
static void on_view_conflicts_clicked (GtkButton *button, gpointer user_data);


OrganizeDlg::OrganizeDlgPriv::OrganizeDlgPriv(OrganizeDlg *parent) :
        m_pOrganizeDlg(parent)
{
	m_pDialogOrganize = NULL;
	m_pLabelWarning = NULL;
	m_iConflictCheckID = 0;
	m_iConflictPollID = 0;
	m_pConflictCancel = NULL;
	m_pConflict.reset(new ConflictShared());
	m_pConflictGeneration.reset(new std::atomic<int>(0));
	m_bConflictFound = false;
	m_bConflictKeyValid = false;
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
	if (0 != m_iConflictCheckID)
	{
		g_source_remove(m_iConflictCheckID);
		m_iConflictCheckID = 0;
	}

	CancelConflictCheck();

	if (0 != m_iConflictPollID)
	{
		g_source_remove(m_iConflictPollID);
		m_iConflictPollID = 0;
	}

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

		GtkWidget* content_area =
			gtk_dialog_get_content_area(m_pDialogOrganize);

		m_pLabelWarning = gtk_label_new(NULL);
		gtk_label_set_use_markup(GTK_LABEL(m_pLabelWarning), TRUE);
		gtk_label_set_line_wrap(GTK_LABEL(m_pLabelWarning), TRUE);
		gtk_label_set_xalign(GTK_LABEL(m_pLabelWarning), 0.0);
		gtk_widget_set_no_show_all(m_pLabelWarning, TRUE);
		gtk_widget_set_margin_top(m_pLabelWarning, 6);
		gtk_box_pack_start(GTK_BOX(content_area), m_pLabelWarning, FALSE, FALSE, 0);

		m_pBtnViewConflicts = gtk_button_new_with_mnemonic("_View Conflicts…");
		gtk_widget_set_no_show_all(m_pBtnViewConflicts, TRUE);
		gtk_widget_set_halign(m_pBtnViewConflicts, GTK_ALIGN_START);
		g_signal_connect(m_pBtnViewConflicts,
			"clicked",(GCallback)on_view_conflicts_clicked,this);
		gtk_box_pack_start(GTK_BOX(content_area), m_pBtnViewConflicts, FALSE, FALSE, 0);

		m_pProgressBar = GTK_PROGRESS_BAR(gtk_progress_bar_new());
		gtk_widget_set_no_show_all(GTK_WIDGET(m_pProgressBar), TRUE);
		gtk_box_pack_start(GTK_BOX(content_area), GTK_WIDGET(m_pProgressBar), FALSE, FALSE, 0);
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
		}
		g_date_time_unref(time);
		gtk_label_set_text(m_pLabelExample, strLabel.c_str());

		gtk_widget_set_sensitive(GTK_WIDGET(m_pEntryTemplateFile), GetRenameFiles() ? TRUE : FALSE);

		if (0 != m_iConflictCheckID)
		{
			g_source_remove(m_iConflictCheckID);
			m_iConflictCheckID = 0;
		}
		m_iConflictCheckID =
			g_timeout_add(400, conflict_check_timeout_cb, this);
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

	if (0 != m_iConflictCheckID)
	{
		g_source_remove(m_iConflictCheckID);
		m_iConflictCheckID = 0;
	}

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

	// conflict gate: reuse the threaded result when it matches the
	// current inputs, otherwise run one synchronous check
	if (bIsValid)
	{
		std::string strKey = GetConflictInputKey();
		bool bConflictFound = false;
		size_t nConflicts = 0;

		if (m_bConflictKeyValid && m_strConflictKey == strKey)
		{
			bConflictFound = m_bConflictFound;
			for (size_t i = 0 ; i < m_vectConflicts.size() ; i++)
			{
				if (m_vectConflicts[i].HasConflict())
					nConflicts++;
			}
		}
		else
		{
			CancelConflictCheck();

			OrganizeTask::Options o;
			gchar* s = gtk_file_chooser_get_uri(GTK_FILE_CHOOSER(m_pFCBtnSourceFolder));
			if (NULL != s) { o.strSrcDirURI = s; g_free(s); }
			gchar* d = gtk_file_chooser_get_uri(GTK_FILE_CHOOSER(m_pFCBtnDestFolder));
			if (NULL != d) { o.strDestDirURI = d; g_free(d); }
			gchar* c = gtk_combo_box_text_get_active_text(m_pComboTemplateFolder);
			if (NULL != c) { o.strFolderTemplate = c; g_free(c); }
			o.strFileTemplate = gtk_entry_get_text(m_pEntryTemplateFile);
			o.strAppendedText = gtk_entry_get_text(m_pEntryFolderName);
			o.bIncludeSubfolders = (TRUE == gtk_toggle_button_get_active(m_pTglBtnSubfolders));
			o.bRenameFiles = GetRenameFiles();
			o.iDayExtension = gtk_spin_button_get_value_as_int(m_pSpinExtension);

			FileConflictCheck::ResultList vectResults;
			if (!o.strSrcDirURI.empty() && !o.strDestDirURI.empty() &&
				CollectAndCheck(o, NULL, NULL, NULL, vectResults))
			{
				m_vectConflicts = std::move(vectResults);
				m_bConflictFound = true;
				for (size_t i = 0 ; i < m_vectConflicts.size() ; i++)
				{
					if (m_vectConflicts[i].HasConflict())
						nConflicts++;
				}
				bConflictFound = true;
			}
			else
			{
				m_bConflictFound = false;
			}
		}

		if (bConflictFound)
		{
			bIsValid = false;
			GtkWidget* dialog = gtk_message_dialog_new(GTK_WINDOW(m_pDialogOrganize),
				GTK_DIALOG_DESTROY_WITH_PARENT,
				GTK_MESSAGE_ERROR,
				GTK_BUTTONS_CLOSE,
				"%d file(s) would collide with existing or generated names. "
				"Use the conflict list to review them.",
				(int)nConflicts);
			gtk_window_set_title(GTK_WINDOW(dialog), "Organize Conflict");
			gtk_dialog_run(GTK_DIALOG(dialog));
			gtk_widget_destroy(dialog);
		}
	}

	return bIsValid;
}

bool OrganizeDlg::OrganizeDlgPriv::GetRenameFiles() const
{
	return (TRUE == gtk_toggle_button_get_active(m_pTglBtnRenameFiles));
}

bool OrganizeDlg::OrganizeDlgPriv::CollectAndCheck(const OrganizeTask::Options& opts,
	GCancellable* pCancellable,
	FileConflictCheck::ProgressFn fnProgress,
	gpointer pUserData,
	FileConflictCheck::ResultList& vectResults)
{
	std::vector<FileConflictCheck::Mapping> vectMappings;
	if (!OrganizeTask::ComputeMappings(opts, vectMappings))
	{
		return false;
	}

	return FileConflictCheck::Check(
		vectMappings, vectResults, pCancellable, fnProgress, pUserData);
}

void OrganizeDlg::OrganizeDlgPriv::ConflictProgressCb(double fraction, gpointer user_data)
{
	ConflictShared* pState = static_cast<ConflictShared*>(user_data);
	std::lock_guard<std::mutex> lock(pState->mutex);
	pState->dProgress = fraction;
}

void OrganizeDlg::OrganizeDlgPriv::StartConflictCheck()
{
	CancelConflictCheck();

	if (0 != m_iConflictPollID)
	{
		g_source_remove(m_iConflictPollID);
		m_iConflictPollID = 0;
	}

	m_bConflictKeyValid = false;

	gtk_widget_hide(m_pLabelWarning);
	gtk_widget_hide(m_pBtnViewConflicts);

	OrganizeTask::Options opts;

	gchar* dir = gtk_file_chooser_get_uri(GTK_FILE_CHOOSER(m_pFCBtnSourceFolder));
	if (NULL != dir) { opts.strSrcDirURI = dir; g_free(dir); }
	dir = gtk_file_chooser_get_uri(GTK_FILE_CHOOSER(m_pFCBtnDestFolder));
	if (NULL != dir) { opts.strDestDirURI = dir; g_free(dir); }

	gchar* szCombo = gtk_combo_box_text_get_active_text(m_pComboTemplateFolder);
	if (NULL != szCombo)
	{
		opts.strFolderTemplate = szCombo;
		g_free(szCombo);
	}
	opts.strFileTemplate = gtk_entry_get_text(m_pEntryTemplateFile);
	opts.strAppendedText = gtk_entry_get_text(m_pEntryFolderName);
	opts.bIncludeSubfolders = (TRUE == gtk_toggle_button_get_active(m_pTglBtnSubfolders));
	opts.bRenameFiles = GetRenameFiles();
	opts.iDayExtension = gtk_spin_button_get_value_as_int(m_pSpinExtension);

	if (opts.strSrcDirURI.empty() || opts.strDestDirURI.empty())
	{
		m_bConflictFound = false;
		gtk_widget_set_sensitive(m_pBtnOK, TRUE);
		return;
	}

	gtk_widget_set_sensitive(m_pBtnOK, FALSE);
	gtk_progress_bar_set_fraction(m_pProgressBar, 0.);
	gtk_widget_show(GTK_WIDGET(m_pProgressBar));

	int iGeneration = ++(*m_pConflictGeneration);

	m_pConflictCancel = g_cancellable_new();
	GCancellable* pCancel = m_pConflictCancel;

	// hold separate refs: one for us, one for the worker
	g_object_ref(pCancel);

	std::shared_ptr<ConflictShared> pState = m_pConflict;
	std::shared_ptr<std::atomic<int> > pGen = m_pConflictGeneration;

	std::thread(
		[pState, pGen, iGeneration, opts, pCancel]()
		{
			FileConflictCheck::ResultList vectResults;
			bool bFound = CollectAndCheck(opts,
				pCancel, ConflictProgressCb, pState.get(), vectResults);

			if (NULL == pCancel || !g_cancellable_is_cancelled(pCancel))
			{
				bool bCurrent = (iGeneration == pGen->load());
				if (bCurrent)
				{
					std::lock_guard<std::mutex> lock(pState->mutex);
					pState->vectResults = std::move(vectResults);
					pState->bFound = bFound;
					pState->bDone = true;
				}
			}

			if (NULL != pCancel)
			{
				g_object_unref(pCancel);
			}
		}).detach();

	m_iConflictPollID = g_timeout_add(100, conflict_poll_cb, this);
}

void OrganizeDlg::OrganizeDlgPriv::CancelConflictCheck()
{
	if (NULL != m_pConflictCancel)
	{
		g_cancellable_cancel(m_pConflictCancel);
		g_object_unref(m_pConflictCancel);
		m_pConflictCancel = NULL;
	}

	// retire any in-flight worker; it holds its own refs and never
	// touches this object, so we never block waiting for it
	(*m_pConflictGeneration)++;
	m_pConflict.reset(new ConflictShared());
}

void OrganizeDlg::OrganizeDlgPriv::ApplyConflictResults(ConflictShared& state)
{
	gtk_widget_hide(GTK_WIDGET(m_pProgressBar));

	{
		std::lock_guard<std::mutex> lock(state.mutex);
		m_vectConflicts = state.vectResults;
		m_bConflictFound = state.bFound;
	}

	size_t nConflicts = 0;
	for (size_t i = 0 ; i < m_vectConflicts.size() ; i++)
	{
		if (m_vectConflicts[i].HasConflict())
			nConflicts++;
	}

	if (m_bConflictFound && !m_vectConflicts.empty())
	{
		std::string strDetails;
		size_t nShown = MIN(nConflicts, (size_t)3);
		for (size_t i = 0 ; nShown > 0 ; i++)
		{
			if (!m_vectConflicts[i].HasConflict())
				continue;
			strDetails += "\n" + m_vectConflicts[i].strSrcName
				+ " -> " + m_vectConflicts[i].strDstName;
			nShown--;
		}
		if (nConflicts > 3)
		{
			char szMore[64];
			g_snprintf(szMore, sizeof(szMore), "\n… and %d more",
				(int)(nConflicts - 3));
			strDetails += szMore;
		}

		std::string strMsg = "<span foreground=\"#e01b24\"><b>Organize conflict:</b> "
			+ std::to_string(nConflicts)
			+ " file(s) would collide with existing or generated names."
			+ "</span>" + strDetails;
		gtk_label_set_markup(GTK_LABEL(m_pLabelWarning), strMsg.c_str());
		gtk_widget_show(m_pLabelWarning);
		gtk_widget_show(m_pBtnViewConflicts);
	}

	gtk_widget_set_sensitive(m_pBtnOK, !m_bConflictFound);

	m_strConflictKey = GetConflictInputKey();
	m_bConflictKeyValid = true;
}

std::string OrganizeDlg::OrganizeDlgPriv::GetConflictInputKey() const
{
	std::string strKey;
	gchar* dir = gtk_file_chooser_get_uri(GTK_FILE_CHOOSER(m_pFCBtnSourceFolder));
	if (NULL != dir) { strKey += dir; g_free(dir); }
	strKey += "\n";
	dir = gtk_file_chooser_get_uri(GTK_FILE_CHOOSER(m_pFCBtnDestFolder));
	if (NULL != dir) { strKey += dir; g_free(dir); }
	strKey += "\n";
	gchar* szCombo = gtk_combo_box_text_get_active_text(m_pComboTemplateFolder);
	if (NULL != szCombo) { strKey += szCombo; g_free(szCombo); }
	strKey += "\n";
	strKey += gtk_entry_get_text(m_pEntryTemplateFile);
	strKey += "\n";
	strKey += gtk_entry_get_text(m_pEntryFolderName);
	strKey += (GetRenameFiles() ? "\nR" : "\nr");
	strKey += (TRUE == gtk_toggle_button_get_active(m_pTglBtnSubfolders) ? "S" : "s");
	char szExt[16];
	g_snprintf(szExt, sizeof(szExt), "%d",
		gtk_spin_button_get_value_as_int(m_pSpinExtension));
	strKey += szExt;
	return strKey;
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

static gboolean conflict_check_timeout_cb (gpointer user_data)
{
	OrganizeDlg::OrganizeDlgPriv *priv = static_cast<OrganizeDlg::OrganizeDlgPriv*>(user_data);
	priv->m_iConflictCheckID = 0;
	priv->StartConflictCheck();
	return FALSE;
}

static gboolean conflict_poll_cb (gpointer user_data)
{
	OrganizeDlg::OrganizeDlgPriv *priv = static_cast<OrganizeDlg::OrganizeDlgPriv*>(user_data);

	std::shared_ptr<OrganizeDlg::OrganizeDlgPriv::ConflictShared> pState =
		priv->m_pConflict;

	double dProgress = 0.;
	bool bDone = false;
	{
		std::lock_guard<std::mutex> lock(pState->mutex);
		dProgress = pState->dProgress;
		bDone = pState->bDone;
	}

	gtk_progress_bar_set_fraction(priv->m_pProgressBar,
		CLAMP(dProgress, 0., 1.));

	if (!bDone)
		return TRUE;

	priv->m_iConflictPollID = 0;
	priv->ApplyConflictResults(*pState);
	return FALSE;
}

static void on_view_conflicts_clicked (GtkButton *button, gpointer user_data)
{ (void)button;
	OrganizeDlg::OrganizeDlgPriv *priv = static_cast<OrganizeDlg::OrganizeDlgPriv*>(user_data);
	FileConflictCheck::ShowResultsDialog(
		GTK_WINDOW(priv->m_pDialogOrganize), priv->m_vectConflicts);
}




