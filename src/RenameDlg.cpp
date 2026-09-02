#include <config.h>
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
	void SetFolderURI(const std::string& folder) { m_strFolderURI = folder; }
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
	GtkWidget*              m_pDialogRename;
	GtkWidget*              m_pBtnOK;

	GtkButton*              m_pBtnChooseFolder;
	std::string             m_strFolderURI;
	GtkEntry*               m_pEntryTemplate;
	GtkLabel*               m_pLabelExample;
	GtkWidget*              m_pLabelPurpose;
	GtkWidget*              m_pLabelWarning;
	GtkWidget*              m_pLabelStatus;
	GtkProgressBar*         m_pProgressBar;
	GtkWidget*              m_pScrolledPreview;
	GtkWidget*              m_pTreeViewPreview;
	GListStore*             m_pListStorePreview;

	guint                   m_iConflictCheckID;
	guint                   m_iConflictPollID;
	GCancellable*           m_pConflictCancel;
	std::shared_ptr<ConflictShared>          m_pConflict;
	std::shared_ptr<std::atomic<int> >       m_pConflictGeneration;

	bool                    m_bConflictFound;
	FileConflictCheck::ResultList m_vectConflicts;
	std::string             m_strConflictKey;
	bool                    m_bConflictKeyValid;

	bool                    m_bRunDone;
	gint                    m_iRunResponse;

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
	if (!m_PrivPtr->m_bLoadedDlg)
		return false;

	m_PrivPtr->m_bRunDone = false;
	m_PrivPtr->m_iRunResponse = GTK_RESPONSE_NONE;
	gtk_window_set_modal(GTK_WINDOW(m_PrivPtr->m_pDialogRename), TRUE);
	gtk_widget_set_visible(GTK_WIDGET(m_PrivPtr->m_pDialogRename), TRUE);

	GMainContext* ctx = g_main_context_default();
	while (!m_PrivPtr->m_bRunDone)
	{
		g_main_context_iteration(ctx, TRUE);
	}
	return (GTK_RESPONSE_OK == m_PrivPtr->m_iRunResponse);
}

std::string RenameDlg::GetTemplate() const
{
	return gtk_editable_get_text(GTK_EDITABLE(m_PrivPtr->m_pEntryTemplate));
}

std::string RenameDlg::GetInputFolder() const
{
	return m_PrivPtr->GetFolderURI();
}

void RenameDlg::SetInputFolder(std::string folder)
{
	m_PrivPtr->SetFolderURI(folder);
	if (m_PrivPtr->m_bLoadedDlg && NULL != m_PrivPtr->m_pBtnChooseFolder)
	{
		gtk_button_set_label(m_PrivPtr->m_pBtnChooseFolder, folder.c_str());
	}
}


// private stuff


// prototypes
static void on_clicked (GtkButton *button, gpointer user_data);
static void on_folder_change (GtkButton *button, gpointer user_data);
static void on_editable_changed (GtkEditable *editable, gpointer user_data);
static gboolean conflict_check_timeout_cb (gpointer user_data);
static gboolean conflict_poll_cb (gpointer user_data);

enum
{
	PREVIEW_COL_ICON = 0,
	PREVIEW_COL_SRC,
	PREVIEW_COL_TYPE,
	PREVIEW_COL_DST,
	PREVIEW_COL_CONFLICT,
	PREVIEW_COL_COUNT
};

static const char* preview_icon_pixbuf(const std::string& strIconName)
{
	if (strIconName.empty())
		return NULL;
	return strIconName.c_str();
}

// row item type for the preview column view
typedef struct {
	GObject  parent_instance;
	gchar*   icon_name;
	gchar*   src_name;
	gchar*   type_desc;
	gchar*   dst_name;
	gchar*   conflict;
} RenamePreviewItem;

typedef struct {
	GObjectClass parent_class;
} RenamePreviewItemClass;

#define RENAME_PREVIEW_ITEM_TYPE (rename_preview_item_get_type())
#define RENAME_PREVIEW_ITEM(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST((obj), RENAME_PREVIEW_ITEM_TYPE, RenamePreviewItem))

G_DEFINE_TYPE(RenamePreviewItem, rename_preview_item, G_TYPE_OBJECT)

