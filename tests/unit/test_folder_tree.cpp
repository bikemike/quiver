#include <catch2/catch_test_macros.hpp>
#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <cstring>
#include <unistd.h>
#include "FolderTree.h"
#include "QuiverUtils.h"
#include "QuiverFile.h"
#include "Bookmarks.h"
#include "Preferences.h"
#include "IFolderTreeEventHandler.h"
#include "test_helpers.h"

/* The bookmark tests below persist to the config file, and the default test
 * config (/tmp/quiver_test_config.ini, test_main.cpp) is shared by every test
 * invocation.  Two things went wrong because of that:
 *
 *  - a bookmark left in the file by one run is loaded by the next, so "bookmark
 *    checkbox checks the whole bookmark" saw 4 rows where it added 2 and
 *    expected 2, and failed on every subsequent run;
 *  - the "Selection and Keyboard Navigation" test unlinks the config file when
 *    it did not exist beforehand, which would destroy state other tests share.
 *
 * Redirect to a private file for the duration of the test, the same way
 * test_bookmarks.cpp does, so neither can happen. */
struct FolderTreeTestConfigFixture {
    char m_tmpPath[256];
    char m_prevConfig[256];

    FolderTreeTestConfigFixture() {
        strncpy(m_prevConfig, g_szConfigFilePath, sizeof(m_prevConfig) - 1);
        m_prevConfig[sizeof(m_prevConfig) - 1] = '\0';
        strncpy(m_tmpPath, "/tmp/quiver_ftree_cfg_XXXXXX.ini", sizeof(m_tmpPath) - 1);
        m_tmpPath[sizeof(m_tmpPath) - 1] = '\0';
        int fd = g_mkstemp(m_tmpPath);
        if (fd >= 0) {
            close(fd);
        }
        /* g_szConfigFilePath is declared "extern gchar[]", so its size is not
         * available here; the buffers match the definition in test_main.cpp. */
        strncpy(g_szConfigFilePath, m_tmpPath, sizeof(m_tmpPath) - 1);
        g_szConfigFilePath[sizeof(m_tmpPath) - 1] = '\0';
        Bookmarks::Reset();
        Preferences::Reset();
    }

    ~FolderTreeTestConfigFixture() {
        Bookmarks::Reset();
        Preferences::Reset();
        strncpy(g_szConfigFilePath, m_prevConfig, sizeof(m_prevConfig) - 1);
        g_szConfigFilePath[sizeof(m_prevConfig) - 1] = '\0';
        g_unlink(m_tmpPath);
    }
};

static std::string folder_tree_test_make_temp_dir()
{
    char tpl[] = "/tmp/quiver_ftree_test_XXXXXX";
    char* dir = g_mkdtemp(tpl);
    REQUIRE(dir != nullptr);
    return std::string(dir);
}

static std::string folder_tree_test_path_to_uri(const std::string& path)
{
    gchar* uri = g_filename_to_uri(path.c_str(), nullptr, nullptr);
    REQUIRE(uri != nullptr);
    std::string str(uri);
    g_free(uri);
    return str;
}

// Records bookmark-open events (with bookmark URIs and recursion flag) that
// the folder tree emits, mirroring what the browser listens for.
class FolderTreeTestBookmarkHandler : public IFolderTreeEventHandler
{
public:
    int bookmarkOpens = 0;
    bool lastRecursive = false;
    std::list<std::string> lastUris;

    void HandleSelectionChanged(FolderTreeEventPtr event) override
    {
        if (event && event->HasBookmarkData())
        {
            bookmarkOpens++;
            lastRecursive = event->GetRecursive();
            lastUris = event->GetURIs();
        }
    }
};

class FolderTreeTestSelectionHandler : public IFolderTreeEventHandler
{
public:
    int selectionEvents = 0;

    void HandleSelectionChanged(FolderTreeEventPtr event) override
    {
        (void)event;
        selectionEvents++;
    }
};

