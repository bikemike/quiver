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


#include "QuiverUtils.h"
// #include "QuiverStockIcons.h" // Commented out as stock icons are deprecated

/* prototypes */

// static GtkTreeModel * create_numbers_model (void); // Deprecated: GtkListStore
// static void exif_orientation_to_text (GtkTreeViewColumn *tree_column, GtkCellRenderer   *cell, GtkTreeModel *tree_model, GtkTreeIter *iter, gpointer data); // Deprecated
// static void exif_tree_store_add_entry (ExifView::ExifViewImpl *pExifViewImpl, GtkTreeStore *store,GtkTreeIter *parent, GtkTreeIter *new_child, ExifEntry *entry); // Deprecated
// static void exif_tree_store_update_iter_entry (ExifView::ExifViewImpl *pExifViewImpl, GtkTreeStore *store, GtkTreeIter *iter, ExifEntry *entry); // Deprecated

// static void exif_value_editing_started_callback (GtkCellRenderer *renderer, GtkCellEditable *editable, gchar *path, gpointer user_data); // Deprecated
// static void exif_value_editing_canceled_callback (GtkCellRenderer *renderer, gpointer user_data); // Deprecated

// static gboolean exif_tree_event_popup_menu (GtkWidget *treeview, gpointer userdata); // Deprecated (GtkMenu)
// static gboolean exif_tree_event_button_press (GtkWidget *treeview, GdkEvent *event, gpointer userdata); // Changed GdkEventButton to GdkEvent, but largely deprecated
// static void exif_tree_show_popup_menu (ExifView::ExifViewImpl *pExifViewImpl, GtkWidget *treeview, guint button, guint32 activate_time); // Deprecated (GtkMenu)
// static void exif_value_cell_edited_callback (GtkCellRendererText *cell, gchar *path_string, gchar *new_text, gpointer user_data); // Deprecated


// static void exif_tree_event_remove_tag(GtkMenuItem *menuitem, gpointer user_data); // Deprecated (GtkMenuItem)
// static void exif_tree_event_add_tag(GtkMenuItem *menuitem, gpointer user_data);    // Deprecated (GtkMenuItem)
// static void exif_tree_update_thumbnail(ExifView::ExifViewImpl *pExifViewImpl, GtkTreeStore *store); // Deprecated (GtkTreeStore)
// static void exif_tree_update_iter_entry (ExifView::ExifViewImpl *pExifViewImpl, GtkTreeIter *iter, ExifEntry *entry); // Deprecated
// static void exif_tree_update_entry (ExifView::ExifViewImpl *pExifViewImpl, ExifIfd ifd, ExifTag tag);	// Deprecated

static void exif_view_map(GtkWidget *widget, gpointer user_data);
// static gboolean entry_focus_out ( GtkWidget *widget, GdkEvent *event, gpointer user_data); // Deprecated


/* private implementation */
class ExifView::ExifViewImpl
{
public:
	ExifViewImpl();
	~ExifViewImpl();

	QuiverFile    m_QuiverFile;
	ExifData*     m_pExifData;
	GtkWidget*    m_pTreeView;
	GtkWidget*    m_pScrolledWindow;
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

enum
{
	EXIF_TREE_COLUMN_TAG_ID, EXIF_TREE_COLUMN_NAME, EXIF_TREE_COLUMN_VALUE_TEXT,
	EXIF_TREE_COLUMN_VALUE_ORIENTATION, EXIF_TREE_COLUMN_VALUE_PIXBUF,
	EXIF_TREE_COLUMN_IS_VISIBLE_TEXT, EXIF_TREE_COLUMN_IS_VISIBLE_PIXBUF,
	EXIF_TREE_COLUMN_IS_VISIBLE_ORIENTATION, EXIF_TREE_COLUMN_IS_GROUP,
	EXIF_TREE_COLUMN_IS_EDITABLE, EXIF_TREE_COLUMN_COUNT,
};
enum { ORIENTATION_COLUMN_TEXT_VALUE, ORIENTATION_COLUMN_COUNT };

const char *orientation_options[] = {
    "top - left", "top - right", "bottom - right", "bottom - left",
    "left - top", "right - top", "right - bottom", "left - bottom",
};
typedef struct _ForEachEntryData {
	ExifView::ExifViewImpl *pExifViewImpl;
	GtkTreeStore *store; // Deprecated
	GtkTreeIter *parent; // Deprecated
} ForEachEntryData;
typedef struct _ExifTagAddRemoveStruct {
	ExifView::ExifViewImpl *pExifViewImpl;
	ExifIfd ifd; ExifTag tag;
} ExifTagAddRemoveStruct;

const int ifd_editable_tags[][10] = {
	{ EXIF_TAG_ARTIST, EXIF_TAG_DATE_TIME, EXIF_TAG_IMAGE_DESCRIPTION, EXIF_TAG_IMAGE_WIDTH, EXIF_TAG_IMAGE_LENGTH, EXIF_TAG_ORIENTATION, EXIF_TAG_SOFTWARE, 0 },
	{ 0 }, { EXIF_TAG_DATE_TIME_ORIGINAL, EXIF_TAG_DATE_TIME_DIGITIZED, EXIF_TAG_PIXEL_X_DIMENSION, EXIF_TAG_PIXEL_Y_DIMENSION, EXIF_TAG_USER_COMMENT, 0 },
	{ 0 }, { EXIF_TAG_RELATED_IMAGE_WIDTH, EXIF_TAG_RELATED_IMAGE_LENGTH, 0 },
};

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

ExifView::ExifView() : m_ExifViewImplPtr (new ExifViewImpl() ) {
	m_ExifViewImplPtr->m_iIdleLoadID = 0;
	m_ExifViewImplPtr->m_bLoaded     = FALSE;
	m_ExifViewImplPtr->m_pTreeView   = NULL;
	m_ExifViewImplPtr->m_pExifData   = NULL;

	m_ExifViewImplPtr->m_pScrolledWindow = gtk_scrolled_window_new();
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(m_ExifViewImplPtr->m_pScrolledWindow),GTK_POLICY_AUTOMATIC,GTK_POLICY_AUTOMATIC);
    g_object_ref(m_ExifViewImplPtr->m_pScrolledWindow);

