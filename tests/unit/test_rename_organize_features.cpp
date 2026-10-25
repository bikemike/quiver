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

#include <exiv2/exiv2.hpp>
#include <gtk/gtk.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <unistd.h>
#include <chrono>
#include <cstring>
#include <vector>
#include <string>
#include <utime.h>

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
    // the copies carry no EXIF date, so the mtime stands in for it and the
    // dialogs warn about it
    for (const auto& m : mappings)
        REQUIRE(m.bHasDateMetadata == false);

    // give one copy a real EXIF date: only that mapping is exempt
    {
        auto image = Exiv2::ImageFactory::open(fileB.c_str());
        REQUIRE(image.get() != nullptr);
        image->readMetadata();
        image->exifData()["Exif.Photo.DateTimeOriginal"] = "2024:06:15 10:20:30";
        image->writeMetadata();
    }
    mappings.clear();
    ok = RenameTask::ComputeMappings(dirUri, "Img_###", ImageList::SORT_BY_DATE, mappings);
    REQUIRE(ok == true);
    REQUIRE(mappings.size() == 2);
    int nWithDate = 0;
    for (const auto& m : mappings)
        if (m.bHasDateMetadata)
            nWithDate++;
    REQUIRE(nWithDate == 1);

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
    REQUIRE(cap.columnTitles[3] == "Issue");

    for (const auto& t : cap.columnTitles)
    {
        REQUIRE(t != "Type");
    }
}

TEST_CASE("Rename Dialog: dry-run indicates renamed count and sorts files to be renamed above unchanged", "[unit][rename][gui]")
{
    if (!QuiverTest_HasDisplay())
        SKIP("No display");

    gchar* tmpDir = g_dir_make_tmp("quiver_test_rename_display_XXXXXX", NULL);
    REQUIRE(tmpDir != nullptr);

    std::string sampleJpg = QuiverTest_GetImagesDir() + "/sample_4k.jpg";
    REQUIRE(g_file_test(sampleJpg.c_str(), G_FILE_TEST_EXISTS));

    std::string fileMatched = std::string(tmpDir) + "/Photo_001.jpg";
    std::string fileToRenameA = std::string(tmpDir) + "/a_unmatched.jpg";
    std::string fileToRenameB = std::string(tmpDir) + "/b_unmatched.jpg";

    char* contents = nullptr;
    gsize length = 0;
    REQUIRE(g_file_get_contents(sampleJpg.c_str(), &contents, &length, NULL));
    REQUIRE(g_file_set_contents(fileMatched.c_str(), contents, length, NULL));
    REQUIRE(g_file_set_contents(fileToRenameA.c_str(), contents, length, NULL));
    REQUIRE(g_file_set_contents(fileToRenameB.c_str(), contents, length, NULL));
    g_free(contents);

    gchar* uriMatched = g_filename_to_uri(fileMatched.c_str(), NULL, NULL);
    gchar* uriA = g_filename_to_uri(fileToRenameA.c_str(), NULL, NULL);
    gchar* uriB = g_filename_to_uri(fileToRenameB.c_str(), NULL, NULL);
    REQUIRE(uriMatched != nullptr);
    REQUIRE(uriA != nullptr);
    REQUIRE(uriB != nullptr);

    std::vector<QuiverFile> files;
    files.push_back(QuiverFile(uriMatched));
    files.push_back(QuiverFile(uriB));
    files.push_back(QuiverFile(uriA));

    g_free(uriMatched);
    g_free(uriA);
    g_free(uriB);

    struct RenameDisplayCapture {
        bool checked;
        std::string statusText;
        int64_t startMs;
    } cap{false, "", static_cast<int64_t>(g_get_monotonic_time() / 1000)};

    RenameDlg rename;
    rename.SetFiles(files);

    g_timeout_add(20, +[](gpointer data) -> gboolean {
        auto* c = static_cast<RenameDisplayCapture*>(data);
        GtkWidget* win = FindToplevelByTitle("Rename");
        if (win != NULL)
        {
            std::vector<GtkWidget*> entries;
            FindWidgetsByType(win, GTK_TYPE_ENTRY, entries);
            for (GtkWidget* e : entries)
            {
                const char* cur = gtk_editable_get_text(GTK_EDITABLE(e));
                if (cur && strcmp(cur, "Photo_###") != 0)
                {
                    gtk_editable_set_text(GTK_EDITABLE(e), "Photo_###");
                    break;
                }
            }

            std::vector<GtkWidget*> labels;
            FindWidgetsByType(win, GTK_TYPE_LABEL, labels);
            for (GtkWidget* l : labels)
            {
                const char* txt = gtk_label_get_text(GTK_LABEL(l));
                if (txt != NULL)
                {
                    if (strstr(txt, "files will be renamed") != NULL)
                    {
                        c->statusText = txt;
                        c->checked = true;
                        break;
                    }
                }
            }

            if (c->checked || static_cast<int64_t>(g_get_monotonic_time() / 1000) - c->startMs > 5000)
            {
                gtk_window_close(GTK_WINDOW(win));
                return G_SOURCE_REMOVE;
            }
        }
        return G_SOURCE_CONTINUE;
    }, &cap);

    rename.Run();

    REQUIRE(cap.checked);
    REQUIRE(cap.statusText.find("2 of 3 files will be renamed, 1 file already matches its template (3 files have no date metadata)") != std::string::npos);

    auto results = rename.GetConflictResults();
    REQUIRE(results.size() == 3);
    REQUIRE(results[0].strSrcName != results[0].strDstName);
    REQUIRE(results[1].strSrcName != results[1].strDstName);
    REQUIRE(results[0].strSrcName == "a_unmatched.jpg");
    REQUIRE(results[1].strSrcName == "b_unmatched.jpg");
    REQUIRE(results[2].strSrcName == results[2].strDstName);
    REQUIRE(results[2].strSrcName == "Photo_001.jpg");

    g_unlink(fileMatched.c_str());
    g_unlink(fileToRenameA.c_str());
    g_unlink(fileToRenameB.c_str());
    g_rmdir(tmpDir);
    g_free(tmpDir);
}