TEST_CASE("FolderTree Selection and Keyboard Navigation", "[unit][foldertree][gui]")
{
    REQUIRE_DISPLAY();

    /* One of this test's sections writes and deletes the config file. */
    FolderTreeTestConfigFixture configFixture;

    SECTION("Enter key on focused row checks it and unchecks others")
    {
        FolderTreePtr tree(new FolderTree());
        GtkWidget* box = tree->GetWidget();
        REQUIRE(box != nullptr);
        REQUIRE(GTK_IS_BOX(box));

        GtkWidget* widget = tree->GetTreeWidget();
        REQUIRE(widget != nullptr);
        REQUIRE(GTK_IS_LIST_VIEW(widget));

        GtkListView* lv = GTK_LIST_VIEW(widget);
        GtkSelectionModel* sel = gtk_list_view_get_model(lv);
        REQUIRE(sel != nullptr);

        guint total = g_list_model_get_n_items(G_LIST_MODEL(sel));
        REQUIRE(total > 0);

        // Locate first GtkTreeListRow item in unified selection model
        guint tree0 = G_MAXUINT;
        for (guint i = 0; i < total; i++)
        {
            gpointer item = g_list_model_get_item(G_LIST_MODEL(sel), i);
            if (item)
            {
                if (GTK_IS_TREE_LIST_ROW(item))
                {
                    tree0 = i;
                    g_object_unref(item);
                    break;
                }
                g_object_unref(item);
            }
        }
        REQUIRE(tree0 != G_MAXUINT);
        guint tree1 = tree0 + 1;

        // Select tree0 in selection model (unselect rest)
        gtk_selection_model_select_item(sel, tree0, TRUE);

        // Find key controller on widget
        GtkEventController* key_ctrl = nullptr;
        GListModel* controllers = gtk_widget_observe_controllers(widget);
        guint n_ctrl = g_list_model_get_n_items(controllers);
        for (guint i = 0; i < n_ctrl; i++)
        {
            gpointer item = g_list_model_get_item(controllers, i);
            if (GTK_IS_EVENT_CONTROLLER_KEY(item))
            {
                key_ctrl = GTK_EVENT_CONTROLLER(item);
                g_object_unref(item);
                break;
            }
            g_object_unref(item);
        }
        g_object_unref(controllers);
        REQUIRE(key_ctrl != nullptr);

        // Simulate plain Enter on tree0
        gboolean handled = FALSE;
        g_signal_emit_by_name(key_ctrl, "key-pressed", GDK_KEY_Return, 0, (GdkModifierType)0, &handled);
        REQUIRE(handled == TRUE);

        std::list<std::string> selected1 = tree->GetSelectedFolders();
        REQUIRE(selected1.size() == 1);

        // Move selection to tree1 (unselect rest) and press plain Enter: tree1 checked, tree0 unchecked
        if (tree1 < total)
        {
            gtk_selection_model_select_item(sel, tree1, TRUE);
            g_signal_emit_by_name(key_ctrl, "key-pressed", GDK_KEY_Return, 0, (GdkModifierType)0, &handled);
            REQUIRE(handled == TRUE);

            std::list<std::string> selected2 = tree->GetSelectedFolders();
            REQUIRE(selected2.size() == 1);
            REQUIRE(selected2.front() != selected1.front());

            // Select tree0 and press Ctrl+Enter: adds tree0 (now both tree0 and tree1 checked)
            gtk_selection_model_select_item(sel, tree0, TRUE);
            g_signal_emit_by_name(key_ctrl, "key-pressed", GDK_KEY_Return, 0, GDK_CONTROL_MASK, &handled);
            REQUIRE(handled == TRUE);

            std::list<std::string> selected3 = tree->GetSelectedFolders();
            REQUIRE(selected3.size() == 2);

            // Press Ctrl+Enter on tree0 again: subtracts tree0 (now only tree1 checked)
            g_signal_emit_by_name(key_ctrl, "key-pressed", GDK_KEY_Return, 0, GDK_CONTROL_MASK, &handled);
            REQUIRE(handled == TRUE);

            std::list<std::string> selected4 = tree->GetSelectedFolders();
            REQUIRE(selected4.size() == 1);
            REQUIRE(selected4.front() == selected2.front());
        }
    }

    SECTION("Mouse click on a folder row checks it off")
    {
        FolderTreePtr tree(new FolderTree());
        boost::shared_ptr<FolderTreeTestSelectionHandler> selHandler(
            new FolderTreeTestSelectionHandler());
        tree->AddEventHandler(boost::static_pointer_cast<IEventHandler>(selHandler));

        GtkWidget* box = tree->GetWidget();
        REQUIRE(box != nullptr);
        REQUIRE(GTK_IS_BOX(box));

        GtkWidget* widget = tree->GetTreeWidget();
        REQUIRE(widget != nullptr);
        REQUIRE(GTK_IS_LIST_VIEW(widget));

        // Window presentation so list items are created and bound
        GtkWidget* win = gtk_window_new();
        gtk_window_set_child(GTK_WINDOW(win), box);
        gtk_window_set_default_size(GTK_WINDOW(win), 400, 400);
        gtk_window_present(GTK_WINDOW(win));

        while (g_main_context_iteration(NULL, FALSE));

        // Look for checkable row widgets in the list view
        auto find_gesture = [](GtkWidget* w, auto& self) -> GtkGestureClick* {
            GListModel* ctrls = gtk_widget_observe_controllers(w);
            if (ctrls)
            {
                guint n = g_list_model_get_n_items(ctrls);
                for (guint i = 0; i < n; i++)
                {
                    gpointer c = g_list_model_get_item(ctrls, i);
                    if (GTK_IS_GESTURE_CLICK(c) && g_object_get_data(G_OBJECT(w), "dir-item") != nullptr)
                    {
                        GtkGestureClick* gc = GTK_GESTURE_CLICK(c);
                        g_object_unref(c);
                        g_object_unref(ctrls);
                        return gc;
                    }
                    g_object_unref(c);
                }
                g_object_unref(ctrls);
            }
            for (GtkWidget* ch = gtk_widget_get_first_child(w); ch != nullptr; ch = gtk_widget_get_next_sibling(ch))
            {
                GtkGestureClick* gc = self(ch, self);
                if (gc) return gc;
            }
            return nullptr;
        };

        std::vector<GtkWidget*> row_widgets;
        for (GtkWidget* ch = gtk_widget_get_first_child(widget); ch != nullptr; ch = gtk_widget_get_next_sibling(ch))
        {
            if (find_gesture(ch, find_gesture) != nullptr)
                row_widgets.push_back(ch);
        }
        REQUIRE(row_widgets.size() >= 2);

        GtkWidget* row0_widget = row_widgets[0];
        GtkGestureClick* row_gesture0 = find_gesture(row0_widget, find_gesture);
        REQUIRE(row_gesture0 != nullptr);

        // Simulate mouse click on folder row 0 body (icon/label at x=50, y=10)
        g_signal_emit_by_name(row_gesture0, "pressed", 1, 50.0, 10.0);
        g_signal_emit_by_name(row_gesture0, "released", 1, 50.0, 10.0);
        while (g_main_context_iteration(NULL, FALSE));

        std::list<std::string> selList0 = tree->GetSelectedFolders();
        REQUIRE(selList0.size() == 1);
        std::string uri0 = selList0.front();
        REQUIRE(selHandler->selectionEvents == 1);

        // Simulate mouse click on the empty white space of row 1 (far to the right of label at x=350, y=10)
        GtkWidget* row1_widget = row_widgets[1];
        GtkGestureClick* row_gesture1 = find_gesture(row1_widget, find_gesture);
        REQUIRE(row_gesture1 != nullptr);

            g_signal_emit_by_name(row_gesture1, "pressed", 1, 350.0, 10.0);
            g_signal_emit_by_name(row_gesture1, "released", 1, 350.0, 10.0);
            while (g_main_context_iteration(NULL, FALSE));

            std::list<std::string> selList1 = tree->GetSelectedFolders();
            REQUIRE(selList1.size() == 1);
            REQUIRE(selList1.front() != uri0);
            REQUIRE(selHandler->selectionEvents == 2);

            // Clicking white space of row 0 again (at x=300, y=5) unchecks row 1 and checks row 0
            g_signal_emit_by_name(row_gesture0, "pressed", 1, 300.0, 5.0);
            g_signal_emit_by_name(row_gesture0, "released", 1, 300.0, 5.0);
            while (g_main_context_iteration(NULL, FALSE));

            std::list<std::string> selListBack = tree->GetSelectedFolders();
            REQUIRE(selListBack.size() == 1);
            REQUIRE(selListBack.front() == uri0);
            REQUIRE(selHandler->selectionEvents == 3);

        gtk_window_set_child(GTK_WINDOW(win), nullptr);
        gtk_window_destroy(GTK_WINDOW(win));
    }

    SECTION("Focus moved independently of selection (e.g. Ctrl+Arrow navigation)")
    {
        FolderTreePtr tree(new FolderTree());
        GtkWidget* box = tree->GetWidget();
        REQUIRE(box != nullptr);
        REQUIRE(GTK_IS_BOX(box));

        GtkWidget* widget = tree->GetTreeWidget();
        REQUIRE(widget != nullptr);
        REQUIRE(GTK_IS_LIST_VIEW(widget));

        GtkWidget* win = gtk_window_new();
        gtk_window_set_child(GTK_WINDOW(win), box);
        gtk_window_set_default_size(GTK_WINDOW(win), 400, 400);
        gtk_window_present(GTK_WINDOW(win));
        while (g_main_context_iteration(NULL, FALSE));

        GtkListView* lv = GTK_LIST_VIEW(widget);
        GtkSelectionModel* sel = gtk_list_view_get_model(lv);
        REQUIRE(sel != nullptr);
        guint total = g_list_model_get_n_items(G_LIST_MODEL(sel));
        if (total > 1)
        {
            guint tree0 = G_MAXUINT;
            for (guint i = 0; i < total; i++)
            {
                gpointer item = g_list_model_get_item(G_LIST_MODEL(sel), i);
                if (item)
                {
                    if (GTK_IS_TREE_LIST_ROW(item))
                    {
                        tree0 = i;
                        g_object_unref(item);
                        break;
                    }
                    g_object_unref(item);
                }
            }
            REQUIRE(tree0 != G_MAXUINT);
            guint tree1 = tree0 + 1;

            auto get_li = [](GtkWidget* w) -> GtkListItem* {
                GtkListItem* li = static_cast<GtkListItem*>(g_object_get_data(G_OBJECT(w), "list-item"));
                if (!li && gtk_widget_get_first_child(w))
                    li = static_cast<GtkListItem*>(g_object_get_data(G_OBJECT(gtk_widget_get_first_child(w)), "list-item"));
                return li;
            };

            GtkWidget* row0 = nullptr;
            GtkWidget* row1 = nullptr;
            for (GtkWidget* ch = gtk_widget_get_first_child(widget); ch != nullptr; ch = gtk_widget_get_next_sibling(ch))
            {
                GtkListItem* li = get_li(ch);
                if (li)
                {
                    guint p = gtk_list_item_get_position(li);
                    if (p == tree0) row0 = ch;
                    if (p == tree1) row1 = ch;
                }
            }
            REQUIRE(row0 != nullptr);
            REQUIRE(row1 != nullptr);

            GtkEventController* key_ctrl = nullptr;
            GListModel* controllers = gtk_widget_observe_controllers(widget);
            guint n_ctrl = g_list_model_get_n_items(controllers);
            for (guint i = 0; i < n_ctrl; i++)
            {
                gpointer item = g_list_model_get_item(controllers, i);
                if (GTK_IS_EVENT_CONTROLLER_KEY(item))
                {
                    key_ctrl = GTK_EVENT_CONTROLLER(item);
                    g_object_unref(item);
                    break;
                }
                g_object_unref(item);
            }
            g_object_unref(controllers);
            REQUIRE(key_ctrl != nullptr);

            // Plain Enter on tree0: tree0 checked and selected
            gboolean handled = FALSE;
            gtk_selection_model_select_item(sel, tree0, TRUE);
            g_signal_emit_by_name(key_ctrl, "key-pressed", GDK_KEY_Return, 0, (GdkModifierType)0, &handled);
            REQUIRE(handled == TRUE);
            REQUIRE(tree->GetSelectedFolders().size() == 1);

            // Now move keyboard FOCUS to row1 (outline), keeping tree0 selected in sel (blue background)
            gtk_widget_grab_focus(row1);
            while (g_main_context_iteration(NULL, FALSE));

            // Press plain Enter: should check row1, uncheck tree0, and update selection model
            g_signal_emit_by_name(key_ctrl, "key-pressed", GDK_KEY_Return, 0, (GdkModifierType)0, &handled);
            REQUIRE(handled == TRUE);
            std::list<std::string> selList = tree->GetSelectedFolders();
            REQUIRE(selList.size() == 1);
            REQUIRE(gtk_selection_model_is_selected(sel, tree1) == TRUE);
            REQUIRE(gtk_selection_model_is_selected(sel, tree0) == FALSE);

            // Now move focus back to row0, while tree1 is selected
            gtk_widget_grab_focus(row0);
            while (g_main_context_iteration(NULL, FALSE));

            // Press Ctrl+Enter on focused row0: should toggle row0 (now both tree0 and tree1 checked)
            g_signal_emit_by_name(key_ctrl, "key-pressed", GDK_KEY_Return, 0, GDK_CONTROL_MASK, &handled);
            REQUIRE(handled == TRUE);
            REQUIRE(tree->GetSelectedFolders().size() == 2);
            REQUIRE(gtk_selection_model_is_selected(sel, tree0) == TRUE);
            REQUIRE(gtk_selection_model_is_selected(sel, tree1) == TRUE);

            // Press Ctrl+Enter on focused row0 again: should uncheck row0 (now only tree1 checked)
            g_signal_emit_by_name(key_ctrl, "key-pressed", GDK_KEY_Return, 0, GDK_CONTROL_MASK, &handled);
            REQUIRE(handled == TRUE);
            REQUIRE(tree->GetSelectedFolders().size() == 1);
            REQUIRE(gtk_selection_model_is_selected(sel, tree0) == FALSE);
            REQUIRE(gtk_selection_model_is_selected(sel, tree1) == TRUE);
        }

        gtk_window_set_child(GTK_WINDOW(win), nullptr);
        gtk_window_destroy(GTK_WINDOW(win));
    }

    SECTION("Multi-selection Spacebar and Enter behavior")
    {
        FolderTreePtr tree(new FolderTree());
        GtkWidget* box = tree->GetWidget();
        REQUIRE(box != nullptr);
        REQUIRE(GTK_IS_BOX(box));

        GtkWidget* widget = tree->GetTreeWidget();
        REQUIRE(widget != nullptr);
        REQUIRE(GTK_IS_LIST_VIEW(widget));

        GtkWidget* win = gtk_window_new();
        gtk_window_set_child(GTK_WINDOW(win), box);
        gtk_window_set_default_size(GTK_WINDOW(win), 400, 400);
        gtk_window_present(GTK_WINDOW(win));
        while (g_main_context_iteration(NULL, FALSE));

        GtkListView* lv = GTK_LIST_VIEW(widget);
        GtkSelectionModel* sel = gtk_list_view_get_model(lv);
        REQUIRE(sel != nullptr);
        guint total = g_list_model_get_n_items(G_LIST_MODEL(sel));
        if (total >= 2)
        {
            // Find key controller
            GtkEventController* key_ctrl = nullptr;
            GListModel* controllers = gtk_widget_observe_controllers(widget);
            guint n_ctrl = g_list_model_get_n_items(controllers);
            for (guint i = 0; i < n_ctrl; i++)
            {
                gpointer item = g_list_model_get_item(controllers, i);
                if (GTK_IS_EVENT_CONTROLLER_KEY(item))
                {
                    key_ctrl = GTK_EVENT_CONTROLLER(item);
                    g_object_unref(item);
                    break;
                }
                g_object_unref(item);
            }
            g_object_unref(controllers);
            REQUIRE(key_ctrl != nullptr);

            guint tree0 = G_MAXUINT;
            for (guint i = 0; i < total; i++)
            {
                gpointer item = g_list_model_get_item(G_LIST_MODEL(sel), i);
                if (item)
                {
                    if (GTK_IS_TREE_LIST_ROW(item))
                    {
                        tree0 = i;
                        g_object_unref(item);
                        break;
                    }
                    g_object_unref(item);
                }
            }
            REQUIRE(tree0 != G_MAXUINT);
            guint tree1 = tree0 + 1;

            auto get_li = [](GtkWidget* w) -> GtkListItem* {
                GtkListItem* li = static_cast<GtkListItem*>(g_object_get_data(G_OBJECT(w), "list-item"));
                if (!li && gtk_widget_get_first_child(w))
                    li = static_cast<GtkListItem*>(g_object_get_data(G_OBJECT(gtk_widget_get_first_child(w)), "list-item"));
                return li;
            };

            GtkWidget* row0 = nullptr;
            for (GtkWidget* ch = gtk_widget_get_first_child(widget); ch != nullptr; ch = gtk_widget_get_next_sibling(ch))
            {
                GtkListItem* li = get_li(ch);
                if (li && gtk_list_item_get_position(li) == tree0)
                {
                    row0 = ch;
                    break;
                }
            }
            REQUIRE(row0 != nullptr);

            // Multi-select tree0 and tree1 in selection model
            gtk_selection_model_unselect_all(sel);
            gtk_selection_model_select_item(sel, tree0, FALSE);
            gtk_selection_model_select_item(sel, tree1, FALSE);
            REQUIRE(gtk_selection_model_is_selected(sel, tree0) == TRUE);
            REQUIRE(gtk_selection_model_is_selected(sel, tree1) == TRUE);

            // Initially both are unchecked
            REQUIRE(tree->GetSelectedFolders().size() == 0);

            // 1. Press Spacebar: both are unchecked -> all selected items become checked off
            gboolean handled = FALSE;
            g_signal_emit_by_name(key_ctrl, "key-pressed", GDK_KEY_space, 0, (GdkModifierType)0, &handled);
            REQUIRE(handled == TRUE);
            REQUIRE(tree->GetSelectedFolders().size() == 2);

            // 2. Press Spacebar again: all selected items are checked -> all selected items become unchecked
            g_signal_emit_by_name(key_ctrl, "key-pressed", GDK_KEY_space, 0, (GdkModifierType)0, &handled);
            REQUIRE(handled == TRUE);
            REQUIRE(tree->GetSelectedFolders().size() == 0);

            // 3. Press Spacebar again: any unchecked -> all selected items become checked
            g_signal_emit_by_name(key_ctrl, "key-pressed", GDK_KEY_space, 0, (GdkModifierType)0, &handled);
            REQUIRE(handled == TRUE);
            REQUIRE(tree->GetSelectedFolders().size() == 2);

            // 4. Test partial check: uncheck tree0 via Ctrl+Enter on row0
            gtk_widget_grab_focus(row0);
            while (g_main_context_iteration(NULL, FALSE));

            g_signal_emit_by_name(key_ctrl, "key-pressed", GDK_KEY_Return, 0, GDK_CONTROL_MASK, &handled);
            REQUIRE(handled == TRUE);
            REQUIRE(tree->GetSelectedFolders().size() == 1); // only tree1 checked

            // Re-select tree0 and tree1 in sel
            gtk_selection_model_unselect_all(sel);
            gtk_selection_model_select_item(sel, tree0, FALSE);
            gtk_selection_model_select_item(sel, tree1, FALSE);
            // Now tree0 is unchecked and tree1 is checked; Spacebar should check both
            g_signal_emit_by_name(key_ctrl, "key-pressed", GDK_KEY_space, 0, (GdkModifierType)0, &handled);
            REQUIRE(handled == TRUE);
            REQUIRE(tree->GetSelectedFolders().size() == 2);

            // 5. Test Enter on multi-select:
            // First uncheck all selected with Spacebar
            g_signal_emit_by_name(key_ctrl, "key-pressed", GDK_KEY_space, 0, (GdkModifierType)0, &handled);
            REQUIRE(handled == TRUE);
            REQUIRE(tree->GetSelectedFolders().size() == 0);

            // Multi-selection is still selected in sel (tree0 and tree1).
            // Pressing plain Enter for multi-select should check all selected items
            g_signal_emit_by_name(key_ctrl, "key-pressed", GDK_KEY_Return, 0, (GdkModifierType)0, &handled);
            REQUIRE(handled == TRUE);
            REQUIRE(tree->GetSelectedFolders().size() == 2);
        }

        gtk_window_set_child(GTK_WINDOW(win), nullptr);
        gtk_window_destroy(GTK_WINDOW(win));
    }

    SECTION("Shortcuts list, separator, and roots layout")
    {
        FolderTreePtr tree(new FolderTree());
        GtkWidget* box = tree->GetWidget();
        REQUIRE(box != nullptr);
        REQUIRE(GTK_IS_BOX(box));

        GtkWidget* sc_widget = tree->GetShortcutsWidget();
        REQUIRE(sc_widget != nullptr);
        REQUIRE(GTK_IS_LIST_VIEW(sc_widget));

        GtkWidget* tree_widget = tree->GetTreeWidget();
        REQUIRE(tree_widget != nullptr);
        REQUIRE(GTK_IS_LIST_VIEW(tree_widget));

        // Single outer scrolled window wrapping unified tree_widget directly (no GtkViewport)
        GtkWidget* main_sw = gtk_widget_get_first_child(box);
        REQUIRE(main_sw != nullptr);
        REQUIRE(GTK_IS_SCROLLED_WINDOW(main_sw));
        GtkWidget* child = gtk_scrolled_window_get_child(GTK_SCROLLED_WINDOW(main_sw));
        REQUIRE(child == tree_widget);
        REQUIRE(GTK_IS_LIST_VIEW(child));

        // Verify CSS classes and styling attributes
        REQUIRE(gtk_widget_has_css_class(box, "sidebar"));
        REQUIRE(gtk_widget_has_css_class(box, "quiver-sidebar"));
        REQUIRE(gtk_widget_has_css_class(tree_widget, "compact-tree"));

        // Verify shortcuts has items
        GtkSelectionModel* sc_sel = gtk_list_view_get_model(GTK_LIST_VIEW(sc_widget));
        REQUIRE(sc_sel != nullptr);
        guint sc_count = g_list_model_get_n_items(G_LIST_MODEL(sc_sel));
        REQUIRE(sc_count >= 1);
    }

    SECTION("Shortcuts and Tree selection synchronization")
    {
        FolderTreePtr tree(new FolderTree());
        const char* home_dir = g_get_home_dir();
        REQUIRE(home_dir != nullptr);
        GFile* home_f = g_file_new_for_path(home_dir);
        char* home_uri = g_file_get_uri(home_f);
        g_object_unref(home_f);

        std::list<std::string> sel_uris;
        sel_uris.push_back(home_uri);
        tree->SetSelectedFolders(sel_uris);

        std::list<std::string> res = tree->GetSelectedFolders();
        REQUIRE(res.size() == 1);
        REQUIRE(res.front() == home_uri);

        g_free(home_uri);
    }

    SECTION("Bookmarks added to Bookmarks instance appear in Bookmarks section")
    {
        FolderTreePtr tree(new FolderTree());
        GtkWidget* bm_widget = tree->GetBookmarksWidget();
        REQUIRE(bm_widget != nullptr);
        GtkSelectionModel* bm_sel = gtk_list_view_get_model(GTK_LIST_VIEW(bm_widget));
        guint count_before = g_list_model_get_n_items(G_LIST_MODEL(bm_sel));

        GtkWidget* sc_widget = tree->GetShortcutsWidget();
        GtkSelectionModel* sc_sel = gtk_list_view_get_model(GTK_LIST_VIEW(sc_widget));
        guint sc_count_before = g_list_model_get_n_items(G_LIST_MODEL(sc_sel));

        BookmarksPtr bm = Bookmarks::GetInstance();
        std::string test_uri = "file:///tmp/quiver_test_bm_" + std::to_string(g_random_int());
        std::list<std::string> uris = {test_uri};
        Bookmark b("Custom Test Bookmark", "desc", "folder-symbolic", uris, false);
        bm->AddBookmark(b);

        guint count_after = g_list_model_get_n_items(G_LIST_MODEL(bm_sel));
        REQUIRE(count_after == count_before + 1);

        /* The dedicated bookmarks section must render the new row while the
         * shortcuts list (Home/Desktop/...) is untouched by bookmarks. */
        guint sc_count_after = g_list_model_get_n_items(G_LIST_MODEL(sc_sel));
        REQUIRE(sc_count_after == sc_count_before);

        int added_id = bm->GetBookmarks().back().GetID();
        bm->Remove(added_id);
    }

    SECTION("Bookmarks loaded from the config file appear in Bookmarks section")
    {
        REQUIRE(g_szConfigFilePath[0] != '\0');

        // Preserve the pre-existing config so other tests are unaffected.
        gchar* prev_contents = nullptr;
        gsize prev_len = 0;
        bool had_config = g_file_get_contents(g_szConfigFilePath, &prev_contents, &prev_len, nullptr);

        auto write_config = [](const char* name) {
            GKeyFile* kf = g_key_file_new();
            if (name != nullptr)
            {
                const gchar* ids[] = { "0" };
                g_key_file_set_string_list(kf, "Bookmarks", "default", ids, 1);
                g_key_file_set_string(kf, "Bookmark_0", "name", name);
                g_key_file_set_string(kf, "Bookmark_0", "description", "startup-load test");
                g_key_file_set_string(kf, "Bookmark_0", "icon", "folder-symbolic");
                g_key_file_set_boolean(kf, "Bookmark_0", "recursive", FALSE);
                const gchar* uris[] = { "file:///tmp/quiver_startup_bm_dir" };
                g_key_file_set_string_list(kf, "Bookmark_0", "uris", uris, 1);
            }
            gsize len = 0;
            gchar* data = g_key_file_to_data(kf, &len, nullptr);
            g_key_file_free(kf);
            g_file_set_contents(g_szConfigFilePath, data, (gssize)len, nullptr);
            g_free(data);
        };

        // Baseline: a FolderTree with a config that contains no bookmarks.
        write_config(nullptr);
        Bookmarks::Reset();
        Preferences::Reset();
        FolderTreePtr tree_a(new FolderTree());
        GtkWidget* sc_a = tree_a->GetShortcutsWidget();
        REQUIRE(sc_a != nullptr);
        GtkSelectionModel* sel_a = gtk_list_view_get_model(GTK_LIST_VIEW(sc_a));
        guint count_a = g_list_model_get_n_items(G_LIST_MODEL(sel_a));
        REQUIRE(count_a >= 1);
        GtkWidget* bm_a = tree_a->GetBookmarksWidget();
        REQUIRE(bm_a != nullptr);
        GtkSelectionModel* bm_sel_a = gtk_list_view_get_model(GTK_LIST_VIEW(bm_a));
        guint bm_count_a = g_list_model_get_n_items(G_LIST_MODEL(bm_sel_a));
        REQUIRE(bm_count_a == 0);
        tree_a.reset();
        Bookmarks::Reset();
        Preferences::Reset();

        // Now the same config plus one pre-existing bookmark: must gain a row.
        write_config("Startup Load Bookmark");
        Bookmarks::Reset();
        Preferences::Reset();
        FolderTreePtr tree_b(new FolderTree());
        GtkWidget* sc_b = tree_b->GetShortcutsWidget();
        REQUIRE(sc_b != nullptr);
        GtkSelectionModel* sel_b = gtk_list_view_get_model(GTK_LIST_VIEW(sc_b));
        guint count_b = g_list_model_get_n_items(G_LIST_MODEL(sel_b));
        GtkWidget* bm_b = tree_b->GetBookmarksWidget();
        REQUIRE(bm_b != nullptr);
        GtkSelectionModel* bm_sel_b = gtk_list_view_get_model(GTK_LIST_VIEW(bm_b));
        guint bm_count_b = g_list_model_get_n_items(G_LIST_MODEL(bm_sel_b));
        REQUIRE(bm_count_b == 1);
        REQUIRE(count_b == count_a);
        tree_b.reset();

        // Clean up: drop the test bookmark and restore the original config.
        BookmarksPtr bm = Bookmarks::GetInstance();
        if (bm)
        {
            std::vector<Bookmark> bms = bm->GetBookmarks();
            for (const auto& b : bms)
                if (b.GetName() == "Startup Load Bookmark")
                    bm->Remove(b.GetID());
        }
        Bookmarks::Reset();
        Preferences::Reset();

        if (had_config)
        {
            g_file_set_contents(g_szConfigFilePath, prev_contents, (gssize)prev_len, nullptr);
            g_free(prev_contents);
        }
        else
        {
            g_unlink(g_szConfigFilePath);
        }
    }

    SECTION("Shortcuts multi-selection spacebar preserves selection when unchecking")
    {
        const char* home = g_get_home_dir();
        if (home) {
            std::string pic = std::string(home) + "/Pictures";
            std::string vid = std::string(home) + "/Videos";
            g_mkdir_with_parents(pic.c_str(), 0755);
            g_mkdir_with_parents(vid.c_str(), 0755);
        }

        FolderTreePtr tree(new FolderTree());
        GtkWidget* win = gtk_window_new();
        GtkWidget* box = tree->GetWidget();
        gtk_window_set_child(GTK_WINDOW(win), box);
        gtk_window_set_default_size(GTK_WINDOW(win), 300, 500);
        gtk_window_present(GTK_WINDOW(win));
        while (g_main_context_iteration(NULL, FALSE));

        GtkWidget* sc_widget = tree->GetShortcutsWidget();
        REQUIRE(sc_widget != nullptr);
        GtkSelectionModel* sc_sel = gtk_list_view_get_model(GTK_LIST_VIEW(sc_widget));
        REQUIRE(sc_sel != nullptr);
        guint sc_count = g_list_model_get_n_items(G_LIST_MODEL(sc_sel));
        REQUIRE(sc_count >= 2);

        // Find key controller on sc_widget
        GtkEventController* sc_key_ctrl = nullptr;
        GListModel* controllers = gtk_widget_observe_controllers(sc_widget);
        guint n_ctrl = g_list_model_get_n_items(controllers);
        for (guint i = 0; i < n_ctrl; i++)
        {
            GObject* item = G_OBJECT(g_list_model_get_item(controllers, i));
            if (GTK_IS_EVENT_CONTROLLER_KEY(item))
            {
                sc_key_ctrl = GTK_EVENT_CONTROLLER(item);
                g_object_unref(item);
                break;
            }
            g_object_unref(item);
        }
        g_object_unref(controllers);
        REQUIRE(sc_key_ctrl != nullptr);

        // Ensure tree below has no selection
        GtkWidget* tree_widget = tree->GetTreeWidget();
        GtkSelectionModel* tree_sel = gtk_list_view_get_model(GTK_LIST_VIEW(tree_widget));
        gtk_selection_model_unselect_all(tree_sel);

        // 1. Multi-select rows 0 and 1 in shortcuts
        gtk_selection_model_select_range(sc_sel, 0, 2, TRUE);
        REQUIRE(gtk_selection_model_is_selected(sc_sel, 0) == TRUE);
        REQUIRE(gtk_selection_model_is_selected(sc_sel, 1) == TRUE);

        // Press Spacebar: checks both items
        gboolean handled = FALSE;
        g_signal_emit_by_name(sc_key_ctrl, "key-pressed", GDK_KEY_space, 0, (GdkModifierType)0, &handled);
        REQUIRE(handled == TRUE);
        REQUIRE(tree->GetSelectedFolders().size() == 2);
        REQUIRE(gtk_selection_model_is_selected(sc_sel, 0) == TRUE);
        REQUIRE(gtk_selection_model_is_selected(sc_sel, 1) == TRUE);

        // Press Spacebar again: unchecks both items, but preserves selection in shortcuts
        g_signal_emit_by_name(sc_key_ctrl, "key-pressed", GDK_KEY_space, 0, (GdkModifierType)0, &handled);
        REQUIRE(handled == TRUE);
        REQUIRE(tree->GetSelectedFolders().size() == 0);
        REQUIRE(gtk_selection_model_is_selected(sc_sel, 0) == TRUE);
        REQUIRE(gtk_selection_model_is_selected(sc_sel, 1) == TRUE);

        // Press Spacebar again: checks both items again
        g_signal_emit_by_name(sc_key_ctrl, "key-pressed", GDK_KEY_space, 0, (GdkModifierType)0, &handled);
        REQUIRE(handled == TRUE);
        REQUIRE(tree->GetSelectedFolders().size() == 2);
        REQUIRE(gtk_selection_model_is_selected(sc_sel, 0) == TRUE);
        REQUIRE(gtk_selection_model_is_selected(sc_sel, 1) == TRUE);

        // Single-select row 0 in shortcuts
        gtk_selection_model_select_item(sc_sel, 0, TRUE);
        REQUIRE(gtk_selection_model_is_selected(sc_sel, 0) == TRUE);
        REQUIRE(gtk_selection_model_is_selected(sc_sel, 1) == FALSE);

        // Press Spacebar to uncheck row 0: row 0 is unchecked, but still selected in sc_sel
        g_signal_emit_by_name(sc_key_ctrl, "key-pressed", GDK_KEY_space, 0, (GdkModifierType)0, &handled);
        REQUIRE(handled == TRUE);
        REQUIRE(tree->GetSelectedFolders().size() == 1); // row 1 is still checked
        REQUIRE(gtk_selection_model_is_selected(sc_sel, 0) == TRUE);

        gtk_window_set_child(GTK_WINDOW(win), nullptr);
        gtk_window_destroy(GTK_WINDOW(win));
    }
}

