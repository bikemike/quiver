#include <config.h>
#include "BookmarksDlg.h"
#include "BookmarkRow.h" // Include the new GObject for our list store
#include <gtk/gtkmultiselection.h> // For GtkMultiSelection
#include <gtk/gtkbitset.h>         // For GtkBitset
#include <list>
#include <vector>
#include "Bookmarks.h"
#include "IBookmarksEventHandler.h"
#include "BookmarkAddEditDlg.h"
#include "QuiverStockIcons.h"

using namespace std;

class BookmarksDlg::BookmarksDlgPriv
{
public:
    BookmarksDlgPriv(BookmarksDlg *parent);
    ~BookmarksDlgPriv();

    void LoadWidgets();
    void UpdateUI();
    void SelectionChanged();
    void ConnectSignals();
    void setup_factories();

    BookmarksDlg*     m_pBookmarksDlg;
    GtkBuilder*         m_pGtkBuilder;
    BookmarksPtr      m_BookmarksPtr;

    bool m_bLoadedDlg;

    GtkWidget*             m_pWidget;
    GtkColumnView*         m_pColumnViewBookmarks;
    GListStore*            m_pBookmarkStore;
    GtkMultiSelection*     m_pSelectionModel;

    GtkButton*             m_pButtonMoveUp;
    GtkButton*             m_pButtonMoveDown;
    GtkButton*             m_pButtonAdd;
    GtkButton*             m_pButtonEdit;
    GtkButton*             m_pButtonRemove;
    GtkButton*             m_pButtonClose;

    class BookmarksEventHandler : public IBookmarksEventHandler
    {
    public:
        BookmarksEventHandler(BookmarksDlgPriv* parent) {this->parent = parent;};
        virtual void HandleBookmarkChanged(BookmarksEventPtr event);
    private:
        BookmarksDlgPriv* parent;
    };
    IBookmarksEventHandlerPtr m_BookmarksEventHandler;
};

// --- Static Callbacks ---
static void on_clicked(GtkButton *button, gpointer user_data);
static void selection_changed_cb(GObject *selection_model, GParamSpec *pspec, gpointer user_data);
static void column_view_activate_cb(GtkColumnView* self, guint position, gpointer user_data);
static void factory_setup_cb(GtkListItemFactory* factory, GtkListItem* list_item, gpointer user_data);
static void factory_bind_name_cb(GtkListItemFactory* factory, GtkListItem* list_item, gpointer user_data);
static void factory_bind_icon_cb(GtkListItemFactory* factory, GtkListItem* list_item, gpointer user_data);

BookmarksDlg::BookmarksDlg() : m_PrivPtr(new BookmarksDlg::BookmarksDlgPriv(this)) {}

void BookmarksDlg::Run()
{
    if (m_PrivPtr->m_bLoadedDlg) {
        gtk_widget_show(m_PrivPtr->m_pWidget);
    }
}

BookmarksDlg::BookmarksDlgPriv::BookmarksDlgPriv(BookmarksDlg *parent) :
        m_pBookmarksDlg(parent),
        m_BookmarksEventHandler(new BookmarksEventHandler(this))
{
    m_BookmarksPtr = Bookmarks::GetInstance();
    m_BookmarksPtr->AddEventHandler(m_BookmarksEventHandler);
    m_bLoadedDlg = false;

    m_pGtkBuilder = gtk_builder_new();
    GError *error = NULL;
    if (!gtk_builder_add_from_file(m_pGtkBuilder, QUIVER_DATADIR "/" "quiver.ui", &error)) {
        g_warning("Could not load UI file: %s", error->message);
        g_error_free(error);
        return;
    }
    LoadWidgets();
    setup_factories();
    UpdateUI();
    ConnectSignals();
}

BookmarksDlg::BookmarksDlgPriv::~BookmarksDlgPriv()
{
    m_BookmarksPtr->RemoveEventHandler(m_BookmarksEventHandler);
    if (m_pGtkBuilder) {
        g_object_unref(m_pGtkBuilder);
        m_pGtkBuilder = NULL;
    }
    if (m_pBookmarkStore) {
        g_object_unref(m_pBookmarkStore);
        m_pBookmarkStore = NULL;
    }
    if (m_pSelectionModel) {
        g_object_unref(m_pSelectionModel);
        m_pSelectionModel = NULL;
    }
}

