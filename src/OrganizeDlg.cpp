#include <config.h>
#include "OrganizeDlg.h"
#include "OrganizeTask.h"
#include "RenameTask.h"

#include "QuiverPrefs.h"
#include "Preferences.h"

#include "QuiverStockIcons.h"

extern GtkApplication *g_pApp;

#include <gio/gio.h>
#include <gtk/gtk.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

extern "C"
{
#include "strnatcmp.h"
}

// human-readable local path for a folder URI
static std::string organize_friendly_folder_path(const std::string& uri)
{
	if (uri.empty())
		return std::string();
	GFile* file = g_file_new_for_uri(uri.c_str());
	gchar* path = g_file_get_path(file);
	g_object_unref(file);
	if (NULL != path)
	{
		std::string strPath(path);
		g_free(path);
		return strPath;
	}
	return uri;
}

enum
{
	ORGANIZE_PREVIEW_COL_ICON = 0,
	ORGANIZE_PREVIEW_COL_SRC,
	ORGANIZE_PREVIEW_COL_PATH,
	ORGANIZE_PREVIEW_COL_DST,
	ORGANIZE_PREVIEW_COL_CONFLICT,
	ORGANIZE_PREVIEW_COL_COUNT
};

#define PREVIEW_ROW_CAP 200

static const char* organize_preview_icon_pixbuf(const std::string& strIconName)
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
	gchar*   rel_path;
	gchar*   dst_name;
	gchar*   conflict;
} OrganizePreviewItem;

typedef struct {
	GObjectClass parent_class;
} OrganizePreviewItemClass;

#define ORGANIZE_PREVIEW_ITEM_TYPE (organize_preview_item_get_type())
#define ORGANIZE_PREVIEW_ITEM(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST((obj), ORGANIZE_PREVIEW_ITEM_TYPE, OrganizePreviewItem))

G_DEFINE_TYPE(OrganizePreviewItem, organize_preview_item, G_TYPE_OBJECT)

static void organize_preview_item_finalize (GObject* object)
{
	OrganizePreviewItem* item = ORGANIZE_PREVIEW_ITEM(object);
	g_free(item->icon_name);
	g_free(item->src_name);
	g_free(item->rel_path);
	g_free(item->dst_name);
	g_free(item->conflict);
	G_OBJECT_CLASS(organize_preview_item_parent_class)->finalize(object);
}

static void organize_preview_item_class_init (OrganizePreviewItemClass* klass)
{
	G_OBJECT_CLASS(klass)->finalize = organize_preview_item_finalize;
}

static void organize_preview_item_init (OrganizePreviewItem* item)
{
	item->icon_name = NULL;
	item->src_name = NULL;
	item->rel_path = NULL;
	item->dst_name = NULL;
	item->conflict = NULL;
}

static OrganizePreviewItem* organize_preview_item_new (const gchar* icon,
	const gchar* src, const gchar* rel_path, const gchar* dst, const gchar* conflict)
{
	OrganizePreviewItem* item = static_cast<OrganizePreviewItem*>(
		g_object_new(ORGANIZE_PREVIEW_ITEM_TYPE, NULL));
	item->icon_name = g_strdup(icon);
	item->src_name = g_strdup(src);
	item->rel_path = g_strdup(rel_path);
	item->dst_name = g_strdup(dst);
	item->conflict = g_strdup(conflict);
	return item;
}

static void organize_preview_icon_setup (GtkSignalListItemFactory* factory, GtkListItem* list_item, gpointer user_data)
{ (void)factory; (void)user_data;
	GtkWidget* image = gtk_image_new();
	gtk_image_set_icon_size(GTK_IMAGE(image), GTK_ICON_SIZE_NORMAL);
	gtk_widget_set_margin_start(image, 6);
	gtk_widget_set_margin_end(image, 6);
	gtk_list_item_set_child(list_item, image);
}

