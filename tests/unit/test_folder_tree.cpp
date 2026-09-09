#include <catch2/catch_test_macros.hpp>
#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include "FolderTree.h"
#include "QuiverFile.h"
#include "Bookmarks.h"
#include "Preferences.h"
#include "test_helpers.h"

TEST_CASE("FolderTree Selection and Keyboard Navigation", "[unit][foldertree][gui]")
{
    REQUIRE_DISPLAY();

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

        // Select row 0 in selection model (unselect rest)
        gtk_selection_model_select_item(sel, 0, TRUE);

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

        // Simulate plain Enter on row 0
        gboolean handled = FALSE;
        g_signal_emit_by_name(key_ctrl, "key-pressed", GDK_KEY_Return, 0, (GdkModifierType)0, &handled);
        REQUIRE(handled == TRUE);

        std::list<std::string> selected1 = tree->GetSelectedFolders();
        REQUIRE(selected1.size() == 1);

        // Move selection to row 1 (unselect rest) and press plain Enter: row 1 checked, row 0 unchecked
        if (total > 1)
        {
            gtk_selection_model_select_item(sel, 1, TRUE);
            g_signal_emit_by_name(key_ctrl, "key-pressed", GDK_KEY_Return, 0, (GdkModifierType)0, &handled);
            REQUIRE(handled == TRUE);

            std::list<std::string> selected2 = tree->GetSelectedFolders();
            REQUIRE(selected2.size() == 1);
            REQUIRE(selected2.front() != selected1.front());

            // Select row 0 and press Ctrl+Enter: adds row 0 (now both row 0 and row 1 checked)
            gtk_selection_model_select_item(sel, 0, TRUE);
            g_signal_emit_by_name(key_ctrl, "key-pressed", GDK_KEY_Return, 0, GDK_CONTROL_MASK, &handled);
            REQUIRE(handled == TRUE);

            std::list<std::string> selected3 = tree->GetSelectedFolders();
            REQUIRE(selected3.size() == 2);

            // Press Ctrl+Enter on row 0 again: subtracts row 0 (now only row 1 checked)
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

        // Find child widgets of the list view
        GtkWidget* first_child = gtk_widget_get_first_child(widget);
        REQUIRE(first_child != nullptr);

        // Look for the row_box controller in the item widget hierarchy
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

        GtkGestureClick* row_gesture = find_gesture(first_child, find_gesture);
        if (row_gesture)
        {
            // Simulate mouse click
            g_signal_emit_by_name(row_gesture, "pressed", 1, 10.0, 10.0);
            g_signal_emit_by_name(row_gesture, "released", 1, 10.0, 10.0);
            std::list<std::string> selList = tree->GetSelectedFolders();
            REQUIRE(selList.size() == 1);
        }

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

            // Plain Enter on row 0: row 0 checked and selected
            gboolean handled = FALSE;
            gtk_selection_model_select_item(sel, 0, TRUE);
            g_signal_emit_by_name(key_ctrl, "key-pressed", GDK_KEY_Return, 0, (GdkModifierType)0, &handled);
            REQUIRE(handled == TRUE);
            REQUIRE(tree->GetSelectedFolders().size() == 1);

            // Now move keyboard FOCUS to row 1 (outline), keeping row 0 selected in sel (blue background)
            GtkWidget* row0 = gtk_widget_get_first_child(widget);
            GtkWidget* row1 = gtk_widget_get_next_sibling(row0);
            REQUIRE(row1 != nullptr);
            gtk_widget_grab_focus(row1);
            while (g_main_context_iteration(NULL, FALSE));

            // Press plain Enter: should check row 1, uncheck row 0, and update selection model
            g_signal_emit_by_name(key_ctrl, "key-pressed", GDK_KEY_Return, 0, (GdkModifierType)0, &handled);
            REQUIRE(handled == TRUE);
            std::list<std::string> selList = tree->GetSelectedFolders();
            REQUIRE(selList.size() == 1);
            REQUIRE(gtk_selection_model_is_selected(sel, 1) == TRUE);
            REQUIRE(gtk_selection_model_is_selected(sel, 0) == FALSE);

            // Now move focus back to row 0, while row 1 is selected
            gtk_widget_grab_focus(row0);
            while (g_main_context_iteration(NULL, FALSE));

            // Press Ctrl+Enter on focused row 0: should toggle row 0 (now both row 0 and row 1 checked)
            g_signal_emit_by_name(key_ctrl, "key-pressed", GDK_KEY_Return, 0, GDK_CONTROL_MASK, &handled);
            REQUIRE(handled == TRUE);
            REQUIRE(tree->GetSelectedFolders().size() == 2);
            REQUIRE(gtk_selection_model_is_selected(sel, 0) == TRUE);
            REQUIRE(gtk_selection_model_is_selected(sel, 1) == TRUE);

            // Press Ctrl+Enter on focused row 0 again: should uncheck row 0 (now only row 1 checked)
            g_signal_emit_by_name(key_ctrl, "key-pressed", GDK_KEY_Return, 0, GDK_CONTROL_MASK, &handled);
            REQUIRE(handled == TRUE);
            REQUIRE(tree->GetSelectedFolders().size() == 1);
            REQUIRE(gtk_selection_model_is_selected(sel, 0) == FALSE);
            REQUIRE(gtk_selection_model_is_selected(sel, 1) == TRUE);
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

            // Multi-select rows 0 and 1 in selection model
            gtk_selection_model_select_range(sel, 0, 2, TRUE);
            REQUIRE(gtk_selection_model_is_selected(sel, 0) == TRUE);
            REQUIRE(gtk_selection_model_is_selected(sel, 1) == TRUE);

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

            // 4. Test partial check: uncheck row 0 via Ctrl+Enter on row 0
            GtkWidget* row0 = gtk_widget_get_first_child(widget);
            gtk_widget_grab_focus(row0);
            while (g_main_context_iteration(NULL, FALSE));

            g_signal_emit_by_name(key_ctrl, "key-pressed", GDK_KEY_Return, 0, GDK_CONTROL_MASK, &handled);
            REQUIRE(handled == TRUE);
            REQUIRE(tree->GetSelectedFolders().size() == 1); // only row 1 checked

            // Re-select rows 0 and 1 in sel
            gtk_selection_model_select_range(sel, 0, 2, TRUE);
            // Now row 0 is unchecked and row 1 is checked; Spacebar should check both
            g_signal_emit_by_name(key_ctrl, "key-pressed", GDK_KEY_space, 0, (GdkModifierType)0, &handled);
            REQUIRE(handled == TRUE);
            REQUIRE(tree->GetSelectedFolders().size() == 2);

            // 5. Test Enter on multi-select:
            // First uncheck all selected with Spacebar
            g_signal_emit_by_name(key_ctrl, "key-pressed", GDK_KEY_space, 0, (GdkModifierType)0, &handled);
            REQUIRE(handled == TRUE);
            REQUIRE(tree->GetSelectedFolders().size() == 0);

            // Multi-selection is still selected in sel (rows 0 and 1).
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

        // Children of box: shortcuts list view, separator, bookmarks
        // section, tree list view
        GtkWidget* first_child = gtk_widget_get_first_child(box);
        REQUIRE(first_child == sc_widget);
        GtkWidget* sep = gtk_widget_get_next_sibling(first_child);
        REQUIRE(sep != nullptr);
        REQUIRE(GTK_IS_SEPARATOR(sep));
        GtkWidget* bm_section = gtk_widget_get_next_sibling(sep);
        REQUIRE(bm_section != nullptr);
        GtkWidget* bm_widget = tree->GetBookmarksWidget();
        REQUIRE(bm_widget != nullptr);
        REQUIRE(GTK_IS_LIST_VIEW(bm_widget));
        GtkWidget* walk = gtk_widget_get_first_child(bm_section);
        REQUIRE(walk != nullptr);
        REQUIRE(GTK_IS_LABEL(walk));
        GtkWidget* bm_list = gtk_widget_get_next_sibling(walk);
        REQUIRE(bm_list == bm_widget);
        GtkWidget* tree_sibling = gtk_widget_get_next_sibling(bm_section);
        while (tree_sibling != tree_widget && tree_sibling != nullptr)
            tree_sibling = gtk_widget_get_next_sibling(tree_sibling);
        REQUIRE(tree_sibling == tree_widget);

        // Verify CSS classes and styling attributes
        REQUIRE(gtk_widget_has_css_class(box, "sidebar"));
        REQUIRE(gtk_widget_has_css_class(box, "quiver-sidebar"));
        REQUIRE(gtk_widget_has_css_class(sc_widget, "navigation-sidebar"));
        REQUIRE(gtk_widget_has_css_class(tree_widget, "compact-tree"));
        REQUIRE(gtk_widget_has_css_class(sep, "sidebar-separator"));
        REQUIRE(gtk_widget_get_margin_top(sep) >= 10);
        REQUIRE(gtk_widget_get_margin_bottom(sep) >= 10);

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
