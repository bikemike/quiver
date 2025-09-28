#include <gtk/gtk.h>
#include "ExifView.h"

#include "Preferences.h"
#include "IPreferencesEventHandler.h"

#include <libexif/exif-ifd.h>
#include <libexif/exif-entry.h>
#include <libexif/exif-data.h>
#include <libexif/exif-loader.h>
#include <libexif/exif-tag.h>
#include <libexif/exif-utils.h>


#include <graphene.h>
#include "QuiverUtils.h"
// #include "QuiverStockIcons.h" // Commented out as stock icons are deprecated

/* For the GtkListView model */
#define EXIF_TYPE_ITEM (exif_item_get_type ())
G_DECLARE_FINAL_TYPE(ExifItem, exif_item, EXIF, ITEM, GObject)

struct _ExifItem
{
    GObject parent_instance;
    char* tag;
    char* value;
};

G_DEFINE_TYPE(ExifItem, exif_item, G_TYPE_OBJECT)

static void exif_item_init(ExifItem *item) {}

static void exif_item_class_init(ExifItemClass *klass) {}

ExifItem* exif_item_new(const char* tag, const char* value)
{
    ExifItem* item = (ExifItem*)g_object_new(EXIF_TYPE_ITEM, NULL);
    item->tag = g_strdup(tag);
    item->value = g_strdup(value);
    return item;
}


/* private implementation */

typedef struct _ForEachEntryData {
	ExifView::ExifViewImpl *pExifViewImpl;
	GListStore *store;
} ForEachEntryData;

static void exif_content_foreach_entry_callback(ExifEntry *entry, void *user_data)
{
    ForEachEntryData* data = (ForEachEntryData*)user_data;
    const char *tag_name = exif_tag_get_name_in_ifd(entry->tag, exif_entry_get_ifd(entry));
    char value[1024];
    exif_entry_get_value(entry, value, sizeof(value));
    g_list_store_append(data->store, exif_item_new(tag_name, value));
}

static void exif_data_foreach_content_wrapper(ExifContent *content, void *user_data)
{
    exif_content_foreach_entry(content, exif_content_foreach_entry_callback, user_data);
}

static void on_copy_tag(GSimpleAction *action, GVariant *parameter, gpointer user_data);
static void on_copy_value(GSimpleAction *action, GVariant *parameter, gpointer user_data);
static void on_show_popup(GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data);


class ExifView::ExifViewImpl
{
public:
	ExifViewImpl();
	~ExifViewImpl();

	QuiverFile    m_QuiverFile;
	ExifData*     m_pExifData;
	GtkWidget*    m_pColumnView;
    GListStore*   m_pListStore;
	GtkWidget*    m_pScrolledWindow;
	ExifItem*     m_pLastClickedItem;
	// GtkUIManager* m_pUIManager; // GtkUIManager is deprecated
	guint         m_iIdleLoadID;
	gboolean      m_bLoaded;

	class PreferencesEventHandler : public IPreferencesEventHandler
	{
	public:
		PreferencesEventHandler(ExifViewImpl* parent) {this->parent = parent;};
		virtual void HandlePreferenceChanged(PreferencesEventPtr event);
	private:
		ExifViewImpl* parent;
	};
	IPreferencesEventHandlerPtr  m_PreferencesEventHandlerPtr;
};

static void on_show_popup(GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data)
{
    ExifView::ExifViewImpl* pExifViewImpl = (ExifView::ExifViewImpl*)user_data;
    GtkWidget* column_view = pExifViewImpl->m_pColumnView;

    GtkWidget* picked = gtk_widget_pick(column_view, x, y, GTK_PICK_DEFAULT);
    if (!picked) return;

    GtkListItem* list_item = GTK_LIST_ITEM(gtk_widget_get_ancestor(picked, GTK_TYPE_LIST_ITEM));
    if (!list_item) return;

    pExifViewImpl->m_pLastClickedItem = (ExifItem*)gtk_list_item_get_item(list_item);
    if (!pExifViewImpl->m_pLastClickedItem) return;

    GMenu *menu = g_menu_new();
    g_menu_append(menu, "Copy Tag", "exif.copy_tag");
    g_menu_append(menu, "Copy Value", "exif.copy_value");

    GtkWidget *popover = gtk_popover_menu_new_from_model(G_MENU_MODEL(menu));
    gtk_widget_set_parent(popover, column_view);

    GdkRectangle rect = { (int)x, (int)y, 1, 1 };
    gtk_popover_set_pointing_to(GTK_POPOVER(popover), &rect);

    gtk_popover_popup(GTK_POPOVER(popover));
    g_object_unref(menu);
}

