#include <catch2/catch_test_macros.hpp>
#include "QuiverUtils.h"
#include "test_helpers.h"
#if HAVE_GDK_PIXBUF
#include <gdk-pixbuf/gdk-pixbuf.h>
#endif
#include <gtk/gtk.h>
#include <glib/gstdio.h>

static GdkTexture* CreateTestTex(int w, int h)
{
    gsize size = (gsize)w * h * 4;
    guint32* data = (guint32*)g_malloc0(size);
    GBytes* bytes = g_bytes_new_take(data, size);
    GdkTexture* tex = gdk_memory_texture_new(w, h, GDK_MEMORY_DEFAULT, bytes, (gsize)w * 4);
    g_bytes_unref(bytes);
    return tex;
}

TEST_CASE("QuiverUtils Texture EXIF Reorientation", "[unit][texture][fast]")
{
    GdkTexture* orig = CreateTestTex(100, 50);
    REQUIRE(orig != nullptr);

    SECTION("Orientation 1: No change")
    {
        GdkTexture* mod = QuiverUtils::TextureExifReorientate(orig, 1);
        REQUIRE(mod == orig);
        g_object_unref(mod);
    }

    SECTION("Orientation 2: Horizontal flip preserves dimensions")
    {
        GdkTexture* mod = QuiverUtils::TextureExifReorientate(orig, 2);
        REQUIRE(mod != nullptr);
        REQUIRE(gdk_texture_get_width(mod) == 100);
        REQUIRE(gdk_texture_get_height(mod) == 50);
        g_object_unref(mod);
    }

    SECTION("Orientation 3: 180 rotation preserves dimensions")
    {
        GdkTexture* mod = QuiverUtils::TextureExifReorientate(orig, 3);
        REQUIRE(mod != nullptr);
        REQUIRE(gdk_texture_get_width(mod) == 100);
        REQUIRE(gdk_texture_get_height(mod) == 50);
        g_object_unref(mod);
    }

    SECTION("Orientation 6: 90 CW rotation swaps width and height")
    {
        GdkTexture* mod = QuiverUtils::TextureExifReorientate(orig, 6);
        REQUIRE(mod != nullptr);
        REQUIRE(gdk_texture_get_width(mod) == 50);
        REQUIRE(gdk_texture_get_height(mod) == 100);
        g_object_unref(mod);
    }

    g_object_unref(orig);
}

#if HAVE_GDK_PIXBUF
TEST_CASE("QuiverUtils Legacy Pixbuf EXIF Reorientation", "[unit][pixbuf][fast]")
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
#endif

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