TEST_CASE("Special Folder Icons in FolderTree and QuiverFile", "[unit][foldertree][quiverfile][icons]")
{
    REQUIRE_DISPLAY();

    SECTION("FolderTree root items have specific icons for special directories")
    {
        FolderTreePtr tree(new FolderTree());
        GtkWidget* widget = tree->GetTreeWidget();
        REQUIRE(widget != nullptr);
        REQUIRE(GTK_IS_LIST_VIEW(widget));

        GtkListView* lv = GTK_LIST_VIEW(widget);
        GtkSelectionModel* sel = gtk_list_view_get_model(lv);
        REQUIRE(sel != nullptr);

        // Verify root items model has items (Home, Filesystem root, etc.)
        guint n = g_list_model_get_n_items(G_LIST_MODEL(sel));
        REQUIRE(n >= 2);
    }

    SECTION("QuiverFile returns special folder icon names and loads pixbufs")
    {
        const char* home = g_get_home_dir();
        REQUIRE(home != nullptr);

        // Test Pictures directory
        std::string pic_path = std::string(home) + "/Pictures";
        g_mkdir_with_parents(pic_path.c_str(), 0755);
        gchar* pic_uri = g_filename_to_uri(pic_path.c_str(), nullptr, nullptr);
        REQUIRE(pic_uri != nullptr);

        QuiverFile f_pic(pic_uri);
        gchar* icon_name = f_pic.GetIconName();
        REQUIRE(icon_name != nullptr);
        REQUIRE(std::string(icon_name) == "folder-pictures");
        g_free(icon_name);

        GdkTexture* tex = f_pic.GetIconTexture(48, 48);
        REQUIRE(tex != nullptr);
        REQUIRE(gdk_texture_get_width(tex) > 0);
        REQUIRE(gdk_texture_get_height(tex) > 0);
        g_object_unref(tex);

        // Test Home directory
        gchar* home_uri = g_filename_to_uri(home, nullptr, nullptr);
        REQUIRE(home_uri != nullptr);

        QuiverFile f_home(home_uri);
        gchar* home_icon = f_home.GetIconName();
        REQUIRE(home_icon != nullptr);
        REQUIRE(std::string(home_icon) == "user-home");
        g_free(home_icon);

        GdkTexture* tex_home = f_home.GetIconTexture(48, 48);
        REQUIRE(tex_home != nullptr);
        g_object_unref(tex_home);

        g_free(pic_uri);
        g_free(home_uri);
    }
}

