#include <catch2/catch_test_macros.hpp>
#include <gtk/gtk.h>
#include "Browser.h"
#include "Viewer.h"
#include "Preferences.h"
#include "QuiverPrefs.h"
#include "QuiverUtils.h"
#include "test_helpers.h"

TEST_CASE("Toolbar UI Layout and Widget Order", "[unit][gui][toolbar]")
{
    std::string uiPath = QuiverTest_GetDataDir() + "/quiver-toolbar.ui";
    GtkBuilder *builder = gtk_builder_new_from_file(uiPath.c_str());
    REQUIRE(builder != nullptr);

    GtkWidget *toolbar = GTK_WIDGET(gtk_builder_get_object(builder, "QuiverToolbar"));
    REQUIRE(toolbar != nullptr);

    GtkWidget *viewerBox = GTK_WIDGET(gtk_builder_get_object(builder, "viewer_box"));
    REQUIRE(viewerBox != nullptr);

    GtkWidget *browserBox = GTK_WIDGET(gtk_builder_get_object(builder, "browser_box"));
    REQUIRE(browserBox != nullptr);

    GtkWidget *sharedBox = GTK_WIDGET(gtk_builder_get_object(builder, "shared_box"));
    REQUIRE(sharedBox != nullptr);

    SECTION("Viewer mode: browser button is the first item in viewer_box")
    {
        GtkWidget *firstViewerChild = gtk_widget_get_first_child(viewerBox);
        REQUIRE(firstViewerChild != nullptr);

        GtkWidget *buttonBrowser = GTK_WIDGET(gtk_builder_get_object(builder, "button_uimode_browser"));
        REQUIRE(buttonBrowser != nullptr);

        // Verify the first child of viewer_box is indeed button_uimode_browser
        REQUIRE(firstViewerChild == buttonBrowser);

        const gchar *actionName = gtk_actionable_get_action_name(GTK_ACTIONABLE(buttonBrowser));
        REQUIRE(actionName != nullptr);
        REQUIRE(std::string(actionName) == "quiver.UIModeBrowser");
    }

    SECTION("Browser mode: viewer button is the first item in browser_box")
    {
        GtkWidget *firstBrowserChild = gtk_widget_get_first_child(browserBox);
        REQUIRE(firstBrowserChild != nullptr);

        GtkWidget *buttonViewer = GTK_WIDGET(gtk_builder_get_object(builder, "button_uimode_viewer"));
        REQUIRE(buttonViewer != nullptr);

        REQUIRE(firstBrowserChild == buttonViewer);

        const gchar *actionName = gtk_actionable_get_action_name(GTK_ACTIONABLE(buttonViewer));
        REQUIRE(actionName != nullptr);
        REQUIRE(std::string(actionName) == "quiver.UIModeViewer");
    }

    g_object_unref(builder);
}

TEST_CASE("HeaderBar and Toolbar Integration", "[gui][headerbar]")
{
    REQUIRE_DISPLAY();

    GtkWidget *headerBar = gtk_header_bar_new();
    REQUIRE(headerBar != nullptr);
    gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(headerBar), TRUE);

    // Hamburger button
    GtkWidget *menuButton = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(menuButton), "open-menu-symbolic");
    gtk_header_bar_pack_end(GTK_HEADER_BAR(headerBar), menuButton);

    // Preferences button
    GtkWidget *prefButton = gtk_button_new_from_icon_name("preferences-system-symbolic");
    gtk_actionable_set_action_name(GTK_ACTIONABLE(prefButton), "quiver.Preferences");
    gtk_header_bar_pack_end(GTK_HEADER_BAR(headerBar), prefButton);

    // Browser with headerbar
    Browser browser;
    browser.SetToolbar(headerBar);

    // On browser show, thumbnail sizer is packed into the header bar
    browser.Show();

    // On browser hide, thumbnail sizer is removed cleanly
    browser.Hide();

    // Can show again without issue
    browser.Show();
    browser.Hide();
}

TEST_CASE("Background Preference Change Integration", "[gui][preferences][background]")
{
    REQUIRE_DISPLAY();

    PreferencesPtr prefs = Preferences::GetInstance();
    REQUIRE(prefs != nullptr);

    // Instantiate Browser and Viewer which listen to preference changes
    Browser browser;
    Viewer viewer;

    // 1. Switch from theme color (default true) to custom colors
    prefs->SetBoolean(QUIVER_PREFS_APP, QUIVER_PREFS_APP_USE_THEME_COLOR, false);

    // 2. Change browser background color (iconview)
    prefs->SetString(QUIVER_PREFS_APP, QUIVER_PREFS_APP_BG_ICONVIEW, "#123456");

    // 3. Change viewer background color (imageview)
    prefs->SetString(QUIVER_PREFS_APP, QUIVER_PREFS_APP_BG_IMAGEVIEW, "#abcdef");

    // 4. Change again (multiple changes as in color picker)
    prefs->SetString(QUIVER_PREFS_APP, QUIVER_PREFS_APP_BG_ICONVIEW, "#234567");
    prefs->SetString(QUIVER_PREFS_APP, QUIVER_PREFS_APP_BG_IMAGEVIEW, "#bcdef0");

    // 5. Switch back to theme colors (NULL color path, previously crashed)
    prefs->SetBoolean(QUIVER_PREFS_APP, QUIVER_PREFS_APP_USE_THEME_COLOR, true);

    // 6. Toggle again
    prefs->SetBoolean(QUIVER_PREFS_APP, QUIVER_PREFS_APP_USE_THEME_COLOR, false);
    prefs->SetBoolean(QUIVER_PREFS_APP, QUIVER_PREFS_APP_USE_THEME_COLOR, true);
}

