#include <config.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <stdint.h>

#include <list>
#include <string>
#include <algorithm>
#include <set>
#include <vector>

#include "FolderTree.h"
#include "quiver-i18n.h"
#include "QuiverStockIcons.h"
#include "QuiverUtils.h"
#include "QuiverFileOps.h"
#include "QuiverClipboard.h"
#include "Bookmarks.h"
#include "BookmarkAddEditDlg.h"
#include "IBookmarksEventHandler.h"
#include "QuiverPrefs.h"
#include "Preferences.h"

#define QUIVER_TREE_COLUMN_TOGGLE      "column_toggle"
#define QUIVER_FOLDER_TREE_ROOT_NAME   "Filesystem"

/* Which "paste" entry the context menu shows for a right-click, if any:
 *   PASTE_NONE         -- row is not a directory (e.g. a file bookmark) or
 *                         the menu was summoned with no usable target
 *   PASTE_BARE         -- click in the list's empty area: plain "Paste" into
 *                         the focused folder
 *   PASTE_INTO_FOLDER  -- click on a directory row: "Paste Into Folder"
 */
enum class TreePasteMode {
	PASTE_NONE = 0,
	PASTE_BARE,
	PASTE_INTO_FOLDER,
};

/* True when the uri points at a real directory (file transfer targets only
 * make sense for such items).  Non-file schemes (trash, GVFS objects ...) are
 * never treated as directories here. */
static bool folder_tree_uri_is_directory(const char *uri)
{
	return QuiverUtils::IsDirectoryURI(uri);
}

enum DirItemRowKind {
	ROW_KIND_FOLDER = 0,
	ROW_KIND_SHORTCUT,
	ROW_KIND_BOOKMARK,
	ROW_KIND_SECTION_SHORTCUTS,
	ROW_KIND_SECTION_BOOKMARKS,
	ROW_KIND_SECTION_FOLDERS,
};

// row item type for the folder tree column view
typedef struct {
	GObject  parent_instance;
	gchar*   uri;
	gchar*   display_name;
	gchar*   icon_name;
	gboolean checked;
	gboolean permanent;
	gboolean separator;
	gint     node_order;
	guint    node_depth;
	gint     bookmark_id;
	gint     row_kind;
} DirItem;

typedef struct {
	GObjectClass parent_class;
} DirItemClass;

#define DIR_ITEM_TYPE (dir_item_get_type())
#define DIR_ITEM(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST((obj), DIR_ITEM_TYPE, DirItem))

G_DEFINE_TYPE(DirItem, dir_item, G_TYPE_OBJECT)

static void dir_item_finalize (GObject* object)
{
	DirItem* item = DIR_ITEM(object);
	g_free(item->uri);
	g_free(item->display_name);
	g_free(item->icon_name);
	G_OBJECT_CLASS(g_type_class_peek_parent(
		G_OBJECT_GET_CLASS(object)))->finalize(object);
}

static void dir_item_set_checked (DirItem* item, gboolean value);

