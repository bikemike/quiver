#include <catch2/catch_test_macros.hpp>
#include <gtk/gtk.h>
#include <functional>
#include <set>
#include <cstring>
#include <string>
#include <vector>
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

    SECTION("Viewer box has no zoom/rotate/trash controls")
    {
        // Viewer box must now only contain the browser mode button, separator,
        // Previous and Next.  Zoom / rotate / trash were moved into the menu
        // and the context menu.
        const char* names[] = {
            "quiver.ZoomIn", "quiver.ZoomOut", "quiver.Zoom100",
            "quiver.ZoomFit", "quiver.RotateCW", "quiver.RotateCCW",
            "quiver.ViewerTrash"
        };
        for (GtkWidget *child = gtk_widget_get_first_child(viewerBox); child;
             child = gtk_widget_get_next_sibling(child))
        {
            if (!GTK_IS_ACTIONABLE(child)) continue;
            const gchar *actionName = gtk_actionable_get_action_name(GTK_ACTIONABLE(child));
            if (!actionName) continue;
            for (const char *n : names)
                REQUIRE(std::string(actionName) != n);
        }
    }

    SECTION("Shared box has no slideshow control")
    {
        for (GtkWidget *child = gtk_widget_get_first_child(sharedBox); child;
             child = gtk_widget_get_next_sibling(child))
        {
            if (!GTK_IS_ACTIONABLE(child)) continue;
            const gchar *actionName = gtk_actionable_get_action_name(GTK_ACTIONABLE(child));
            REQUIRE(std::string(actionName ? actionName : "") != "quiver.SlideShow");
        }
    }

    SECTION("Shared box is the first child (full-screen on the far left)")
    {
        GtkWidget *firstToolbarChild = gtk_widget_get_first_child(toolbar);
        REQUIRE(firstToolbarChild == sharedBox);
    }

    SECTION("Browser box has no folder-tree / open-folder controls")
    {
        for (GtkWidget *child = gtk_widget_get_first_child(browserBox); child;
             child = gtk_widget_get_next_sibling(child))
        {
            if (!GTK_IS_ACTIONABLE(child)) continue;
            const gchar *actionName = gtk_actionable_get_action_name(GTK_ACTIONABLE(child));
            if (!actionName) continue;
            REQUIRE(std::string(actionName) != "quiver.FileOpenFolder");
            REQUIRE(std::string(actionName) != "quiver.BrowserViewSidebar");
        }
    }

    SECTION("Browser box has no trash control")
    {
        for (GtkWidget *child = gtk_widget_get_first_child(browserBox); child;
             child = gtk_widget_get_next_sibling(child))
        {
            if (!GTK_IS_ACTIONABLE(child)) continue;
            const gchar *actionName = gtk_actionable_get_action_name(GTK_ACTIONABLE(child));
            REQUIRE(std::string(actionName ? actionName : "") != "quiver.BrowserTrash");
        }
    }

    SECTION("All toolbar icons are symbolic variants")
    {
        for (GtkWidget *child = gtk_widget_get_first_child(toolbar); child;
             child = gtk_widget_get_next_sibling(child))
        {
            if (!GTK_IS_BOX(child)) continue;
            for (GtkWidget *inner = gtk_widget_get_first_child(child); inner;
                 inner = gtk_widget_get_next_sibling(inner))
            {
                if (!GTK_IS_BUTTON(inner) && !GTK_IS_TOGGLE_BUTTON(inner)) continue;
                GtkWidget *img = gtk_widget_get_first_child(inner);
                if (img && GTK_IS_IMAGE(img))
                {
                    const gchar *icon = gtk_image_get_icon_name(GTK_IMAGE(img));
                    REQUIRE(icon != nullptr);
                    REQUIRE(g_str_has_suffix(icon, "-symbolic"));
                }
            }
        }
    }

    g_object_unref(builder);
}