TEST_CASE("QuiverUtils Rename Basename Selection and Extension Preservation", "[unit][rename][fast]")
{
    SECTION("GetBasenameCharLength computes correct offset")
    {
        REQUIRE(QuiverUtils::GetBasenameCharLength("photo.jpg") == 5);
        REQUIRE(QuiverUtils::GetBasenameCharLength("archive.tar.gz") == 11);
        REQUIRE(QuiverUtils::GetBasenameCharLength("Makefile") == -1);
        REQUIRE(QuiverUtils::GetBasenameCharLength(".bashrc") == -1);
        REQUIRE(QuiverUtils::GetBasenameCharLength(".profile.bak") == 8);
        REQUIRE(QuiverUtils::GetBasenameCharLength("") == -1);
        REQUIRE(QuiverUtils::GetBasenameCharLength(nullptr) == -1);
    }

    SECTION("ResolveRenameString appends original extension if omitted")
    {
        // Extension omitted -> append original extension
        REQUIRE(QuiverUtils::ResolveRenameString("photo.jpg", "vacation") == "vacation.jpg");
        // Trailing dot entered -> append original extension (without double dot)
        REQUIRE(QuiverUtils::ResolveRenameString("photo.jpg", "vacation.") == "vacation.jpg");
        // Explicit extension entered -> preserve entered extension
        REQUIRE(QuiverUtils::ResolveRenameString("photo.jpg", "vacation.png") == "vacation.png");
        REQUIRE(QuiverUtils::ResolveRenameString("photo.jpg", "vacation.jpeg") == "vacation.jpeg");
        // Multi-dot filename
        REQUIRE(QuiverUtils::ResolveRenameString("backup.tar.gz", "archive") == "archive.gz");
        REQUIRE(QuiverUtils::ResolveRenameString("backup.tar.gz", "archive.tar") == "archive.tar");
        // Original file had no extension -> keep entered name as is
        REQUIRE(QuiverUtils::ResolveRenameString("Makefile", "GNUmakefile") == "GNUmakefile");
        REQUIRE(QuiverUtils::ResolveRenameString("Makefile", "Makefile.old") == "Makefile.old");
        // Hidden file with no extension
        REQUIRE(QuiverUtils::ResolveRenameString(".bashrc", "my_bashrc") == "my_bashrc");
        // Hidden file with extension
        REQUIRE(QuiverUtils::ResolveRenameString(".profile.bak", "new_profile") == "new_profile.bak");
        REQUIRE(QuiverUtils::ResolveRenameString(".profile.bak", "new_profile.txt") == "new_profile.txt");
    }

    SECTION("PromptForString opens with base name selected and cancels on Escape")
    {
        g_timeout_add(50, +[](gpointer) -> gboolean {
            GListModel *toplevels = gtk_window_get_toplevels();
            guint n = g_list_model_get_n_items(toplevels);
            for (guint i = 0; i < n; i++)
            {
                GtkWindow *win = GTK_WINDOW(g_list_model_get_item(toplevels, i));
                if (win != nullptr && g_strcmp0(gtk_window_get_title(win), "TestPromptEscape") == 0)
                {
                    // Find entry child in dlgBox
                    GtkWidget *child = gtk_window_get_child(win);
                    if (child != nullptr && GTK_IS_BOX(child))
                    {
                        GtkWidget *first = gtk_widget_get_first_child(child);
                        GtkWidget *entry_widget = first ? gtk_widget_get_next_sibling(first) : nullptr;
                        if (entry_widget && GTK_IS_ENTRY(entry_widget))
                        {
                            int start = -1, end = -1;
                            gboolean has_sel = gtk_editable_get_selection_bounds(
                                GTK_EDITABLE(entry_widget), &start, &end);
                            // "summer_vacation" has length 15
                            CHECK(has_sel == TRUE);
                            CHECK(start == 0);
                            CHECK(end == 15);
                        }
                    }

                    // Test Escape key via key controller
                    GListModel *controllers = gtk_widget_observe_controllers(GTK_WIDGET(win));
                    guint nc = g_list_model_get_n_items(controllers);
                    bool escape_emitted = false;
                    for (guint ci = 0; ci < nc; ci++)
                    {
                        GObject *ctrl = G_OBJECT(g_list_model_get_item(controllers, ci));
                        if (GTK_IS_EVENT_CONTROLLER_KEY(ctrl))
                        {
                            gboolean handled = FALSE;
                            g_signal_emit_by_name(ctrl, "key-pressed", GDK_KEY_Escape, 0, (GdkModifierType)0, &handled);
                            CHECK(handled == TRUE);
                            escape_emitted = true;
                            g_object_unref(ctrl);
                            break;
                        }
                        g_object_unref(ctrl);
                    }
                    g_object_unref(controllers);
                    CHECK(escape_emitted == true);
                    g_object_unref(win);
                    break;
                }
                g_object_unref(win);
            }
            return G_SOURCE_REMOVE;
        }, nullptr);

        char *result = QuiverUtils::PromptForString("TestPromptEscape", "Enter name:", "summer_vacation.jpg");
        REQUIRE(result == nullptr);
    }

    SECTION("PromptForString cancels on window close")
    {
        g_timeout_add(50, +[](gpointer) -> gboolean {
            GListModel *toplevels = gtk_window_get_toplevels();
            guint n = g_list_model_get_n_items(toplevels);
            for (guint i = 0; i < n; i++)
            {
                GtkWindow *win = GTK_WINDOW(g_list_model_get_item(toplevels, i));
                if (win != nullptr && g_strcmp0(gtk_window_get_title(win), "TestPromptClose") == 0)
                {
                    gtk_window_close(win);
                    g_object_unref(win);
                    break;
                }
                g_object_unref(win);
            }
            return G_SOURCE_REMOVE;
        }, nullptr);

        char *result = QuiverUtils::PromptForString("TestPromptClose", "Enter name:", "photo.jpg");
        REQUIRE(result == nullptr);
    }

    SECTION("PromptForString accepts with custom button label")
    {
        g_timeout_add(50, +[](gpointer) -> gboolean {
            GListModel *toplevels = gtk_window_get_toplevels();
            guint n = g_list_model_get_n_items(toplevels);
            for (guint i = 0; i < n; i++)
            {
                GtkWindow *win = GTK_WINDOW(g_list_model_get_item(toplevels, i));
                if (win != nullptr && g_strcmp0(gtk_window_get_title(win), "TestPromptCreate") == 0)
                {
                    GtkWidget *child = gtk_window_get_child(win);
                    if (child != nullptr && GTK_IS_BOX(child))
                    {
                        for (GtkWidget *b = gtk_widget_get_first_child(child); b != nullptr; b = gtk_widget_get_next_sibling(b))
                        {
                            if (GTK_IS_BOX(b))
                            {
                                for (GtkWidget *btn = gtk_widget_get_first_child(b); btn != nullptr; btn = gtk_widget_get_next_sibling(btn))
                                {
                                    if (GTK_IS_BUTTON(btn) && g_strcmp0(gtk_button_get_label(GTK_BUTTON(btn)), "Create") == 0)
                                    {
                                        g_signal_emit_by_name(btn, "clicked");
                                        g_object_unref(win);
                                        return G_SOURCE_REMOVE;
                                    }
                                }
                            }
                        }
                    }
                }
                g_object_unref(win);
            }
            return G_SOURCE_REMOVE;
        }, nullptr);

        char *result = QuiverUtils::PromptForString("TestPromptCreate", "Folder name:", "New Folder", "Create");
        REQUIRE(result != nullptr);
        CHECK(std::string(result) == "New Folder");
        g_free(result);
    }
}

