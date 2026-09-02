#include <config.h>
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
#include <utility>

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
	GtkWidget*              m_pDialogOrganize;

	GtkWidget*              m_pBtnOK;

	GtkDropDown*            m_pComboTemplateFolder;
	GtkEntry*               m_pEntryTemplateFile;
	GtkWidget*              m_pFCBtnSourceFolder;
	GtkWidget*              m_pFCBtnDestFolder;
	std::string             m_strSrcFolder;
	std::string             m_strDestFolder;
	GtkCheckButton*        m_pTglBtnSubfolders;
	GtkCheckButton*        m_pTglBtnRenameFiles;
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
		GtkWidget* dlg = m_PrivPtr->m_pDialogOrganize;
		int iResult = GTK_RESPONSE_NONE;
		g_object_set_data(G_OBJECT(dlg), "organize-result", &iResult);
		gtk_widget_set_visible(dlg, TRUE);
		GMainLoop* loop = g_main_loop_new(NULL, FALSE);
		gulong doneId = g_signal_connect_swapped(dlg, "destroy",
			G_CALLBACK(g_main_loop_quit), loop);
		g_main_loop_run(loop);
		g_signal_handler_disconnect(dlg, doneId);
		g_object_set_data(G_OBJECT(dlg), "organize-result", NULL);
		g_main_loop_unref(loop);
		return (GTK_RESPONSE_OK == iResult);
	}
	return false;
}

std::string OrganizeDlg::GetFolderTemplate() const
{
	GtkStringObject* pItem = GTK_STRING_OBJECT(
		gtk_drop_down_get_selected_item(m_PrivPtr->m_pComboTemplateFolder));
	if (NULL == pItem)
		return std::string();
	return gtk_string_object_get_string(pItem);
}

std::string OrganizeDlg::GetFileTemplate() const
{
	return gtk_editable_get_text(GTK_EDITABLE(m_PrivPtr->m_pEntryTemplateFile));
}

std::string OrganizeDlg::GetOutputFolder() const
{
	return m_PrivPtr->m_strDestFolder;
}

std::string OrganizeDlg::GetAppendedText() const
{
	return gtk_editable_get_text(GTK_EDITABLE(m_PrivPtr->m_pEntryFolderName));
}

std::string OrganizeDlg::GetInputFolder() const
{
	return m_PrivPtr->m_strSrcFolder;
}

void OrganizeDlg::SetInputFolder(std::string dir)
{
	m_PrivPtr->m_strSrcFolder = dir;
}


int OrganizeDlg::GetDayExtention() const
{
	return gtk_spin_button_get_value_as_int (m_PrivPtr->m_pSpinExtension);
}

bool OrganizeDlg::GetIncludeSubfolders() const
{
	return (TRUE == gtk_check_button_get_active(m_PrivPtr->m_pTglBtnSubfolders));
}

bool OrganizeDlg::GetRenameFiles() const
{
	return m_PrivPtr->GetRenameFiles();
}



// private stuff


// prototypes
static void  on_clicked (GtkButton *button, gpointer   user_data);
static void on_folder_change (GtkButton *button, gpointer user_data);
static void on_editable_changed (GtkEditable *editable, gpointer user_data);
static void combo_changed (GObject *widget, gpointer user_data);
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
	const gchar* objectids[] = {
		"OrganizeDialog",
		"adjustment8",
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
		gtk_window_destroy(GTK_WINDOW(m_pDialogOrganize));
		m_pDialogOrganize = NULL;
	}
}