TEST_CASE("Rename Dialog: plural matching files count and all-renamed count", "[unit][rename][gui]")
{
    if (!QuiverTest_HasDisplay())
        SKIP("No display");

    gchar* tmpDir = g_dir_make_tmp("quiver_test_rename_plural_XXXXXX", NULL);
    REQUIRE(tmpDir != nullptr);

    std::string sampleJpg = QuiverTest_GetImagesDir() + "/sample_4k.jpg";
    REQUIRE(g_file_test(sampleJpg.c_str(), G_FILE_TEST_EXISTS));

    std::string fMatch1 = std::string(tmpDir) + "/Pic_001.jpg";
    std::string fMatch2 = std::string(tmpDir) + "/Pic_002.jpg";
    std::string fNew1 = std::string(tmpDir) + "/img_x.jpg";
    std::string fNew2 = std::string(tmpDir) + "/img_y.jpg";

    char* contents = nullptr;
    gsize length = 0;
    REQUIRE(g_file_get_contents(sampleJpg.c_str(), &contents, &length, NULL));
    REQUIRE(g_file_set_contents(fMatch1.c_str(), contents, length, NULL));
    REQUIRE(g_file_set_contents(fMatch2.c_str(), contents, length, NULL));
    REQUIRE(g_file_set_contents(fNew1.c_str(), contents, length, NULL));
    REQUIRE(g_file_set_contents(fNew2.c_str(), contents, length, NULL));
    g_free(contents);

    gchar* uriMatch1 = g_filename_to_uri(fMatch1.c_str(), NULL, NULL);
    gchar* uriMatch2 = g_filename_to_uri(fMatch2.c_str(), NULL, NULL);
    gchar* uriNew1 = g_filename_to_uri(fNew1.c_str(), NULL, NULL);
    gchar* uriNew2 = g_filename_to_uri(fNew2.c_str(), NULL, NULL);

    std::vector<QuiverFile> files;
    files.push_back(QuiverFile(uriMatch1));
    files.push_back(QuiverFile(uriMatch2));
    files.push_back(QuiverFile(uriNew2));
    files.push_back(QuiverFile(uriNew1));

    g_free(uriMatch1);
    g_free(uriMatch2);
    g_free(uriNew1);
    g_free(uriNew2);

    struct RenamePluralCapture {
        bool checked;
        std::string statusText;
        int64_t startMs;
    } cap{false, "", static_cast<int64_t>(g_get_monotonic_time() / 1000)};

    RenameDlg rename;
    rename.SetFiles(files);

    g_timeout_add(20, +[](gpointer data) -> gboolean {
        auto* c = static_cast<RenamePluralCapture*>(data);
        GtkWidget* win = FindToplevelByTitle("Rename");
        if (win != NULL)
        {
            std::vector<GtkWidget*> entries;
            FindWidgetsByType(win, GTK_TYPE_ENTRY, entries);
            for (GtkWidget* e : entries)
            {
                const char* cur = gtk_editable_get_text(GTK_EDITABLE(e));
                if (cur && strcmp(cur, "Pic_###") != 0)
                {
                    gtk_editable_set_text(GTK_EDITABLE(e), "Pic_###");
                    break;
                }
            }

            std::vector<GtkWidget*> labels;
            FindWidgetsByType(win, GTK_TYPE_LABEL, labels);
            for (GtkWidget* l : labels)
            {
                const char* txt = gtk_label_get_text(GTK_LABEL(l));
                if (txt != NULL && strstr(txt, "files will be renamed") != NULL)
                {
                    c->statusText = txt;
                    c->checked = true;
                    break;
                }
            }

            if (c->checked || static_cast<int64_t>(g_get_monotonic_time() / 1000) - c->startMs > 5000)
            {
                gtk_window_close(GTK_WINDOW(win));
                return G_SOURCE_REMOVE;
            }
        }
        return G_SOURCE_CONTINUE;
    }, &cap);

    rename.Run();

    REQUIRE(cap.checked);
    // 2 files to be renamed, 2 files already match
    REQUIRE(cap.statusText.find("2 of 4 files will be renamed, 2 files already match their template (4 files have no date metadata)") != std::string::npos);

    auto results = rename.GetConflictResults();
    REQUIRE(results.size() == 4);
    // Files to be renamed are sorted above unchanged ones
    REQUIRE(results[0].strSrcName != results[0].strDstName);
    REQUIRE(results[1].strSrcName != results[1].strDstName);
    REQUIRE(results[2].strSrcName == results[2].strDstName);
    REQUIRE(results[3].strSrcName == results[3].strDstName);

    g_unlink(fMatch1.c_str());
    g_unlink(fMatch2.c_str());
    g_unlink(fNew1.c_str());
    g_unlink(fNew2.c_str());
    g_rmdir(tmpDir);
    g_free(tmpDir);
}

TEST_CASE("Rename Dialog: 'See All' button expands preview beyond row cap sampling", "[unit][rename][gui]")
{
    if (!QuiverTest_HasDisplay())
        SKIP("No display");

    gchar* tmpDir = g_dir_make_tmp("quiver_test_rename_see_all_XXXXXX", NULL);
    REQUIRE(tmpDir != nullptr);

    std::string sampleJpg = QuiverTest_GetImagesDir() + "/sample_4k.jpg";
    REQUIRE(g_file_test(sampleJpg.c_str(), G_FILE_TEST_EXISTS));

    char* contents = nullptr;
    gsize length = 0;
    REQUIRE(g_file_get_contents(sampleJpg.c_str(), &contents, &length, NULL));

    const int totalFiles = 505;
    std::vector<std::string> filePaths;
    filePaths.reserve(totalFiles);
    std::vector<QuiverFile> files;
    files.reserve(totalFiles);

    for (int i = 0; i < totalFiles; ++i)
    {
        char filename[64];
        g_snprintf(filename, sizeof(filename), "/img_%04d.jpg", i + 1);
        std::string fullPath = std::string(tmpDir) + filename;
        REQUIRE(g_file_set_contents(fullPath.c_str(), contents, length, NULL));
        filePaths.push_back(fullPath);

        gchar* uri = g_filename_to_uri(fullPath.c_str(), NULL, NULL);
        REQUIRE(uri != nullptr);
        files.push_back(QuiverFile(uri));
        g_free(uri);
    }
    g_free(contents);

    struct SeeAllCapture {
        bool checked;
        GtkWidget* btnSeeAll;
        guint initialCount;
        guint expandedCount;
        guint collapsedCount;
        std::string initialBtnLabel;
        std::string expandedBtnLabel;
        std::string collapsedBtnLabel;
        int64_t startMs;
    } cap{false, NULL, 0, 0, 0, "", "", "", static_cast<int64_t>(g_get_monotonic_time() / 1000)};

    RenameDlg rename;
    rename.SetFiles(files);

    g_timeout_add(30, +[](gpointer data) -> gboolean {
        auto* c = static_cast<SeeAllCapture*>(data);
        GtkWidget* win = FindToplevelByTitle("Rename");
        if (win != NULL)
        {
            if (c->btnSeeAll == NULL)
            {
                std::vector<GtkWidget*> buttons;
                FindWidgetsByType(win, GTK_TYPE_BUTTON, buttons);
                for (GtkWidget* b : buttons)
                {
                    const char* lbl = gtk_button_get_label(GTK_BUTTON(b));
                    if (lbl && strcmp(lbl, "See All") == 0)
                    {
                        if (gtk_widget_get_visible(b))
                        {
                            c->btnSeeAll = b;
                            break;
                        }
                    }
                }
            }

            if (c->btnSeeAll != NULL && !c->checked)
            {
                std::vector<GtkWidget*> colViews;
                FindWidgetsByType(win, GTK_TYPE_COLUMN_VIEW, colViews);
                if (!colViews.empty())
                {
                    GListModel* model = G_LIST_MODEL(gtk_column_view_get_model(GTK_COLUMN_VIEW(colViews[0])));
                    c->initialCount = g_list_model_get_n_items(model);
                    c->initialBtnLabel = gtk_button_get_label(GTK_BUTTON(c->btnSeeAll));

                    // Click "See All"
                    g_signal_emit_by_name(c->btnSeeAll, "clicked");
                    c->expandedCount = g_list_model_get_n_items(model);
                    c->expandedBtnLabel = gtk_button_get_label(GTK_BUTTON(c->btnSeeAll));

                    // Click again to toggle back
                    g_signal_emit_by_name(c->btnSeeAll, "clicked");
                    c->collapsedCount = g_list_model_get_n_items(model);
                    c->collapsedBtnLabel = gtk_button_get_label(GTK_BUTTON(c->btnSeeAll));

                    c->checked = true;
                    gtk_window_close(GTK_WINDOW(win));
                    return G_SOURCE_REMOVE;
                }
            }

            if (static_cast<int64_t>(g_get_monotonic_time() / 1000) - c->startMs > 5000)
            {
                gtk_window_close(GTK_WINDOW(win));
                return G_SOURCE_REMOVE;
            }
        }
        return G_SOURCE_CONTINUE;
    }, &cap);

    rename.Run();

    REQUIRE(cap.checked);
    REQUIRE(cap.initialBtnLabel == "See All");
    REQUIRE(cap.initialCount == 501);

    REQUIRE(cap.expandedBtnLabel == "Show Sample");
    REQUIRE(cap.expandedCount == 505);

    REQUIRE(cap.collapsedBtnLabel == "See All");
    REQUIRE(cap.collapsedCount == 501);

    for (const auto& p : filePaths)
    {
        g_unlink(p.c_str());
    }
    g_rmdir(tmpDir);
    g_free(tmpDir);
}