    GtkWidget *placeholder_label = gtk_label_new("EXIF View (Under Construction - GTK4 Migration)");
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(m_ExifViewImplPtr->m_pScrolledWindow), placeholder_label);
    gtk_widget_set_visible(m_ExifViewImplPtr->m_pScrolledWindow, TRUE);

	g_signal_connect(m_ExifViewImplPtr->m_pScrolledWindow, "map", (GCallback) exif_view_map, m_ExifViewImplPtr.get());
}

ExifView::~ExifView() {}

GtkWidget * ExifView::GetWidget() {
	return m_ExifViewImplPtr->m_pScrolledWindow;
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
			m_ExifViewImplPtr->m_iIdleLoadID = 0;
		}
	} else {
		m_ExifViewImplPtr->m_bLoaded = FALSE;
	}
}

/* Commented out GtkUIManager related functions
void ExifView::SetUIManager(GtkUIManager *ui_manager) { ... }
*/

/* Commented out GtkTreeView/GtkTreeStore/GtkCellRenderer related static functions */
/*
static GtkTreeModel * create_numbers_model (void) { return NULL; }
static void exif_orientation_to_text (GtkTreeViewColumn *tree_column, GtkCellRenderer *cell, GtkTreeModel *tree_model, GtkTreeIter *iter, gpointer data) {}
static void exif_content_foreach_entry_func (ExifEntry *entry, void *user_data) {}
static void exif_tree_store_add_entry (ExifView::ExifViewImpl *pExifViewImpl, GtkTreeStore *store,GtkTreeIter *parent, GtkTreeIter *new_child, ExifEntry *entry) {}
static void exif_tree_store_update_iter_entry (ExifView::ExifViewImpl *pExifViewImpl, GtkTreeStore *store, GtkTreeIter *iter, ExifEntry *entry) {}
// static gboolean entry_focus_out ( GtkWidget *widget, GdkEvent *event, gpointer user_data) { return FALSE; } // GdkEventFocus changed to GdkEvent
static void exif_value_editing_started_callback (GtkCellRenderer *renderer, GtkCellEditable *editable, gchar *path, gpointer user_data) {}
static void exif_value_editing_canceled_callback (GtkCellRenderer *renderer, gpointer user_data) {}
static void exif_value_cell_edited_callback (GtkCellRendererText *cell, gchar *path_string, gchar *new_text, gpointer user_data) {}
static void exif_tree_update_thumbnail(ExifView::ExifViewImpl *pExifViewImpl, GtkTreeStore *store) {}
static void exif_tree_update_iter_entry (ExifView::ExifViewImpl *pExifViewImpl, GtkTreeIter *iter, ExifEntry *entry) {}
static void exif_tree_update_entry (ExifView::ExifViewImpl *pExifViewImpl, ExifIfd ifd, ExifTag tag) {}
*/

// Utility functions (mostly EXIF logic, less GTK dependent)

void ExifView::ExifViewImpl::PreferencesEventHandler::HandlePreferenceChanged(PreferencesEventPtr event) {}
