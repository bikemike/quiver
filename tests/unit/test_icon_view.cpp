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

struct FilmstripTestContext {
    GdkTexture *strip_tex_128 = nullptr;
    GdkTexture *strip_tex_256 = nullptr;
    GdkTexture *thumb_tex = nullptr;
    bool callback_called = false;
    gint last_drawn_w = 0;
    gint last_drawn_h = 0;
};

static void collect_texture_bounds(GskRenderNode *node, std::vector<graphene_rect_t> &bounds)
{
    if (!node) return;
    GskRenderNodeType type = gsk_render_node_get_node_type(node);
    if (type == GSK_TEXTURE_NODE)
    {
        graphene_rect_t b;
        gsk_render_node_get_bounds(node, &b);
        bounds.push_back(b);
    }
    else if (type == GSK_CONTAINER_NODE)
    {
        guint n = gsk_container_node_get_n_children(node);
        for (guint i = 0; i < n; i++)
            collect_texture_bounds(gsk_container_node_get_child(node, i), bounds);
    }
    else if (type == GSK_CLIP_NODE)
    {
        collect_texture_bounds(gsk_clip_node_get_child(node), bounds);
    }
    else if (type == GSK_TRANSFORM_NODE)
    {
        collect_texture_bounds(gsk_transform_node_get_child(node), bounds);
    }
}

static GdkTexture* test_filmstrip_cb(QuiverIconView *iv, gulong cell,
    gint nw, gint nh, gint dw, gint dh,
    QuiverIconViewFilmstripSide side, gpointer user_data)
{
    (void)iv; (void)cell; (void)nw; (void)nh; (void)side;
    auto *ctx = static_cast<FilmstripTestContext*>(user_data);
    ctx->callback_called = true;
    ctx->last_drawn_w = dw;
    ctx->last_drawn_h = dh;

    gint max_dim = std::max(dw, dh);
    GdkTexture *chosen = (max_dim > 128) ? ctx->strip_tex_256 : ctx->strip_tex_128;
    if (chosen)
        g_object_ref(chosen);
    return chosen;
}

static GdkTexture* test_thumb_texture_cb(QuiverIconView *iv, gulong cell,
    gint *aw, gint *ah, gpointer user_data)
{
    (void)iv; (void)cell;
    auto *ctx = static_cast<FilmstripTestContext*>(user_data);
    if (ctx->thumb_tex)
    {
        *aw = gdk_texture_get_width(ctx->thumb_tex);
        *ah = gdk_texture_get_height(ctx->thumb_tex);
        g_object_ref(ctx->thumb_tex);
    }
    return ctx->thumb_tex;
}

static gulong test_single_item_cb(QuiverIconView *iv, gpointer user_data)
{
    (void)iv; (void)user_data;
    return 1;
}