static void on_copy_tag(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    ExifView::ExifViewImpl* pExifViewImpl = (ExifView::ExifViewImpl*)user_data;
    if (pExifViewImpl && pExifViewImpl->m_pLastClickedItem)
    {
        gdk_clipboard_set_text(gtk_widget_get_clipboard(pExifViewImpl->m_pColumnView), pExifViewImpl->m_pLastClickedItem->tag);
    }
}

static void on_copy_value(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    ExifView::ExifViewImpl* pExifViewImpl = (ExifView::ExifViewImpl*)user_data;
    if (pExifViewImpl && pExifViewImpl->m_pLastClickedItem)
    {
        gdk_clipboard_set_text(gtk_widget_get_clipboard(pExifViewImpl->m_pColumnView), pExifViewImpl->m_pLastClickedItem->value);
    }
}


ExifView::ExifViewImpl::ExifViewImpl() : m_PreferencesEventHandlerPtr ( new PreferencesEventHandler(this) ) {
	PreferencesPtr prefPtr = Preferences::GetInstance();
	prefPtr->AddEventHandler( m_PreferencesEventHandlerPtr );
}

ExifView::ExifViewImpl::~ExifViewImpl() {
	PreferencesPtr prefPtr = Preferences::GetInstance();
	prefPtr->RemoveEventHandler( m_PreferencesEventHandlerPtr );
	if (NULL != m_pExifData) { exif_data_unref(m_pExifData); m_pExifData = NULL; }
	if (m_pScrolledWindow) { g_object_unref(m_pScrolledWindow); m_pScrolledWindow = NULL; }
}

static void exif_view_map(GtkWidget *widget, gpointer user_data) {
	ExifView::ExifViewImpl *pExifViewImpl = static_cast<ExifView::ExifViewImpl*>(user_data);
	if (!pExifViewImpl->m_bLoaded) {
		if (0 != pExifViewImpl->m_iIdleLoadID) {
			g_source_remove(pExifViewImpl->m_iIdleLoadID );
			pExifViewImpl->m_iIdleLoadID = 0;
		}
	}
}

static void setup_list_item_label(GtkListItemFactory *factory, GtkListItem *list_item, gpointer user_data) {
    GtkWidget *label = gtk_label_new("");
    gtk_list_item_set_child(list_item, label);
}

static void bind_list_item_tag(GtkSignalListItemFactory *self, GtkListItem *list_item, gpointer user_data) {
    GtkWidget *label = gtk_list_item_get_child(list_item);
    ExifItem *item = (ExifItem*)gtk_list_item_get_item(list_item);
    if (item) {
        gtk_label_set_text(GTK_LABEL(label), item->tag);
    }
}

static void bind_list_item_value(GtkSignalListItemFactory *self, GtkListItem *list_item, gpointer user_data) {
    GtkWidget *label = gtk_list_item_get_child(list_item);
    ExifItem *item = (ExifItem*)gtk_list_item_get_item(list_item);
    if (item) {
        gtk_label_set_text(GTK_LABEL(label), item->value);
    }
}