TEST_CASE("QuiverUtils IsDirectoryURI and GetUniqueFolderName", "[unit][fileops][fast]")
{
    gchar *tmp_dir = g_dir_make_tmp("quiver_test_dir_XXXXXX", NULL);
    REQUIRE(tmp_dir != nullptr);

    GFile *parent_file = g_file_new_for_path(tmp_dir);
    gchar *parent_uri = g_file_get_uri(parent_file);
    REQUIRE(parent_uri != nullptr);

    SECTION("IsDirectoryURI validates directories")
    {
        CHECK(QuiverUtils::IsDirectoryURI(parent_uri) == true);
        CHECK(QuiverUtils::IsDirectoryURI(tmp_dir) == true);
        CHECK(QuiverUtils::IsDirectoryURI("trash:///") == false);
        CHECK(QuiverUtils::IsDirectoryURI(nullptr) == false);

        gchar *nonexistent = g_build_filename(tmp_dir, "nonexistent_dir", NULL);
        gchar *nonexistent_uri = g_filename_to_uri(nonexistent, NULL, NULL);
        CHECK(QuiverUtils::IsDirectoryURI(nonexistent_uri) == false);
        g_free(nonexistent);
        g_free(nonexistent_uri);

        // Regular file is not a directory
        gchar *file_path = g_build_filename(tmp_dir, "test.txt", NULL);
        GError *err = NULL;
        g_file_set_contents(file_path, "hello", -1, &err);
        gchar *file_uri = g_filename_to_uri(file_path, NULL, NULL);
        CHECK(QuiverUtils::IsDirectoryURI(file_uri) == false);
        g_free(file_path);
        g_free(file_uri);
    }

    SECTION("FileFromURIOrPath supports both plain paths and file:// URIs")
    {
        GFile *f_path = QuiverUtils::FileFromURIOrPath(tmp_dir);
        REQUIRE(f_path != nullptr);
        char *uri_from_path = g_file_get_uri(f_path);
        CHECK(g_strcmp0(uri_from_path, parent_uri) == 0);
        g_free(uri_from_path);

        GFile *f_uri = QuiverUtils::FileFromURIOrPath(parent_uri);
        REQUIRE(f_uri != nullptr);
        char *uri_from_uri = g_file_get_uri(f_uri);
        CHECK(g_strcmp0(uri_from_uri, parent_uri) == 0);
        g_free(uri_from_uri);

        CHECK(QuiverUtils::NormalizeURI(tmp_dir) == parent_uri);
        CHECK(QuiverUtils::NormalizeURI(parent_uri) == parent_uri);

        // Making a directory with parent created from a plain path must succeed without "Operation not supported"
        GFile *sub = g_file_get_child(f_path, "sub_from_plain_path");
        GError *mk_err = nullptr;
        gboolean ok = g_file_make_directory(sub, NULL, &mk_err);
        CHECK(ok == TRUE);
        CHECK(mk_err == nullptr);
        g_file_delete(sub, NULL, NULL);
        g_object_unref(sub);

        g_object_unref(f_path);
        g_object_unref(f_uri);
    }

    SECTION("GetUniqueFolderName generates distinct names")
    {
        std::string name1 = QuiverUtils::GetUniqueFolderName(parent_file, "New Folder");
        CHECK(name1 == "New Folder");

        GFile *child1 = g_file_get_child(parent_file, "New Folder");
        g_file_make_directory(child1, NULL, NULL);

        std::string name2 = QuiverUtils::GetUniqueFolderName(parent_file, "New Folder");
        CHECK(name2 == "New Folder 2");

        GFile *child2 = g_file_get_child(parent_file, "New Folder 2");
        g_file_make_directory(child2, NULL, NULL);

        std::string name3 = QuiverUtils::GetUniqueFolderName(parent_file, "New Folder");
        CHECK(name3 == "New Folder 3");

        g_file_delete(child2, NULL, NULL);
        g_file_delete(child1, NULL, NULL);
        g_object_unref(child2);
        g_object_unref(child1);
    }

    g_object_unref(parent_file);
    g_free(parent_uri);
    g_rmdir(tmp_dir);
    g_free(tmp_dir);
}