static void organize_preview_icon_bind (GtkSignalListItemFactory* factory, GtkListItem* list_item, gpointer user_data)
{ (void)factory; (void)user_data;
	OrganizePreviewItem* item =
		ORGANIZE_PREVIEW_ITEM(gtk_list_item_get_item(list_item));
	GtkWidget* image = gtk_list_item_get_child(list_item);
	if (item && item->icon_name)
		gtk_image_set_from_icon_name(GTK_IMAGE(image), item->icon_name);
	else
		gtk_image_clear(GTK_IMAGE(image));
}

static void organize_preview_text_setup (GtkSignalListItemFactory* factory, GtkListItem* list_item, gpointer user_data)
{ (void)factory; (void)user_data;
	GtkWidget* label = gtk_label_new(NULL);
	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
	gtk_widget_set_hexpand(label, TRUE);
	gtk_list_item_set_child(list_item, label);
}

static void organize_preview_text_bind (GtkSignalListItemFactory* factory, GtkListItem* list_item, gpointer user_data)
{ (void)factory;
	int iCol = GPOINTER_TO_INT(user_data);
	OrganizePreviewItem* item =
		ORGANIZE_PREVIEW_ITEM(gtk_list_item_get_item(list_item));
	GtkWidget* label = gtk_list_item_get_child(list_item);
	if (!item)
	{
		gtk_label_set_text(GTK_LABEL(label), "");
		return;
	}
	const char* szText = NULL;
	switch (iCol)
	{
		case ORGANIZE_PREVIEW_COL_SRC:      szText = item->src_name; break;
		case ORGANIZE_PREVIEW_COL_PATH:     szText = item->rel_path; break;
		case ORGANIZE_PREVIEW_COL_DST:      szText = item->dst_name; break;
		default:                            szText = item->conflict; break;
	}
	gboolean bConflicted =
		(NULL != item->conflict && '\0' != item->conflict[0]);
	if (bConflicted && ORGANIZE_PREVIEW_COL_CONFLICT == iCol)
	{
		gchar* esc = g_markup_escape_text(szText ? szText : "", -1);
		gchar* markup =
			g_strdup_printf("<span foreground=\"#e01b24\">%s</span>", esc);
		gtk_label_set_markup(GTK_LABEL(label), markup);
		g_free(markup);
		g_free(esc);
	}
	else
	{
		gtk_label_set_text(GTK_LABEL(label), szText ? szText : "");
	}
}

static GtkListItemFactory* organize_preview_column_factory (int iCol)
{
	GtkListItemFactory* factory = gtk_signal_list_item_factory_new();
	if (ORGANIZE_PREVIEW_COL_ICON == iCol)
	{
		g_signal_connect(factory, "setup", G_CALLBACK(organize_preview_icon_setup), NULL);
		g_signal_connect(factory, "bind", G_CALLBACK(organize_preview_icon_bind), NULL);
	}
	else
	{
		g_signal_connect(factory, "setup", G_CALLBACK(organize_preview_text_setup), NULL);
		g_signal_connect(factory, "bind", G_CALLBACK(organize_preview_text_bind),
			GINT_TO_POINTER(iCol));
	}
	return factory;
}

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
		std::string                   strStatus;
		FileConflictCheck::ResultList vectResults;

		ConflictShared() : bDone(false), bFound(false), dProgress(0.), strStatus("Checking for conflicts…") {}
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

	std::string GetFolderTemplate() const;
	bool GetRenameFiles() const;

	bool m_bRunDone;
	int  m_iRunResponse;

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
	GtkWidget*              m_pLabelTemplateInfo;
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

	//GtkToggleButton*        m_pTglBtnCopy;
	//GtkToggleButton*        m_pTglBtnMove;
};


OrganizeDlg::OrganizeDlg()
	: m_PrivPtr(new OrganizeDlgPriv(this))
{
}


GtkWidget* OrganizeDlg::GetWidget() const
{
	  return NULL;
}


