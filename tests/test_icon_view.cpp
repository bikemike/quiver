#include <gtk/gtk.h>
#include "quiver-icon-view.h"
#include "ImageCache.h"
#include <iostream>
#include <cassert>

static gulong test_n_items_cb(QuiverIconView *iconview, gpointer user_data)
{
    (void)iconview;
    (void)user_data;
    return 6;
}

static GdkTexture* create_test_texture(int width, int height, guint32 color_rgba)
{
    GdkPixbuf *pb = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, width, height);
    assert(pb != NULL);
    gdk_pixbuf_fill(pb, color_rgba);
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    GdkTexture *tex = gdk_texture_new_for_pixbuf(pb);
G_GNUC_END_IGNORE_DEPRECATIONS
    g_object_unref(pb);
    return tex;
}

struct IconTestData {
    ImageCache cache{10};
    bool texture_called = false;
    bool pixbuf_called = false;
};

static GdkTexture* test_thumbnail_texture_cb(QuiverIconView *iconview, gulong cell, gint* actual_width, gint* actual_height, gpointer user_data)
{
    (void)iconview;
    IconTestData *data = static_cast<IconTestData*>(user_data);
    data->texture_called = true;

    std::string key = "item_" + std::to_string(cell);
    GdkTexture *tex = data->cache.GetTexture(key);
    if (tex)
    {
        *actual_width = gdk_texture_get_width(tex);
        *actual_height = gdk_texture_get_height(tex);
    }
    return tex;
}

