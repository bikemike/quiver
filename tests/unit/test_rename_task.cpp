#include <catch2/catch_test_macros.hpp>
#include "RenameTask.h"
#include "QuiverFile.h"
#include "FileConflictCheck.h"
#include "test_helpers.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <string>
#include <vector>

TEST_CASE("RenameTask Variable Substitution Engine", "[unit][rename][task]")
{
    // Fix a known datetime: 2024-09-06 14:35:08
    GDateTime* dt = g_date_time_new_local(2024, 9, 6, 14, 35, 8);
    REQUIRE(dt != nullptr);

    SECTION("Standard Date and Time Format Specifiers")
    {
        // %Y -> 4-digit year
        REQUIRE(RenameTask::DoVariableSubstitution("%Y", dt, 0) == "2024");
        // %m -> 2-digit month
        REQUIRE(RenameTask::DoVariableSubstitution("%m", dt, 0) == "09");
        // %d -> 2-digit day of month
        REQUIRE(RenameTask::DoVariableSubstitution("%d", dt, 0) == "06");
        // %H -> 2-digit hour
        REQUIRE(RenameTask::DoVariableSubstitution("%H", dt, 0) == "14");
        // %M -> 2-digit minute
        REQUIRE(RenameTask::DoVariableSubstitution("%M", dt, 0) == "35");
        // %S -> 2-digit second
        REQUIRE(RenameTask::DoVariableSubstitution("%S", dt, 0) == "08");

        // Combined pattern
        std::string combined = RenameTask::DoVariableSubstitution("IMG_%Y%m%d_%H%M%S", dt, 0);
        REQUIRE(combined == "IMG_20240906_143508");
    }

    SECTION("Sequence Counter Zero-Padding Specifiers")
    {
        // Single '#' -> unpadded number
        REQUIRE(RenameTask::DoVariableSubstitution("photo_#", dt, 1) == "photo_1");
        REQUIRE(RenameTask::DoVariableSubstitution("photo_#", dt, 42) == "photo_42");

        // '##' -> 2-digit padded
        REQUIRE(RenameTask::DoVariableSubstitution("photo_##", dt, 1) == "photo_01");
        REQUIRE(RenameTask::DoVariableSubstitution("photo_##", dt, 42) == "photo_42");

        // '###' -> 3-digit padded
        REQUIRE(RenameTask::DoVariableSubstitution("photo_###", dt, 1) == "photo_001");
        REQUIRE(RenameTask::DoVariableSubstitution("photo_###", dt, 42) == "photo_042");

        // '####' -> 4-digit padded
        REQUIRE(RenameTask::DoVariableSubstitution("photo_####", dt, 7) == "photo_0007");

        // '########' -> 8-digit padded
        REQUIRE(RenameTask::DoVariableSubstitution("photo_########", dt, 99) == "photo_00000099");

        // Combined Date and Sequence
        std::string fullPattern = RenameTask::DoVariableSubstitution("Vacation_%Y-%m-%d_###", dt, 5);
        REQUIRE(fullPattern == "Vacation_2024-09-06_005");
    }

    SECTION("Literal Strings and Boundary Handling")
    {
        // Template with no substitution tokens remains unchanged
        REQUIRE(RenameTask::DoVariableSubstitution("FixedFilename", dt, 10) == "FixedFilename");
        // Empty template returns empty
        REQUIRE(RenameTask::DoVariableSubstitution("", dt, 1).empty());
    }

    g_date_time_unref(dt);
}

TEST_CASE("RenameTask Dry-Run Mapping and Conflict Check", "[unit][rename][task]")
{
    // Create an isolated temporary directory
    gchar* tmpDir = g_dir_make_tmp("quiver_test_rename_XXXXXX", NULL);
    REQUIRE(tmpDir != nullptr);

    std::string sampleJpg = QuiverTest_GetImagesDir() + "/sample_4k.jpg";
    REQUIRE(g_file_test(sampleJpg.c_str(), G_FILE_TEST_EXISTS));

    // Create test image files: img_b.jpg, img_a.jpg
    std::string fileB = std::string(tmpDir) + "/img_b.jpg";
    std::string fileA = std::string(tmpDir) + "/img_a.jpg";

    char* contents = nullptr;
    gsize length = 0;
    REQUIRE(g_file_get_contents(sampleJpg.c_str(), &contents, &length, NULL));
    REQUIRE(g_file_set_contents(fileB.c_str(), contents, length, NULL));
    REQUIRE(g_file_set_contents(fileA.c_str(), contents, length, NULL));
    g_free(contents);

    // Also create a subfolder to ensure folders are ignored
    std::string subfolder = std::string(tmpDir) + "/subfolder";
    REQUIRE(g_mkdir(subfolder.c_str(), 0755) == 0);

    gchar* tmpDirUri = g_filename_to_uri(tmpDir, NULL, NULL);
    REQUIRE(tmpDirUri != nullptr);

    SECTION("ComputeMappings Generates Correct Source to Destination Plan")
    {
        std::vector<FileConflictCheck::Mapping> mappings;
        bool ok = RenameTask::ComputeMappings(tmpDirUri, "Renamed_###",
                                              ImageList::SORT_BY_FILENAME, mappings);
        REQUIRE(ok == true);
        // Only the 2 images should be mapped; subfolder must be excluded
        REQUIRE(mappings.size() == 2);

        // Sorted by name: img_a.jpg mapped to Renamed_001.jpg, img_b.jpg to Renamed_002.jpg
        REQUIRE(mappings[0].strSrcURI.find("img_a.jpg") != std::string::npos);
        REQUIRE(mappings[0].strDstURI.find("Renamed_001.jpg") != std::string::npos);

        REQUIRE(mappings[1].strSrcURI.find("img_b.jpg") != std::string::npos);
        REQUIRE(mappings[1].strDstURI.find("Renamed_002.jpg") != std::string::npos);
    }

    SECTION("ComputeMappings Respects GCancellable")
    {
        GCancellable* cancellable = g_cancellable_new();
        g_cancellable_cancel(cancellable);

        std::vector<FileConflictCheck::Mapping> mappings;
        bool ok = RenameTask::ComputeMappings(tmpDirUri, "Renamed_###",
                                              ImageList::SORT_BY_FILENAME, mappings, cancellable);
        REQUIRE(ok == false);
        g_object_unref(cancellable);
    }

    // Cleanup
    g_free(tmpDirUri);
    g_unlink(fileA.c_str());
    g_unlink(fileB.c_str());
    g_rmdir(subfolder.c_str());
    g_rmdir(tmpDir);
    g_free(tmpDir);
}

TEST_CASE("RenameTask Lifecycle and Progress Metrics", "[unit][rename][task]")
{
    RenameTask task;

    REQUIRE(task.GetDescription() == "Renaming Images");
    REQUIRE(task.GetIterationTypeName(true, false) == "img");
    REQUIRE(task.GetIterationTypeName(true, true) == "imgs");
    REQUIRE(task.GetIterationTypeName(false, true) == "images");

    REQUIRE(task.GetTotalIterations() == 0);
    REQUIRE(task.GetCurrentIteration() == 0);
    REQUIRE(task.GetProgress() == 0.0);
}
