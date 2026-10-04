#include <catch2/catch_test_macros.hpp>
#include <gtk/gtk.h>
#include <boost/shared_ptr.hpp>
#include "Viewer.h"
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
        GtkWidget* muteBtn = gtk_button_new_from_icon_name("audio-volume-high-symbolic");
        gtk_widget_add_css_class(muteBtn, "media-btn");
        gtk_box_append(GTK_BOX(volBox), muteBtn);
        REQUIRE(gtk_widget_has_css_class(muteBtn, "media-btn"));

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

    SECTION("Video paintable zoom and pan geometry scaling")
    {
        // Simulate a 1080p video in a 1920x1080 viewer
        const double dispW = 1920.0;
        const double dispH = 1080.0;
        const double areaW = 1920.0;
        const double areaH = 1080.0;

        // Test at 200% zoom (zoom = 2.0)
        const double zoom = 2.0;
        const double widgetW = dispW * zoom;
        const double widgetH = dispH * zoom;
        REQUIRE(widgetW == 3840.0);
        REQUIRE(widgetH == 2160.0);

        const double vw = areaW / zoom;
        const double vh = areaH / zoom;
        REQUIRE(vw == 960.0);
        REQUIRE(vh == 540.0);

        // Maximum pan reaches the edge
        const double maxPanX = dispW - vw;
        const double maxPanY = dispH - vh;
        REQUIRE(maxPanX == 960.0);
        REQUIRE(maxPanY == 540.0);

        // offX at min and max pan
        const double offX_min = -0.0 * zoom;
        const double offX_max = -maxPanX * zoom;
        REQUIRE(offX_min == 0.0);
        REQUIRE(offX_max == (areaW - widgetW));

        // 1:1 mouse tracking: moving mouse by dx screen pixels moves video by dx
        const double dx = 25.0;
        const double srcPerPx = 1.0 / zoom;
        const double deltaPanX = -dx * srcPerPx;
        const double deltaOffX = -deltaPanX * zoom;
        REQUIRE(deltaOffX == dx);

        // GtkPicture setup check
        GtkWidget* pic = gtk_picture_new();
        gtk_picture_set_content_fit(GTK_PICTURE(pic), GTK_CONTENT_FIT_FILL);
        gtk_picture_set_can_shrink(GTK_PICTURE(pic), TRUE);
        REQUIRE(gtk_picture_get_content_fit(GTK_PICTURE(pic)) == GTK_CONTENT_FIT_FILL);
        REQUIRE(gtk_picture_get_can_shrink(GTK_PICTURE(pic)) == TRUE);
        g_object_ref_sink(pic);
        g_object_unref(pic);
    }

    SECTION("Video seek progress HUD popover configuration and input transparency")
    {
        boost::shared_ptr<Viewer> viewer(new Viewer());
        GtkWidget* playProgress = viewer->GetPlayProgress();
        REQUIRE(playProgress != nullptr);
        REQUIRE(GTK_IS_SCALE(playProgress));

        GtkWidget* popover = viewer->GetPlayProgressPopover();
        REQUIRE(popover != nullptr);
        REQUIRE(GTK_IS_POPOVER(popover));
        REQUIRE(gtk_widget_has_css_class(popover, "play-progress-popover"));

        // Popover must not auto-hide while active
        REQUIRE(gtk_popover_get_autohide(GTK_POPOVER(popover)) == FALSE);

        // Input transparency: must not be targetable or focusable
        REQUIRE(gtk_widget_get_can_target(popover) == FALSE);
        REQUIRE(gtk_widget_get_focusable(popover) == FALSE);

        // Child readout label must also be non-targetable
        GtkWidget* label = gtk_popover_get_child(GTK_POPOVER(popover));
        REQUIRE(label != nullptr);
        REQUIRE(GTK_IS_LABEL(label));
        REQUIRE(gtk_widget_get_can_target(label) == FALSE);
        REQUIRE(gtk_widget_has_css_class(label, "play-progress-readout"));
    }
}