TEST_CASE("FolderTree reveals and checks a plain-path folder argument",
          "[unit][foldertree][gui]")
{
    REQUIRE_DISPLAY();

    // Build a nested temp directory tree the tree must expand to reach.
    std::string base = folder_tree_test_make_temp_dir();
    std::string level1 = base + "/level1";
    std::string level2 = level1 + "/level2";
    g_mkdir_with_parents(level2.c_str(), 0755);

    FolderTreePtr tree(new FolderTree());
    GtkWidget* win = gtk_window_new();
    gtk_window_set_child(GTK_WINDOW(win), tree->GetWidget());
    gtk_window_set_default_size(GTK_WINDOW(win), 400, 600);
    gtk_window_present(GTK_WINDOW(win));
    while (g_main_context_iteration(NULL, FALSE));

    // Pass the folder the exact way a command line argument arrives: a plain
    // filesystem path with no file:// scheme. It must still resolve to the
    // right row (expanding the parents) and become checked.
    std::list<std::string> sel;
    sel.push_back(level2);
    tree->SetSelectedFolders(sel);

    std::list<std::string> res = tree->GetSelectedFolders();
    REQUIRE(res.size() == 1);
    REQUIRE(res.front() == folder_tree_test_path_to_uri(level2));

    gtk_window_set_child(GTK_WINDOW(win), nullptr);
    gtk_window_destroy(GTK_WINDOW(win));
}

TEST_CASE("FolderTree bookmark checkbox checks the whole bookmark",
          "[unit][foldertree][gui]")
{
    REQUIRE_DISPLAY();

    /* Adding bookmarks persists them to the config file. */
    FolderTreeTestConfigFixture configFixture;

    // Two sibling temp folders under a temporary parent; both are reachable
    // through the Filesystem root and must both get checked.
    std::string base = folder_tree_test_make_temp_dir();
    std::string subA = base + "/subA";
    std::string subB = base + "/subB";
    g_mkdir_with_parents(subA.c_str(), 0755);
    g_mkdir_with_parents(subB.c_str(), 0755);
    std::string uriA = folder_tree_test_path_to_uri(subA);
    std::string uriB = folder_tree_test_path_to_uri(subB);

    BookmarksPtr bm = Bookmarks::GetInstance();
    std::list<std::string> uris = { uriA, uriB };
    Bookmark b1("Check Test Recursive", "desc", "folder-symbolic", uris, true);
    REQUIRE(bm->AddBookmark(b1));
    int added_id1 = bm->GetBookmarks().back().GetID();
    std::string subC = base + "/subC";
    g_mkdir_with_parents(subC.c_str(), 0755);
    std::string uriC = folder_tree_test_path_to_uri(subC);
    std::list<std::string> uris2 = { uriC };
    Bookmark b2("Check Test Flat", "desc", "folder-symbolic", uris2, false);
    REQUIRE(bm->AddBookmark(b2));
    int added_id2 = bm->GetBookmarks().back().GetID();
    /* Persistence writes bookmarks to the shared config file, so remove the
     * test bookmarks on EVERY path (including REQUIRE failures) to avoid
     * leaving stale bookmarks that break later test runs. */
    struct AutoRemoveBookmark {
        BookmarksPtr bm;
        int id;
        ~AutoRemoveBookmark() { if (bm) bm->Remove(id); }
    } cleanup1{ bm, added_id1 }, cleanup2{ bm, added_id2 };

    FolderTreePtr tree(new FolderTree());
    boost::shared_ptr<FolderTreeTestBookmarkHandler> handler(
        new FolderTreeTestBookmarkHandler());
    boost::shared_ptr<FolderTreeTestSelectionHandler> selHandler(
        new FolderTreeTestSelectionHandler());
    tree->AddEventHandler(boost::static_pointer_cast<IEventHandler>(handler));
    tree->AddEventHandler(boost::static_pointer_cast<IEventHandler>(selHandler));

    tree->SetBookmarksExpanded(true);
    GtkWidget* win = gtk_window_new();
    gtk_window_set_child(GTK_WINDOW(win), tree->GetWidget());
    gtk_window_set_default_size(GTK_WINDOW(win), 400, 600);
    gtk_window_present(GTK_WINDOW(win));
    while (g_main_context_iteration(NULL, FALSE));

    // Grab the bookmark rows' checkboxes (bound to each row's DirItem once
    // the list items are realized).
    GtkWidget* bm_widget = tree->GetBookmarksWidget();
    REQUIRE(bm_widget != nullptr);
    GtkSelectionModel* bm_sel = gtk_list_view_get_model(GTK_LIST_VIEW(bm_widget));
    REQUIRE(bm_sel != nullptr);
    REQUIRE(g_list_model_get_n_items(G_LIST_MODEL(bm_sel)) == 2);
    GObject* item0 = (GObject*)g_list_model_get_item(G_LIST_MODEL(bm_sel), 0);
    GObject* item1 = (GObject*)g_list_model_get_item(G_LIST_MODEL(bm_sel), 1);
    REQUIRE(item0 != nullptr);
    REQUIRE(item1 != nullptr);
    GtkWidget* check0 = GTK_WIDGET(g_object_get_data(item0, "bound-check"));
    GtkWidget* check1 = GTK_WIDGET(g_object_get_data(item1, "bound-check"));
    g_object_unref(item0);
    g_object_unref(item1);
    REQUIRE(check0 != nullptr);
    REQUIRE(check1 != nullptr);

    // Toggling a bookmark checkbox behaves like a combined-list folder
    // checkbox: the bookmark stays part of the multi-check selection (no
    // exclusive bookmark-open event) BUT every folder it points at joins the
    // list and its "include subfolders" flag is honored.
    gtk_check_button_set_active(GTK_CHECK_BUTTON(check0), TRUE);
    while (g_main_context_iteration(NULL, FALSE));

    std::list<std::string> res = tree->GetSelectedFolders();
    REQUIRE(res.size() == 2);
    REQUIRE(std::find(res.begin(), res.end(), uriA) != res.end());
    REQUIRE(std::find(res.begin(), res.end(), uriB) != res.end());

    REQUIRE(handler->bookmarkOpens == 0);
    REQUIRE(selHandler->selectionEvents >= 1);
    REQUIRE(tree->GetSelectedFoldersRecursive() == true);
    std::set<std::string> rec = tree->GetSelectedRecursiveFolders();
    REQUIRE(rec.find(uriA) != rec.end());
    REQUIRE(rec.find(uriB) != rec.end());
    REQUIRE(rec.find(uriC) == rec.end());

    // Checking a SECOND bookmark must NOT clear the first one: the combined
    // selection accumulates, all folders from both bookmarks are present, and
    // recursion stays on because the first bookmark asks for it.
    gtk_check_button_set_active(GTK_CHECK_BUTTON(check1), TRUE);
    while (g_main_context_iteration(NULL, FALSE));
    REQUIRE(gtk_check_button_get_active(GTK_CHECK_BUTTON(check0)) == TRUE);

    res = tree->GetSelectedFolders();
    REQUIRE(res.size() == 3);
    REQUIRE(std::find(res.begin(), res.end(), uriA) != res.end());
    REQUIRE(std::find(res.begin(), res.end(), uriB) != res.end());
    REQUIRE(std::find(res.begin(), res.end(), uriC) != res.end());
    REQUIRE(tree->GetSelectedFoldersRecursive() == true);
    rec = tree->GetSelectedRecursiveFolders();
    REQUIRE(rec.find(uriA) != rec.end());
    REQUIRE(rec.find(uriB) != rec.end());
    REQUIRE(rec.find(uriC) == rec.end());
    REQUIRE(handler->bookmarkOpens == 0);
    REQUIRE(selHandler->selectionEvents >= 2);

    // Unchecking the recursive bookmark must drop its folders and clear the
    // recursive flag while leaving the other bookmark checked.
    gtk_check_button_set_active(GTK_CHECK_BUTTON(check0), FALSE);
    while (g_main_context_iteration(NULL, FALSE));
    REQUIRE(gtk_check_button_get_active(GTK_CHECK_BUTTON(check0)) == FALSE);
    REQUIRE(gtk_check_button_get_active(GTK_CHECK_BUTTON(check1)) == TRUE);

    res = tree->GetSelectedFolders();
    REQUIRE(res.size() == 1);
    REQUIRE(std::find(res.begin(), res.end(), uriC) != res.end());
    REQUIRE(tree->GetSelectedFoldersRecursive() == false);
    REQUIRE(tree->GetSelectedRecursiveFolders().empty());
    REQUIRE(handler->bookmarkOpens == 0);

    // Unchecking the last bookmark clears the selection entirely.
    gtk_check_button_set_active(GTK_CHECK_BUTTON(check1), FALSE);
    while (g_main_context_iteration(NULL, FALSE));
    REQUIRE(tree->GetSelectedFolders().size() == 0);
    REQUIRE(tree->GetSelectedFoldersRecursive() == false);

    gtk_window_set_child(GTK_WINDOW(win), nullptr);
    gtk_window_destroy(GTK_WINDOW(win));

    bm->Remove(added_id1);
    bm->Remove(added_id2);
}

