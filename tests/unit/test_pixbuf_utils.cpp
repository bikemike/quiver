#include <config.h>
#include <catch2/catch_test_macros.hpp>
#if HAVE_GDK_PIXBUF
#include <gdk-pixbuf/gdk-pixbuf.h>
#endif
#include "quiver-pixbuf-utils.h"

TEST_CASE("quiver_rect_get_bound_size Aspect Ratio Calculations", "[unit][pixbuf][fast]")
{
    SECTION("Landscape image fitted inside square bounds")
    {
        guint w = 800;
        guint h = 400; // 2:1 aspect ratio
        quiver_rect_get_bound_size(200, 200, &w, &h, TRUE);

        REQUIRE(w == 200);
        REQUIRE(h == 100);
    }

    SECTION("Portrait image fitted inside square bounds")
    {
        guint w = 300;
        guint h = 600; // 1:2 aspect ratio
        quiver_rect_get_bound_size(200, 200, &w, &h, TRUE);

        REQUIRE(w == 100);
        REQUIRE(h == 200);
    }

    SECTION("Smaller image with fill_if_smaller = FALSE remains unchanged")
    {
        guint w = 50;
        guint h = 60;
        quiver_rect_get_bound_size(200, 200, &w, &h, FALSE);

        REQUIRE(w == 50);
        REQUIRE(h == 60);
    }

    SECTION("Smaller image with fill_if_smaller = TRUE is upscaled")
    {
        guint w = 50;
        guint h = 100;
        quiver_rect_get_bound_size(200, 200, &w, &h, TRUE);

        REQUIRE(w == 100);
        REQUIRE(h == 200);
    }

    SECTION("Exact match bounds")
    {
        guint w = 1920;
        guint h = 1080;
        quiver_rect_get_bound_size(1920, 1080, &w, &h, TRUE);

        REQUIRE(w == 1920);
        REQUIRE(h == 1080);
    }
}

#if HAVE_GDK_PIXBUF
TEST_CASE("pixbuf pixel manipulation functions", "[unit][pixbuf][fast]")
{
    SECTION("pixbuf_set_alpha scales existing alpha")
    {
        // Create 2x2 RGBA pixbuf with alpha=255
        GdkPixbuf* pb = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, 2, 2);
        REQUIRE(pb != nullptr);
        gdk_pixbuf_fill(pb, 0xFFFFFFFF);

        // Reduce alpha to 128 (~50%)
        pixbuf_set_alpha(pb, 128);

        guchar* pixels = gdk_pixbuf_get_pixels(pb);
        int alpha = pixels[3];
        // 255 * (128/255) ~ 128
        REQUIRE((alpha >= 127 && alpha <= 129));

        g_object_unref(pb);
    }

    SECTION("pixbuf_set_grayscale turns RGB into equal luminance")
    {
        GdkPixbuf* pb = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 2, 2);
        REQUIRE(pb != nullptr);
        // Pure red
        gdk_pixbuf_fill(pb, 0xFF000000);

        pixbuf_set_grayscale(pb);

        guchar* pixels = gdk_pixbuf_get_pixels(pb);
        guchar r = pixels[0];
        guchar g = pixels[1];
        guchar b = pixels[2];

        // Gray pixel must have R == G == B
        REQUIRE(r == g);
        REQUIRE(g == b);
        // For pure red, luminance is around ~76 (0.30 * 255)
        REQUIRE((r >= 70 && r <= 85));

        g_object_unref(pb);
    }
}
#endif
