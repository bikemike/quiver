#include <config.h>
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "RenameDlg.h"
#include "RenameTask.h"

#include "QuiverPrefs.h"
#include "Preferences.h"

#include "QuiverStockIcons.h"

#include <gio/gio.h>
#include <gtk/gtk.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <boost/algorithm/string/classification.hpp>

extern "C"
{
#include "strnatcmp.h"
}

class RenameDlg::RenameDlgPriv
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
		std::string                   strStatus;
		FileConflictCheck::ResultList vectResults;

		ConflictShared() : bDone(false), bFound(false), dProgress(0.) {}
	};

// maximum rows rendered in the live preview table
	static const size_t PREVIEW_ROW_CAP = 500;

// constructor, destructor
	RenameDlgPriv(RenameDlg *parent);
	~RenameDlgPriv();

// methods
	void LoadWidgets();
	void UpdateUI();
	void ConnectSignals();

	bool ValidateInput();
	void StartConflictCheck();
	void CancelConflictCheck();
	void ApplyConflictResults(ConflictShared& state);
	std::string GetFolderURI() const;
	std::string GetConflictInputKey() const;

	static bool CollectAndCheck(const std::string& strFolder,
			const std::string& strTemplate,
			GCancellable* pCancellable,
			ConflictShared* pState,
			FileConflictCheck::ResultList& vectResults);

	static void ConflictProgressCb(double fraction, gpointer user_data);

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
	GtkWidget*              m_pLabelPurpose;
	GtkWidget*              m_pLabelWarning;
	GtkWidget*              m_pLabelStatus;
	GtkProgressBar*         m_pProgressBar;
	GtkWidget*              m_pScrolledPreview;
	GtkTreeView*            m_pTreeViewPreview;
	GtkListStore*           m_pListStorePreview;

	guint                   m_iConflictCheckID;
	guint                   m_iConflictPollID;
	GCancellable*           m_pConflictCancel;
	std::shared_ptr<ConflictShared>          m_pConflict;
	std::shared_ptr<std::atomic<int> >       m_pConflictGeneration;

	bool                    m_bConflictFound;
	FileConflictCheck::ResultList m_vectConflicts;
	std::string             m_strConflictKey;
	bool                    m_bConflictKeyValid;

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
static gboolean conflict_check_timeout_cb (gpointer user_data);
static gboolean conflict_poll_cb (gpointer user_data);
[[maybe_unused]] static void __attribute__((unused)) combo_changed (GtkComboBox *widget, gpointer user_data);

enum
{
	PREVIEW_COL_ICON = 0,
	PREVIEW_COL_SRC,
	PREVIEW_COL_TYPE,
	PREVIEW_COL_DST,
	PREVIEW_COL_CONFLICT,
	PREVIEW_COL_COUNT
};

static GdkPixbuf* preview_icon_pixbuf(const std::string& strIconName)
{
	static std::map<std::string, GdkPixbuf*> s_mapIconCache;
	if (strIconName.empty())
		return NULL;

	std::map<std::string, GdkPixbuf*>::iterator itr = s_mapIconCache.find(strIconName);
	if (s_mapIconCache.end() != itr)
	{
		return itr->second;
	}

	GdkPixbuf* pixbuf = gtk_icon_theme_load_icon(
		gtk_icon_theme_get_default(), strIconName.c_str(),
		16, (GtkIconLookupFlags)(GTK_ICON_LOOKUP_USE_BUILTIN |
			GTK_ICON_LOOKUP_GENERIC_FALLBACK), NULL);
	if (NULL == pixbuf)
	{
		pixbuf = gtk_icon_theme_load_icon(
			gtk_icon_theme_get_default(), "text-x-generic",
			16, GTK_ICON_LOOKUP_USE_BUILTIN, NULL);
	}
	s_mapIconCache[strIconName] = pixbuf;
	return pixbuf;
}