TEST_CASE("FolderTree SetSelectedFolders matches bookmark and checks bookmark and folders",
          "[unit][foldertree][gui]")
{
    REQUIRE_DISPLAY();
    FolderTreeTestConfigFixture cfg;

    std::string base = folder_tree_test_make_temp_dir();
    std::string subA = base + "/subA";
    std::string subB = base + "/subB";
    g_mkdir_with_parents(subA.c_str(), 0755);
    g_mkdir_with_parents(subB.c_str(), 0755);
    std::string uriA = folder_tree_test_path_to_uri(subA);
    std::string uriB = folder_tree_test_path_to_uri(subB);

    BookmarksPtr bm = Bookmarks::GetInstance();
    std::list<std::string> uris = { uriA, uriB };
    Bookmark b1("Two Folders Recursive", "desc", "folder-symbolic", uris, true);
    REQUIRE(bm->AddBookmark(b1));
    int added_id1 = bm->GetBookmarks().back().GetID();

    struct AutoRemoveBookmark {
        BookmarksPtr bm;
        int id;
        ~AutoRemoveBookmark() { if (bm) bm->Remove(id); }
    } cleanup1{ bm, added_id1 };

    FolderTreePtr tree(new FolderTree());
    tree->SetBookmarksExpanded(true);
    GtkWidget* win = gtk_window_new();
    gtk_window_set_child(GTK_WINDOW(win), tree->GetWidget());
    gtk_window_set_default_size(GTK_WINDOW(win), 400, 600);
    gtk_window_present(GTK_WINDOW(win));
    while (g_main_context_iteration(NULL, FALSE));

    GtkWidget* bm_widget = tree->GetBookmarksWidget();
    REQUIRE(bm_widget != nullptr);
    GtkSelectionModel* bm_sel = gtk_list_view_get_model(GTK_LIST_VIEW(bm_widget));
    REQUIRE(bm_sel != nullptr);
    REQUIRE(g_list_model_get_n_items(G_LIST_MODEL(bm_sel)) >= 1);
    GObject* item0 = (GObject*)g_list_model_get_item(G_LIST_MODEL(bm_sel), 0);
    REQUIRE(item0 != nullptr);
    GtkWidget* check0 = GTK_WIDGET(g_object_get_data(item0, "bound-check"));
    REQUIRE(check0 != nullptr);
    REQUIRE(gtk_check_button_get_active(GTK_CHECK_BUTTON(check0)) == FALSE);

    // Call SetSelectedFolders with the bookmark's URIs and recursive = true
    tree->SetSelectedFolders(uris, true);
    while (g_main_context_iteration(NULL, FALSE));

    // Bookmark should be checked off!
    gboolean item0_checked = FALSE;
    g_object_get(item0, "checked", &item0_checked, NULL);
    CHECK(item0_checked == TRUE);
    GtkWidget* active_check = GTK_WIDGET(g_object_get_data(item0, "bound-check"));
    if (active_check)
        CHECK(gtk_check_button_get_active(GTK_CHECK_BUTTON(active_check)) == TRUE);
    g_object_unref(item0);

    // GetSelectedFolders should return the bookmark's folders
    std::list<std::string> selected = tree->GetSelectedFolders();
    CHECK(std::find(selected.begin(), selected.end(), uriA) != selected.end());
    CHECK(std::find(selected.begin(), selected.end(), uriB) != selected.end());

    gtk_window_set_child(GTK_WINDOW(win), nullptr);
    gtk_window_destroy(GTK_WINDOW(win));
    g_rmdir(subA.c_str());
    g_rmdir(subB.c_str());
    g_rmdir(base.c_str());
}

TEST_CASE("FolderTree selecting a single folder that is part of a multi-folder recursive bookmark does not activate bookmark",
          "[unit][foldertree][gui]")
{
    REQUIRE_DISPLAY();
    FolderTreeTestConfigFixture cfg;

    std::string base = folder_tree_test_make_temp_dir();
    std::string subA = base + "/subA";
    std::string subB = base + "/subB";
    g_mkdir_with_parents(subA.c_str(), 0755);
    g_mkdir_with_parents(subB.c_str(), 0755);
    std::string uriA = folder_tree_test_path_to_uri(subA);
    std::string uriB = folder_tree_test_path_to_uri(subB);

    BookmarksPtr bm = Bookmarks::GetInstance();
    std::list<std::string> uris = { uriA, uriB };
    Bookmark b1("Two Folders Recursive", "desc", "folder-symbolic", uris, true);
    REQUIRE(bm->AddBookmark(b1));
    int added_id1 = bm->GetBookmarks().back().GetID();

    struct AutoRemoveBookmark {
        BookmarksPtr bm;
        int id;
        ~AutoRemoveBookmark() { if (bm) bm->Remove(id); }
    } cleanup1{ bm, added_id1 };

    FolderTreePtr tree(new FolderTree());
    tree->SetBookmarksExpanded(true);
    GtkWidget* win = gtk_window_new();
    gtk_window_set_child(GTK_WINDOW(win), tree->GetWidget());
    gtk_window_set_default_size(GTK_WINDOW(win), 400, 600);
    gtk_window_present(GTK_WINDOW(win));
    while (g_main_context_iteration(NULL, FALSE));

    // Select ONLY folder A (e.g. user clicked subA in the folder tree)
    std::list<std::string> sel = { uriA };
    tree->SetSelectedFolders(sel, false);
    while (g_main_context_iteration(NULL, FALSE));

    // 1. The bookmark must NOT be checked
    GtkWidget* bm_widget = tree->GetBookmarksWidget();
    REQUIRE(bm_widget != nullptr);
    GtkSelectionModel* bm_sel = gtk_list_view_get_model(GTK_LIST_VIEW(bm_widget));
    REQUIRE(bm_sel != nullptr);
    REQUIRE(g_list_model_get_n_items(G_LIST_MODEL(bm_sel)) >= 1);
    GObject* item0 = (GObject*)g_list_model_get_item(G_LIST_MODEL(bm_sel), 0);
    REQUIRE(item0 != nullptr);
    gboolean item0_checked = TRUE;
    g_object_get(item0, "checked", &item0_checked, NULL);
    CHECK(item0_checked == FALSE);
    g_object_unref(item0);

    // 2. Selected folders must only contain uriA (NOT uriB)
    std::list<std::string> selected = tree->GetSelectedFolders();
    CHECK(selected.size() == 1);
    CHECK(std::find(selected.begin(), selected.end(), uriA) != selected.end());
    CHECK(std::find(selected.begin(), selected.end(), uriB) == selected.end());

    // 3. Recursive flags must be FALSE and empty
    CHECK(tree->GetSelectedFoldersRecursive() == false);
    CHECK(tree->GetSelectedRecursiveFolders().empty());

    gtk_window_set_child(GTK_WINDOW(win), nullptr);
    gtk_window_destroy(GTK_WINDOW(win));
    g_rmdir(subA.c_str());
    g_rmdir(subB.c_str());
    g_rmdir(base.c_str());
}

TEST_CASE("FolderTree drop expand on folder with many subfolders materializes rows",
          "[unit][foldertree][gui]")
{
    REQUIRE_DISPLAY();

    // Create a temp folder with 250 subfolders
    std::string base = folder_tree_test_make_temp_dir();
    for (int i = 0; i < 250; i++)
    {
        char name[32];
        snprintf(name, sizeof(name), "subfolder_%03d", i);
        std::string sub = base + "/" + name;
        g_mkdir(sub.c_str(), 0755);
    }

    FolderTreePtr tree(new FolderTree());
    GtkWidget* win = gtk_window_new();
    GtkWidget* sw = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), tree->GetWidget());
    gtk_window_set_child(GTK_WINDOW(win), sw);
    gtk_window_set_default_size(GTK_WINDOW(win), 400, 600);
    gtk_window_present(GTK_WINDOW(win));
    while (g_main_context_iteration(NULL, FALSE));

    // Reveal subfolder_200, which causes the parent base folder to expand
    // with its 250 subfolders
    std::string target_sub = base + "/subfolder_200";
    std::list<std::string> sel = { target_sub };
    tree->SetSelectedFolders(sel);
    while (g_main_context_iteration(NULL, FALSE));

    // Verify the folder tree list view has items
    GtkWidget* tree_widget = tree->GetTreeWidget();
    REQUIRE(tree_widget != nullptr);
    REQUIRE(GTK_IS_LIST_VIEW(tree_widget));

    GtkListView* lv = GTK_LIST_VIEW(tree_widget);
    GtkSelectionModel* sel_model = gtk_list_view_get_model(lv);
    REQUIRE(sel_model != nullptr);

    guint total = g_list_model_get_n_items(G_LIST_MODEL(sel_model));
    // Base folder + 250 subfolders must be present in the model
    REQUIRE(total >= 250);

    // Verify that children are materialized (bound widgets with non-empty labels exist)
    int child_count = 0;
    int bound_with_labels = 0;
    for (GtkWidget* ch = gtk_widget_get_first_child(tree_widget); ch != nullptr; ch = gtk_widget_get_next_sibling(ch))
    {
        child_count++;
        auto find_label = [](GtkWidget* w, auto& self) -> GtkLabel* {
            if (GTK_IS_LABEL(w)) return GTK_LABEL(w);
            for (GtkWidget* c = gtk_widget_get_first_child(w); c != nullptr; c = gtk_widget_get_next_sibling(c))
            {
                GtkLabel* l = self(c, self);
                if (l) return l;
            }
            return nullptr;
        };
        GtkLabel* label = find_label(ch, find_label);
        if (label && gtk_label_get_text(label) != nullptr &&
            gtk_label_get_text(label)[0] != '\0')
        {
            bound_with_labels++;
        }
    }
    REQUIRE(child_count > 0);
    REQUIRE(bound_with_labels > 0);

    gtk_window_set_child(GTK_WINDOW(win), nullptr);
    gtk_window_destroy(GTK_WINDOW(win));
}