TEST_CASE("QuiverIconView Filmstrip Overlay Scaling and Asset Selection", "[gui][filmstrip]")
{
    REQUIRE_DISPLAY();

    FilmstripTestContext ctx;
    // filmstrip.png is 9x35; filmstrip-big.png is 23x80
    ctx.strip_tex_128 = create_test_texture(9, 35, 0x000000FF);
    ctx.strip_tex_256 = create_test_texture(23, 80, 0x000000FF);
    REQUIRE(ctx.strip_tex_128 != nullptr);
    REQUIRE(ctx.strip_tex_256 != nullptr);

    GtkWidget *scrolled = gtk_scrolled_window_new();
    GtkWidget *iconview = quiver_icon_view_new();
    REQUIRE(QUIVER_IS_ICON_VIEW(iconview));
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), iconview);

    quiver_icon_view_set_n_items_func(QUIVER_ICON_VIEW(iconview), test_single_item_cb, &ctx, NULL);
    quiver_icon_view_set_thumbnail_texture_func(QUIVER_ICON_VIEW(iconview), test_thumb_texture_cb, &ctx, NULL);
    quiver_icon_view_set_get_filmstrip_texture_func(QUIVER_ICON_VIEW(iconview), test_filmstrip_cb, &ctx, NULL);
    quiver_icon_view_set_filmstrip_enabled(QUIVER_ICON_VIEW(iconview), TRUE);

    SECTION("128px icon size uses 128px strip at 1.0 scale (width 9, tile 35)")
    {
        ctx.thumb_tex = create_test_texture(128, 72, 0xFFFFFFFF);
        quiver_icon_view_set_icon_size(QUIVER_ICON_VIEW(iconview), 128, 128);
        gtk_widget_allocate(scrolled, 400, 300, -1, NULL);

        GtkSnapshot *snapshot = gtk_snapshot_new();
        GTK_WIDGET_GET_CLASS(iconview)->snapshot(iconview, snapshot);
        GskRenderNode *node = gtk_snapshot_free_to_node(snapshot);
        REQUIRE(node != nullptr);

        REQUIRE(ctx.callback_called == true);
        CHECK(ctx.last_drawn_w == 128);
        CHECK(ctx.last_drawn_h == 72);

        std::vector<graphene_rect_t> bounds;
        collect_texture_bounds(node, bounds);
        gsk_render_node_unref(node);

        // Find filmstrip tile bounds (width 9, height 35)
        bool found_tile = false;
        for (const auto &b : bounds)
        {
            if ((int)(b.size.width + 0.5) == 9 && (int)(b.size.height + 0.5) == 35)
                found_tile = true;
        }
        CHECK(found_tile == true);
        g_object_unref(ctx.thumb_tex);
    }

    SECTION("64px icon size uses 128px strip scaled by 0.5 (width 5, tile 18)")
    {
        ctx.thumb_tex = create_test_texture(64, 36, 0xFFFFFFFF);
        quiver_icon_view_set_icon_size(QUIVER_ICON_VIEW(iconview), 64, 64);
        gtk_widget_allocate(scrolled, 400, 300, -1, NULL);

        GtkSnapshot *snapshot = gtk_snapshot_new();
        GTK_WIDGET_GET_CLASS(iconview)->snapshot(iconview, snapshot);
        GskRenderNode *node = gtk_snapshot_free_to_node(snapshot);
        REQUIRE(node != nullptr);

        REQUIRE(ctx.callback_called == true);
        CHECK(ctx.last_drawn_w == 64);
        CHECK(ctx.last_drawn_h == 36);

        std::vector<graphene_rect_t> bounds;
        collect_texture_bounds(node, bounds);
        gsk_render_node_unref(node);

        // Scale factor: 64 / 128.0 = 0.5
        // strip_drawn_w: round(9 * 0.5) = 5
        // tile_h: round(35 * 0.5) = 18
        bool found_tile = false;
        for (const auto &b : bounds)
        {
            if ((int)(b.size.width + 0.5) == 5 && (int)(b.size.height + 0.5) == 18)
                found_tile = true;
        }
        CHECK(found_tile == true);
        g_object_unref(ctx.thumb_tex);
    }

    SECTION("256px icon size uses large strip at 1.0 scale (width 23, tile 80)")
    {
        ctx.thumb_tex = create_test_texture(256, 144, 0xFFFFFFFF);
        quiver_icon_view_set_icon_size(QUIVER_ICON_VIEW(iconview), 256, 256);
        gtk_widget_allocate(scrolled, 600, 500, -1, NULL);

        GtkSnapshot *snapshot = gtk_snapshot_new();
        GTK_WIDGET_GET_CLASS(iconview)->snapshot(iconview, snapshot);
        GskRenderNode *node = gtk_snapshot_free_to_node(snapshot);
        REQUIRE(node != nullptr);

        REQUIRE(ctx.callback_called == true);
        CHECK(ctx.last_drawn_w == 256);
        CHECK(ctx.last_drawn_h == 144);

        std::vector<graphene_rect_t> bounds;
        collect_texture_bounds(node, bounds);
        gsk_render_node_unref(node);

        // Large strip 23x80 at scale 1.0
        bool found_tile = false;
        for (const auto &b : bounds)
        {
            if ((int)(b.size.width + 0.5) == 23 && (int)(b.size.height + 0.5) == 80)
                found_tile = true;
        }
        CHECK(found_tile == true);
        g_object_unref(ctx.thumb_tex);
    }

    SECTION("192px icon size uses large strip scaled by 0.75 (width 17, tile 60)")
    {
        ctx.thumb_tex = create_test_texture(192, 108, 0xFFFFFFFF);
        quiver_icon_view_set_icon_size(QUIVER_ICON_VIEW(iconview), 192, 192);
        gtk_widget_allocate(scrolled, 600, 500, -1, NULL);

        GtkSnapshot *snapshot = gtk_snapshot_new();
        GTK_WIDGET_GET_CLASS(iconview)->snapshot(iconview, snapshot);
        GskRenderNode *node = gtk_snapshot_free_to_node(snapshot);
        REQUIRE(node != nullptr);

        REQUIRE(ctx.callback_called == true);
        CHECK(ctx.last_drawn_w == 192);
        CHECK(ctx.last_drawn_h == 108);

        std::vector<graphene_rect_t> bounds;
        collect_texture_bounds(node, bounds);
        gsk_render_node_unref(node);

        // Scale factor: 192 / 256.0 = 0.75
        // strip_drawn_w: round(23 * 0.75) = 17
        // tile_h: round(80 * 0.75) = 60
        bool found_tile = false;
        for (const auto &b : bounds)
        {
            if ((int)(b.size.width + 0.5) == 17 && (int)(b.size.height + 0.5) == 60)
                found_tile = true;
        }
        CHECK(found_tile == true);
        g_object_unref(ctx.thumb_tex);
    }

    SECTION("Disabled filmstrip skips callback")
    {
        ctx.thumb_tex = create_test_texture(128, 72, 0xFFFFFFFF);
        quiver_icon_view_set_filmstrip_enabled(QUIVER_ICON_VIEW(iconview), FALSE);
        quiver_icon_view_set_icon_size(QUIVER_ICON_VIEW(iconview), 128, 128);
        gtk_widget_allocate(scrolled, 400, 300, -1, NULL);

        GtkSnapshot *snapshot = gtk_snapshot_new();
        GTK_WIDGET_GET_CLASS(iconview)->snapshot(iconview, snapshot);
        GskRenderNode *node = gtk_snapshot_free_to_node(snapshot);
        REQUIRE(node != nullptr);
        gsk_render_node_unref(node);

        CHECK(ctx.callback_called == false);
        g_object_unref(ctx.thumb_tex);
    }

    SECTION("Stock icon fallback skips filmstrip callback")
    {
        ctx.thumb_tex = nullptr; // Simulates thumbnail still loading
        quiver_icon_view_set_icon_size(QUIVER_ICON_VIEW(iconview), 128, 128);
        gtk_widget_allocate(scrolled, 400, 300, -1, NULL);

        GtkSnapshot *snapshot = gtk_snapshot_new();
        GTK_WIDGET_GET_CLASS(iconview)->snapshot(iconview, snapshot);
        GskRenderNode *node = gtk_snapshot_free_to_node(snapshot);
        if (node)
            gsk_render_node_unref(node);

        CHECK(ctx.callback_called == false);
    }

    g_object_unref(ctx.strip_tex_128);
    g_object_unref(ctx.strip_tex_256);
    g_object_ref_sink(scrolled);
    g_object_unref(scrolled);
}