TEST_CASE("Rename Dialog: files without date metadata sort below conflicts and above dated files, status indicates no-date count", "[unit][rename][gui][date]")
{
    if (!QuiverTest_HasDisplay())
        SKIP("No display");

    gchar* tmpDir = g_dir_make_tmp("quiver_test_rename_date_XXXXXX", NULL);
    REQUIRE(tmpDir != nullptr);

    std::string sampleJpg = QuiverTest_GetImagesDir() + "/sample_4k.jpg";
    REQUIRE(g_file_test(sampleJpg.c_str(), G_FILE_TEST_EXISTS));

    char* contents = nullptr;
    gsize length = 0;
    REQUIRE(g_file_get_contents(sampleJpg.c_str(), &contents, &length, NULL));

    std::string fDated = std::string(tmpDir) + "/z_dated.jpg";
    std::string fUndatedMatch = std::string(tmpDir) + "/Pic_002.jpg";
    std::string fUndatedRename = std::string(tmpDir) + "/a_undated.jpg";

    REQUIRE(g_file_set_contents(fDated.c_str(), contents, length, NULL));
    REQUIRE(g_file_set_contents(fUndatedMatch.c_str(), contents, length, NULL));
    REQUIRE(g_file_set_contents(fUndatedRename.c_str(), contents, length, NULL));
    g_free(contents);

    // Give fDated an EXIF date tag so it has date metadata
    {
        auto image = Exiv2::ImageFactory::open(fDated.c_str());
        REQUIRE(image.get() != nullptr);
        image->readMetadata();
        image->exifData()["Exif.Photo.DateTimeOriginal"] = "2024:06:15 10:20:30";
        image->writeMetadata();
    }

    gchar* uDated = g_filename_to_uri(fDated.c_str(), NULL, NULL);
    gchar* uUndatedMatch = g_filename_to_uri(fUndatedMatch.c_str(), NULL, NULL);
    gchar* uUndatedRename = g_filename_to_uri(fUndatedRename.c_str(), NULL, NULL);

    std::vector<QuiverFile> files;
    files.push_back(QuiverFile(uDated));
    files.push_back(QuiverFile(uUndatedMatch));
    files.push_back(QuiverFile(uUndatedRename));

    g_free(uDated);
    g_free(uUndatedMatch);
    g_free(uUndatedRename);

    struct DateSortCapture {
        bool checked;
        std::string statusText;
        std::vector<bool> colResizable;
        std::vector<int> colFixedWidth;
        int64_t startMs;
    } cap{false, "", {}, {}, static_cast<int64_t>(g_get_monotonic_time() / 1000)};

    RenameDlg rename;
    rename.SetFiles(files);

    g_timeout_add(20, +[](gpointer data) -> gboolean {
        auto* c = static_cast<DateSortCapture*>(data);
        GtkWidget* win = FindToplevelByTitle("Rename");
        if (win != NULL)
        {
            std::vector<GtkWidget*> entries;
            FindWidgetsByType(win, GTK_TYPE_ENTRY, entries);
            for (GtkWidget* e : entries)
            {
                const char* cur = gtk_editable_get_text(GTK_EDITABLE(e));
                if (cur && strcmp(cur, "Pic_###") != 0)
                {
                    gtk_editable_set_text(GTK_EDITABLE(e), "Pic_###");
                    break;
                }
            }

            std::vector<GtkWidget*> labels;
            FindWidgetsByType(win, GTK_TYPE_LABEL, labels);
            for (GtkWidget* l : labels)
            {
                const char* txt = gtk_label_get_text(GTK_LABEL(l));
                if (txt != NULL && strstr(txt, "files will be renamed") != NULL)
                {
                    c->statusText = txt;
                    c->checked = true;
                    break;
                }
            }

            if (c->checked)
            {
                std::vector<GtkWidget*> colViews;
                FindWidgetsByType(win, GTK_TYPE_COLUMN_VIEW, colViews);
                if (!colViews.empty())
                {
                    GListModel* cols = gtk_column_view_get_columns(GTK_COLUMN_VIEW(colViews[0]));
                    guint nCols = g_list_model_get_n_items(cols);
                    for (guint i = 0; i < nCols; ++i)
                    {
                        GtkColumnViewColumn* col = GTK_COLUMN_VIEW_COLUMN(g_list_model_get_item(cols, i));
                        c->colResizable.push_back(gtk_column_view_column_get_resizable(col) == TRUE);
                        c->colFixedWidth.push_back(gtk_column_view_column_get_fixed_width(col));
                        g_object_unref(col);
                    }
                }
                gtk_window_close(GTK_WINDOW(win));
                return G_SOURCE_REMOVE;
            }

            if (static_cast<int64_t>(g_get_monotonic_time() / 1000) - c->startMs > 5000)
            {
                gtk_window_close(GTK_WINDOW(win));
                return G_SOURCE_REMOVE;
            }
        }
        return G_SOURCE_CONTINUE;
    }, &cap);

    rename.Run();

    REQUIRE(cap.checked);
    // Status message specifies count of files that have no date metadata
    REQUIRE(cap.statusText.find("2 of 3 files will be renamed, 1 file already matches its template (2 files have no date metadata)") != std::string::npos);

    // Conflict results ordering:
    // 1. Conflicts at top
    // 2. No metadata warnings (all files lacking date metadata sort here above normal renames)
    // 3. Normal rename (files to be renamed with date metadata)
    // 4. 'no change' items at the very bottom
    auto results = rename.GetConflictResults();
    REQUIRE(results.size() == 3);
    // Row 0: a_undated.jpg (no date metadata)
    REQUIRE_FALSE(results[0].bHasDateMetadata);
    REQUIRE(results[0].strSrcName == "a_undated.jpg");
    REQUIRE(rename.GetPreviewItemIssue(0) == "No date metadata (uses file modification time)");
    // Row 1: Pic_002.jpg (no date metadata, matches template -> warning sorts above normal rename!)
    REQUIRE_FALSE(results[1].bHasDateMetadata);
    REQUIRE(results[1].strSrcName == "Pic_002.jpg");
    REQUIRE(rename.GetPreviewItemIssue(1) == "No date metadata (uses file modification time)");
    // Row 2: z_dated.jpg (has date metadata, will be renamed)
    REQUIRE(results[2].bHasDateMetadata);
    REQUIRE(results[2].strSrcName == "z_dated.jpg");
    REQUIRE(rename.GetPreviewItemIssue(2) == "");

    // Columns: icon is fixed (32), text columns are resizable with fixed base widths
    REQUIRE(cap.colFixedWidth.size() == 4);
    REQUIRE(cap.colFixedWidth[0] == 32);
    REQUIRE(cap.colFixedWidth[1] == 170);
    REQUIRE(cap.colFixedWidth[2] == 170);
    REQUIRE(cap.colFixedWidth[3] == 180);
    REQUIRE(cap.colResizable[1] == true);
    REQUIRE(cap.colResizable[2] == true);
    REQUIRE(cap.colResizable[3] == true);

    g_unlink(fDated.c_str());
    g_unlink(fUndatedMatch.c_str());
    g_unlink(fUndatedRename.c_str());
    g_rmdir(tmpDir);
    g_free(tmpDir);
}