TEST_CASE("FolderTree drop hover expansion materializes child rows with checkboxes",
          "[unit][foldertree][gui]")
{
    REQUIRE_DISPLAY();

    std::string base = folder_tree_test_make_temp_dir();
    for (int i = 0; i < 150; i++)
    {
        char name[32];
        snprintf(name, sizeof(name), "sub_%03d", i);
        std::string sub = base + "/" + name;
        g_mkdir(sub.c_str(), 0755);
    }

    FolderTreePtr tree(new FolderTree());
    GtkWidget* win = gtk_window_new();
    GtkWidget* sw = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), tree->GetWidget());
    gtk_window_set_child(GTK_WINDOW(win), sw);
    gtk_window_set_default_size(GTK_WINDOW(win), 400, 600);
    gtk_window_present(GTK_WINDOW(win));
    while (g_main_context_iteration(NULL, FALSE));

    // Reveal the parent folder without expanding
    std::list<std::string> sel = { base };
    tree->SetSelectedFolders(sel);
    while (g_main_context_iteration(NULL, FALSE));

    GtkWidget* tree_widget = tree->GetTreeWidget();
    REQUIRE(tree_widget != nullptr);
    GtkListView* lv = GTK_LIST_VIEW(tree_widget);
    GtkSelectionModel* sel_model = gtk_list_view_get_model(lv);
    REQUIRE(sel_model != nullptr);

    // Find an expandable row representing our base directory
    guint n_items = g_list_model_get_n_items(G_LIST_MODEL(sel_model));
    GtkTreeListRow* base_row = nullptr;
    for (guint i = 0; i < n_items; i++)
    {
        GtkTreeListRow* row = GTK_TREE_LIST_ROW(g_list_model_get_item(G_LIST_MODEL(sel_model), i));
        if (row)
        {
            if (gtk_tree_list_row_is_expandable(row) && !gtk_tree_list_row_get_expanded(row))
            {
                base_row = row;
                break;
            }
            g_object_unref(row);
        }
    }

    if (base_row)
    {
        // Expand base_row (simulating drop expand timeout - must not scroll or jump)
        GtkAdjustment* vadj = nullptr;
        for (GtkWidget* p = tree_widget; p != nullptr; p = gtk_widget_get_parent(p))
        {
            if (GTK_IS_SCROLLED_WINDOW(p))
            {
                vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(p));
                break;
            }
        }
        // Ensure any pending scroll animation from SetSelectedFolders has settled
        for (int p = 0; p < 20; p++)
        {
            while (g_main_context_iteration(NULL, FALSE));
            g_usleep(5000);
        }
        double adj_before = vadj ? gtk_adjustment_get_value(vadj) : 0.0;
        gtk_tree_list_row_set_expanded(base_row, TRUE);
        gtk_widget_queue_allocate(tree_widget);
        gtk_widget_queue_draw(tree_widget);
        while (g_main_context_iteration(NULL, FALSE));

        // Restore vadj if GTK changed it during expansion (matching folder_tree_drop_expand_timeout)
        if (vadj && std::abs(gtk_adjustment_get_value(vadj) - adj_before) > 0.5)
        {
            gtk_adjustment_set_value(vadj, adj_before);
            while (g_main_context_iteration(NULL, FALSE));
        }

        double adj_after = vadj ? gtk_adjustment_get_value(vadj) : 0.0;
        REQUIRE(adj_before == adj_after);

        // Verify child rows contain checkboxes and labels
        int check_count = 0;
        int label_count = 0;
        for (GtkWidget* ch = gtk_widget_get_first_child(tree_widget); ch != nullptr; ch = gtk_widget_get_next_sibling(ch))
        {
            auto find_check = [](GtkWidget* w, auto& self) -> GtkCheckButton* {
                if (GTK_IS_CHECK_BUTTON(w)) return GTK_CHECK_BUTTON(w);
                for (GtkWidget* c = gtk_widget_get_first_child(w); c != nullptr; c = gtk_widget_get_next_sibling(c))
                {
                    GtkCheckButton* cb = self(c, self);
                    if (cb) return cb;
                }
                return nullptr;
            };
            auto find_label = [](GtkWidget* w, auto& self) -> GtkLabel* {
                if (GTK_IS_LABEL(w)) return GTK_LABEL(w);
                for (GtkWidget* c = gtk_widget_get_first_child(w); c != nullptr; c = gtk_widget_get_next_sibling(c))
                {
                    GtkLabel* l = self(c, self);
                    if (l) return l;
                }
                return nullptr;
            };

            if (find_check(ch, find_check)) check_count++;
            if (find_label(ch, find_label)) label_count++;
        }
        REQUIRE(check_count > 0);
        REQUIRE(label_count > 0);
        g_object_unref(base_row);
    }

    gtk_window_set_child(GTK_WINDOW(win), nullptr);
    gtk_window_destroy(GTK_WINDOW(win));
}

TEST_CASE("FolderTree shortcuts and bookmarks have drop target controllers",
          "[unit][foldertree][gui][dnd]")
{
    /* builds the sidebar's widgets */
    REQUIRE_DISPLAY();

    FolderTreePtr tree(new FolderTree());
    GtkWidget* sidebar = tree->GetWidget();
    REQUIRE(sidebar != nullptr);

    GtkWidget* sc_widget = tree->GetShortcutsWidget();
    REQUIRE(sc_widget != nullptr);
    REQUIRE(GTK_IS_LIST_VIEW(sc_widget));

    GListModel* controllers = gtk_widget_observe_controllers(sc_widget);
    REQUIRE(controllers != nullptr);
    bool has_sc_drop = false;
    guint n_ctrl = g_list_model_get_n_items(controllers);
    for (guint i = 0; i < n_ctrl; i++)
    {
        GObject* ctrl = G_OBJECT(g_list_model_get_item(controllers, i));
        if (GTK_IS_DROP_TARGET(ctrl))
            has_sc_drop = true;
        g_object_unref(ctrl);
    }
    g_object_unref(controllers);
    CHECK(has_sc_drop);
}

TEST_CASE("FolderTree AddChildFolder and FolderTreeNewFolder action", "[unit][foldertree][gui]")
{
    REQUIRE_DISPLAY();

    FolderTreePtr tree(new FolderTree());
    GtkWidget* sidebar = tree->GetWidget();
    REQUIRE(sidebar != nullptr);

    GAction* action = QuiverUtils::GetAction("FolderTreeNewFolder");
    REQUIRE(action != nullptr);

    std::string temp_dir = folder_tree_test_make_temp_dir();
    std::string temp_uri = folder_tree_test_path_to_uri(temp_dir);

    std::list<std::string> sel;
    sel.push_back(temp_uri);
    tree->SetSelectedFolders(sel);

    std::string child_path = temp_dir + "/subfolder";
    std::string child_uri = folder_tree_test_path_to_uri(child_path);

    tree->AddChildFolder(temp_uri.c_str(), child_uri.c_str(), "subfolder");

    // RemoveFolder should safely prune the folder
    tree->RemoveFolder(child_uri.c_str());

    g_rmdir(temp_dir.c_str());
}

TEST_CASE("FolderTree Left/Right arrow navigation and Menu key", "[unit][foldertree][gui]")
{
    REQUIRE_DISPLAY();

    FolderTreePtr tree(new FolderTree());
    GtkWidget* box = tree->GetWidget();
    REQUIRE(box != nullptr);

    GtkWidget* win = gtk_window_new();
    gtk_window_set_child(GTK_WINDOW(win), box);
    gtk_window_set_default_size(GTK_WINDOW(win), 400, 400);
    gtk_window_present(GTK_WINDOW(win));
    while (g_main_context_iteration(NULL, FALSE));

    GtkWidget* widget = tree->GetTreeWidget();
    REQUIRE(widget != nullptr);
    REQUIRE(GTK_IS_LIST_VIEW(widget));

    GtkListView* lv = GTK_LIST_VIEW(widget);
    GtkSelectionModel* sel = gtk_list_view_get_model(lv);
    REQUIRE(sel != nullptr);

    guint total = g_list_model_get_n_items(G_LIST_MODEL(sel));
    REQUIRE(total > 0);

    // Find key controller on widget
    GtkEventController* key_ctrl = nullptr;
    GListModel* controllers = gtk_widget_observe_controllers(widget);
    guint n_ctrl = g_list_model_get_n_items(controllers);
    for (guint i = 0; i < n_ctrl; i++)
    {
        gpointer item = g_list_model_get_item(controllers, i);
        if (GTK_IS_EVENT_CONTROLLER_KEY(item))
        {
            key_ctrl = GTK_EVENT_CONTROLLER(item);
            g_object_unref(item);
            break;
        }
        g_object_unref(item);
    }
    g_object_unref(controllers);
    REQUIRE(key_ctrl != nullptr);

    // Test Menu key on folder tree
    gboolean handled = FALSE;
    g_signal_emit_by_name(key_ctrl, "key-pressed", GDK_KEY_Menu, 0, (GdkModifierType)0, &handled);
    CHECK(handled == TRUE);

    // Test Shift+F10 on folder tree
    handled = FALSE;
    g_signal_emit_by_name(key_ctrl, "key-pressed", GDK_KEY_F10, 0, GDK_SHIFT_MASK, &handled);
    CHECK(handled == TRUE);

    // Test Right arrow expands expandable row, and Left arrow collapses it
    gtk_selection_model_select_item(sel, 0, TRUE);
    GtkTreeListRow* row0 = GTK_TREE_LIST_ROW(g_list_model_get_item(G_LIST_MODEL(sel), 0));
    if (row0)
    {
        if (gtk_tree_list_row_is_expandable(row0))
        {
            // Right arrow to expand
            handled = FALSE;
            g_signal_emit_by_name(key_ctrl, "key-pressed", GDK_KEY_Right, 0, (GdkModifierType)0, &handled);
            CHECK(handled == TRUE);
            CHECK(gtk_tree_list_row_get_expanded(row0) == TRUE);

            // Left arrow to collapse
            handled = FALSE;
            g_signal_emit_by_name(key_ctrl, "key-pressed", GDK_KEY_Left, 0, (GdkModifierType)0, &handled);
            CHECK(handled == TRUE);
            CHECK(gtk_tree_list_row_get_expanded(row0) == FALSE);

            // KP_Right arrow expands
            handled = FALSE;
            g_signal_emit_by_name(key_ctrl, "key-pressed", GDK_KEY_KP_Right, 0, (GdkModifierType)0, &handled);
            CHECK(handled == TRUE);
            CHECK(gtk_tree_list_row_get_expanded(row0) == TRUE);

            // KP_Left arrow collapses
            handled = FALSE;
            g_signal_emit_by_name(key_ctrl, "key-pressed", GDK_KEY_KP_Left, 0, (GdkModifierType)0, &handled);
            CHECK(handled == TRUE);
            CHECK(gtk_tree_list_row_get_expanded(row0) == FALSE);
        }
        g_object_unref(row0);
    }

    gtk_window_destroy(GTK_WINDOW(win));
    while (g_main_context_iteration(NULL, FALSE));
}

