#include <catch2/catch_test_macros.hpp>
#include "RenameDlg.h"
#include "RenameTask.h"
#include "OrganizeDlg.h"
#include "OrganizeTask.h"
#include "ImageList.h"
#include "FileConflictCheck.h"
#include "Preferences.h"
#include "QuiverPrefs.h"
#include "test_helpers.h"

#include <gtk/gtk.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <unistd.h>
#include <chrono>
#include <cstring>
#include <vector>
#include <string>

static GtkWidget* FindToplevelByTitle(const char* title)
{
    GList* tops = gtk_window_list_toplevels();
    GtkWidget* w = NULL;
    for (GList* l = tops; l != NULL; l = l->next)
    {
        const char* t = gtk_window_get_title(GTK_WINDOW(l->data));
        if (t != NULL && strcmp(t, title) == 0)
        {
            w = GTK_WIDGET(l->data);
            break;
        }
    }
    g_list_free(tops);
    return w;
}

static void FindWidgetsByType(GtkWidget* parent, GType type, std::vector<GtkWidget*>& out)
{
    if (!parent) return;
    if (G_TYPE_CHECK_INSTANCE_TYPE(parent, type))
        out.push_back(parent);
    for (GtkWidget* child = gtk_widget_get_first_child(parent); child != NULL; child = gtk_widget_get_next_sibling(child))
    {
        FindWidgetsByType(child, type, out);
    }
}

TEST_CASE("RenameTask: SORT_BY_DATE synchronous execution without crash", "[unit][rename][task]")
{
    gchar* tmpDir = g_dir_make_tmp("quiver_test_sortdate_XXXXXX", NULL);
    REQUIRE(tmpDir != nullptr);

    std::string sampleJpg = QuiverTest_GetImagesDir() + "/sample_4k.jpg";
    REQUIRE(g_file_test(sampleJpg.c_str(), G_FILE_TEST_EXISTS));

    std::string fileA = std::string(tmpDir) + "/a.jpg";
    std::string fileB = std::string(tmpDir) + "/b.jpg";
    char* contents = nullptr;
    gsize length = 0;
    REQUIRE(g_file_get_contents(sampleJpg.c_str(), &contents, &length, NULL));
    REQUIRE(g_file_set_contents(fileA.c_str(), contents, length, NULL));
    REQUIRE(g_file_set_contents(fileB.c_str(), contents, length, NULL));
    g_free(contents);

    gchar* dirUri = g_filename_to_uri(tmpDir, NULL, NULL);
    REQUIRE(dirUri != nullptr);

    std::vector<FileConflictCheck::Mapping> mappings;
    bool ok = RenameTask::ComputeMappings(dirUri, "Img_###", ImageList::SORT_BY_DATE, mappings);
    REQUIRE(ok == true);
    REQUIRE(mappings.size() == 2);

    g_free(dirUri);
    g_unlink(fileA.c_str());
    g_unlink(fileB.c_str());
    g_rmdir(tmpDir);
    g_free(tmpDir);
}

TEST_CASE("Rename Dialog: Type column removed from preview table", "[unit][rename][gui]")
{
    if (!QuiverTest_HasDisplay())
        SKIP("No display");

    struct RenameCapture {
        bool checked;
        std::vector<std::string> columnTitles;
    } cap{false, {}};

    g_timeout_add(50, +[](gpointer data) -> gboolean {
        auto* c = static_cast<RenameCapture*>(data);
        GtkWidget* win = FindToplevelByTitle("Rename");
        if (win != NULL)
        {
            std::vector<GtkWidget*> columnViews;
            FindWidgetsByType(win, GTK_TYPE_COLUMN_VIEW, columnViews);
            if (!columnViews.empty())
            {
                GListModel* cols = gtk_column_view_get_columns(GTK_COLUMN_VIEW(columnViews[0]));
                guint nCols = g_list_model_get_n_items(cols);
                for (guint i = 0; i < nCols; ++i)
                {
                    GtkColumnViewColumn* col = GTK_COLUMN_VIEW_COLUMN(g_list_model_get_item(cols, i));
                    const char* title = gtk_column_view_column_get_title(col);
                    c->columnTitles.push_back(title ? title : "");
                    g_object_unref(col);
                }
                c->checked = true;
            }
            gtk_window_close(GTK_WINDOW(win));
        }
        return G_SOURCE_REMOVE;
    }, &cap);

    RenameDlg rename;
    rename.Run();

    REQUIRE(cap.checked);
    REQUIRE(cap.columnTitles.size() == 4);
    REQUIRE(cap.columnTitles[0] == "");
    REQUIRE(cap.columnTitles[1] == "Original Name");
    REQUIRE(cap.columnTitles[2] == "New Name");
    REQUIRE(cap.columnTitles[3] == "Conflict");

    for (const auto& t : cap.columnTitles)
    {
        REQUIRE(t != "Type");
    }
}