static void dir_item_get_property (GObject* object, guint prop_id, GValue* value, GParamSpec* pspec)
{
	DirItem* item = DIR_ITEM(object);
	switch (prop_id)
	{
		case 1:
			g_value_set_boolean(value, item->checked);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}

static void dir_item_set_property (GObject* object, guint prop_id, const GValue* value, GParamSpec* pspec)
{
	DirItem* item = DIR_ITEM(object);
	switch (prop_id)
	{
		case 1:
			dir_item_set_checked(item, g_value_get_boolean(value));
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}

static void dir_item_class_init (DirItemClass* klass)
{
	GObjectClass* gobject_class = G_OBJECT_CLASS(klass);
	gobject_class->finalize = dir_item_finalize;
	gobject_class->get_property = dir_item_get_property;
	gobject_class->set_property = dir_item_set_property;

	/* Make "checked" a real GObject property so the list factory can be told
	 * when a row's checkbox changes and update its widget. */
	g_object_class_install_property(gobject_class, 1,
		g_param_spec_boolean("checked", "checked", "checked", FALSE,
			(GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY)));
}

static void dir_item_set_checked (DirItem* item, gboolean value)
{
	if (NULL == item)
		return;
	if (item->checked != value)
	{
		item->checked = value;
		g_object_notify(G_OBJECT(item), "checked");
	}
}

static void dir_item_init (DirItem* item)
{
	item->uri = NULL;
	item->display_name = NULL;
	item->icon_name = NULL;
	item->checked = FALSE;
	item->permanent = FALSE;
	item->separator = FALSE;
	item->node_order = 0;
	item->node_depth = 0;
	item->bookmark_id = -1;
	item->row_kind = ROW_KIND_FOLDER;
}

static DirItem* dir_item_new (const gchar* uri, const gchar* display_name,
	const gchar* icon_name, gboolean permanent, gint node_order, guint node_depth)
{
	DirItem* item = static_cast<DirItem*>(g_object_new(DIR_ITEM_TYPE, NULL));
	item->uri = g_strdup(uri);
	item->display_name = g_strdup(display_name);
	item->icon_name = g_strdup(icon_name);
	item->checked = FALSE;
	item->permanent = permanent;
	item->node_order = node_order;
	item->node_depth = node_depth;
	item->bookmark_id = -1;
	item->row_kind = ROW_KIND_FOLDER;
	return item;
}

/* Canonicalize a filesystem path or a file URI into a file:// URI so the
 * folder tree can match it against its root/child items, whose uri fields
 * are always file:// URIs. Callers pass both URIs (bookmarks, shortcuts)
 * and plain paths (command line folder arguments), and matching happens
 * only if both sides use the same representation. */
static std::string folder_tree_normalize_uri(const gchar* arg)
{
	if (NULL == arg || '\0' == arg[0])
		return std::string();
	GFile* file = g_file_new_for_commandline_arg(arg);
	char* uri = g_file_get_uri(file);
	std::string str(uri ? uri : arg);
	g_free(uri);
	g_object_unref(file);
	return str;
}

// prototype
static gboolean view_capture_button_press (GtkEventController *controller, GdkEvent *event, gpointer userdata);
static gboolean view_on_key_press (GtkEventControllerKey *controller, guint keyval, guint keycode, GdkModifierType state, gpointer userdata);
static void view_popup_menu_at (GtkWidget *treeview, gdouble x, gdouble y,
	TreePasteMode paste_mode, const gchar* target_uri, gpointer userdata, int bookmark_id = -1);
static void folder_tree_action_paste_into(GSimpleAction* action, GVariant* parameter, gpointer userdata);
static void folder_tree_action_new_folder(GSimpleAction* action, GVariant* parameter, gpointer userdata);
static void folder_tree_action_add_bookmark(GSimpleAction* action, GVariant* parameter, gpointer userdata);
static void folder_tree_action_edit_bookmark(GSimpleAction* action, GVariant* parameter, gpointer userdata);
static gboolean folder_tree_drop_motion_cb(GtkDropTarget *target, gdouble x, gdouble y, gpointer userdata);
static void folder_tree_drop_value_notify_cb(GObject *object, GParamSpec *pspec, gpointer userdata);
static gboolean folder_tree_update_status(FolderTree::FolderTreeImpl* impl, GtkDropTarget* target);
static void folder_tree_drop_leave_cb(GtkDropTarget *target, gpointer userdata);
static gboolean folder_tree_drop_cb(GtkDropTarget *target, const GValue *value, gdouble x, gdouble y, gpointer userdata);
static gboolean shortcuts_drop_motion_cb(GtkDropTarget *target, gdouble x, gdouble y, gpointer userdata);
static void shortcuts_drop_value_notify_cb(GObject *object, GParamSpec *pspec, gpointer userdata);
static gboolean shortcuts_update_status(FolderTree::FolderTreeImpl* impl, GtkDropTarget* target);
static void shortcuts_drop_leave_cb(GtkDropTarget *target, gpointer userdata);
static gboolean shortcuts_drop_cb(GtkDropTarget *target, const GValue *value, gdouble x, gdouble y, gpointer userdata);
static void folder_tree_cancel_drop_expand(FolderTree::FolderTreeImpl *impl);
static void folder_tree_arm_drop_expand(FolderTree::FolderTreeImpl *impl, GtkTreeListRow *row);
static guint folder_tree_get_focused_position(FolderTree::FolderTreeImpl* impl);
static guint shortcuts_get_focused_position(FolderTree::FolderTreeImpl* impl);
static void folder_tree_set_checkbox_for_selected(FolderTree::FolderTreeImpl* impl, gboolean value);
static void shortcut_row_on_clicked(GtkGestureClick* gesture, int n_press, double x, double y);
static void bookmark_row_on_clicked(GtkGestureClick* gesture, int n_press, double x, double y);
static gboolean shortcuts_on_key_press(GtkEventControllerKey *controller, guint keyval, guint keycode, GdkModifierType state, gpointer userdata);

static gchar* folder_tree_get_icon_name(GFile* gfile);
static gchar* folder_tree_get_icon_name_from_info(GFileInfo* info, GFile* gfile);

class FolderTree::FolderTreeImpl
{
public:
// constructor / destructor
	FolderTreeImpl(FolderTree *pFolderTree);
	~FolderTreeImpl();


// methods
	void CreateWidget();
	void PopulateShortcutsModel(GListStore *store);
	void PopulateBookmarksModel(GListStore *store);
	void UpdateBookmarkSectionVisibility();
	void UpdateSectionsModel();
	guint GetSlotFlattenOffset(guint slot_idx) const;
	guint GetTreeFlattenOffset() const;
	void ReloadShortcuts();
	void PopulateTreeModel(GListStore *roots);
	std::list<std::string> GetSelectedFolders() const;
	bool GetSelectedFoldersRecursive() const;
	std::set<std::string> GetSelectedRecursiveFolders() const;

	void SetSelectedFolders(const std::list<std::string> &uris, bool bRecursive = false, const std::set<std::string>& recursiveFolders = std::set<std::string>());
	std::list<std::string> GetSelectedURIsFromModels() const;
	std::list<std::string> GetAddBookmarkURIs(const std::string& clicked_uri) const;
	void ClearAllCheckboxes();
	void SetCheckboxForItem(DirItem* item, gboolean value);
	guint FindItemPosition(DirItem* target);
	guint FindItemByURI(const gchar* uri, gboolean check_only);
	DirItem* FindRootForPath(const gchar* uri);
	void ExpandItem(DirItem* item);

	void SyncTreeSelectionForURI(const gchar* uri, gboolean value);
	void SyncShortcutSelectionForURI(const gchar* uri, gboolean value);
	void AddChildFolder(const char *parent_uri, const char *child_uri, const char *folder_name);
	void RemoveFolder(const char *folder_uri);
	void ScrollToTreePosition(guint pos, GtkListScrollFlags flags);

// member variables
	GtkWidget*       m_pWidget;
	GtkWidget*       m_pMainScrolledWindow;
	GtkWidget*       m_pMenuPopover;
	FolderTree*      m_pFolderTree;

	// Unified flatten model & section header stores
	GListStore*          m_pSectionsStore;
	GtkFlattenListModel* m_pFlattenModel;
	GListStore*          m_pEmptyStore;
	GListStore*          m_pShortcutsHeaderStore;
	GListStore*          m_pBookmarksHeaderStore;
	GListStore*          m_pFoldersHeaderStore;
	DirItem*             m_pShortcutsHeaderItem;
	DirItem*             m_pBookmarksHeaderItem;
	DirItem*             m_pFoldersHeaderItem;
	bool                 m_bShortcutsExpanded;
	bool                 m_bBookmarksExpanded;
	bool                 m_bFoldersExpanded;

	// Shortcuts
	GListStore*        m_pShortcutsStore;
	GtkMultiSelection* m_pShortcutsSelectionModel;
	GtkListView*       m_pShortcutsListView;

	// Bookmarks (dedicated section between the shortcuts list and the tree)
	GListStore*        m_pBookmarkStore;
	GtkMultiSelection* m_pBookmarkSelectionModel;
	GtkListView*       m_pBookmarkListView;

	// Folder Tree & Unified ListView
	GListStore*        m_pListStoreRoots;
	GtkTreeListModel*  m_pTreeListModel;
	GtkMultiSelection* m_pSelectionModel;
	GtkListView*       m_pListView;
	GtkWidget*         m_pDropHover;
	DirItem*           m_pDropHoverItem;
	gdouble            m_dDropX;
	gdouble            m_dDropY;

	/* What the current drag carries, so a row can be judged against it while
	 * the pointer is still moving; see QuiverClipboard::DropContents.
	 * m_bDropFinished is TRUE once the user has released: the drop is then
	 * past the point where a status may be set, so a payload that lands
	 * afterwards must not touch it. */
	QuiverClipboard::DropContents m_DropContents;
	bool              m_bDropFinished;
	guint              m_DropExpandTimer;
	GtkTreeListRow*    m_DropExpandRow;
	guint              m_iFocusedTreePos;

	gchar*             m_pScrollToURI;
	IBookmarksEventHandlerPtr m_pBookmarksEventHandler;
};

// --- Lightweight widget cache struct attached to each GtkListItem to avoid dozens of g_object_get_data lookups per bind ---
struct FolderTreeRowWidgets {
	FolderTree::FolderTreeImpl* impl = nullptr;
	GtkWidget* outer_box = nullptr;
	GtkWidget* sep = nullptr;
	GtkWidget* header_box = nullptr;
	GtkWidget* header_arrow = nullptr;
	GtkWidget* header_label = nullptr;
	GtkWidget* check = nullptr;
	GtkWidget* hbox = nullptr;
	GtkWidget* row_box = nullptr;
	GtkWidget* image = nullptr;
	GtkWidget* label = nullptr;
	GtkWidget* expander = nullptr;
	GtkListItem* list_item = nullptr;

	DirItem* bound_item = nullptr;
	GtkTreeListRow* bound_row = nullptr;
	guint bound_sc_pos = G_MAXUINT;
	guint bound_bm_pos = G_MAXUINT;
	std::string bound_icon_name;
	std::string bound_text;
	bool is_sidebar_row = false;
	bool is_header_row = false;
};

class FolderTreeBookmarksEventHandler : public IBookmarksEventHandler
{
public:
	FolderTreeBookmarksEventHandler(FolderTree::FolderTreeImpl* parent) : m_pParent(parent) {}
	virtual void HandleBookmarkChanged(BookmarksEventPtr event)
	{
		(void)event;
		if (m_pParent)
			m_pParent->ReloadShortcuts();
	}
	virtual ~FolderTreeBookmarksEventHandler() {}
private:
	FolderTree::FolderTreeImpl* m_pParent;
};


FolderTree::FolderTree() : m_FolderTreeImplPtr(new FolderTreeImpl(this))
{
	
}

FolderTree::~FolderTree()
{
}

GtkWidget* FolderTree::GetWidget() const
{
	return m_FolderTreeImplPtr->m_pWidget;
}

GtkWidget* FolderTree::GetTreeWidget() const
{
	return GTK_WIDGET(m_FolderTreeImplPtr->m_pListView);
}

GtkWidget* FolderTree::GetShortcutsWidget() const
{
	return GTK_WIDGET(m_FolderTreeImplPtr->m_pShortcutsListView);
}

GtkWidget* FolderTree::GetBookmarksWidget() const
{
	return GTK_WIDGET(m_FolderTreeImplPtr->m_pBookmarkListView);
}

void FolderTree::SetShortcutsExpanded(bool expanded)
{
	if (!m_FolderTreeImplPtr)
		return;
	if (m_FolderTreeImplPtr->m_bShortcutsExpanded != expanded)
	{
		m_FolderTreeImplPtr->m_bShortcutsExpanded = expanded;
		PreferencesPtr p = Preferences::GetInstance();
		if (p)
			p->SetBoolean(QUIVER_PREFS_BROWSER, "foldertree_expand_shortcuts", expanded);
		if (m_FolderTreeImplPtr->m_pShortcutsHeaderItem)
		{
			GtkWidget* arrow = GTK_WIDGET(g_object_get_data(G_OBJECT(m_FolderTreeImplPtr->m_pShortcutsHeaderItem), "bound-arrow"));
			if (arrow && GTK_IS_IMAGE(arrow))
				gtk_image_set_from_icon_name(GTK_IMAGE(arrow), expanded ? "pan-down-symbolic" : "pan-end-symbolic");
		}
		m_FolderTreeImplPtr->UpdateSectionsModel();
	}
}

bool FolderTree::GetShortcutsExpanded() const
{
	if (m_FolderTreeImplPtr)
		return m_FolderTreeImplPtr->m_bShortcutsExpanded;
	return true;
}

void FolderTree::SetBookmarksExpanded(bool expanded)
{
	if (!m_FolderTreeImplPtr)
		return;
	if (m_FolderTreeImplPtr->m_bBookmarksExpanded != expanded)
	{
		m_FolderTreeImplPtr->m_bBookmarksExpanded = expanded;
		PreferencesPtr p = Preferences::GetInstance();
		if (p)
			p->SetBoolean(QUIVER_PREFS_BROWSER, "foldertree_expand_bookmarks", expanded);
		if (m_FolderTreeImplPtr->m_pBookmarksHeaderItem)
		{
			GtkWidget* arrow = GTK_WIDGET(g_object_get_data(G_OBJECT(m_FolderTreeImplPtr->m_pBookmarksHeaderItem), "bound-arrow"));
			if (arrow && GTK_IS_IMAGE(arrow))
				gtk_image_set_from_icon_name(GTK_IMAGE(arrow), expanded ? "pan-down-symbolic" : "pan-end-symbolic");
		}
		m_FolderTreeImplPtr->UpdateSectionsModel();
	}
}

bool FolderTree::GetBookmarksExpanded() const
{
	if (m_FolderTreeImplPtr)
		return m_FolderTreeImplPtr->m_bBookmarksExpanded;
	return true;
}

void FolderTree::SetFoldersExpanded(bool expanded)
{
	if (!m_FolderTreeImplPtr)
		return;
	if (m_FolderTreeImplPtr->m_bFoldersExpanded != expanded)
	{
		m_FolderTreeImplPtr->m_bFoldersExpanded = expanded;
		PreferencesPtr p = Preferences::GetInstance();
		if (p)
			p->SetBoolean(QUIVER_PREFS_BROWSER, "foldertree_expand_folders", expanded);
		if (m_FolderTreeImplPtr->m_pFoldersHeaderItem)
		{
			GtkWidget* arrow = GTK_WIDGET(g_object_get_data(G_OBJECT(m_FolderTreeImplPtr->m_pFoldersHeaderItem), "bound-arrow"));
			if (arrow && GTK_IS_IMAGE(arrow))
				gtk_image_set_from_icon_name(GTK_IMAGE(arrow), expanded ? "pan-down-symbolic" : "pan-end-symbolic");
		}
		m_FolderTreeImplPtr->UpdateSectionsModel();
	}
}

bool FolderTree::GetFoldersExpanded() const
{
	if (m_FolderTreeImplPtr)
		return m_FolderTreeImplPtr->m_bFoldersExpanded;
	return true;
}

std::list<std::string> FolderTree::GetSelectedFolders() const
{
	return m_FolderTreeImplPtr->GetSelectedFolders();	
}

bool FolderTree::GetSelectedFoldersRecursive() const
{
	return m_FolderTreeImplPtr->GetSelectedFoldersRecursive();
}

std::set<std::string> FolderTree::GetSelectedRecursiveFolders() const
{
	return m_FolderTreeImplPtr->GetSelectedRecursiveFolders();
}

std::list<std::string> FolderTree::GetAddBookmarkURIs(const std::string& clicked_uri) const
{
	if (m_FolderTreeImplPtr)
	{
		return m_FolderTreeImplPtr->GetAddBookmarkURIs(clicked_uri);
	}
	return std::list<std::string>();
}

void FolderTree::ShowContextMenuForTest(const char* target_uri, int bookmark_id)
{
	if (m_FolderTreeImplPtr && m_FolderTreeImplPtr->m_pWidget)
	{
		TreePasteMode mode = (target_uri && folder_tree_uri_is_directory(target_uri))
			? TreePasteMode::PASTE_INTO_FOLDER : TreePasteMode::PASTE_NONE;
		view_popup_menu_at(m_FolderTreeImplPtr->m_pWidget, 0, 0, mode, target_uri, m_FolderTreeImplPtr.get(), bookmark_id);
	}
}

void FolderTree::SetSelectedFolders(const std::list<std::string> &uris, bool bRecursive, const std::set<std::string>& recursiveFolders)
{
	m_FolderTreeImplPtr->SetSelectedFolders(uris, bRecursive, recursiveFolders);
}

void FolderTree::AddChildFolder(const char *parent_uri, const char *child_uri, const char *folder_name)
{
	if (m_FolderTreeImplPtr)
		m_FolderTreeImplPtr->AddChildFolder(parent_uri, child_uri, folder_name);
}

void FolderTree::RemoveFolder(const char *folder_uri)
{
	if (m_FolderTreeImplPtr)
		m_FolderTreeImplPtr->RemoveFolder(folder_uri);
}



FolderTree::FolderTreeImpl::FolderTreeImpl(FolderTree *parent)
{
	m_pFolderTree = parent;
	
	m_pWidget = NULL;
	m_pMainScrolledWindow = NULL;
	m_pMenuPopover = NULL;
	m_pScrollToURI = NULL;
	m_pSectionsStore = NULL;
	m_pFlattenModel = NULL;
	m_pEmptyStore = NULL;
	m_pShortcutsHeaderStore = NULL;
	m_pBookmarksHeaderStore = NULL;
	m_pFoldersHeaderStore = NULL;
	m_pShortcutsHeaderItem = NULL;
	m_pBookmarksHeaderItem = NULL;
	m_pFoldersHeaderItem = NULL;
	m_bShortcutsExpanded = true;
	m_bBookmarksExpanded = true;
	m_bFoldersExpanded = true;
	m_pShortcutsStore = NULL;
	m_pShortcutsSelectionModel = NULL;
	m_pShortcutsListView = NULL;
	m_pBookmarkStore = NULL;
	m_pBookmarkSelectionModel = NULL;
	m_pBookmarkListView = NULL;
	m_pListStoreRoots = NULL;
	m_pTreeListModel = NULL;
	m_pSelectionModel = NULL;
	m_pListView = NULL;
	m_pDropHover = NULL;
	m_pDropHoverItem = NULL;
	m_dDropX = 0;
	m_dDropY = 0;
	m_DropExpandTimer = 0;
	m_DropExpandRow = NULL;
	m_iFocusedTreePos = G_MAXUINT;

	CreateWidget();

	try {
		BookmarksPtr bmPtr = Bookmarks::GetInstance();
		if (bmPtr)
		{
			m_pBookmarksEventHandler.reset(new FolderTreeBookmarksEventHandler(this));
			bmPtr->AddEventHandler(m_pBookmarksEventHandler);
		}
	} catch (...) {}
}

FolderTree::FolderTreeImpl::~FolderTreeImpl()
{
	/* the global action group outlives this object: actions still pointing
	 * at it would call back into freed memory */
	QuiverUtils::RemoveActionsFor(this);
	try {
		BookmarksPtr bmPtr = Bookmarks::GetInstance();
		if (bmPtr && m_pBookmarksEventHandler)
		{
			bmPtr->RemoveEventHandler(m_pBookmarksEventHandler);
		}
	} catch (...) {}
	m_pBookmarksEventHandler.reset();

	/* Cancel any pending spring-load expand from a drag. */
	if (0 != m_DropExpandTimer)
	{
		g_source_remove(m_DropExpandTimer);
		m_DropExpandTimer = 0;
	}
	if (NULL != m_DropExpandRow)
	{
		g_object_unref(m_DropExpandRow);
		m_DropExpandRow = NULL;
	}

	if (NULL != m_pScrollToURI)
	{
		g_free(m_pScrollToURI);
		m_pScrollToURI = NULL;
	}

	if (NULL != m_pWidget && G_IS_OBJECT(m_pWidget))
		g_signal_handlers_disconnect_matched(
			m_pWidget,
			G_SIGNAL_MATCH_DATA,
			0,
			0,
			NULL,
			NULL,
			this);

	if (NULL != m_pMenuPopover && GTK_IS_WIDGET(m_pMenuPopover) && gtk_widget_get_parent(m_pMenuPopover) != NULL)
	{
		gtk_widget_unparent(m_pMenuPopover);
	}
	m_pMenuPopover = NULL;

	if (m_pShortcutsListView)
	{
		g_object_unref(m_pShortcutsListView);
		m_pShortcutsListView = NULL;
	}
	if (m_pBookmarkListView)
	{
		g_object_unref(m_pBookmarkListView);
		m_pBookmarkListView = NULL;
	}
	if (m_pEmptyStore)
	{
		g_object_unref(m_pEmptyStore);
		m_pEmptyStore = NULL;
	}
	if (m_pShortcutsHeaderStore)
	{
		g_object_unref(m_pShortcutsHeaderStore);
		m_pShortcutsHeaderStore = NULL;
	}
	if (m_pBookmarksHeaderStore)
	{
		g_object_unref(m_pBookmarksHeaderStore);
		m_pBookmarksHeaderStore = NULL;
	}
	if (m_pFoldersHeaderStore)
	{
		g_object_unref(m_pFoldersHeaderStore);
		m_pFoldersHeaderStore = NULL;
	}
	if (m_pSectionsStore)
	{
		g_object_unref(m_pSectionsStore);
		m_pSectionsStore = NULL;
	}
	if (m_pFlattenModel)
	{
		g_object_unref(m_pFlattenModel);
		m_pFlattenModel = NULL;
	}

	m_pShortcutsHeaderItem = NULL;
	m_pBookmarksHeaderItem = NULL;
	m_pFoldersHeaderItem = NULL;

	m_pShortcutsSelectionModel = NULL;
	m_pShortcutsStore = NULL;

	m_pBookmarkSelectionModel = NULL;
	m_pBookmarkStore = NULL;

	m_pSelectionModel = NULL;
	m_pTreeListModel = NULL;
	m_pListStoreRoots = NULL;
	m_pListView = NULL;
	m_pDropHover = NULL;
	m_pDropHoverItem = NULL;
	m_dDropX = 0;
	m_dDropY = 0;
	m_DropExpandTimer = 0;
	m_DropExpandRow = NULL;
	m_pWidget = NULL;
	m_bDropFinished = false;
}

std::list<std::string> FolderTree::FolderTreeImpl::GetSelectedFolders() const
{
	std::list<std::string> listSelectedFolders;
	std::set<std::string> seen;

	if (m_pShortcutsStore)
	{
		guint n = g_list_model_get_n_items(G_LIST_MODEL(m_pShortcutsStore));
		for (guint i = 0 ; i < n ; i++)
		{
			DirItem* item = DIR_ITEM(g_list_model_get_item(G_LIST_MODEL(m_pShortcutsStore), i));
			if (item)
			{
				if (item->checked && item->uri)
				{
					if (seen.insert(item->uri).second)
						listSelectedFolders.push_back(item->uri);
				}
				g_object_unref(item);
			}
		}
	}

	if (m_pBookmarkStore)
	{
		BookmarksPtr bmPtr = Bookmarks::GetInstance();
		guint n = g_list_model_get_n_items(G_LIST_MODEL(m_pBookmarkStore));
		for (guint i = 0 ; i < n ; i++)
		{
			DirItem* item = DIR_ITEM(g_list_model_get_item(G_LIST_MODEL(m_pBookmarkStore), i));
			if (item)
			{
				if (item->checked)
				{
					/* A checked bookmark contributes EVERY folder it points
					 * at, not just the row's single display URI. */
					if (item->bookmark_id >= 0 && bmPtr)
					{
						const Bookmark* bm = bmPtr->GetBookmark(item->bookmark_id);
						if (bm)
						{
							for (const auto& uri : bm->GetURIs())
							{
								if (seen.insert(uri).second)
									listSelectedFolders.push_back(uri);
							}
						}
						else if (item->uri && seen.insert(item->uri).second)
						{
							listSelectedFolders.push_back(item->uri);
						}
					}
					else if (item->uri && seen.insert(item->uri).second)
					{
						listSelectedFolders.push_back(item->uri);
					}
				}
				g_object_unref(item);
			}
		}
	}

	if (m_pTreeListModel)
	{
		guint n = g_list_model_get_n_items(G_LIST_MODEL(m_pTreeListModel));
		for (guint i = 0 ; i < n ; i++)
		{
			GtkTreeListRow* row = gtk_tree_list_model_get_row(m_pTreeListModel, i);
			if (NULL == row)
				continue;
			DirItem* item = DIR_ITEM(gtk_tree_list_row_get_item(row));
			if (item)
			{
				if (item->checked && item->uri)
				{
					if (seen.insert(item->uri).second)
						listSelectedFolders.push_back(item->uri);
				}
				g_object_unref(item);
			}
			g_object_unref(row);
		}
	}

	return listSelectedFolders;
}

bool FolderTree::FolderTreeImpl::GetSelectedFoldersRecursive() const
{
	/* A combined selection is loadable recursively when any checked item asks
	 * for recursion -- currently only bookmarks carry a recursive flag. */
	if (!m_pBookmarkStore)
		return false;
	BookmarksPtr bmPtr = Bookmarks::GetInstance();
	guint n = g_list_model_get_n_items(G_LIST_MODEL(m_pBookmarkStore));
	for (guint i = 0 ; i < n ; i++)
	{
		DirItem* item = DIR_ITEM(g_list_model_get_item(G_LIST_MODEL(m_pBookmarkStore), i));
		if (item)
		{
			if (item->checked && item->bookmark_id >= 0 && bmPtr)
			{
				const Bookmark* bm = bmPtr->GetBookmark(item->bookmark_id);
				if (bm && bm->GetRecursive())
				{
					g_object_unref(item);
					return true;
				}
			}
			g_object_unref(item);
		}
	}
	return false;
}

std::set<std::string> FolderTree::FolderTreeImpl::GetSelectedRecursiveFolders() const
{
	std::set<std::string> recursiveFolders;
	if (!m_pBookmarkStore)
		return recursiveFolders;

	BookmarksPtr bmPtr = Bookmarks::GetInstance();
	guint n = g_list_model_get_n_items(G_LIST_MODEL(m_pBookmarkStore));
	for (guint i = 0 ; i < n ; i++)
	{
		DirItem* item = DIR_ITEM(g_list_model_get_item(G_LIST_MODEL(m_pBookmarkStore), i));
		if (item)
		{
			if (item->checked && item->bookmark_id >= 0 && bmPtr)
			{
				const Bookmark* bm = bmPtr->GetBookmark(item->bookmark_id);
				if (bm && bm->GetRecursive())
				{
					for (const auto& uri : bm->GetURIs())
					{
						std::string norm = folder_tree_normalize_uri(uri.c_str());
						if (!norm.empty())
							recursiveFolders.insert(norm);
						recursiveFolders.insert(uri);
					}
				}
			}
			g_object_unref(item);
		}
	}
	return recursiveFolders;
}

std::list<std::string> FolderTree::FolderTreeImpl::GetSelectedURIsFromModels() const
{
	/* The URI list the "Add Bookmark" action must use: the rows that are
	 * actually SELECTED (highlighted) in the folder tree, shortcuts and
	 * bookmark lists -- NOT the checkbox state that feeds the combined
	 * picture list.  Shift-range clicks and focus moves can leave these
	 * two notions out of sync, and the user's intent on right-click is the
	 * highlighted selection. */
	std::list<std::string> uris;
	std::set<std::string> seen;

	if (m_pFlattenModel && m_pSelectionModel)
	{
		guint n = g_list_model_get_n_items(G_LIST_MODEL(m_pFlattenModel));
		for (guint i = 0; i < n; i++)
		{
			if (!gtk_selection_model_is_selected(GTK_SELECTION_MODEL(m_pSelectionModel), i))
				continue;
			GObject* obj = G_OBJECT(g_list_model_get_item(G_LIST_MODEL(m_pFlattenModel), i));
			if (!obj)
				continue;
			if (GTK_IS_TREE_LIST_ROW(obj))
			{
				GtkTreeListRow* row = GTK_TREE_LIST_ROW(obj);
				DirItem* item = DIR_ITEM(gtk_tree_list_row_get_item(row));
				if (item && item->uri && seen.insert(item->uri).second)
					uris.push_back(item->uri);
				if (item) g_object_unref(item);
			}
			else if (G_TYPE_CHECK_INSTANCE_TYPE(obj, DIR_ITEM_TYPE))
			{
				DirItem* item = DIR_ITEM(obj);
				if (item->uri && seen.insert(item->uri).second)
					uris.push_back(item->uri);
			}
			g_object_unref(obj);
		}
	}

	if (m_pShortcutsStore && m_pShortcutsSelectionModel)
	{
		guint n = g_list_model_get_n_items(G_LIST_MODEL(m_pShortcutsStore));
		for (guint i = 0; i < n; i++)
		{
			if (!gtk_selection_model_is_selected(GTK_SELECTION_MODEL(m_pShortcutsSelectionModel), i))
				continue;
			DirItem* item = DIR_ITEM(g_list_model_get_item(G_LIST_MODEL(m_pShortcutsStore), i));
			if (item)
			{
				if (item->uri && seen.insert(item->uri).second)
					uris.push_back(item->uri);
				g_object_unref(item);
			}
		}
	}

	if (m_pBookmarkStore && m_pBookmarkSelectionModel)
	{
		guint n = g_list_model_get_n_items(G_LIST_MODEL(m_pBookmarkStore));
		for (guint i = 0; i < n; i++)
		{
			if (!gtk_selection_model_is_selected(GTK_SELECTION_MODEL(m_pBookmarkSelectionModel), i))
				continue;
			DirItem* item = DIR_ITEM(g_list_model_get_item(G_LIST_MODEL(m_pBookmarkStore), i));
			if (item)
			{
				if (item->uri && seen.insert(item->uri).second)
					uris.push_back(item->uri);
				g_object_unref(item);
			}
		}
	}

	return uris;
}

std::list<std::string> FolderTree::FolderTreeImpl::GetAddBookmarkURIs(const std::string& clicked_uri) const
{
	/* "Add Bookmark" follows the row SELECTION (highlighted folders), not the
	 * checkbox state used for the combined picture list. */
	std::list<std::string> selected = GetSelectedURIsFromModels();

	/* Right-clicking a folder that is part of the current selection bookmarks
	 * the whole selection; clicking anything outside it (or with no
	 * selection) bookmarks just the clicked folder. */
	if (!clicked_uri.empty() && !selected.empty())
	{
		for (const std::string& uri : selected)
		{
			if (uri == clicked_uri)
			{
				return selected;
			}
		}
	}

	std::list<std::string> uris;
	if (!clicked_uri.empty())
	{
		uris.push_back(clicked_uri);
	}
	return uris;
}

void FolderTree::FolderTreeImpl::ClearAllCheckboxes()
{
	if (m_pShortcutsStore)
	{
		guint n = g_list_model_get_n_items(G_LIST_MODEL(m_pShortcutsStore));
		for (guint i = 0 ; i < n ; i++)
		{
			DirItem* item = DIR_ITEM(g_list_model_get_item(G_LIST_MODEL(m_pShortcutsStore), i));
			if (item)
			{
				dir_item_set_checked(item, FALSE);
				g_object_unref(item);
			}
		}
		if (m_pShortcutsSelectionModel)
			gtk_selection_model_unselect_all(GTK_SELECTION_MODEL(m_pShortcutsSelectionModel));
	}

	if (m_pBookmarkStore)
	{
		guint n = g_list_model_get_n_items(G_LIST_MODEL(m_pBookmarkStore));
		for (guint i = 0 ; i < n ; i++)
		{
			DirItem* item = DIR_ITEM(g_list_model_get_item(G_LIST_MODEL(m_pBookmarkStore), i));
			if (item)
			{
				dir_item_set_checked(item, FALSE);
				g_object_unref(item);
			}
		}
		if (m_pBookmarkSelectionModel)
			gtk_selection_model_unselect_all(GTK_SELECTION_MODEL(m_pBookmarkSelectionModel));
	}

	if (m_pTreeListModel)
	{
		guint n = g_list_model_get_n_items(G_LIST_MODEL(m_pTreeListModel));
		for (guint i = 0 ; i < n ; i++)
		{
			GtkTreeListRow* row = gtk_tree_list_model_get_row(m_pTreeListModel, i);
			if (NULL == row)
				continue;
			DirItem* item = DIR_ITEM(gtk_tree_list_row_get_item(row));
			if (item)
				dir_item_set_checked(item, FALSE);
		}
	}

	if (m_pSelectionModel)
		gtk_selection_model_unselect_all(GTK_SELECTION_MODEL(m_pSelectionModel));
}

void FolderTree::FolderTreeImpl::SyncShortcutSelectionForURI(const gchar* uri, gboolean value)
{
	if (!uri || !m_pShortcutsStore)
		return;

	std::string normalized = folder_tree_normalize_uri(uri);
	if (normalized.empty())
		return;
	const char* target = normalized.c_str();

	guint sc_offset = GetSlotFlattenOffset(1);
	guint n = g_list_model_get_n_items(G_LIST_MODEL(m_pShortcutsStore));
	for (guint i = 0; i < n; i++)
	{
		DirItem* it = DIR_ITEM(g_list_model_get_item(G_LIST_MODEL(m_pShortcutsStore), i));
		if (it)
		{
			if (it->uri && 0 == g_strcmp0(it->uri, target))
			{
				dir_item_set_checked(it, value);
				if (m_pShortcutsSelectionModel)
				{
					if (value)
						gtk_selection_model_select_item(GTK_SELECTION_MODEL(m_pShortcutsSelectionModel), i, FALSE);
					else
						gtk_selection_model_unselect_item(GTK_SELECTION_MODEL(m_pShortcutsSelectionModel), i);
				}
				if (m_bShortcutsExpanded && m_pSelectionModel)
				{
					if (value)
						gtk_selection_model_select_item(GTK_SELECTION_MODEL(m_pSelectionModel), sc_offset + i, FALSE);
					else
						gtk_selection_model_unselect_item(GTK_SELECTION_MODEL(m_pSelectionModel), sc_offset + i);
				}
			}
			g_object_unref(it);
		}
	}
}

void FolderTree::FolderTreeImpl::SyncTreeSelectionForURI(const gchar* uri, gboolean value)
{
	if (!uri || !m_pTreeListModel || !m_pListStoreRoots)
		return;

	std::string normalized = folder_tree_normalize_uri(uri);
	if (normalized.empty())
		return;
	const char* target = normalized.c_str();

	DirItem* root = FindRootForPath(target);
	if (!root)
		return;

	if (root->uri && 0 == g_strcmp0(root->uri, target))
	{
		dir_item_set_checked(root, value);
		return;
	}

	DirItem* current = root;
	ExpandItem(current);

	gchar* remaining = g_strdup(target);
	gint tries = 0;
	gboolean done = FALSE;
	while (!done && tries < 1000)
	{
		tries++;
		guint pos = FindItemPosition(current);
		if (G_MAXUINT == pos)
			break;
		GtkTreeListRow* row = gtk_tree_list_model_get_row(m_pTreeListModel, pos);
		if (!row)
			break;

		GListModel* children = gtk_tree_list_row_get_children(row);
		if (!children)
		{
			break;
		}

		gchar* base = g_strdup(current->uri);
		GFile* cur_file = g_file_new_for_uri(base);
		GFile* tgt_file = g_file_new_for_uri(remaining);

		DirItem* next = NULL;
		guint child_count = g_list_model_get_n_items(children);
		for (guint c = 0; c < child_count; c++)
		{
			DirItem* ch = DIR_ITEM(g_list_model_get_item(children, c));
			if (!ch || !ch->uri)
			{
				if (ch) g_object_unref(ch);
				continue;
			}
			GFile* ch_file = g_file_new_for_uri(ch->uri);
			if (g_file_equal(ch_file, tgt_file))
			{
				next = ch;
				done = TRUE;
				g_object_unref(ch_file);
				break;
			}
			else if (g_file_has_prefix(tgt_file, ch_file))
			{
				next = ch;
				g_object_unref(ch_file);
				break;
			}
			g_object_unref(ch_file);
			g_object_unref(ch);
		}
		g_free(base);
		g_object_unref(cur_file);
		g_object_unref(tgt_file);

		if (next)
		{
			current = next;
			if (!done)
				ExpandItem(current);
			g_object_unref(next);
		}
		else
		{
			break;
		}
	}
	g_free(remaining);

	if (current)
	{
		dir_item_set_checked(current, value);
	}
}

void FolderTree::FolderTreeImpl::SetCheckboxForItem(DirItem* item, gboolean value)
{
	dir_item_set_checked(item, value);
	if (m_pSelectionModel && m_bFoldersExpanded)
	{
		guint pos = FindItemPosition(item);
		if (pos != G_MAXUINT)
		{
			guint flat_pos = GetTreeFlattenOffset() + pos;
			if (value)
				gtk_selection_model_select_item(GTK_SELECTION_MODEL(m_pSelectionModel), flat_pos, FALSE);
			else
				gtk_selection_model_unselect_item(GTK_SELECTION_MODEL(m_pSelectionModel), flat_pos);
		}
	}
}

guint FolderTree::FolderTreeImpl::FindItemPosition(DirItem* target)
{
	if (NULL == target)
		return G_MAXUINT;
	guint n = g_list_model_get_n_items(G_LIST_MODEL(m_pTreeListModel));
	for (guint i = 0 ; i < n ; i++)
	{
		GtkTreeListRow* row = gtk_tree_list_model_get_row(m_pTreeListModel, i);
		if (NULL == row)
			continue;
		DirItem* item = DIR_ITEM(gtk_tree_list_row_get_item(row));
		gboolean match = (item == target);
		if (item)
			g_object_unref(item);
		g_object_unref(row);
		if (match)
			return i;
	}
	return G_MAXUINT;
}

DirItem* FolderTree::FolderTreeImpl::FindRootForPath(const gchar* uri)
{
	guint n = g_list_model_get_n_items(G_LIST_MODEL(m_pListStoreRoots));
	DirItem* best = NULL;
	guint best_depth = 0;
	for (guint i = 0 ; i < n ; i++)
	{
		DirItem* item = DIR_ITEM(g_list_model_get_item(G_LIST_MODEL(m_pListStoreRoots), i));
		if (NULL == item->uri)
		{
			g_object_unref(item);
			continue;
		}
		GFile* file_root = g_file_new_for_uri(item->uri);
		GFile* file = g_file_new_for_uri(uri);
		if (g_file_has_prefix(file, file_root) || g_file_equal(file, file_root))
		{
			guint depth = 0;
			GFile* p = g_file_get_parent(file_root);
			while (NULL != p)
			{
				GFile* next = g_file_get_parent(p);
				g_object_unref(p);
				p = next;
				depth++;
			}
			if (depth >= best_depth)
			{
				if (NULL != best)
					g_object_unref(best);
				best_depth = depth;
				best = item;
				g_object_ref(best);
			}
		}
		g_object_unref(file_root);
		g_object_unref(file);
		g_object_unref(item);
	}
	return best;
}

void FolderTree::FolderTreeImpl::ExpandItem(DirItem* item)
{
	guint pos = FindItemPosition(item);
	if (G_MAXUINT == pos)
		return;
	GtkTreeListRow* row = gtk_tree_list_model_get_row(m_pTreeListModel, pos);
	if (NULL != row)
	{
		gtk_tree_list_row_set_expanded(row, TRUE);
		g_object_unref(row);
	}
}

void FolderTree::FolderTreeImpl::AddChildFolder(const char *parent_uri, const char *child_uri, const char *folder_name)
{
	if (NULL == parent_uri || NULL == child_uri || NULL == folder_name || NULL == m_pTreeListModel)
		return;

	std::string norm_parent = folder_tree_normalize_uri(parent_uri);
	guint pos = FindItemByURI(norm_parent.c_str(), FALSE);
	if (pos == G_MAXUINT)
		pos = FindItemByURI(parent_uri, FALSE);
	if (pos == G_MAXUINT)
		return;

	GtkTreeListRow* row = gtk_tree_list_model_get_row(m_pTreeListModel, pos);
	if (NULL == row)
		return;

	gboolean is_expanded = gtk_tree_list_row_get_expanded(row);
	if (is_expanded)
	{
		GListModel* children = gtk_tree_list_row_get_children(row);
		if (children && G_IS_LIST_STORE(children))
		{
			guint n_children = g_list_model_get_n_items(children);
			gboolean exists = FALSE;
			for (guint c = 0; c < n_children; c++)
			{
				DirItem* ci = DIR_ITEM(g_list_model_get_item(children, c));
				if (ci && ci->uri && g_strcmp0(ci->uri, child_uri) == 0)
				{
					exists = TRUE;
					g_object_unref(ci);
					break;
				}
				if (ci)
					g_object_unref(ci);
			}
			if (!exists)
			{
				DirItem* parent_item = DIR_ITEM(gtk_tree_list_row_get_item(row));
				guint depth = parent_item ? parent_item->node_depth + 1 : 1;
				if (parent_item)
					g_object_unref(parent_item);

				DirItem* new_child = dir_item_new(child_uri, folder_name, "folder", FALSE, -1, depth);
				g_list_store_insert_sorted(G_LIST_STORE(children), G_OBJECT(new_child),
					GCompareDataFunc(+[](gconstpointer a, gconstpointer b, gpointer) -> gint {
						DirItem* ia = DIR_ITEM(a);
						DirItem* ib = DIR_ITEM(b);
						return g_ascii_strcasecmp(ia->display_name, ib->display_name);
					}), NULL);
				g_object_unref(new_child);
			}
		}
	}
	else
	{
		gtk_tree_list_row_set_expanded(row, TRUE);
	}
	g_object_unref(row);
}

void FolderTree::FolderTreeImpl::RemoveFolder(const char *folder_uri)
{
	if (NULL == folder_uri || NULL == m_pTreeListModel)
		return;

	std::string norm = folder_tree_normalize_uri(folder_uri);
	guint pos = FindItemByURI(norm.c_str(), FALSE);
	if (pos == G_MAXUINT)
		pos = FindItemByURI(folder_uri, FALSE);
	if (pos == G_MAXUINT)
		return;

	GtkTreeListRow* row = gtk_tree_list_model_get_row(m_pTreeListModel, pos);
	if (NULL == row)
		return;

	GtkTreeListRow* parent_row = gtk_tree_list_row_get_parent(row);
	if (parent_row)
	{
		GListModel* children = gtk_tree_list_row_get_children(parent_row);
		if (children && G_IS_LIST_STORE(children))
		{
			guint n_children = g_list_model_get_n_items(children);
			for (guint c = 0; c < n_children; c++)
			{
				DirItem* ci = DIR_ITEM(g_list_model_get_item(children, c));
				if (ci && ci->uri && (g_strcmp0(ci->uri, folder_uri) == 0 || g_strcmp0(ci->uri, norm.c_str()) == 0))
				{
					g_object_unref(ci);
					g_list_store_remove(G_LIST_STORE(children), c);
					break;
				}
				if (ci)
					g_object_unref(ci);
			}
		}
		g_object_unref(parent_row);
	}
	g_object_unref(row);
}

guint FolderTree::FolderTreeImpl::FindItemByURI(const gchar* uri, gboolean check_only)
{
	(void)check_only;
	if (NULL == uri)
		return G_MAXUINT;
	guint n = g_list_model_get_n_items(G_LIST_MODEL(m_pTreeListModel));
	for (guint i = 0 ; i < n ; i++)
	{
		GtkTreeListRow* row = gtk_tree_list_model_get_row(m_pTreeListModel, i);
		if (NULL == row)
			continue;
		DirItem* item = DIR_ITEM(gtk_tree_list_row_get_item(row));
		if (NULL != item->uri && 0 == g_strcmp0(item->uri, uri))
			return i;
	}
	return G_MAXUINT;
}

void  FolderTree::FolderTreeImpl::SetSelectedFolders(const std::list<std::string> &uris, bool bRecursive, const std::set<std::string>& recursiveFolders)
{
	(void)recursiveFolders;
	ClearAllCheckboxes();

	// normalize plain paths and file URIs to file:// URIs so the tree can
	// match them; a folder passed on the command line arrives as a plain
	// path while tree items always carry file:// URIs
	std::list<std::string> normUris;
	{
		std::list<std::string>::const_iterator itr;
		for (itr = uris.begin(); uris.end() != itr; ++itr)
		{
			std::string normalized = folder_tree_normalize_uri(itr->c_str());
			if (!normalized.empty())
				normUris.push_back(normalized);
		}
	}

	// Compare with bookmarks: if a bookmark has the same folder set and recursive setting,
	// check off that bookmark in the bookmarks list and check its folders.
	bool bBookmarkMatched = false;
	if (m_pBookmarkStore)
	{
		BookmarksPtr bmPtr = Bookmarks::GetInstance();
		if (bmPtr)
		{
			std::set<std::string> targetSet;
			for (const auto& u : normUris)
			{
				std::string s = u;
				while (!s.empty() && s.back() == '/') s.pop_back();
				targetSet.insert(s);
			}

			guint bn = g_list_model_get_n_items(G_LIST_MODEL(m_pBookmarkStore));
			for (guint i = 0; i < bn; i++)
			{
				DirItem* it = DIR_ITEM(g_list_model_get_item(G_LIST_MODEL(m_pBookmarkStore), i));
				if (it)
				{
					if (it->bookmark_id >= 0)
					{
						const Bookmark* bm = bmPtr->GetBookmark(it->bookmark_id);
						if (bm && bm->GetRecursive() == bRecursive)
						{
							std::set<std::string> bmSet;
							for (const auto& u : bm->GetURIs())
							{
								std::string norm = folder_tree_normalize_uri(u.c_str());
								std::string s = !norm.empty() ? norm : u;
								while (!s.empty() && s.back() == '/') s.pop_back();
								bmSet.insert(s);
							}
							if (!bmSet.empty() && bmSet == targetSet)
							{
								dir_item_set_checked(it, TRUE);
								if (m_pBookmarkSelectionModel)
								{
									gtk_selection_model_select_item(
										GTK_SELECTION_MODEL(m_pBookmarkSelectionModel), i, FALSE);
								}
								bBookmarkMatched = true;
							}
						}
					}
					g_object_unref(it);
				}
			}
		}
	}

	if (!uris.empty() && !m_bFoldersExpanded)
	{
		m_pFolderTree->SetFoldersExpanded(true);
	}
	if (bBookmarkMatched && !m_bBookmarksExpanded)
	{
		m_pFolderTree->SetBookmarksExpanded(true);
	}

	guint first_found = G_MAXUINT;

	std::list<std::string>::iterator itr;
	for (itr = normUris.begin(); normUris.end() != itr; ++itr)
	{
		SyncShortcutSelectionForURI(itr->c_str(), TRUE);

		DirItem* root = FindRootForPath(itr->c_str());
		if (NULL == root)
			continue;

		if (root->uri && (0 == g_strcmp0(root->uri, itr->c_str()) || 0 == g_ascii_strcasecmp(root->uri, itr->c_str())))
		{
			SetCheckboxForItem(root, TRUE);
			guint fpos = FindItemPosition(root);
			if (G_MAXUINT == first_found)
				first_found = fpos;
			continue;
		}

		// walk down the path expanding each intermediate row and
		// locating (creating is unnecessary: subdirs are enumerated on
		// expansion) the target folder
		DirItem* current = root;
		ExpandItem(current);

		gchar* remaining = g_strdup(itr->c_str());
		gint tries = 0;

		gboolean done = FALSE;
		while (!done && tries < 1000)
		{
			tries++;
			guint pos = FindItemPosition(current);
			if (G_MAXUINT == pos)
				break;
			GtkTreeListRow* row = gtk_tree_list_model_get_row(m_pTreeListModel, pos);
			if (NULL == row)
				break;

			GListModel* children = gtk_tree_list_row_get_children(row);
			if (NULL == children)
			{
				break;
			}

			gchar* base = g_strdup(current->uri);
			GFile* cur_file = g_file_new_for_uri(base);
			// remaining stays the FULL target path for the whole walk;
			// the loop locates the child that is either an ancestor of
			// the target (drop down into it) or equals the target.
			GFile* tgt_file = g_file_new_for_uri(remaining);

			DirItem* next = NULL;
			guint child_count = g_list_model_get_n_items(children);
			for (guint c = 0 ; c < child_count ; c++)
			{
				// child is a real row item returned by the model; take
				// ownership of the reference so the walk can descend into
				// it on later iterations (FindItemPosition must see the
				// model item, not a copy)
				DirItem* child = DIR_ITEM(g_list_model_get_item(children, c));
				if (NULL == child || NULL == child->uri)
				{
					if (NULL != child)
						g_object_unref(child);
					continue;
				}

				GFile* child_file = g_file_new_for_uri(child->uri);
				if (g_file_equal(child_file, tgt_file))
				{
					next = child;
					done = TRUE;
					g_object_unref(child_file);
					break;
				}
				else if (g_file_has_prefix(tgt_file, child_file))
				{
					next = child;
					g_object_unref(child_file);
					break;
				}
				g_object_unref(child_file);
				g_object_unref(child);
			}
			g_object_unref(cur_file);
			g_object_unref(tgt_file);
			g_free(base);

			if (NULL != next)
			{
				if (done)
				{
					SetCheckboxForItem(next, TRUE);
					guint fpos = FindItemPosition(next);
					if (G_MAXUINT == first_found)
						first_found = fpos;
					g_object_unref(next);
				}
				else
				{
					g_object_unref(current);
					current = next;
					ExpandItem(current);
				}
			}
			else
			{
				done = TRUE;
			}
		}

		if (NULL != remaining)
			g_free(remaining);
		g_object_unref(current);
	}

	if (G_MAXUINT != first_found)
	{
		ScrollToTreePosition(first_found, GTK_LIST_SCROLL_NONE);
	}
}

static GListModel* create_subdirs (gpointer item_data, gpointer user_data)
{
	(void)user_data;
	DirItem* folder = DIR_ITEM(item_data);
	GListStore* children = g_list_store_new(DIR_ITEM_TYPE);

	if (NULL != folder->uri)
	{
		GFile* file = g_file_new_for_uri(folder->uri);
		GFileEnumerator* enumerator = g_file_enumerate_children(
			file,
			G_FILE_ATTRIBUTE_STANDARD_NAME ","
			G_FILE_ATTRIBUTE_STANDARD_TYPE ","
			G_FILE_ATTRIBUTE_STANDARD_IS_HIDDEN ","
			G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME ","
			G_FILE_ATTRIBUTE_STANDARD_ICON,
			G_FILE_QUERY_INFO_NONE,
			NULL,
			NULL);
		if (NULL != enumerator)
		{
			GFileInfo* info = NULL;
			GList* listed = NULL;
			while (NULL != (info = g_file_enumerator_next_file(enumerator, NULL, NULL)))
			{
				const char* name = g_file_info_get_name(info);
				if (name && name[0] == '.')
				{
					g_object_unref(info);
					continue;
				}
				if (g_file_info_get_is_hidden(info))
				{
					g_object_unref(info);
					continue;
				}
				if (G_FILE_TYPE_DIRECTORY == g_file_info_get_file_type(info))
				{
					GFile* child_file = g_file_get_child(file, name);
					gchar* child_uri = g_file_get_uri(child_file);
					gchar* display = g_strdup(g_file_info_get_display_name(info));
					gchar* icon = folder_tree_get_icon_name_from_info(info, child_file);
					DirItem* child = dir_item_new(child_uri, display, icon,
						FALSE, -1, folder->node_depth + 1);
					listed = g_list_prepend(listed, child);
					g_free(icon);
					g_free(display);
					g_free(child_uri);
					g_object_unref(child_file);
				}
				g_object_unref(info);
			}
			g_object_unref(enumerator);

			// sort children alphabetically (case-insensitive)
			listed = g_list_sort(listed, GCompareFunc(+[](gconstpointer a, gconstpointer b) -> gint {
				DirItem* ia = DIR_ITEM(a);
				DirItem* ib = DIR_ITEM(b);
				return g_ascii_strcasecmp(ia->display_name, ib->display_name);
			}));

			GList* itr;
			for (itr = listed ; NULL != itr ; itr = itr->next)
			{
				DirItem* child = DIR_ITEM(itr->data);
				g_list_store_append(children, G_OBJECT(child));
				g_object_unref(child);
			}
			g_list_free(listed);
		}
		g_object_unref(file);
	}

	return G_LIST_MODEL(children);
}

static guint shortcuts_get_focused_position(FolderTree::FolderTreeImpl* impl)
{
	if (!impl)
		return G_MAXUINT;

	if (impl->m_pListView && impl->m_bShortcutsExpanded)
	{
		GtkWidget* lv = GTK_WIDGET(impl->m_pListView);
		GtkRoot* root = gtk_widget_get_root(lv);
		GtkWidget* f = root ? gtk_root_get_focus(root) : NULL;
		if (!f)
			f = gtk_widget_get_focus_child(lv);

		for (GtkWidget* w = f; w && w != lv; w = gtk_widget_get_parent(w))
		{
			if (g_object_get_data(G_OBJECT(w), "sc-item") != NULL)
			{
				return GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(w), "sc-pos"));
			}
		}
	}

	// Fallback to first selected item in shortcuts selection model
	if (impl->m_pShortcutsSelectionModel && impl->m_pShortcutsStore)
	{
		guint n = g_list_model_get_n_items(G_LIST_MODEL(impl->m_pShortcutsStore));
		for (guint i = 0; i < n; i++)
		{
			if (gtk_selection_model_is_selected(GTK_SELECTION_MODEL(impl->m_pShortcutsSelectionModel), i))
				return i;
		}
		if (n > 0)
			return 0;
	}

	return G_MAXUINT;
}