TEST_CASE("FolderTree Add Bookmark context menu action", "[unit][foldertree][bookmarks]")
{
    REQUIRE_DISPLAY();

    FolderTreePtr tree(new FolderTree());
    GtkWidget* sidebar = tree->GetWidget();
    REQUIRE(sidebar != nullptr);

    GAction* action = QuiverUtils::GetAction("FolderTreeAddBookmark");
    REQUIRE(action != nullptr);
    CHECK(g_action_get_enabled(action) == TRUE);

    SECTION("Add Bookmark follows the row selection, not the checkbox state")
    {
        /* The shortcuts list is a real GtkListView: its selection model is
         * reachable through the public widget, so the test can drive the
         * highlighted selection exactly as Ctrl/Shift clicks would.  Home is
         * always row 0 and Trash is always the last shortcut row, so their
         * URIs are deterministic regardless of the environment. */
        GtkWidget* shortcuts = tree->GetShortcutsWidget();
        REQUIRE(shortcuts != nullptr);
        GtkSelectionModel* sc_sel = gtk_list_view_get_model(GTK_LIST_VIEW(shortcuts));
        REQUIRE(sc_sel != nullptr);
        guint n = g_list_model_get_n_items(G_LIST_MODEL(sc_sel));
        REQUIRE(n >= 2);

        std::string home_uri = folder_tree_test_path_to_uri(g_get_home_dir());
        std::string trash_uri = "trash:///";

        // Select only in the selection model; the checkboxes are NOT touched.
        gtk_selection_model_select_item(sc_sel, 0, FALSE);
        gtk_selection_model_select_item(sc_sel, n - 1, FALSE);
        while (g_main_context_iteration(NULL, FALSE));

        // Right-clicking one of the selected shortcuts must bookmark both.
        std::list<std::string> uris_from_selection = tree->GetAddBookmarkURIs(home_uri);
        REQUIRE(uris_from_selection.size() == 2);
        REQUIRE(std::find(uris_from_selection.begin(), uris_from_selection.end(), home_uri) != uris_from_selection.end());
        REQUIRE(std::find(uris_from_selection.begin(), uris_from_selection.end(), trash_uri) != uris_from_selection.end());

        // The selection -- not the (untouched) checkbox state -- is what
        // feeds the bookmark list.
        REQUIRE(tree->GetSelectedFolders().size() == 0);

        // Clearing the selection: the clicked folder is used alone.
        gtk_selection_model_unselect_all(sc_sel);
        while (g_main_context_iteration(NULL, FALSE));
        std::list<std::string> uris_no_selection = tree->GetAddBookmarkURIs(home_uri);
        REQUIRE(uris_no_selection.size() == 1);
        REQUIRE(uris_no_selection.front() == home_uri);

        // Right-clicking a folder OUTSIDE the selection bookmarks only it.
        gtk_selection_model_select_item(sc_sel, 0, FALSE);
        gtk_selection_model_select_item(sc_sel, n - 1, FALSE);
        while (g_main_context_iteration(NULL, FALSE));
        std::string outside = folder_tree_test_path_to_uri(
            folder_tree_test_make_temp_dir());
        std::list<std::string> uris_outside = tree->GetAddBookmarkURIs(outside);
        REQUIRE(uris_outside.size() == 1);
        REQUIRE(uris_outside.front() == outside);

        // No clicked folder and no selection: nothing to bookmark.
        gtk_selection_model_unselect_all(sc_sel);
        while (g_main_context_iteration(NULL, FALSE));
        REQUIRE(tree->GetAddBookmarkURIs("").empty());
    }
}

TEST_CASE("FolderTree Edit Bookmark context menu action", "[unit][foldertree][bookmarks]")
{
    REQUIRE_DISPLAY();
    FolderTreeTestConfigFixture cfg;

    BookmarksPtr bmPtr = Bookmarks::GetInstance();
    REQUIRE(bmPtr != nullptr);
    std::string testDir = folder_tree_test_make_temp_dir();
    std::string testUri = folder_tree_test_path_to_uri(testDir);
    std::list<std::string> uris = { testUri };
    Bookmark bm("Sample Bookmark", "Sample Desc", "folder-symbolic", uris, false);
    bmPtr->AddBookmark(bm);

    int bmId = -1;
    auto bmList = bmPtr->GetBookmarks();
    for (const auto& b : bmList)
    {
        if (b.GetName() == "Sample Bookmark")
        {
            bmId = b.GetID();
            break;
        }
    }
    REQUIRE(bmId >= 0);

    FolderTreePtr tree(new FolderTree());
    GtkWidget* sidebar = tree->GetWidget();
    REQUIRE(sidebar != nullptr);

    GtkWidget* win = gtk_window_new();
    gtk_window_set_child(GTK_WINDOW(win), sidebar);
    gtk_window_set_default_size(GTK_WINDOW(win), 400, 400);
    gtk_window_present(GTK_WINDOW(win));
    while (g_main_context_iteration(NULL, FALSE));

    GAction* editAction = QuiverUtils::GetAction("FolderTreeEditBookmark");
    REQUIRE(editAction != nullptr);
    CHECK(g_action_get_enabled(editAction) == TRUE);

    GtkPopoverMenu* popover = nullptr;
    for (GtkWidget* child = gtk_widget_get_first_child(sidebar); child != nullptr; child = gtk_widget_get_next_sibling(child))
    {
        if (GTK_IS_POPOVER_MENU(child))
        {
            popover = GTK_POPOVER_MENU(child);
            break;
        }
    }
    REQUIRE(popover != nullptr);

    auto menu_has_action = [](GMenuModel* model, const char* action_name, auto& self, std::string* out_label = nullptr, int* out_target_int = nullptr, std::string* out_icon = nullptr) -> bool {
        if (!model) return false;
        guint n = g_menu_model_get_n_items(model);
        for (guint i = 0; i < n; i++)
        {
            gchar* act = nullptr;
            if (g_menu_model_get_item_attribute(model, i, G_MENU_ATTRIBUTE_ACTION, "s", &act))
            {
                if (act && strcmp(act, action_name) == 0)
                {
                    if (out_label)
                    {
                        gchar* lbl = nullptr;
                        if (g_menu_model_get_item_attribute(model, i, G_MENU_ATTRIBUTE_LABEL, "s", &lbl))
                        {
                            *out_label = lbl ? lbl : "";
                            g_free(lbl);
                        }
                    }
                    if (out_target_int)
                    {
                        gint val = -1;
                        if (g_menu_model_get_item_attribute(model, i, G_MENU_ATTRIBUTE_TARGET, "i", &val))
                        {
                            *out_target_int = val;
                        }
                    }
                    if (out_icon)
                    {
                        gchar* ic = nullptr;
                        if (g_menu_model_get_item_attribute(model, i, "icon", "s", &ic))
                        {
                            *out_icon = ic ? ic : "";
                            g_free(ic);
                        }
                    }
                    g_free(act);
                    return true;
                }
                g_free(act);
            }
            GMenuModel* section = g_menu_model_get_item_link(model, i, G_MENU_LINK_SECTION);
            if (section)
            {
                bool found = self(section, action_name, self, out_label, out_target_int, out_icon);
                g_object_unref(section);
                if (found) return true;
            }
            GMenuModel* submenu = g_menu_model_get_item_link(model, i, G_MENU_LINK_SUBMENU);
            if (submenu)
            {
                bool found = self(submenu, action_name, self, out_label, out_target_int, out_icon);
                g_object_unref(submenu);
                if (found) return true;
            }
        }
        return false;
    };

    SECTION("Context menu on a bookmark row contains Edit Bookmark with bookmark ID and suppresses Add Bookmark")
    {
        tree->ShowContextMenuForTest(testUri.c_str(), bmId);
        GMenuModel* model = gtk_popover_menu_get_menu_model(popover);
        REQUIRE(model != nullptr);

        std::string label;
        int target_id = -1;
        std::string icon;
        bool has_edit = menu_has_action(model, "quiver.FolderTreeEditBookmark", menu_has_action, &label, &target_id, &icon);
        CHECK(has_edit == true);
        CHECK(label == "Edit Bookmark…");
        CHECK(target_id == bmId);
        CHECK(icon == "document-edit-symbolic");

        bool has_add = menu_has_action(model, "quiver.FolderTreeAddBookmark", menu_has_action);
        CHECK(has_add == false);
    }

    SECTION("Context menu on a normal folder row contains Add Bookmark and does NOT contain Edit Bookmark")
    {
        tree->ShowContextMenuForTest(testUri.c_str(), -1);
        GMenuModel* model = gtk_popover_menu_get_menu_model(popover);
        REQUIRE(model != nullptr);

        bool has_add = menu_has_action(model, "quiver.FolderTreeAddBookmark", menu_has_action);
        CHECK(has_add == true);

        bool has_edit = menu_has_action(model, "quiver.FolderTreeEditBookmark", menu_has_action);
        CHECK(has_edit == false);
    }

    gtk_window_destroy(GTK_WINDOW(win));
    while (g_main_context_iteration(NULL, FALSE));
}

TEST_CASE("FolderTree collapsible sections", "[unit][foldertree][gui]")
{
    REQUIRE_DISPLAY();
    FolderTreeTestConfigFixture cfg;

    FolderTreePtr tree(new FolderTree());
    GtkWidget* box = tree->GetWidget();
    REQUIRE(box != nullptr);
    REQUIRE(GTK_IS_BOX(box));

    // All three sections default to expanded and can be toggled independently
    CHECK(tree->GetShortcutsExpanded() == true);
    CHECK(tree->GetBookmarksExpanded() == true);
    CHECK(tree->GetFoldersExpanded() == true);

    tree->SetShortcutsExpanded(false);
    CHECK(tree->GetShortcutsExpanded() == false);
    CHECK(tree->GetBookmarksExpanded() == true);
    CHECK(tree->GetFoldersExpanded() == true);

    tree->SetBookmarksExpanded(false);
    CHECK(tree->GetShortcutsExpanded() == false);
    CHECK(tree->GetBookmarksExpanded() == false);
    CHECK(tree->GetFoldersExpanded() == true);

    tree->SetFoldersExpanded(false);
    CHECK(tree->GetShortcutsExpanded() == false);
    CHECK(tree->GetBookmarksExpanded() == false);
    CHECK(tree->GetFoldersExpanded() == false);

    // Re-expand all three sections simultaneously
    tree->SetShortcutsExpanded(true);
    tree->SetBookmarksExpanded(true);
    tree->SetFoldersExpanded(true);
    CHECK(tree->GetShortcutsExpanded() == true);
    CHECK(tree->GetBookmarksExpanded() == true);
    CHECK(tree->GetFoldersExpanded() == true);

    // SetSelectedFolders automatically re-expands Folders if collapsed without hiding others
    tree->SetFoldersExpanded(false);
    CHECK(tree->GetFoldersExpanded() == false);
    std::string home_uri = folder_tree_test_path_to_uri(g_get_home_dir());
    std::list<std::string> sel = { home_uri };
    tree->SetSelectedFolders(sel);
    CHECK(tree->GetFoldersExpanded() == true);
    CHECK(tree->GetShortcutsExpanded() == true);
    CHECK(tree->GetBookmarksExpanded() == true);
}