TEST_CASE("Rename Dialog: items that won't be renamed show 'no change' in issue column", "[unit][rename][gui][issue]")
{
    if (!QuiverTest_HasDisplay())
        SKIP("No display");

    gchar* tmpDir = g_dir_make_tmp("quiver_test_rename_no_change_XXXXXX", NULL);
    REQUIRE(tmpDir != nullptr);

    std::string sampleJpg = QuiverTest_GetImagesDir() + "/sample_4k.jpg";
    REQUIRE(g_file_test(sampleJpg.c_str(), G_FILE_TEST_EXISTS));

    std::string fWillRename = std::string(tmpDir) + "/orig_1.jpg";
    std::string fMatches = std::string(tmpDir) + "/Photo_002.jpg";

    char* contents = nullptr;
    gsize length = 0;
    REQUIRE(g_file_get_contents(sampleJpg.c_str(), &contents, &length, NULL));
    REQUIRE(g_file_set_contents(fWillRename.c_str(), contents, length, NULL));
    REQUIRE(g_file_set_contents(fMatches.c_str(), contents, length, NULL));
    g_free(contents);

    // Give both files EXIF dates so neither has a missing-date warning
    {
        auto img1 = Exiv2::ImageFactory::open(fWillRename.c_str());
        REQUIRE(img1.get() != nullptr);
        img1->readMetadata();
        img1->exifData()["Exif.Photo.DateTimeOriginal"] = "2024:06:15 10:20:30";
        img1->writeMetadata();

        auto img2 = Exiv2::ImageFactory::open(fMatches.c_str());
        REQUIRE(img2.get() != nullptr);
        img2->readMetadata();
        img2->exifData()["Exif.Photo.DateTimeOriginal"] = "2024:06:15 10:20:30";
        img2->writeMetadata();
    }

    gchar* uWillRename = g_filename_to_uri(fWillRename.c_str(), NULL, NULL);
    gchar* uMatches = g_filename_to_uri(fMatches.c_str(), NULL, NULL);

    std::vector<QuiverFile> files;
    files.push_back(QuiverFile(uWillRename));
    files.push_back(QuiverFile(uMatches));

    g_free(uWillRename);
    g_free(uMatches);

    struct NoChangeCapture {
        bool checked;
        std::string statusText;
        int64_t startMs;
    } cap{false, "", static_cast<int64_t>(g_get_monotonic_time() / 1000)};

    RenameDlg rename;
    rename.SetFiles(files);

    g_timeout_add(20, +[](gpointer data) -> gboolean {
        auto* c = static_cast<NoChangeCapture*>(data);
        GtkWidget* win = FindToplevelByTitle("Rename");
        if (win != NULL)
        {
            std::vector<GtkWidget*> entries;
            FindWidgetsByType(win, GTK_TYPE_ENTRY, entries);
            for (GtkWidget* e : entries)
            {
                const char* cur = gtk_editable_get_text(GTK_EDITABLE(e));
                if (cur && strcmp(cur, "Photo_###") != 0)
                {
                    gtk_editable_set_text(GTK_EDITABLE(e), "Photo_###");
                    break;
                }
            }

            std::vector<GtkWidget*> labels;
            FindWidgetsByType(win, GTK_TYPE_LABEL, labels);
            for (GtkWidget* l : labels)
            {
                const char* txt = gtk_label_get_text(GTK_LABEL(l));
                if (txt != NULL && strstr(txt, "files will be renamed") != NULL)
                {
                    c->statusText = txt;
                    c->checked = true;
                    break;
                }
            }

            if (c->checked)
            {
                gtk_window_close(GTK_WINDOW(win));
                return G_SOURCE_REMOVE;
            }

            if (static_cast<int64_t>(g_get_monotonic_time() / 1000) - c->startMs > 5000)
            {
                gtk_window_close(GTK_WINDOW(win));
                return G_SOURCE_REMOVE;
            }
        }
        return G_SOURCE_CONTINUE;
    }, &cap);

    rename.Run();

    REQUIRE(cap.checked);
    REQUIRE(cap.statusText.find("1 of 2 files will be renamed, 1 file already matches its template") != std::string::npos);

    REQUIRE(rename.GetPreviewItemCount() == 2);
    auto results = rename.GetConflictResults();
    REQUIRE(results.size() == 2);
    REQUIRE(results[0].strSrcName != results[0].strDstName);
    REQUIRE(results[1].strSrcName == results[1].strDstName);

    // Row 1 is the unchanged file (Photo_002.jpg -> Photo_002.jpg), issue is "no change"
    REQUIRE(rename.GetPreviewItemIssue(1) == "no change");

    g_unlink(fWillRename.c_str());
    g_unlink(fMatches.c_str());
    g_rmdir(tmpDir);
    g_free(tmpDir);
}