static void shortcut_row_on_clicked(GtkGestureClick* gesture, int n_press, double x, double y)
{
	(void)n_press;
	GtkWidget* w = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
	FolderTreeRowWidgets* rw = static_cast<FolderTreeRowWidgets*>(
		g_object_get_data(G_OBJECT(w), "row-widgets"));
	FolderTree::FolderTreeImpl* impl = rw ? rw->impl : static_cast<FolderTree::FolderTreeImpl*>(g_object_get_data(G_OBJECT(w), "dir-impl"));
	DirItem* item = rw ? rw->bound_item : static_cast<DirItem*>(g_object_get_data(G_OBJECT(w), "sc-item"));
	guint pos = rw ? rw->bound_sc_pos : GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(w), "sc-pos"));
	GtkListItem* li = rw ? rw->list_item : static_cast<GtkListItem*>(g_object_get_data(G_OBJECT(w), "list-item"));
	guint flat_pos = li ? gtk_list_item_get_position(li) : G_MAXUINT;
	if (!impl || !item) return;

	GtkWidget* target = gtk_widget_pick(w, x, y, GTK_PICK_DEFAULT);
	GtkWidget* check = rw ? rw->check : GTK_WIDGET(g_object_get_data(G_OBJECT(w), "f-check"));
	if (!check)
		check = GTK_WIDGET(g_object_get_data(G_OBJECT(w), "sc-check"));
	if (target != NULL && (GTK_IS_CHECK_BUTTON(target) || (check && (target == check || gtk_widget_is_ancestor(target, check)))))
	{
		return;
	}

	guint button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));
	if (button == 0)
		button = 1;
	GdkModifierType state = gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(gesture));

	if (button == 1)
	{
		if (state & GDK_CONTROL_MASK)
		{
			gboolean new_val = !item->checked;
			if (new_val && !QuiverUtils::CheckLocationExists(item->uri))
			{
				std::string msg = _("Location unavailable");
				if (item->display_name && item->display_name[0])
					msg += std::string(": ") + item->display_name;
				QuiverUtils::ShowHUDToast(msg.c_str(), "dialog-warning-symbolic", 4000);
				return;
			}
			dir_item_set_checked(item, new_val);
			if (impl->m_pShortcutsSelectionModel && pos != G_MAXUINT)
			{
				if (new_val)
					gtk_selection_model_select_item(GTK_SELECTION_MODEL(impl->m_pShortcutsSelectionModel), pos, FALSE);
				else
					gtk_selection_model_unselect_item(GTK_SELECTION_MODEL(impl->m_pShortcutsSelectionModel), pos);
			}
			if (impl->m_pSelectionModel && flat_pos != G_MAXUINT)
			{
				if (new_val)
					gtk_selection_model_select_item(GTK_SELECTION_MODEL(impl->m_pSelectionModel), flat_pos, FALSE);
				else
					gtk_selection_model_unselect_item(GTK_SELECTION_MODEL(impl->m_pSelectionModel), flat_pos);
			}
			impl->SyncTreeSelectionForURI(item->uri, new_val);
		}
		else if (state & GDK_SHIFT_MASK)
		{
			guint start = shortcuts_get_focused_position(impl);
			if (start == G_MAXUINT)
				start = pos;
			guint min_pos = std::min(start, pos);
			guint max_pos = std::max(start, pos);
			guint count = max_pos - min_pos + 1;
			if (pos != G_MAXUINT && impl->m_pShortcutsSelectionModel)
			{
				gtk_selection_model_select_range(GTK_SELECTION_MODEL(impl->m_pShortcutsSelectionModel), min_pos, count, TRUE);
			}
			if (pos != G_MAXUINT && impl->m_pSelectionModel && impl->m_bShortcutsExpanded)
			{
				guint sc_off = impl->GetSlotFlattenOffset(1);
				gtk_selection_model_select_range(GTK_SELECTION_MODEL(impl->m_pSelectionModel), sc_off + min_pos, count, TRUE);
			}
		}
		else
		{
			if (!QuiverUtils::CheckLocationExists(item->uri))
			{
				std::string msg = _("Location unavailable");
				if (item->display_name && item->display_name[0])
					msg += std::string(": ") + item->display_name;
				QuiverUtils::ShowHUDToast(msg.c_str(), "dialog-warning-symbolic", 4000);
				return;
			}
			impl->ClearAllCheckboxes();
			dir_item_set_checked(item, TRUE);
			if (impl->m_pShortcutsSelectionModel && pos != G_MAXUINT)
			{
				gtk_selection_model_select_item(GTK_SELECTION_MODEL(impl->m_pShortcutsSelectionModel), pos, TRUE);
			}
			if (impl->m_pSelectionModel && flat_pos != G_MAXUINT)
			{
				gtk_selection_model_select_item(GTK_SELECTION_MODEL(impl->m_pSelectionModel), flat_pos, TRUE);
			}
			impl->SyncTreeSelectionForURI(item->uri, TRUE);
		}
		gtk_widget_grab_focus(w);
		impl->m_pFolderTree->EmitSelectionChangedEvent(false);
	}
	else if (button == 2)
	{
		gboolean new_val = !item->checked;
		if (new_val && !QuiverUtils::CheckLocationExists(item->uri))
		{
			std::string msg = _("Location unavailable");
			if (item->display_name && item->display_name[0])
				msg += std::string(": ") + item->display_name;
			QuiverUtils::ShowHUDToast(msg.c_str(), "dialog-warning-symbolic", 4000);
			return;
		}
		dir_item_set_checked(item, new_val);
		if (impl->m_pShortcutsSelectionModel && pos != G_MAXUINT)
		{
			if (new_val)
				gtk_selection_model_select_item(GTK_SELECTION_MODEL(impl->m_pShortcutsSelectionModel), pos, FALSE);
			else
				gtk_selection_model_unselect_item(GTK_SELECTION_MODEL(impl->m_pShortcutsSelectionModel), pos);
		}
		if (impl->m_pSelectionModel && flat_pos != G_MAXUINT)
		{
			if (new_val)
				gtk_selection_model_select_item(GTK_SELECTION_MODEL(impl->m_pSelectionModel), flat_pos, FALSE);
			else
				gtk_selection_model_unselect_item(GTK_SELECTION_MODEL(impl->m_pSelectionModel), flat_pos);
		}
		impl->SyncTreeSelectionForURI(item->uri, new_val);
		gtk_widget_grab_focus(w);
		impl->m_pFolderTree->EmitSelectionChangedEvent();
	}
	else if (button == 3)
	{
		if (pos != G_MAXUINT && impl->m_pShortcutsSelectionModel &&
		    !gtk_selection_model_is_selected(GTK_SELECTION_MODEL(impl->m_pShortcutsSelectionModel), pos))
		{
			gtk_selection_model_select_item(GTK_SELECTION_MODEL(impl->m_pShortcutsSelectionModel), pos, TRUE);
		}
		if (flat_pos != G_MAXUINT && impl->m_pSelectionModel &&
		    !gtk_selection_model_is_selected(GTK_SELECTION_MODEL(impl->m_pSelectionModel), flat_pos))
		{
			gtk_selection_model_select_item(GTK_SELECTION_MODEL(impl->m_pSelectionModel), flat_pos, TRUE);
		}
		view_popup_menu_at(w, x, y, TreePasteMode::PASTE_INTO_FOLDER, item->uri, impl);
	}
}

static void bookmark_row_on_clicked(GtkGestureClick* gesture, int n_press, double x, double y)
{
	(void)n_press;
	GtkWidget* w = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
	FolderTreeRowWidgets* rw = static_cast<FolderTreeRowWidgets*>(
		g_object_get_data(G_OBJECT(w), "row-widgets"));
	FolderTree::FolderTreeImpl* impl = rw ? rw->impl : static_cast<FolderTree::FolderTreeImpl*>(g_object_get_data(G_OBJECT(w), "dir-impl"));
	DirItem* item = rw ? rw->bound_item : static_cast<DirItem*>(g_object_get_data(G_OBJECT(w), "bm-item"));
	guint pos = rw ? rw->bound_bm_pos : GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(w), "bm-pos"));
	GtkListItem* li = rw ? rw->list_item : static_cast<GtkListItem*>(g_object_get_data(G_OBJECT(w), "list-item"));
	guint flat_pos = li ? gtk_list_item_get_position(li) : G_MAXUINT;
	if (!impl || !item) return;

	GtkWidget* target = gtk_widget_pick(w, x, y, GTK_PICK_DEFAULT);
	GtkWidget* check = rw ? rw->check : GTK_WIDGET(g_object_get_data(G_OBJECT(w), "f-check"));
	if (!check)
		check = GTK_WIDGET(g_object_get_data(G_OBJECT(w), "bm-check"));
	if (target != NULL && (GTK_IS_CHECK_BUTTON(target) || (check && (target == check || gtk_widget_is_ancestor(target, check)))))
	{
		return;
	}

	guint button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));
	if (button == 0)
		button = 1;
	GdkModifierType state = gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(gesture));

	if (button == 1)
	{
		const Bookmark* bm = NULL;
		if (item->bookmark_id >= 0)
		{
			BookmarksPtr bmPtr = Bookmarks::GetInstance();
			bm = bmPtr->GetBookmark(item->bookmark_id);
		}

		if (state & GDK_CONTROL_MASK)
		{
			gboolean new_val = !item->checked;
			if (new_val)
			{
				std::string unavail_name;
				bool avail = bm ? QuiverUtils::CheckBookmarkLocationAvailable(*bm, &unavail_name)
				                : QuiverUtils::CheckLocationExists(item->uri);
				if (!avail)
				{
					std::string msg = _("Location unavailable");
					if (!unavail_name.empty())
						msg += ": " + unavail_name;
					else if (item->display_name && item->display_name[0])
						msg += std::string(": ") + item->display_name;
					QuiverUtils::ShowHUDToast(msg.c_str(), "dialog-warning-symbolic", 4000);
					return;
				}
			}
			dir_item_set_checked(item, new_val);
			if (impl->m_pBookmarkSelectionModel && pos != G_MAXUINT)
			{
				if (new_val)
					gtk_selection_model_select_item(GTK_SELECTION_MODEL(impl->m_pBookmarkSelectionModel), pos, FALSE);
				else
					gtk_selection_model_unselect_item(GTK_SELECTION_MODEL(impl->m_pBookmarkSelectionModel), pos);
			}
			if (impl->m_pSelectionModel && flat_pos != G_MAXUINT)
			{
				if (new_val)
					gtk_selection_model_select_item(GTK_SELECTION_MODEL(impl->m_pSelectionModel), flat_pos, FALSE);
				else
					gtk_selection_model_unselect_item(GTK_SELECTION_MODEL(impl->m_pSelectionModel), flat_pos);
			}
			if (NULL != bm)
			{
				std::list<std::string> uris = bm->GetURIs();
				for (const auto& uri : uris)
				{
					impl->SyncTreeSelectionForURI(uri.c_str(), new_val);
					impl->SyncShortcutSelectionForURI(uri.c_str(), new_val);
				}
			}
			else
			{
				impl->SyncTreeSelectionForURI(item->uri, new_val);
				impl->SyncShortcutSelectionForURI(item->uri, new_val);
			}
		}
		else
		{
			std::string unavail_name;
			bool avail = bm ? QuiverUtils::CheckBookmarkLocationAvailable(*bm, &unavail_name)
			                : QuiverUtils::CheckLocationExists(item->uri);
			if (!avail)
			{
				std::string msg = _("Location unavailable");
				if (!unavail_name.empty())
					msg += ": " + unavail_name;
				else if (item->display_name && item->display_name[0])
					msg += std::string(": ") + item->display_name;
				QuiverUtils::ShowHUDToast(msg.c_str(), "dialog-warning-symbolic", 4000);
				return;
			}
			impl->ClearAllCheckboxes();
			dir_item_set_checked(item, TRUE);
			if (impl->m_pBookmarkSelectionModel && pos != G_MAXUINT)
			{
				gtk_selection_model_select_item(GTK_SELECTION_MODEL(impl->m_pBookmarkSelectionModel), pos, TRUE);
			}
			if (impl->m_pSelectionModel && flat_pos != G_MAXUINT)
			{
				gtk_selection_model_select_item(GTK_SELECTION_MODEL(impl->m_pSelectionModel), flat_pos, TRUE);
			}
			if (NULL != bm)
			{
				std::list<std::string> uris = bm->GetURIs();
				std::list<std::string>::const_iterator uri;
				for (uri = uris.begin(); uris.end() != uri; ++uri)
				{
					impl->SyncTreeSelectionForURI(uri->c_str(), TRUE);
					impl->SyncShortcutSelectionForURI(uri->c_str(), TRUE);
				}
				impl->m_pFolderTree->EmitBookmarkOpenEvent(uris, bm->GetRecursive());
				gtk_widget_grab_focus(w);
				return;
			}
			impl->SyncTreeSelectionForURI(item->uri, TRUE);
		}
		gtk_widget_grab_focus(w);
		impl->m_pFolderTree->EmitSelectionChangedEvent(false);
	}
	else if (button == 2)
	{
		gboolean new_val = !item->checked;
		if (new_val)
		{
			const Bookmark* bm = NULL;
			if (item->bookmark_id >= 0)
			{
				BookmarksPtr bmPtr = Bookmarks::GetInstance();
				bm = bmPtr->GetBookmark(item->bookmark_id);
			}
			std::string unavail_name;
			bool avail = bm ? QuiverUtils::CheckBookmarkLocationAvailable(*bm, &unavail_name)
			                : QuiverUtils::CheckLocationExists(item->uri);
			if (!avail)
			{
				std::string msg = _("Location unavailable");
				if (!unavail_name.empty())
					msg += ": " + unavail_name;
				else if (item->display_name && item->display_name[0])
					msg += std::string(": ") + item->display_name;
				QuiverUtils::ShowHUDToast(msg.c_str(), "dialog-warning-symbolic", 4000);
				return;
			}
		}
		dir_item_set_checked(item, new_val);
		if (impl->m_pBookmarkSelectionModel && pos != G_MAXUINT)
		{
			if (new_val)
				gtk_selection_model_select_item(GTK_SELECTION_MODEL(impl->m_pBookmarkSelectionModel), pos, FALSE);
			else
				gtk_selection_model_unselect_item(GTK_SELECTION_MODEL(impl->m_pBookmarkSelectionModel), pos);
		}
		if (impl->m_pSelectionModel && flat_pos != G_MAXUINT)
		{
			if (new_val)
				gtk_selection_model_select_item(GTK_SELECTION_MODEL(impl->m_pSelectionModel), flat_pos, FALSE);
			else
				gtk_selection_model_unselect_item(GTK_SELECTION_MODEL(impl->m_pSelectionModel), flat_pos);
		}
		const Bookmark* bm = NULL;
		if (item->bookmark_id >= 0)
		{
			BookmarksPtr bmPtr = Bookmarks::GetInstance();
			bm = bmPtr->GetBookmark(item->bookmark_id);
		}
		if (NULL != bm)
		{
			std::list<std::string> uris = bm->GetURIs();
			for (const auto& uri : uris)
			{
				impl->SyncTreeSelectionForURI(uri.c_str(), new_val);
				impl->SyncShortcutSelectionForURI(uri.c_str(), new_val);
			}
		}
		else
		{
			impl->SyncTreeSelectionForURI(item->uri, new_val);
			impl->SyncShortcutSelectionForURI(item->uri, new_val);
		}
		gtk_widget_grab_focus(w);
		impl->m_pFolderTree->EmitSelectionChangedEvent();
	}
	else if (button == 3)
	{
		if (pos != G_MAXUINT && impl->m_pBookmarkSelectionModel &&
		    !gtk_selection_model_is_selected(GTK_SELECTION_MODEL(impl->m_pBookmarkSelectionModel), pos))
		{
			gtk_selection_model_select_item(GTK_SELECTION_MODEL(impl->m_pBookmarkSelectionModel), pos, TRUE);
		}
		if (flat_pos != G_MAXUINT && impl->m_pSelectionModel &&
		    !gtk_selection_model_is_selected(GTK_SELECTION_MODEL(impl->m_pSelectionModel), flat_pos))
		{
			gtk_selection_model_select_item(GTK_SELECTION_MODEL(impl->m_pSelectionModel), flat_pos, TRUE);
		}
		TreePasteMode mode = folder_tree_uri_is_directory(item->uri)
			? TreePasteMode::PASTE_INTO_FOLDER : TreePasteMode::PASTE_NONE;
		view_popup_menu_at(w, x, y, mode, item->uri, impl, item->bookmark_id);
	}
}