void OrganizeDlg::OrganizeDlgPriv::LoadWidgets()
{
	m_pDialogOrganize         = GTK_WIDGET(gtk_builder_get_object (m_pGtkBuilder, "OrganizeDialog"));

	m_pBtnOK = gtk_button_new_with_mnemonic("_OK");
	if (m_pDialogOrganize)
	{
		GtkHeaderBar* hbar = GTK_HEADER_BAR(gtk_header_bar_new());
		gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(hbar), TRUE);
		gtk_header_bar_pack_end(hbar, GTK_WIDGET(m_pBtnOK));
		gtk_window_set_titlebar(GTK_WINDOW(m_pDialogOrganize), GTK_WIDGET(hbar));
	}
	m_pComboTemplateFolder       = GTK_DROP_DOWN( gtk_builder_get_object(m_pGtkBuilder, "organize_combo_template") );
	if (NULL != m_pComboTemplateFolder)
	{
		const char* templates[] = {
			"YYYY/YYYY-MM-DD",
			"YYYY/MM/DD",
			"YYYY/MM-DD",
			"YYYY-MM-DD",
			NULL};
		GtkStringList* model = gtk_string_list_new(templates);
		gtk_drop_down_set_model(m_pComboTemplateFolder, G_LIST_MODEL(model));
		g_object_unref(model);
	}
	m_pEntryTemplateFile       = GTK_ENTRY( gtk_builder_get_object(m_pGtkBuilder, "organize_entry_filename_template") );
	//m_pTglBtnCurrentSelection = GTK_TOGGLE_BUTTON( gtk_builder_get_object(m_pGtkBuilder, "organize_rb_current_selection") );
	//m_pTglBtnFolder           = GTK_TOGGLE_BUTTON( gtk_builder_get_object(m_pGtkBuilder, "organize_rb_folder") );
	//m_pTglBtnCopy             = GTK_TOGGLE_BUTTON( gtk_builder_get_object(m_pGtkBuilder, "organize_rb_copy") );
	//m_pTglBtnMove             = GTK_TOGGLE_BUTTON( gtk_builder_get_object(m_pGtkBuilder, "organize_rb_move") );
	m_pSpinExtension            = GTK_SPIN_BUTTON( gtk_builder_get_object(m_pGtkBuilder, "organize_spinbutton_day_offset") );

	m_pTglBtnSubfolders       = GTK_CHECK_BUTTON( gtk_builder_get_object(m_pGtkBuilder, "organize_cb_subfolders") );
	m_pTglBtnRenameFiles      = GTK_CHECK_BUTTON( gtk_builder_get_object(m_pGtkBuilder, "organize_cb_rename_files") );

	GtkWidget* src_cont = GTK_WIDGET( gtk_builder_get_object(m_pGtkBuilder, "organize_align_source_folder") );
	GtkWidget* dst_cont = GTK_WIDGET( gtk_builder_get_object(m_pGtkBuilder, "organize_align_dest_folder") );
		m_pFCBtnSourceFolder = gtk_button_new_with_label("Choose Source Folder…");
		gtk_widget_set_halign(m_pFCBtnSourceFolder, GTK_ALIGN_START);
		m_pFCBtnDestFolder = gtk_button_new_with_label("Choose Destination Folder…");
		gtk_widget_set_halign(m_pFCBtnDestFolder, GTK_ALIGN_START);
		if (NULL != src_cont)
			gtk_widget_set_parent(m_pFCBtnSourceFolder, src_cont);
		if (NULL != dst_cont)
			gtk_widget_set_parent(m_pFCBtnDestFolder, dst_cont);
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

		gtk_drop_down_set_selected(m_pComboTemplateFolder, 0);


		PreferencesPtr prefs = Preferences::GetInstance();
		std::string strPhotoLibrary = prefs->GetString(QUIVER_PREFS_APP,QUIVER_PREFS_APP_PHOTO_LIBRARY);
		if (!strPhotoLibrary.empty())
		{
			m_strDestFolder = strPhotoLibrary;
		}

		GtkWidget* content_area =
			gtk_window_get_child(GTK_WINDOW(m_pDialogOrganize));

		m_pLabelWarning = gtk_label_new(NULL);
		gtk_label_set_use_markup(GTK_LABEL(m_pLabelWarning), TRUE);
		gtk_label_set_wrap(GTK_LABEL(m_pLabelWarning), TRUE);
		gtk_label_set_xalign(GTK_LABEL(m_pLabelWarning), 0.0);
		gtk_label_set_wrap_mode(GTK_LABEL(m_pLabelWarning), PANGO_WRAP_WORD_CHAR);
		gtk_widget_set_margin_top(m_pLabelWarning, 6);
		gtk_box_append(GTK_BOX(content_area), m_pLabelWarning);

		m_pBtnViewConflicts = gtk_button_new_with_mnemonic("_View Conflicts…");
		gtk_widget_set_halign(m_pBtnViewConflicts, GTK_ALIGN_START);
		g_signal_connect(m_pBtnViewConflicts,
			"clicked",(GCallback)on_view_conflicts_clicked,this);
		gtk_box_append(GTK_BOX(content_area), m_pBtnViewConflicts);

		m_pProgressBar = GTK_PROGRESS_BAR(gtk_progress_bar_new());
		gtk_box_append(GTK_BOX(content_area), GTK_WIDGET(m_pProgressBar));
	}
}