TEST_CASE("Rename Dialog: preview sorting hierarchy (conflicts, no metadata, no conflicts, no change)", "[unit][rename][gui][sort]")
{
    if (!QuiverTest_HasDisplay())
        SKIP("No display");

    gchar* tmpDir = g_dir_make_tmp("quiver_test_rename_4tier_XXXXXX", NULL);
    REQUIRE(tmpDir != nullptr);

    std::string sampleJpg = QuiverTest_GetImagesDir() + "/sample_4k.jpg";
    REQUIRE(g_file_test(sampleJpg.c_str(), G_FILE_TEST_EXISTS));

    char* contents = nullptr;
    gsize length = 0;
    REQUIRE(g_file_get_contents(sampleJpg.c_str(), &contents, &length, NULL));

    // Template "Item_###"
    // Item_001.jpg -> pre-existing on disk (not in rename list) causing collision
    // 1. File to collide: orig_a.jpg -> maps to Item_001.jpg (Conflict, Rank 0)
    // 2. File without date: orig_b.jpg -> maps to Item_002.jpg (No metadata, Rank 1)
    // 3. File with date: orig_c.jpg -> maps to Item_003.jpg (No conflict/normal, Rank 2)
    // 4. File already matching: Item_004.jpg -> maps to Item_004.jpg (No change, Rank 3)

    std::string fDiskExisting = std::string(tmpDir) + "/Item_001.jpg";
    std::string fCollide = std::string(tmpDir) + "/orig_a.jpg";
    std::string fNoDate = std::string(tmpDir) + "/orig_b.jpg";
    std::string fWithDate = std::string(tmpDir) + "/orig_c.jpg";
    std::string fNoChange = std::string(tmpDir) + "/Item_004.jpg";

    REQUIRE(g_file_set_contents(fDiskExisting.c_str(), contents, length, NULL));
    REQUIRE(g_file_set_contents(fCollide.c_str(), contents, length, NULL));
    REQUIRE(g_file_set_contents(fNoDate.c_str(), contents, length, NULL));
    REQUIRE(g_file_set_contents(fWithDate.c_str(), contents, length, NULL));
    REQUIRE(g_file_set_contents(fNoChange.c_str(), contents, length, NULL));
    g_free(contents);

    // Give fWithDate and fNoChange EXIF date tags so they have date metadata
    {
        auto image = Exiv2::ImageFactory::open(fWithDate.c_str());
        REQUIRE(image.get() != nullptr);
        image->readMetadata();
        image->exifData()["Exif.Photo.DateTimeOriginal"] = "2024:06:15 10:20:30";
        image->writeMetadata();

        auto image2 = Exiv2::ImageFactory::open(fNoChange.c_str());
        REQUIRE(image2.get() != nullptr);
        image2->readMetadata();
        image2->exifData()["Exif.Photo.DateTimeOriginal"] = "2024:06:15 10:20:30";
        image2->writeMetadata();
    }

    gchar* uCollide = g_filename_to_uri(fCollide.c_str(), NULL, NULL);
    gchar* uNoDate = g_filename_to_uri(fNoDate.c_str(), NULL, NULL);
    gchar* uWithDate = g_filename_to_uri(fWithDate.c_str(), NULL, NULL);
    gchar* uNoChange = g_filename_to_uri(fNoChange.c_str(), NULL, NULL);

    std::vector<QuiverFile> files;
    files.push_back(QuiverFile(uCollide));
    files.push_back(QuiverFile(uNoDate));
    files.push_back(QuiverFile(uWithDate));
    files.push_back(QuiverFile(uNoChange));

    g_free(uCollide);
    g_free(uNoDate);
    g_free(uWithDate);
    g_free(uNoChange);

    RenameDlg rename;
    rename.SetFiles(files);

    struct FourTierCapture {
        bool checked;
        int64_t startMs;
    } cap{false, static_cast<int64_t>(g_get_monotonic_time() / 1000)};

    g_timeout_add(20, +[](gpointer data) -> gboolean {
        auto* c = static_cast<FourTierCapture*>(data);
        GtkWidget* win = FindToplevelByTitle("Rename");
        if (win != NULL)
        {
            std::vector<GtkWidget*> entries;
            FindWidgetsByType(win, GTK_TYPE_ENTRY, entries);
            for (GtkWidget* e : entries)
            {
                const char* cur = gtk_editable_get_text(GTK_EDITABLE(e));
                if (cur && strcmp(cur, "Item_###") != 0)
                {
                    gtk_editable_set_text(GTK_EDITABLE(e), "Item_###");
                    break;
                }
            }

            std::vector<GtkWidget*> labels;
            FindWidgetsByType(win, GTK_TYPE_LABEL, labels);
            for (GtkWidget* l : labels)
            {
                const char* txt = gtk_label_get_text(GTK_LABEL(l));
                if (txt != NULL && strstr(txt, "files will be renamed") != NULL)
                {
                    c->checked = true;
                    break;
                }
            }

            if (c->checked)
            {
                gtk_window_close(GTK_WINDOW(win));
                return G_SOURCE_REMOVE;
            }

            if (static_cast<int64_t>(g_get_monotonic_time() / 1000) - c->startMs > 5000)
            {
                gtk_window_close(GTK_WINDOW(win));
                return G_SOURCE_REMOVE;
            }
        }
        return G_SOURCE_CONTINUE;
    }, &cap);

    rename.Run();

    REQUIRE(cap.checked);
    REQUIRE(rename.GetPreviewItemCount() == 4);
    auto results = rename.GetConflictResults();
    REQUIRE(results.size() == 4);

    // 1. Conflict at top
    REQUIRE(results[0].HasConflict());
    REQUIRE(results[0].strSrcName == "orig_a.jpg");

    // 2. No metadata below conflict (for file that will be renamed)
    REQUIRE_FALSE(results[1].HasConflict());
    REQUIRE_FALSE(results[1].bHasDateMetadata);
    REQUIRE(results[1].strSrcName != results[1].strDstName);
    REQUIRE(results[1].strSrcName == "orig_b.jpg");

    // 3. No conflict (normal rename with date metadata) below no metadata
    REQUIRE_FALSE(results[2].HasConflict());
    REQUIRE(results[2].bHasDateMetadata);
    REQUIRE(results[2].strSrcName != results[2].strDstName);
    REQUIRE(results[2].strSrcName == "orig_c.jpg");

    // 4. 'No change' items at the very bottom
    REQUIRE_FALSE(results[3].HasConflict());
    REQUIRE(results[3].strSrcName == results[3].strDstName);
    REQUIRE(results[3].strSrcName == "Item_004.jpg");
    REQUIRE(rename.GetPreviewItemIssue(3) == "no change");

    g_unlink(fDiskExisting.c_str());
    g_unlink(fCollide.c_str());
    g_unlink(fNoDate.c_str());
    g_unlink(fWithDate.c_str());
    g_unlink(fNoChange.c_str());
    g_rmdir(tmpDir);
    g_free(tmpDir);
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
        std::vector<int> columnWidths;
        std::vector<bool> columnResizable;
    } cap{false, "", true, true, {}, {}, {}};

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
                    c->columnWidths.push_back(gtk_column_view_column_get_fixed_width(col));
                    c->columnResizable.push_back(gtk_column_view_column_get_resizable(col) == TRUE);
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

    // Column titles: "", "Original Name", "New Path", "New Name", "Issue"
    REQUIRE(cap.columnTitles.size() == 5);
    REQUIRE(cap.columnTitles[0] == "");
    REQUIRE(cap.columnTitles[1] == "Original Name");
    REQUIRE(cap.columnTitles[2] == "New Path");
    REQUIRE(cap.columnTitles[3] == "New Name");
    REQUIRE(cap.columnTitles[4] == "Issue");

    // Column widths must be fixed to prevent jumping when row counts change
    REQUIRE(cap.columnWidths.size() == 5);
    REQUIRE(cap.columnWidths[0] == 32);
    REQUIRE(cap.columnWidths[1] == 160);
    REQUIRE(cap.columnWidths[2] == 170);
    REQUIRE(cap.columnWidths[3] == 160);
    REQUIRE(cap.columnWidths[4] == 180);

    // Icon column non-resizable, data columns resizable
    REQUIRE(cap.columnResizable.size() == 5);
    REQUIRE_FALSE(cap.columnResizable[0]);
    REQUIRE(cap.columnResizable[1]);
    REQUIRE(cap.columnResizable[2]);
    REQUIRE(cap.columnResizable[3]);
    REQUIRE(cap.columnResizable[4]);
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