static gboolean shortcuts_on_key_press(GtkEventControllerKey *controller, guint keyval, guint keycode, GdkModifierType state, gpointer userdata)
{
	(void)keycode; (void)controller;
	FolderTree::FolderTreeImpl* pFolderTreeImpl = (FolderTree::FolderTreeImpl*)userdata;
	if (!pFolderTreeImpl || !pFolderTreeImpl->m_pShortcutsStore || !pFolderTreeImpl->m_pShortcutsSelectionModel)
		return FALSE;

	guint n = g_list_model_get_n_items(G_LIST_MODEL(pFolderTreeImpl->m_pShortcutsStore));
	if (n == 0) return FALSE;

	if (GDK_KEY_Menu == keyval || ((state & GDK_SHIFT_MASK) && (GDK_KEY_F10 == keyval)))
	{
		const gchar* target_uri = NULL;
		guint cursor_pos = shortcuts_get_focused_position(pFolderTreeImpl);
		if (cursor_pos != G_MAXUINT && cursor_pos < n)
		{
			DirItem* item = DIR_ITEM(g_list_model_get_item(G_LIST_MODEL(pFolderTreeImpl->m_pShortcutsStore), cursor_pos));
			if (item)
			{
				target_uri = item->uri;
				g_object_unref(item);
			}
		}
		view_popup_menu_at(pFolderTreeImpl->m_pWidget, -1, -1, TreePasteMode::PASTE_INTO_FOLDER, target_uri, userdata);
		return TRUE;
	}

	GtkSelectionModel* sel = GTK_SELECTION_MODEL(pFolderTreeImpl->m_pShortcutsSelectionModel);
	guint cursor_pos = shortcuts_get_focused_position(pFolderTreeImpl);

	guint sel_count = 0;
	for (guint i = 0; i < n; i++)
	{
		if (gtk_selection_model_is_selected(sel, i))
			sel_count++;
	}

	if (GDK_KEY_space == keyval && !(state & GDK_CONTROL_MASK))
	{
		if (sel_count >= 1)
		{
			gboolean has_unchecked = FALSE;
			for (guint i = 0; i < n; i++)
			{
				if (gtk_selection_model_is_selected(sel, i))
				{
					DirItem* item = DIR_ITEM(g_list_model_get_item(G_LIST_MODEL(pFolderTreeImpl->m_pShortcutsStore), i));
					if (item)
					{
						if (!item->checked)
							has_unchecked = TRUE;
						g_object_unref(item);
						if (has_unchecked) break;
					}
				}
			}
			gboolean target_val = has_unchecked ? TRUE : FALSE;
			for (guint i = 0; i < n; i++)
			{
				if (gtk_selection_model_is_selected(sel, i))
				{
					DirItem* item = DIR_ITEM(g_list_model_get_item(G_LIST_MODEL(pFolderTreeImpl->m_pShortcutsStore), i));
					if (item)
					{
						dir_item_set_checked(item, target_val);
						pFolderTreeImpl->SyncTreeSelectionForURI(item->uri, target_val);
						g_object_unref(item);
					}
				}
			}
			pFolderTreeImpl->m_pFolderTree->EmitSelectionChangedEvent();
		}
		else
		{
			if (cursor_pos != G_MAXUINT)
			{
				DirItem* item = DIR_ITEM(g_list_model_get_item(G_LIST_MODEL(pFolderTreeImpl->m_pShortcutsStore), cursor_pos));
				if (item)
				{
					gboolean new_val = !item->checked;
					dir_item_set_checked(item, new_val);
					pFolderTreeImpl->SyncTreeSelectionForURI(item->uri, new_val);
					g_object_unref(item);
					pFolderTreeImpl->m_pFolderTree->EmitSelectionChangedEvent();
				}
			}
		}
		return TRUE;
	}

	if (GDK_KEY_Return == keyval || GDK_KEY_KP_Enter == keyval)
	{
		if (state & GDK_CONTROL_MASK)
		{
			if (cursor_pos != G_MAXUINT)
			{
				DirItem* item = DIR_ITEM(g_list_model_get_item(G_LIST_MODEL(pFolderTreeImpl->m_pShortcutsStore), cursor_pos));
				if (item)
				{
					gboolean new_val = !item->checked;
					dir_item_set_checked(item, new_val);
					if (new_val)
						gtk_selection_model_select_item(sel, cursor_pos, FALSE);
					else
						gtk_selection_model_unselect_item(sel, cursor_pos);
					pFolderTreeImpl->SyncTreeSelectionForURI(item->uri, new_val);
					g_object_unref(item);
					pFolderTreeImpl->m_pFolderTree->EmitSelectionChangedEvent();
				}
			}
		}
		else if (sel_count > 1)
		{
			for (guint i = 0; i < n; i++)
			{
				DirItem* item = DIR_ITEM(g_list_model_get_item(G_LIST_MODEL(pFolderTreeImpl->m_pShortcutsStore), i));
				if (item)
				{
					gboolean is_sel = gtk_selection_model_is_selected(sel, i);
					dir_item_set_checked(item, is_sel);
					pFolderTreeImpl->SyncTreeSelectionForURI(item->uri, is_sel);
					g_object_unref(item);
				}
			}
			pFolderTreeImpl->m_pFolderTree->EmitSelectionChangedEvent();
		}
		else
		{
			if (cursor_pos != G_MAXUINT)
			{
				pFolderTreeImpl->ClearAllCheckboxes();
				DirItem* item = DIR_ITEM(g_list_model_get_item(G_LIST_MODEL(pFolderTreeImpl->m_pShortcutsStore), cursor_pos));
				if (item)
				{
					dir_item_set_checked(item, TRUE);
					gtk_selection_model_select_item(sel, cursor_pos, TRUE);
					pFolderTreeImpl->SyncTreeSelectionForURI(item->uri, TRUE);
					g_object_unref(item);
					pFolderTreeImpl->m_pFolderTree->EmitSelectionChangedEvent(false);
				}
			}
		}
		return TRUE;
	}

	return FALSE;
}

static void folder_tree_row_on_clicked(GtkGestureClick* gesture, int n_press, double x, double y)
{
	GtkWidget* w = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
	FolderTreeRowWidgets* rw = static_cast<FolderTreeRowWidgets*>(
		g_object_get_data(G_OBJECT(w), "row-widgets"));
	if (!rw || !rw->impl || !rw->bound_item) return;

	FolderTree::FolderTreeImpl* impl = rw->impl;
	DirItem* item = rw->bound_item;
	GtkTreeListRow* row = rw->bound_row;

	if (item->row_kind == ROW_KIND_SHORTCUT)
	{
		shortcut_row_on_clicked(gesture, n_press, x, y);
		return;
	}
	if (item->row_kind == ROW_KIND_BOOKMARK)
	{
		bookmark_row_on_clicked(gesture, n_press, x, y);
		return;
	}

	guint tree_pos = row ? gtk_tree_list_row_get_position(row) : G_MAXUINT;
	if (tree_pos != G_MAXUINT)
		impl->m_iFocusedTreePos = tree_pos;

	GtkListItem* li = rw->list_item;
	guint flat_pos = li ? gtk_list_item_get_position(li)
		: (tree_pos != G_MAXUINT ? impl->GetTreeFlattenOffset() + tree_pos : G_MAXUINT);

	GtkWidget* target = gtk_widget_pick(w, x, y, GTK_PICK_DEFAULT);

	// If click is on or within the checkbox, let the check button handle its own toggled event
	GtkWidget* check = rw->check;
	if (target != NULL && (GTK_IS_CHECK_BUTTON(target) || (check && (target == check || gtk_widget_is_ancestor(target, check)))))
	{
		return;
	}

	// If the click is on the expander arrow of an expandable row, let GtkTreeExpander handle it
	GtkWidget* expander = rw->expander;
	GtkWidget* row_box = rw->row_box;
	if (target != NULL && expander != NULL && (target == expander || gtk_widget_is_ancestor(target, expander)))
	{
		if (g_strcmp0(G_OBJECT_TYPE_NAME(target), "GtkBuiltinIcon") == 0 &&
		    row != NULL && GTK_IS_TREE_LIST_ROW(row) && gtk_tree_list_row_is_expandable(row))
		{
			if (!row_box || !gtk_widget_is_ancestor(target, row_box))
			{
				return;
			}
		}
	}
	else if (target != NULL && g_strcmp0(G_OBJECT_TYPE_NAME(target), "GtkBuiltinIcon") == 0 &&
	         row != NULL && GTK_IS_TREE_LIST_ROW(row) && gtk_tree_list_row_is_expandable(row))
	{
		return;
	}

	guint button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));
	if (button == 0)
		button = 1;
	GdkModifierType state = gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(gesture));

	if (button == 1)
	{
		if (state & GDK_CONTROL_MASK)
		{
			// Ctrl pressed: adds/subtracts (toggle)
			gboolean new_val = !item->checked;
			dir_item_set_checked(item, new_val);
			if (flat_pos != G_MAXUINT && impl->m_pSelectionModel)
			{
				if (new_val)
					gtk_selection_model_select_item(GTK_SELECTION_MODEL(impl->m_pSelectionModel), flat_pos, FALSE);
				else
					gtk_selection_model_unselect_item(GTK_SELECTION_MODEL(impl->m_pSelectionModel), flat_pos);
			}
			impl->SyncShortcutSelectionForURI(item->uri, new_val);
		}
		else if (state & GDK_SHIFT_MASK)
		{
			// Shift pressed: multi-selection range click
			guint start_tree = folder_tree_get_focused_position(impl);
			guint tree_off = impl->GetTreeFlattenOffset();
			guint start_flat = (start_tree != G_MAXUINT) ? (tree_off + start_tree) : flat_pos;
			if (start_flat == G_MAXUINT)
				start_flat = flat_pos;
			guint min_pos = std::min(start_flat, flat_pos);
			guint max_pos = std::max(start_flat, flat_pos);
			guint count = max_pos - min_pos + 1;
			if (flat_pos != G_MAXUINT && impl->m_pSelectionModel)
			{
				gtk_selection_model_select_range(GTK_SELECTION_MODEL(impl->m_pSelectionModel), min_pos, count, TRUE);
			}
		}
		else
		{
			// Plain click: check this item and uncheck all others
			impl->ClearAllCheckboxes();
			dir_item_set_checked(item, TRUE);
			if (flat_pos != G_MAXUINT && impl->m_pSelectionModel)
			{
				gtk_selection_model_select_item(GTK_SELECTION_MODEL(impl->m_pSelectionModel), flat_pos, TRUE);
			}
			impl->SyncShortcutSelectionForURI(item->uri, TRUE);
		}
		if (n_press == 2 && row && GTK_IS_TREE_LIST_ROW(row) && gtk_tree_list_row_is_expandable(row))
		{
			gtk_tree_list_row_set_expanded(row, !gtk_tree_list_row_get_expanded(row));
		}
		gtk_widget_grab_focus(w);
		// plain click: the other checkboxes were just cleared, so this is a
		// navigation and the rebuilt list starts at its first item
		impl->m_pFolderTree->EmitSelectionChangedEvent(false);
	}
	else if (button == 2)
	{
		// Middle click: toggle
		gboolean new_val = !item->checked;
		dir_item_set_checked(item, new_val);
		if (flat_pos != G_MAXUINT && impl->m_pSelectionModel)
		{
			if (new_val)
				gtk_selection_model_select_item(GTK_SELECTION_MODEL(impl->m_pSelectionModel), flat_pos, FALSE);
			else
				gtk_selection_model_unselect_item(GTK_SELECTION_MODEL(impl->m_pSelectionModel), flat_pos);
		}
		impl->SyncShortcutSelectionForURI(item->uri, new_val);
		gtk_widget_grab_focus(w);
		impl->m_pFolderTree->EmitSelectionChangedEvent();
	}
	else if (button == 3)
	{
		if (flat_pos != G_MAXUINT && impl->m_pSelectionModel &&
		    !gtk_selection_model_is_selected(GTK_SELECTION_MODEL(impl->m_pSelectionModel), flat_pos))
		{
			gtk_selection_model_select_item(GTK_SELECTION_MODEL(impl->m_pSelectionModel), flat_pos, TRUE);
		}
		view_popup_menu_at(w, x, y, TreePasteMode::PASTE_INTO_FOLDER, item->uri, impl);
	}
}