void OrganizeDlg::OrganizeDlgPriv::UpdateUI()
{
	if (m_bLoadedDlg)
	{
		std::string strLabel = m_strDestFolder;
		GDateTime* time = g_date_time_new_now_local();

		strLabel += G_DIR_SEPARATOR_S;
		strLabel += m_pOrganizeDlg->GetFolderTemplate();
		strLabel += gtk_editable_get_text(GTK_EDITABLE(m_pEntryFolderName));
		strLabel = OrganizeTask::DoVariableSubstitution(strLabel, time);
		if (GetRenameFiles())
		{
			strLabel += G_DIR_SEPARATOR_S;
			std::string strFileName = gtk_editable_get_text(GTK_EDITABLE(m_pEntryTemplateFile));
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
			"clicked",(GCallback)on_folder_change,this);
		g_signal_connect(m_pFCBtnDestFolder,
			"clicked",(GCallback)on_folder_change,this);

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
			"notify::selected",(GCallback)combo_changed,this);

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

	const std::string& src_uri = m_strSrcFolder;
	const std::string& dst_uri = m_strDestFolder;

	if (!src_uri.empty() && !dst_uri.empty())
	{
		GFile* file_src = g_file_new_for_uri(src_uri.c_str());
		GFile* file_dst = g_file_new_for_uri(dst_uri.c_str());

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

			GtkAlertDialog* alert = gtk_alert_dialog_new(
				"Source and Destination folders overlap. Please choose a different destination folder.");
			gtk_alert_dialog_show(alert, GTK_WINDOW(m_pDialogOrganize));
		}
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
			o.strSrcDirURI = m_strSrcFolder;
			o.strDestDirURI = m_strDestFolder;
			gchar* c = g_strdup(m_pOrganizeDlg->GetFolderTemplate().c_str());
			if (NULL != c) { o.strFolderTemplate = c; g_free(c); }
			o.strFileTemplate = gtk_editable_get_text(GTK_EDITABLE(m_pEntryTemplateFile));
			o.strAppendedText = gtk_editable_get_text(GTK_EDITABLE(m_pEntryFolderName));
			o.bIncludeSubfolders = (TRUE == gtk_check_button_get_active(m_pTglBtnSubfolders));
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
			gchar* detail = g_strdup_printf(
				"%d file(s) would collide with existing or generated names. "
				"Use the conflict list to review them.",
				(int)nConflicts);
			GtkAlertDialog* alert = gtk_alert_dialog_new("%s", detail);
			gtk_alert_dialog_show(alert, GTK_WINDOW(m_pDialogOrganize));
			g_free(detail);
		}
	}

	return bIsValid;
}

bool OrganizeDlg::OrganizeDlgPriv::GetRenameFiles() const
{
	return (TRUE == gtk_check_button_get_active(m_pTglBtnRenameFiles));
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

	gtk_widget_set_visible(m_pLabelWarning, FALSE);
	gtk_widget_set_visible(m_pBtnViewConflicts, FALSE);

	OrganizeTask::Options opts;

	opts.strSrcDirURI = m_strSrcFolder;
	opts.strDestDirURI = m_strDestFolder;

	gchar* szCombo = g_strdup(m_pOrganizeDlg->GetFolderTemplate().c_str());
	if (NULL != szCombo)
	{
		opts.strFolderTemplate = szCombo;
		g_free(szCombo);
	}
	opts.strFileTemplate = gtk_editable_get_text(GTK_EDITABLE(m_pEntryTemplateFile));
	opts.strAppendedText = gtk_editable_get_text(GTK_EDITABLE(m_pEntryFolderName));
	opts.bIncludeSubfolders = (TRUE == gtk_check_button_get_active(m_pTglBtnSubfolders));
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
	gtk_widget_set_visible(GTK_WIDGET(m_pProgressBar), FALSE);

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
	}

	gtk_widget_set_sensitive(m_pBtnOK, !m_bConflictFound);

	m_strConflictKey = GetConflictInputKey();
	m_bConflictKeyValid = true;
}