void BookmarksDlg::BookmarksDlgPriv::LoadWidgets()
{
    if (!m_pGtkBuilder) return;

    m_pWidget = GTK_WIDGET(gtk_builder_get_object(m_pGtkBuilder, "BookmarksDialog"));
    m_pColumnViewBookmarks = GTK_COLUMN_VIEW(gtk_builder_get_object(m_pGtkBuilder, "treeview_bookmarks"));

    m_pButtonMoveUp   = GTK_BUTTON(gtk_builder_get_object(m_pGtkBuilder, "button_move_up"));
    m_pButtonMoveDown = GTK_BUTTON(gtk_builder_get_object(m_pGtkBuilder, "button_move_down"));
    m_pButtonAdd      = GTK_BUTTON(gtk_builder_get_object(m_pGtkBuilder, "button_add"));
    m_pButtonEdit     = GTK_BUTTON(gtk_builder_get_object(m_pGtkBuilder, "button_edit"));
    m_pButtonRemove   = GTK_BUTTON(gtk_builder_get_object(m_pGtkBuilder, "button_remove"));
    m_pButtonClose    = GTK_BUTTON(gtk_builder_get_object(m_pGtkBuilder, "button_close"));

    m_bLoadedDlg = (m_pWidget && m_pColumnViewBookmarks && m_pButtonAdd && m_pButtonEdit && m_pButtonRemove && m_pButtonClose);
}

void BookmarksDlg::BookmarksDlgPriv::setup_factories()
{
    if (!m_pColumnViewBookmarks) return;

    // --- Icon Column ---
    GtkListItemFactory *icon_factory = gtk_signal_list_item_factory_new();
    g_signal_connect(icon_factory, "setup", G_CALLBACK(factory_setup_cb), (gpointer)"icon");
    g_signal_connect(icon_factory, "bind", G_CALLBACK(factory_bind_icon_cb), this);
    GtkColumnViewColumn *icon_column = gtk_column_view_column_new("Icon", icon_factory);
    gtk_column_view_append_column(m_pColumnViewBookmarks, icon_column);

    // --- Name Column ---
    GtkListItemFactory *name_factory = gtk_signal_list_item_factory_new();
    g_signal_connect(name_factory, "setup", G_CALLBACK(factory_setup_cb), (gpointer)"name");
    g_signal_connect(name_factory, "bind", G_CALLBACK(factory_bind_name_cb), this);
    GtkColumnViewColumn *name_column = gtk_column_view_column_new("Bookmark", name_factory);
    gtk_column_view_append_column(m_pColumnViewBookmarks, name_column);
}

void BookmarksDlg::BookmarksDlgPriv::UpdateUI()
{
    if (!m_bLoadedDlg) return;

    g_clear_object(&m_pBookmarkStore);
    m_pBookmarkStore = g_list_store_new(BOOKMARK_TYPE_ROW);

    vector<Bookmark> bookmarks = m_BookmarksPtr->GetBookmarks();
    for (const auto& bm : bookmarks) {
        BookmarkRow *row = bookmark_row_new(bm.GetID(), bm.GetIcon().c_str(), bm.GetName().c_str());
        g_list_store_append(m_pBookmarkStore, row);
        g_object_unref(row);
    }

    g_clear_object(&m_pSelectionModel);
    m_pSelectionModel = gtk_multi_selection_new(G_LIST_MODEL(m_pBookmarkStore));
    gtk_column_view_set_model(m_pColumnViewBookmarks, GTK_SELECTION_MODEL(m_pSelectionModel));

    SelectionChanged();
}