void FolderTree::FolderTreeImpl::CreateWidget()
{
	static bool s_css_initialized = false;
	if (!s_css_initialized)
	{
		s_css_initialized = true;
		GtkCssProvider* cssProvider = gtk_css_provider_new();
		gtk_css_provider_load_from_string(cssProvider,
			".sidebar, .quiver-sidebar {\n"
			"    background-color: @theme_bg_color;\n"
			"}\n"
			".quiver-sidebar listview, .quiver-sidebar listview.view {\n"
			"    background-color: transparent;\n"
			"}\n"
			".quiver-sidebar listview.navigation-sidebar row {\n"
			"    border-radius: 4px;\n"
			"    margin: 0 4px;\n"
			"    padding: 0;\n"
			"}\n"
			".quiver-sidebar listview.navigation-sidebar row:hover {\n"
			"    background-color: alpha(currentColor, 0.07);\n"
			"}\n"
			".quiver-sidebar listview.navigation-sidebar row:selected {\n"
			"    background-color: alpha(currentColor, 0.12);\n"
			"    color: inherit;\n"
			"}\n"
			".quiver-sidebar listview.navigation-sidebar row:selected:hover {\n"
			"    background-color: alpha(currentColor, 0.16);\n"
			"}\n"
			".sidebar-separator, .quiver-sidebar separator {\n"
			"    min-height: 1px;\n"
			"    background-color: alpha(currentColor, 0.15);\n"
			"    border: none;\n"
			"    box-shadow: none;\n"
			"    outline: none;\n"
			"}\n"
			".sidebar-row {\n"
			"    min-height: 24px;\n"
			"    padding: 1px 4px;\n"
			"    border-radius: 4px;\n"
			"}\n"
			".sidebar-section-title {\n"
			"    font-size: inherit;\n"
			"    font-weight: bold;\n"
			"    color: @theme_fg_color;\n"
			"}\n"
			".sidebar-expander-header {\n"
			"    min-height: 26px;\n"
			"    padding: 3px 6px;\n"
			"    border-radius: 4px;\n"
			"}\n"
			".sidebar-expander-header:hover {\n"
			"    background-color: alpha(currentColor, 0.06);\n"
			"}\n"
			"expander.sidebar-expander title {\n"
			"    min-height: 26px;\n"
			"    padding: 3px 6px;\n"
			"    border-radius: 4px;\n"
			"}\n"
			"expander.sidebar-expander title:hover {\n"
			"    background-color: alpha(currentColor, 0.06);\n"
			"}\n"
			"expander.sidebar-expander {\n"
			"    margin: 1px 0;\n"
			"}\n"
			".compact-tree row {\n"
			"    padding: 0;\n"
			"    margin: 0;\n"
			"    border-radius: 4px;\n"
			"    min-height: 22px;\n"
			"}\n"
			".compact-tree row:hover {\n"
			"    background-color: alpha(currentColor, 0.07);\n"
			"}\n"
			".compact-tree row:selected {\n"
			"    background-color: alpha(currentColor, 0.12);\n"
			"    color: inherit;\n"
			"}\n"
			".compact-tree row:selected:hover {\n"
			"    background-color: alpha(currentColor, 0.16);\n"
			"}\n"
			".compact-tree-row {\n"
			"    min-height: 22px;\n"
			"    padding: 0 4px;\n"
			"}\n"
			".sidebar-row.drop-hover,\n"
			".compact-tree-row.drop-hover {\n"
			"    outline: 2px solid #2ec27e;\n"
			"    outline-offset: -2px;\n"
			"    border-radius: 4px;\n"
			"    background-color: alpha(#2ec27e, 0.15);\n"
			"}\n"
		);
		gtk_style_context_add_provider_for_display(gdk_display_get_default(),
			GTK_STYLE_PROVIDER(cssProvider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
		g_object_unref(cssProvider);
	}

	// Restore expanded states from preferences before building the section models
	PreferencesPtr prefs = Preferences::GetInstance();
	if (prefs)
	{
		m_bShortcutsExpanded = prefs->GetBoolean(QUIVER_PREFS_BROWSER, "foldertree_expand_shortcuts", true);
		m_bBookmarksExpanded = prefs->GetBoolean(QUIVER_PREFS_BROWSER, "foldertree_expand_bookmarks", true);
		m_bFoldersExpanded = prefs->GetBoolean(QUIVER_PREFS_BROWSER, "foldertree_expand_folders", true);
	}

	// --- Shortcuts store & helper list view ---
	m_pShortcutsStore = g_list_store_new(DIR_ITEM_TYPE);
	PopulateShortcutsModel(m_pShortcutsStore);
	m_pShortcutsSelectionModel = gtk_multi_selection_new(G_LIST_MODEL(g_object_ref(m_pShortcutsStore)));
	m_pShortcutsListView = GTK_LIST_VIEW(g_object_ref_sink(
		gtk_list_view_new(GTK_SELECTION_MODEL(g_object_ref(m_pShortcutsSelectionModel)), NULL)));
	gtk_widget_add_css_class(GTK_WIDGET(m_pShortcutsListView), "navigation-sidebar");

	GtkEventController *sc_key_controller = gtk_event_controller_key_new();
	gtk_event_controller_set_propagation_phase(sc_key_controller, GTK_PHASE_CAPTURE);
	g_signal_connect(sc_key_controller, "key-pressed", G_CALLBACK(shortcuts_on_key_press), this);
	gtk_widget_add_controller(GTK_WIDGET(m_pShortcutsListView), sc_key_controller);

	GtkEventController *sc_legacy = gtk_event_controller_legacy_new();
	gtk_event_controller_set_propagation_phase(sc_legacy, GTK_PHASE_CAPTURE);
	g_signal_connect(sc_legacy, "event", G_CALLBACK(view_capture_button_press), this);
	gtk_widget_add_controller(GTK_WIDGET(m_pShortcutsListView), sc_legacy);

	GtkDropTarget *sc_drop = gtk_drop_target_new(G_TYPE_STRING,
		static_cast<GdkDragAction>(GDK_ACTION_COPY | GDK_ACTION_MOVE)); // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)
	gtk_drop_target_set_preload(sc_drop, TRUE);
	g_signal_connect(sc_drop, "motion",
		G_CALLBACK(shortcuts_drop_motion_cb), this);
	g_signal_connect(sc_drop, "notify::value",
		G_CALLBACK(shortcuts_drop_value_notify_cb), this);
	g_signal_connect(sc_drop, "leave",
		G_CALLBACK(shortcuts_drop_leave_cb), this);
	g_signal_connect(sc_drop, "drop",
		G_CALLBACK(shortcuts_drop_cb), this);
	gtk_widget_add_controller(GTK_WIDGET(m_pShortcutsListView),
		GTK_EVENT_CONTROLLER(sc_drop));

	// --- Bookmarks store & helper list view ---
	m_pBookmarkStore = g_list_store_new(DIR_ITEM_TYPE);
	PopulateBookmarksModel(m_pBookmarkStore);
	m_pBookmarkSelectionModel = gtk_multi_selection_new(G_LIST_MODEL(g_object_ref(m_pBookmarkStore)));
	m_pBookmarkListView = GTK_LIST_VIEW(g_object_ref_sink(
		gtk_list_view_new(GTK_SELECTION_MODEL(g_object_ref(m_pBookmarkSelectionModel)), NULL)));
	gtk_widget_add_css_class(GTK_WIDGET(m_pBookmarkListView), "navigation-sidebar");

	{
		GtkEventController *bm_legacy = gtk_event_controller_legacy_new();
		gtk_event_controller_set_propagation_phase(bm_legacy, GTK_PHASE_CAPTURE);
		g_signal_connect(bm_legacy, "event", G_CALLBACK(view_capture_button_press), this);
		gtk_widget_add_controller(GTK_WIDGET(m_pBookmarkListView), bm_legacy);
	}

	GtkDropTarget *bm_drop = gtk_drop_target_new(G_TYPE_STRING,
		static_cast<GdkDragAction>(GDK_ACTION_COPY | GDK_ACTION_MOVE)); // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)
	gtk_drop_target_set_preload(bm_drop, TRUE);
	g_signal_connect(bm_drop, "motion",
		G_CALLBACK(shortcuts_drop_motion_cb), this);
	g_signal_connect(bm_drop, "notify::value",
		G_CALLBACK(shortcuts_drop_value_notify_cb), this);
	g_signal_connect(bm_drop, "leave",
		G_CALLBACK(shortcuts_drop_leave_cb), this);
	g_signal_connect(bm_drop, "drop",
		G_CALLBACK(shortcuts_drop_cb), this);
	gtk_widget_add_controller(GTK_WIDGET(m_pBookmarkListView),
		GTK_EVENT_CONTROLLER(bm_drop));

	// --- Folder tree root model ---
	m_pListStoreRoots = g_list_store_new(DIR_ITEM_TYPE);
	PopulateTreeModel(m_pListStoreRoots);

	m_pTreeListModel = gtk_tree_list_model_new(
		G_LIST_MODEL(m_pListStoreRoots), FALSE, FALSE,
		create_subdirs, this, NULL);

	// --- Section header stores & unified GtkFlattenListModel ---
	m_pEmptyStore = g_list_store_new(DIR_ITEM_TYPE);

	m_pShortcutsHeaderStore = g_list_store_new(DIR_ITEM_TYPE);
	m_pShortcutsHeaderItem = dir_item_new(NULL, "Shortcuts", NULL, TRUE, 0, 0);
	m_pShortcutsHeaderItem->row_kind = ROW_KIND_SECTION_SHORTCUTS;
	m_pShortcutsHeaderItem->separator = FALSE;
	g_list_store_append(m_pShortcutsHeaderStore, G_OBJECT(m_pShortcutsHeaderItem));
	g_object_unref(m_pShortcutsHeaderItem);

	m_pBookmarksHeaderStore = g_list_store_new(DIR_ITEM_TYPE);
	m_pBookmarksHeaderItem = dir_item_new(NULL, "Bookmarks", NULL, TRUE, 0, 0);
	m_pBookmarksHeaderItem->row_kind = ROW_KIND_SECTION_BOOKMARKS;
	m_pBookmarksHeaderItem->separator = TRUE;
	g_list_store_append(m_pBookmarksHeaderStore, G_OBJECT(m_pBookmarksHeaderItem));
	g_object_unref(m_pBookmarksHeaderItem);

	m_pFoldersHeaderStore = g_list_store_new(DIR_ITEM_TYPE);
	m_pFoldersHeaderItem = dir_item_new(NULL, "Folders", NULL, TRUE, 0, 0);
	m_pFoldersHeaderItem->row_kind = ROW_KIND_SECTION_FOLDERS;
	m_pFoldersHeaderItem->separator = TRUE;
	g_list_store_append(m_pFoldersHeaderStore, G_OBJECT(m_pFoldersHeaderItem));
	g_object_unref(m_pFoldersHeaderItem);

	m_pSectionsStore = g_list_store_new(G_TYPE_LIST_MODEL);
	UpdateSectionsModel();

	m_pFlattenModel = gtk_flatten_list_model_new(G_LIST_MODEL(g_object_ref(m_pSectionsStore)));
	m_pSelectionModel = gtk_multi_selection_new(G_LIST_MODEL(g_object_ref(m_pFlattenModel)));


	// --- Unified list view factory (renders section headers, shortcuts, bookmarks, and folder tree rows) ---
	GtkListItemFactory* factory = gtk_signal_list_item_factory_new();

	g_signal_connect(factory, "setup", G_CALLBACK(+[](GtkListItemFactory* fact, GtkListItem* list_item, gpointer user_data) {
		(void)fact;
		FolderTree::FolderTreeImpl* impl =
			static_cast<FolderTree::FolderTreeImpl*>(user_data);

		FolderTreeRowWidgets* rw = new FolderTreeRowWidgets();
		rw->impl = impl;
		rw->list_item = list_item;

		rw->outer_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
		gtk_widget_set_hexpand(rw->outer_box, TRUE);
		gtk_widget_set_vexpand(rw->outer_box, TRUE);
		gtk_widget_set_halign(rw->outer_box, GTK_ALIGN_FILL);
		gtk_widget_set_valign(rw->outer_box, GTK_ALIGN_FILL);

		// Optional separator above Bookmarks / Folders section headers
		rw->sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
		gtk_widget_add_css_class(rw->sep, "sidebar-separator");
		gtk_widget_set_margin_top(rw->sep, 4);
		gtk_widget_set_margin_bottom(rw->sep, 4);
		gtk_widget_set_margin_start(rw->sep, 8);
		gtk_widget_set_margin_end(rw->sep, 8);
		gtk_widget_set_visible(rw->sep, FALSE);
		gtk_box_append(GTK_BOX(rw->outer_box), rw->sep);

		// Collapsible section header row (arrow + title label)
		rw->header_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
		gtk_widget_add_css_class(rw->header_box, "sidebar-expander-header");
		gtk_widget_set_hexpand(rw->header_box, TRUE);
		gtk_widget_set_halign(rw->header_box, GTK_ALIGN_FILL);
		gtk_widget_set_valign(rw->header_box, GTK_ALIGN_CENTER);
		gtk_widget_set_visible(rw->header_box, FALSE);

		rw->header_arrow = gtk_image_new_from_icon_name("pan-down-symbolic");
		gtk_widget_set_valign(rw->header_arrow, GTK_ALIGN_CENTER);
		rw->header_label = gtk_label_new(NULL);
		gtk_widget_add_css_class(rw->header_label, "sidebar-section-title");
		gtk_label_set_xalign(GTK_LABEL(rw->header_label), 0.0);
		gtk_widget_set_hexpand(rw->header_label, TRUE);
		gtk_widget_set_valign(rw->header_label, GTK_ALIGN_CENTER);
		gtk_box_append(GTK_BOX(rw->header_box), rw->header_arrow);
		gtk_box_append(GTK_BOX(rw->header_box), rw->header_label);

		GtkGesture* header_click = gtk_gesture_click_new();
		gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(header_click), 1);
		g_signal_connect(header_click, "pressed", G_CALLBACK(+[](GtkGestureClick* gesture, int, double, double, gpointer) {
			GtkWidget* hb = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
			FolderTree::FolderTreeImpl* impl = static_cast<FolderTree::FolderTreeImpl*>(
				g_object_get_data(G_OBJECT(hb), "dir-impl"));
			DirItem* sec_item = static_cast<DirItem*>(
				g_object_get_data(G_OBJECT(hb), "section-item"));
			if (!impl || !impl->m_pFolderTree || !sec_item)
				return;
			if (sec_item->row_kind == ROW_KIND_SECTION_SHORTCUTS)
				impl->m_pFolderTree->SetShortcutsExpanded(!impl->m_bShortcutsExpanded);
			else if (sec_item->row_kind == ROW_KIND_SECTION_BOOKMARKS)
				impl->m_pFolderTree->SetBookmarksExpanded(!impl->m_bBookmarksExpanded);
			else if (sec_item->row_kind == ROW_KIND_SECTION_FOLDERS)
				impl->m_pFolderTree->SetFoldersExpanded(!impl->m_bFoldersExpanded);
		}), NULL);
		gtk_widget_add_controller(rw->header_box, GTK_EVENT_CONTROLLER(header_click));
		gtk_box_append(GTK_BOX(rw->outer_box), rw->header_box);

		// Checkable item row: hbox [ check, expander[ icon, label ] ]
		rw->hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
		gtk_widget_add_css_class(rw->hbox, "compact-tree-row");
		gtk_widget_set_hexpand(rw->hbox, TRUE);
		gtk_widget_set_vexpand(rw->hbox, TRUE);
		gtk_widget_set_halign(rw->hbox, GTK_ALIGN_FILL);
		gtk_widget_set_valign(rw->hbox, GTK_ALIGN_FILL);
		gtk_widget_set_focusable(rw->hbox, TRUE);

		rw->check = gtk_check_button_new();
		gtk_widget_set_margin_start(rw->check, 4);
		gtk_widget_set_margin_end(rw->check, 6);
		gtk_widget_set_valign(rw->check, GTK_ALIGN_CENTER);
		g_signal_connect(rw->check, "toggled", G_CALLBACK(+[](GtkWidget* w, gpointer) {
			FolderTreeRowWidgets* rw = static_cast<FolderTreeRowWidgets*>(
				g_object_get_data(G_OBJECT(w), "row-widgets"));
			if (!rw || !rw->impl)
				return;
			FolderTree::FolderTreeImpl* impl = rw->impl;
			DirItem* item = rw->bound_item;
			GtkTreeListRow* row = rw->bound_row;
			GtkListItem* li = rw->list_item;
			guint flat_pos = li ? gtk_list_item_get_position(li) : G_MAXUINT;
			if (NULL != item && !g_object_get_data(G_OBJECT(w), "set-active-guard"))
			{
				gboolean active = gtk_check_button_get_active(GTK_CHECK_BUTTON(w));
				if (item->checked != active)
				{
					if (item->row_kind == ROW_KIND_SHORTCUT)
					{
						if (active && !QuiverUtils::CheckLocationExists(item->uri))
						{
							g_object_set_data(G_OBJECT(w), "set-active-guard", GINT_TO_POINTER(1));
							gtk_check_button_set_active(GTK_CHECK_BUTTON(w), FALSE);
							g_object_set_data(G_OBJECT(w), "set-active-guard", NULL);
							std::string msg = _("Location unavailable");
							if (item->display_name && item->display_name[0])
								msg += std::string(": ") + item->display_name;
							QuiverUtils::ShowHUDToast(msg.c_str(), "dialog-warning-symbolic", 4000);
							return;
						}
						dir_item_set_checked(item, active);
						guint sc_pos = rw->bound_sc_pos;
						if (impl->m_pShortcutsSelectionModel && sc_pos != G_MAXUINT)
						{
							if (active)
								gtk_selection_model_select_item(GTK_SELECTION_MODEL(impl->m_pShortcutsSelectionModel), sc_pos, FALSE);
							else
								gtk_selection_model_unselect_item(GTK_SELECTION_MODEL(impl->m_pShortcutsSelectionModel), sc_pos);
						}
						if (impl->m_pSelectionModel && flat_pos != G_MAXUINT)
						{
							if (active)
								gtk_selection_model_select_item(GTK_SELECTION_MODEL(impl->m_pSelectionModel), flat_pos, FALSE);
							else
								gtk_selection_model_unselect_item(GTK_SELECTION_MODEL(impl->m_pSelectionModel), flat_pos);
						}
						impl->SyncTreeSelectionForURI(item->uri, active);
						impl->m_pFolderTree->EmitSelectionChangedEvent();
					}
					else if (item->row_kind == ROW_KIND_BOOKMARK)
					{
						guint bm_pos = rw->bound_bm_pos;
						const Bookmark* bm = NULL;
						if (item->bookmark_id >= 0)
						{
							BookmarksPtr bmPtr = Bookmarks::GetInstance();
							bm = bmPtr->GetBookmark(item->bookmark_id);
						}
						if (active)
						{
							std::string unavail_name;
							bool avail = bm ? QuiverUtils::CheckBookmarkLocationAvailable(*bm, &unavail_name)
							                : QuiverUtils::CheckLocationExists(item->uri);
							if (!avail)
							{
								g_object_set_data(G_OBJECT(w), "set-active-guard", GINT_TO_POINTER(1));
								gtk_check_button_set_active(GTK_CHECK_BUTTON(w), FALSE);
								g_object_set_data(G_OBJECT(w), "set-active-guard", NULL);
								std::string msg = _("Location unavailable");
								if (!unavail_name.empty())
									msg += ": " + unavail_name;
								else if (item->display_name && item->display_name[0])
									msg += std::string(": ") + item->display_name;
								QuiverUtils::ShowHUDToast(msg.c_str(), "dialog-warning-symbolic", 4000);
								return;
							}
						}
						dir_item_set_checked(item, active);
						if (impl->m_pBookmarkSelectionModel && bm_pos != G_MAXUINT)
						{
							if (active)
								gtk_selection_model_select_item(GTK_SELECTION_MODEL(impl->m_pBookmarkSelectionModel), bm_pos, FALSE);
							else
								gtk_selection_model_unselect_item(GTK_SELECTION_MODEL(impl->m_pBookmarkSelectionModel), bm_pos);
						}
						if (impl->m_pSelectionModel && flat_pos != G_MAXUINT)
						{
							if (active)
								gtk_selection_model_select_item(GTK_SELECTION_MODEL(impl->m_pSelectionModel), flat_pos, FALSE);
							else
								gtk_selection_model_unselect_item(GTK_SELECTION_MODEL(impl->m_pSelectionModel), flat_pos);
						}
						if (NULL != bm)
						{
							std::list<std::string> uris = bm->GetURIs();
							for (const auto& uri : uris)
							{
								impl->SyncTreeSelectionForURI(uri.c_str(), active);
								impl->SyncShortcutSelectionForURI(uri.c_str(), active);
							}
						}
						else
						{
							impl->SyncTreeSelectionForURI(item->uri, active);
							if (active)
								impl->SyncShortcutSelectionForURI(item->uri, TRUE);
						}
						gtk_widget_grab_focus(w);
						impl->m_pFolderTree->EmitSelectionChangedEvent();
					}
					else
					{
						dir_item_set_checked(item, active);
						if (flat_pos == G_MAXUINT && row)
							flat_pos = impl->GetTreeFlattenOffset() + gtk_tree_list_row_get_position(row);
						if (flat_pos != G_MAXUINT && impl->m_pSelectionModel)
						{
							if (active)
								gtk_selection_model_select_item(GTK_SELECTION_MODEL(impl->m_pSelectionModel), flat_pos, FALSE);
							else
								gtk_selection_model_unselect_item(GTK_SELECTION_MODEL(impl->m_pSelectionModel), flat_pos);
						}
						impl->SyncShortcutSelectionForURI(item->uri, active);
						impl->m_pFolderTree->EmitSelectionChangedEvent();
					}
				}
			}
		}), NULL);
		gtk_box_append(GTK_BOX(rw->hbox), rw->check);

		rw->expander = gtk_tree_expander_new();
		gtk_tree_expander_set_indent_for_icon(GTK_TREE_EXPANDER(rw->expander), TRUE);
		gtk_tree_expander_set_hide_expander(GTK_TREE_EXPANDER(rw->expander), FALSE);
		gtk_widget_set_hexpand(rw->expander, TRUE);
		gtk_widget_set_vexpand(rw->expander, TRUE);
		gtk_widget_set_halign(rw->expander, GTK_ALIGN_FILL);
		gtk_widget_set_valign(rw->expander, GTK_ALIGN_FILL);
		rw->row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
		gtk_widget_set_hexpand(rw->row_box, TRUE);
		gtk_widget_set_vexpand(rw->row_box, TRUE);
		gtk_widget_set_halign(rw->row_box, GTK_ALIGN_FILL);
		gtk_widget_set_valign(rw->row_box, GTK_ALIGN_FILL);
		rw->image = gtk_image_new();
		gtk_image_set_icon_size(GTK_IMAGE(rw->image), GTK_ICON_SIZE_NORMAL);
		gtk_widget_set_valign(rw->image, GTK_ALIGN_CENTER);
		rw->label = gtk_label_new(NULL);
		gtk_label_set_xalign(GTK_LABEL(rw->label), 0.0);
		gtk_label_set_ellipsize(GTK_LABEL(rw->label), PANGO_ELLIPSIZE_END);
		gtk_widget_set_hexpand(rw->label, TRUE);
		gtk_widget_set_valign(rw->label, GTK_ALIGN_CENTER);
		gtk_box_append(GTK_BOX(rw->row_box), rw->image);
		gtk_box_append(GTK_BOX(rw->row_box), rw->label);
		gtk_tree_expander_set_child(GTK_TREE_EXPANDER(rw->expander), rw->row_box);
		gtk_box_append(GTK_BOX(rw->hbox), rw->expander);

		GtkGesture* row_click = gtk_gesture_click_new();
		gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(row_click), 0);
		g_signal_connect(row_click, "pressed", G_CALLBACK(+[](GtkGestureClick* gesture, int n_press, double x, double y, gpointer) {
			g_object_set_data(G_OBJECT(gesture), "press-handled", GINT_TO_POINTER(1));
			folder_tree_row_on_clicked(gesture, n_press, x, y);
		}), NULL);
		g_signal_connect(row_click, "released", G_CALLBACK(+[](GtkGestureClick* gesture, int n_press, double x, double y, gpointer) {
			if (GPOINTER_TO_INT(g_object_get_data(G_OBJECT(gesture), "press-handled")))
			{
				g_object_set_data(G_OBJECT(gesture), "press-handled", GINT_TO_POINTER(0));
				return;
			}
			folder_tree_row_on_clicked(gesture, n_press, x, y);
		}), NULL);
		gtk_widget_add_controller(rw->hbox, GTK_EVENT_CONTROLLER(row_click));
		gtk_box_append(GTK_BOX(rw->outer_box), rw->hbox);

		gtk_list_item_set_child(list_item, rw->outer_box);

		// Store row-widgets struct on list_item (destroyed automatically when list_item is finalized)
		g_object_set_data_full(G_OBJECT(list_item), "row-widgets", rw, [](gpointer d) {
			delete static_cast<FolderTreeRowWidgets*>(d);
		});

		// Attach pointers once during setup
		g_object_set_data(G_OBJECT(rw->outer_box), "row-widgets", rw);
		g_object_set_data(G_OBJECT(rw->outer_box), "list-item", list_item);
		g_object_set_data(G_OBJECT(rw->outer_box), "dir-impl", impl);
		g_object_set_data(G_OBJECT(rw->outer_box), "f-expander", rw->expander);
		g_object_set_data(G_OBJECT(rw->outer_box), "f-check", rw->check);
		g_object_set_data(G_OBJECT(rw->outer_box), "f-hbox", rw->hbox);

		g_object_set_data(G_OBJECT(rw->header_box), "dir-impl", impl);

		g_object_set_data(G_OBJECT(rw->hbox), "row-widgets", rw);
		g_object_set_data(G_OBJECT(rw->hbox), "list-item", list_item);
		g_object_set_data(G_OBJECT(rw->hbox), "dir-impl", impl);
		g_object_set_data(G_OBJECT(rw->hbox), "sc-impl", impl);
		g_object_set_data(G_OBJECT(rw->hbox), "bm-impl", impl);
		g_object_set_data(G_OBJECT(rw->hbox), "f-expander", rw->expander);
		g_object_set_data(G_OBJECT(rw->hbox), "f-check", rw->check);
		g_object_set_data(G_OBJECT(rw->hbox), "f-row-box", rw->row_box);

		g_object_set_data(G_OBJECT(rw->check), "row-widgets", rw);
		g_object_set_data(G_OBJECT(rw->check), "list-item", list_item);
		g_object_set_data(G_OBJECT(rw->check), "dir-impl", impl);

		g_object_set_data(G_OBJECT(rw->expander), "row-widgets", rw);
		g_object_set_data(G_OBJECT(rw->expander), "dir-impl", impl);
	}), this);

	g_signal_connect(factory, "bind", G_CALLBACK((+[](GtkListItemFactory*, GtkListItem* list_item, gpointer user_data) {
		FolderTree::FolderTreeImpl* impl =
			static_cast<FolderTree::FolderTreeImpl*>(user_data);

		FolderTreeRowWidgets* rw = static_cast<FolderTreeRowWidgets*>(
			g_object_get_data(G_OBJECT(list_item), "row-widgets"));
		if (!rw) return;

		GObject* raw_item = G_OBJECT(gtk_list_item_get_item(list_item));
		guint flat_pos = gtk_list_item_get_position(list_item);

		GtkTreeListRow* row = NULL;
		DirItem* item = NULL;
		bool item_needs_unref = false;
		if (GTK_IS_TREE_LIST_ROW(raw_item))
		{
			row = GTK_TREE_LIST_ROW(raw_item);
			item = DIR_ITEM(gtk_tree_list_row_get_item(row));
			item_needs_unref = true;
		}
		else if (raw_item && G_TYPE_CHECK_INSTANCE_TYPE(raw_item, DIR_ITEM_TYPE))
		{
			item = DIR_ITEM(raw_item);
		}

		if (!item)
			return;

		rw->bound_item = item;

		// Section header row
		if (item->row_kind == ROW_KIND_SECTION_SHORTCUTS ||
		    item->row_kind == ROW_KIND_SECTION_BOOKMARKS ||
		    item->row_kind == ROW_KIND_SECTION_FOLDERS)
		{
			rw->is_header_row = true;
			gtk_widget_set_visible(rw->sep, item->separator);
			gtk_widget_set_visible(rw->header_box, TRUE);
			gtk_widget_set_visible(rw->hbox, FALSE);
			gtk_label_set_text(GTK_LABEL(rw->header_label), item->display_name ? item->display_name : "");
			bool expanded = true;
			if (impl)
			{
				if (item->row_kind == ROW_KIND_SECTION_SHORTCUTS)
					expanded = impl->m_bShortcutsExpanded;
				else if (item->row_kind == ROW_KIND_SECTION_BOOKMARKS)
					expanded = impl->m_bBookmarksExpanded;
				else if (item->row_kind == ROW_KIND_SECTION_FOLDERS)
					expanded = impl->m_bFoldersExpanded;
			}
			gtk_image_set_from_icon_name(GTK_IMAGE(rw->header_arrow),
				expanded ? "pan-down-symbolic" : "pan-end-symbolic");
			g_object_set_data(G_OBJECT(rw->header_box), "section-item", item);
			g_object_set_data(G_OBJECT(item), "bound-arrow", rw->header_arrow);
			if (item_needs_unref)
				g_object_unref(item);
			return;
		}

		// Item row (Shortcut, Bookmark, or Folder)
		if (rw->is_header_row)
		{
			rw->is_header_row = false;
			gtk_widget_set_visible(rw->sep, FALSE);
			gtk_widget_set_visible(rw->header_box, FALSE);
			gtk_widget_set_visible(rw->hbox, TRUE);
		}

		const char* target_icon = (NULL != item->icon_name && '\0' != item->icon_name[0])
			? item->icon_name : ((row != NULL) ? "folder" : "folder-symbolic");
		if (rw->bound_icon_name != target_icon)
		{
			gtk_image_set_from_icon_name(GTK_IMAGE(rw->image), target_icon);
			rw->bound_icon_name = target_icon;
		}

		const char* target_text = item->display_name ? item->display_name : "";
		if (rw->bound_text != target_text)
		{
			gtk_label_set_text(GTK_LABEL(rw->label), target_text);
			rw->bound_text = target_text;
		}

		guint sc_pos = G_MAXUINT;
		guint bm_pos = G_MAXUINT;

		if (item->row_kind == ROW_KIND_SHORTCUT)
		{
			guint sc_off = impl ? impl->GetSlotFlattenOffset(1) : 1;
			sc_pos = (flat_pos >= sc_off) ? (flat_pos - sc_off) : 0;
			if (!rw->is_sidebar_row)
			{
				gtk_widget_remove_css_class(rw->hbox, "compact-tree-row");
				gtk_widget_add_css_class(rw->hbox, "sidebar-row");
				rw->is_sidebar_row = true;
				gtk_tree_expander_set_hide_expander(GTK_TREE_EXPANDER(rw->expander), TRUE);
				gtk_tree_expander_set_indent_for_icon(GTK_TREE_EXPANDER(rw->expander), FALSE);
			}
			if (rw->bound_row != NULL)
			{
				gtk_tree_expander_set_list_row(GTK_TREE_EXPANDER(rw->expander), NULL);
				rw->bound_row = NULL;
			}
		}
		else if (item->row_kind == ROW_KIND_BOOKMARK)
		{
			guint bm_off = impl ? impl->GetSlotFlattenOffset(3) : 0;
			bm_pos = (flat_pos >= bm_off) ? (flat_pos - bm_off) : 0;
			if (!rw->is_sidebar_row)
			{
				gtk_widget_remove_css_class(rw->hbox, "compact-tree-row");
				gtk_widget_add_css_class(rw->hbox, "sidebar-row");
				rw->is_sidebar_row = true;
				gtk_tree_expander_set_hide_expander(GTK_TREE_EXPANDER(rw->expander), TRUE);
				gtk_tree_expander_set_indent_for_icon(GTK_TREE_EXPANDER(rw->expander), FALSE);
			}
			if (rw->bound_row != NULL)
			{
				gtk_tree_expander_set_list_row(GTK_TREE_EXPANDER(rw->expander), NULL);
				rw->bound_row = NULL;
			}
		}
		else
		{
			if (rw->is_sidebar_row)
			{
				gtk_widget_remove_css_class(rw->hbox, "sidebar-row");
				gtk_widget_add_css_class(rw->hbox, "compact-tree-row");
				rw->is_sidebar_row = false;
				gtk_tree_expander_set_indent_for_icon(GTK_TREE_EXPANDER(rw->expander), TRUE);
				gtk_tree_expander_set_hide_expander(GTK_TREE_EXPANDER(rw->expander), FALSE);
			}
			// Only update the list row if it differs to avoid re-layout churn
			if (rw->bound_row != row)
			{
				gtk_tree_expander_set_list_row(GTK_TREE_EXPANDER(rw->expander), row);
				rw->bound_row = row;
			}
		}

		rw->bound_sc_pos = sc_pos;
		rw->bound_bm_pos = bm_pos;

		g_object_set_data(G_OBJECT(rw->hbox), "dir-item", item);
		g_object_set_data(G_OBJECT(item), "bound-check", rw->check);

		if (!g_object_get_data(G_OBJECT(item), "checked-connected"))
		{
			g_object_set_data(G_OBJECT(item), "checked-connected", GINT_TO_POINTER(1));
			g_signal_connect(item, "notify::checked", G_CALLBACK(+[](GObject* obj, GParamSpec* ps, gpointer user_data) {
				(void)ps; (void)user_data;
				DirItem* it = DIR_ITEM(obj);
				GtkWidget* cb = GTK_WIDGET(g_object_get_data(G_OBJECT(it), "bound-check"));
				if (NULL == cb)
					return;
				g_object_set_data(G_OBJECT(cb), "set-active-guard", GINT_TO_POINTER(1));
				gtk_check_button_set_active(GTK_CHECK_BUTTON(cb), it->checked);
				g_object_set_data(G_OBJECT(cb), "set-active-guard", GINT_TO_POINTER(0));
			}), NULL);
		}

		gboolean cur_active = gtk_check_button_get_active(GTK_CHECK_BUTTON(rw->check));
		if (cur_active != item->checked)
		{
			g_object_set_data(G_OBJECT(rw->check), "set-active-guard", GINT_TO_POINTER(1));
			gtk_check_button_set_active(GTK_CHECK_BUTTON(rw->check), item->checked);
			g_object_set_data(G_OBJECT(rw->check), "set-active-guard", GINT_TO_POINTER(0));
		}

		if (NULL != impl && impl->m_pDropHoverItem == item)
		{
			gtk_widget_add_css_class(rw->hbox, "drop-hover");
			impl->m_pDropHover = rw->hbox;
		}

		if (item_needs_unref)
			g_object_unref(item);
	})), this);

	g_signal_connect(factory, "unbind", G_CALLBACK((+[](GtkListItemFactory*, GtkListItem* list_item, gpointer) {
		FolderTreeRowWidgets* rw = static_cast<FolderTreeRowWidgets*>(
			g_object_get_data(G_OBJECT(list_item), "row-widgets"));
		if (!rw) return;

		if (rw->bound_item)
		{
			if (g_object_get_data(G_OBJECT(rw->bound_item), "bound-check") == rw->check)
				g_object_set_data(G_OBJECT(rw->bound_item), "bound-check", NULL);
			if (g_object_get_data(G_OBJECT(rw->bound_item), "bound-arrow") == rw->header_arrow)
				g_object_set_data(G_OBJECT(rw->bound_item), "bound-arrow", NULL);
			rw->bound_item = NULL;
		}
		if (rw->impl && rw->impl->m_pDropHover == rw->hbox)
		{
			rw->impl->m_pDropHover = NULL;
			gtk_widget_remove_css_class(rw->hbox, "drop-hover");
		}
		g_object_set_data(G_OBJECT(rw->hbox), "dir-item", NULL);
	})), this);

	m_pListView = GTK_LIST_VIEW(gtk_list_view_new(GTK_SELECTION_MODEL(m_pSelectionModel), factory));

	GtkEventController *listview_legacy = gtk_event_controller_legacy_new();
	gtk_event_controller_set_propagation_phase(listview_legacy, GTK_PHASE_CAPTURE);
	g_signal_connect(listview_legacy, "event", G_CALLBACK(view_capture_button_press), this);
	gtk_widget_add_controller(GTK_WIDGET(m_pListView), listview_legacy);

	GtkDropTarget *tree_drop = gtk_drop_target_new(G_TYPE_STRING,
		static_cast<GdkDragAction>(GDK_ACTION_COPY | GDK_ACTION_MOVE)); // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)
	gtk_drop_target_set_preload(tree_drop, TRUE);
	g_signal_connect(tree_drop, "motion",
		G_CALLBACK(folder_tree_drop_motion_cb), this);
	g_signal_connect(tree_drop, "notify::value",
		G_CALLBACK(folder_tree_drop_value_notify_cb), this);
	g_signal_connect(tree_drop, "leave",
		G_CALLBACK(folder_tree_drop_leave_cb), this);
	g_signal_connect(tree_drop, "drop",
		G_CALLBACK(folder_tree_drop_cb), this);
	gtk_widget_add_controller(GTK_WIDGET(m_pListView),
		GTK_EVENT_CONTROLLER(tree_drop));

	GtkEventController *key_controller = gtk_event_controller_key_new();
	gtk_event_controller_set_propagation_phase(key_controller, GTK_PHASE_CAPTURE);
	g_signal_connect(key_controller, "key-pressed", G_CALLBACK(view_on_key_press), this);
	gtk_widget_add_controller(GTK_WIDGET(m_pListView), key_controller);

	// Assemble m_pWidget box with m_pListView directly inside m_pMainScrolledWindow
	// so GTK4 natively virtualizes all sections and rows with O(1) realized widgets.
	m_pWidget = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_add_css_class(m_pWidget, "sidebar");
	gtk_widget_add_css_class(m_pWidget, "quiver-sidebar");
	gtk_widget_add_css_class(GTK_WIDGET(m_pListView), "compact-tree");
	gtk_widget_add_css_class(GTK_WIDGET(m_pListView), "navigation-sidebar");

	m_pMainScrolledWindow = gtk_scrolled_window_new();
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(m_pMainScrolledWindow),
		GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_kinetic_scrolling(GTK_SCROLLED_WINDOW(m_pMainScrolledWindow), TRUE);
	gtk_widget_set_vexpand(m_pMainScrolledWindow, TRUE);
	gtk_widget_set_hexpand(m_pMainScrolledWindow, TRUE);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(m_pMainScrolledWindow), GTK_WIDGET(m_pListView));
	gtk_box_append(GTK_BOX(m_pWidget), m_pMainScrolledWindow);

	GtkEventController *scroll_ctrl = gtk_event_controller_scroll_new(
		GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
	gtk_event_controller_set_propagation_phase(scroll_ctrl, GTK_PHASE_CAPTURE);
	g_signal_connect(scroll_ctrl, "scroll", G_CALLBACK(+[](GtkEventControllerScroll* ctrl, double dx, double dy, gpointer user_data) -> gboolean {
		(void)dx;
		FolderTree::FolderTreeImpl* impl = static_cast<FolderTree::FolderTreeImpl*>(user_data);
		if (!impl || !impl->m_pMainScrolledWindow)
			return GDK_EVENT_PROPAGATE;
		GtkAdjustment* vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(impl->m_pMainScrolledWindow));
		if (!vadj)
			return GDK_EVENT_PROPAGATE;
		GdkScrollUnit unit = gtk_event_controller_scroll_get_unit(ctrl);
		double page_size = gtk_adjustment_get_page_size(vadj);
		double upper = gtk_adjustment_get_upper(vadj);
		double cur_val = gtk_adjustment_get_value(vadj);
		double max_val = std::max(0.0, upper - page_size);
		if (unit == GDK_SCROLL_UNIT_WHEEL)
		{
			double step = std::max(75.0, gtk_adjustment_get_step_increment(vadj) * 3.0);
			double new_val = std::clamp(cur_val + dy * step, 0.0, max_val);
			if (std::abs(new_val - cur_val) > 0.5)
			{
				gtk_adjustment_set_value(vadj, new_val);
				return GDK_EVENT_STOP;
			}
		}
		return GDK_EVENT_PROPAGATE;
	}), this);
	gtk_widget_add_controller(m_pMainScrolledWindow, scroll_ctrl);

	// build the right-click / menu context popover
	{
		GMenu *menu = g_menu_new();
		m_pMenuPopover = gtk_popover_menu_new_from_model(G_MENU_MODEL(menu));
		g_object_unref(menu);

		/* FolderTree actions live in the shared action group.  They are
		 * registered here so the standard menu-model items can drive them. */
		if (NULL == QuiverUtils::GetAction("FolderTreeCheckSelected"))
		{
			QuiverUtils::AddSimpleAction("FolderTreeCheckSelected", NULL,
				(void(*)(GSimpleAction*, GVariant*, gpointer))(
				+[](GSimpleAction*, GVariant*, gpointer ud) {
					folder_tree_set_checkbox_for_selected(
						static_cast<FolderTree::FolderTreeImpl*>(ud), TRUE);
				}), this);
			QuiverUtils::AddSimpleAction("FolderTreeUncheckSelected", NULL,
				(void(*)(GSimpleAction*, GVariant*, gpointer))(
				+[](GSimpleAction*, GVariant*, gpointer ud) {
					folder_tree_set_checkbox_for_selected(
						static_cast<FolderTree::FolderTreeImpl*>(ud), FALSE);
				}), this);
		}
		if (NULL == QuiverUtils::GetAction("FolderTreePasteInto"))
		{
			GSimpleAction *a = g_simple_action_new("FolderTreePasteInto", G_VARIANT_TYPE_STRING);
			g_signal_connect(a, "activate", G_CALLBACK(folder_tree_action_paste_into), this);
			QuiverUtils::AddAction(G_ACTION(a));
		}
		if (NULL == QuiverUtils::GetAction("FolderTreeNewFolder"))
		{
			GSimpleAction *a = g_simple_action_new("FolderTreeNewFolder", G_VARIANT_TYPE_STRING);
			g_signal_connect(a, "activate", G_CALLBACK(folder_tree_action_new_folder), this);
			QuiverUtils::AddAction(G_ACTION(a));
		}
		if (NULL == QuiverUtils::GetAction("FolderTreeAddBookmark"))
		{
			GSimpleAction *a = g_simple_action_new("FolderTreeAddBookmark", G_VARIANT_TYPE_STRING);
			g_signal_connect(a, "activate", G_CALLBACK(folder_tree_action_add_bookmark), this);
			QuiverUtils::AddAction(G_ACTION(a));
		}
		if (NULL == QuiverUtils::GetAction("FolderTreeEditBookmark"))
		{
			GSimpleAction *a = g_simple_action_new("FolderTreeEditBookmark", G_VARIANT_TYPE_INT32);
			g_signal_connect(a, "activate", G_CALLBACK(folder_tree_action_edit_bookmark), this);
			QuiverUtils::AddAction(G_ACTION(a));
		}
	}
	gtk_widget_set_parent(m_pMenuPopover, m_pWidget);
	g_signal_connect(m_pWidget, "destroy",
		G_CALLBACK(+[](GtkWidget*, gpointer user_data) {
			FolderTree::FolderTreeImpl* t = static_cast<FolderTree::FolderTreeImpl*>(user_data);
			if (NULL != t->m_pMenuPopover && GTK_IS_WIDGET(t->m_pMenuPopover) &&
					gtk_widget_get_parent(t->m_pMenuPopover) != NULL)
			{
				gtk_widget_unparent(t->m_pMenuPopover);
			}
			t->m_pMenuPopover = NULL;
		}), this);
}