ExifView::ExifView() : m_ExifViewImplPtr (new ExifViewImpl() ) {
	m_ExifViewImplPtr->m_iIdleLoadID = 0;
	m_ExifViewImplPtr->m_bLoaded     = FALSE;
	m_ExifViewImplPtr->m_pColumnView   = NULL;
	m_ExifViewImplPtr->m_pExifData   = NULL;
	m_ExifViewImplPtr->m_pLastClickedItem = NULL;

	m_ExifViewImplPtr->m_pScrolledWindow = gtk_scrolled_window_new();
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(m_ExifViewImplPtr->m_pScrolledWindow),GTK_POLICY_AUTOMATIC,GTK_POLICY_AUTOMATIC);
    g_object_ref(m_ExifViewImplPtr->m_pScrolledWindow);

    m_ExifViewImplPtr->m_pListStore = g_list_store_new(EXIF_TYPE_ITEM);
    GtkNoSelection* selection_model = gtk_no_selection_new(G_LIST_MODEL(m_ExifViewImplPtr->m_pListStore));

    GtkListItemFactory* tag_factory = gtk_signal_list_item_factory_new();
    g_signal_connect(tag_factory, "setup", G_CALLBACK(setup_list_item_label), NULL);
    g_signal_connect(tag_factory, "bind", G_CALLBACK(bind_list_item_tag), NULL);

    GtkListItemFactory* value_factory = gtk_signal_list_item_factory_new();
    g_signal_connect(value_factory, "setup", G_CALLBACK(setup_list_item_label), NULL);
    g_signal_connect(value_factory, "bind", G_CALLBACK(bind_list_item_value), NULL);

    GtkColumnViewColumn* tag_column = gtk_column_view_column_new("Tag", tag_factory);
    GtkColumnViewColumn* value_column = gtk_column_view_column_new("Value", value_factory);

    m_ExifViewImplPtr->m_pColumnView = gtk_column_view_new(GTK_SELECTION_MODEL(selection_model));
    gtk_column_view_append_column(GTK_COLUMN_VIEW(m_ExifViewImplPtr->m_pColumnView), tag_column);
    gtk_column_view_append_column(GTK_COLUMN_VIEW(m_ExifViewImplPtr->m_pColumnView), value_column);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(m_ExifViewImplPtr->m_pScrolledWindow), m_ExifViewImplPtr->m_pColumnView);
    gtk_widget_set_visible(m_ExifViewImplPtr->m_pScrolledWindow, TRUE);

	g_signal_connect(m_ExifViewImplPtr->m_pScrolledWindow, "map", (GCallback) exif_view_map, m_ExifViewImplPtr.get());

    GtkGesture* gesture = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), GDK_BUTTON_SECONDARY);
    g_signal_connect(gesture, "pressed", G_CALLBACK(on_show_popup), m_ExifViewImplPtr.get());
    gtk_widget_add_controller(m_ExifViewImplPtr->m_pColumnView, GTK_EVENT_CONTROLLER(gesture));

    const GActionEntry actions[] = {
        { "copy_tag", on_copy_tag },
        { "copy_value", on_copy_value }
    };
    GSimpleActionGroup* action_group = g_simple_action_group_new();
    g_action_map_add_action_entries(G_ACTION_MAP(action_group), actions, G_N_ELEMENTS(actions), m_ExifViewImplPtr.get());
    gtk_widget_insert_action_group(m_ExifViewImplPtr->m_pColumnView, "exif", G_ACTION_GROUP(action_group));
    g_object_unref(action_group);
}

ExifView::~ExifView() {}

GtkWidget * ExifView::GetWidget() {
	return m_ExifViewImplPtr->m_pScrolledWindow;
}

static gboolean exif_view_idle_load_exif_tree_view(gpointer data) {
	ExifView::ExifViewImpl *pExifViewImpl = static_cast<ExifView::ExifViewImpl*>(data);

    g_list_store_remove_all(pExifViewImpl->m_pListStore);

	pExifViewImpl->m_pExifData = pExifViewImpl->m_QuiverFile.GetExifData();
	if (pExifViewImpl->m_pExifData)
	{
        ForEachEntryData fe_data;
        fe_data.pExifViewImpl = pExifViewImpl;
        fe_data.store = pExifViewImpl->m_pListStore;
		exif_data_foreach_content(pExifViewImpl->m_pExifData, exif_data_foreach_content_wrapper, &fe_data);
	}

	pExifViewImpl->m_iIdleLoadID = 0;
	return FALSE;
}

void ExifView::SetQuiverFile(QuiverFile quiverFile) {
	m_ExifViewImplPtr->m_QuiverFile = quiverFile;
	if (NULL != m_ExifViewImplPtr->m_pExifData) {
		exif_data_unref(m_ExifViewImplPtr->m_pExifData);
		m_ExifViewImplPtr->m_pExifData = NULL;
	}
	if (gtk_widget_get_mapped(m_ExifViewImplPtr->m_pScrolledWindow)) {
		if (0 != m_ExifViewImplPtr->m_iIdleLoadID) {
			g_source_remove(m_ExifViewImplPtr->m_iIdleLoadID );
		}
        m_ExifViewImplPtr->m_iIdleLoadID = g_idle_add(exif_view_idle_load_exif_tree_view, m_ExifViewImplPtr.get());
	} else {
		m_ExifViewImplPtr->m_bLoaded = FALSE;
	}
}

void ExifView::ExifViewImpl::PreferencesEventHandler::HandlePreferenceChanged(PreferencesEventPtr event) {}