TEST_CASE("Organize Dialog: files without date metadata are flagged in the Issue column",
	"[unit][organize][gui][date]")
{
    REQUIRE_DISPLAY();

    // source holds one dated and one undated copy of the sample
    gchar* tmpSrcDir = g_dir_make_tmp("quiver_test_org_date_src_XXXXXX", NULL);
    gchar* tmpDstDir = g_dir_make_tmp("quiver_test_org_date_dst_XXXXXX", NULL);
    REQUIRE(tmpSrcDir != NULL);
    REQUIRE(tmpDstDir != NULL);

    std::string sampleJpg = QuiverTest_GetImagesDir() + "/sample_4k.jpg";
    REQUIRE(g_file_test(sampleJpg.c_str(), G_FILE_TEST_EXISTS));

    std::string datedFile = std::string(tmpSrcDir) + "/dated.jpg";
    std::string undatedFile = std::string(tmpSrcDir) + "/undated.jpg";
    char* contents = NULL;
    gsize length = 0;
    REQUIRE(g_file_get_contents(sampleJpg.c_str(), &contents, &length, NULL));
    REQUIRE(g_file_set_contents(datedFile.c_str(), contents, length, NULL));
    REQUIRE(g_file_set_contents(undatedFile.c_str(), contents, length, NULL));
    g_free(contents);

    {
        auto image = Exiv2::ImageFactory::open(datedFile.c_str());
        REQUIRE(image.get() != nullptr);
        image->readMetadata();
        image->exifData()["Exif.Photo.DateTimeOriginal"] = "2024:06:15 10:20:30";
        image->writeMetadata();
    }

    gchar* srcUri = g_filename_to_uri(tmpSrcDir, NULL, NULL);
    gchar* dstUri = g_filename_to_uri(tmpDstDir, NULL, NULL);
    REQUIRE(srcUri != NULL);
    REQUIRE(dstUri != NULL);

    char cfgPath[256];
    strncpy(cfgPath, "/tmp/quiver_test_org_date_XXXXXX.ini", sizeof(cfgPath) - 1);
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

    struct IssueCapture {
        bool sawReady;
        bool sawNoDateIssue;
        bool sawConflictIssue;
        int64_t startMs;
    } cap{false, false, false, static_cast<int64_t>(g_get_monotonic_time() / 1000)};

    g_timeout_add(10, +[](gpointer data) -> gboolean {
        auto* cap = static_cast<IssueCapture*>(data);
        GtkWidget* win = FindToplevelByTitle("Organize");
        if (win == NULL)
            return G_SOURCE_CONTINUE;

        std::vector<GtkWidget*> labels;
        FindWidgetsByType(win, GTK_TYPE_LABEL, labels);
        for (GtkWidget* l : labels)
        {
            const char* txt = gtk_label_get_text(GTK_LABEL(l));
            if (txt == NULL) continue;
            if (strstr(txt, "Ready:") != NULL)
                cap->sawReady = true;
            if (strstr(txt, "No date metadata") != NULL)
                cap->sawNoDateIssue = true;
            if (strstr(txt, "collide") != NULL)
                cap->sawConflictIssue = true;
        }

        if (cap->sawNoDateIssue ||
            static_cast<int64_t>(g_get_monotonic_time() / 1000) - cap->startMs > 5000)
        {
            gtk_window_close(GTK_WINDOW(win));
            return G_SOURCE_REMOVE;
        }
        return G_SOURCE_CONTINUE;
    }, &cap);

    OrganizeDlg organize;
    organize.SetInputFolder(srcUri);
    organize.Run();

    REQUIRE(cap.sawReady);
    REQUIRE(cap.sawNoDateIssue);
    // the dated file must not be flagged, and these two names do not collide
    REQUIRE_FALSE(cap.sawConflictIssue);

    Preferences::Reset();
    g_unlink(cfgPath);
    g_free(srcUri);
    g_free(dstUri);
    g_unlink(datedFile.c_str());
    g_unlink(undatedFile.c_str());
    g_rmdir(tmpSrcDir);
    g_rmdir(tmpDstDir);
    g_free(tmpSrcDir);
    g_free(tmpDstDir);
}