static void folder_tree_set_checkbox_for_selected(FolderTree::FolderTreeImpl* impl, gboolean value)
{
	if (impl->m_pSelectionModel && impl->m_pFlattenModel)
	{
		GtkSelectionModel* sel = GTK_SELECTION_MODEL(impl->m_pSelectionModel);
		guint n = g_list_model_get_n_items(G_LIST_MODEL(impl->m_pFlattenModel));
		for (guint i = 0; i < n; i++)
		{
			if (!gtk_selection_model_is_selected(sel, i))
				continue;
			GObject* obj = G_OBJECT(g_list_model_get_item(G_LIST_MODEL(impl->m_pFlattenModel), i));
			if (!obj)
				continue;
			if (GTK_IS_TREE_LIST_ROW(obj))
			{
				GtkTreeListRow* row = GTK_TREE_LIST_ROW(obj);
				DirItem* item = DIR_ITEM(gtk_tree_list_row_get_item(row));
				if (item)
				{
					dir_item_set_checked(item, value);
					impl->SyncShortcutSelectionForURI(item->uri, value);
					g_object_unref(item);
				}
			}
			else if (G_TYPE_CHECK_INSTANCE_TYPE(obj, DIR_ITEM_TYPE))
			{
				DirItem* item = DIR_ITEM(obj);
				dir_item_set_checked(item, value);
				if (item->bookmark_id >= 0)
				{
					BookmarksPtr bmPtr = Bookmarks::GetInstance();
					if (bmPtr)
					{
						const Bookmark* bm = bmPtr->GetBookmark(item->bookmark_id);
						if (bm)
						{
							for (const auto& u : bm->GetURIs())
								impl->SyncTreeSelectionForURI(u.c_str(), value);
						}
					}
				}
				else if (item->uri)
				{
					impl->SyncTreeSelectionForURI(item->uri, value);
				}
			}
			g_object_unref(obj);
		}
	}
	if (impl->m_pShortcutsSelectionModel && impl->m_pShortcutsStore)
	{
		GtkSelectionModel* sc_sel = GTK_SELECTION_MODEL(impl->m_pShortcutsSelectionModel);
		guint sc_n = g_list_model_get_n_items(G_LIST_MODEL(impl->m_pShortcutsStore));
		for (guint i = 0; i < sc_n; i++)
		{
			if (gtk_selection_model_is_selected(sc_sel, i))
			{
				DirItem* item = DIR_ITEM(g_list_model_get_item(G_LIST_MODEL(impl->m_pShortcutsStore), i));
				if (item)
				{
					dir_item_set_checked(item, value);
					impl->SyncTreeSelectionForURI(item->uri, value);
					g_object_unref(item);
				}
			}
		}
	}
	if (impl->m_pBookmarkSelectionModel && impl->m_pBookmarkStore)
	{
		GtkSelectionModel* bm_sel = GTK_SELECTION_MODEL(impl->m_pBookmarkSelectionModel);
		guint bm_n = g_list_model_get_n_items(G_LIST_MODEL(impl->m_pBookmarkStore));
		for (guint i = 0; i < bm_n; i++)
		{
			if (gtk_selection_model_is_selected(bm_sel, i))
			{
				DirItem* item = DIR_ITEM(g_list_model_get_item(G_LIST_MODEL(impl->m_pBookmarkStore), i));
				if (item)
				{
					dir_item_set_checked(item, value);
					if (item->bookmark_id >= 0)
					{
						BookmarksPtr bmPtr = Bookmarks::GetInstance();
						if (bmPtr)
						{
							const Bookmark* bm = bmPtr->GetBookmark(item->bookmark_id);
							if (bm)
							{
								for (const auto& u : bm->GetURIs())
									impl->SyncTreeSelectionForURI(u.c_str(), value);
							}
						}
					}
					else if (item->uri)
					{
						impl->SyncTreeSelectionForURI(item->uri, value);
					}
					g_object_unref(item);
				}
			}
		}
	}
	impl->m_pFolderTree->EmitSelectionChangedEvent();
}

/* Folder-tree drag & drop: hover highlight + drop-into-folder.  Tree rows are
 * built by the list-item factory, which stamps "dir-item"/"list-item" on the
 * row hbox at bind time, so the hovered folder is found by picking the widget
 * under the pointer and walking up to that hbox. */
/* Paste / drop conflict resolution: prompt once per collision, remember the
 * choice when the user ticks "do this for all remaining conflicts". */
struct FolderTreePasteConflictUI
{
	bool bApplyToAll = false;
	QuiverFileOps::PasteConflictAction lastAction = QuiverFileOps::PASTE_SKIP;
};

static QuiverFileOps::PasteConflictAction folder_tree_paste_conflict_cb(
	const gchar *src_uri, const gchar *dest_uri, gpointer user_data)
{
	FolderTreePasteConflictUI *ui = (FolderTreePasteConflictUI*)user_data;
	if (ui->bApplyToAll)
		return ui->lastAction;
	QuiverFileOps::PasteConflictAction action =
		QuiverUtils::ResolvePasteConflict(src_uri, dest_uri, ui->bApplyToAll);
	ui->lastAction = action;
	return action;
}
static void
folder_tree_clear_drop_hover(FolderTree::FolderTreeImpl *impl)
{
	if (NULL != impl && NULL != impl->m_pDropHover)
	{
		gtk_widget_remove_css_class(impl->m_pDropHover, "drop-hover");
		impl->m_pDropHover = NULL;
	}
}

static GtkWidget*
folder_tree_row_hbox_at_pos(FolderTree::FolderTreeImpl *impl, gdouble x, gdouble y, DirItem **item_out, GtkTreeListRow **row_out)
{
	if (NULL != item_out)
		*item_out = NULL;
	if (NULL != row_out)
		*row_out = NULL;
	if (NULL == impl || NULL == impl->m_pListView)
		return NULL;
	GtkWidget *w = gtk_widget_pick(GTK_WIDGET(impl->m_pListView), x, y, GTK_PICK_DEFAULT);
	while (NULL != w && w != GTK_WIDGET(impl->m_pListView))
	{
		if (NULL != g_object_get_data(G_OBJECT(w), "list-item"))
		{
			FolderTreeRowWidgets* rw = static_cast<FolderTreeRowWidgets*>(
				g_object_get_data(G_OBJECT(w), "row-widgets"));
			DirItem *item = rw ? rw->bound_item : nullptr;
			GtkTreeListRow *row = rw ? rw->bound_row : nullptr;
			if (NULL != item && NULL != item->uri &&
			    (g_strcmp0(item->uri, "trash:///") == 0 || folder_tree_uri_is_directory(item->uri)))
			{
				if (NULL != item_out)
					*item_out = item;
				if (NULL != row_out)
					*row_out = row;
				return w;
			}
			return NULL;
		}
		w = gtk_widget_get_parent(w);
	}
	return NULL;
}

/* Spring-loaded folders: hovering a collapsed folder for a moment expands
 * it so the drag can continue deeper.  The row is referenced while armed so
 * a tree rebuild mid-hover cannot leave a dangling pointer. */
#define FOLDER_TREE_DROP_EXPAND_DELAY_MS 1000
static gboolean
folder_tree_drop_expand_timeout(gpointer user_data)
{
	FolderTree::FolderTreeImpl *impl = (FolderTree::FolderTreeImpl*)user_data;
	impl->m_DropExpandTimer = 0;
	GtkTreeListRow *row = impl->m_DropExpandRow;
	impl->m_DropExpandRow = NULL;
	if (NULL != row)
	{
		GtkWidget *sw = gtk_widget_get_ancestor(GTK_WIDGET(impl->m_pListView), GTK_TYPE_SCROLLED_WINDOW);
		GtkAdjustment *vadj = sw ? gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(sw)) : NULL;
		gdouble vadj_before = vadj ? gtk_adjustment_get_value(vadj) : 0.0;

		gtk_tree_list_row_set_expanded(row, TRUE);
		gtk_widget_queue_allocate(GTK_WIDGET(impl->m_pListView));
		gtk_widget_queue_draw(GTK_WIDGET(impl->m_pListView));

		// Process pending GTK layout and factory bind cycles so newly expanded
		// rows are materialized and painted during the active DnD grab
		while (g_main_context_iteration(NULL, FALSE));

		GtkNative *native = gtk_widget_get_native(GTK_WIDGET(impl->m_pListView));
		if (native)
		{
			GdkSurface *surface = gtk_native_get_surface(native);
			if (surface)
			{
				gdk_surface_queue_render(surface);
				GdkFrameClock *clock = gdk_surface_get_frame_clock(surface);
				if (clock)
				{
					gdk_frame_clock_request_phase(clock, GDK_FRAME_CLOCK_PHASE_LAYOUT);
					gdk_frame_clock_request_phase(clock, GDK_FRAME_CLOCK_PHASE_PAINT);
				}
			}
		}

		while (g_main_context_iteration(NULL, FALSE));

		// Keep the scroll offset exactly at vadj_before so the expanded folder
		// row does not jump or scroll away from under the cursor.
		if (vadj)
		{
			double cur_v = gtk_adjustment_get_value(vadj);
			if (std::abs(cur_v - vadj_before) > 0.5)
			{
				gtk_adjustment_set_value(vadj, vadj_before);
				while (g_main_context_iteration(NULL, FALSE));
			}
		}

		DirItem *item = NULL;
		GtkTreeListRow *cur_row = NULL;
		GtkWidget *hbox = folder_tree_row_hbox_at_pos(impl, impl->m_dDropX, impl->m_dDropY, &item, &cur_row);

		if (NULL != hbox && NULL != item)
		{
			if (impl->m_pDropHover != hbox)
			{
				if (NULL != impl->m_pDropHover)
					gtk_widget_remove_css_class(impl->m_pDropHover, "drop-hover");
				gtk_widget_add_css_class(hbox, "drop-hover");
				impl->m_pDropHover = hbox;
			}
			impl->m_pDropHoverItem = item;
		}
		g_object_unref(row);
	}
	return G_SOURCE_REMOVE;
}

static void
folder_tree_cancel_drop_expand(FolderTree::FolderTreeImpl *impl)
{
	if (NULL == impl)
		return;
	if (0 != impl->m_DropExpandTimer)
	{
		g_source_remove(impl->m_DropExpandTimer);
		impl->m_DropExpandTimer = 0;
	}
	if (NULL != impl->m_DropExpandRow)
	{
		g_object_unref(impl->m_DropExpandRow);
		impl->m_DropExpandRow = NULL;
	}
}

static void
folder_tree_arm_drop_expand(FolderTree::FolderTreeImpl *impl, GtkTreeListRow *row)
{
	if (NULL == impl)
		return;
	if (row == impl->m_DropExpandRow)
		return;
	folder_tree_cancel_drop_expand(impl);
	if (NULL != row && GTK_IS_TREE_LIST_ROW(row) && gtk_tree_list_row_is_expandable(row) &&
	    !gtk_tree_list_row_get_expanded(row))
	{
		impl->m_DropExpandRow = (GtkTreeListRow*)g_object_ref(row);
		impl->m_DropExpandTimer = g_timeout_add(
			FOLDER_TREE_DROP_EXPAND_DELAY_MS,
			folder_tree_drop_expand_timeout, impl);
	}
}

static gboolean folder_tree_update_status(FolderTree::FolderTreeImpl* impl,
	GtkDropTarget* target);

/* Hovering a folder that cannot receive the drop must not look like a
 * target: an item that already lives in the folder it is dragged over, and a
 * folder dragged onto itself or its own subtree, are refused by the drop
 * handler, and the row has to say so while the drag is still hovering. */
static gboolean
folder_tree_update_status(FolderTree::FolderTreeImpl* impl, GtkDropTarget* target)
{
	if (NULL == impl)
		return GDK_EVENT_PROPAGATE;
	GdkDrop *drop = gtk_drop_target_get_current_drop(target);
	if (NULL == drop)
		return GDK_EVENT_PROPAGATE;

	DirItem *item = NULL;
	GtkTreeListRow *row = NULL;
	GtkWidget *hbox = folder_tree_row_hbox_at_pos(impl, impl->m_dDropX,
		impl->m_dDropY, &item, &row);
	/* The drag contents decide whether this row is a target at all: a file
	 * over the folder it already lives in, or a folder over itself or its
	 * own subtree, must not be highlighted.  While GTK is still preloading
	 * them nothing is highlighted either - guessing there is what made a
	 * refused drop look accepted.  The drop handler applies the same
	 * policy to the value it is handed. */
	std::list<std::string> drop_uris;
	bool bDropCut = false;
	bool bRefused = (NULL == hbox || NULL == item || NULL == item->uri)
		|| !impl->m_DropContents.Accepts(target, item ? item->uri : std::string(),
			drop_uris, bDropCut);
	if (bRefused)
	{
		folder_tree_clear_drop_hover(impl);
		folder_tree_cancel_drop_expand(impl);
		gdk_drop_status(drop, (GdkDragAction)0, (GdkDragAction)0);
		return GDK_EVENT_PROPAGATE;
	}
	if (hbox != impl->m_pDropHover)
	{
		if (NULL != impl->m_pDropHover)
			gtk_widget_remove_css_class(impl->m_pDropHover, "drop-hover");
		gtk_widget_add_css_class(hbox, "drop-hover");
		impl->m_pDropHover = hbox;
	}
	impl->m_pDropHoverItem = item;
	folder_tree_arm_drop_expand(impl, row);
	GdkDragAction actions = gdk_drop_get_actions(drop);
	GdkDragAction chosen = (0 != (actions & GDK_ACTION_MOVE)) ? GDK_ACTION_MOVE : GDK_ACTION_COPY;
	gdk_drop_status(drop, chosen, chosen);
	return GDK_EVENT_STOP;
}

static gboolean
folder_tree_drop_motion_cb(GtkDropTarget *target, gdouble x, gdouble y, gpointer userdata)
{
	FolderTree::FolderTreeImpl *impl = (FolderTree::FolderTreeImpl*)userdata;
	if (NULL == impl)
		return GDK_EVENT_PROPAGATE;
	GdkDrop *drop = gtk_drop_target_get_current_drop(target);
	if (NULL == drop)
		return GDK_EVENT_PROPAGATE;
	impl->m_dDropX = x;
	impl->m_dDropY = y;
	impl->m_bDropFinished = false;

	return folder_tree_update_status(impl, target) ? GDK_EVENT_STOP : GDK_EVENT_PROPAGATE;
}

/* The contents of the drag arrive a moment after it enters, and a drag that
 * stops moving gets no further motion event: decide the status again at the
 * position the pointer is actually at. */
static void
folder_tree_drop_value_notify_cb(GObject *object, GParamSpec *pspec, gpointer userdata)
{
	(void)pspec;
	FolderTree::FolderTreeImpl *impl = (FolderTree::FolderTreeImpl*)userdata;
	if (NULL == impl)
		return;
	GtkDropTarget *target = GTK_DROP_TARGET(object);
	if (NULL == gtk_drop_target_get_current_drop(target) || impl->m_bDropFinished)
	{
		folder_tree_clear_drop_hover(impl);
		return;
	}
	folder_tree_update_status(impl, target);
}

static void
shortcuts_drop_value_notify_cb(GObject *object, GParamSpec *pspec, gpointer userdata)
{
	(void)pspec;
	FolderTree::FolderTreeImpl *impl = (FolderTree::FolderTreeImpl*)userdata;
	if (NULL == impl)
		return;
	GtkDropTarget *target = GTK_DROP_TARGET(object);
	if (NULL == gtk_drop_target_get_current_drop(target) || impl->m_bDropFinished)
	{
		folder_tree_clear_drop_hover(impl);
		return;
	}
	shortcuts_update_status(impl, target);
}

static void
folder_tree_drop_leave_cb(GtkDropTarget *target, gpointer userdata)
{ (void)target;
	FolderTree::FolderTreeImpl *impl = (FolderTree::FolderTreeImpl*)userdata;
	folder_tree_clear_drop_hover(impl);
	folder_tree_cancel_drop_expand(impl);
	if (NULL != impl)
	{
		impl->m_DropContents.Reset();
		impl->m_bDropFinished = false;
	}
}

static gboolean
folder_tree_drop_cb(GtkDropTarget *target, const GValue *value, gdouble x, gdouble y, gpointer userdata)
{
	FolderTree::FolderTreeImpl *impl = (FolderTree::FolderTreeImpl*)userdata;
	if (NULL != impl)
		impl->m_bDropFinished = true;
	if (NULL == impl || NULL == value || !G_VALUE_HOLDS_STRING(value))
		return GDK_EVENT_PROPAGATE;
	DirItem *item = NULL;
	folder_tree_row_hbox_at_pos(impl, x, y, &item, NULL);
	folder_tree_clear_drop_hover(impl);
	folder_tree_cancel_drop_expand(impl);
	if (NULL == item || NULL == item->uri)
		return GDK_EVENT_PROPAGATE;

	std::list<std::string> uris;
	bool bCut = false;
	const char *text = g_value_get_string(value);
	if (!QuiverClipboard::ParseClipboardText(text ? text : "", uris, bCut) || uris.empty())
		return GDK_EVENT_PROPAGATE;

	/* The row's URI is the target; the value is only handed over when the
	 * user releases, so the shared policy runs here, on the URIs the drop
	 * really carries: a folder aimed at itself or its own subtree, or an
	 * item dropped into the folder it already sits in.  A real ancestor
	 * further up stays an allowed move. */
	if (QuiverFileOps::DropRefused(uris, item->uri))
		return GDK_EVENT_PROPAGATE;

	/* Same move/copy rule as the browser icon view: a cut payload dropped
	 * with MOVE moves; anything else copies. */
	GdkDrop *drop = gtk_drop_target_get_current_drop(target);
	bool bMove = true;
	if (NULL != drop)
	{
		GdkDragAction actions = gdk_drop_get_actions(drop);
		if (actions == GDK_ACTION_COPY && !bCut)
			bMove = false;
	}
	else
		bMove = bCut;

	struct FolderTreeDropData
	{
		FolderTree::FolderTreeImpl *impl;
		std::list<std::string> uris;
		std::string target_uri;
		bool bMove;
		bool bCut;
	};

	FolderTreeDropData *data = new FolderTreeDropData();
	data->impl = impl;
	data->uris = uris;
	data->target_uri = item->uri;
	data->bMove = bMove;
	data->bCut = bCut;

	/* The URIs have been copied out of the drop value and returning STOP
	 * hands the drop back to GtkDropTarget, which finishes it for us. */

	g_idle_add(+[](gpointer user_data) -> gboolean {
		FolderTreeDropData *d = static_cast<FolderTreeDropData*>(user_data);
		if (g_strcmp0(d->target_uri.c_str(), "trash:///") == 0)
		{
			std::list<QuiverFile> trashed_files;
			std::list<std::string> trashed_uris;
			for (const auto &u : d->uris)
			{
				QuiverFile qf(u.c_str());
				if (QuiverFileOps::MoveToTrash(qf))
				{
					trashed_files.push_back(qf);
					trashed_uris.push_back(u);
				}
			}
			if (!trashed_files.empty())
			{
				QuiverFileOps::UndoStackRecordDelete(trashed_files);
			}
			QuiverFileOps::ClipboardRemoveURIs(trashed_uris);
			if (!trashed_uris.empty() && d->impl && d->impl->m_pFolderTree)
			{
				d->impl->m_pFolderTree->EmitSelectionChangedEvent();
			}
		}
		else
		{
			FolderTreePasteConflictUI ui;
			std::list<std::string> moved;
			int movedCount = QuiverFileOps::TransferFiles(d->uris, d->bMove, d->target_uri.c_str(),
				folder_tree_paste_conflict_cb, &ui, &moved);
			if (d->bCut || d->bMove)
				QuiverFileOps::ClipboardRemoveURIs(moved);
			if (movedCount > 0 && d->impl && d->impl->m_pFolderTree)
			{
				d->impl->m_pFolderTree->EmitSelectionChangedEvent();
			}
		}
		delete d;
		return G_SOURCE_REMOVE;
	}, data);

	return GDK_EVENT_STOP;
}

static GtkWidget*
shortcuts_row_hbox_at_pos(FolderTree::FolderTreeImpl *impl, GtkListView *listview, gdouble x, gdouble y, DirItem **item_out)
{
	if (NULL != item_out)
		*item_out = NULL;
	if (NULL == impl || NULL == listview)
		return NULL;
	GtkWidget *w = gtk_widget_pick(GTK_WIDGET(listview), x, y, GTK_PICK_DEFAULT);
	while (NULL != w && w != GTK_WIDGET(listview))
	{
		if (NULL != g_object_get_data(G_OBJECT(w), "list-item"))
		{
			FolderTreeRowWidgets* rw = static_cast<FolderTreeRowWidgets*>(
				g_object_get_data(G_OBJECT(w), "row-widgets"));
			DirItem *item = rw ? rw->bound_item : nullptr;
			if (NULL != item && NULL != item->uri)
			{
				if (g_strcmp0(item->uri, "trash:///") == 0 || folder_tree_uri_is_directory(item->uri))
				{
					if (NULL != item_out)
						*item_out = item;
					return w;
				}
			}
			return NULL;
		}
		w = gtk_widget_get_parent(w);
	}
	return NULL;
}

static gboolean shortcuts_update_status(FolderTree::FolderTreeImpl* impl,
	GtkDropTarget* target);

/* The same refusal as the folder tree: a row that cannot receive the drop is
 * not highlighted and offers no action. */
static gboolean
shortcuts_update_status(FolderTree::FolderTreeImpl* impl, GtkDropTarget* target)
{
	if (NULL == impl)
		return GDK_EVENT_PROPAGATE;
	GdkDrop *drop = gtk_drop_target_get_current_drop(target);
	if (NULL == drop)
		return GDK_EVENT_PROPAGATE;
	GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(target));
	GtkListView *lv = GTK_IS_LIST_VIEW(widget) ? GTK_LIST_VIEW(widget) : impl->m_pShortcutsListView;

	DirItem *item = NULL;
	GtkWidget *hbox = shortcuts_row_hbox_at_pos(impl, lv, impl->m_dDropX,
		impl->m_dDropY, &item);
	/* the same refusal as the folder tree, from the same preloaded contents */
	std::list<std::string> drop_uris;
	bool bDropCut = false;
	bool bRefused = (NULL == hbox || NULL == item || NULL == item->uri)
		|| !impl->m_DropContents.Accepts(target, item ? item->uri : std::string(),
			drop_uris, bDropCut);
	if (bRefused)
	{
		folder_tree_clear_drop_hover(impl);
		gdk_drop_status(drop, (GdkDragAction)0, (GdkDragAction)0);
		return GDK_EVENT_PROPAGATE;
	}
	if (hbox != impl->m_pDropHover)
	{
		if (NULL != impl->m_pDropHover)
			gtk_widget_remove_css_class(impl->m_pDropHover, "drop-hover");
		gtk_widget_add_css_class(hbox, "drop-hover");
		impl->m_pDropHover = hbox;
	}
	impl->m_pDropHoverItem = item;
	GdkDragAction actions = gdk_drop_get_actions(drop);
	GdkDragAction chosen = (0 != (actions & GDK_ACTION_MOVE)) ? GDK_ACTION_MOVE : GDK_ACTION_COPY;
	gdk_drop_status(drop, chosen, chosen);
	return GDK_EVENT_STOP;
}