TEST_CASE("QuiverUtils EnablePopoverMenuIcons and Preferences UI", "[unit][popover][fast]")
{
    REQUIRE_DISPLAY();
    gtk_init();

    const char *xml =
        "<interface>"
        "  <menu id='m'>"
        "    <item>"
        "      <attribute name='label'>Cut</attribute>"
        "      <attribute name='icon'>edit-cut-symbolic</attribute>"
        "    </item>"
        "  </menu>"
        "</interface>";
    GtkBuilder *b = gtk_builder_new_from_string(xml, -1);
    REQUIRE(b != NULL);
    GMenuModel *m = G_MENU_MODEL(gtk_builder_get_object(b, "m"));
    REQUIRE(m != NULL);
    GtkWidget *popover = gtk_popover_menu_new_from_model(m);
    REQUIRE(popover != NULL);
    QuiverUtils::EnablePopoverMenuIcons(popover);

    GtkWidget *sw = gtk_widget_get_first_child(gtk_widget_get_first_child(popover));
    GtkWidget *vp = gtk_widget_get_first_child(sw);
    GtkWidget *stk = gtk_widget_get_first_child(vp);
    GtkWidget *msb = gtk_widget_get_first_child(stk);
    GtkWidget *box = gtk_widget_get_first_child(msb);
    GtkWidget *mb = gtk_widget_get_first_child(box);
    GtkWidget *box_child = gtk_widget_get_first_child(mb);
    GtkWidget *img = gtk_widget_get_next_sibling(box_child);

    REQUIRE(GTK_IS_IMAGE(img));
    CHECK(gtk_widget_get_visible(img) == TRUE);
    CHECK(gtk_widget_get_margin_end(img) == 6);

    g_object_unref(b);

    // Verify preferences dialog UI
    std::string uiPath = QuiverTest_GetDataDir() + "/quiver.ui";
    GtkBuilder *b_ui = gtk_builder_new_from_file(uiPath.c_str());
    REQUIRE(b_ui != NULL);

    GtkWidget *pref_dlg = GTK_WIDGET(gtk_builder_get_object(b_ui, "QuiverPreferencesDialog"));
    REQUIRE(pref_dlg != NULL);
    int def_w = 0, def_h = 0;
    gtk_window_get_default_size(GTK_WINDOW(pref_dlg), &def_w, &def_h);
    CHECK(def_w == 680);
    CHECK(def_h == 520);

    GtkImage *img_browser = GTK_IMAGE(gtk_builder_get_object(b_ui, "image5"));
    REQUIRE(img_browser != NULL);
    CHECK(std::string(gtk_image_get_icon_name(img_browser)) == "folder-symbolic");

    GtkImage *img_general = GTK_IMAGE(gtk_builder_get_object(b_ui, "image1"));
    REQUIRE(img_general != NULL);
    CHECK(std::string(gtk_image_get_icon_name(img_general)) == "preferences-system-symbolic");

    GtkImage *img_viewer = GTK_IMAGE(gtk_builder_get_object(b_ui, "image2"));
    REQUIRE(img_viewer != NULL);
    CHECK(std::string(gtk_image_get_icon_name(img_viewer)) == "image-x-generic-symbolic");

    GtkImage *img_shortcuts = GTK_IMAGE(gtk_builder_get_object(b_ui, "image_tab_shortcuts"));
    REQUIRE(img_shortcuts != NULL);
    CHECK(std::string(gtk_image_get_icon_name(img_shortcuts)) == "insert-link-symbolic");

    GtkImage *img_slideshow = GTK_IMAGE(gtk_builder_get_object(b_ui, "image3"));
    REQUIRE(img_slideshow != NULL);
    CHECK(std::string(gtk_image_get_icon_name(img_slideshow)) == "display-projector-symbolic");

    const char *tabs[] = {"alignment1", "alignment10", "alignment8", "alignment3"};
    for (int i = 0; i < 4; i++) {
        GtkWidget *w = GTK_WIDGET(gtk_builder_get_object(b_ui, tabs[i]));
        REQUIRE(w != NULL);
        CHECK(gtk_widget_get_margin_start(w) == 8);
        CHECK(gtk_widget_get_margin_end(w) == 8);
        CHECK(gtk_widget_get_margin_top(w) == 8);
        CHECK(gtk_widget_get_margin_bottom(w) == 8);
    }

    GtkScrolledWindow *sw_sc = GTK_SCROLLED_WINDOW(gtk_builder_get_object(b_ui, "scrolled_shortcuts"));
    REQUIRE(sw_sc != NULL);
    GtkPolicyType hp = GTK_POLICY_AUTOMATIC, vp_policy = GTK_POLICY_AUTOMATIC;
    gtk_scrolled_window_get_policy(sw_sc, &hp, &vp_policy);
    CHECK(hp == GTK_POLICY_NEVER);

    g_object_unref(b_ui);
}