TEST_CASE("Organize Dialog: 'See All' button expands preview beyond row cap sampling", "[unit][organize][gui]")
{
    if (!QuiverTest_HasDisplay())
        SKIP("No display");

    gchar* tmpSrcDir = g_dir_make_tmp("quiver_test_org_see_all_src_XXXXXX", NULL);
    gchar* tmpDstDir = g_dir_make_tmp("quiver_test_org_see_all_dst_XXXXXX", NULL);
    REQUIRE(tmpSrcDir != nullptr);
    REQUIRE(tmpDstDir != nullptr);

    std::string sampleJpg = QuiverTest_GetImagesDir() + "/sample_4k.jpg";
    REQUIRE(g_file_test(sampleJpg.c_str(), G_FILE_TEST_EXISTS));

    char* contents = nullptr;
    gsize length = 0;
    REQUIRE(g_file_get_contents(sampleJpg.c_str(), &contents, &length, NULL));

    const int totalFiles = 205;
    std::vector<std::string> filePaths;
    filePaths.reserve(totalFiles);

    for (int i = 0; i < totalFiles; ++i)
    {
        char filename[64];
        g_snprintf(filename, sizeof(filename), "/img_%04d.jpg", i + 1);
        std::string fullPath = std::string(tmpSrcDir) + filename;
        REQUIRE(g_file_set_contents(fullPath.c_str(), contents, length, NULL));
        filePaths.push_back(fullPath);
    }
    g_free(contents);

    gchar* srcUri = g_filename_to_uri(tmpSrcDir, NULL, NULL);
    gchar* dstUri = g_filename_to_uri(tmpDstDir, NULL, NULL);
    REQUIRE(srcUri != NULL);
    REQUIRE(dstUri != NULL);

    char cfgPath[256];
    strncpy(cfgPath, "/tmp/quiver_test_org_see_all_XXXXXX.ini", sizeof(cfgPath) - 1);
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

    struct SeeAllCapture {
        bool checked;
        GtkWidget* btnSeeAll;
        guint initialCount;
        guint expandedCount;
        guint collapsedCount;
        std::string initialBtnLabel;
        std::string expandedBtnLabel;
        std::string collapsedBtnLabel;
        int64_t startMs;
    } cap{false, NULL, 0, 0, 0, "", "", "", static_cast<int64_t>(g_get_monotonic_time() / 1000)};

    OrganizeDlg organize;
    organize.SetInputFolder(srcUri);

    g_timeout_add(30, +[](gpointer data) -> gboolean {
        auto* c = static_cast<SeeAllCapture*>(data);
        GtkWidget* win = FindToplevelByTitle("Organize");
        if (win != NULL)
        {
            if (c->btnSeeAll == NULL)
            {
                std::vector<GtkWidget*> buttons;
                FindWidgetsByType(win, GTK_TYPE_BUTTON, buttons);
                for (GtkWidget* b : buttons)
                {
                    const char* lbl = gtk_button_get_label(GTK_BUTTON(b));
                    if (lbl && strcmp(lbl, "See All") == 0)
                    {
                        if (gtk_widget_get_visible(b))
                        {
                            c->btnSeeAll = b;
                            break;
                        }
                    }
                }
            }

            if (c->btnSeeAll != NULL && !c->checked)
            {
                std::vector<GtkWidget*> colViews;
                FindWidgetsByType(win, GTK_TYPE_COLUMN_VIEW, colViews);
                if (!colViews.empty())
                {
                    GListModel* model = G_LIST_MODEL(gtk_column_view_get_model(GTK_COLUMN_VIEW(colViews[0])));
                    c->initialCount = g_list_model_get_n_items(model);
                    c->initialBtnLabel = gtk_button_get_label(GTK_BUTTON(c->btnSeeAll));

                    // Click "See All"
                    g_signal_emit_by_name(c->btnSeeAll, "clicked");
                    c->expandedCount = g_list_model_get_n_items(model);
                    c->expandedBtnLabel = gtk_button_get_label(GTK_BUTTON(c->btnSeeAll));

                    // Click again to toggle back
                    g_signal_emit_by_name(c->btnSeeAll, "clicked");
                    c->collapsedCount = g_list_model_get_n_items(model);
                    c->collapsedBtnLabel = gtk_button_get_label(GTK_BUTTON(c->btnSeeAll));

                    c->checked = true;
                    gtk_window_close(GTK_WINDOW(win));
                    return G_SOURCE_REMOVE;
                }
            }

            if (static_cast<int64_t>(g_get_monotonic_time() / 1000) - c->startMs > 5000)
            {
                gtk_window_close(GTK_WINDOW(win));
                return G_SOURCE_REMOVE;
            }
        }
        return G_SOURCE_CONTINUE;
    }, &cap);

    organize.Run();

    REQUIRE(cap.checked);
    REQUIRE(cap.initialBtnLabel == "See All");
    REQUIRE(cap.initialCount == 201); // 200 items + 1 dummy item

    REQUIRE(cap.expandedBtnLabel == "Show Sample");
    REQUIRE(cap.expandedCount == 205);

    REQUIRE(cap.collapsedBtnLabel == "See All");
    REQUIRE(cap.collapsedCount == 201);

    for (const auto& p : filePaths)
    {
        g_unlink(p.c_str());
    }
    Preferences::Reset();
    g_unlink(cfgPath);
    g_free(srcUri);
    g_free(dstUri);
    g_rmdir(tmpSrcDir);
    g_rmdir(tmpDstDir);
    g_free(tmpSrcDir);
    g_free(tmpDstDir);
}

