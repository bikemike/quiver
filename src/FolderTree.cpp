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
#include "QuiverStockIcons.h"
#include "QuiverUtils.h"
#include "Bookmarks.h"
#include "IBookmarksEventHandler.h"

#define QUIVER_TREE_COLUMN_TOGGLE      "column_toggle"
#define QUIVER_FOLDER_TREE_ROOT_NAME   "Filesystem"

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
	return item;
}

// prototype
static void view_onButtonPressed (GtkGestureClick *gesture, int n_press, double x, double y, gpointer userdata);
static void view_onButtonReleased (GtkGestureClick *gesture, int n_press, double x, double y, gpointer userdata);
static gboolean view_on_key_press (GtkEventControllerKey *controller, guint keyval, guint keycode, GdkModifierType state, gpointer userdata);
static void view_popup_menu_at (GtkWidget *treeview, gdouble x, gdouble y, gpointer userdata);
static void signal_check_selected (GtkWidget *menuitem, gpointer userdata);
static void signal_uncheck_selected (GtkWidget *menuitem, gpointer userdata);
static guint folder_tree_get_focused_position(FolderTree::FolderTreeImpl* impl);
static guint shortcuts_get_focused_position(FolderTree::FolderTreeImpl* impl);
static void folder_tree_set_checkbox_for_selected(FolderTree::FolderTreeImpl* impl, gboolean value);
static void shortcut_row_on_clicked(GtkGestureClick* gesture, int n_press, double x, double y);
static gboolean shortcuts_on_key_press(GtkEventControllerKey *controller, guint keyval, guint keycode, GdkModifierType state, gpointer userdata);

static gchar* folder_tree_get_icon_name(GFile* gfile);

class FolderTree::FolderTreeImpl
{
public:
// constructor / destructor
	FolderTreeImpl(FolderTree *pFolderTree);
	~FolderTreeImpl();


// methods
	void CreateWidget();
	void PopulateShortcutsModel(GListStore *store);
	void ReloadShortcuts();
	void PopulateTreeModel(GListStore *roots);

	void SetSelectedFolders(std::list<std::string> &uris);
	std::list<std::string> GetSelectedFolders() const;
	void ClearAllCheckboxes();
	void SetCheckboxForItem(DirItem* item, gboolean value);
	guint FindItemPosition(DirItem* target);
	guint FindItemByURI(const gchar* uri, gboolean check_only);
	DirItem* FindRootForPath(const gchar* uri);
	void ExpandItem(DirItem* item);

	void SyncTreeSelectionForURI(const gchar* uri, gboolean value);
	void SyncShortcutSelectionForURI(const gchar* uri, gboolean value);

// member variables
	GtkWidget*       m_pWidget;
	GtkWidget*       m_pMenuPopover;
	FolderTree*      m_pFolderTree;

	// Shortcuts
	GListStore*        m_pShortcutsStore;
	GtkMultiSelection* m_pShortcutsSelectionModel;
	GtkListView*       m_pShortcutsListView;
	GtkWidget*         m_pSeparator;

	// Folder Tree
	GListStore*        m_pListStoreRoots;
	GtkTreeListModel*  m_pTreeListModel;
	GtkMultiSelection* m_pSelectionModel;
	GtkListView*       m_pListView;

	gchar*             m_pScrollToURI;
	IBookmarksEventHandlerPtr m_pBookmarksEventHandler;
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

std::list<std::string> FolderTree::GetSelectedFolders() const
{
	return m_FolderTreeImplPtr->GetSelectedFolders();	
}

void FolderTree::SetSelectedFolders(std::list<std::string> &uris)
{
	m_FolderTreeImplPtr->SetSelectedFolders(uris);
}



FolderTree::FolderTreeImpl::FolderTreeImpl(FolderTree *parent)
{
	m_pFolderTree = parent;
	
	m_pWidget = NULL;
	m_pMenuPopover = NULL;
	m_pScrollToURI = NULL;
	m_pShortcutsStore = NULL;
	m_pShortcutsSelectionModel = NULL;
	m_pShortcutsListView = NULL;
	m_pSeparator = NULL;
	m_pListStoreRoots = NULL;
	m_pTreeListModel = NULL;
	m_pSelectionModel = NULL;
	m_pListView = NULL;

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
	try {
		BookmarksPtr bmPtr = Bookmarks::GetInstance();
		if (bmPtr && m_pBookmarksEventHandler)
		{
			bmPtr->RemoveEventHandler(m_pBookmarksEventHandler);
		}
	} catch (...) {}
	m_pBookmarksEventHandler.reset();

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

	if (NULL != m_pMenuPopover && gtk_widget_get_parent(m_pMenuPopover) != NULL)
	{
		gtk_widget_unparent(m_pMenuPopover);
	}
	m_pMenuPopover = NULL;

	m_pShortcutsSelectionModel = NULL;
	m_pShortcutsStore = NULL;
	m_pShortcutsListView = NULL;

	m_pSelectionModel = NULL;
	m_pTreeListModel = NULL;
	m_pListStoreRoots = NULL;
	m_pListView = NULL;
	m_pWidget = NULL;
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

	if (m_pTreeListModel)
	{
		guint n = g_list_model_get_n_items(G_LIST_MODEL(m_pTreeListModel));
		for (guint i = 0 ; i < n ; i++)
		{
			GtkTreeListRow* row = gtk_tree_list_model_get_row(m_pTreeListModel, i);
			if (NULL == row)
				continue;
			DirItem* item = DIR_ITEM(gtk_tree_list_row_get_item(row));
			if (item && item->checked && item->uri)
			{
				if (seen.insert(item->uri).second)
					listSelectedFolders.push_back(item->uri);
			}
		}
	}

	return listSelectedFolders;
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
		if (m_pSelectionModel)
			gtk_selection_model_unselect_all(GTK_SELECTION_MODEL(m_pSelectionModel));
	}
}

void FolderTree::FolderTreeImpl::SyncShortcutSelectionForURI(const gchar* uri, gboolean value)
{
	if (!uri || !m_pShortcutsStore)
		return;

	guint n = g_list_model_get_n_items(G_LIST_MODEL(m_pShortcutsStore));
	for (guint i = 0; i < n; i++)
	{
		DirItem* it = DIR_ITEM(g_list_model_get_item(G_LIST_MODEL(m_pShortcutsStore), i));
		if (it)
		{
			if (it->uri && 0 == g_strcmp0(it->uri, uri))
			{
				dir_item_set_checked(it, value);
			}
			g_object_unref(it);
		}
	}
}