static void preview_cell_color(GtkTreeViewColumn* column,
	GtkCellRenderer* renderer, GtkTreeModel* model, GtkTreeIter* iter,
	gpointer user_data)
{ (void)column;  (void)user_data;
	gchar* szConflict = NULL;
	gtk_tree_model_get(model, iter, PREVIEW_COL_CONFLICT, &szConflict, -1);
	gboolean bConflicted = (NULL != szConflict && '\0' != szConflict[0]);
	g_object_set(renderer,
		"foreground", bConflicted ? "#e01b24" : NULL,
		"foreground-set", bConflicted ? TRUE : FALSE, NULL);
	g_free(szConflict);
}


RenameDlg::RenameDlgPriv::RenameDlgPriv(RenameDlg *parent) :
        m_pRenameDlg(parent)
{
	m_pDialogRename = NULL;
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
		(gchar*)"RenameDialog",
		NULL};
	gtk_builder_add_objects_from_file(m_pGtkBuilder, QUIVER_DATADIR "/" "quiver.ui", objectids, NULL);

	LoadWidgets();
	UpdateUI();
	ConnectSignals();
}

RenameDlg::RenameDlgPriv::~RenameDlgPriv()
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

		GtkWidget* content_area =
			gtk_dialog_get_content_area(m_pDialogRename);

		m_pLabelPurpose = gtk_label_new(NULL);
		gtk_label_set_markup(GTK_LABEL(m_pLabelPurpose),
			"Renames every supported photo or video in the selected folder "
			"using the pattern below. Use <b>#</b> characters in the template "
			"for sequence numbers.");
		gtk_label_set_use_markup(GTK_LABEL(m_pLabelPurpose), TRUE);
		gtk_label_set_line_wrap(GTK_LABEL(m_pLabelPurpose), TRUE);
		gtk_label_set_xalign(GTK_LABEL(m_pLabelPurpose), 0.0);
		gtk_box_pack_start(GTK_BOX(content_area), m_pLabelPurpose, FALSE, FALSE, 0);
		gtk_box_reorder_child(GTK_BOX(content_area), m_pLabelPurpose, 0);
		gtk_widget_show(m_pLabelPurpose);

		m_pScrolledPreview = gtk_scrolled_window_new(NULL, NULL);
		gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(m_pScrolledPreview),
			GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
		gtk_widget_set_vexpand(m_pScrolledPreview, TRUE);
		gtk_widget_set_size_request(m_pScrolledPreview, -1, 150);

		m_pListStorePreview = gtk_list_store_new(PREVIEW_COL_COUNT,
			GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_STRING,
			G_TYPE_STRING, G_TYPE_STRING);
		m_pTreeViewPreview =
			GTK_TREE_VIEW(gtk_tree_view_new_with_model(
				GTK_TREE_MODEL(m_pListStorePreview)));
		g_object_unref(m_pListStorePreview);
		gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(m_pTreeViewPreview), TRUE);

		{
			GtkCellRenderer* renderer = gtk_cell_renderer_pixbuf_new();
			GtkTreeViewColumn* column = gtk_tree_view_column_new();
			gtk_tree_view_column_set_title(column, "");
			gtk_tree_view_column_pack_start(column, renderer, FALSE);
			gtk_tree_view_column_add_attribute(column, renderer, "pixbuf",
				PREVIEW_COL_ICON);
			gtk_tree_view_append_column(m_pTreeViewPreview, column);
		}
		static const char* szTitles[] = { NULL, "Original Name", "Type", "New Name", "Conflict" };
		for (int c = PREVIEW_COL_SRC ; c <= PREVIEW_COL_CONFLICT ; c++)
		{
			GtkCellRenderer* renderer = gtk_cell_renderer_text_new();
			g_object_set(renderer, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
			GtkTreeViewColumn* column = gtk_tree_view_column_new();
			gtk_tree_view_column_set_title(column, szTitles[c]);
			gtk_tree_view_column_pack_start(column, renderer, TRUE);
			gtk_tree_view_column_set_expand(column, TRUE);
			gtk_tree_view_column_add_attribute(column, renderer, "text", c);
			gtk_tree_view_column_set_cell_data_func(column, renderer,
				preview_cell_color, NULL, NULL);
			gtk_tree_view_append_column(m_pTreeViewPreview, column);
		}

		gtk_container_add(GTK_CONTAINER(m_pScrolledPreview),
			GTK_WIDGET(m_pTreeViewPreview));
		gtk_box_pack_start(GTK_BOX(content_area), m_pScrolledPreview, TRUE, TRUE, 4);
		gtk_widget_show_all(m_pScrolledPreview);

		m_pLabelStatus = gtk_label_new(NULL);
		gtk_label_set_use_markup(GTK_LABEL(m_pLabelStatus), TRUE);
		gtk_label_set_line_wrap(GTK_LABEL(m_pLabelStatus), TRUE);
		gtk_label_set_xalign(GTK_LABEL(m_pLabelStatus), 0.0);
		gtk_widget_set_no_show_all(m_pLabelStatus, TRUE);
		gtk_box_pack_start(GTK_BOX(content_area), m_pLabelStatus, FALSE, FALSE, 2);

		m_pProgressBar = GTK_PROGRESS_BAR(gtk_progress_bar_new());
		gtk_widget_set_no_show_all(GTK_WIDGET(m_pProgressBar), TRUE);
		gtk_box_pack_start(GTK_BOX(content_area), GTK_WIDGET(m_pProgressBar), FALSE, FALSE, 0);

		m_pLabelWarning = gtk_label_new(NULL);
		gtk_label_set_use_markup(GTK_LABEL(m_pLabelWarning), TRUE);
		gtk_label_set_line_wrap(GTK_LABEL(m_pLabelWarning), TRUE);
		gtk_label_set_xalign(GTK_LABEL(m_pLabelWarning), 0.0);
		gtk_widget_set_no_show_all(m_pLabelWarning, TRUE);
		gtk_widget_set_margin_top(m_pLabelWarning, 6);
		gtk_box_pack_start(GTK_BOX(content_area), m_pLabelWarning, FALSE, FALSE, 0);
	}
}

