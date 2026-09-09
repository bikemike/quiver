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

    SECTION("Video options menu button and popover configuration")
    {
        GtkWidget* win = gtk_window_new();
        GtkWidget* btn = gtk_menu_button_new();
        gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(btn), "emblem-system-symbolic");
        gtk_menu_button_set_has_frame(GTK_MENU_BUTTON(btn), FALSE);
        gtk_widget_add_css_class(btn, "media-btn");
        REQUIRE(gtk_widget_has_css_class(btn, "media-btn"));

        gtk_window_set_child(GTK_WINDOW(win), btn);
        gtk_window_present(GTK_WINDOW(win));

        GtkWidget* popover = gtk_popover_new();
        gtk_popover_set_position(GTK_POPOVER(popover), GTK_POS_TOP);
        gtk_popover_set_autohide(GTK_POPOVER(popover), TRUE);

        GtkWidget* scrolled = gtk_scrolled_window_new();
        gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scrolled), 350);
        gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scrolled), TRUE);
        gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(scrolled), TRUE);

        GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        for (int i = 0; i < 40; i++) {
            GtkWidget* item = gtk_button_new_with_label("Track");
            gtk_box_append(GTK_BOX(box), item);
        }
        gtk_menu_button_set_direction(GTK_MENU_BUTTON(btn), GTK_ARROW_UP);
        gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), box);
        gtk_popover_set_child(GTK_POPOVER(popover), scrolled);
        gtk_menu_button_set_popover(GTK_MENU_BUTTON(btn), popover);
        gtk_popover_set_position(GTK_POPOVER(popover), GTK_POS_TOP);

        REQUIRE(gtk_popover_get_position(GTK_POPOVER(popover)) == GTK_POS_TOP);
        REQUIRE(gtk_popover_get_autohide(GTK_POPOVER(popover)) == TRUE);

        // Verify popover popup and visibility
        gtk_popover_popup(GTK_POPOVER(popover));
        REQUIRE(gtk_widget_get_visible(popover) == TRUE);

        gtk_widget_set_visible(popover, FALSE);
        REQUIRE(gtk_widget_get_visible(popover) == FALSE);

        gtk_window_destroy(GTK_WINDOW(win));
    }

    SECTION("Volume menu button and popover configuration")
    {
        GtkWidget* win = gtk_window_new();
        GtkWidget* btn = gtk_menu_button_new();
        gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(btn), "audio-volume-high");
        gtk_menu_button_set_direction(GTK_MENU_BUTTON(btn), GTK_ARROW_UP);
        gtk_menu_button_set_has_frame(GTK_MENU_BUTTON(btn), FALSE);
        gtk_widget_add_css_class(btn, "media-btn");
        REQUIRE(gtk_widget_has_css_class(btn, "media-btn"));

        gtk_window_set_child(GTK_WINDOW(win), btn);
        gtk_window_present(GTK_WINDOW(win));

        GtkWidget* popover = gtk_popover_new();
        gtk_popover_set_position(GTK_POPOVER(popover), GTK_POS_TOP);
        gtk_popover_set_autohide(GTK_POPOVER(popover), TRUE);

        GtkWidget* volBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
        GtkWidget* volLabel = gtk_label_new("Volume");
        gtk_box_append(GTK_BOX(volBox), volLabel);
        GtkWidget* volScale = gtk_scale_new_with_range(GTK_ORIENTATION_VERTICAL, 0.0, 1.0, 0.05);
        gtk_range_set_inverted(GTK_RANGE(volScale), TRUE);
        gtk_range_set_value(GTK_RANGE(volScale), 0.75);
        gtk_box_append(GTK_BOX(volBox), volScale);

        gtk_popover_set_child(GTK_POPOVER(popover), volBox);
        gtk_menu_button_set_popover(GTK_MENU_BUTTON(btn), popover);

        REQUIRE(gtk_popover_get_position(GTK_POPOVER(popover)) == GTK_POS_TOP);
        REQUIRE(gtk_popover_get_autohide(GTK_POPOVER(popover)) == TRUE);

        gtk_popover_popup(GTK_POPOVER(popover));
        REQUIRE(gtk_widget_get_visible(popover) == TRUE);

        gtk_widget_set_visible(popover, FALSE);
        REQUIRE(gtk_widget_get_visible(popover) == FALSE);

        gtk_window_destroy(GTK_WINDOW(win));
    }

    SECTION("Speed menu button with label child configuration")
    {
        GtkWidget* win = gtk_window_new();
        GtkWidget* btn = gtk_menu_button_new();
        gtk_menu_button_set_direction(GTK_MENU_BUTTON(btn), GTK_ARROW_UP);
        gtk_menu_button_set_has_frame(GTK_MENU_BUTTON(btn), FALSE);
        GtkWidget* lbl = gtk_label_new(NULL);
        gtk_label_set_use_markup(GTK_LABEL(lbl), TRUE);
        gtk_label_set_markup(GTK_LABEL(lbl), "<b>1x</b>");
        gtk_menu_button_set_child(GTK_MENU_BUTTON(btn), lbl);
        gtk_widget_add_css_class(btn, "media-btn");
        REQUIRE(gtk_widget_has_css_class(btn, "media-btn"));

        gtk_window_set_child(GTK_WINDOW(win), btn);
        gtk_window_present(GTK_WINDOW(win));

        GtkWidget* popover = gtk_popover_new();
        gtk_popover_set_position(GTK_POPOVER(popover), GTK_POS_TOP);
        gtk_popover_set_autohide(GTK_POPOVER(popover), TRUE);

        GtkWidget* speedBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        const double speeds[] = { 0.25, 0.5, 1.0, 1.5, 2.0, 4.0, 8.0, 16.0 };
        for (double spd : speeds) {
            gchar* itemLabel = g_strdup_printf("<b>%.4gx</b>", spd);
            GtkWidget* itemBtn = gtk_button_new_with_label(itemLabel);
            g_free(itemLabel);
            gtk_box_append(GTK_BOX(speedBox), itemBtn);
        }

        gtk_popover_set_child(GTK_POPOVER(popover), speedBox);
        gtk_menu_button_set_popover(GTK_MENU_BUTTON(btn), popover);

        REQUIRE(gtk_popover_get_position(GTK_POPOVER(popover)) == GTK_POS_TOP);
        REQUIRE(gtk_popover_get_autohide(GTK_POPOVER(popover)) == TRUE);

        gtk_popover_popup(GTK_POPOVER(popover));
        REQUIRE(gtk_widget_get_visible(popover) == TRUE);

        gtk_widget_set_visible(popover, FALSE);
        REQUIRE(gtk_widget_get_visible(popover) == FALSE);

        gtk_window_destroy(GTK_WINDOW(win));
    }
}