static gboolean
shortcuts_drop_motion_cb(GtkDropTarget *target, gdouble x, gdouble y, gpointer userdata)
{
	FolderTree::FolderTreeImpl *impl = (FolderTree::FolderTreeImpl*)userdata;
	if (NULL == impl)
		return GDK_EVENT_PROPAGATE;
	GdkDrop *drop = gtk_drop_target_get_current_drop(target);
	if (NULL == drop)
		return GDK_EVENT_PROPAGATE;
	impl->m_dDropX = x;
	impl->m_dDropY = y;
	impl->m_bDropFinished = false;

	return shortcuts_update_status(impl, target) ? GDK_EVENT_STOP : GDK_EVENT_PROPAGATE;
}

static void
shortcuts_drop_leave_cb(GtkDropTarget *target, gpointer userdata)
{
	(void)target;
	FolderTree::FolderTreeImpl *impl = (FolderTree::FolderTreeImpl*)userdata;
	folder_tree_clear_drop_hover(impl);
	if (NULL != impl)
	{
		impl->m_DropContents.Reset();
		impl->m_bDropFinished = false;
	}
}

static gboolean
shortcuts_drop_cb(GtkDropTarget *target, const GValue *value, gdouble x, gdouble y, gpointer userdata)
{
	FolderTree::FolderTreeImpl *impl = (FolderTree::FolderTreeImpl*)userdata;
	if (NULL != impl)
		impl->m_bDropFinished = true;
	if (NULL == impl || NULL == value || !G_VALUE_HOLDS_STRING(value))
		return GDK_EVENT_PROPAGATE;
	GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(target));
	GtkListView *lv = GTK_IS_LIST_VIEW(widget) ? GTK_LIST_VIEW(widget) : impl->m_pShortcutsListView;

	DirItem *item = NULL;
	shortcuts_row_hbox_at_pos(impl, lv, x, y, &item);
	folder_tree_clear_drop_hover(impl);
	if (NULL == item || NULL == item->uri)
		return GDK_EVENT_PROPAGATE;

	std::list<std::string> uris;
	bool bCut = false;
	const char *text = g_value_get_string(value);
	if (!QuiverClipboard::ParseClipboardText(text ? text : "", uris, bCut) || uris.empty())
		return GDK_EVENT_PROPAGATE;

	GdkDrop *drop = gtk_drop_target_get_current_drop(target);
	bool bMove = true;
	if (NULL != drop)
	{
		GdkDragAction actions = gdk_drop_get_actions(drop);
		if (actions == GDK_ACTION_COPY && !bCut)
			bMove = false;
	}
	else
		bMove = bCut;

	struct ShortcutDropData
	{
		FolderTree::FolderTreeImpl *impl;
		std::list<std::string> uris;
		std::string target_uri;
		bool bMove;
		bool bCut;
	};

	ShortcutDropData *data = new ShortcutDropData();
	data->impl = impl;
	data->uris = uris;
	data->target_uri = item->uri;
	data->bMove = bMove;
	data->bCut = bCut;

	/* The URIs have been copied out of the drop value and returning STOP
	 * hands the drop back to GtkDropTarget, which finishes it for us. */

	g_idle_add(+[](gpointer user_data) -> gboolean {
		ShortcutDropData *d = static_cast<ShortcutDropData*>(user_data);
		if (g_strcmp0(d->target_uri.c_str(), "trash:///") == 0)
		{
			std::list<QuiverFile> trashed_files;
			std::list<std::string> trashed_uris;
			for (const auto &u : d->uris)
			{
				QuiverFile qf(u.c_str());
				if (QuiverFileOps::MoveToTrash(qf))
				{
					trashed_files.push_back(qf);
					trashed_uris.push_back(u);
				}
			}
			if (!trashed_files.empty())
			{
				QuiverFileOps::UndoStackRecordDelete(trashed_files);
			}
			QuiverFileOps::ClipboardRemoveURIs(trashed_uris);
			if (!trashed_uris.empty() && d->impl && d->impl->m_pFolderTree)
			{
				d->impl->m_pFolderTree->EmitSelectionChangedEvent();
			}
		}
		else
		{
			FolderTreePasteConflictUI ui;
			std::list<std::string> moved;
			int movedCount = QuiverFileOps::TransferFiles(d->uris, d->bMove, d->target_uri.c_str(),
				folder_tree_paste_conflict_cb, &ui, &moved);
			if (d->bCut || d->bMove)
				QuiverFileOps::ClipboardRemoveURIs(moved);
			if (movedCount > 0 && d->impl && d->impl->m_pFolderTree)
			{
				d->impl->m_pFolderTree->EmitSelectionChangedEvent();
			}
		}
		delete d;
		return G_SOURCE_REMOVE;
	}, data);

	return GDK_EVENT_STOP;
}

static void
folder_tree_action_paste_into (GSimpleAction* action, GVariant* parameter, gpointer userdata)
{ (void)action;
	FolderTree::FolderTreeImpl* pFolderTreeImpl = (FolderTree::FolderTreeImpl*)userdata;
	if (NULL == pFolderTreeImpl || NULL == parameter)
		return;

	const gchar* uri = g_variant_get_string(parameter, NULL);
	if (NULL == uri || '\0' == uri[0])
		return;

	std::list<std::string> uris = *QuiverFileOps::ClipboardGetUris();
	bool bCut = QuiverFileOps::ClipboardIsCut();
	if (uris.empty())
	{
		if (!QuiverClipboard::GetClipboardUris(uris, bCut))
			return;
		bCut = false;
	}

	FolderTreePasteConflictUI ui;
	std::list<std::string> moved;
	int movedCount = QuiverFileOps::TransferFiles(uris, bCut, uri,
		folder_tree_paste_conflict_cb, &ui, &moved);
	if (bCut)
		QuiverFileOps::ClipboardRemoveURIs(moved);
	if (movedCount <= 0)
		return;

	/* Switch the browser to the target folder so the pasted material is
	 * immediately visible. */
	std::list<std::string> sel;
	sel.push_back(uri);
	pFolderTreeImpl->SetSelectedFolders(sel);
	pFolderTreeImpl->m_pFolderTree->EmitSelectionChangedEvent();
}

static void
folder_tree_action_new_folder (GSimpleAction* action, GVariant* parameter, gpointer userdata)
{ (void)action;
	FolderTree::FolderTreeImpl* pFolderTreeImpl = (FolderTree::FolderTreeImpl*)userdata;
	if (NULL == pFolderTreeImpl || NULL == parameter)
		return;

	const gchar* uri = g_variant_get_string(parameter, NULL);
	if (NULL == uri || '\0' == uri[0] || !QuiverUtils::IsDirectoryURI(uri))
		return;

	GFile *parent = QuiverUtils::FileFromURIOrPath(uri);
	if (NULL == parent)
		return;

	std::string initial_name = QuiverUtils::GetUniqueFolderName(parent, "New Folder");
	char *folder_name = QuiverUtils::PromptForString("New Folder", "Folder name:", initial_name.c_str(), "Create");
	if (NULL == folder_name || '\0' == folder_name[0])
	{
		if (folder_name)
			g_free(folder_name);
		g_object_unref(parent);
		return;
	}

	GFile *child = g_file_get_child(parent, folder_name);
	GError *error = NULL;
	gboolean ok = g_file_make_directory(child, NULL, &error);
	if (!ok)
	{
		QuiverUtils::ConfirmDialog("Create Folder Failed",
			error && error->message ? error->message : "The folder could not be created.",
			"OK", "Close");
		if (error)
			g_error_free(error);
		g_object_unref(child);
		g_object_unref(parent);
		g_free(folder_name);
		return;
	}

	char *child_uri = g_file_get_uri(child);
	char *parent_canonical_uri = g_file_get_uri(parent);
	pFolderTreeImpl->AddChildFolder(
		parent_canonical_uri ? parent_canonical_uri : uri,
		child_uri, folder_name);
	if (child_uri)
		QuiverFileOps::UndoStackRecordNewFolder(child_uri);

	/* If the target folder is currently viewed in the browser, emit selection changed
	 * so the browser reloads and shows the newly created folder in the icon view */
	std::string norm_target = folder_tree_normalize_uri(uri);
	std::list<std::string> current_sel = pFolderTreeImpl->GetSelectedFolders();
	for (const auto &s : current_sel)
	{
		std::string norm_s = folder_tree_normalize_uri(s.c_str());
		if (norm_s == norm_target && pFolderTreeImpl->m_pFolderTree)
		{
			pFolderTreeImpl->m_pFolderTree->EmitSelectionChangedEvent();
			break;
		}
	}

	g_free(parent_canonical_uri);
	if (child_uri)
		g_free(child_uri);
	g_object_unref(child);
	g_object_unref(parent);
	g_free(folder_name);
}

static void
folder_tree_action_add_bookmark (GSimpleAction* action, GVariant* parameter, gpointer userdata)
{ (void)action;
	/* A right-click on a folder that is part of the current (checkbox)
	 * selection should bookmark the whole selection, not just that one item. */
	FolderTree::FolderTreeImpl* impl = (FolderTree::FolderTreeImpl*)userdata;
	if (NULL == impl)
		return;

	const char* clicked_uri = (NULL != parameter)
		? g_variant_get_string(parameter, NULL) : NULL;

	std::list<std::string> uris = impl->GetAddBookmarkURIs(
		(NULL != clicked_uri) ? clicked_uri : "");
	if (uris.empty())
		return;

	QuiverUtils::PromptAddBookmark(uris);
}

static void
folder_tree_action_edit_bookmark (GSimpleAction* action, GVariant* parameter, gpointer userdata)
{ (void)action; (void)userdata;
	if (NULL == parameter)
		return;

	gint32 bm_id = g_variant_get_int32(parameter);
	if (bm_id < 0)
		return;

	BookmarksPtr bmPtr = Bookmarks::GetInstance();
	if (!bmPtr)
		return;

	const Bookmark* b = bmPtr->GetBookmark(bm_id);
	if (!b)
		return;

	BookmarkAddEditDlg dlg(*b);
	dlg.Run();
	if (!dlg.Cancelled())
	{
		Bookmark newbm = dlg.GetBookmark();
		if (!newbm.GetName().empty())
		{
			bmPtr->UpdateBookmark(newbm);
		}
	}
}

void view_popup_menu_at (GtkWidget *treeview, gdouble x, gdouble y,
	TreePasteMode paste_mode, const gchar* target_uri, gpointer userdata, int bookmark_id)
{
	FolderTree::FolderTreeImpl* pFolderTreeImpl = (FolderTree::FolderTreeImpl*)userdata;
	if (NULL == pFolderTreeImpl || NULL == pFolderTreeImpl->m_pMenuPopover)
		return;

	/* The menu model is rebuilt every time: the paste item carries the
	 * right-clicked folder's URI as its action target.  Only directories get
	 * a paste entry, and the label depends on where the click landed. */
	GMenu *menu = g_menu_new();

	GMenu *folder_section = g_menu_new();
	if (bookmark_id >= 0)
	{
		GMenuItem *edit_bookmark = g_menu_item_new("Edit Bookmark…", NULL);
		g_menu_item_set_action_and_target(edit_bookmark, "quiver.FolderTreeEditBookmark", "i", bookmark_id);
		g_menu_item_set_attribute(edit_bookmark, "icon", "s", "document-edit-symbolic");
		g_menu_append_item(folder_section, edit_bookmark);
		g_object_unref(edit_bookmark);
	}

	if (target_uri != NULL && QuiverUtils::IsDirectoryURI(target_uri))
	{
		GMenuItem *new_folder = g_menu_item_new("New Folder", NULL);
		g_menu_item_set_action_and_target(new_folder, "quiver.FolderTreeNewFolder", "s", target_uri);
		g_menu_item_set_attribute(new_folder, "icon", "s", "folder-new-symbolic");
		g_menu_append_item(folder_section, new_folder);
		g_object_unref(new_folder);

		if (bookmark_id < 0)
		{
			GMenuItem *add_bookmark = g_menu_item_new("Add Bookmark", NULL);
			g_menu_item_set_action_and_target(add_bookmark, "quiver.FolderTreeAddBookmark", "s", target_uri);
			g_menu_item_set_attribute(add_bookmark, "icon", "s", "bookmark-new-symbolic");
			g_menu_append_item(folder_section, add_bookmark);
			g_object_unref(add_bookmark);
		}
	}

	if (TreePasteMode::PASTE_NONE != paste_mode &&
	    target_uri != NULL && QuiverFileOps::ClipboardHasItems())
	{
		const char *label = (TreePasteMode::PASTE_INTO_FOLDER == paste_mode)
			? "Paste Into Folder" : "Paste";
		GMenuItem *paste = g_menu_item_new(label, NULL);
		g_menu_item_set_action_and_target(paste, "quiver.FolderTreePasteInto", "s", target_uri);
		g_menu_item_set_attribute(paste, "accel", "s", "<Control>v");
		g_menu_append_item(folder_section, paste);
		g_object_unref(paste);
	}

	if (g_menu_model_get_n_items(G_MENU_MODEL(folder_section)) > 0)
	{
		g_menu_append_section(menu, NULL, G_MENU_MODEL(folder_section));
	}
	g_object_unref(folder_section);

	GMenu *select_section = g_menu_new();
	QuiverUtils::MenuAppendAction(select_section, "Check Selected Item(s)",
		"quiver.FolderTreeCheckSelected", NULL);
	QuiverUtils::MenuAppendAction(select_section, "Uncheck Selected Item(s)",
		"quiver.FolderTreeUncheckSelected", NULL);
	g_menu_append_section(menu, NULL, G_MENU_MODEL(select_section));
	g_object_unref(select_section);

	gtk_popover_menu_set_menu_model(
		GTK_POPOVER_MENU(pFolderTreeImpl->m_pMenuPopover), G_MENU_MODEL(menu));
	g_object_unref(menu);

	QuiverUtils::ShowContextMenuAt(
		GTK_POPOVER(pFolderTreeImpl->m_pMenuPopover), treeview, x, y);
}

static void folder_tree_focus_position(FolderTree::FolderTreeImpl* impl, guint pos)
{
	if (!impl || !impl->m_pTreeListModel)
		return;

	guint n = g_list_model_get_n_items(G_LIST_MODEL(impl->m_pTreeListModel));
	if (pos >= n)
		return;

	impl->m_iFocusedTreePos = pos;

	guint flat_offset = impl->GetTreeFlattenOffset();
	if (impl->m_pSelectionModel)
	{
		gtk_selection_model_select_item(GTK_SELECTION_MODEL(impl->m_pSelectionModel), flat_offset + pos, TRUE);
	}

	if (impl->m_pListView)
	{
		impl->ScrollToTreePosition(pos, GTK_LIST_SCROLL_FOCUS);

		GtkWidget* lv = GTK_WIDGET(impl->m_pListView);
		for (GtkWidget* item_widget = gtk_widget_get_first_child(lv);
		     item_widget != NULL;
		     item_widget = gtk_widget_get_next_sibling(item_widget))
		{
			GtkWidget* outer_box = gtk_widget_get_first_child(item_widget);
			if (outer_box)
			{
				GtkTreeListRow* r = static_cast<GtkTreeListRow*>(
					g_object_get_data(G_OBJECT(outer_box), "dir-row"));
				if (r && gtk_tree_list_row_get_position(r) == pos)
				{
					GtkWidget* exp = static_cast<GtkWidget*>(
						g_object_get_data(G_OBJECT(outer_box), "f-expander"));
					gtk_widget_grab_focus(exp ? exp : outer_box);
					break;
				}
			}
		}
	}
}

static guint folder_tree_get_focused_position(FolderTree::FolderTreeImpl* impl)
{
	if (!impl || !impl->m_pListView)
		return G_MAXUINT;

	guint n = impl->m_pTreeListModel ? g_list_model_get_n_items(G_LIST_MODEL(impl->m_pTreeListModel)) : 0;
	if (n == 0)
		return G_MAXUINT;

	guint flat_offset = impl->GetTreeFlattenOffset();

	GtkWidget* lv = GTK_WIDGET(impl->m_pListView);
	GtkRoot* root = gtk_widget_get_root(lv);
	GtkWidget* f = root ? gtk_root_get_focus(root) : NULL;
	if (!f)
		f = gtk_widget_get_focus_child(lv);

	for (GtkWidget* w = f; w && w != lv; w = gtk_widget_get_parent(w))
	{
		GtkTreeListRow* row = static_cast<GtkTreeListRow*>(
			g_object_get_data(G_OBJECT(w), "dir-row"));
		if (row)
		{
			guint pos = gtk_tree_list_row_get_position(row);
			if (pos < n)
			{
				impl->m_iFocusedTreePos = pos;
				return pos;
			}
		}
		GtkListItem* li = static_cast<GtkListItem*>(
			g_object_get_data(G_OBJECT(w), "list-item"));
		if (li)
		{
			guint flat_pos = gtk_list_item_get_position(li);
			if (flat_pos >= flat_offset && (flat_pos - flat_offset) < n)
			{
				guint pos = flat_pos - flat_offset;
				impl->m_iFocusedTreePos = pos;
				return pos;
			}
		}
	}

	GtkWidget* fc = gtk_widget_get_focus_child(lv);
	if (fc)
	{
		for (GtkWidget* ch = gtk_widget_get_first_child(fc); ch; ch = gtk_widget_get_next_sibling(ch))
		{
			GtkTreeListRow* row = static_cast<GtkTreeListRow*>(
				g_object_get_data(G_OBJECT(ch), "dir-row"));
			if (row)
			{
				guint pos = gtk_tree_list_row_get_position(row);
				if (pos < n)
				{
					impl->m_iFocusedTreePos = pos;
					return pos;
				}
			}
			GtkListItem* li = static_cast<GtkListItem*>(
				g_object_get_data(G_OBJECT(ch), "list-item"));
			if (li)
			{
				guint flat_pos = gtk_list_item_get_position(li);
				if (flat_pos >= flat_offset && (flat_pos - flat_offset) < n)
				{
					guint pos = flat_pos - flat_offset;
					impl->m_iFocusedTreePos = pos;
					return pos;
				}
			}
		}
	}

	// Fallback to selection model
	if (impl->m_pSelectionModel && impl->m_pTreeListModel)
	{
		if (impl->m_iFocusedTreePos != G_MAXUINT && impl->m_iFocusedTreePos < n &&
		    gtk_selection_model_is_selected(GTK_SELECTION_MODEL(impl->m_pSelectionModel), flat_offset + impl->m_iFocusedTreePos))
		{
			return impl->m_iFocusedTreePos;
		}

		for (guint i = 0; i < n; i++)
		{
			if (gtk_selection_model_is_selected(GTK_SELECTION_MODEL(impl->m_pSelectionModel), flat_offset + i))
			{
				impl->m_iFocusedTreePos = i;
				return i;
			}
		}
		if (n > 0)
			return 0;
	}

	return G_MAXUINT;
}

/* While the folder tree has keyboard focus the global unmodified "space"
 * accelerator is disabled in browser mode (see Quiver::ShowBrowser), so
 * space/Enter reach this handler to toggle the checkbox selection instead of
 * moving to the next image. */
static gboolean view_on_key_press(GtkEventControllerKey *controller, guint keyval, guint keycode, GdkModifierType state, gpointer userdata)
{
	(void)keycode;
	FolderTree::FolderTreeImpl* pFolderTreeImpl = (FolderTree::FolderTreeImpl*)userdata;
	GtkWidget *treeview = GTK_WIDGET(gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(controller)));

	if (GDK_KEY_Menu == keyval || ((state & GDK_SHIFT_MASK) && (GDK_KEY_F10 == keyval)))
	{
		const gchar* target_uri = NULL;
		int bookmark_id = -1;
		TreePasteMode paste_mode = TreePasteMode::PASTE_INTO_FOLDER;

		GtkWidget* lv = GTK_WIDGET(pFolderTreeImpl->m_pListView);
		GtkRoot* root = lv ? gtk_widget_get_root(lv) : NULL;
		GtkWidget* f = root ? gtk_root_get_focus(root) : NULL;
		if (!f && lv)
			f = gtk_widget_get_focus_child(lv);

		DirItem* focused_item = NULL;
		for (GtkWidget* w = f; w && w != lv; w = gtk_widget_get_parent(w))
		{
			FolderTreeRowWidgets* rw = static_cast<FolderTreeRowWidgets*>(
				g_object_get_data(G_OBJECT(w), "row-widgets"));
			if (rw && rw->bound_item)
			{
				focused_item = rw->bound_item;
				break;
			}
		}

		if (focused_item)
		{
			target_uri = focused_item->uri;
			bookmark_id = focused_item->bookmark_id;
			if (!folder_tree_uri_is_directory(target_uri))
				paste_mode = TreePasteMode::PASTE_NONE;
		}
		else
		{
			guint cursor_pos = folder_tree_get_focused_position(pFolderTreeImpl);
			guint n = pFolderTreeImpl->m_pTreeListModel ? g_list_model_get_n_items(G_LIST_MODEL(pFolderTreeImpl->m_pTreeListModel)) : 0;
			if (cursor_pos != G_MAXUINT && cursor_pos < n)
			{
				GtkTreeListRow* row = gtk_tree_list_model_get_row(
					pFolderTreeImpl->m_pTreeListModel, cursor_pos);
				if (row)
				{
					DirItem* item = DIR_ITEM(gtk_tree_list_row_get_item(row));
					if (item)
					{
						target_uri = item->uri;
						bookmark_id = item->bookmark_id;
					}
					g_object_unref(row);
				}
			}
		}
		view_popup_menu_at(treeview, -1, -1, paste_mode, target_uri, userdata, bookmark_id);
		return TRUE;
	}

	GtkSelectionModel* sel = GTK_SELECTION_MODEL(pFolderTreeImpl->m_pSelectionModel);
	guint cursor_pos = folder_tree_get_focused_position(pFolderTreeImpl);
	guint n = g_list_model_get_n_items(G_LIST_MODEL(pFolderTreeImpl->m_pTreeListModel));
	guint flat_offset = pFolderTreeImpl->GetTreeFlattenOffset();

	guint sel_count = 0;
	for (guint i = 0 ; i < n ; i++)
	{
		if (gtk_selection_model_is_selected(sel, flat_offset + i))
			sel_count++;
	}

	gboolean rval = FALSE;

	if (GDK_KEY_space == keyval && !(state & GDK_CONTROL_MASK))
	{
		if (sel_count >= 1)
		{
			/* Spacebar on selection (single or multi-select):
			 * Check all items if any are unchecked; otherwise uncheck all items. */
			gboolean has_unchecked = FALSE;
			for (guint i = 0 ; i < n ; i++)
			{
				if (gtk_selection_model_is_selected(sel, flat_offset + i))
				{
					GtkTreeListRow* row = gtk_tree_list_model_get_row(pFolderTreeImpl->m_pTreeListModel, i);
					if (row)
					{
						DirItem* item = DIR_ITEM(gtk_tree_list_row_get_item(row));
						if (item && !item->checked)
						{
							has_unchecked = TRUE;
							break;
						}
					}
				}
			}
			gboolean target_val = has_unchecked ? TRUE : FALSE;
			for (guint i = 0 ; i < n ; i++)
			{
				if (gtk_selection_model_is_selected(sel, flat_offset + i))
				{
					GtkTreeListRow* row = gtk_tree_list_model_get_row(pFolderTreeImpl->m_pTreeListModel, i);
					if (row)
					{
						DirItem* item = DIR_ITEM(gtk_tree_list_row_get_item(row));
						if (item)
						{
							dir_item_set_checked(item, target_val);
							pFolderTreeImpl->SyncShortcutSelectionForURI(item->uri, target_val);
						}
					}
				}
			}
			pFolderTreeImpl->m_pFolderTree->EmitSelectionChangedEvent();
		}
		else
		{
			if (G_MAXUINT != cursor_pos)
			{
				GtkTreeListRow* row = gtk_tree_list_model_get_row(pFolderTreeImpl->m_pTreeListModel, cursor_pos);
				if (NULL != row)
				{
					DirItem* item = DIR_ITEM(gtk_tree_list_row_get_item(row));
					if (NULL != item)
					{
						gboolean new_val = !item->checked;
						dir_item_set_checked(item, new_val);
						pFolderTreeImpl->SyncShortcutSelectionForURI(item->uri, new_val);
						pFolderTreeImpl->m_pFolderTree->EmitSelectionChangedEvent();
					}
				}
			}
		}
		rval = TRUE;
		return rval;
	}

	if (GDK_KEY_Return == keyval || GDK_KEY_KP_Enter == keyval)
	{
		if (state & GDK_CONTROL_MASK)
		{
			/* Ctrl pressed: adds/subtracts (toggle checked state of focused row) */
			if (G_MAXUINT != cursor_pos)
			{
				GtkTreeListRow* row = gtk_tree_list_model_get_row(pFolderTreeImpl->m_pTreeListModel, cursor_pos);
				DirItem* item = row ? DIR_ITEM(gtk_tree_list_row_get_item(row)) : NULL;
				if (NULL != item)
				{
					gboolean new_val = !item->checked;
					dir_item_set_checked(item, new_val);
					if (new_val)
						gtk_selection_model_select_item(sel, flat_offset + cursor_pos, FALSE);
					else
						gtk_selection_model_unselect_item(sel, flat_offset + cursor_pos);
					pFolderTreeImpl->SyncShortcutSelectionForURI(item->uri, new_val);
					pFolderTreeImpl->m_pFolderTree->EmitSelectionChangedEvent();
				}
			}
		}
		else if (sel_count > 1)
		{
			/* Multi-selection plain Enter: all selected items become checked off */
			for (guint i = 0 ; i < n ; i++)
			{
				GtkTreeListRow* r = gtk_tree_list_model_get_row(pFolderTreeImpl->m_pTreeListModel, i);
				if (!r) continue;
				DirItem* it = DIR_ITEM(gtk_tree_list_row_get_item(r));
				if (!it) continue;

				if (gtk_selection_model_is_selected(sel, flat_offset + i))
				{
					dir_item_set_checked(it, TRUE);
					pFolderTreeImpl->SyncShortcutSelectionForURI(it->uri, TRUE);
				}
				else
				{
					dir_item_set_checked(it, FALSE);
					pFolderTreeImpl->SyncShortcutSelectionForURI(it->uri, FALSE);
				}
			}
			pFolderTreeImpl->m_pFolderTree->EmitSelectionChangedEvent();
		}
		else
		{
			/* Single item plain Enter: check off focused item and uncheck all others */
			if (G_MAXUINT != cursor_pos)
			{
				GtkTreeListRow* row = gtk_tree_list_model_get_row(pFolderTreeImpl->m_pTreeListModel, cursor_pos);
				DirItem* item = row ? DIR_ITEM(gtk_tree_list_row_get_item(row)) : NULL;
				if (NULL != item)
				{
					pFolderTreeImpl->ClearAllCheckboxes();
					dir_item_set_checked(item, TRUE);
					gtk_selection_model_select_item(sel, flat_offset + cursor_pos, TRUE);
					pFolderTreeImpl->SyncShortcutSelectionForURI(item->uri, TRUE);
					pFolderTreeImpl->m_pFolderTree->EmitSelectionChangedEvent();
				}
			}
		}
		rval = TRUE;
		return rval;
	}

	if (GDK_KEY_Left == keyval || GDK_KEY_KP_Left == keyval)
	{
		if (G_MAXUINT != cursor_pos && cursor_pos < n)
		{
			GtkTreeListRow* row = gtk_tree_list_model_get_row(pFolderTreeImpl->m_pTreeListModel, cursor_pos);
			if (NULL != row && GTK_IS_TREE_LIST_ROW(row))
			{
				if (gtk_tree_list_row_get_expanded(row))
				{
					gtk_tree_list_row_set_expanded(row, FALSE);
					folder_tree_focus_position(pFolderTreeImpl, cursor_pos);
				}
				else
				{
					GtkTreeListRow* parent = gtk_tree_list_row_get_parent(row);
					if (NULL != parent && GTK_IS_TREE_LIST_ROW(parent))
					{
						guint parent_pos = gtk_tree_list_row_get_position(parent);
						folder_tree_focus_position(pFolderTreeImpl, parent_pos);
						g_object_unref(parent);
					}
				}
				g_object_unref(row);
			}
		}
		rval = TRUE;
		return rval;
	}

	if (GDK_KEY_Right == keyval || GDK_KEY_KP_Right == keyval)
	{
		if (G_MAXUINT != cursor_pos && cursor_pos < n)
		{
			GtkTreeListRow* row = gtk_tree_list_model_get_row(pFolderTreeImpl->m_pTreeListModel, cursor_pos);
			if (NULL != row && GTK_IS_TREE_LIST_ROW(row))
			{
				if (!gtk_tree_list_row_get_expanded(row) && gtk_tree_list_row_is_expandable(row))
				{
					gtk_tree_list_row_set_expanded(row, TRUE);
					folder_tree_focus_position(pFolderTreeImpl, cursor_pos);
				}
				else if (gtk_tree_list_row_get_expanded(row))
				{
					GListModel* children = gtk_tree_list_row_get_children(row);
					if (NULL != children && g_list_model_get_n_items(children) > 0)
					{
						guint child_pos = cursor_pos + 1;
						folder_tree_focus_position(pFolderTreeImpl, child_pos);
					}
				}
				g_object_unref(row);
			}
		}
		rval = TRUE;
		return rval;
	}

	return rval;
}