TEST_CASE("Organize Dialog: Friendly path, non-expanding spinbox, and preview columns", "[unit][organize][gui]")
{
    if (!QuiverTest_HasDisplay())
        SKIP("No display");

    struct OrgCapture {
        bool checked;
        std::string srcButtonLabel;
        bool spinVExpand;
        bool parentVExpand;
        std::vector<std::string> columnTitles;
    } cap{false, "", true, true, {}};

    OrganizeDlg organize;
    organize.SetInputFolder("file:///tmp/quiver_organize_test_folder");

    g_timeout_add(50, +[](gpointer data) -> gboolean {
        auto* c = static_cast<OrgCapture*>(data);
        GtkWidget* win = FindToplevelByTitle("Organize");
        if (win != NULL)
        {
            // Find buttons to check source button label
            std::vector<GtkWidget*> buttons;
            FindWidgetsByType(win, GTK_TYPE_BUTTON, buttons);
            for (GtkWidget* b : buttons)
            {
                const char* label = gtk_button_get_label(GTK_BUTTON(b));
                if (label != NULL && strstr(label, "quiver_organize_test_folder") != NULL)
                {
                    c->srcButtonLabel = label;
                    break;
                }
            }

            // Find spinbutton and verify vexpand is false
            std::vector<GtkWidget*> spinButtons;
            FindWidgetsByType(win, GTK_TYPE_SPIN_BUTTON, spinButtons);
            if (!spinButtons.empty())
            {
                c->spinVExpand = (gtk_widget_get_vexpand(spinButtons[0]) == TRUE);
                GtkWidget* parent = gtk_widget_get_parent(spinButtons[0]);
                if (parent)
                    c->parentVExpand = (gtk_widget_get_vexpand(parent) == TRUE);
            }

            // Find columnview to check columns
            std::vector<GtkWidget*> columnViews;
            FindWidgetsByType(win, GTK_TYPE_COLUMN_VIEW, columnViews);
            if (!columnViews.empty())
            {
                GListModel* cols = gtk_column_view_get_columns(GTK_COLUMN_VIEW(columnViews[0]));
                guint nCols = g_list_model_get_n_items(cols);
                for (guint i = 0; i < nCols; ++i)
                {
                    GtkColumnViewColumn* col = GTK_COLUMN_VIEW_COLUMN(g_list_model_get_item(cols, i));
                    const char* title = gtk_column_view_column_get_title(col);
                    c->columnTitles.push_back(title ? title : "");
                    g_object_unref(col);
                }
            }
            c->checked = true;
            gtk_window_close(GTK_WINDOW(win));
        }
        return G_SOURCE_REMOVE;
    }, &cap);

    organize.Run();

    REQUIRE(cap.checked);
    // Source folder button must display friendly path without "file://" scheme
    REQUIRE(cap.srcButtonLabel == "/tmp/quiver_organize_test_folder");
    REQUIRE(cap.srcButtonLabel.find("file://") == std::string::npos);

    // Spinbutton and its parent box must not have vexpand set
    REQUIRE_FALSE(cap.spinVExpand);
    REQUIRE_FALSE(cap.parentVExpand);

    // Column titles: "", "Original Name", "New Path", "New Name", "Conflict"
    REQUIRE(cap.columnTitles.size() == 5);
    REQUIRE(cap.columnTitles[0] == "");
    REQUIRE(cap.columnTitles[1] == "Original Name");
    REQUIRE(cap.columnTitles[2] == "New Path");
    REQUIRE(cap.columnTitles[3] == "New Name");
    REQUIRE(cap.columnTitles[4] == "Conflict");
}

