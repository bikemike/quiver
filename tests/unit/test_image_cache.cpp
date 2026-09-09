#include <catch2/catch_test_macros.hpp>
#include "ImageCache.h"
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gtk/gtk.h>

static GdkTexture* CreateTestTexture(int w, int h)
{
    gsize size = (gsize)w * h * 4;
    guint32* data = (guint32*)g_malloc0(size);
    for (int i = 0; i < w * h; ++i) data[i] = 0xFF0000FF;
    GBytes* bytes = g_bytes_new_take(data, size);
    GdkTexture* tex = gdk_memory_texture_new(w, h, GDK_MEMORY_DEFAULT, bytes, (gsize)w * 4);
    g_bytes_unref(bytes);
    return tex;
}

#if HAVE_GDK_PIXBUF
static GdkPixbuf* CreateTestPixbuf(int w, int h, guint32 color = 0xFF0000FF)
{
    GdkPixbuf* pb = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, w, h);
    if (pb)
        gdk_pixbuf_fill(pb, color);
    return pb;
}
#endif

TEST_CASE("ImageCache Texture Caching and Retrieval", "[unit][cache][fast]")
{
    ImageCache cache(5);
    REQUIRE(cache.GetSize() == 5);

    GdkTexture* t1 = CreateTestTexture(10, 10);
    REQUIRE(t1 != nullptr);

    cache.AddTexture("file1.png", t1);
    REQUIRE(cache.InCache("file1.png"));

    GdkTexture* cached = cache.GetTexture("file1.png");
    REQUIRE(cached != nullptr);
    REQUIRE(gdk_texture_get_width(cached) == 10);
    REQUIRE(gdk_texture_get_height(cached) == 10);

    g_object_unref(cached);
    g_object_unref(t1);

    SECTION("Explicit removal")
    {
        REQUIRE(cache.RemoveTexture("file1.png") == true);
        REQUIRE_FALSE(cache.InCache("file1.png"));
        REQUIRE(cache.GetTexture("file1.png") == nullptr);
    }
}

TEST_CASE("ImageCache Texture Capacity and LRU Eviction", "[unit][cache][fast]")
{
    // Cache with capacity of 2 items
    ImageCache cache(2);

    GdkTexture* p1 = CreateTestTexture(10, 10);
    GdkTexture* p2 = CreateTestTexture(20, 20);
    GdkTexture* p3 = CreateTestTexture(30, 30);

    // Add item 1 at time 100
    cache.AddTexture("item1", p1, 100);
    // Add item 2 at time 200
    cache.AddTexture("item2", p2, 200);

    REQUIRE(cache.InCache("item1"));
    REQUIRE(cache.InCache("item2"));

    // Adding item 3 at time 300 should evict oldest (item1)
    cache.AddTexture("item3", p3, 300);

    REQUIRE_FALSE(cache.InCache("item1"));
    REQUIRE(cache.InCache("item2"));
    REQUIRE(cache.InCache("item3"));

    g_object_unref(p1);
    g_object_unref(p2);
    g_object_unref(p3);
}

TEST_CASE("ImageCache Texture Dynamic Resizing", "[unit][cache][fast]")
{
    ImageCache cache(4);

    GdkTexture* p1 = CreateTestTexture(10, 10);
    GdkTexture* p2 = CreateTestTexture(10, 10);
    GdkTexture* p3 = CreateTestTexture(10, 10);

    cache.AddTexture("a", p1, 10);
    cache.AddTexture("b", p2, 20);
    cache.AddTexture("c", p3, 30);

    REQUIRE(cache.InCache("a"));
    REQUIRE(cache.InCache("b"));
    REQUIRE(cache.InCache("c"));

    // Shrink cache size to 1: should evict oldest two ("a" and "b")
    cache.SetSize(1);
    REQUIRE(cache.GetSize() == 1);

    REQUIRE_FALSE(cache.InCache("a"));
    REQUIRE_FALSE(cache.InCache("b"));
    REQUIRE(cache.InCache("c"));

    g_object_unref(p1);
    g_object_unref(p2);
    g_object_unref(p3);
}

#if HAVE_GDK_PIXBUF
TEST_CASE("ImageCache Legacy Pixbuf Caching", "[unit][cache][fast]")
{
    ImageCache cache(5);
    GdkPixbuf* pb1 = CreateTestPixbuf(10, 10);
    REQUIRE(pb1 != nullptr);

    cache.AddPixbuf("file1.png", pb1);
    REQUIRE(cache.InCache("file1.png"));

    GdkPixbuf* cached = cache.GetPixbuf("file1.png");
    REQUIRE(cached != nullptr);
    g_object_unref(cached);
    g_object_unref(pb1);
}
#endif

TEST_CASE("ImageCache Failure Tracking and Clear", "[unit][cache][fast]")
{
    ImageCache cache(5);

    REQUIRE_FALSE(cache.HasFailed("corrupt.jpg"));
    cache.AddFailure("corrupt.jpg");
    REQUIRE(cache.HasFailed("corrupt.jpg"));
    REQUIRE(cache.InCache("corrupt.jpg"));

    cache.Clear();
    REQUIRE_FALSE(cache.HasFailed("corrupt.jpg"));
    REQUIRE_FALSE(cache.InCache("corrupt.jpg"));
}