std::string RenameDlg::RenameDlgPriv::GetFolderURI() const
{
	std::string strFolder;
	gchar* dir = gtk_file_chooser_get_uri(GTK_FILE_CHOOSER(m_pFCBtnSourceFolder));
	if (NULL != dir)
	{
		strFolder = dir;
		g_free(dir);
	}
	return strFolder;
}

bool RenameDlg::RenameDlgPriv::CollectAndCheck(const std::string& strFolder,
	const std::string& strTemplate,
	GCancellable* pCancellable,
	ConflictShared* pState,
	FileConflictCheck::ResultList& vectResults)
{
	if (NULL != pState)
	{
		std::lock_guard<std::mutex> lock(pState->mutex);
		pState->strStatus = "Scanning folder…";
	}

	std::vector<FileConflictCheck::Mapping> vectMappings;
	bool bHaveMappings = RenameTask::ComputeMappings(
			strFolder, strTemplate, ImageList::SORT_BY_DATE, vectMappings,
			pCancellable,
			(NULL != pState) ? ConflictProgressCb : NULL,
			pState);
	if (!bHaveMappings)
	{
		return false;
	}

	if (NULL != pState &&
		(NULL == pCancellable || !g_cancellable_is_cancelled(pCancellable)))
	{
		std::lock_guard<std::mutex> lock(pState->mutex);
		pState->strStatus = "Checking for conflicts…";
	}

	return FileConflictCheck::Check(
		vectMappings, vectResults, pCancellable,
		(NULL != pState) ? ConflictProgressCb : NULL,
		pState);
}

void RenameDlg::RenameDlgPriv::ConflictProgressCb(double fraction, gpointer user_data)
{
	ConflictShared* pState = static_cast<ConflictShared*>(user_data);
	std::lock_guard<std::mutex> lock(pState->mutex);
	pState->dProgress = fraction;
}

