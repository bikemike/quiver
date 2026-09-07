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

TEST_CASE("QuiverUtils GetSpecialFolderIconName", "[unit][icons][folders]")
{
    const char* home = g_get_home_dir();
    REQUIRE(home != nullptr);

    SECTION("Home directory returns user-home")
    {
        const char* icon = QuiverUtils::GetSpecialFolderIconName(home);
        REQUIRE(icon != nullptr);
        REQUIRE(std::string(icon) == "user-home");

        // With trailing slash
        std::string home_slash = std::string(home) + "/";
        REQUIRE(std::string(QuiverUtils::GetSpecialFolderIconName(home_slash.c_str())) == "user-home");

        // As file:// URI
        gchar* home_uri = g_filename_to_uri(home, nullptr, nullptr);
        REQUIRE(home_uri != nullptr);
        REQUIRE(std::string(QuiverUtils::GetSpecialFolderIconName(home_uri)) == "user-home");
        g_free(home_uri);

        // GFile overload
        GFile* f_home = g_file_new_for_path(home);
        REQUIRE(std::string(QuiverUtils::GetSpecialFolderIconName(f_home)) == "user-home");
        g_object_unref(f_home);
    }

    SECTION("Special XDG user directories return standard themed icon names")
    {
        struct TestFolder {
            GUserDirectory dir_type;
            const char* fallback_name;
            const char* expected_icon;
        };

        const TestFolder test_folders[] = {
            { G_USER_DIRECTORY_DESKTOP, "Desktop", "user-desktop" },
            { G_USER_DIRECTORY_DOCUMENTS, "Documents", "folder-documents" },
            { G_USER_DIRECTORY_DOWNLOAD, "Downloads", "folder-download" },
            { G_USER_DIRECTORY_MUSIC, "Music", "folder-music" },
            { G_USER_DIRECTORY_PICTURES, "Pictures", "folder-pictures" },
            { G_USER_DIRECTORY_PUBLIC_SHARE, "Public", "folder-publicshare" },
            { G_USER_DIRECTORY_TEMPLATES, "Templates", "folder-templates" },
            { G_USER_DIRECTORY_VIDEOS, "Videos", "folder-videos" }
        };

        for (const auto& tf : test_folders)
        {
            const char* sdir = g_get_user_special_dir(tf.dir_type);
            std::string folder_path;
            if (sdir && sdir[0] && 0 != g_strcmp0(sdir, home))
            {
                folder_path = sdir;
            }
            else
            {
                folder_path = std::string(home) + "/" + tf.fallback_name;
            }

            const char* icon = QuiverUtils::GetSpecialFolderIconName(folder_path.c_str());
            REQUIRE(icon != nullptr);
            REQUIRE(std::string(icon) == tf.expected_icon);

            // As file:// URI
            gchar* uri = g_filename_to_uri(folder_path.c_str(), nullptr, nullptr);
            if (uri)
            {
                REQUIRE(std::string(QuiverUtils::GetSpecialFolderIconName(uri)) == tf.expected_icon);
                g_free(uri);
            }
        }
    }

    SECTION("Non-special directories and invalid paths return NULL")
    {
        REQUIRE(QuiverUtils::GetSpecialFolderIconName((const char*)nullptr) == nullptr);
        REQUIRE(QuiverUtils::GetSpecialFolderIconName((GFile*)nullptr) == nullptr);
        REQUIRE(QuiverUtils::GetSpecialFolderIconName("") == nullptr);
        REQUIRE(QuiverUtils::GetSpecialFolderIconName("/non/existent/random/path") == nullptr);
        std::string custom_folder = std::string(home) + "/my_custom_random_folder_xyz";
        REQUIRE(QuiverUtils::GetSpecialFolderIconName(custom_folder.c_str()) == nullptr);
    }

    SECTION("Symbolic icons returned correctly")
    {
        REQUIRE(std::string(QuiverUtils::GetSpecialFolderSymbolicIconName(home)) == "user-home-symbolic");
        std::string pic_path = std::string(home) + "/Pictures";
        REQUIRE(std::string(QuiverUtils::GetSpecialFolderSymbolicIconName(pic_path.c_str())) == "folder-pictures-symbolic");

        GFile* f_home = g_file_new_for_path(home);
        REQUIRE(std::string(QuiverUtils::GetSpecialFolderSymbolicIconName(f_home)) == "user-home-symbolic");
        g_object_unref(f_home);

        REQUIRE(QuiverUtils::GetSpecialFolderSymbolicIconName((const char*)nullptr) == nullptr);
        REQUIRE(QuiverUtils::GetSpecialFolderSymbolicIconName((GFile*)nullptr) == nullptr);
    }
}