static void rename_preview_item_finalize (GObject* object)
{
	RenamePreviewItem* item = RENAME_PREVIEW_ITEM(object);
	g_free(item->icon_name);
	g_free(item->src_name);
	g_free(item->type_desc);
	g_free(item->dst_name);
	g_free(item->conflict);
	G_OBJECT_CLASS(g_type_class_peek_parent(
		G_OBJECT_GET_CLASS(object)))->finalize(object);
}

static void rename_preview_item_class_init (RenamePreviewItemClass* klass)
{
	G_OBJECT_CLASS(klass)->finalize = rename_preview_item_finalize;
}

static void rename_preview_item_init (RenamePreviewItem* item)
{
	item->icon_name = NULL;
	item->src_name = NULL;
	item->type_desc = NULL;
	item->dst_name = NULL;
	item->conflict = NULL;
}

static RenamePreviewItem* rename_preview_item_new (const gchar* icon,
	const gchar* src, const gchar* type, const gchar* dst, const gchar* conflict)
{
	RenamePreviewItem* item = static_cast<RenamePreviewItem*>(
		g_object_new(RENAME_PREVIEW_ITEM_TYPE, NULL));
	item->icon_name = g_strdup(icon);
	item->src_name = g_strdup(src);
	item->type_desc = g_strdup(type);
	item->dst_name = g_strdup(dst);
	item->conflict = g_strdup(conflict);
	return item;
}

static void preview_icon_setup (GtkListItem* list_item, gpointer user_data)
{ (void)user_data;
	GtkWidget* image = gtk_image_new();
	gtk_image_set_icon_size(GTK_IMAGE(image), GTK_ICON_SIZE_NORMAL);
	gtk_widget_set_margin_start(image, 6);
	gtk_widget_set_margin_end(image, 6);
	gtk_list_item_set_child(list_item, image);
}

static void preview_icon_bind (GtkListItem* list_item, gpointer user_data)
{ (void)user_data;
	RenamePreviewItem* item =
		RENAME_PREVIEW_ITEM(gtk_list_item_get_item(list_item));
	GtkWidget* image = gtk_list_item_get_child(list_item);
	gtk_image_set_from_icon_name(GTK_IMAGE(image), item->icon_name);
}

static void preview_text_setup (GtkListItem* list_item, gpointer user_data)
{ (void)user_data;
	GtkWidget* label = gtk_label_new(NULL);
	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
	gtk_widget_set_hexpand(label, TRUE);
	gtk_list_item_set_child(list_item, label);
}

static void preview_text_bind (GtkListItem* list_item, gpointer user_data)
{
	int iCol = GPOINTER_TO_INT(user_data);
	RenamePreviewItem* item =
		RENAME_PREVIEW_ITEM(gtk_list_item_get_item(list_item));
	GtkWidget* label = gtk_list_item_get_child(list_item);
	const char* szText = NULL;
	switch (iCol)
	{
		case PREVIEW_COL_SRC:    szText = item->src_name;   break;
		case PREVIEW_COL_TYPE:   szText = item->type_desc;  break;
		case PREVIEW_COL_DST:    szText = item->dst_name;   break;
		default:                 szText = item->conflict;   break;
	}
	gboolean bConflicted =
		(NULL != item->conflict && '\0' != item->conflict[0]);
	if (bConflicted && PREVIEW_COL_CONFLICT == iCol)
	{
		gchar* esc = g_markup_escape_text(szText, -1);
		gchar* markup =
			g_strdup_printf("<span foreground=\"#e01b24\">%s</span>", esc);
		gtk_label_set_markup(GTK_LABEL(label), markup);
		g_free(markup);
		g_free(esc);
	}
	else
	{
		gtk_label_set_text(GTK_LABEL(label), szText);
	}
}

static GtkListItemFactory* preview_column_factory (int iCol)
{
	GtkListItemFactory* factory = gtk_signal_list_item_factory_new();
	if (PREVIEW_COL_ICON == iCol)
	{
		g_signal_connect(factory, "setup", G_CALLBACK(preview_icon_setup), NULL);
		g_signal_connect(factory, "bind", G_CALLBACK(preview_icon_bind), NULL);
	}
	else
	{
		g_signal_connect(factory, "setup", G_CALLBACK(preview_text_setup), NULL);
		g_signal_connect(factory, "bind", G_CALLBACK(preview_text_bind),
			GINT_TO_POINTER(iCol));
	}
	return factory;
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
	m_bRunDone = false;
	m_iRunResponse = GTK_RESPONSE_NONE;
	m_pGtkBuilder = gtk_builder_new();
	const char* objectids[] = {
		"RenameDialog",
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
		gtk_window_destroy(GTK_WINDOW(m_pDialogRename));
		m_pDialogRename = NULL;
	}

	if (NULL != m_pListStorePreview)
	{
		g_object_unref(m_pListStorePreview);
		m_pListStorePreview = NULL;
	}
}