std::string RenameDlg::RenameDlgPriv::GetConflictInputKey() const
{
	return GetFolderURI() + "\n" + gtk_entry_get_text(m_pEntryTemplate);
}

void RenameDlg::RenameDlgPriv::StartConflictCheck()
{
	CancelConflictCheck();

	if (0 != m_iConflictPollID)
	{
		g_source_remove(m_iConflictPollID);
		m_iConflictPollID = 0;
	}

	m_bConflictKeyValid = false;

	gtk_widget_hide(m_pLabelWarning);
	gtk_widget_hide(m_pLabelStatus);

	std::string strFolder = GetFolderURI();
	std::string strTemplate = gtk_entry_get_text(m_pEntryTemplate);

	if (strFolder.empty() || strTemplate.empty())
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
		[pState, pGen, iGeneration, strFolder, strTemplate, pCancel]()
		{
			FileConflictCheck::ResultList vectResults;
			bool bFound = CollectAndCheck(strFolder, strTemplate,
				pCancel, pState.get(), vectResults);

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

void RenameDlg::RenameDlgPriv::CancelConflictCheck()
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

void RenameDlg::RenameDlgPriv::ApplyConflictResults(ConflictShared& state)
{
	gtk_widget_hide(GTK_WIDGET(m_pProgressBar));

	{
		std::lock_guard<std::mutex> lock(state.mutex);
		m_vectConflicts = state.vectResults;
		m_bConflictFound = state.bFound;
	}

	// conflicted rows first, then by name (natural order)
	std::stable_sort(m_vectConflicts.begin(), m_vectConflicts.end(),
		[](const FileConflictCheck::Result& a, const FileConflictCheck::Result& b)
		{
			if (a.HasConflict() != b.HasConflict())
				return a.HasConflict();
			return 0 > strnatcasecmp(a.strSrcName.c_str(), b.strSrcName.c_str());
		});

	size_t nConflicts = 0;
	for (size_t i = 0 ; i < m_vectConflicts.size() ; i++)
	{
		if (m_vectConflicts[i].HasConflict())
			nConflicts++;
	}

	// live preview table
	gtk_list_store_clear(m_pListStorePreview);
	const size_t nRows = MIN(m_vectConflicts.size(), PREVIEW_ROW_CAP);
	for (size_t i = 0 ; i < nRows ; i++)
	{
		const FileConflictCheck::Result& r = m_vectConflicts[i];
		GtkTreeIter iter;
		gtk_list_store_append(m_pListStorePreview, &iter);
		gtk_list_store_set(m_pListStorePreview, &iter,
			PREVIEW_COL_ICON, preview_icon_pixbuf(r.strIconName),
			PREVIEW_COL_SRC, r.strSrcName.c_str(),
			PREVIEW_COL_TYPE, r.strTypeDescription.c_str(),
			PREVIEW_COL_DST, r.strDstName.c_str(),
			PREVIEW_COL_CONFLICT, r.strConflictWith.c_str(),
			-1);
	}
	if (m_vectConflicts.size() > PREVIEW_ROW_CAP)
	{
		char szMore[128];
		g_snprintf(szMore, sizeof(szMore), "… and %d more files",
			(int)(m_vectConflicts.size() - PREVIEW_ROW_CAP));
		GtkTreeIter iter;
		gtk_list_store_append(m_pListStorePreview, &iter);
		gtk_list_store_set(m_pListStorePreview, &iter,
			PREVIEW_COL_SRC, szMore, -1);
	}

	if (0 == m_vectConflicts.size())
	{
		gtk_label_set_markup(GTK_LABEL(m_pLabelStatus),
			"<span foreground=\"#77767e\">No supported files found "
			"in this folder.</span>");
		gtk_widget_show(m_pLabelStatus);
	}
	else if (m_bConflictFound)
	{
		gtk_label_set_markup(GTK_LABEL(m_pLabelStatus),
			(("<span foreground=\"#e01b24\"><b>")
			+ std::to_string(nConflicts)
			+ (1 == nConflicts ? " conflict" : " conflicts")
			+ " found.</b></span>").c_str());
		gtk_widget_show(m_pLabelStatus);

		std::string strMsg = "<span foreground=\"#e01b24\"><b>Rename conflict:</b> "
			+ std::to_string(nConflicts)
			+ " file(s) would collide with existing or generated names."
			"</span>";
		gtk_label_set_markup(GTK_LABEL(m_pLabelWarning), strMsg.c_str());
		gtk_widget_show(m_pLabelWarning);
	}
	else
	{
		gtk_widget_hide(m_pLabelWarning);
		gtk_label_set_markup(GTK_LABEL(m_pLabelStatus),
			"<span foreground=\"#26a269\"><b>No conflicts.</b></span>");
		gtk_widget_show(m_pLabelStatus);
	}

	gtk_widget_set_sensitive(m_pBtnOK, !m_bConflictFound);

	m_strConflictKey = GetConflictInputKey();
	m_bConflictKeyValid = true;
}

void RenameDlg::RenameDlgPriv::UpdateUI()
{
	if (m_bLoadedDlg)
	{
		std::string strTemplate = gtk_entry_get_text(m_pEntryTemplate);
		GDateTime* time = g_date_time_new_now_local();
		std::string strFileName = RenameTask::DoVariableSubstitution(strTemplate, time, 1);
		g_date_time_unref(time);
		gtk_label_set_text(m_pLabelExample, strFileName.c_str());

		if (0 != m_iConflictCheckID)
		{
			g_source_remove(m_iConflictCheckID);
			m_iConflictCheckID = 0;
		}
		m_iConflictCheckID =
			g_timeout_add(400, conflict_check_timeout_cb, this);
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

static gboolean conflict_check_timeout_cb (gpointer user_data)
{
	RenameDlg::RenameDlgPriv *priv = static_cast<RenameDlg::RenameDlgPriv*>(user_data);
	priv->m_iConflictCheckID = 0;
	priv->StartConflictCheck();
	return FALSE;
}

static gboolean conflict_poll_cb (gpointer user_data)
{
	RenameDlg::RenameDlgPriv *priv = static_cast<RenameDlg::RenameDlgPriv*>(user_data);

	std::shared_ptr<RenameDlg::RenameDlgPriv::ConflictShared> pState =
		priv->m_pConflict;

	double dProgress = 0.;
	bool bDone = false;
	std::string strStatus;
	{
		std::lock_guard<std::mutex> lock(pState->mutex);
		dProgress = pState->dProgress;
		bDone = pState->bDone;
		strStatus = pState->strStatus;
	}

	gtk_progress_bar_set_fraction(priv->m_pProgressBar,
		CLAMP(dProgress, 0., 1.));

	if (!strStatus.empty())
	{
		std::string strMarkup =
			"<span foreground=\"#77767e\">" + strStatus + "</span>";
		gtk_label_set_markup(GTK_LABEL(priv->m_pLabelStatus),
			strMarkup.c_str());
		gtk_widget_show(priv->m_pLabelStatus);
	}

	if (!bDone)
		return TRUE;

	priv->m_iConflictPollID = 0;
	priv->ApplyConflictResults(*pState);
	return FALSE;
}

bool RenameDlg::RenameDlgPriv::ValidateInput()
{
	bool bIsValid = true;
	std::string strMsg, strTitle;

	if (0 != m_iConflictCheckID)
	{
		g_source_remove(m_iConflictCheckID);
		m_iConflictCheckID = 0;
	}

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

			FileConflictCheck::ResultList vectResults;
			if (CollectAndCheck(
					GetFolderURI(), gtk_entry_get_text(m_pEntryTemplate),
					NULL, NULL, vectResults))
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
			strTitle = "Rename Conflict";
			strMsg = std::to_string(nConflicts)
				+ " file(s) would collide with existing or generated names."
				+ " Use the conflict list to review them.";
		}
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