TEST_CASE("QuiverIconView Activate Modifiers", "[iconview][activate]")
{
    REQUIRE_DISPLAY();

    GtkWidget *iconview = quiver_icon_view_new();
    REQUIRE(iconview != nullptr);

    // Default modifiers should be 0
    CHECK(quiver_icon_view_get_last_activate_modifiers(QUIVER_ICON_VIEW(iconview)) == (GdkModifierType)0);

    // Set and get modifiers (e.g. Shift / Control)
    quiver_icon_view_set_last_activate_modifiers(QUIVER_ICON_VIEW(iconview), GDK_SHIFT_MASK);
    CHECK((quiver_icon_view_get_last_activate_modifiers(QUIVER_ICON_VIEW(iconview)) & GDK_SHIFT_MASK) != 0);

    quiver_icon_view_set_last_activate_modifiers(QUIVER_ICON_VIEW(iconview), GDK_CONTROL_MASK);
    CHECK((quiver_icon_view_get_last_activate_modifiers(QUIVER_ICON_VIEW(iconview)) & GDK_CONTROL_MASK) != 0);

    quiver_icon_view_set_last_activate_modifiers(QUIVER_ICON_VIEW(iconview), (GdkModifierType)0);
    CHECK(quiver_icon_view_get_last_activate_modifiers(QUIVER_ICON_VIEW(iconview)) == (GdkModifierType)0);

    g_object_ref_sink(iconview);
    g_object_unref(iconview);
}