void RenameDlg::RenameDlgPriv::LoadWidgets()
{
	m_pDialogRename         = GTK_WIDGET(gtk_builder_get_object (m_pGtkBuilder, (gchar*)"RenameDialog"));

	m_pBtnOK = gtk_button_new_with_mnemonic("_OK");
	if (m_pDialogRename)
	{
		GtkHeaderBar* hbar = GTK_HEADER_BAR(gtk_header_bar_new());
		gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(hbar), TRUE);
		gtk_header_bar_pack_end(hbar, GTK_WIDGET(m_pBtnOK));
		gtk_window_set_titlebar(GTK_WINDOW(m_pDialogRename), GTK_WIDGET(hbar));
	}


	m_pBtnChooseFolder = GTK_BUTTON(gtk_button_new_with_mnemonic("Choose Source Folder…"));
	gtk_widget_set_halign(GTK_WIDGET(m_pBtnChooseFolder), GTK_ALIGN_START);
	m_pEntryTemplate        = GTK_ENTRY( gtk_builder_get_object(m_pGtkBuilder, "rename_entry_template") );

	m_pLabelExample           = GTK_LABEL( gtk_builder_get_object(m_pGtkBuilder, "rename_label_example") );

	m_bLoadedDlg = (
		NULL != m_pDialogRename        &&
		NULL != m_pBtnChooseFolder     &&
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
			gtk_window_get_child(GTK_WINDOW(m_pDialogRename));

		gtk_box_append(GTK_BOX(content_area), GTK_WIDGET(m_pBtnChooseFolder));

		m_pLabelPurpose = gtk_label_new(NULL);
		gtk_label_set_markup(GTK_LABEL(m_pLabelPurpose),
			"Renames every supported photo or video in the selected folder "
			"using the pattern below. Use <b>#</b> characters in the template "
			"for sequence numbers.");
		gtk_label_set_use_markup(GTK_LABEL(m_pLabelPurpose), TRUE);
		gtk_label_set_wrap(GTK_LABEL(m_pLabelPurpose), TRUE);
		gtk_label_set_xalign(GTK_LABEL(m_pLabelPurpose), 0.0);
		gtk_box_append(GTK_BOX(content_area), m_pLabelPurpose);

		m_pScrolledPreview = gtk_scrolled_window_new();
		gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(m_pScrolledPreview),
			GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
		gtk_widget_set_vexpand(m_pScrolledPreview, TRUE);
		gtk_widget_set_size_request(m_pScrolledPreview, -1, 150);

		m_pListStorePreview = g_list_store_new(RENAME_PREVIEW_ITEM_TYPE);
		GtkSingleSelection* sel =
			gtk_single_selection_new(G_LIST_MODEL(m_pListStorePreview));
		m_pTreeViewPreview =
			gtk_column_view_new(GTK_SELECTION_MODEL(sel));
		g_object_unref(sel);

		{
			GtkColumnViewColumn* column =
				gtk_column_view_column_new("", preview_column_factory(PREVIEW_COL_ICON));
			gtk_column_view_append_column(GTK_COLUMN_VIEW(m_pTreeViewPreview), column);
		}
		static const char* szTitles[] = { NULL, "Original Name", "Type", "New Name", "Conflict" };
		for (int c = PREVIEW_COL_SRC ; c <= PREVIEW_COL_CONFLICT ; c++)
		{
			GtkColumnViewColumn* column =
				gtk_column_view_column_new(szTitles[c], preview_column_factory(c));
			gtk_column_view_column_set_expand(column, TRUE);
			gtk_column_view_append_column(GTK_COLUMN_VIEW(m_pTreeViewPreview), column);
		}

		gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(m_pScrolledPreview),
			m_pTreeViewPreview);
		gtk_box_append(GTK_BOX(content_area), m_pScrolledPreview);

		m_pLabelStatus = gtk_label_new(NULL);
		gtk_label_set_use_markup(GTK_LABEL(m_pLabelStatus), TRUE);
		gtk_label_set_wrap(GTK_LABEL(m_pLabelStatus), TRUE);
		gtk_label_set_xalign(GTK_LABEL(m_pLabelStatus), 0.0);
		gtk_box_append(GTK_BOX(content_area), m_pLabelStatus);

		m_pProgressBar = GTK_PROGRESS_BAR(gtk_progress_bar_new());
		gtk_box_append(GTK_BOX(content_area), GTK_WIDGET(m_pProgressBar));

		m_pLabelWarning = gtk_label_new(NULL);
		gtk_label_set_use_markup(GTK_LABEL(m_pLabelWarning), TRUE);
		gtk_label_set_wrap(GTK_LABEL(m_pLabelWarning), TRUE);
		gtk_label_set_xalign(GTK_LABEL(m_pLabelWarning), 0.0);
		gtk_widget_set_margin_top(m_pLabelWarning, 6);
		gtk_box_append(GTK_BOX(content_area), m_pLabelWarning);
	}
}

