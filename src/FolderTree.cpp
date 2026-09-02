#include <config.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <stdint.h>

#include <list>
#include <string>
#include <algorithm>

#include "FolderTree.h"
#include "QuiverStockIcons.h"

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

static void dir_item_class_init (DirItemClass* klass)
{
	G_OBJECT_CLASS(klass)->finalize = dir_item_finalize;
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

static gchar* folder_tree_get_icon_name(GFile* gfile);

class FolderTree::FolderTreeImpl
{
public:
// constructor / destructor
	FolderTreeImpl(FolderTree *pFolderTree);
	~FolderTreeImpl();


// methods
	void CreateWidget();
	void PopulateTreeModel(GListStore *roots);

	void SetSelectedFolders(std::list<std::string> &uris);
	std::list<std::string> GetSelectedFolders() const;
	void ClearAllCheckboxes();
	void SetCheckboxForItem(DirItem* item, gboolean value);
	guint FindItemPosition(DirItem* target);
	guint FindItemByURI(const gchar* uri, gboolean check_only);
	DirItem* FindRootForPath(const gchar* uri);
	void ExpandItem(DirItem* item);

// member variables
	GtkWidget*       m_pWidget;
	GtkWidget*       m_pMenuPopover;
	FolderTree*      m_pFolderTree;
	GListStore*      m_pListStoreRoots;
	GtkTreeListModel* m_pTreeListModel;
	GtkMultiSelection* m_pSelectionModel;
	GtkListView*     m_pListView;
	gchar*           m_pScrollToURI;
	bool             m_bButtonDown;
	bool             m_bMouseSelect;
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


std::list<std::string> FolderTree::GetSelectedFolders() const
{
	return m_FolderTreeImplPtr->GetSelectedFolders();	
}

void FolderTree::SetSelectedFolders(std::list<std::string> &uris)
{
	m_FolderTreeImplPtr->SetSelectedFolders(uris);
}



FolderTree::FolderTreeImpl::FolderTreeImpl(FolderTree *parent) :
	m_bButtonDown(false), m_bMouseSelect(false)
{
	m_pFolderTree = parent;
	
	m_pMenuPopover = NULL;
	m_pScrollToURI = NULL;
	m_pListStoreRoots = NULL;
	m_pTreeListModel = NULL;
	m_pSelectionModel = NULL;

	CreateWidget();
}

FolderTree::FolderTreeImpl::~FolderTreeImpl()
{
	if (NULL != m_pScrollToURI)
	{
		g_free(m_pScrollToURI);
		m_pScrollToURI = NULL;
	}

	/* Disconnect every handler on the list view that captured `this` (the
	 * click gestures, the key controller and the "destroy" safety net).  The
	 * list view is owned by the window tree and outlives us: ~BrowserImpl
	 * resets us (m_FolderTreePtr.reset()) BEFORE it unparents the browser
	 * subtree, so when the tree is destroyed a moment later it would fire the
	 * "destroy" handler with this (already freed) object as user_data. */
	if (NULL != m_pWidget)
		g_signal_handlers_disconnect_matched(
			m_pWidget,
			G_SIGNAL_MATCH_DATA,
			0,
			0,
			NULL,
			NULL,
			this);

	/* m_pMenuPopover is parented to the list view.  It must be unparented
	 * here while the widget tree is still alive: gtk_window_destroy() runs
	 * after us and would otherwise finalize the list view with the popover
	 * still attached ("Finalizing GtkListView... still has children left"),
	 * and unparenting inside the tree's own "destroy" signal handler
	 * crashes because the parent is mid-destruction. */
	if (NULL != m_pMenuPopover && gtk_widget_get_parent(m_pMenuPopover) != NULL)
	{
		gtk_widget_unparent(m_pMenuPopover);
	}
	m_pMenuPopover = NULL;

	/* The selection model, tree list model, list store and the list view
	 * widget are all owned through the parented widget tree: the list view
	 * holds the selection model, which holds the tree list model, which holds
	 * the list store.  The window destroy at the end of ~QuiverImpl tears that
	 * whole subtree down after us, so unref'ing them again would double-free. */
	m_pSelectionModel = NULL;
	m_pTreeListModel = NULL;
	m_pListStoreRoots = NULL;
	m_pWidget = NULL;
}

std::list<std::string> FolderTree::FolderTreeImpl::GetSelectedFolders() const
{
	std::list<std::string> listSelectedFolders;

	guint n = g_list_model_get_n_items(G_LIST_MODEL(m_pTreeListModel));
	for (guint i = 0 ; i < n ; i++)
	{
		GtkTreeListRow* row = gtk_tree_list_model_get_row(m_pTreeListModel, i);
		if (NULL == row)
			continue;
		DirItem* item = DIR_ITEM(gtk_tree_list_row_get_item(row));
		if (item->checked)
		{
			if (NULL != item->uri)
				listSelectedFolders.push_back(item->uri);
		}
	}

	return listSelectedFolders;
}

void FolderTree::FolderTreeImpl::ClearAllCheckboxes()
{
	guint n = g_list_model_get_n_items(G_LIST_MODEL(m_pTreeListModel));
	for (guint i = 0 ; i < n ; i++)
	{
		GtkTreeListRow* row = gtk_tree_list_model_get_row(m_pTreeListModel, i);
		if (NULL == row)
			continue;
		DirItem* item = DIR_ITEM(gtk_tree_list_row_get_item(row));
		item->checked = FALSE;
	}
}

void FolderTree::FolderTreeImpl::SetCheckboxForItem(DirItem* item, gboolean value)
{
	if (NULL != item)
	{
		item->checked = value;
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
		if (g_file_has_prefix(file, file_root))
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
		DirItem* root = FindRootForPath(itr->c_str());
		if (NULL == root)
			continue;

		// walk down the path expanding each intermediate row and
		// locating (creating is unnecessary: subdirs are enumerated on
		// expansion) the target folder
		DirItem* current = root;
		g_object_ref(current);
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
				g_object_unref(next);
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

void FolderTree::FolderTreeImpl::CreateWidget()
{
	// single row factory: checkbox + expander + icon + name
	GtkListItemFactory* factory = gtk_signal_list_item_factory_new();

	g_signal_connect(factory, "setup", G_CALLBACK(+[](GtkListItemFactory* fact, GtkListItem* list_item) {
		(void)fact;
		// build: hbox [ check, expander[ icon, label ] ]
		GtkWidget* hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

		GtkWidget* check = gtk_check_button_new();
		gtk_widget_set_margin_start(check, 6);
		gtk_widget_set_margin_end(check, 6);
		gtk_widget_set_valign(check, GTK_ALIGN_CENTER);
		g_signal_connect(check, "toggled", G_CALLBACK(+[](GtkWidget* w, gpointer) {
			FolderTree::FolderTreeImpl* impl =
				static_cast<FolderTree::FolderTreeImpl*>(
					g_object_get_data(G_OBJECT(w), "dir-impl"));
			DirItem* item = static_cast<DirItem*>(
				g_object_get_data(G_OBJECT(w), "dir-item"));
			if (NULL != impl && NULL != item &&
				!g_object_get_data(G_OBJECT(w), "set-active-guard"))
			{
				gboolean active = gtk_check_button_get_active(GTK_CHECK_BUTTON(w));
				if (item->checked != active)
				{
					item->checked = active;
					impl->m_pFolderTree->EmitSelectionChangedEvent();
				}
			}
		}), NULL);
		gtk_box_append(GTK_BOX(hbox), check);
		g_object_set_data(G_OBJECT(list_item), "f-check", check);

		GtkWidget* expander = gtk_tree_expander_new();
		GtkWidget* row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
		GtkWidget* image = gtk_image_new();
		gtk_image_set_icon_size(GTK_IMAGE(image), GTK_ICON_SIZE_NORMAL);
		GtkWidget* label = gtk_label_new(NULL);
		gtk_label_set_xalign(GTK_LABEL(label), 0.0);
		gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
		gtk_widget_set_hexpand(label, TRUE);
		gtk_box_append(GTK_BOX(row_box), image);
		gtk_box_append(GTK_BOX(row_box), label);
		gtk_tree_expander_set_child(GTK_TREE_EXPANDER(expander), row_box);
		gtk_box_append(GTK_BOX(hbox), expander);

		gtk_list_item_set_child(list_item, hbox);
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
		g_object_set_data(G_OBJECT(check), "dir-item", NULL);
		g_object_set_data(G_OBJECT(check), "dir-impl", NULL);
	}), NULL);

	// root model
	m_pListStoreRoots = g_list_store_new(DIR_ITEM_TYPE);
	PopulateTreeModel(m_pListStoreRoots);

	m_pTreeListModel = gtk_tree_list_model_new(
		G_LIST_MODEL(m_pListStoreRoots), FALSE, FALSE,
		create_subdirs, this, NULL);
	m_pSelectionModel = gtk_multi_selection_new(G_LIST_MODEL(m_pTreeListModel));

	m_pListView = GTK_LIST_VIEW(gtk_list_view_new(GTK_SELECTION_MODEL(m_pSelectionModel), factory));
	m_pWidget = GTK_WIDGET(m_pListView);

	// input handling via GTK4 event controllers / gestures
	GtkGesture *gesture = gtk_gesture_click_new();
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), 0);
	g_signal_connect(gesture, "pressed", G_CALLBACK(view_onButtonPressed), this);
	g_signal_connect(gesture, "released", G_CALLBACK(view_onButtonReleased), this);
	gtk_widget_add_controller(m_pWidget, GTK_EVENT_CONTROLLER(gesture));

	GtkEventController *key_controller = gtk_event_controller_key_new();
	g_signal_connect(key_controller, "key-pressed", G_CALLBACK(view_on_key_press), this);
	gtk_widget_add_controller(m_pWidget, key_controller);

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
				item->checked = value;
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
	guint n = g_list_model_get_n_items(G_LIST_MODEL(pFolderTreeImpl->m_pTreeListModel));

	// find the first selected / focused row position
	guint cursor_pos = G_MAXUINT;
	for (guint i = 0 ; i < n ; i++)
	{
		if (gtk_selection_model_is_selected(sel, i))
		{
			cursor_pos = i;
			break;
		}
	}

	gboolean rval = FALSE;

	if (GDK_KEY_space == keyval && !(state & GDK_CONTROL_MASK))
	{
		if (G_MAXUINT != cursor_pos)
		{
			GtkTreeListRow* row = gtk_tree_list_model_get_row(pFolderTreeImpl->m_pTreeListModel, cursor_pos);
			if (NULL != row)
			{
				DirItem* item = DIR_ITEM(gtk_tree_list_row_get_item(row));
				gboolean value = item->checked;
				folder_tree_set_checkbox_for_selected(pFolderTreeImpl, !value);
			}
		}
		rval = TRUE;
		return rval;
	}

	if (GDK_KEY_Return == keyval)
	{
		if (G_MAXUINT != cursor_pos)
		{
			GtkTreeListRow* row = gtk_tree_list_model_get_row(pFolderTreeImpl->m_pTreeListModel, cursor_pos);
			if (NULL != row)
			{
				gtk_tree_list_row_set_expanded(row, !gtk_tree_list_row_get_expanded(row));
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
					gtk_selection_model_select_item(sel, parent_pos, FALSE);
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
						gtk_selection_model_select_item(sel, child_pos, FALSE);
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
	FolderTree::FolderTreeImpl* pFolderTreeImpl = (FolderTree::FolderTreeImpl*)userdata;
	GtkWidget *treeview = GTK_WIDGET(gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture)));
	guint button = gtk_gesture_single_get_button(GTK_GESTURE_SINGLE(gesture));

	if (button == 2)
	{
		// toggle the checked state of the row at the clicked position
		GtkSelectionModel* sel = GTK_SELECTION_MODEL(pFolderTreeImpl->m_pSelectionModel);
		guint n = g_list_model_get_n_items(G_LIST_MODEL(pFolderTreeImpl->m_pTreeListModel));
		// approximate: toggle the first selected row (or the row under cursor)
		guint pos = G_MAXUINT;
		for (guint i = 0 ; i < n ; i++)
		{
			if (gtk_selection_model_is_selected(sel, i))
			{
				pos = i;
				break;
			}
		}
		if (G_MAXUINT != pos)
		{
			GtkTreeListRow* row = gtk_tree_list_model_get_row(pFolderTreeImpl->m_pTreeListModel, pos);
			if (NULL != row)
			{
				DirItem* item = DIR_ITEM(gtk_tree_list_row_get_item(row));
				item->checked = !item->checked;
				pFolderTreeImpl->m_pFolderTree->EmitSelectionChangedEvent();
			}
		}
		return;
	}

	if (button == 1)
	{
		pFolderTreeImpl->m_bButtonDown = TRUE;
		pFolderTreeImpl->m_bMouseSelect = TRUE;
	}

	if (button == 3 && n_press == 1)
	{
		view_popup_menu_at(treeview, x, y, userdata);
	}
}

static void view_onButtonReleased (GtkGestureClick *gesture, int n_press, double x, double y, gpointer userdata)
{
	(void)n_press; (void)x; (void)y; (void)gesture;
	FolderTree::FolderTreeImpl* pFolderTreeImpl = (FolderTree::FolderTreeImpl*)userdata;

	if (pFolderTreeImpl->m_bButtonDown)
	{
		pFolderTreeImpl->m_bButtonDown = FALSE;
		pFolderTreeImpl->m_bMouseSelect = FALSE;
	}
}

void FolderTree::FolderTreeImpl::PopulateTreeModel(GListStore *roots)
{
	int iNodeOrder = 0;

	const char* home_dir = g_get_home_dir();
	GFile* file_home = g_file_new_for_path(home_dir);
	const char* desktop_dir = g_get_user_special_dir(G_USER_DIRECTORY_DESKTOP);
	const char* pictures_dir = g_get_user_special_dir(G_USER_DIRECTORY_PICTURES);
	const char* docs_dir = g_get_user_special_dir(G_USER_DIRECTORY_DOCUMENTS);
	GFile* file_desktop = g_file_new_for_path(desktop_dir && desktop_dir[0] ? desktop_dir : home_dir);
	GFile* file_pictures = g_file_new_for_path(pictures_dir && pictures_dir[0] ? pictures_dir : home_dir);
	GFile* file_docs = g_file_new_for_path(docs_dir && docs_dir[0] ? docs_dir : home_dir);
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

	// desktop
	icon = folder_tree_get_icon_name(file_desktop);
	uri = g_file_get_uri(file_desktop);
	g_list_store_append(roots,
		G_OBJECT(dir_item_new(uri, "Desktop", icon, TRUE, iNodeOrder++, 0)));
	g_free(uri);
	g_free(icon);

	// documents
	icon = folder_tree_get_icon_name(file_docs);
	uri = g_file_get_uri(file_docs);
	g_list_store_append(roots,
		G_OBJECT(dir_item_new(uri, "Documents", icon, TRUE, iNodeOrder++, 0)));
	g_free(uri);
	g_free(icon);

	// pictures
	icon = folder_tree_get_icon_name(file_pictures);
	uri = g_file_get_uri(file_pictures);
	g_list_store_append(roots,
		G_OBJECT(dir_item_new(uri, "Pictures", icon, TRUE, iNodeOrder++, 0)));
	g_free(uri);
	g_free(icon);

	// other mounts (filesystem root)
	GMount* root_mount = g_file_find_enclosing_mount(file_root, NULL, NULL);
	if (NULL != root_mount)
	{
		GIcon* root_icon = g_mount_get_icon(root_mount);
		gchar* root_icon_name = NULL;
		if (G_IS_THEMED_ICON(root_icon))
		{
			const gchar* const* names = g_themed_icon_get_names(G_THEMED_ICON(root_icon));
			if (NULL != names && NULL != names[0])
				root_icon_name = g_strdup(names[0]);
		}
		char* root_uri = g_file_get_uri(file_root);

		g_list_store_append(roots,
			G_OBJECT(dir_item_new(root_uri, QUIVER_FOLDER_TREE_ROOT_NAME,
				root_icon_name, TRUE, iNodeOrder++, 0)));

		g_free(root_uri);
		if (NULL != root_icon_name)
			g_free(root_icon_name);
		if (NULL != root_icon)
			g_object_unref(root_icon);
		g_object_unref(root_mount);
	}

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
	g_object_unref(file_desktop);
	g_object_unref(file_pictures);
	g_object_unref(file_docs);
	g_object_unref(file_root);
}


gchar* folder_tree_get_icon_name(GFile* gfile)
{
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
