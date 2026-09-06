#include <catch2/catch_test_macros.hpp>
#include "ImageCache.h"
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gtk/gtk.h>

static GdkPixbuf* CreateTestPixbuf(int w, int h, guint32 color = 0xFF0000FF)
{
    GdkPixbuf* pb = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, w, h);
    if (pb)
        gdk_pixbuf_fill(pb, color);
    return pb;
}

TEST_CASE("ImageCache Pixbuf Caching and Retrieval", "[unit][cache][fast]")
{
    ImageCache cache(5);
    REQUIRE(cache.GetSize() == 5);

    GdkPixbuf* pb1 = CreateTestPixbuf(10, 10);
    REQUIRE(pb1 != nullptr);

    cache.AddPixbuf("file1.png", pb1);
    REQUIRE(cache.InCache("file1.png"));

    GdkPixbuf* cached = cache.GetPixbuf("file1.png");
    REQUIRE(cached != nullptr);
    REQUIRE(gdk_pixbuf_get_width(cached) == 10);
    REQUIRE(gdk_pixbuf_get_height(cached) == 10);

    g_object_unref(cached);
    g_object_unref(pb1);

    SECTION("Explicit removal")
    {
        REQUIRE(cache.RemovePixbuf("file1.png") == true);
        REQUIRE_FALSE(cache.InCache("file1.png"));
        REQUIRE(cache.GetPixbuf("file1.png") == nullptr);
    }
}

TEST_CASE("ImageCache Capacity and LRU Eviction", "[unit][cache][fast]")
{
    // Cache with capacity of 2 items
    ImageCache cache(2);

    GdkPixbuf* p1 = CreateTestPixbuf(10, 10);
    GdkPixbuf* p2 = CreateTestPixbuf(20, 20);
    GdkPixbuf* p3 = CreateTestPixbuf(30, 30);

    // Add item 1 at time 100
    cache.AddPixbuf("item1", p1, 100);
    // Add item 2 at time 200
    cache.AddPixbuf("item2", p2, 200);

    REQUIRE(cache.InCache("item1"));
    REQUIRE(cache.InCache("item2"));

    // Adding item 3 at time 300 should evict oldest (item1)
    cache.AddPixbuf("item3", p3, 300);

    REQUIRE_FALSE(cache.InCache("item1"));
    REQUIRE(cache.InCache("item2"));
    REQUIRE(cache.InCache("item3"));

    g_object_unref(p1);
    g_object_unref(p2);
    g_object_unref(p3);
}

TEST_CASE("ImageCache Dynamic Resizing", "[unit][cache][fast]")
{
    ImageCache cache(4);

    GdkPixbuf* p1 = CreateTestPixbuf(10, 10);
    GdkPixbuf* p2 = CreateTestPixbuf(10, 10);
    GdkPixbuf* p3 = CreateTestPixbuf(10, 10);

    cache.AddPixbuf("a", p1, 10);
    cache.AddPixbuf("b", p2, 20);
    cache.AddPixbuf("c", p3, 30);

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