TEST_CASE("Organize Dialog: 2-column layout and rename sensitivity", "[unit][organize][gui]")
{
    if (!QuiverTest_HasDisplay())
        SKIP("No display");

    struct LayoutCapture {
        bool checked;
        int defaultWidth;
        int defaultHeight;
        bool hasSeparator;
        bool renameInitialSensitive;
        bool renameAfterToggleSensitive;
    } cap{false, 0, 0, false, false, false};

    OrganizeDlg organize;

    g_timeout_add(50, +[](gpointer data) -> gboolean {
        auto* c = static_cast<LayoutCapture*>(data);
        GtkWidget* win = FindToplevelByTitle("Organize");
        if (win != NULL)
        {
            gtk_window_get_default_size(GTK_WINDOW(win), &c->defaultWidth, &c->defaultHeight);

            // Find check button for rename
            std::vector<GtkWidget*> checkButtons;
            FindWidgetsByType(win, GTK_TYPE_CHECK_BUTTON, checkButtons);
            GtkWidget* renameCheck = NULL;
            for (GtkWidget* cb : checkButtons)
            {
                const char* lbl = gtk_check_button_get_label(GTK_CHECK_BUTTON(cb));
                if (lbl != NULL && strcmp(lbl, "Rename files") == 0)
                {
                    renameCheck = cb;
                    break;
                }
            }

            // Find entry for filename template
            std::vector<GtkWidget*> entries;
            FindWidgetsByType(win, GTK_TYPE_ENTRY, entries);
            GtkWidget* templateEntry = NULL;
            for (GtkWidget* e : entries)
            {
                const char* txt = gtk_editable_get_text(GTK_EDITABLE(e));
                if (txt != NULL && strstr(txt, "%Y") != NULL)
                {
                    templateEntry = e;
                    break;
                }
            }

            if (renameCheck && templateEntry)
            {
                c->renameInitialSensitive = gtk_widget_is_sensitive(templateEntry);
                // Toggle off rename files
                gtk_check_button_set_active(GTK_CHECK_BUTTON(renameCheck), FALSE);
                c->renameAfterToggleSensitive = gtk_widget_is_sensitive(templateEntry);
            }

            // Check for separator
            std::vector<GtkWidget*> seps;
            FindWidgetsByType(win, GTK_TYPE_SEPARATOR, seps);
            c->hasSeparator = !seps.empty();

            c->checked = true;
            gtk_window_close(GTK_WINDOW(win));
        }
        return G_SOURCE_REMOVE;
    }, &cap);

    organize.Run();

    REQUIRE(cap.checked);
    REQUIRE(cap.defaultWidth >= 780);
    REQUIRE(cap.defaultHeight >= 580);
    REQUIRE(cap.hasSeparator);
    REQUIRE(cap.renameInitialSensitive == true);
    REQUIRE(cap.renameAfterToggleSensitive == false);
}

