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

    g_object_ref_sink(scrolled);
    g_object_unref(scrolled);
}