void FolderTree::FolderTreeImpl::SyncTreeSelectionForURI(const gchar* uri, gboolean value)
{
	if (!uri || !m_pTreeListModel || !m_pListStoreRoots)
		return;

	DirItem* root = FindRootForPath(uri);
	if (!root)
		return;

	if (root->uri && 0 == g_strcmp0(root->uri, uri))
	{
		dir_item_set_checked(root, value);
		return;
	}

	DirItem* current = root;
	ExpandItem(current);

	gchar* remaining = g_strdup(uri);
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
			done = TRUE;
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
	if (m_pSelectionModel)
	{
		guint pos = FindItemPosition(item);
		if (pos != G_MAXUINT)
		{
			if (value)
				gtk_selection_model_select_item(GTK_SELECTION_MODEL(m_pSelectionModel), pos, FALSE);
			else
				gtk_selection_model_unselect_item(GTK_SELECTION_MODEL(m_pSelectionModel), pos);
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
		if (item == target)
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
	}
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

void  FolderTree::FolderTreeImpl::SetSelectedFolders(std::list<std::string> &uris)
{
	ClearAllCheckboxes();

	guint first_found = G_MAXUINT;

	std::list<std::string>::iterator itr;
	for (itr = uris.begin(); uris.end() != itr; ++itr)
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
				done = TRUE;
				break;
			}

			gchar* base = g_strdup(current->uri);
			GFile* cur_file = g_file_new_for_uri(base);
			GFile* tgt_file = g_file_new_for_uri(remaining);

			DirItem* next = NULL;
			guint child_count = g_list_model_get_n_items(children);
			for (guint c = 0 ; c < child_count ; c++)
			{
				DirItem* child = DIR_ITEM(g_list_model_get_item(children, c));
				DirItem* child_copy = dir_item_new(child->uri, child->display_name, child->icon_name, child->permanent, child->node_order, child->node_depth);
				g_object_unref(child);

				GFile* child_file = g_file_new_for_uri(child_copy->uri);
				if (g_file_has_prefix(tgt_file, child_file))
				{
					next = child_copy;
					// do not unref: returning ownership
					g_object_unref(child_file);
					break;
				}
				g_object_unref(child_file);
				g_object_unref(child_copy);
			}
			g_object_unref(cur_file);
			g_object_unref(tgt_file);
			g_free(base);

			if (NULL != next)
			{
				if (0 == g_ascii_strcasecmp(next->uri, itr->c_str())
					|| 0 == g_strcmp0(next->uri, itr->c_str()))
				{
					SetCheckboxForItem(next, TRUE);
					guint fpos = FindItemPosition(next);
					if (G_MAXUINT == first_found)
						first_found = fpos;
					g_object_unref(next);
					done = TRUE;
				}
				else
				{
					g_object_unref(current);
					current = next;
					ExpandItem(current);
					g_free(remaining);
					remaining = g_strdup(next->uri);
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
		gtk_list_view_scroll_to(m_pListView,
			first_found, GTK_LIST_SCROLL_NONE, NULL);
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
			G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME,
			G_FILE_QUERY_INFO_NONE,
			NULL,
			NULL);
		if (NULL != enumerator)
		{
			GFileInfo* info = NULL;
			GList* listed = NULL;
			while (NULL != (info = g_file_enumerator_next_file(enumerator, NULL, NULL)))
			{
				if (G_FILE_TYPE_DIRECTORY == g_file_info_get_file_type(info))
				{
					GFile* child_file = g_file_get_child(file, g_file_info_get_name(info));
					gchar* child_uri = g_file_get_uri(child_file);
					gchar* display = g_strdup(g_file_info_get_display_name(info));
					gchar* icon = folder_tree_get_icon_name(child_file);
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
	if (!impl || !impl->m_pShortcutsListView)
		return G_MAXUINT;

	GtkWidget* lv = GTK_WIDGET(impl->m_pShortcutsListView);
	GtkRoot* root = gtk_widget_get_root(lv);
	GtkWidget* f = root ? gtk_root_get_focus(root) : NULL;
	if (!f)
		f = gtk_widget_get_focus_child(lv);

	for (GtkWidget* w = f; w && w != lv; w = gtk_widget_get_parent(w))
	{
		GtkListItem* li = static_cast<GtkListItem*>(
			g_object_get_data(G_OBJECT(w), "list-item"));
		if (li)
			return gtk_list_item_get_position(li);
	}

	GtkWidget* fc = gtk_widget_get_focus_child(lv);
	if (fc)
	{
		for (GtkWidget* ch = gtk_widget_get_first_child(fc); ch; ch = gtk_widget_get_next_sibling(ch))
		{
			GtkListItem* li = static_cast<GtkListItem*>(
				g_object_get_data(G_OBJECT(ch), "list-item"));
			if (li)
				return gtk_list_item_get_position(li);
		}
	}

	// Fallback to first selected item in selection model
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
	(void)n_press; (void)x; (void)y;
	GtkWidget* w = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
	FolderTree::FolderTreeImpl* impl = static_cast<FolderTree::FolderTreeImpl*>(
		g_object_get_data(G_OBJECT(w), "sc-impl"));
	DirItem* item = static_cast<DirItem*>(
		g_object_get_data(G_OBJECT(w), "sc-item"));
	guint pos = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(w), "sc-pos"));
	if (!impl || !item) return;

	guint button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));
	GdkModifierType state = gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(gesture));

	if (button == 1)
	{
		if (state & GDK_CONTROL_MASK)
		{
			gboolean new_val = !item->checked;
			dir_item_set_checked(item, new_val);
			if (impl->m_pShortcutsSelectionModel && pos != G_MAXUINT)
			{
				if (new_val)
					gtk_selection_model_select_item(GTK_SELECTION_MODEL(impl->m_pShortcutsSelectionModel), pos, FALSE);
				else
					gtk_selection_model_unselect_item(GTK_SELECTION_MODEL(impl->m_pShortcutsSelectionModel), pos);
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
		}
		else
		{
			impl->ClearAllCheckboxes();
			dir_item_set_checked(item, TRUE);
			if (impl->m_pShortcutsSelectionModel && pos != G_MAXUINT)
			{
				gtk_selection_model_select_item(GTK_SELECTION_MODEL(impl->m_pShortcutsSelectionModel), pos, TRUE);
			}
			impl->SyncTreeSelectionForURI(item->uri, TRUE);
		}
		gtk_widget_grab_focus(w);
		impl->m_pFolderTree->EmitSelectionChangedEvent();
	}
	else if (button == 2)
	{
		gboolean new_val = !item->checked;
		dir_item_set_checked(item, new_val);
		if (impl->m_pShortcutsSelectionModel && pos != G_MAXUINT)
		{
			if (new_val)
				gtk_selection_model_select_item(GTK_SELECTION_MODEL(impl->m_pShortcutsSelectionModel), pos, FALSE);
			else
				gtk_selection_model_unselect_item(GTK_SELECTION_MODEL(impl->m_pShortcutsSelectionModel), pos);
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
		view_popup_menu_at(impl->m_pWidget, -1, -1, impl);
	}
}

static gboolean shortcuts_on_key_press(GtkEventControllerKey *controller, guint keyval, guint keycode, GdkModifierType state, gpointer userdata)
{
	(void)keycode; (void)controller;
	FolderTree::FolderTreeImpl* pFolderTreeImpl = (FolderTree::FolderTreeImpl*)userdata;
	if (!pFolderTreeImpl || !pFolderTreeImpl->m_pShortcutsStore || !pFolderTreeImpl->m_pShortcutsSelectionModel)
		return FALSE;

	if (GDK_KEY_Menu == keyval)
	{
		view_popup_menu_at(pFolderTreeImpl->m_pWidget, -1, -1, userdata);
		return TRUE;
	}

	guint n = g_list_model_get_n_items(G_LIST_MODEL(pFolderTreeImpl->m_pShortcutsStore));
	if (n == 0) return FALSE;

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
			/* Spacebar on selection (single or multi-select):
			 * Check all selected items if any are unchecked; otherwise uncheck all selected items. */
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
					pFolderTreeImpl->m_pFolderTree->EmitSelectionChangedEvent();
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
	FolderTree::FolderTreeImpl* impl = static_cast<FolderTree::FolderTreeImpl*>(
		g_object_get_data(G_OBJECT(w), "dir-impl"));
	DirItem* item = static_cast<DirItem*>(
		g_object_get_data(G_OBJECT(w), "dir-item"));
	GtkTreeListRow* row = static_cast<GtkTreeListRow*>(
		g_object_get_data(G_OBJECT(w), "dir-row"));
	if (!impl || !item) return;

	// If the click is on the expander arrow of an expandable row, let GtkTreeExpander handle it
	GtkWidget* target = gtk_widget_pick(w, x, y, GTK_PICK_DEFAULT);
	if (target != NULL && g_strcmp0(G_OBJECT_TYPE_NAME(target), "GtkBuiltinIcon") == 0 &&
	    row != NULL && gtk_tree_list_row_is_expandable(row))
	{
		return;
	}

	guint button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));
	GdkModifierType state = gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(gesture));
	guint pos = row ? gtk_tree_list_row_get_position(row) : G_MAXUINT;

	if (button == 1)
	{
		if (state & GDK_CONTROL_MASK)
		{
			// Ctrl pressed: adds/subtracts (toggle)
			gboolean new_val = !item->checked;
			dir_item_set_checked(item, new_val);
			if (pos != G_MAXUINT && impl->m_pSelectionModel)
			{
				if (new_val)
					gtk_selection_model_select_item(GTK_SELECTION_MODEL(impl->m_pSelectionModel), pos, FALSE);
				else
					gtk_selection_model_unselect_item(GTK_SELECTION_MODEL(impl->m_pSelectionModel), pos);
			}
			impl->SyncShortcutSelectionForURI(item->uri, new_val);
		}
		else if (state & GDK_SHIFT_MASK)
		{
			// Shift pressed: multi-selection range click
			guint start = folder_tree_get_focused_position(impl);
			if (start == G_MAXUINT)
				start = pos;
			guint min_pos = std::min(start, pos);
			guint max_pos = std::max(start, pos);
			guint count = max_pos - min_pos + 1;
			if (pos != G_MAXUINT && impl->m_pSelectionModel)
			{
				gtk_selection_model_select_range(GTK_SELECTION_MODEL(impl->m_pSelectionModel), min_pos, count, TRUE);
			}
		}
		else
		{
			// Plain click: check this item and uncheck all others
			impl->ClearAllCheckboxes();
			dir_item_set_checked(item, TRUE);
			if (pos != G_MAXUINT && impl->m_pSelectionModel)
			{
				gtk_selection_model_select_item(GTK_SELECTION_MODEL(impl->m_pSelectionModel), pos, TRUE);
			}
			impl->SyncShortcutSelectionForURI(item->uri, TRUE);
		}
		if (n_press == 2 && row && gtk_tree_list_row_is_expandable(row))
		{
			gtk_tree_list_row_set_expanded(row, !gtk_tree_list_row_get_expanded(row));
		}
		gtk_widget_grab_focus(w);
		impl->m_pFolderTree->EmitSelectionChangedEvent();
	}
	else if (button == 2)
	{
		// Middle click: toggle
		gboolean new_val = !item->checked;
		dir_item_set_checked(item, new_val);
		if (pos != G_MAXUINT && impl->m_pSelectionModel)
		{
			if (new_val)
				gtk_selection_model_select_item(GTK_SELECTION_MODEL(impl->m_pSelectionModel), pos, FALSE);
			else
				gtk_selection_model_unselect_item(GTK_SELECTION_MODEL(impl->m_pSelectionModel), pos);
		}
		impl->SyncShortcutSelectionForURI(item->uri, new_val);
		gtk_widget_grab_focus(w);
		impl->m_pFolderTree->EmitSelectionChangedEvent();
	}
	else if (button == 3)
	{
		if (pos != G_MAXUINT && impl->m_pSelectionModel &&
		    !gtk_selection_model_is_selected(GTK_SELECTION_MODEL(impl->m_pSelectionModel), pos))
		{
			gtk_selection_model_select_item(GTK_SELECTION_MODEL(impl->m_pSelectionModel), pos, TRUE);
		}
		view_popup_menu_at(impl->m_pWidget, -1, -1, impl);
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
			"    border-radius: 6px;\n"
			"    margin: 1px 6px;\n"
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
			"    min-height: 34px;\n"
			"    padding: 2px 6px;\n"
			"    border-radius: 6px;\n"
			"}\n"
			".compact-tree row {\n"
			"    padding: 0 4px;\n"
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
			"    padding: 0;\n"
			"}\n"
		);
		gtk_style_context_add_provider_for_display(gdk_display_get_default(),
			GTK_STYLE_PROVIDER(cssProvider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
		g_object_unref(cssProvider);
	}

	// --- Shortcuts list view factory ---
	GtkListItemFactory* sc_factory = gtk_signal_list_item_factory_new();

	g_signal_connect(sc_factory, "setup", G_CALLBACK(+[](GtkListItemFactory* fact, GtkListItem* list_item) {
		(void)fact;
		// build: hbox [ check, row_box[ image, label ] ]
		GtkWidget* hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
		gtk_widget_add_css_class(hbox, "sidebar-row");

		GtkWidget* check = gtk_check_button_new();
		gtk_widget_set_margin_start(check, 4);
		gtk_widget_set_margin_end(check, 6);
		gtk_widget_set_valign(check, GTK_ALIGN_CENTER);
		g_signal_connect(check, "toggled", G_CALLBACK(+[](GtkWidget* w, gpointer) {
			FolderTree::FolderTreeImpl* impl =
				static_cast<FolderTree::FolderTreeImpl*>(
					g_object_get_data(G_OBJECT(w), "sc-impl"));
			DirItem* item = static_cast<DirItem*>(
				g_object_get_data(G_OBJECT(w), "sc-item"));
			if (NULL != impl && NULL != item &&
				!g_object_get_data(G_OBJECT(w), "set-active-guard"))
			{
				gboolean active = gtk_check_button_get_active(GTK_CHECK_BUTTON(w));
				if (item->checked != active)
				{
					dir_item_set_checked(item, active);
					guint pos = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(w), "sc-pos"));
					if (impl->m_pShortcutsSelectionModel && pos != G_MAXUINT)
					{
						if (active)
							gtk_selection_model_select_item(GTK_SELECTION_MODEL(impl->m_pShortcutsSelectionModel), pos, FALSE);
						else
							gtk_selection_model_unselect_item(GTK_SELECTION_MODEL(impl->m_pShortcutsSelectionModel), pos);
					}
					impl->SyncTreeSelectionForURI(item->uri, active);
					impl->m_pFolderTree->EmitSelectionChangedEvent();
				}
			}
		}), NULL);
		gtk_box_append(GTK_BOX(hbox), check);

		GtkWidget* row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
		gtk_widget_set_hexpand(row_box, TRUE);
		gtk_widget_set_valign(row_box, GTK_ALIGN_CENTER);
		GtkWidget* image = gtk_image_new();
		gtk_image_set_icon_size(GTK_IMAGE(image), GTK_ICON_SIZE_NORMAL);
		gtk_widget_set_valign(image, GTK_ALIGN_CENTER);
		GtkWidget* label = gtk_label_new(NULL);
		gtk_label_set_xalign(GTK_LABEL(label), 0.0);
		gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
		gtk_widget_set_hexpand(label, TRUE);
		gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
		gtk_box_append(GTK_BOX(row_box), image);
		gtk_box_append(GTK_BOX(row_box), label);
		gtk_box_append(GTK_BOX(hbox), row_box);

		GtkGesture* row_click = gtk_gesture_click_new();
		gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(row_click), 0);
		g_signal_connect(row_click, "pressed", G_CALLBACK(+[](GtkGestureClick* gesture, int n_press, double x, double y, gpointer) {
			g_object_set_data(G_OBJECT(gesture), "press-handled", GINT_TO_POINTER(1));
			shortcut_row_on_clicked(gesture, n_press, x, y);
		}), NULL);
		g_signal_connect(row_click, "released", G_CALLBACK(+[](GtkGestureClick* gesture, int n_press, double x, double y, gpointer) {
			if (GPOINTER_TO_INT(g_object_get_data(G_OBJECT(gesture), "press-handled")))
			{
				g_object_set_data(G_OBJECT(gesture), "press-handled", GINT_TO_POINTER(0));
				return;
			}
			shortcut_row_on_clicked(gesture, n_press, x, y);
		}), NULL);
		gtk_widget_add_controller(row_box, GTK_EVENT_CONTROLLER(row_click));

		gtk_list_item_set_child(list_item, hbox);
		g_object_set_data(G_OBJECT(list_item), "sc-check", check);
		g_object_set_data(G_OBJECT(list_item), "sc-hbox", hbox);
		g_object_set_data(G_OBJECT(list_item), "sc-row-box", row_box);
		g_object_set_data(G_OBJECT(list_item), "sc-image", image);
		g_object_set_data(G_OBJECT(list_item), "sc-label", label);
	}), NULL);

	g_signal_connect(sc_factory, "bind", G_CALLBACK(+[](GtkListItemFactory* fact, GtkListItem* list_item, gpointer user_data) {
		(void)fact;
		FolderTree::FolderTreeImpl* impl =
			static_cast<FolderTree::FolderTreeImpl*>(user_data);
		DirItem* item = DIR_ITEM(gtk_list_item_get_item(list_item));
		guint pos = gtk_list_item_get_position(list_item);
		GtkWidget* check = GTK_WIDGET(g_object_get_data(G_OBJECT(list_item), "sc-check"));
		GtkWidget* hbox = GTK_WIDGET(g_object_get_data(G_OBJECT(list_item), "sc-hbox"));
		GtkWidget* row_box = GTK_WIDGET(g_object_get_data(G_OBJECT(list_item), "sc-row-box"));
		GtkWidget* image = GTK_WIDGET(g_object_get_data(G_OBJECT(list_item), "sc-image"));
		GtkWidget* label = GTK_WIDGET(g_object_get_data(G_OBJECT(list_item), "sc-label"));

		if (NULL != item->icon_name && '\0' != item->icon_name[0])
			gtk_image_set_from_icon_name(GTK_IMAGE(image), item->icon_name);
		else
			gtk_image_set_from_icon_name(GTK_IMAGE(image), "folder-symbolic");
		gtk_label_set_text(GTK_LABEL(label), item->display_name);

		g_object_set_data(G_OBJECT(check), "sc-item", item);
		g_object_set_data(G_OBJECT(check), "sc-impl", impl);
		g_object_set_data(G_OBJECT(check), "sc-pos", GUINT_TO_POINTER(pos));
		g_object_set_data(G_OBJECT(item), "bound-check", check);

		g_object_set_data(G_OBJECT(hbox), "sc-item", item);
		g_object_set_data(G_OBJECT(hbox), "sc-impl", impl);
		g_object_set_data(G_OBJECT(hbox), "sc-pos", GUINT_TO_POINTER(pos));
		g_object_set_data(G_OBJECT(hbox), "list-item", list_item);

		g_object_set_data(G_OBJECT(row_box), "sc-item", item);
		g_object_set_data(G_OBJECT(row_box), "sc-impl", impl);
		g_object_set_data(G_OBJECT(row_box), "sc-pos", GUINT_TO_POINTER(pos));

		if (!g_object_get_data(G_OBJECT(item), "checked-connected"))
		{
			g_object_set_data(G_OBJECT(item), "checked-connected", GINT_TO_POINTER(1));
			/* Keep the checkbox widget in sync when "checked" changes. */
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

		g_object_set_data(G_OBJECT(check), "set-active-guard", GINT_TO_POINTER(1));
		gtk_check_button_set_active(GTK_CHECK_BUTTON(check), item->checked);
		g_object_set_data(G_OBJECT(check), "set-active-guard", GINT_TO_POINTER(0));
	}), this);

	g_signal_connect(sc_factory, "unbind", G_CALLBACK(+[](GtkListItemFactory* fact, GtkListItem* list_item) {
		(void)fact;
		GtkWidget* check = GTK_WIDGET(g_object_get_data(G_OBJECT(list_item), "sc-check"));
		if (check)
		{
			DirItem* old_item = static_cast<DirItem*>(g_object_get_data(G_OBJECT(check), "sc-item"));
			if (old_item && g_object_get_data(G_OBJECT(old_item), "bound-check") == check)
				g_object_set_data(G_OBJECT(old_item), "bound-check", NULL);
			g_object_set_data(G_OBJECT(check), "sc-item", NULL);
			g_object_set_data(G_OBJECT(check), "sc-impl", NULL);
		}
		GtkWidget* hbox = GTK_WIDGET(g_object_get_data(G_OBJECT(list_item), "sc-hbox"));
		if (hbox)
		{
			g_object_set_data(G_OBJECT(hbox), "sc-item", NULL);
			g_object_set_data(G_OBJECT(hbox), "sc-impl", NULL);
			g_object_set_data(G_OBJECT(hbox), "list-item", NULL);
		}
		GtkWidget* row_box = GTK_WIDGET(g_object_get_data(G_OBJECT(list_item), "sc-row-box"));
		if (row_box)
		{
			g_object_set_data(G_OBJECT(row_box), "sc-item", NULL);
			g_object_set_data(G_OBJECT(row_box), "sc-impl", NULL);
		}
	}), NULL);

	m_pShortcutsStore = g_list_store_new(DIR_ITEM_TYPE);
	PopulateShortcutsModel(m_pShortcutsStore);
	m_pShortcutsSelectionModel = gtk_multi_selection_new(G_LIST_MODEL(m_pShortcutsStore));
	m_pShortcutsListView = GTK_LIST_VIEW(gtk_list_view_new(GTK_SELECTION_MODEL(m_pShortcutsSelectionModel), sc_factory));

	GtkEventController *sc_key_controller = gtk_event_controller_key_new();
	gtk_event_controller_set_propagation_phase(sc_key_controller, GTK_PHASE_CAPTURE);
	g_signal_connect(sc_key_controller, "key-pressed", G_CALLBACK(shortcuts_on_key_press), this);
	gtk_widget_add_controller(GTK_WIDGET(m_pShortcutsListView), sc_key_controller);

	GtkGesture *sc_gesture = gtk_gesture_click_new();
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(sc_gesture), 0);
	g_signal_connect(sc_gesture, "pressed", G_CALLBACK(view_onButtonPressed), this);
	gtk_widget_add_controller(GTK_WIDGET(m_pShortcutsListView), GTK_EVENT_CONTROLLER(sc_gesture));

	m_pSeparator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_widget_add_css_class(m_pSeparator, "sidebar-separator");
	gtk_widget_set_margin_top(m_pSeparator, 10);
	gtk_widget_set_margin_bottom(m_pSeparator, 10);
	gtk_widget_set_margin_start(m_pSeparator, 12);
	gtk_widget_set_margin_end(m_pSeparator, 12);

	// --- Folder tree list view factory ---
	GtkListItemFactory* factory = gtk_signal_list_item_factory_new();

	g_signal_connect(factory, "setup", G_CALLBACK(+[](GtkListItemFactory* fact, GtkListItem* list_item) {
		(void)fact;
		// build: hbox [ check, expander[ icon, label ] ]
		GtkWidget* hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
		gtk_widget_add_css_class(hbox, "compact-tree-row");

		GtkWidget* check = gtk_check_button_new();
		gtk_widget_set_margin_start(check, 4);
		gtk_widget_set_margin_end(check, 6);
		gtk_widget_set_valign(check, GTK_ALIGN_CENTER);
		g_signal_connect(check, "toggled", G_CALLBACK(+[](GtkWidget* w, gpointer) {
			FolderTree::FolderTreeImpl* impl =
				static_cast<FolderTree::FolderTreeImpl*>(
					g_object_get_data(G_OBJECT(w), "dir-impl"));
			DirItem* item = static_cast<DirItem*>(
				g_object_get_data(G_OBJECT(w), "dir-item"));
			GtkTreeListRow* row = static_cast<GtkTreeListRow*>(
				g_object_get_data(G_OBJECT(w), "dir-row"));
			if (NULL != impl && NULL != item &&
				!g_object_get_data(G_OBJECT(w), "set-active-guard"))
			{
				gboolean active = gtk_check_button_get_active(GTK_CHECK_BUTTON(w));
				if (item->checked != active)
				{
					dir_item_set_checked(item, active);
					if (row && impl->m_pSelectionModel)
					{
						guint pos = gtk_tree_list_row_get_position(row);
						if (active)
							gtk_selection_model_select_item(GTK_SELECTION_MODEL(impl->m_pSelectionModel), pos, FALSE);
						else
							gtk_selection_model_unselect_item(GTK_SELECTION_MODEL(impl->m_pSelectionModel), pos);
					}
					impl->SyncShortcutSelectionForURI(item->uri, active);
					impl->m_pFolderTree->EmitSelectionChangedEvent();
				}
			}
		}), NULL);
		gtk_box_append(GTK_BOX(hbox), check);
		g_object_set_data(G_OBJECT(list_item), "f-check", check);

		GtkWidget* expander = gtk_tree_expander_new();
		gtk_widget_set_hexpand(expander, TRUE);
		gtk_widget_set_valign(expander, GTK_ALIGN_CENTER);
		GtkWidget* row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
		gtk_widget_set_hexpand(row_box, TRUE);
		gtk_widget_set_valign(row_box, GTK_ALIGN_CENTER);
		GtkWidget* image = gtk_image_new();
		gtk_image_set_icon_size(GTK_IMAGE(image), GTK_ICON_SIZE_NORMAL);
		gtk_widget_set_valign(image, GTK_ALIGN_CENTER);
		GtkWidget* label = gtk_label_new(NULL);
		gtk_label_set_xalign(GTK_LABEL(label), 0.0);
		gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
		gtk_widget_set_hexpand(label, TRUE);
		gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
		gtk_box_append(GTK_BOX(row_box), image);
		gtk_box_append(GTK_BOX(row_box), label);
		gtk_tree_expander_set_child(GTK_TREE_EXPANDER(expander), row_box);
		gtk_box_append(GTK_BOX(hbox), expander);

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
		gtk_widget_add_controller(expander, GTK_EVENT_CONTROLLER(row_click));

		gtk_list_item_set_child(list_item, hbox);
		g_object_set_data(G_OBJECT(list_item), "f-hbox", hbox);
		g_object_set_data(G_OBJECT(list_item), "f-row-box", row_box);
		g_object_set_data(G_OBJECT(list_item), "f-image", image);
		g_object_set_data(G_OBJECT(list_item), "f-label", label);
		g_object_set_data(G_OBJECT(list_item), "f-expander", expander);
	}), NULL);

	g_signal_connect(factory, "bind", G_CALLBACK(+[](GtkListItemFactory* fact, GtkListItem* list_item, gpointer user_data) {
		(void)fact;
		FolderTree::FolderTreeImpl* impl =
			static_cast<FolderTree::FolderTreeImpl*>(user_data);
		GtkTreeListRow* row = GTK_TREE_LIST_ROW(gtk_list_item_get_item(list_item));
		DirItem* item = DIR_ITEM(gtk_tree_list_row_get_item(row));
		GtkWidget* check = GTK_WIDGET(g_object_get_data(G_OBJECT(list_item), "f-check"));
		GtkWidget* hbox = GTK_WIDGET(g_object_get_data(G_OBJECT(list_item), "f-hbox"));
		GtkWidget* row_box = GTK_WIDGET(g_object_get_data(G_OBJECT(list_item), "f-row-box"));
		GtkWidget* image = GTK_WIDGET(g_object_get_data(G_OBJECT(list_item), "f-image"));
		GtkWidget* label = GTK_WIDGET(g_object_get_data(G_OBJECT(list_item), "f-label"));
		GtkWidget* expander = GTK_WIDGET(g_object_get_data(G_OBJECT(list_item), "f-expander"));
		if (NULL != item->icon_name && '\0' != item->icon_name[0])
			gtk_image_set_from_icon_name(GTK_IMAGE(image), item->icon_name);
		else
			gtk_image_set_from_icon_name(GTK_IMAGE(image), "folder");
		gtk_label_set_text(GTK_LABEL(label), item->display_name);

		g_object_set_data(G_OBJECT(check), "dir-item", item);
		g_object_set_data(G_OBJECT(check), "dir-impl", impl);
		g_object_set_data(G_OBJECT(check), "dir-row", row);
		g_object_set_data(G_OBJECT(item), "bound-check", check);

		g_object_set_data(G_OBJECT(hbox), "dir-item", item);
		g_object_set_data(G_OBJECT(hbox), "dir-impl", impl);
		g_object_set_data(G_OBJECT(hbox), "dir-row", row);
		g_object_set_data(G_OBJECT(hbox), "list-item", list_item);

		g_object_set_data(G_OBJECT(expander), "dir-item", item);
		g_object_set_data(G_OBJECT(expander), "dir-impl", impl);
		g_object_set_data(G_OBJECT(expander), "dir-row", row);

		g_object_set_data(G_OBJECT(row_box), "dir-item", item);
		g_object_set_data(G_OBJECT(row_box), "dir-impl", impl);
		g_object_set_data(G_OBJECT(row_box), "dir-row", row);

		if (!g_object_get_data(G_OBJECT(item), "checked-connected"))
		{
			g_object_set_data(G_OBJECT(item), "checked-connected", GINT_TO_POINTER(1));
			/* Keep the checkbox widget in sync when "checked" changes. */
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

		g_object_set_data(G_OBJECT(check), "set-active-guard", GINT_TO_POINTER(1));
		gtk_check_button_set_active(GTK_CHECK_BUTTON(check), item->checked);
		g_object_set_data(G_OBJECT(check), "set-active-guard", GINT_TO_POINTER(0));
		gtk_tree_expander_set_hide_expander(GTK_TREE_EXPANDER(expander),
			!gtk_tree_list_row_is_expandable(row));
		gtk_tree_expander_set_list_row(GTK_TREE_EXPANDER(expander), row);
	}), this);

	g_signal_connect(factory, "unbind", G_CALLBACK(+[](GtkListItemFactory* fact, GtkListItem* list_item) {
		(void)fact;
		GtkWidget* check = GTK_WIDGET(g_object_get_data(G_OBJECT(list_item), "f-check"));
		if (check)
		{
			DirItem* old_item = static_cast<DirItem*>(g_object_get_data(G_OBJECT(check), "dir-item"));
			if (old_item && g_object_get_data(G_OBJECT(old_item), "bound-check") == check)
				g_object_set_data(G_OBJECT(old_item), "bound-check", NULL);
			g_object_set_data(G_OBJECT(check), "dir-item", NULL);
			g_object_set_data(G_OBJECT(check), "dir-impl", NULL);
			g_object_set_data(G_OBJECT(check), "dir-row", NULL);
		}
		GtkWidget* hbox = GTK_WIDGET(g_object_get_data(G_OBJECT(list_item), "f-hbox"));
		if (hbox)
		{
			g_object_set_data(G_OBJECT(hbox), "dir-item", NULL);
			g_object_set_data(G_OBJECT(hbox), "dir-impl", NULL);
			g_object_set_data(G_OBJECT(hbox), "dir-row", NULL);
			g_object_set_data(G_OBJECT(hbox), "list-item", NULL);
		}
		GtkWidget* expander = GTK_WIDGET(g_object_get_data(G_OBJECT(list_item), "f-expander"));
		if (expander)
		{
			g_object_set_data(G_OBJECT(expander), "dir-item", NULL);
			g_object_set_data(G_OBJECT(expander), "dir-impl", NULL);
			g_object_set_data(G_OBJECT(expander), "dir-row", NULL);
		}
		GtkWidget* row_box = GTK_WIDGET(g_object_get_data(G_OBJECT(list_item), "f-row-box"));
		if (row_box)
		{
			g_object_set_data(G_OBJECT(row_box), "dir-item", NULL);
			g_object_set_data(G_OBJECT(row_box), "dir-impl", NULL);
			g_object_set_data(G_OBJECT(row_box), "dir-row", NULL);
		}
	}), NULL);

	// root model
	m_pListStoreRoots = g_list_store_new(DIR_ITEM_TYPE);
	PopulateTreeModel(m_pListStoreRoots);

	m_pTreeListModel = gtk_tree_list_model_new(
		G_LIST_MODEL(m_pListStoreRoots), FALSE, FALSE,
		create_subdirs, this, NULL);
	m_pSelectionModel = gtk_multi_selection_new(G_LIST_MODEL(m_pTreeListModel));

	m_pListView = GTK_LIST_VIEW(gtk_list_view_new(GTK_SELECTION_MODEL(m_pSelectionModel), factory));

	// input handling via GTK4 event controllers / gestures on m_pListView
	GtkGesture *gesture = gtk_gesture_click_new();
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), 0);
	g_signal_connect(gesture, "pressed", G_CALLBACK(view_onButtonPressed), this);
	g_signal_connect(gesture, "released", G_CALLBACK(view_onButtonReleased), this);
	gtk_widget_add_controller(GTK_WIDGET(m_pListView), GTK_EVENT_CONTROLLER(gesture));

	GtkEventController *key_controller = gtk_event_controller_key_new();
	/* Use capture phase so space/Enter are seen here before GtkListView's own
	 * key handling (activate on Enter, mnemonic/selection handling) consumes
	 * them.  Unhandled keys are returned as FALSE so normal list navigation
	 * (Up/Down etc.) still propagates to the list view. */
	gtk_event_controller_set_propagation_phase(key_controller, GTK_PHASE_CAPTURE);
	g_signal_connect(key_controller, "key-pressed", G_CALLBACK(view_on_key_press), this);
	gtk_widget_add_controller(GTK_WIDGET(m_pListView), key_controller);

	// Assemble m_pWidget box
	m_pWidget = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_add_css_class(m_pWidget, "sidebar");
	gtk_widget_add_css_class(m_pWidget, "quiver-sidebar");
	gtk_widget_add_css_class(GTK_WIDGET(m_pShortcutsListView), "navigation-sidebar");
	gtk_widget_add_css_class(GTK_WIDGET(m_pListView), "compact-tree");
	gtk_box_append(GTK_BOX(m_pWidget), GTK_WIDGET(m_pShortcutsListView));
	gtk_box_append(GTK_BOX(m_pWidget), m_pSeparator);
	gtk_box_append(GTK_BOX(m_pWidget), GTK_WIDGET(m_pListView));
	gtk_widget_set_vexpand(GTK_WIDGET(m_pListView), TRUE);

	// build the right-click / menu context popover
	m_pMenuPopover = gtk_popover_new();
	{
		GtkWidget* menu_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

		GtkWidget* menuitem = gtk_button_new_with_label("Check Selected Item(s)");
		gtk_widget_set_halign(menuitem, GTK_ALIGN_FILL);
		g_signal_connect(menuitem, "clicked",
		                 (GCallback) signal_check_selected, this);
		gtk_box_append(GTK_BOX(menu_box), menuitem);

		menuitem = gtk_button_new_with_label("Uncheck Selected Item(s)");
		gtk_widget_set_halign(menuitem, GTK_ALIGN_FILL);
		g_signal_connect(menuitem, "clicked",
		                 (GCallback) signal_uncheck_selected, this);
		gtk_box_append(GTK_BOX(menu_box), menuitem);

		gtk_popover_set_child(GTK_POPOVER(m_pMenuPopover), menu_box);
	}
	gtk_widget_set_parent(m_pMenuPopover, m_pWidget);
	g_signal_connect(m_pWidget, "destroy",
		G_CALLBACK(+[](GtkWidget*, gpointer user_data) {
			FolderTree::FolderTreeImpl* t = static_cast<FolderTree::FolderTreeImpl*>(user_data);
			/* Normally ~FolderTreeImpl unparents the popover while the tree is
			 * still alive.  This is just a safety net in case the window is
			 * destroyed another way: only unparent while a parent still exists,
			 * so that gtk_widget_unparent() is never called on a widget whose
			 * parent is already mid-destruction. */
			if (NULL != t->m_pMenuPopover &&
					gtk_widget_get_parent(t->m_pMenuPopover) != NULL)
			{
				gtk_widget_unparent(t->m_pMenuPopover);
			}
			t->m_pMenuPopover = NULL;
		}), this);
}