TEST_CASE("Hamburger menu structure has no File/Edit/Help top levels and About last", "[unit][gui][menu]")
{
    std::string uiPath = QuiverTest_GetDataDir() + "/quiver-menus.ui";
    GtkBuilder *builder = gtk_builder_new_from_file(uiPath.c_str());
    REQUIRE(builder != nullptr);

    GMenuModel *model = G_MENU_MODEL(gtk_builder_get_object(builder, "app_menu"));
    REQUIRE(model != nullptr);
    const int n = g_menu_model_get_n_items(model);
    REQUIRE(n > 0);

    // The top-level app_menu is a flat series of sections and submenus.
    // Collect all labels recursively (sections + their items) and all top-level
    // labels, then assert there is no File/Edit/Help and About is the final
    // leaf item in the model.
    std::vector<std::string> allLabels;
    std::vector<std::string> topLabels;
    std::function<void(GMenuModel*, bool)> walk = [&](GMenuModel *mm, bool top) {
        const int c = g_menu_model_get_n_items(mm);
        for (int i = 0; i < c; ++i)
        {
            g_autoptr(GMenuModel) sec = g_menu_model_get_item_link(mm, i, G_MENU_LINK_SECTION);
            g_autoptr(GMenuModel) sub = g_menu_model_get_item_link(mm, i, G_MENU_LINK_SUBMENU);
            g_autofree gchar *label = NULL;
            g_menu_model_get_item_attribute(mm, i, "label", "s", &label);
            if (label)
            {
                allLabels.push_back(label);
                if (top) topLabels.push_back(label);
            }
            if (sec) walk(sec, false);
            if (sub) walk(sub, false);
        }
    };
    walk(model, true);

    for (const auto &s : allLabels)
    {
        REQUIRE(s != "_File");
        REQUIRE(s != "File");
        REQUIRE(s != "_Edit");
        REQUIRE(s != "Edit");
        REQUIRE(s != "_Help");
        REQUIRE(s != "Help");
    }

    // About must be the final entry in the flat menu model.
    REQUIRE(!allLabels.empty());
    REQUIRE(allLabels.back() == "_About");

    g_object_unref(builder);
}

/* Mirrors QuiverImpl::FilterMenuModel(): clones a menu keeping only the items
 * whose "location" attribute names one of the active contexts, always keeping
 * the live bookmarks/tools placeholders, and dropping submenus/sections whose
 * filtered contents become empty.  Used to assert the per-context hamburger
 * menu shape without constructing the whole application. */
static GMenu *FilterMenuModelTest(GMenuModel *model, const std::set<std::string> &contexts)
{
    GMenu *dst = g_menu_new();
    const int count = g_menu_model_get_n_items(model);
    for (int i = 0; i < count; ++i)
    {
        g_autoptr(GMenuModel) section = g_menu_model_get_item_link(model, i, G_MENU_LINK_SECTION);
        g_autoptr(GMenuModel) submenu = g_menu_model_get_item_link(model, i, G_MENU_LINK_SUBMENU);
        g_autofree gchar *location = NULL;
        g_menu_model_get_item_attribute(model, i, "location", "s", &location);

        bool keep = true;
        if (location)
        {
            if (strcmp(location, "bookmarks") != 0 && strcmp(location, "tools") != 0 &&
                contexts.find(location) == contexts.end())
                keep = false;
        }
        if (!keep) continue;

        if (section)
        {
            GMenu *ns = FilterMenuModelTest(section, contexts);
            if (g_menu_model_get_n_items(G_MENU_MODEL(ns)) > 0)
                g_menu_append_section(dst, NULL, G_MENU_MODEL(ns));
            g_object_unref(ns);
            continue;
        }

        GMenuItem *item = g_menu_item_new_from_model(model, i);
        if (submenu)
        {
            GMenu *ns = FilterMenuModelTest(submenu, contexts);
            if (g_menu_model_get_n_items(G_MENU_MODEL(ns)) == 0)
            {
                g_object_unref(item);
                g_object_unref(ns);
                continue;
            }
            g_menu_item_set_submenu(item, G_MENU_MODEL(ns));
            g_object_unref(ns);
        }
        g_menu_append_item(dst, item);
        g_object_unref(item);
    }
    return dst;
}

static int MenuCountAttribute(GMenuModel *m, const char *attr, const char *value)
{
    int found = 0;
    for (int i = 0; i < g_menu_model_get_n_items(m); ++i)
    {
        g_autoptr(GMenuModel) sec = g_menu_model_get_item_link(m, i, G_MENU_LINK_SECTION);
        g_autoptr(GMenuModel) sub = g_menu_model_get_item_link(m, i, G_MENU_LINK_SUBMENU);
        g_autofree gchar *v = NULL;
        g_menu_model_get_item_attribute(m, i, attr, "s", &v);
        if (v && strcmp(v, value) == 0) ++found;
        if (sec) found += MenuCountAttribute(sec, attr, value);
        if (sub) found += MenuCountAttribute(sub, attr, value);
    }
    return found;
}

static bool MenuHasAction(GMenuModel *m, const char *action)
{
    return MenuCountAttribute(m, "action", action) > 0;
}

static bool MenuHasCustom(GMenuModel *m, const char *custom)
{
    return MenuCountAttribute(m, "custom", custom) > 0;
}

static bool MenuHasLabel(GMenuModel *m, const char *label)
{
    return MenuCountAttribute(m, "label", label) > 0;
}