std::string RenameDlg::RenameDlgPriv::GetFolderURI() const
{
	return m_strFolderURI;
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
	return GetFolderURI() + "\n" + gtk_editable_get_text(GTK_EDITABLE(m_pEntryTemplate));
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

	gtk_widget_set_visible(m_pLabelWarning, FALSE);
	gtk_widget_set_visible(m_pLabelStatus, FALSE);

	std::string strFolder = GetFolderURI();
	std::string strTemplate = gtk_editable_get_text(GTK_EDITABLE(m_pEntryTemplate));

	if (strFolder.empty() || strTemplate.empty())
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
	gtk_widget_set_visible(GTK_WIDGET(m_pProgressBar), FALSE);

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
	g_list_store_remove_all(m_pListStorePreview);
	const size_t nRows = MIN(m_vectConflicts.size(), PREVIEW_ROW_CAP);
	for (size_t i = 0 ; i < nRows ; i++)
	{
		const FileConflictCheck::Result& r = m_vectConflicts[i];
		gchar* szIcon = preview_icon_pixbuf(r.strIconName)
			? g_strdup(r.strIconName.c_str()) : NULL;
		RenamePreviewItem* item = rename_preview_item_new(
			szIcon, r.strSrcName.c_str(), r.strTypeDescription.c_str(),
			r.strDstName.c_str(), r.strConflictWith.c_str());
		g_free(szIcon);
		g_list_store_append(m_pListStorePreview, item);
		g_object_unref(item);
	}
	if (m_vectConflicts.size() > PREVIEW_ROW_CAP)
	{
		char szMore[128];
		g_snprintf(szMore, sizeof(szMore), "… and %d more files",
			(int)(m_vectConflicts.size() - PREVIEW_ROW_CAP));
		RenamePreviewItem* item = rename_preview_item_new(
			NULL, szMore, NULL, NULL, NULL);
		g_list_store_append(m_pListStorePreview, item);
		g_object_unref(item);
	}

	if (0 == m_vectConflicts.size())
	{
		gtk_label_set_markup(GTK_LABEL(m_pLabelStatus),
			"<span foreground=\"#77767e\">No supported files found "
			"in this folder.</span>");
	}
	else if (m_bConflictFound)
	{
		gtk_label_set_markup(GTK_LABEL(m_pLabelStatus),
			(("<span foreground=\"#e01b24\"><b>")
			+ std::to_string(nConflicts)
			+ (1 == nConflicts ? " conflict" : " conflicts")
			+ " found.</b></span>").c_str());

		std::string strMsg = "<span foreground=\"#e01b24\"><b>Rename conflict:</b> "
			+ std::to_string(nConflicts)
			+ " file(s) would collide with existing or generated names."
			"</span>";
		gtk_label_set_markup(GTK_LABEL(m_pLabelWarning), strMsg.c_str());
	}
	else
	{
		gtk_widget_set_visible(m_pLabelWarning, FALSE);
		gtk_label_set_markup(GTK_LABEL(m_pLabelStatus),
			"<span foreground=\"#26a269\"><b>No conflicts.</b></span>");
	}

	gtk_widget_set_sensitive(m_pBtnOK, !m_bConflictFound);

	m_strConflictKey = GetConflictInputKey();
	m_bConflictKeyValid = true;
}