void BookmarksDlg::BookmarksDlgPriv::ConnectSignals()
{
    if (!m_bLoadedDlg) return;

    g_signal_connect(m_pButtonMoveUp, "clicked", G_CALLBACK(on_clicked), this);
    g_signal_connect(m_pButtonMoveDown, "clicked", G_CALLBACK(on_clicked), this);
    g_signal_connect(m_pButtonAdd, "clicked", G_CALLBACK(on_clicked), this);
    g_signal_connect(m_pButtonEdit, "clicked", G_CALLBACK(on_clicked), this);
    g_signal_connect(m_pButtonRemove, "clicked", G_CALLBACK(on_clicked), this);
    g_signal_connect(m_pButtonClose, "clicked", G_CALLBACK(on_clicked), this);

    if (m_pSelectionModel) {
        g_signal_connect(m_pSelectionModel, "notify::selection", G_CALLBACK(selection_changed_cb), this);
    }
    if (m_pColumnViewBookmarks) {
        g_signal_connect(m_pColumnViewBookmarks, "activate", G_CALLBACK(column_view_activate_cb), this);
    }
}

void BookmarksDlg::BookmarksDlgPriv::SelectionChanged()
{
    if (!m_pSelectionModel) return;

    GtkBitset* selection = gtk_selection_model_get_selection(GTK_SELECTION_MODEL(m_pSelectionModel));
    guint n_selected = gtk_bitset_get_size(selection);

    gtk_widget_set_sensitive(GTK_WIDGET(m_pButtonRemove), n_selected > 0);
    gtk_widget_set_sensitive(GTK_WIDGET(m_pButtonEdit), n_selected == 1);
    gtk_widget_set_sensitive(GTK_WIDGET(m_pButtonMoveUp), n_selected == 1);
    gtk_widget_set_sensitive(GTK_WIDGET(m_pButtonMoveDown), n_selected == 1);

    gtk_bitset_unref(selection);
}

void BookmarksDlg::BookmarksDlgPriv::BookmarksEventHandler::HandleBookmarkChanged(BookmarksEventPtr event)
{
    if (parent->m_bLoadedDlg) {
        parent->UpdateUI();
    }
}

static void factory_setup_cb(GtkListItemFactory* factory, GtkListItem* list_item, gpointer user_data)
{
    const char *type = (const char *)user_data;
    GtkWidget *widget = NULL;

    if (g_strcmp0(type, "icon") == 0) {
        widget = gtk_image_new();
    } else if (g_strcmp0(type, "name") == 0) {
        widget = gtk_label_new(NULL);
        gtk_label_set_xalign(GTK_LABEL(widget), 0.0);
    }
    if(widget) {
        gtk_list_item_set_child(list_item, widget);
    }
}

static void factory_bind_icon_cb(GtkListItemFactory* factory, GtkListItem* list_item, gpointer user_data)
{
    GtkWidget *image = gtk_list_item_get_child(list_item);
    BookmarkRow *row = BOOKMARK_ROW(gtk_list_item_get_item(list_item));
    if (row) {
        gtk_image_set_from_icon_name(GTK_IMAGE(image), bookmark_row_get_icon_name(row));
    }
}

static void factory_bind_name_cb(GtkListItemFactory* factory, GtkListItem* list_item, gpointer user_data)
{
    GtkWidget *label = gtk_list_item_get_child(list_item);
    BookmarkRow *row = BOOKMARK_ROW(gtk_list_item_get_item(list_item));
    if (row) {
        gtk_label_set_text(GTK_LABEL(label), bookmark_row_get_name(row));
    }
}

static void selection_changed_cb(GObject *selection_model, GParamSpec *pspec, gpointer user_data)
{
    BookmarksDlg::BookmarksDlgPriv *priv = static_cast<BookmarksDlg::BookmarksDlgPriv*>(user_data);
    priv->SelectionChanged();
}

static void column_view_activate_cb(GtkColumnView* self, guint position, gpointer user_data)
{
    BookmarksDlg::BookmarksDlgPriv *priv = static_cast<BookmarksDlg::BookmarksDlgPriv*>(user_data);
    on_clicked(priv->m_pButtonEdit, priv);
}

static void on_clicked(GtkButton *button, gpointer user_data)
{
    BookmarksDlg::BookmarksDlgPriv *priv = static_cast<BookmarksDlg::BookmarksDlgPriv*>(user_data);
    if (!priv) return;

    if (button == priv->m_pButtonClose) {
        gtk_window_destroy(GTK_WINDOW(priv->m_pWidget));
        return;
    }

    g_warning("Button functionality (Add, Edit, Remove, Move) is not fully implemented yet.");
}