static void folder_tree_set_checkbox_for_selected(FolderTree::FolderTreeImpl* impl, gboolean value)
{
	if (impl->m_pSelectionModel && impl->m_pTreeListModel)
	{
		GtkSelectionModel* sel = GTK_SELECTION_MODEL(impl->m_pSelectionModel);
		guint n = g_list_model_get_n_items(G_LIST_MODEL(impl->m_pTreeListModel));
		for (guint i = 0 ; i < n ; i++)
		{
			if (gtk_selection_model_is_selected(sel, i))
			{
				GtkTreeListRow* row = gtk_tree_list_model_get_row(impl->m_pTreeListModel, i);
				if (NULL != row)
				{
					DirItem* item = DIR_ITEM(gtk_tree_list_row_get_item(row));
					if (item)
					{
						dir_item_set_checked(item, value);
						impl->SyncShortcutSelectionForURI(item->uri, value);
					}
				}
			}
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
	impl->m_pFolderTree->EmitSelectionChangedEvent();
}

static void
signal_check_selected (GtkWidget *menuitem, gpointer userdata)
{ (void)menuitem; 
	FolderTree::FolderTreeImpl* pFolderTreeImpl = (FolderTree::FolderTreeImpl*)userdata;
	folder_tree_set_checkbox_for_selected(pFolderTreeImpl, TRUE);
}

static void
signal_uncheck_selected (GtkWidget *menuitem, gpointer userdata)
{ (void)menuitem; 
	FolderTree::FolderTreeImpl* pFolderTreeImpl = (FolderTree::FolderTreeImpl*)userdata;
	folder_tree_set_checkbox_for_selected(pFolderTreeImpl, FALSE);
}

void view_popup_menu_at (GtkWidget *treeview, gdouble x, gdouble y, gpointer userdata)
{ (void)treeview; 
	FolderTree::FolderTreeImpl* pFolderTreeImpl = (FolderTree::FolderTreeImpl*)userdata;

	if (NULL != pFolderTreeImpl->m_pMenuPopover)
	{
		if (x >= 0 && y >= 0)
		{
			GdkRectangle rect;
			rect.x = (int)x;
			rect.y = (int)y;
			rect.width = 1;
			rect.height = 1;
			gtk_popover_set_pointing_to(GTK_POPOVER(pFolderTreeImpl->m_pMenuPopover), &rect);
		}
		gtk_popover_popup(GTK_POPOVER(pFolderTreeImpl->m_pMenuPopover));
	}
}

static guint folder_tree_get_focused_position(FolderTree::FolderTreeImpl* impl)
{
	if (!impl || !impl->m_pListView)
		return G_MAXUINT;

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
			return gtk_tree_list_row_get_position(row);
		GtkListItem* li = static_cast<GtkListItem*>(
			g_object_get_data(G_OBJECT(w), "list-item"));
		if (li)
			return gtk_list_item_get_position(li);
	}

	GtkWidget* fc = gtk_widget_get_focus_child(lv);
	if (fc)
	{
		for (GtkWidget* ch = gtk_widget_get_first_child(fc); ch; ch = gtk_widget_get_next_sibling(ch))
		{
			GtkTreeListRow* row = static_cast<GtkTreeListRow*>(
				g_object_get_data(G_OBJECT(ch), "dir-row"));
			if (row)
				return gtk_tree_list_row_get_position(row);
			GtkListItem* li = static_cast<GtkListItem*>(
				g_object_get_data(G_OBJECT(ch), "list-item"));
			if (li)
				return gtk_list_item_get_position(li);
		}
	}

	// Fallback to first selected item in selection model
	if (impl->m_pSelectionModel && impl->m_pTreeListModel)
	{
		guint n = g_list_model_get_n_items(G_LIST_MODEL(impl->m_pTreeListModel));
		for (guint i = 0; i < n; i++)
		{
			if (gtk_selection_model_is_selected(GTK_SELECTION_MODEL(impl->m_pSelectionModel), i))
				return i;
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

	if (GDK_KEY_Menu == keyval)
	{
		view_popup_menu_at(treeview, -1, -1, userdata);
		return TRUE;
	}

	GtkSelectionModel* sel = GTK_SELECTION_MODEL(pFolderTreeImpl->m_pSelectionModel);
	guint cursor_pos = folder_tree_get_focused_position(pFolderTreeImpl);
	guint n = g_list_model_get_n_items(G_LIST_MODEL(pFolderTreeImpl->m_pTreeListModel));

	guint sel_count = 0;
	for (guint i = 0 ; i < n ; i++)
	{
		if (gtk_selection_model_is_selected(sel, i))
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
				if (gtk_selection_model_is_selected(sel, i))
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
				if (gtk_selection_model_is_selected(sel, i))
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
						gtk_selection_model_select_item(sel, cursor_pos, FALSE);
					else
						gtk_selection_model_unselect_item(sel, cursor_pos);
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

				if (gtk_selection_model_is_selected(sel, i))
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
					gtk_selection_model_select_item(sel, cursor_pos, TRUE);
					pFolderTreeImpl->SyncShortcutSelectionForURI(item->uri, TRUE);
					pFolderTreeImpl->m_pFolderTree->EmitSelectionChangedEvent();
				}
			}
		}
		rval = TRUE;
		return rval;
	}

	if (GDK_KEY_Left == keyval)
	{
		if (G_MAXUINT != cursor_pos)
		{
			GtkTreeListRow* row = gtk_tree_list_model_get_row(pFolderTreeImpl->m_pTreeListModel, cursor_pos);
			if (NULL != row)
			{
				GtkTreeListRow* parent = gtk_tree_list_row_get_parent(row);
				if (gtk_tree_list_row_get_expanded(row))
				{
					gtk_tree_list_row_set_expanded(row, FALSE);
				}
				else if (NULL != parent)
				{
					guint parent_pos = gtk_tree_list_row_get_position(parent);
					gtk_selection_model_select_item(sel, parent_pos, TRUE);
				}
			}
		}
		rval = TRUE;
		return rval;
	}

	if (GDK_KEY_Right == keyval)
	{
		if (G_MAXUINT != cursor_pos)
		{
			GtkTreeListRow* row = gtk_tree_list_model_get_row(pFolderTreeImpl->m_pTreeListModel, cursor_pos);
			if (NULL != row)
			{
				if (!gtk_tree_list_row_get_expanded(row) && gtk_tree_list_row_is_expandable(row))
				{
					gtk_tree_list_row_set_expanded(row, TRUE);
				}
				else if (gtk_tree_list_row_get_expanded(row))
				{
					GListModel* children = gtk_tree_list_row_get_children(row);
					if (NULL != children && g_list_model_get_n_items(children) > 0)
					{
						guint child_pos = cursor_pos + 1;
						gtk_selection_model_select_item(sel, child_pos, TRUE);
					}
				}
			}
		}
		rval = TRUE;
		return rval;
	}

	return rval;
}