bool OrganizeDlg::Run()
{
	if (!m_PrivPtr->m_bLoadedDlg)
		return false;

	m_PrivPtr->m_bRunDone = false;
	m_PrivPtr->m_iRunResponse = GTK_RESPONSE_NONE;
	/* Keep the dialog above its parent window without grabbing input. */
	if (NULL != g_pApp)
	{
		GtkWindow *mainWin = gtk_application_get_active_window(g_pApp);
		if (mainWin != NULL)
			gtk_window_set_transient_for(GTK_WINDOW(m_PrivPtr->m_pDialogOrganize), mainWin);
	}
	gtk_widget_set_visible(m_PrivPtr->m_pDialogOrganize, TRUE);

	GMainContext* ctx = g_main_context_default();
	while (!m_PrivPtr->m_bRunDone)
	{
		g_main_context_iteration(ctx, TRUE);
	}
	return (GTK_RESPONSE_OK == m_PrivPtr->m_iRunResponse);
}

std::string OrganizeDlg::OrganizeDlgPriv::GetFolderTemplate() const
{
	if (NULL == m_pComboTemplateFolder)
		return std::string();
	GtkStringObject* pItem = GTK_STRING_OBJECT(
		gtk_drop_down_get_selected_item(m_pComboTemplateFolder));
	if (NULL == pItem)
		return std::string();
	return gtk_string_object_get_string(pItem);
}