TEST_CASE("Organize Dialog: preview sorting hierarchy and day extension offset indication", "[unit][organize][gui][sort]")
{
    if (!QuiverTest_HasDisplay())
        SKIP("No display");

    gchar* tmpSrcDir = g_dir_make_tmp("quiver_test_org_sort_src_XXXXXX", NULL);
    gchar* tmpDstDir = g_dir_make_tmp("quiver_test_org_sort_dst_XXXXXX", NULL);
    REQUIRE(tmpSrcDir != nullptr);
    REQUIRE(tmpDstDir != nullptr);

    std::string sampleJpg = QuiverTest_GetImagesDir() + "/sample_4k.jpg";
    REQUIRE(g_file_test(sampleJpg.c_str(), G_FILE_TEST_EXISTS));

    char* contents = nullptr;
    gsize length = 0;
    REQUIRE(g_file_get_contents(sampleJpg.c_str(), &contents, &length, NULL));

    // Files to create:
    // 1. a_collide.jpg (EXIF: 2024:06:15 10:20:30) -> collides with pre-existing file in dest
    // 2. b_undated.jpg (no EXIF metadata)
    // 3. c_night.jpg (EXIF: 2024:06:15 01:15:00) -> 1:15 AM < 3 hours offset, lands in previous day
    // 4. d_day.jpg (EXIF: 2024:06:15 15:30:00) -> 3:30 PM >= 3 hours offset, normal organize
    std::string fCollide = std::string(tmpSrcDir) + "/a_collide.jpg";
    std::string fUndated = std::string(tmpSrcDir) + "/b_undated.jpg";
    std::string fNight = std::string(tmpSrcDir) + "/c_night.jpg";
    std::string fDay = std::string(tmpSrcDir) + "/d_day.jpg";

    REQUIRE(g_file_set_contents(fCollide.c_str(), contents, length, NULL));
    REQUIRE(g_file_set_contents(fUndated.c_str(), contents, length, NULL));
    {
        time_t now = time(NULL);
        struct tm tmNow;
        localtime_r(&now, &tmNow);
        tmNow.tm_hour = 12;
        tmNow.tm_min = 0;
        tmNow.tm_sec = 0;
        time_t noon = mktime(&tmNow);
        struct utimbuf utUndated = { noon, noon };
        utime(fUndated.c_str(), &utUndated);
    }
    REQUIRE(g_file_set_contents(fNight.c_str(), contents, length, NULL));
    REQUIRE(g_file_set_contents(fDay.c_str(), contents, length, NULL));

    // Pre-create conflicting file in destination (default template is YYYY/YYYY-MM-DD)
    std::string destSubdir = std::string(tmpDstDir) + "/2024/2024-06-15";
    REQUIRE(0 == g_mkdir_with_parents(destSubdir.c_str(), 0755));
    std::string fPreExisting = destSubdir + "/2024-06-15 10.20.30-01.jpg";
    REQUIRE(g_file_set_contents(fPreExisting.c_str(), contents, length, NULL));
    g_free(contents);

    // Set EXIF dates
    {
        auto imgCollide = Exiv2::ImageFactory::open(fCollide.c_str());
        REQUIRE(imgCollide.get() != nullptr);
        imgCollide->readMetadata();
        imgCollide->exifData()["Exif.Photo.DateTimeOriginal"] = "2024:06:15 10:20:30";
        imgCollide->writeMetadata();

        auto imgNight = Exiv2::ImageFactory::open(fNight.c_str());
        REQUIRE(imgNight.get() != nullptr);
        imgNight->readMetadata();
        imgNight->exifData()["Exif.Photo.DateTimeOriginal"] = "2024:06:15 01:15:00";
        imgNight->writeMetadata();

        auto imgDay = Exiv2::ImageFactory::open(fDay.c_str());
        REQUIRE(imgDay.get() != nullptr);
        imgDay->readMetadata();
        imgDay->exifData()["Exif.Photo.DateTimeOriginal"] = "2024:06:15 15:30:00";
        imgDay->writeMetadata();
    }

    gchar* srcUri = g_filename_to_uri(tmpSrcDir, NULL, NULL);
    gchar* dstUri = g_filename_to_uri(tmpDstDir, NULL, NULL);
    REQUIRE(srcUri != NULL);
    REQUIRE(dstUri != NULL);

    char cfgPath[256];
    strncpy(cfgPath, "/tmp/quiver_test_org_sort_XXXXXX.ini", sizeof(cfgPath) - 1);
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
    OrganizeDlg organize;
    organize.SetInputFolder(srcUri);

    struct SortCapture {
        OrganizeDlg* organize;
        bool checked;
        int64_t startMs;
    } cap{&organize, false, static_cast<int64_t>(g_get_monotonic_time() / 1000)};

    g_timeout_add(30, +[](gpointer data) -> gboolean {
        auto* c = static_cast<SortCapture*>(data);
        GtkWidget* win = FindToplevelByTitle("Organize");
        if (win != NULL)
        {
            if (c->organize->GetPreviewItemCount() == 4)
            {
                c->checked = true;
                gtk_window_close(GTK_WINDOW(win));
                return G_SOURCE_REMOVE;
            }

            if (static_cast<int64_t>(g_get_monotonic_time() / 1000) - c->startMs > 5000)
            {
                gtk_window_close(GTK_WINDOW(win));
                return G_SOURCE_REMOVE;
            }
        }
        return G_SOURCE_CONTINUE;
    }, &cap);

    organize.Run();

    REQUIRE(cap.checked);
    REQUIRE(organize.GetPreviewItemCount() == 4);
    auto results = organize.GetConflictResults();
    REQUIRE(results.size() == 4);

    // 1. Conflict at top (Rank 0)
    REQUIRE(results[0].HasConflict());
    REQUIRE(results[0].strSrcName == "a_collide.jpg");
    REQUIRE_FALSE(organize.GetPreviewItemIssue(0).empty());

    // 2. No date metadata (Rank 1)
    REQUIRE_FALSE(results[1].HasConflict());
    REQUIRE_FALSE(results[1].bHasDateMetadata);
    REQUIRE(results[1].strSrcName == "b_undated.jpg");
    REQUIRE(organize.GetPreviewItemIssue(1) == "No date metadata (uses file modification time)");

    // 3. Day extension offset applied (Rank 2)
    REQUIRE_FALSE(results[2].HasConflict());
    REQUIRE(results[2].bHasDateMetadata);
    REQUIRE(results[2].bDayExtensionApplied);
    REQUIRE(results[2].strSrcName == "c_night.jpg");
    REQUIRE(organize.GetPreviewItemIssue(2) == "Lands in previous day (offset applied)");

    // 4. Normal organize (Rank 3)
    REQUIRE_FALSE(results[3].HasConflict());
    REQUIRE(results[3].bHasDateMetadata);
    REQUIRE_FALSE(results[3].bDayExtensionApplied);
    REQUIRE(results[3].strSrcName == "d_day.jpg");
    REQUIRE(organize.GetPreviewItemIssue(3) == "");

    Preferences::Reset();
    g_unlink(cfgPath);
    g_free(srcUri);
    g_free(dstUri);
    g_unlink(fCollide.c_str());
    g_unlink(fUndated.c_str());
    g_unlink(fNight.c_str());
    g_unlink(fDay.c_str());
    g_unlink(fPreExisting.c_str());
    g_rmdir(destSubdir.c_str());
    std::string destYearDir = std::string(tmpDstDir) + "/2024";
    g_rmdir(destYearDir.c_str());
    g_rmdir(tmpSrcDir);
    g_rmdir(tmpDstDir);
    g_free(tmpSrcDir);
    g_free(tmpDstDir);
}

