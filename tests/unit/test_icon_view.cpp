#include <catch2/catch_test_macros.hpp>
#include <gtk/gtk.h>
#include "quiver-icon-view.h"
#include "ImageCache.h"
#include "test_helpers.h"

struct IconTestData {
    ImageCache cache{10};
    bool texture_called = false;
    bool pixbuf_called = false;
};

static gulong test_n_items_cb(QuiverIconView *iconview, gpointer user_data)
{
    (void)iconview;
    (void)user_data;
    return 4;
}

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

static GdkTexture* create_test_texture(int width, int height, guint32 color_rgba)
{
    GdkPixbuf *pb = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, width, height);
    if (!pb) return nullptr;
    gdk_pixbuf_fill(pb, color_rgba);
    GBytes *bytes = gdk_pixbuf_read_pixel_bytes(pb);
    GdkTexture *tex = gdk_memory_texture_new(
        width, height,
        GDK_MEMORY_R8G8B8A8,
        bytes,
        gdk_pixbuf_get_rowstride(pb)
    );
    g_bytes_unref(bytes);
    g_object_unref(pb);
    return tex;
}

TEST_CASE("QuiverIconView Widget Integration", "[gui][widget]")
{
    REQUIRE_DISPLAY();

    IconTestData testData;
    GdkTexture *t0 = create_test_texture(96, 96, 0xFF0000FF);
    REQUIRE(t0 != nullptr);
    testData.cache.AddTexture("item_0", t0);
    g_object_unref(t0);

    GtkWidget *scrolled = gtk_scrolled_window_new();
    GtkWidget *iconview = quiver_icon_view_new();
    REQUIRE(QUIVER_IS_ICON_VIEW(iconview));

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), iconview);
    quiver_icon_view_set_n_items_func(QUIVER_ICON_VIEW(iconview), test_n_items_cb, &testData, NULL);
    quiver_icon_view_set_thumbnail_texture_func(QUIVER_ICON_VIEW(iconview), test_thumbnail_texture_cb, &testData, NULL);
    quiver_icon_view_set_icon_size(QUIVER_ICON_VIEW(iconview), 96, 96);

    // Allocate size for scrolled window and icon view
    gtk_widget_allocate(scrolled, 400, 300, -1, NULL);

    // Snapshot layout
    GtkSnapshot *snapshot = gtk_snapshot_new();
    GTK_WIDGET_GET_CLASS(iconview)->snapshot(iconview, snapshot);
    GskRenderNode *node = gtk_snapshot_free_to_node(snapshot);
    REQUIRE(node != nullptr);
    gsk_render_node_unref(node);

    REQUIRE(testData.texture_called == true);

    // Test quiver_icon_view_get_cell_rect
    GdkRectangle rect0;
    REQUIRE(quiver_icon_view_get_cell_rect(QUIVER_ICON_VIEW(iconview), 0, &rect0) == TRUE);
    REQUIRE(rect0.width > 0);
    REQUIRE(rect0.height > 0);
    REQUIRE(rect0.x >= 0);
    REQUIRE(rect0.y >= 0);

    GdkRectangle rect_invalid;
    REQUIRE(quiver_icon_view_get_cell_rect(QUIVER_ICON_VIEW(iconview), 9999, &rect_invalid) == FALSE);

    // Test quiver_icon_view_is_cell_visible and quiver_icon_view_get_cell_target_rect
    REQUIRE(quiver_icon_view_is_cell_visible(QUIVER_ICON_VIEW(iconview), 0) == TRUE);
    GdkRectangle target_rect0;
    REQUIRE(quiver_icon_view_get_cell_target_rect(QUIVER_ICON_VIEW(iconview), 0, &target_rect0) == TRUE);
    REQUIRE(target_rect0.x == rect0.x);
    REQUIRE(target_rect0.y == rect0.y);

    g_object_ref_sink(scrolled);
    g_object_unref(scrolled);
}

static gulong test_many_items_cb(QuiverIconView *iconview, gpointer user_data)
{
    (void)iconview;
    (void)user_data;
    return 100;
}

static void drain_main_loop()
{
    while (g_main_context_iteration(NULL, FALSE)) {}
}

