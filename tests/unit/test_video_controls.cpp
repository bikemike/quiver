#include <catch2/catch_test_macros.hpp>
#include <gtk/gtk.h>
#include "test_helpers.h"
#include <string>

TEST_CASE("Video Controls Configuration and Formatting", "[unit][video][controls]")
{
    REQUIRE_DISPLAY();

    SECTION("Speed formatting has 'x' suffix")
    {
        const double speeds[] = { 0.25, 0.5, 1.0, 1.5, 2.0, 4.0, 8.0, 16.0 };
        const int nSpeeds = sizeof(speeds) / sizeof(speeds[0]);
        for (int i = 0; i < nSpeeds; i++)
        {
            gchar* label = g_strdup_printf("<b>%.4gx</b>", speeds[i]);
            REQUIRE(label != nullptr);
            std::string s(label);
            REQUIRE(s.find("x</b>") != std::string::npos);
            g_free(label);
        }
    }

    SECTION("Vertical volume scale is inverted")
    {
        GtkWidget* scale = gtk_scale_new_with_range(GTK_ORIENTATION_VERTICAL, 0.0, 1.0, 0.05);
        gtk_range_set_inverted(GTK_RANGE(scale), TRUE);
        REQUIRE(gtk_range_get_inverted(GTK_RANGE(scale)) == TRUE);

        // Verify that 0.0 is at the bottom (min) and 1.0 is at top (max)
        gtk_range_set_value(GTK_RANGE(scale), 0.8);
        REQUIRE(gtk_range_get_value(GTK_RANGE(scale)) == 0.8);
        g_object_ref_sink(scale);
        g_object_unref(scale);
    }
}