/* Last leaf label of the (filtered) menu model must be About. */
static bool MenuAboutLast(GMenuModel *m)
{
    std::string last;
    std::function<void(GMenuModel*)> walk = [&](GMenuModel *mm) {
        for (int i = 0; i < g_menu_model_get_n_items(mm); ++i)
        {
            g_autoptr(GMenuModel) sec = g_menu_model_get_item_link(mm, i, G_MENU_LINK_SECTION);
            g_autoptr(GMenuModel) sub = g_menu_model_get_item_link(mm, i, G_MENU_LINK_SUBMENU);
            g_autofree gchar *label = NULL;
            g_menu_model_get_item_attribute(mm, i, "label", "s", &label);
            if (label) last = label;
            if (sec) walk(sec);
            if (sub) walk(sub);
        }
    };
    walk(m);
    return last == "_About";
}

/* True if both named custom-row placeholders occur within a single shared
 * GMenu section.  They must share one section so GtkPopoverMenu draws no
 * separator between the Zoom and Rotate rows. */
static bool MenuSameSection(GMenuModel *m, const char *a, const char *b)
{
    for (int i = 0; i < g_menu_model_get_n_items(m); ++i)
    {
        g_autoptr(GMenuModel) sec = g_menu_model_get_item_link(m, i, G_MENU_LINK_SECTION);
        if (!sec) continue;
        bool hasA = MenuCountAttribute(sec, "custom", a) > 0;
        bool hasB = MenuCountAttribute(sec, "custom", b) > 0;
        if (hasA && hasB) return true;
    }
    return false;
}

/* True if 'label' names an item that sits directly inside a top-level section
 * of 'm' (rather than being nested in a submenu). */
static bool MenuInTopLevelSection(GMenuModel *m, const char *label)
{
    for (int i = 0; i < g_menu_model_get_n_items(m); ++i)
    {
        g_autoptr(GMenuModel) sec = g_menu_model_get_item_link(m, i, G_MENU_LINK_SECTION);
        if (sec && MenuCountAttribute(sec, "label", label) > 0)
            return true;
    }
    return false;
}