TEST_CASE("QuiverUtils GetFilmstripPath Selection", "[unit][filmstrip][fast]")
{
    // For 128px or smaller thumbnails, use filmstrip.png
    CHECK(QuiverUtils::GetFilmstripPath(128).find("filmstrip.png") != std::string::npos);
    CHECK(QuiverUtils::GetFilmstripPath(96).find("filmstrip.png") != std::string::npos);
    CHECK(QuiverUtils::GetFilmstripPath(64).find("filmstrip.png") != std::string::npos);
    CHECK(QuiverUtils::GetFilmstripPath(32).find("filmstrip.png") != std::string::npos);

    // For larger thumbnails (> 128px, up to 256px), use filmstrip-big.png
    CHECK(QuiverUtils::GetFilmstripPath(129).find("filmstrip-big.png") != std::string::npos);
    CHECK(QuiverUtils::GetFilmstripPath(192).find("filmstrip-big.png") != std::string::npos);
    CHECK(QuiverUtils::GetFilmstripPath(256).find("filmstrip-big.png") != std::string::npos);
}

TEST_CASE("QuiverUtils PromptAddBookmark Empty URIs", "[unit][bookmarks][fast]")
{
    // Empty URIs returns false without prompting
    CHECK(QuiverUtils::PromptAddBookmark({}) == false);
}