TEST_CASE("QuiverIconView Resize Top-Left Preservation and Scroll Callback", "[gui][widget]")
{
    REQUIRE_DISPLAY();

    GtkWidget *scrolled = gtk_scrolled_window_new();
    GtkWidget *iconview = quiver_icon_view_new();
    REQUIRE(QUIVER_IS_ICON_VIEW(iconview));

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), iconview);
    quiver_icon_view_set_n_items_func(QUIVER_ICON_VIEW(iconview), test_many_items_cb, NULL, NULL);
    quiver_icon_view_set_icon_size(QUIVER_ICON_VIEW(iconview), 80, 80);

    guint cw = quiver_icon_view_get_cell_width(QUIVER_ICON_VIEW(iconview));
    guint ch = quiver_icon_view_get_cell_height(QUIVER_ICON_VIEW(iconview));
    REQUIRE(cw > 0);
    REQUIRE(ch > 0);

    // Initial allocate: 4 columns wide
    int init_w = (int)(cw * 4);
    int init_h = (int)(ch * 5);
    gtk_widget_allocate(scrolled, init_w, init_h, -1, NULL);

    GtkAdjustment *vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scrolled));
    REQUIRE(vadj != nullptr);

    // Set cursor to cell 0
    quiver_icon_view_set_cursor_cell_silent(QUIVER_ICON_VIEW(iconview), 0);

    // Scroll down to row 6 (item 24 at 4 cols)
    double target_scroll = (double)(ch * 6);
    gtk_adjustment_set_value(vadj, target_scroll);
    drain_main_loop();

    // Verify cell 24 is at the top of the view
    GdkRectangle r24;
    REQUIRE(quiver_icon_view_get_cell_rect(QUIVER_ICON_VIEW(iconview), 24, &r24) == TRUE);
    REQUIRE(r24.y == 0);

    // Re-allocate with SAME size (simulating popover show/hide)
    // Should NOT scroll back to cursor cell 0!
    gtk_widget_allocate(scrolled, init_w, init_h, -1, NULL);
    drain_main_loop();

    REQUIRE(quiver_icon_view_get_cell_rect(QUIVER_ICON_VIEW(iconview), 24, &r24) == TRUE);
    REQUIRE(r24.y == 0);

    // Resize window wider: 4 -> 6 columns (top-right item 27 used -> 27 / 6 = row 4)
    int new_w = (int)(cw * 6);
    gtk_widget_allocate(scrolled, new_w, init_h, -1, NULL);
    drain_main_loop();

    REQUIRE(quiver_icon_view_get_cell_rect(QUIVER_ICON_VIEW(iconview), 24, &r24) == TRUE);
    REQUIRE(r24.y == 0);

    // Resize window narrower back: 6 -> 4 columns (top-left item 24 used -> 24 / 4 = row 6)
    gtk_widget_allocate(scrolled, init_w, init_h, -1, NULL);
    drain_main_loop();

    REQUIRE(quiver_icon_view_get_cell_rect(QUIVER_ICON_VIEW(iconview), 24, &r24) == TRUE);
    REQUIRE(r24.y == 0);

    // Repeat cycle: 4 -> 6 -> 4 to confirm zero drift
    gtk_widget_allocate(scrolled, new_w, init_h, -1, NULL);
    drain_main_loop();
    REQUIRE(quiver_icon_view_get_cell_rect(QUIVER_ICON_VIEW(iconview), 24, &r24) == TRUE);
    REQUIRE(r24.y == 0);

    gtk_widget_allocate(scrolled, init_w, init_h, -1, NULL);
    drain_main_loop();
    REQUIRE(quiver_icon_view_get_cell_rect(QUIVER_ICON_VIEW(iconview), 24, &r24) == TRUE);
    REQUIRE(r24.y == 0);

    // Multi-step continuous drag (4 -> 5 -> 7 -> 3 -> 4 columns)
    int w5 = (int)(cw * 5);
    int w7 = (int)(cw * 7);
    int w3 = (int)(cw * 3);
    gtk_widget_allocate(scrolled, w5, init_h, -1, NULL);
    drain_main_loop();
    gtk_widget_allocate(scrolled, w7, init_h, -1, NULL);
    drain_main_loop();
    gtk_widget_allocate(scrolled, w3, init_h, -1, NULL);
    drain_main_loop();
    gtk_widget_allocate(scrolled, init_w, init_h, -1, NULL);
    drain_main_loop();

    REQUIRE(quiver_icon_view_get_cell_rect(QUIVER_ICON_VIEW(iconview), 24, &r24) == TRUE);
    REQUIRE(r24.y == 0);

    // Test scroll-to-cell with callback: scroll to cell 95 (offscreen)
    bool callback_called = false;
    quiver_icon_view_scroll_to_cell_with_callback(
        QUIVER_ICON_VIEW(iconview),
        95,
        [](QuiverIconView *iv, gulong cell, gpointer data) {
            (void)iv;
            (void)cell;
            auto *pCalled = static_cast<bool*>(data);
            *pCalled = true;
        },
        &callback_called
    );

    // Process event loop for smooth scroll animation to finish
    int iterations = 0;
    while (!callback_called && iterations++ < 50)
    {
        drain_main_loop();
        g_usleep(25000);
    }
    REQUIRE(callback_called == true);
    REQUIRE(quiver_icon_view_is_cell_visible(QUIVER_ICON_VIEW(iconview), 95) == TRUE);

    g_object_ref_sink(scrolled);
    g_object_unref(scrolled);
}