void RenameDlg::RenameDlgPriv::UpdateUI()
{
	if (m_bLoadedDlg)
	{
		std::string strTemplate = gtk_editable_get_text(GTK_EDITABLE(m_pEntryTemplate));
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
		g_signal_connect(m_pDialogRename, "close-request",
			G_CALLBACK(+[](GtkWidget* widget, gpointer user_data) -> gboolean {
				RenameDlg::RenameDlgPriv* priv = static_cast<RenameDlg::RenameDlgPriv*>(user_data);
				priv->m_iRunResponse = GTK_RESPONSE_CANCEL;
				priv->m_bRunDone = true;
				gtk_widget_set_visible(widget, FALSE);
				return FALSE;
			}), this);

		g_signal_connect(m_pBtnChooseFolder,
			"clicked",(GCallback)on_folder_change,this);

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

	std::string strSrcURI = GetFolderURI();


	if (!strSrcURI.empty() && strSrcURI != "NULL" && !strSrcURI.empty())
	{
		GFile* file_src = g_file_new_for_uri(strSrcURI.c_str());

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
					GetFolderURI(), gtk_editable_get_text(GTK_EDITABLE(m_pEntryTemplate)),
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
		GtkWidget* dialog = gtk_window_new();
		gtk_window_set_title(GTK_WINDOW(dialog), strTitle.c_str());
		gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
		gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(m_pDialogRename));
		gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);
		GtkWidget* dlgBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
		gtk_widget_set_margin_start(dlgBox, 12);
		gtk_widget_set_margin_end(dlgBox, 12);
		gtk_widget_set_margin_top(dlgBox, 12);
		gtk_widget_set_margin_bottom(dlgBox, 12);
		GtkWidget* dlgLabel = gtk_label_new(strMsg.c_str());
		gtk_label_set_wrap(GTK_LABEL(dlgLabel), TRUE);
		gtk_label_set_xalign(GTK_LABEL(dlgLabel), 0.0);
		gtk_box_append(GTK_BOX(dlgBox), dlgLabel);
		GtkWidget* btnBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
		gtk_widget_set_halign(btnBox, GTK_ALIGN_END);
		gtk_box_append(GTK_BOX(dlgBox), btnBox);
		gtk_window_set_child(GTK_WINDOW(dialog), dlgBox);
		GtkWidget* btnClose = gtk_button_new_with_label("Close");
		gtk_box_append(GTK_BOX(btnBox), btnClose);
		g_signal_connect_swapped(btnClose, "clicked",
			G_CALLBACK(gtk_window_destroy), dialog);
		gtk_window_present(GTK_WINDOW(dialog));
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
			priv->m_iRunResponse = GTK_RESPONSE_OK;
			priv->m_bRunDone = true;
			gtk_widget_set_visible(GTK_WIDGET(priv->m_pDialogRename), FALSE);
		}
	}
}

static void on_folder_selected (GObject* source, GAsyncResult* res, gpointer user_data)
{
	RenameDlg::RenameDlgPriv *priv = static_cast<RenameDlg::RenameDlgPriv*>(user_data);
	GFile* folder = gtk_file_dialog_select_folder_finish(GTK_FILE_DIALOG(source), res, NULL);
	if (NULL != folder)
	{
		gchar* uri = g_file_get_uri(folder);
		if (NULL != uri)
		{
			priv->SetFolderURI(uri);
			gtk_button_set_label(priv->m_pBtnChooseFolder, uri);
			g_free(uri);
		}
		priv->UpdateUI();
		g_object_unref(folder);
	}
}

void on_folder_change (GtkButton *button, gpointer user_data)
{
	RenameDlg::RenameDlgPriv *priv = static_cast<RenameDlg::RenameDlgPriv*>(user_data);

	if (GTK_BUTTON(priv->m_pBtnChooseFolder) == button)
	{
		GtkFileDialog* filedlg = gtk_file_dialog_new();
		gtk_file_dialog_set_title(filedlg, "Choose Source Folder");
		gtk_file_dialog_set_modal(filedlg, TRUE);
		gtk_file_dialog_select_folder(filedlg, GTK_WINDOW(priv->m_pDialogRename),
			NULL, on_folder_selected, priv);
	}
}


static void on_editable_changed (GtkEditable *editable, gpointer user_data)
{
	(void)editable;
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