TEST_CASE("Hamburger menu per-context structure", "[unit][gui][menu]")
{
    std::string uiPath = QuiverTest_GetDataDir() + "/quiver-menus.ui";
    GtkBuilder *builder = gtk_builder_new_from_file(uiPath.c_str());
    REQUIRE(builder != nullptr);

    GMenuModel *model = G_MENU_MODEL(gtk_builder_get_object(builder, "app_menu"));
    REQUIRE(model != nullptr);

    /* The pristine model must reference both custom row placeholders. */
    REQUIRE(MenuHasCustom(model, "zoom-row"));
    REQUIRE(MenuHasCustom(model, "rotate-row"));

    /* The Zoom and Rotate rows share one section so GtkPopoverMenu draws no
     * separator between them. */
    REQUIRE(MenuSameSection(model, "zoom-row", "rotate-row"));

    /* Bookmarks / Tools / About each sit in their own top-level section so
     * GtkPopoverMenu separates them (and thus draws a separator after Tools). */
    REQUIRE(MenuInTopLevelSection(model, "_Bookmarks"));
    REQUIRE(MenuInTopLevelSection(model, "_Tools"));
    REQUIRE(MenuInTopLevelSection(model, "_About"));

    SECTION("Browser context")
    {
        GMenu *m = FilterMenuModelTest(model, {"browser"});
        REQUIRE(g_menu_model_get_n_items(G_MENU_MODEL(m)) > 0);

        /* Only the non-current mode is offered. */
        REQUIRE(MenuHasAction(G_MENU_MODEL(m), "quiver.UIModeViewer"));
        REQUIRE(!MenuHasAction(G_MENU_MODEL(m), "quiver.UIModeBrowser"));
        REQUIRE(MenuCountAttribute(G_MENU_MODEL(m), "action", "quiver.UIModeViewer") == 1);
        REQUIRE(MenuCountAttribute(G_MENU_MODEL(m), "action", "quiver.UIModeBrowser") == 0);

        /* No custom rows in browser mode. */
        REQUIRE(!MenuHasCustom(G_MENU_MODEL(m), "zoom-row"));
        REQUIRE(!MenuHasCustom(G_MENU_MODEL(m), "rotate-row"));

        /* View submenu carries the browser-only toggles. */
        REQUIRE(MenuHasAction(G_MENU_MODEL(m), "quiver.BrowserViewSidebar"));
        REQUIRE(MenuHasAction(G_MENU_MODEL(m), "quiver.BrowserViewPreview"));
        REQUIRE(!MenuHasAction(G_MENU_MODEL(m), "quiver.ViewFilmStrip"));

        /* Arrange Items is browser-only. */
        REQUIRE(MenuHasAction(G_MENU_MODEL(m), "quiver.SortByName"));

        /* Navigation / open / zoom menu items are gone in every context. */
        REQUIRE(!MenuHasAction(G_MENU_MODEL(m), "quiver.ImageFirst"));
        REQUIRE(!MenuHasAction(G_MENU_MODEL(m), "quiver.ImagePrevious"));
        REQUIRE(!MenuHasAction(G_MENU_MODEL(m), "quiver.ImageNext"));
        REQUIRE(!MenuHasAction(G_MENU_MODEL(m), "quiver.ImageLast"));
        REQUIRE(!MenuHasAction(G_MENU_MODEL(m), "quiver.BrowserHistoryBack"));
        REQUIRE(!MenuHasAction(G_MENU_MODEL(m), "quiver.GoFolderParent"));
        REQUIRE(!MenuHasAction(G_MENU_MODEL(m), "quiver.ZoomIn"));
        REQUIRE(!MenuHasAction(G_MENU_MODEL(m), "quiver.ZoomOut"));

        /* Video menu was removed entirely. */
        REQUIRE(!MenuHasAction(G_MENU_MODEL(m), "quiver.VideoPlay"));

        /* Placeholders and About survive. */
        REQUIRE(MenuHasLabel(G_MENU_MODEL(m), "_Bookmarks"));
        REQUIRE(MenuHasLabel(G_MENU_MODEL(m), "_Tools"));
        REQUIRE(MenuAboutLast(G_MENU_MODEL(m)));

        g_object_unref(m);
    }

    SECTION("Viewer image context")
    {
        GMenu *m = FilterMenuModelTest(model, {"viewer", "viewer-image"});
        REQUIRE(g_menu_model_get_n_items(G_MENU_MODEL(m)) > 0);

        /* Only the non-current mode is offered. */
        REQUIRE(MenuHasAction(G_MENU_MODEL(m), "quiver.UIModeBrowser"));
        REQUIRE(!MenuHasAction(G_MENU_MODEL(m), "quiver.UIModeViewer"));
        REQUIRE(MenuCountAttribute(G_MENU_MODEL(m), "action", "quiver.UIModeBrowser") == 1);

        /* Both inline rows are anchored for still images. */
        REQUIRE(MenuHasCustom(G_MENU_MODEL(m), "zoom-row"));
        REQUIRE(MenuHasCustom(G_MENU_MODEL(m), "rotate-row"));

        /* View submenu carries the viewer-only toggle. */
        REQUIRE(MenuHasAction(G_MENU_MODEL(m), "quiver.ViewFilmStrip"));
        REQUIRE(!MenuHasAction(G_MENU_MODEL(m), "quiver.BrowserViewSidebar"));
        REQUIRE(!MenuHasAction(G_MENU_MODEL(m), "quiver.BrowserViewPreview"));

        /* Arrange Items is browser-only. */
        REQUIRE(!MenuHasAction(G_MENU_MODEL(m), "quiver.SortByName"));

        /* Rotation is provided by the rotate row widget, not menu items. */
        REQUIRE(!MenuHasAction(G_MENU_MODEL(m), "quiver.RotateCW"));
        REQUIRE(!MenuHasAction(G_MENU_MODEL(m), "quiver.FlipH"));

        REQUIRE(!MenuHasAction(G_MENU_MODEL(m), "quiver.VideoPlay"));
        REQUIRE(MenuHasAction(G_MENU_MODEL(m), "quiver.FullScreen"));
        REQUIRE(MenuHasAction(G_MENU_MODEL(m), "quiver.SlideShow"));
        REQUIRE(MenuHasLabel(G_MENU_MODEL(m), "_Bookmarks"));
        REQUIRE(MenuHasLabel(G_MENU_MODEL(m), "_Tools"));
        REQUIRE(MenuAboutLast(G_MENU_MODEL(m)));

        g_object_unref(m);
    }

    SECTION("Viewer video context")
    {
        GMenu *m = FilterMenuModelTest(model, {"viewer", "viewer-video"});
        REQUIRE(g_menu_model_get_n_items(G_MENU_MODEL(m)) > 0);

        REQUIRE(MenuHasAction(G_MENU_MODEL(m), "quiver.UIModeBrowser"));
        REQUIRE(!MenuHasAction(G_MENU_MODEL(m), "quiver.UIModeViewer"));

        /* Zoom row is shared with images; rotate row is image-only. */
        REQUIRE(MenuHasCustom(G_MENU_MODEL(m), "zoom-row"));
        REQUIRE(!MenuHasCustom(G_MENU_MODEL(m), "rotate-row"));

        /* No video submenu: playback lives in the player controls. */
        REQUIRE(!MenuHasAction(G_MENU_MODEL(m), "quiver.VideoPlay"));
        REQUIRE(!MenuHasAction(G_MENU_MODEL(m), "quiver.VideoSpeed10"));
        REQUIRE(!MenuHasAction(G_MENU_MODEL(m), "quiver.VideoSnapshot"));

        REQUIRE(MenuHasAction(G_MENU_MODEL(m), "quiver.ViewFilmStrip"));
        REQUIRE(MenuAboutLast(G_MENU_MODEL(m)));

        g_object_unref(m);
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