std::string OrganizeDlg::GetFolderTemplate() const
{
	return m_PrivPtr->GetFolderTemplate();
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
	if (m_PrivPtr->m_bLoadedDlg)
	{
		m_PrivPtr->UpdateUI();
	}
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
static void on_clicked (GtkButton *button, gpointer   user_data);
static void on_folder_change (GtkButton *button, gpointer user_data);
static void on_editable_changed (GtkEditable *editable, gpointer user_data);
static void on_spin_changed (GtkSpinButton *spin_button, gpointer user_data);
static void combo_changed (GObject *widget, gpointer user_data);
static gboolean conflict_check_timeout_cb (gpointer user_data);
static gboolean conflict_poll_cb (gpointer user_data);


OrganizeDlg::OrganizeDlgPriv::OrganizeDlgPriv(OrganizeDlg *parent) :
        m_pOrganizeDlg(parent)
{
	m_pDialogOrganize = NULL;
	m_pLabelTemplateInfo = NULL;
	m_pLabelWarning = NULL;
	m_pLabelStatus = NULL;
	m_pProgressBar = NULL;
	m_pScrolledPreview = NULL;
	m_pTreeViewPreview = NULL;
	m_pListStorePreview = NULL;
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
	const gchar* objectids[] = {
		"OrganizeDialog",
		"adjustment8",
		NULL};
	gtk_builder_add_objects_from_file(m_pGtkBuilder, quiver_get_resource_path("quiver.ui").c_str(), objectids, NULL);

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

	if (NULL != m_pDialogOrganize)
	{
		gtk_window_destroy(GTK_WINDOW(m_pDialogOrganize));
		m_pDialogOrganize = NULL;
	}

	if (NULL != m_pGtkBuilder)
	{
		g_object_unref(m_pGtkBuilder);
		m_pGtkBuilder = NULL;
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

	m_pLabelTemplateInfo      = GTK_WIDGET( gtk_builder_get_object(m_pGtkBuilder, "label_filename_tempate_info") );

	GtkWidget* src_cont = GTK_WIDGET( gtk_builder_get_object(m_pGtkBuilder, "organize_align_source_folder") );
	GtkWidget* dst_cont = GTK_WIDGET( gtk_builder_get_object(m_pGtkBuilder, "organize_align_dest_folder") );
		m_pFCBtnSourceFolder = gtk_button_new_with_label("Choose Source Folder…");
		gtk_widget_set_halign(m_pFCBtnSourceFolder, GTK_ALIGN_FILL);
		gtk_widget_set_hexpand(m_pFCBtnSourceFolder, TRUE);
		m_pFCBtnDestFolder = gtk_button_new_with_label("Choose Destination Folder…");
		gtk_widget_set_halign(m_pFCBtnDestFolder, GTK_ALIGN_FILL);
		gtk_widget_set_hexpand(m_pFCBtnDestFolder, TRUE);
		{
			GtkWidget* lblSrc = gtk_button_get_child(GTK_BUTTON(m_pFCBtnSourceFolder));
			if (GTK_IS_LABEL(lblSrc))
			{
				gtk_label_set_xalign(GTK_LABEL(lblSrc), 0.0);
				gtk_label_set_ellipsize(GTK_LABEL(lblSrc), PANGO_ELLIPSIZE_START);
			}
			GtkWidget* lblDst = gtk_button_get_child(GTK_BUTTON(m_pFCBtnDestFolder));
			if (GTK_IS_LABEL(lblDst))
			{
				gtk_label_set_xalign(GTK_LABEL(lblDst), 0.0);
				gtk_label_set_ellipsize(GTK_LABEL(lblDst), PANGO_ELLIPSIZE_START);
			}
		}
		if (NULL != src_cont)
			gtk_box_append(GTK_BOX(src_cont), m_pFCBtnSourceFolder);
		if (NULL != dst_cont)
			gtk_box_append(GTK_BOX(dst_cont), m_pFCBtnDestFolder);
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

		gtk_window_set_default_size(GTK_WINDOW(m_pDialogOrganize), 780, 580);

		gtk_drop_down_set_selected(m_pComboTemplateFolder, 0);


		PreferencesPtr prefs = Preferences::GetInstance();
		std::string strPhotoLibrary = prefs->GetString(QUIVER_PREFS_APP,QUIVER_PREFS_APP_PHOTO_LIBRARY);
		if (!strPhotoLibrary.empty())
		{
			m_strDestFolder = strPhotoLibrary;
		}

		GtkWidget* content_area =
			gtk_window_get_child(GTK_WINDOW(m_pDialogOrganize));

		m_pScrolledPreview = gtk_scrolled_window_new();
		gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(m_pScrolledPreview),
			GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
		gtk_scrolled_window_set_has_frame(GTK_SCROLLED_WINDOW(m_pScrolledPreview), TRUE);
		gtk_widget_set_vexpand(m_pScrolledPreview, TRUE);
		gtk_widget_set_size_request(m_pScrolledPreview, -1, 150);

		m_pListStorePreview = g_list_store_new(ORGANIZE_PREVIEW_ITEM_TYPE);
		GtkSingleSelection* sel =
			gtk_single_selection_new(G_LIST_MODEL(m_pListStorePreview));
		m_pTreeViewPreview =
			gtk_column_view_new(GTK_SELECTION_MODEL(sel));

		{
			GtkColumnViewColumn* column =
				gtk_column_view_column_new("", organize_preview_column_factory(ORGANIZE_PREVIEW_COL_ICON));
			gtk_column_view_append_column(GTK_COLUMN_VIEW(m_pTreeViewPreview), column);
		}
		static const char* szTitles[] = { NULL, "Original Name", "New Path", "New Name", "Conflict" };
		for (int c = ORGANIZE_PREVIEW_COL_SRC ; c <= ORGANIZE_PREVIEW_COL_CONFLICT ; c++)
		{
			GtkColumnViewColumn* column =
				gtk_column_view_column_new(szTitles[c], organize_preview_column_factory(c));
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
		gtk_label_set_wrap_mode(GTK_LABEL(m_pLabelWarning), PANGO_WRAP_WORD_CHAR);
		gtk_widget_set_margin_top(m_pLabelWarning, 6);
		gtk_box_append(GTK_BOX(content_area), m_pLabelWarning);
	}
}

void OrganizeDlg::OrganizeDlgPriv::UpdateUI()
{
	if (m_bLoadedDlg)
	{
		if (NULL != m_pFCBtnSourceFolder)
		{
			std::string strSrc = organize_friendly_folder_path(m_strSrcFolder);
			gtk_button_set_label(GTK_BUTTON(m_pFCBtnSourceFolder),
				strSrc.empty() ? "Choose Source Folder…" : strSrc.c_str());
			gtk_widget_set_tooltip_text(m_pFCBtnSourceFolder,
				strSrc.empty() ? "Choose Source Folder…" : strSrc.c_str());
			GtkWidget* lbl = gtk_button_get_child(GTK_BUTTON(m_pFCBtnSourceFolder));
			if (GTK_IS_LABEL(lbl))
			{
				gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
				gtk_label_set_ellipsize(GTK_LABEL(lbl), PANGO_ELLIPSIZE_START);
			}
		}
		if (NULL != m_pFCBtnDestFolder)
		{
			std::string strDst = organize_friendly_folder_path(m_strDestFolder);
			gtk_button_set_label(GTK_BUTTON(m_pFCBtnDestFolder),
				strDst.empty() ? "Choose Destination Folder…" : strDst.c_str());
			gtk_widget_set_tooltip_text(m_pFCBtnDestFolder,
				strDst.empty() ? "Choose Destination Folder…" : strDst.c_str());
			GtkWidget* lbl = gtk_button_get_child(GTK_BUTTON(m_pFCBtnDestFolder));
			if (GTK_IS_LABEL(lbl))
			{
				gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
				gtk_label_set_ellipsize(GTK_LABEL(lbl), PANGO_ELLIPSIZE_START);
			}
		}

		std::string strDestDisplay = organize_friendly_folder_path(m_strDestFolder);
		std::string strLabel = strDestDisplay.empty() ? m_strDestFolder : strDestDisplay;
		GDateTime* time = g_date_time_new_now_local();

		strLabel += G_DIR_SEPARATOR_S;
		strLabel += GetFolderTemplate();
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
		gtk_widget_set_tooltip_text(GTK_WIDGET(m_pLabelExample), strLabel.c_str());

		gtk_widget_set_sensitive(GTK_WIDGET(m_pEntryTemplateFile), GetRenameFiles() ? TRUE : FALSE);
		if (NULL != m_pLabelTemplateInfo)
		{
			gtk_widget_set_sensitive(m_pLabelTemplateInfo, GetRenameFiles() ? TRUE : FALSE);
		}

		if (m_strSrcFolder.empty() || m_strDestFolder.empty())
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
			m_bConflictFound = false;
			if (NULL != m_pListStorePreview)
				g_list_store_remove_all(m_pListStorePreview);
			if (NULL != m_pLabelStatus)
			{
				gtk_label_set_markup(GTK_LABEL(m_pLabelStatus),
					"<span foreground=\"#77767e\">Please select both a source and destination folder.</span>");
				gtk_widget_set_visible(m_pLabelStatus, TRUE);
			}
			if (NULL != m_pProgressBar)
				gtk_widget_set_visible(GTK_WIDGET(m_pProgressBar), FALSE);
			if (NULL != m_pLabelWarning)
				gtk_widget_set_visible(m_pLabelWarning, FALSE);
			gtk_widget_set_sensitive(m_pBtnOK, FALSE);
		}
		else
		{
			// A change invalidates the previous dry-run: retire its completed
			// state now so the poll doesn't re-apply stale results before the
			// debounced re-check fires, letting the pending feedback stick.
			CancelConflictCheck();
			m_bConflictKeyValid = false;

			// Immediate visual feedback that conflict checking is pending / running
			if (NULL != m_pLabelStatus)
			{
				gtk_label_set_markup(GTK_LABEL(m_pLabelStatus),
					"<span foreground=\"#77767e\">Checking for conflicts…</span>");
				gtk_widget_set_visible(m_pLabelStatus, TRUE);
			}
			if (NULL != m_pProgressBar)
			{
				gtk_widget_set_visible(GTK_WIDGET(m_pProgressBar), TRUE);
				gtk_progress_bar_pulse(m_pProgressBar);
			}
			if (NULL != m_pLabelWarning)
			{
				gtk_widget_set_visible(m_pLabelWarning, FALSE);
			}
			gtk_widget_set_sensitive(m_pBtnOK, FALSE);

			if (0 == m_iConflictPollID)
			{
				m_iConflictPollID = g_timeout_add(80, conflict_poll_cb, this);
			}

			if (0 != m_iConflictCheckID)
			{
				g_source_remove(m_iConflictCheckID);
				m_iConflictCheckID = 0;
			}
			m_iConflictCheckID =
				g_timeout_add(300, conflict_check_timeout_cb, this);
		}
	}
}




void OrganizeDlg::OrganizeDlgPriv::ConnectSignals()
{
	if (m_bLoadedDlg)
	{
		g_signal_connect(m_pDialogOrganize, "close-request",
			G_CALLBACK(+[](GtkWidget* widget, gpointer user_data) -> gboolean {
				OrganizeDlg::OrganizeDlgPriv* priv = static_cast<OrganizeDlg::OrganizeDlgPriv*>(user_data);
				priv->m_iRunResponse = GTK_RESPONSE_CANCEL;
				priv->m_bRunDone = true;
				gtk_widget_set_visible(widget, FALSE);
				return TRUE;
			}), this);

		g_signal_connect(m_pFCBtnSourceFolder,
			"clicked",(GCallback)on_folder_change,this);
		g_signal_connect(m_pFCBtnDestFolder,
			"clicked",(GCallback)on_folder_change,this);

		g_signal_connect(m_pBtnOK,
			"clicked",(GCallback)on_clicked,this);

		g_signal_connect(m_pTglBtnRenameFiles,
			"toggled",(GCallback)on_editable_changed,this);

		g_signal_connect(m_pTglBtnSubfolders,
			"toggled",(GCallback)on_editable_changed,this);

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

		g_signal_connect(m_pSpinExtension,
			"value-changed",(GCallback)on_spin_changed,this);

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
				"Please resolve the conflicts shown in the table.",
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
	if (NULL != pUserData)
	{
		ConflictShared* pState = static_cast<ConflictShared*>(pUserData);
		std::lock_guard<std::mutex> lock(pState->mutex);
		pState->strStatus = "Scanning source folder…";
	}

	std::vector<FileConflictCheck::Mapping> vectMappings;
	if (!OrganizeTask::ComputeMappings(opts, vectMappings))
	{
		return false;
	}

	if (NULL != pUserData &&
		(NULL == pCancellable || !g_cancellable_is_cancelled(pCancellable)))
	{
		ConflictShared* pState = static_cast<ConflictShared*>(pUserData);
		std::lock_guard<std::mutex> lock(pState->mutex);
		pState->strStatus = "Checking for conflicts…";
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

	m_bConflictKeyValid = false;

	if (NULL != m_pLabelWarning)
		gtk_widget_set_visible(m_pLabelWarning, FALSE);

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
		if (NULL != m_pListStorePreview)
			g_list_store_remove_all(m_pListStorePreview);
		if (NULL != m_pLabelStatus)
		{
			gtk_label_set_markup(GTK_LABEL(m_pLabelStatus),
				"<span foreground=\"#77767e\">Please select both a source and destination folder.</span>");
			gtk_widget_set_visible(m_pLabelStatus, TRUE);
		}
		if (NULL != m_pProgressBar)
			gtk_widget_set_visible(GTK_WIDGET(m_pProgressBar), FALSE);
		gtk_widget_set_sensitive(m_pBtnOK, FALSE);
		return;
	}

	gtk_widget_set_sensitive(m_pBtnOK, FALSE);
	if (NULL != m_pProgressBar)
	{
		gtk_widget_set_visible(GTK_WIDGET(m_pProgressBar), TRUE);
		gtk_progress_bar_pulse(m_pProgressBar);
	}
	if (NULL != m_pLabelStatus)
	{
		gtk_label_set_markup(GTK_LABEL(m_pLabelStatus),
			"<span foreground=\"#77767e\">Checking for conflicts…</span>");
		gtk_widget_set_visible(m_pLabelStatus, TRUE);
	}

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

	if (0 == m_iConflictPollID)
	{
		m_iConflictPollID = g_timeout_add(80, conflict_poll_cb, this);
	}
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
	if (NULL != m_pProgressBar)
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
	if (NULL != m_pListStorePreview)
	{
		g_list_store_remove_all(m_pListStorePreview);
		const size_t nRows = MIN(m_vectConflicts.size(), PREVIEW_ROW_CAP);
		for (size_t i = 0 ; i < nRows ; i++)
		{
			const FileConflictCheck::Result& r = m_vectConflicts[i];
			gchar* szIcon = organize_preview_icon_pixbuf(r.strIconName)
				? g_strdup(r.strIconName.c_str()) : NULL;
			OrganizePreviewItem* item = organize_preview_item_new(
				szIcon, r.strSrcName.c_str(), r.strDstRelPath.c_str(),
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
			OrganizePreviewItem* item = organize_preview_item_new(
				NULL, szMore, NULL, NULL, NULL);
			g_list_store_append(m_pListStorePreview, item);
			g_object_unref(item);
		}
	}

	if (0 == m_vectConflicts.size())
	{
		if (NULL != m_pLabelStatus)
		{
			gtk_label_set_markup(GTK_LABEL(m_pLabelStatus),
				"<span foreground=\"#77767e\">No supported files found "
				"in source folder.</span>");
		}
		if (NULL != m_pLabelWarning)
			gtk_widget_set_visible(m_pLabelWarning, FALSE);
	}
	else if (m_bConflictFound)
	{
		if (NULL != m_pLabelStatus)
		{
			gtk_label_set_markup(GTK_LABEL(m_pLabelStatus),
				(("<span foreground=\"#e01b24\"><b>")
				+ std::to_string(nConflicts)
				+ (1 == nConflicts ? " conflict" : " conflicts")
				+ " found.</b></span>").c_str());
		}

		std::string strMsg = "<span foreground=\"#e01b24\"><b>Organize conflict:</b> "
			+ std::to_string(nConflicts)
			+ " file(s) would collide with existing or generated names."
			"</span>";
		if (NULL != m_pLabelWarning)
		{
			gtk_label_set_markup(GTK_LABEL(m_pLabelWarning), strMsg.c_str());
			gtk_widget_set_visible(m_pLabelWarning, TRUE);
		}
	}
	else
	{
		if (NULL != m_pLabelWarning)
			gtk_widget_set_visible(m_pLabelWarning, FALSE);
		if (NULL != m_pLabelStatus)
		{
			std::string strCount = std::to_string(m_vectConflicts.size());
			gtk_label_set_markup(GTK_LABEL(m_pLabelStatus),
				(("<span foreground=\"#2ec27e\">Ready: <b>")
				+ strCount
				+ (1 == m_vectConflicts.size() ? " file" : " files")
				+ "</b> to organize.</span>").c_str());
		}
	}

	gtk_widget_set_sensitive(m_pBtnOK, !m_bConflictFound && !m_vectConflicts.empty());

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
		*(ctx->second) = uri ? uri : "";
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

static void on_clicked (GtkButton *button, gpointer user_data)
{
	OrganizeDlg::OrganizeDlgPriv *priv = static_cast<OrganizeDlg::OrganizeDlgPriv*>(user_data);
	if (GTK_BUTTON(priv->m_pBtnOK) == button)
	{
		if (priv->ValidateInput())
		{
			priv->m_iRunResponse = GTK_RESPONSE_OK;
			priv->m_bRunDone = true;
			gtk_widget_set_visible(priv->m_pDialogOrganize, FALSE);
		}
	}
}

static void on_spin_changed (GtkSpinButton *spin_button, gpointer user_data)
{
	(void)spin_button;
	OrganizeDlg::OrganizeDlgPriv *priv = static_cast<OrganizeDlg::OrganizeDlgPriv*>(user_data);
	priv->UpdateUI();
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
	std::string strStatus;
	{
		std::lock_guard<std::mutex> lock(pState->mutex);
		dProgress = pState->dProgress;
		bDone = pState->bDone;
		strStatus = pState->strStatus;
	}

	if (NULL != priv->m_pProgressBar)
	{
		if (dProgress > 0.)
		{
			gtk_progress_bar_set_fraction(priv->m_pProgressBar,
				CLAMP(dProgress, 0., 1.));
		}
		else
		{
			gtk_progress_bar_pulse(priv->m_pProgressBar);
		}
	}

	if (!strStatus.empty() && NULL != priv->m_pLabelStatus)
	{
		std::string strMarkup =
			"<span foreground=\"#77767e\">" + strStatus + "</span>";
		gtk_label_set_markup(GTK_LABEL(priv->m_pLabelStatus),
			strMarkup.c_str());
		gtk_widget_set_visible(priv->m_pLabelStatus, TRUE);
	}

	if (!bDone)
		return TRUE;

	priv->m_iConflictPollID = 0;
	priv->ApplyConflictResults(*pState);
	return FALSE;
}