std::string OrganizeDlg::OrganizeDlgPriv::GetConflictInputKey() const
{
	std::string strKey = m_strSrcFolder;
	strKey += "\n";
	strKey += m_strDestFolder;
	strKey += "\n";
	gchar* szCombo = g_strdup(m_pOrganizeDlg->GetFolderTemplate().c_str());
	if (NULL != szCombo) { strKey += szCombo; g_free(szCombo); }
	strKey += "\n";
	strKey += gtk_editable_get_text(GTK_EDITABLE(m_pEntryTemplateFile));
	strKey += "\n";
	strKey += gtk_editable_get_text(GTK_EDITABLE(m_pEntryFolderName));
	strKey += (GetRenameFiles() ? "\nR" : "\nr");
	strKey += (TRUE == gtk_check_button_get_active(m_pTglBtnSubfolders) ? "S" : "s");
	char szExt[16];
	g_snprintf(szExt, sizeof(szExt), "%d",
		gtk_spin_button_get_value_as_int(m_pSpinExtension));
	strKey += szExt;
	return strKey;
}

static void on_folder_selected(GObject* source, GAsyncResult* res, gpointer data)
{
	GtkFileDialog* dlg = GTK_FILE_DIALOG(source);
	GError* error = NULL;
	GFile* folder = gtk_file_dialog_select_folder_finish(dlg, res, &error);
	auto* ctx = static_cast<std::pair<OrganizeDlg::OrganizeDlgPriv*, std::string*>*>(data);
	if (folder != NULL)
	{
		gchar* uri = g_file_get_uri(folder);
		*(ctx->second) = uri;
		g_free(uri);
		g_object_unref(folder);
		ctx->first->UpdateUI();
	}
	if (error != NULL)
		g_error_free(error);
	delete ctx;
	g_object_unref(dlg);
}

static void on_folder_change (GtkButton *button, gpointer user_data)
{
	OrganizeDlg::OrganizeDlgPriv *priv = static_cast<OrganizeDlg::OrganizeDlgPriv*>(user_data);

	bool bSource = (GTK_WIDGET(button) == priv->m_pFCBtnSourceFolder);
	std::pair<OrganizeDlg::OrganizeDlgPriv*, std::string*>* ctx =
		new std::pair<OrganizeDlg::OrganizeDlgPriv*, std::string*>(priv,
			bSource ? &priv->m_strSrcFolder : &priv->m_strDestFolder);

	GtkFileDialog* dlg = gtk_file_dialog_new();
	gtk_file_dialog_set_title(dlg, "Choose Folder");
	gtk_file_dialog_select_folder(dlg, GTK_WINDOW(priv->m_pDialogOrganize), NULL,
		on_folder_selected, ctx);
}

static void  on_clicked (GtkButton *button, gpointer   user_data)
{
	OrganizeDlg::OrganizeDlgPriv *priv = static_cast<OrganizeDlg::OrganizeDlgPriv*>(user_data);
	if (GTK_BUTTON(priv->m_pBtnOK) == button)
	{
		if (priv->ValidateInput())
		{
			int* pResult = (int*)g_object_get_data(G_OBJECT(priv->m_pDialogOrganize), "organize-result");
			if (pResult)
				*pResult = GTK_RESPONSE_OK;
			gtk_window_destroy(GTK_WINDOW(priv->m_pDialogOrganize));
			priv->m_pDialogOrganize = NULL;
		}
	}
	else if (button == GTK_BUTTON(priv->m_pTglBtnRenameFiles))
	{
		priv->UpdateUI();
	}
}

static void on_editable_changed (GtkEditable *editable, gpointer user_data)
{
	(void)editable;
	OrganizeDlg::OrganizeDlgPriv *priv = static_cast<OrganizeDlg::OrganizeDlgPriv*>(user_data);
	priv->UpdateUI();
}

static void combo_changed (GObject *widget, gpointer user_data)
{
	(void)widget;
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