static GdkPixbuf* test_thumbnail_pixbuf_cb(QuiverIconView *iconview, gulong cell, gint* actual_width, gint* actual_height, gpointer user_data)
{
    (void)iconview;
    IconTestData *data = static_cast<IconTestData*>(user_data);
    data->pixbuf_called = true;

    std::string key = "item_" + std::to_string(cell);
    GdkPixbuf *pb = data->cache.GetPixbuf(key);
    if (pb)
    {
        *actual_width = gdk_pixbuf_get_width(pb);
        *actual_height = gdk_pixbuf_get_height(pb);
    }
    return pb;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    gtk_init();

    std::cout << "Testing QuiverIconView & ImageCache GdkTexture integration...\n";

    IconTestData testData;

    // Test 1: ImageCache texture insertion and retrieval
    std::cout << "  [1] Verifying ImageCache Texture operations...";
    GdkTexture *t0 = create_test_texture(128, 96, 0xff0000ff);
    testData.cache.AddTexture("item_0", t0);
    g_object_unref(t0);

    GdkTexture *got_t0 = testData.cache.GetTexture("item_0");
    assert(got_t0 != NULL);
    assert(gdk_texture_get_width(got_t0) == 128);
    assert(gdk_texture_get_height(got_t0) == 96);
    g_object_unref(got_t0);

    // Also verify backward compatibility: GetPixbuf on an item added as Texture
    GdkPixbuf *got_pb0 = testData.cache.GetPixbuf("item_0");
    assert(got_pb0 != NULL);
    assert(gdk_pixbuf_get_width(got_pb0) == 128);
    g_object_unref(got_pb0);
    std::cout << " OK\n";

    // Test 2: Add item as Pixbuf, verify GetTexture works lazily
    std::cout << "  [2] Verifying ImageCache Pixbuf->Texture lazy conversion...";
    GdkPixbuf *pb1 = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, 64, 64);
    testData.cache.AddPixbuf("item_1", pb1);
    g_object_unref(pb1);

    GdkTexture *got_t1 = testData.cache.GetTexture("item_1");
    assert(got_t1 != NULL);
    assert(gdk_texture_get_width(got_t1) == 64);
    assert(gdk_texture_get_height(got_t1) == 64);
    g_object_unref(got_t1);
    std::cout << " OK\n";

    // Test 3: Create QuiverIconView in a GtkScrolledWindow and attach texture callback
    std::cout << "  [3] Testing QuiverIconView with GdkTexture callback...";
    GtkWidget *scrolled = gtk_scrolled_window_new();
    GtkWidget *iconview = quiver_icon_view_new();
    assert(QUIVER_IS_ICON_VIEW(iconview));
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), iconview);

    quiver_icon_view_set_n_items_func(QUIVER_ICON_VIEW(iconview), test_n_items_cb, &testData, NULL);
    quiver_icon_view_set_thumbnail_texture_func(QUIVER_ICON_VIEW(iconview), test_thumbnail_texture_cb, &testData, NULL);
    quiver_icon_view_set_icon_size(QUIVER_ICON_VIEW(iconview), 96, 96);

    // Allocate size for scrolled window and icon view
    gtk_widget_allocate(scrolled, 400, 300, -1, NULL);

    // Trigger snapshot via GtkSnapshot
    GtkSnapshot *snapshot = gtk_snapshot_new();
    GTK_WIDGET_GET_CLASS(iconview)->snapshot(iconview, snapshot);
    GskRenderNode *node = gtk_snapshot_free_to_node(snapshot);
    assert(node != NULL);
    gsk_render_node_unref(node);

    assert(testData.texture_called == true);
    std::cout << " OK\n";

    // Test 4: QuiverIconView fallback to pixbuf callback if texture callback is not set
    std::cout << "  [4] Testing QuiverIconView fallback to Pixbuf callback...";
    testData.texture_called = false;
    testData.pixbuf_called = false;

    // Reset texture callback to NULL, set pixbuf callback
    quiver_icon_view_set_thumbnail_texture_func(QUIVER_ICON_VIEW(iconview), NULL, NULL, NULL);
    quiver_icon_view_set_thumbnail_pixbuf_func(QUIVER_ICON_VIEW(iconview), test_thumbnail_pixbuf_cb, &testData, NULL);

    snapshot = gtk_snapshot_new();
    GTK_WIDGET_GET_CLASS(iconview)->snapshot(iconview, snapshot);
    node = gtk_snapshot_free_to_node(snapshot);
    assert(node != NULL);
    gsk_render_node_unref(node);

    assert(testData.pixbuf_called == true);
    std::cout << " OK\n";

    // Test 5: Rubber band drag selection and GPU rendering
    std::cout << "  [5] Testing Rubber Band selection and GPU snapshot...";
    quiver_icon_view_set_drag_behavior(QUIVER_ICON_VIEW(iconview), QUIVER_ICON_VIEW_DRAG_BEHAVIOR_RUBBER_BAND);

    // Snapshot with rubber band enabled to verify GPU render nodes (color + border)
    snapshot = gtk_snapshot_new();
    GTK_WIDGET_GET_CLASS(iconview)->snapshot(iconview, snapshot);
    node = gtk_snapshot_free_to_node(snapshot);
    assert(node != NULL);
    gsk_render_node_unref(node);
    std::cout << " OK\n";

    // Test 6: Folder cell mouse position tracking
    std::cout << "  [6] Testing quiver_icon_view_get_cell_mouse_position...";
    {
        gint mx = 0, my = 0;
        // Initially, pointer is not over any cell
        quiver_icon_view_get_cell_mouse_position(QUIVER_ICON_VIEW(iconview), 0, &mx, &my);
        assert(mx == -1 && my == -1);

        // Find the motion controller
        GListModel *controllers = gtk_widget_observe_controllers(iconview);
        guint n_ctrl = g_list_model_get_n_items(controllers);
        GtkEventController *motion_ctrl = NULL;
        GtkGesture *drag_gesture = NULL;
        GtkGesture *click_gesture = NULL;

        for (guint i = 0; i < n_ctrl; i++)
        {
            gpointer item = g_list_model_get_item(controllers, i);
            if (GTK_IS_EVENT_CONTROLLER_MOTION(item))
            {
                motion_ctrl = GTK_EVENT_CONTROLLER(item);
            }
            if (GTK_IS_GESTURE_DRAG(item))
            {
                drag_gesture = GTK_GESTURE(item);
            }
            if (GTK_IS_GESTURE_CLICK(item))
            {
                click_gesture = GTK_GESTURE(item);
            }
            g_object_unref(item);
        }
        g_object_unref(controllers);

        assert(motion_ctrl != NULL);
        assert(drag_gesture != NULL);
        assert(click_gesture != NULL);

        // Calculate approximate center of cell 0
        guint cell_w = quiver_icon_view_get_cell_width(QUIVER_ICON_VIEW(iconview));
        guint cell_h = quiver_icon_view_get_cell_height(QUIVER_ICON_VIEW(iconview));
        guint icon_w = 0, icon_h = 0;
        quiver_icon_view_get_icon_size(QUIVER_ICON_VIEW(iconview), &icon_w, &icon_h);

        // Simulate mouse moving inside cell 0
        double cell0_x = cell_w / 2.0;
        double cell0_y = cell_h / 2.0;
        g_signal_emit_by_name(motion_ctrl, "motion", cell0_x, cell0_y);

        // Query position for cell 0: should be within [0, icon_w) and [0, icon_h)
        quiver_icon_view_get_cell_mouse_position(QUIVER_ICON_VIEW(iconview), 0, &mx, &my);
        assert(mx >= 0 && mx < (gint)icon_w);
        assert(my >= 0 && my < (gint)icon_h);

        // Query position for cell 1: must be -1, -1 because mouse is NOT over cell 1
        quiver_icon_view_get_cell_mouse_position(QUIVER_ICON_VIEW(iconview), 1, &mx, &my);
        assert(mx == -1 && my == -1);

        // Query position for cell 2: must also be -1, -1
        quiver_icon_view_get_cell_mouse_position(QUIVER_ICON_VIEW(iconview), 2, &mx, &my);
        assert(mx == -1 && my == -1);

        // Now move mouse over cell 1
        double cell1_x = cell_w + cell_w / 2.0;
        double cell1_y = cell_h / 2.0;
        g_signal_emit_by_name(motion_ctrl, "motion", cell1_x, cell1_y);

        // Cell 0 must now return -1, -1
        quiver_icon_view_get_cell_mouse_position(QUIVER_ICON_VIEW(iconview), 0, &mx, &my);
        assert(mx == -1 && my == -1);

        // Cell 1 must return valid coordinates
        quiver_icon_view_get_cell_mouse_position(QUIVER_ICON_VIEW(iconview), 1, &mx, &my);
        assert(mx >= 0 && mx < (gint)icon_w);
        assert(my >= 0 && my < (gint)icon_h);

        // When mouse leaves the widget, all cells should return -1, -1
        g_signal_emit_by_name(motion_ctrl, "leave");
        quiver_icon_view_get_cell_mouse_position(QUIVER_ICON_VIEW(iconview), 0, &mx, &my);
        assert(mx == -1 && my == -1);
        quiver_icon_view_get_cell_mouse_position(QUIVER_ICON_VIEW(iconview), 1, &mx, &my);
        assert(mx == -1 && my == -1);
        std::cout << " OK\n";

        // Test 7: Rubber band drag and release preserves selection
        std::cout << "  [7] Testing Rubber Band release preserves multi-selection...";
        // Deselect all
        quiver_icon_view_set_selection(QUIVER_ICON_VIEW(iconview), NULL);
        GList *sel = quiver_icon_view_get_selection(QUIVER_ICON_VIEW(iconview));
        assert(sel == NULL);

        // Start rubber band on padding/empty space (0, 0)
        g_signal_emit_by_name(click_gesture, "pressed", 1, 0.0, 0.0);
        g_signal_emit_by_name(drag_gesture, "drag-begin", 0.0, 0.0);

        // Drag across cell 0 and cell 1
        double drag_target_x = cell_w * 2.5;
        double drag_target_y = cell_h * 1.5;
        g_signal_emit_by_name(drag_gesture, "drag-update", drag_target_x, drag_target_y);

        sel = quiver_icon_view_get_selection(QUIVER_ICON_VIEW(iconview));
        assert(sel != NULL);
        guint n_selected_during_drag = g_list_length(sel);
        assert(n_selected_during_drag >= 2);
        g_list_free(sel);

        // End drag and release mouse directly over cell 1
        g_signal_emit_by_name(drag_gesture, "drag-end", drag_target_x, drag_target_y);
        g_signal_emit_by_name(click_gesture, "released", 1, drag_target_x, drag_target_y);

        // Verify selection is still preserved (NOT reset to single cell)
        sel = quiver_icon_view_get_selection(QUIVER_ICON_VIEW(iconview));
        assert(sel != NULL);
        guint n_selected_after_release = g_list_length(sel);
        assert(n_selected_after_release == n_selected_during_drag);
        g_list_free(sel);
        std::cout << " OK\n";
    }

    // Cleanup widget
    g_object_ref_sink(scrolled);
    g_object_unref(scrolled);

    std::cout << "All QuiverIconView & ImageCache tests PASSED successfully!\n";
    return 0;
}