TEST_CASE("FolderTree does not show hidden folders", "[unit][foldertree][gui]")
{
    REQUIRE_DISPLAY();

    std::string temp_dir = folder_tree_test_make_temp_dir();
    std::string visible_sub = temp_dir + "/visible_folder";
    std::string hidden_sub = temp_dir + "/.hidden_folder";
    g_mkdir(visible_sub.c_str(), 0755);
    g_mkdir(hidden_sub.c_str(), 0755);

    FolderTreePtr tree(new FolderTree());
    GtkWidget* win = gtk_window_new();
    gtk_window_set_child(GTK_WINDOW(win), tree->GetWidget());
    gtk_window_present(GTK_WINDOW(win));
    while (g_main_context_iteration(NULL, FALSE));

    std::string temp_uri = folder_tree_test_path_to_uri(temp_dir);
    std::list<std::string> sel = { temp_uri };
    tree->SetSelectedFolders(sel);
    while (g_main_context_iteration(NULL, FALSE));

    // Now reveal visible_sub to force expanding temp_dir's subdirectories
    std::string vis_uri = folder_tree_test_path_to_uri(visible_sub);
    sel = { vis_uri };
    tree->SetSelectedFolders(sel);
    while (g_main_context_iteration(NULL, FALSE));

    // Verify visible_sub is in the selected folders
    std::list<std::string> selected = tree->GetSelectedFolders();
    REQUIRE(std::find(selected.begin(), selected.end(), vis_uri) != selected.end());

    // Verify hidden_sub is NOT present anywhere in the list model
    GtkWidget* tree_widget = tree->GetTreeWidget();
    REQUIRE(tree_widget != nullptr);
    GtkListView* lv = GTK_LIST_VIEW(tree_widget);
    GtkSelectionModel* model = gtk_list_view_get_model(lv);
    REQUIRE(model != nullptr);
    REQUIRE(g_list_model_get_n_items(G_LIST_MODEL(model)) >= 1);
    auto has_label_matching = [](GtkWidget* root, const char* target) -> bool {
        auto check_w = [target](GtkWidget* w, auto& self) -> bool {
            if (GTK_IS_LABEL(w))
            {
                const char* txt = gtk_label_get_text(GTK_LABEL(w));
                if (txt && strcmp(txt, target) == 0)
                    return true;
            }
            for (GtkWidget* c = gtk_widget_get_first_child(w); c != nullptr; c = gtk_widget_get_next_sibling(c))
            {
                if (self(c, self))
                    return true;
            }
            return false;
        };
        for (GtkWidget* ch = gtk_widget_get_first_child(root); ch != nullptr; ch = gtk_widget_get_next_sibling(ch))
        {
            if (check_w(ch, check_w))
                return true;
        }
        return false;
    };

    for (int iter = 0; iter < 10; iter++)
    {
        while (g_main_context_iteration(NULL, FALSE));
        g_usleep(5000);
    }

    CHECK(has_label_matching(tree_widget, "visible_folder") == true);
    CHECK(has_label_matching(tree_widget, ".hidden_folder") == false);

    gtk_window_set_child(GTK_WINDOW(win), nullptr);
    gtk_window_destroy(GTK_WINDOW(win));
    g_rmdir(visible_sub.c_str());
    g_rmdir(hidden_sub.c_str());
    g_rmdir(temp_dir.c_str());
}

TEST_CASE("FolderTree handles large folder expansion with virtualized scrolling", "[unit][foldertree][gui]")
{
    REQUIRE_DISPLAY();

    std::string base = folder_tree_test_make_temp_dir();
    for (int i = 0; i < 300; i++)
    {
        char name[32];
        snprintf(name, sizeof(name), "sub_%03d", i);
        std::string sub = base + "/" + name;
        g_mkdir(sub.c_str(), 0755);
    }

    FolderTreePtr tree(new FolderTree());
    GtkWidget* win = gtk_window_new();
    gtk_window_set_child(GTK_WINDOW(win), tree->GetWidget());
    gtk_window_set_default_size(GTK_WINDOW(win), 300, 400);
    gtk_window_present(GTK_WINDOW(win));
    while (g_main_context_iteration(NULL, FALSE));

    // Reveal base/sub_000 to expand base and populate all 300 subdirectories
    std::string sub0_uri = folder_tree_test_path_to_uri(base + "/sub_000");
    std::list<std::string> sel = { sub0_uri };
    tree->SetSelectedFolders(sel);
    for (int iter = 0; iter < 10; iter++)
    {
        while (g_main_context_iteration(NULL, FALSE));
        g_usleep(5000);
    }

    GtkWidget* tree_widget = tree->GetTreeWidget();
    REQUIRE(tree_widget != nullptr);
    GtkListView* lv = GTK_LIST_VIEW(tree_widget);
    GtkSelectionModel* model = gtk_list_view_get_model(lv);
    guint n_total = g_list_model_get_n_items(G_LIST_MODEL(model));
    REQUIRE(n_total >= 300);

    // Verify outer unified scrolled window
    GtkWidget* main_sw = gtk_widget_get_first_child(tree->GetWidget());
    REQUIRE(main_sw != nullptr);
    REQUIRE(GTK_IS_SCROLLED_WINDOW(main_sw));

    GtkAdjustment* vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(main_sw));
    REQUIRE(vadj != nullptr);
    CHECK(gtk_adjustment_get_upper(vadj) > 5000.0);

    auto has_realized_label = [](GtkWidget* root, const char* target) -> bool {
        std::vector<std::string> found;
        auto check_w = [target, &found](GtkWidget* w, auto& self) -> bool {
            if (GTK_IS_LABEL(w))
            {
                const char* txt = gtk_label_get_text(GTK_LABEL(w));
                if (txt) found.push_back(txt);
                if (txt && strcmp(txt, target) == 0)
                    return true;
            }
            for (GtkWidget* c = gtk_widget_get_first_child(w); c != nullptr; c = gtk_widget_get_next_sibling(c))
            {
                if (self(c, self))
                    return true;
            }
            return false;
        };
        for (GtkWidget* ch = gtk_widget_get_first_child(root); ch != nullptr; ch = gtk_widget_get_next_sibling(ch))
        {
            if (check_w(ch, check_w))
                return true;
        }
        return false;
    };

    auto count_realized_rows = [](GtkWidget* root) -> guint {
        guint count = 0;
        for (GtkWidget* ch = gtk_widget_get_first_child(root); ch != nullptr; ch = gtk_widget_get_next_sibling(ch))
        {
            if (gtk_widget_get_visible(ch) && gtk_widget_get_first_child(ch) != nullptr)
                count++;
        }
        return count;
    };

    double sub0_scroll = gtk_adjustment_get_value(vadj);
    // Native GTK4 virtualization: realizes only visible + prefetch rows (O(1) widgets, <= 210)
    CHECK(count_realized_rows(tree_widget) < n_total);
    CHECK(count_realized_rows(tree_widget) <= 210);
    CHECK(has_realized_label(tree_widget, "sub_000") == true);
    CHECK(has_realized_label(tree_widget, "sub_299") == false);

    // 1. Scroll to sub_299 via SetSelectedFolders and verify sub_299 is realized while sub_000 is virtualized away
    std::string sub299_uri = folder_tree_test_path_to_uri(base + "/sub_299");
    sel = { sub299_uri };
    tree->SetSelectedFolders(sel);
    for (int iter = 0; iter < 10; iter++)
    {
        while (g_main_context_iteration(NULL, FALSE));
        g_usleep(5000);
    }
    double sub299_scroll = gtk_adjustment_get_value(vadj);
    CHECK(sub299_scroll > 0.0);
    CHECK(gtk_widget_get_margin_top(tree_widget) == 0);
    CHECK(gtk_widget_get_margin_bottom(tree_widget) == 0);
    CHECK(count_realized_rows(tree_widget) <= 210);
    CHECK(has_realized_label(tree_widget, "sub_299") == true);
    CHECK(has_realized_label(tree_widget, "sub_000") == false);

    // 2. Scroll the outer ScrolledWindow back to sub0_scroll and verify sub_000 is realized while sub_299 is virtualized away
    gtk_adjustment_set_value(vadj, sub0_scroll);
    for (int iter = 0; iter < 20; iter++)
    {
        while (g_main_context_iteration(NULL, FALSE));
        if (has_realized_label(tree_widget, "sub_000"))
            break;
        g_usleep(10000);
    }
    CHECK(std::abs(gtk_adjustment_get_value(vadj) - sub0_scroll) <= 50.0);
    CHECK(count_realized_rows(tree_widget) <= 210);
    CHECK(has_realized_label(tree_widget, "sub_000") == true);
    CHECK(has_realized_label(tree_widget, "sub_299") == false);

    // 3. Scroll smoothly in steps from sub0_scroll to sub299_scroll and verify vadj never jumps and rows stay virtualized
    REQUIRE(sub299_scroll > sub0_scroll + 1000.0);
    for (int step = 1; step <= 10; step++)
    {
        double target_val = sub0_scroll + ((sub299_scroll - sub0_scroll) * step) / 10.0;
        gtk_adjustment_set_value(vadj, target_val);
        while (g_main_context_iteration(NULL, FALSE));
        CHECK(count_realized_rows(tree_widget) <= 210);
    }

    // 4. Scroll smoothly in steps from sub299_scroll back to sub0_scroll and verify vadj never jumps and rows stay virtualized
    for (int step = 9; step >= 0; step--)
    {
        double target_val = sub0_scroll + ((sub299_scroll - sub0_scroll) * step) / 10.0;
        gtk_adjustment_set_value(vadj, target_val);
        while (g_main_context_iteration(NULL, FALSE));
        CHECK(count_realized_rows(tree_widget) <= 210);
    }
    gtk_adjustment_set_value(vadj, sub0_scroll);
    for (int iter = 0; iter < 20; iter++)
    {
        while (g_main_context_iteration(NULL, FALSE));
        if (has_realized_label(tree_widget, "sub_000"))
            break;
        g_usleep(10000);
    }
    CHECK(std::abs(gtk_adjustment_get_value(vadj) - sub0_scroll) <= 50.0);
    CHECK(has_realized_label(tree_widget, "sub_000") == true);
    CHECK(has_realized_label(tree_widget, "sub_299") == false);

    gtk_window_set_child(GTK_WINDOW(win), nullptr);
    gtk_window_destroy(GTK_WINDOW(win));
    for (int i = 0; i < 300; i++)
    {
        char name[32];
        snprintf(name, sizeof(name), "sub_%03d", i);
        std::string sub = base + "/" + name;
        g_rmdir(sub.c_str());
    }
    g_rmdir(base.c_str());
}

TEST_CASE("FolderTree directory rows display tree expander arrows and bind list rows", "[unit][foldertree][gui]")
{
    REQUIRE_DISPLAY();
    FolderTreePtr tree(new FolderTree());
    GtkWidget* win = gtk_window_new();
    gtk_window_set_child(GTK_WINDOW(win), tree->GetWidget());
    gtk_window_set_default_size(GTK_WINDOW(win), 400, 600);
    gtk_window_present(GTK_WINDOW(win));
    for (int i = 0; i < 10; i++)
    {
        while (g_main_context_iteration(NULL, FALSE));
        g_usleep(5000);
    }

    GtkWidget* tree_widget = tree->GetTreeWidget();
    REQUIRE(tree_widget != nullptr);

    bool found_folder_row_with_expander = false;

    auto check_row = [&found_folder_row_with_expander](GtkWidget* w, auto& self) -> void {
        if (GTK_IS_TREE_EXPANDER(w))
        {
            GtkTreeExpander* exp = GTK_TREE_EXPANDER(w);
            GtkTreeListRow* r = gtk_tree_expander_get_list_row(exp);
            if (r != nullptr)
            {
                CHECK(gtk_tree_expander_get_hide_expander(exp) == FALSE);
                CHECK(gtk_tree_expander_get_indent_for_icon(exp) == TRUE);
                if (gtk_tree_list_row_is_expandable(r))
                {
                    // Verify that GtkTreeExpander contains its disclosure arrow icon
                    bool has_builtin_icon = false;
                    for (GtkWidget* c = gtk_widget_get_first_child(w); c != nullptr; c = gtk_widget_get_next_sibling(c))
                    {
                        if (g_strcmp0(G_OBJECT_TYPE_NAME(c), "GtkBuiltinIcon") == 0)
                            has_builtin_icon = true;
                    }
                    CHECK(has_builtin_icon == true);
                    found_folder_row_with_expander = true;
                }
            }
        }
        for (GtkWidget* c = gtk_widget_get_first_child(w); c != nullptr; c = gtk_widget_get_next_sibling(c))
        {
            self(c, self);
        }
    };

    for (GtkWidget* ch = gtk_widget_get_first_child(tree_widget); ch != nullptr; ch = gtk_widget_get_next_sibling(ch))
    {
        check_row(ch, check_row);
    }

    CHECK(found_folder_row_with_expander == true);

    gtk_window_set_child(GTK_WINDOW(win), nullptr);
    gtk_window_destroy(GTK_WINDOW(win));
}