TEST_CASE("Organize Dialog: template change gives immediate conflict-check feedback", "[unit][organize][gui]")
{
    REQUIRE_DISPLAY();

    // temp dirs: source holds one supported file, destination is empty
    gchar* tmpSrcDir = g_dir_make_tmp("quiver_test_org_fb_src_XXXXXX", NULL);
    gchar* tmpDstDir = g_dir_make_tmp("quiver_test_org_fb_dst_XXXXXX", NULL);
    REQUIRE(tmpSrcDir != NULL);
    REQUIRE(tmpDstDir != NULL);

    std::string sampleJpg = QuiverTest_GetImagesDir() + "/sample_4k.jpg";
    REQUIRE(g_file_test(sampleJpg.c_str(), G_FILE_TEST_EXISTS));
    std::string srcFile = std::string(tmpSrcDir) + "/sample.jpg";
    char* contents = NULL;
    gsize length = 0;
    REQUIRE(g_file_get_contents(sampleJpg.c_str(), &contents, &length, NULL));
    REQUIRE(g_file_set_contents(srcFile.c_str(), contents, length, NULL));
    g_free(contents);

    gchar* srcUri = g_filename_to_uri(tmpSrcDir, NULL, NULL);
    gchar* dstUri = g_filename_to_uri(tmpDstDir, NULL, NULL);
    REQUIRE(srcUri != NULL);
    REQUIRE(dstUri != NULL);

    // point the dialog's default destination at the temp dst folder
    char cfgPath[256];
    strncpy(cfgPath, "/tmp/quiver_test_org_fb_XXXXXX.ini", sizeof(cfgPath) - 1);
    cfgPath[sizeof(cfgPath) - 1] = '\0';
    int fd = g_mkstemp(cfgPath);
    if (fd >= 0)
        close(fd);
    strncpy(g_szConfigFilePath, cfgPath, sizeof(cfgPath) - 1);
    g_szConfigFilePath[sizeof(cfgPath) - 1] = '\0';
    Preferences::Reset();
    PreferencesPtr prefs = Preferences::GetInstance();
    REQUIRE(prefs != NULL);
    prefs->SetString(QUIVER_PREFS_APP, QUIVER_PREFS_APP_PHOTO_LIBRARY, dstUri);

    struct OrgFeedbackCapture {
        bool changed;          // template entry already modified
        int64_t firstReadyMs;  // ms between template change and next "Ready:" text
        bool sawChecking;      // "Checking for conflicts…" shown after the change
        int64_t startMs;       // monotonic ms when the dialog was opened
        GtkWidget* statusLabel;
        GtkEditable* templateEntry;
        std::chrono::steady_clock::time_point tChange;
    } fb{false, -1, false,
        static_cast<int64_t>(g_get_monotonic_time() / 1000), NULL, NULL, {}};

    g_timeout_add(10, +[](gpointer data) -> gboolean {
        auto* fb = static_cast<OrgFeedbackCapture*>(data);
        GtkWidget* win = FindToplevelByTitle("Organize");
        if (win == NULL)
            return G_SOURCE_CONTINUE;

        if (fb->statusLabel == NULL)
        {
            std::vector<GtkWidget*> labels;
            FindWidgetsByType(win, GTK_TYPE_LABEL, labels);
            for (GtkWidget* l : labels)
            {
                const char* txt = gtk_label_get_text(GTK_LABEL(l));
                if (txt != NULL &&
                    (strstr(txt, "Ready:") != NULL || strstr(txt, "Checking for conflicts") != NULL))
                {
                    fb->statusLabel = l;
                    break;
                }
            }
            if (fb->statusLabel != NULL)
            {
                std::vector<GtkWidget*> entries;
                FindWidgetsByType(win, GTK_TYPE_ENTRY, entries);
                for (GtkWidget* e : entries)
                {
                    const char* txt = gtk_editable_get_text(GTK_EDITABLE(e));
                    if (txt != NULL && strstr(txt, "%Y") != NULL)
                    {
                        fb->templateEntry = GTK_EDITABLE(e);
                        break;
                    }
                }
            }
            return G_SOURCE_CONTINUE;
        }

        const char* ctxt = gtk_label_get_text(GTK_LABEL(fb->statusLabel));
        std::string cur = ctxt ? ctxt : "";

        if (!fb->changed)
        {
            if (cur.find("Ready:") != std::string::npos)
            {
                fb->changed = true;
                fb->tChange = std::chrono::steady_clock::now();
                gtk_editable_set_text(fb->templateEntry, "Photo-###");
            }
            else if (static_cast<int64_t>(g_get_monotonic_time() / 1000) - fb->startMs > 5000)
            {
                gtk_window_close(GTK_WINDOW(win));
                return G_SOURCE_REMOVE;
            }
            return G_SOURCE_CONTINUE;
        }

        const int64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - fb->tChange).count();
        if (cur.find("Checking for conflicts") != std::string::npos)
            fb->sawChecking = true;
        if (fb->firstReadyMs < 0 && cur.find("Ready:") != std::string::npos)
            fb->firstReadyMs = ms;

        if (fb->firstReadyMs >= 0 || ms > 3000)
        {
            gtk_window_close(GTK_WINDOW(win));
            return G_SOURCE_REMOVE;
        }
        return G_SOURCE_CONTINUE;
    }, &fb);

    OrganizeDlg organize;
    organize.SetInputFolder(srcUri);
    organize.Run();

    REQUIRE(fb.statusLabel != NULL);
    REQUIRE(fb.templateEntry != NULL);
    REQUIRE(fb.changed);
    // The "Ready:" line may not reappear before the debounced re-check fires
    // (300 ms) at the earliest; a stale result being re-applied shows up much
    // sooner (~80 ms poll interval).
    REQUIRE(fb.firstReadyMs >= 0);
    REQUIRE(fb.firstReadyMs >= 250);
    // and the status text must flip to "Checking for conflicts…" right away
    REQUIRE(fb.sawChecking);

    Preferences::Reset();
    g_unlink(cfgPath);
    g_free(srcUri);
    g_free(dstUri);
    g_unlink(srcFile.c_str());
    g_rmdir(tmpSrcDir);
    g_rmdir(tmpDstDir);
    g_free(tmpSrcDir);
    g_free(tmpDstDir);
}