static gboolean
view_capture_button_press (GtkEventController *controller, GdkEvent *event, gpointer userdata)
{
	GdkEventType type = gdk_event_get_event_type(event);
	if (type != GDK_BUTTON_PRESS)
		return GDK_EVENT_PROPAGATE;
	if (gdk_button_event_get_button(event) != 3)
		return GDK_EVENT_PROPAGATE;

	FolderTree::FolderTreeImpl* pFolderTreeImpl = (FolderTree::FolderTreeImpl*)userdata;
	GtkWidget *treeview = gtk_event_controller_get_widget(controller);

	double x = 0, y = 0;
	gdk_event_get_position(event, &x, &y);

	/* gdk_event_get_position reports surface (toplevel) coordinates, but
	 * gtk_widget_pick() and view_popup_menu_at() take coordinates in this
	 * list view's own space.  Translate, otherwise lower rows are misread as
	 * empty area and the popover lands offset below the cursor. */
	{
		GtkWidget *root = GTK_WIDGET(gtk_widget_get_root(treeview));
		if (root != NULL && root != treeview)
		{
			graphene_point_t in = GRAPHENE_POINT_INIT((float)x, (float)y);
			graphene_point_t out;
			if (gtk_widget_compute_point(root, treeview, &in, &out))
			{
				x = out.x;
				y = out.y;
			}
		}
	}

	/* Row clicks are handled by the per-row gestures (their widgets carry the
	 * item data), so only the empty area on the list's background is handled
	 * here. */
	bool over_row = false;
	{
		GtkWidget *picked = gtk_widget_pick(treeview, (gint)x, (gint)y, GTK_PICK_DEFAULT);
		for (GtkWidget *w = picked; w != NULL && w != treeview; w = gtk_widget_get_parent(w))
		{
			if (g_object_get_data(G_OBJECT(w), "row-widgets") != NULL ||
			    g_object_get_data(G_OBJECT(w), "list-item") != NULL)
			{
				over_row = true;
				break;
			}
		}
	}
	if (over_row)
		return GDK_EVENT_PROPAGATE;

	/* Paste target: the focused row's folder -- a bare-area paste lands in the
	 * folder that currently has the list's input focus. */
	const gchar* target_uri = NULL;
	if (pFolderTreeImpl->m_pSelectionModel && pFolderTreeImpl->m_pTreeListModel)
	{
		guint pos = folder_tree_get_focused_position(pFolderTreeImpl);
		if (pos != G_MAXUINT)
		{
			GtkTreeListRow* row = gtk_tree_list_model_get_row(
				pFolderTreeImpl->m_pTreeListModel, pos);
			if (row)
			{
				DirItem* item = DIR_ITEM(gtk_tree_list_row_get_item(row));
				if (item)
					target_uri = item->uri;
				g_object_unref(row);
			}
		}
	}
	view_popup_menu_at(treeview, x, y, TreePasteMode::PASTE_BARE, target_uri, userdata);
	return GDK_EVENT_STOP;
}

void FolderTree::FolderTreeImpl::PopulateShortcutsModel(GListStore *store)
{
	int order = 0;
	const char* home_dir = g_get_home_dir();

	auto add_sc = [store, &order](const char* uri, const char* name, const char* icon) {
		DirItem* item = dir_item_new(uri, name, icon, TRUE, order++, 0);
		item->row_kind = ROW_KIND_SHORTCUT;
		g_list_store_append(store, G_OBJECT(item));
		g_object_unref(item);
	};

	// Home
	if (home_dir && g_file_test(home_dir, G_FILE_TEST_IS_DIR))
	{
		GFile* f = g_file_new_for_path(home_dir);
		char* uri = g_file_get_uri(f);
		const char* icon = QuiverUtils::GetSpecialFolderSymbolicIconName(uri);
		add_sc(uri, "Home", icon ? icon : "user-home-symbolic");
		g_free(uri);
		g_object_unref(f);
	}

	// Desktop
	const char* desktop_dir = g_get_user_special_dir(G_USER_DIRECTORY_DESKTOP);
	char* fallback_desktop = g_build_filename(home_dir, "Desktop", NULL);
	const char* actual_desktop = (desktop_dir && desktop_dir[0] && 0 != g_strcmp0(desktop_dir, home_dir))
		? desktop_dir : fallback_desktop;
	if (g_file_test(actual_desktop, G_FILE_TEST_IS_DIR))
	{
		GFile* f = g_file_new_for_path(actual_desktop);
		char* uri = g_file_get_uri(f);
		const char* icon = QuiverUtils::GetSpecialFolderSymbolicIconName(uri);
		add_sc(uri, "Desktop", icon ? icon : "user-desktop-symbolic");
		g_free(uri);
		g_object_unref(f);
	}
	g_free(fallback_desktop);

	// Documents
	const char* docs_dir = g_get_user_special_dir(G_USER_DIRECTORY_DOCUMENTS);
	char* fallback_docs = g_build_filename(home_dir, "Documents", NULL);
	const char* actual_docs = (docs_dir && docs_dir[0] && 0 != g_strcmp0(docs_dir, home_dir))
		? docs_dir : fallback_docs;
	if (g_file_test(actual_docs, G_FILE_TEST_IS_DIR))
	{
		GFile* f = g_file_new_for_path(actual_docs);
		char* uri = g_file_get_uri(f);
		const char* icon = QuiverUtils::GetSpecialFolderSymbolicIconName(uri);
		add_sc(uri, "Documents", icon ? icon : "folder-documents-symbolic");
		g_free(uri);
		g_object_unref(f);
	}
	g_free(fallback_docs);

	// Downloads
	const char* download_dir = g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD);
	char* fallback_download = g_build_filename(home_dir, "Downloads", NULL);
	const char* actual_download = (download_dir && download_dir[0] && 0 != g_strcmp0(download_dir, home_dir))
		? download_dir : fallback_download;
	if (g_file_test(actual_download, G_FILE_TEST_IS_DIR))
	{
		GFile* f = g_file_new_for_path(actual_download);
		char* uri = g_file_get_uri(f);
		const char* icon = QuiverUtils::GetSpecialFolderSymbolicIconName(uri);
		add_sc(uri, "Downloads", icon ? icon : "folder-download-symbolic");
		g_free(uri);
		g_object_unref(f);
	}
	g_free(fallback_download);

	// Pictures
	const char* pictures_dir = g_get_user_special_dir(G_USER_DIRECTORY_PICTURES);
	char* fallback_pictures = g_build_filename(home_dir, "Pictures", NULL);
	const char* actual_pictures = (pictures_dir && pictures_dir[0] && 0 != g_strcmp0(pictures_dir, home_dir))
		? pictures_dir : fallback_pictures;
	if (g_file_test(actual_pictures, G_FILE_TEST_IS_DIR))
	{
		GFile* f = g_file_new_for_path(actual_pictures);
		char* uri = g_file_get_uri(f);
		const char* icon = QuiverUtils::GetSpecialFolderSymbolicIconName(uri);
		add_sc(uri, "Pictures", icon ? icon : "folder-pictures-symbolic");
		g_free(uri);
		g_object_unref(f);
	}
	g_free(fallback_pictures);

	// Videos
	const char* videos_dir = g_get_user_special_dir(G_USER_DIRECTORY_VIDEOS);
	char* fallback_videos = g_build_filename(home_dir, "Videos", NULL);
	const char* actual_videos = (videos_dir && videos_dir[0] && 0 != g_strcmp0(videos_dir, home_dir))
		? videos_dir : fallback_videos;
	if (g_file_test(actual_videos, G_FILE_TEST_IS_DIR))
	{
		GFile* f = g_file_new_for_path(actual_videos);
		char* uri = g_file_get_uri(f);
		const char* icon = QuiverUtils::GetSpecialFolderSymbolicIconName(uri);
		add_sc(uri, "Videos", icon ? icon : "folder-videos-symbolic");
		g_free(uri);
		g_object_unref(f);
	}
	g_free(fallback_videos);

	// Trash (gvfs trash:/// — enumerate works when gvfs is running)
	add_sc("trash:///", "Trash", "user-trash-symbolic");

	return;
}

void FolderTree::FolderTreeImpl::PopulateBookmarksModel(GListStore *store)
{
	if (!store)
		return;
	try {
		BookmarksPtr bmPtr = Bookmarks::GetInstance();
		if (!bmPtr)
			return;
		std::vector<Bookmark> bms = bmPtr->GetBookmarks();
		int order = 0;
		for (const auto& bm : bms)
		{
			if (bm.GetURIs().empty()) continue;
			std::string bm_uri = bm.GetURIs().front();
			const char* icon = QuiverUtils::GetSpecialFolderSymbolicIconName(bm_uri.c_str());
			std::string icon_name = icon ? icon : (!bm.GetIcon().empty() ? bm.GetIcon() : "folder-symbolic");
			DirItem* item = dir_item_new(bm_uri.c_str(), bm.GetName().c_str(), icon_name.c_str(), FALSE, order++, 0);
			item->bookmark_id = bm.GetID();
			item->row_kind = ROW_KIND_BOOKMARK;
			g_list_store_append(store, G_OBJECT(item));
			g_object_unref(item);
		}
	} catch (...) {}
}

guint FolderTree::FolderTreeImpl::GetSlotFlattenOffset(guint slot_idx) const
{
	if (!m_pSectionsStore)
		return 0;
	guint offset = 0;
	guint n_sections = g_list_model_get_n_items(G_LIST_MODEL(m_pSectionsStore));
	for (guint i = 0; i < slot_idx && i < n_sections; i++)
	{
		GListModel* m = G_LIST_MODEL(g_list_model_get_item(G_LIST_MODEL(m_pSectionsStore), i));
		if (m)
		{
			offset += g_list_model_get_n_items(m);
			g_object_unref(m);
		}
	}
	return offset;
}

guint FolderTree::FolderTreeImpl::GetTreeFlattenOffset() const
{
	return GetSlotFlattenOffset(5);
}

void FolderTree::FolderTreeImpl::UpdateSectionsModel()
{
	if (!m_pSectionsStore)
		return;

	bool has_bookmarks = m_pBookmarkStore && g_list_model_get_n_items(G_LIST_MODEL(m_pBookmarkStore)) > 0;

	GListModel* sc_hdr = G_LIST_MODEL(m_pShortcutsHeaderStore);
	GListModel* sc_model = m_bShortcutsExpanded ? G_LIST_MODEL(m_pShortcutsStore) : G_LIST_MODEL(m_pEmptyStore);
	GListModel* bm_hdr_model = has_bookmarks ? G_LIST_MODEL(m_pBookmarksHeaderStore) : G_LIST_MODEL(m_pEmptyStore);
	GListModel* bm_model = (has_bookmarks && m_bBookmarksExpanded) ? G_LIST_MODEL(m_pBookmarkStore) : G_LIST_MODEL(m_pEmptyStore);
	GListModel* fld_hdr = G_LIST_MODEL(m_pFoldersHeaderStore);
	GListModel* tree_model = (m_bFoldersExpanded && m_pTreeListModel) ? G_LIST_MODEL(m_pTreeListModel) : G_LIST_MODEL(m_pEmptyStore);

	if (g_list_model_get_n_items(G_LIST_MODEL(m_pSectionsStore)) < 6)
	{
		g_list_store_remove_all(m_pSectionsStore);
		g_list_store_append(m_pSectionsStore, sc_hdr);
		g_list_store_append(m_pSectionsStore, sc_model);
		g_list_store_append(m_pSectionsStore, bm_hdr_model);
		g_list_store_append(m_pSectionsStore, bm_model);
		g_list_store_append(m_pSectionsStore, fld_hdr);
		g_list_store_append(m_pSectionsStore, tree_model);
		return;
	}

	GListModel* current_1 = G_LIST_MODEL(g_list_model_get_item(G_LIST_MODEL(m_pSectionsStore), 1));
	if (current_1 != sc_model)
	{
		g_list_store_splice(m_pSectionsStore, 1, 1, (gpointer*)&sc_model, 1);
	}
	if (current_1) g_object_unref(current_1);

	GListModel* current_2 = G_LIST_MODEL(g_list_model_get_item(G_LIST_MODEL(m_pSectionsStore), 2));
	if (current_2 != bm_hdr_model)
	{
		g_list_store_splice(m_pSectionsStore, 2, 1, (gpointer*)&bm_hdr_model, 1);
	}
	if (current_2) g_object_unref(current_2);

	GListModel* current_3 = G_LIST_MODEL(g_list_model_get_item(G_LIST_MODEL(m_pSectionsStore), 3));
	if (current_3 != bm_model)
	{
		g_list_store_splice(m_pSectionsStore, 3, 1, (gpointer*)&bm_model, 1);
	}
	if (current_3) g_object_unref(current_3);

	GListModel* current_5 = G_LIST_MODEL(g_list_model_get_item(G_LIST_MODEL(m_pSectionsStore), 5));
	if (current_5 != tree_model)
	{
		g_list_store_splice(m_pSectionsStore, 5, 1, (gpointer*)&tree_model, 1);
	}
	if (current_5) g_object_unref(current_5);
}

void FolderTree::FolderTreeImpl::ScrollToTreePosition(guint pos, GtkListScrollFlags flags)
{
	if (!m_pListView || !m_pTreeListModel)
		return;

	guint n_items = g_list_model_get_n_items(G_LIST_MODEL(m_pTreeListModel));
	if (pos >= n_items)
		return;

	guint flat_pos = GetTreeFlattenOffset() + pos;
	GtkScrollInfo* sinfo = gtk_scroll_info_new();
	gtk_scroll_info_set_enable_vertical(sinfo, TRUE);
	gtk_list_view_scroll_to(m_pListView, flat_pos, flags, sinfo);
}

void FolderTree::FolderTreeImpl::UpdateBookmarkSectionVisibility()
{
	UpdateSectionsModel();
}

void FolderTree::FolderTreeImpl::ReloadShortcuts()
{
	if (!m_pShortcutsStore)
		return;

	std::set<std::string> checked_uris;
	guint n = g_list_model_get_n_items(G_LIST_MODEL(m_pShortcutsStore));
	for (guint i = 0; i < n; i++)
	{
		DirItem* it = DIR_ITEM(g_list_model_get_item(G_LIST_MODEL(m_pShortcutsStore), i));
		if (it && it->checked && it->uri)
			checked_uris.insert(it->uri);
		if (it) g_object_unref(it);
	}

	if (m_pTreeListModel)
	{
		guint tn = g_list_model_get_n_items(G_LIST_MODEL(m_pTreeListModel));
		for (guint i = 0; i < tn; i++)
		{
			GtkTreeListRow* row = gtk_tree_list_model_get_row(m_pTreeListModel, i);
			if (!row) continue;
			DirItem* it = DIR_ITEM(gtk_tree_list_row_get_item(row));
			if (it && it->checked && it->uri)
				checked_uris.insert(it->uri);
		}
	}

	/* Bookmarks live in their own store/section; preserve their checked state
	 * across a reload just like the shortcuts. */
	std::set<std::string> bm_checked_uris;
	if (m_pBookmarkStore)
	{
		guint bn = g_list_model_get_n_items(G_LIST_MODEL(m_pBookmarkStore));
		for (guint i = 0; i < bn; i++)
		{
			DirItem* it = DIR_ITEM(g_list_model_get_item(G_LIST_MODEL(m_pBookmarkStore), i));
			if (it && it->checked && it->uri)
				bm_checked_uris.insert(it->uri);
			if (it) g_object_unref(it);
		}
	}

	g_list_store_remove_all(m_pShortcutsStore);
	PopulateShortcutsModel(m_pShortcutsStore);

	if (m_pBookmarkStore && m_pBookmarkSelectionModel)
	{
		g_list_store_remove_all(m_pBookmarkStore);
		PopulateBookmarksModel(m_pBookmarkStore);
		gtk_selection_model_unselect_all(GTK_SELECTION_MODEL(m_pBookmarkSelectionModel));
		guint bn = g_list_model_get_n_items(G_LIST_MODEL(m_pBookmarkStore));
		for (guint i = 0; i < bn; i++)
		{
			DirItem* it = DIR_ITEM(g_list_model_get_item(G_LIST_MODEL(m_pBookmarkStore), i));
			if (it && it->uri && bm_checked_uris.count(it->uri))
			{
				dir_item_set_checked(it, TRUE);
				gtk_selection_model_select_item(GTK_SELECTION_MODEL(m_pBookmarkSelectionModel), i, FALSE);
			}
			if (it) g_object_unref(it);
		}
		UpdateBookmarkSectionVisibility();
	}

	n = g_list_model_get_n_items(G_LIST_MODEL(m_pShortcutsStore));
	for (guint i = 0; i < n; i++)
	{
		DirItem* it = DIR_ITEM(g_list_model_get_item(G_LIST_MODEL(m_pShortcutsStore), i));
		if (it && it->uri && checked_uris.count(it->uri))
		{
			dir_item_set_checked(it, TRUE);
			if (m_pShortcutsSelectionModel)
				gtk_selection_model_select_item(GTK_SELECTION_MODEL(m_pShortcutsSelectionModel), i, FALSE);
		}
		if (it) g_object_unref(it);
	}

	UpdateSectionsModel();
}

void FolderTree::FolderTreeImpl::PopulateTreeModel(GListStore *roots)
{
	int iNodeOrder = 0;

	const char* home_dir = g_get_home_dir();
	GFile* file_home = g_file_new_for_path(home_dir);
	GFile* file_root = g_file_new_for_uri("file:///");

	// home folder
	const char* name = g_get_real_name();
	gchar home_name[256];
	if (0 == strcmp(name,"Unknown"))
	{
		strcpy(home_name,"Home");
	}
	else
	{
		// get the first name
		gchar** tokens = g_strsplit(name," ",2);
		g_snprintf(home_name,256,"%s's Home",tokens[0]);
		g_strfreev(tokens);
	}

	gchar* icon = folder_tree_get_icon_name(file_home);
	gchar* uri = g_file_get_uri(file_home);
	g_list_store_append(roots,
		G_OBJECT(dir_item_new(uri, home_name, icon, TRUE, iNodeOrder++, 0)));
	g_free(uri);
	g_free(icon);

	// other mounts (filesystem root)
	GMount* root_mount = g_file_find_enclosing_mount(file_root, NULL, NULL);
	gchar* root_icon_name = NULL;
	if (NULL != root_mount)
	{
		GIcon* root_icon = g_mount_get_icon(root_mount);
		if (G_IS_THEMED_ICON(root_icon))
		{
			const gchar* const* names = g_themed_icon_get_names(G_THEMED_ICON(root_icon));
			if (NULL != names && NULL != names[0])
				root_icon_name = g_strdup(names[0]);
		}
		if (NULL != root_icon)
			g_object_unref(root_icon);
		g_object_unref(root_mount);
	}
	if (NULL == root_icon_name)
	{
		root_icon_name = g_strdup("drive-harddisk");
	}

	char* root_uri = g_file_get_uri(file_root);
	g_list_store_append(roots,
		G_OBJECT(dir_item_new(root_uri, QUIVER_FOLDER_TREE_ROOT_NAME,
			root_icon_name, TRUE, iNodeOrder++, 0)));
	g_free(root_uri);
	g_free(root_icon_name);

	GVolumeMonitor* monitor = g_volume_monitor_get();
	GList *mounts = g_volume_monitor_get_mounts(monitor);
	GList *mount_itr = mounts;
	while (NULL != mount_itr)
	{
		GMount *mount = G_MOUNT(mount_itr->data);
		if (NULL != mount)
		{
			GFile* mount_root = g_mount_get_root(mount);
			char* mname = g_mount_get_name(mount);

			GIcon* micon = g_mount_get_icon(mount);
			gchar* micon_name = NULL;
			if (G_IS_THEMED_ICON(micon))
			{
				const gchar* const* names = g_themed_icon_get_names(G_THEMED_ICON(micon));
				if (NULL != names && NULL != names[0])
					micon_name = g_strdup(names[0]);
			}
			char* muri = g_file_get_uri(mount_root);

			g_list_store_append(roots,
				G_OBJECT(dir_item_new(muri, mname, micon_name, TRUE, iNodeOrder++, 0)));

			g_free(muri);
			if (NULL != micon_name)
				g_free(micon_name);
			if (NULL != micon)
				g_object_unref(micon);
			if (NULL != mname)
				g_free(mname);
			g_object_unref(mount_root);
			g_object_unref(mount);
		}
		mount_itr = g_list_next(mount_itr);
	}
	g_list_free(mounts);

	g_object_unref(file_home);
	g_object_unref(file_root);
}


gchar* folder_tree_get_icon_name_from_info(GFileInfo* info, GFile* gfile)
{
	if (NULL != gfile)
	{
		const char* special_icon = QuiverUtils::GetSpecialFolderIconName(gfile);
		if (NULL != special_icon)
		{
			return g_strdup(special_icon);
		}
	}

	gchar* icon_name = NULL;
	if (NULL != info)
	{
		GIcon* icon = g_file_info_get_icon(info);
		if (G_IS_THEMED_ICON(icon))
		{
			const gchar* const* names = g_themed_icon_get_names(G_THEMED_ICON(icon));
			if (NULL != names && NULL != names[0])
				icon_name = g_strdup(names[0]);
		}
		else if (NULL != icon)
		{
			gchar* s = g_icon_to_string(icon);
			if (NULL != s)
			{
				icon_name = s;
			}
		}
	}
	if (NULL == icon_name)
	{
		icon_name = g_strdup("folder");
	}
	return icon_name;
}

gchar* folder_tree_get_icon_name(GFile* gfile)
{
	if (NULL == gfile)
		return g_strdup("folder");

	const char* special_icon = QuiverUtils::GetSpecialFolderIconName(gfile);
	if (NULL != special_icon)
	{
		return g_strdup(special_icon);
	}

	GFileInfo* info = g_file_query_info(
		gfile,
		G_FILE_ATTRIBUTE_STANDARD_ICON,
		G_FILE_QUERY_INFO_NONE,
		NULL,
		NULL);
	gchar* icon_name = folder_tree_get_icon_name_from_info(info, gfile);
	if (NULL != info)
		g_object_unref(info);
	return icon_name;
}
