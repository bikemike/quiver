#include <catch2/catch_test_macros.hpp>
#include "QuiverUtils.h"
#include "test_helpers.h"
#include <gdk-pixbuf/gdk-pixbuf.h>

TEST_CASE("QuiverUtils EXIF Reorientation", "[unit][pixbuf][fast]")
{
    // Create an asymmetric 100x50 pixbuf
    GdkPixbuf* orig = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 100, 50);
    REQUIRE(orig != nullptr);

    SECTION("Orientation 1: No change")
    {
        GdkPixbuf* mod = QuiverUtils::GdkPixbufExifReorientate(orig, 1);
        // Returns NULL indicating original is untouched
        REQUIRE(mod == nullptr);
    }

    SECTION("Orientation 2: Horizontal flip preserves dimensions")
    {
        GdkPixbuf* mod = QuiverUtils::GdkPixbufExifReorientate(orig, 2);
        REQUIRE(mod != nullptr);
        REQUIRE(gdk_pixbuf_get_width(mod) == 100);
        REQUIRE(gdk_pixbuf_get_height(mod) == 50);
        g_object_unref(mod);
    }

    SECTION("Orientation 3: 180 rotation preserves dimensions")
    {
        GdkPixbuf* mod = QuiverUtils::GdkPixbufExifReorientate(orig, 3);
        REQUIRE(mod != nullptr);
        REQUIRE(gdk_pixbuf_get_width(mod) == 100);
        REQUIRE(gdk_pixbuf_get_height(mod) == 50);
        g_object_unref(mod);
    }

    SECTION("Orientation 4: Vertical flip preserves dimensions")
    {
        GdkPixbuf* mod = QuiverUtils::GdkPixbufExifReorientate(orig, 4);
        REQUIRE(mod != nullptr);
        REQUIRE(gdk_pixbuf_get_width(mod) == 100);
        REQUIRE(gdk_pixbuf_get_height(mod) == 50);
        g_object_unref(mod);
    }

    SECTION("Orientation 6: 90 CW rotation swaps width and height")
    {
        GdkPixbuf* mod = QuiverUtils::GdkPixbufExifReorientate(orig, 6);
        REQUIRE(mod != nullptr);
        REQUIRE(gdk_pixbuf_get_width(mod) == 50);
        REQUIRE(gdk_pixbuf_get_height(mod) == 100);
        g_object_unref(mod);
    }

    SECTION("Orientation 8: 270 CW rotation swaps width and height")
    {
        GdkPixbuf* mod = QuiverUtils::GdkPixbufExifReorientate(orig, 8);
        REQUIRE(mod != nullptr);
        REQUIRE(gdk_pixbuf_get_width(mod) == 50);
        REQUIRE(gdk_pixbuf_get_height(mod) == 100);
        g_object_unref(mod);
    }

    g_object_unref(orig);
}

TEST_CASE("QuiverUtils Action Management", "[unit][actions][fast]")
{
    QuiverUtils::InitActions();

    SECTION("Toggle Action state operations")
    {
        QuiverUtils::AddToggleAction("test_toggle", nullptr, FALSE, nullptr, nullptr);
        REQUIRE(QuiverUtils::ToggleActionGetActive("test_toggle") == FALSE);

        QuiverUtils::ToggleActionSetActive("test_toggle", TRUE);
        REQUIRE(QuiverUtils::ToggleActionGetActive("test_toggle") == TRUE);

        QuiverUtils::ToggleActionSetActive("test_toggle", FALSE);
        REQUIRE(QuiverUtils::ToggleActionGetActive("test_toggle") == FALSE);
    }

    SECTION("Radio Action state operations")
    {
        const char* const radioNames[] = {"sort_name", "sort_date", "sort_size"};
        const gint values[] = {1, 2, 3};

        QuiverUtils::AddRadioActions(radioNames, values, 3, 1, nullptr, nullptr);
        REQUIRE(QuiverUtils::GetRadioActionCurrent("sort_name") == 1);

        QuiverUtils::SetRadioActionCurrent("sort_name", 2);
        REQUIRE(QuiverUtils::GetRadioActionCurrent("sort_name") == 2);

        QuiverUtils::SetRadioActionCurrent("sort_name", 3);
        REQUIRE(QuiverUtils::GetRadioActionCurrent("sort_name") == 3);
    }
}

TEST_CASE("QuiverUtils SetWidgetBgColor", "[unit][gui][color]")
{
    REQUIRE_DISPLAY();

    // NULL widget should be a safe no-op
    GdkRGBA c1 = { 0.1f, 0.2f, 0.3f, 1.0f };
    QuiverUtils::SetWidgetBgColor(nullptr, &c1);
    QuiverUtils::SetWidgetBgColor(nullptr, nullptr);

    GtkWidget *widget = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    g_object_ref_sink(widget);

    // Initial color set
    QuiverUtils::SetWidgetBgColor(widget, &c1);

    // Change color (previously caused double free if old color existed)
    GdkRGBA c2 = { 0.4f, 0.5f, 0.6f, 1.0f };
    QuiverUtils::SetWidgetBgColor(widget, &c2);

    // Change color again
    GdkRGBA c3 = { 0.7f, 0.8f, 0.9f, 1.0f };
    QuiverUtils::SetWidgetBgColor(widget, &c3);

    // Reset to theme color (color == NULL) - was the crash trigger in user trace
    QuiverUtils::SetWidgetBgColor(widget, nullptr);

    // Reset again (redundant call should be safe)
    QuiverUtils::SetWidgetBgColor(widget, nullptr);

    // Set color again after reset
    QuiverUtils::SetWidgetBgColor(widget, &c1);

    // Destroy widget while custom color is still set (must clean up provider from display safely)
    g_object_unref(widget);
}

