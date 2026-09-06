#include <catch2/catch_test_macros.hpp>
#include <gtk/gtk.h>
#include "FolderTree.h"
#include "test_helpers.h"

TEST_CASE("FolderTree Selection and Keyboard Navigation", "[unit][foldertree][gui]")
{
    REQUIRE_DISPLAY();

    SECTION("Enter key on focused row checks it and unchecks others")
    {
        FolderTreePtr tree(new FolderTree());
        GtkWidget* widget = tree->GetWidget();
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
        GtkWidget* widget = tree->GetWidget();
        REQUIRE(widget != nullptr);
        REQUIRE(GTK_IS_LIST_VIEW(widget));

        // Window presentation so list items are created and bound
        GtkWidget* win = gtk_window_new();
        gtk_window_set_child(GTK_WINDOW(win), widget);
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
        GtkWidget* widget = tree->GetWidget();
        REQUIRE(widget != nullptr);

        GtkWidget* win = gtk_window_new();
        gtk_window_set_child(GTK_WINDOW(win), widget);
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
        GtkWidget* widget = tree->GetWidget();
        REQUIRE(widget != nullptr);

        GtkWidget* win = gtk_window_new();
        gtk_window_set_child(GTK_WINDOW(win), widget);
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
}