static void
view_onButtonPressed (GtkGestureClick *gesture, int n_press, double x, double y, gpointer userdata)
{
	(void)n_press;
	FolderTree::FolderTreeImpl* pFolderTreeImpl = (FolderTree::FolderTreeImpl*)userdata;
	(void)pFolderTreeImpl;
	GtkWidget *treeview = GTK_WIDGET(gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture)));
	guint button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));

	if (button == 3 && n_press == 1)
	{
		view_popup_menu_at(treeview, x, y, userdata);
	}
}

static void view_onButtonReleased (GtkGestureClick *gesture, int n_press, double x, double y, gpointer userdata)
{
	(void)gesture; (void)n_press; (void)x; (void)y; (void)userdata;
}

void FolderTree::FolderTreeImpl::PopulateShortcutsModel(GListStore *store)
{
	int order = 0;
	const char* home_dir = g_get_home_dir();

	// Home
	if (home_dir && g_file_test(home_dir, G_FILE_TEST_IS_DIR))
	{
		GFile* f = g_file_new_for_path(home_dir);
		char* uri = g_file_get_uri(f);
		const char* icon = QuiverUtils::GetSpecialFolderSymbolicIconName(uri);
		g_list_store_append(store,
			G_OBJECT(dir_item_new(uri, "Home", icon ? icon : "user-home-symbolic", TRUE, order++, 0)));
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
		g_list_store_append(store,
			G_OBJECT(dir_item_new(uri, "Desktop", icon ? icon : "user-desktop-symbolic", TRUE, order++, 0)));
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
		g_list_store_append(store,
			G_OBJECT(dir_item_new(uri, "Documents", icon ? icon : "folder-documents-symbolic", TRUE, order++, 0)));
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
		g_list_store_append(store,
			G_OBJECT(dir_item_new(uri, "Downloads", icon ? icon : "folder-download-symbolic", TRUE, order++, 0)));
		g_free(uri);
		g_object_unref(f);
	}
	g_free(fallback_download);

	// Music
	const char* music_dir = g_get_user_special_dir(G_USER_DIRECTORY_MUSIC);
	char* fallback_music = g_build_filename(home_dir, "Music", NULL);
	const char* actual_music = (music_dir && music_dir[0] && 0 != g_strcmp0(music_dir, home_dir))
		? music_dir : fallback_music;
	if (g_file_test(actual_music, G_FILE_TEST_IS_DIR))
	{
		GFile* f = g_file_new_for_path(actual_music);
		char* uri = g_file_get_uri(f);
		const char* icon = QuiverUtils::GetSpecialFolderSymbolicIconName(uri);
		g_list_store_append(store,
			G_OBJECT(dir_item_new(uri, "Music", icon ? icon : "folder-music-symbolic", TRUE, order++, 0)));
		g_free(uri);
		g_object_unref(f);
	}
	g_free(fallback_music);

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
		g_list_store_append(store,
			G_OBJECT(dir_item_new(uri, "Pictures", icon ? icon : "folder-pictures-symbolic", TRUE, order++, 0)));
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
		g_list_store_append(store,
			G_OBJECT(dir_item_new(uri, "Videos", icon ? icon : "folder-videos-symbolic", TRUE, order++, 0)));
		g_free(uri);
		g_object_unref(f);
	}
	g_free(fallback_videos);

	// User Bookmarks
	try {
		BookmarksPtr bmPtr = Bookmarks::GetInstance();
		if (bmPtr)
		{
			std::vector<Bookmark> bms = bmPtr->GetBookmarks();
			for (const auto& bm : bms)
			{
				if (bm.GetURIs().empty()) continue;
				std::string bm_uri = bm.GetURIs().front();
				bool duplicate = false;
				guint n = g_list_model_get_n_items(G_LIST_MODEL(store));
				for (guint i = 0; i < n; i++)
				{
					DirItem* it = DIR_ITEM(g_list_model_get_item(G_LIST_MODEL(store), i));
					if (it && it->uri && bm_uri == it->uri)
					{
						duplicate = true;
						g_object_unref(it);
						break;
					}
					if (it) g_object_unref(it);
				}
				if (duplicate) continue;

				const char* icon = QuiverUtils::GetSpecialFolderSymbolicIconName(bm_uri.c_str());
				std::string icon_name = icon ? icon : (!bm.GetIcon().empty() ? bm.GetIcon() : "folder-symbolic");
				g_list_store_append(store,
					G_OBJECT(dir_item_new(bm_uri.c_str(), bm.GetName().c_str(), icon_name.c_str(), FALSE, order++, 0)));
			}
		}
	} catch (...) {}
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

	g_list_store_remove_all(m_pShortcutsStore);
	PopulateShortcutsModel(m_pShortcutsStore);

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


gchar* folder_tree_get_icon_name(GFile* gfile)
{
	if (NULL == gfile)
		return g_strdup("folder");

	const char* special_icon = QuiverUtils::GetSpecialFolderIconName(gfile);
	if (NULL != special_icon)
	{
		return g_strdup(special_icon);
	}

	gchar* icon_name = NULL;
	GFileInfo* info = g_file_query_info(
		gfile,
		G_FILE_ATTRIBUTE_STANDARD_ICON,
		G_FILE_QUERY_INFO_NONE,
		NULL,
		NULL);
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
		g_object_unref(info);
	}
	if (NULL == icon_name)
	{
		icon_name = g_strdup("folder");
	}
	return icon_name;
}