TEST_CASE("QuiverIconView Cell Selected Check", "[iconview][selection]")
{
    REQUIRE_DISPLAY();

    GtkWidget *iconview = quiver_icon_view_new();
    REQUIRE(iconview != nullptr);

    // Set 4 items
    quiver_icon_view_set_n_items_func(QUIVER_ICON_VIEW(iconview), test_n_items_cb, NULL, NULL);

    // Initially no cell is selected
    CHECK(quiver_icon_view_is_cell_selected(QUIVER_ICON_VIEW(iconview), 0) == FALSE);
    CHECK(quiver_icon_view_is_cell_selected(QUIVER_ICON_VIEW(iconview), 1) == FALSE);
    CHECK(quiver_icon_view_is_cell_selected(QUIVER_ICON_VIEW(iconview), 2) == FALSE);
    CHECK(quiver_icon_view_is_cell_selected(QUIVER_ICON_VIEW(iconview), 3) == FALSE);

    // Out of bounds check
    CHECK(quiver_icon_view_is_cell_selected(QUIVER_ICON_VIEW(iconview), 99) == FALSE);

    // Select cells 1 and 2
    GList *sel = nullptr;
    sel = g_list_append(sel, (gpointer)(uintptr_t)1);
    sel = g_list_append(sel, (gpointer)(uintptr_t)2);
    quiver_icon_view_set_selection(QUIVER_ICON_VIEW(iconview), sel);
    g_list_free(sel);

    CHECK(quiver_icon_view_is_cell_selected(QUIVER_ICON_VIEW(iconview), 0) == FALSE);
    CHECK(quiver_icon_view_is_cell_selected(QUIVER_ICON_VIEW(iconview), 1) == TRUE);
    CHECK(quiver_icon_view_is_cell_selected(QUIVER_ICON_VIEW(iconview), 2) == TRUE);
    CHECK(quiver_icon_view_is_cell_selected(QUIVER_ICON_VIEW(iconview), 3) == FALSE);
    CHECK(quiver_icon_view_is_cell_selected(QUIVER_ICON_VIEW(iconview), 100) == FALSE);

    // Clear selection
    quiver_icon_view_set_selection(QUIVER_ICON_VIEW(iconview), nullptr);
    CHECK(quiver_icon_view_is_cell_selected(QUIVER_ICON_VIEW(iconview), 1) == FALSE);
    CHECK(quiver_icon_view_is_cell_selected(QUIVER_ICON_VIEW(iconview), 2) == FALSE);

    g_object_ref_sink(iconview);
    g_object_unref(iconview);
}


