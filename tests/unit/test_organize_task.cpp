#include <catch2/catch_test_macros.hpp>
#include "OrganizeTask.h"
#include "RenameTask.h"
#include "QuiverFile.h"
#include "FileConflictCheck.h"
#include "test_helpers.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <string>
#include <vector>

TEST_CASE("OrganizeTask Variable Substitution and Day Extension", "[unit][organize][task]")
{
    // Date: 2024-09-06 02:30:00 (early morning)
    GDateTime* dtEarly = g_date_time_new_local(2024, 9, 6, 2, 30, 0);
    REQUIRE(dtEarly != nullptr);

    SECTION("Standard Date Token Substitution (YYYY, MM, DD)")
    {
        REQUIRE(OrganizeTask::DoVariableSubstitution("YYYY", dtEarly, 0) == "2024");
        REQUIRE(OrganizeTask::DoVariableSubstitution("MM", dtEarly, 0) == "09");
        REQUIRE(OrganizeTask::DoVariableSubstitution("DD", dtEarly, 0) == "06");
        REQUIRE(OrganizeTask::DoVariableSubstitution("YYYY/MM/DD", dtEarly, 0) == "2024/09/06");
        REQUIRE(OrganizeTask::DoVariableSubstitution("Photos_YYYY-MM", dtEarly, 0) == "Photos_2024-09");
    }

    SECTION("Day Extension Shifts Early Morning Hours to Previous Day")
    {
        // Without dayExtension (0 hours): September 6th
        std::string dateNoExt = OrganizeTask::DoVariableSubstitution("YYYY-MM-DD", dtEarly, 0);
        REQUIRE(dateNoExt == "2024-09-06");

        // With dayExtension = 4 hours: 02:30 minus 4 hours = 22:30 on September 5th!
        std::string dateExt4 = OrganizeTask::DoVariableSubstitution("YYYY-MM-DD", dtEarly, 4);
        REQUIRE(dateExt4 == "2024-09-05");

        // With dayExtension = 2 hours: 02:30 minus 2 hours = 00:30 on September 6th
        std::string dateExt2 = OrganizeTask::DoVariableSubstitution("YYYY-MM-DD", dtEarly, 2);
        REQUIRE(dateExt2 == "2024-09-06");

        // With dayExtension = 3 hours: 02:30 minus 3 hours = 23:30 on September 5th
        std::string dateExt3 = OrganizeTask::DoVariableSubstitution("YYYY-MM-DD", dtEarly, 3);
        REQUIRE(dateExt3 == "2024-09-05");
    }

    SECTION("Day Extension Leaves Later Times on Same Day")
    {
        // 08:00 AM minus 4 hours = 04:00 AM on the same day (2024-09-06)
        GDateTime* dtMorning = g_date_time_new_local(2024, 9, 6, 8, 0, 0);
        std::string dateMorning = OrganizeTask::DoVariableSubstitution("YYYY-MM-DD", dtMorning, 4);
        REQUIRE(dateMorning == "2024-09-06");
        g_date_time_unref(dtMorning);
    }

    g_date_time_unref(dtEarly);
}

TEST_CASE("OrganizeTask Options and Dry-Run ComputeMappings", "[unit][organize][task]")
{
    OrganizeTask::Options opts;
    REQUIRE(opts.bIncludeSubfolders == false);
    REQUIRE(opts.bRenameFiles == false);
    REQUIRE(opts.iDayExtension == 0);

    // Create temp source and destination directories
    gchar* tmpSrcDir = g_dir_make_tmp("quiver_test_org_src_XXXXXX", NULL);
    gchar* tmpDstDir = g_dir_make_tmp("quiver_test_org_dst_XXXXXX", NULL);
    REQUIRE(tmpSrcDir != nullptr);
    REQUIRE(tmpDstDir != nullptr);

    std::string sampleJpg = QuiverTest_GetImagesDir() + "/sample_4k.jpg";
    REQUIRE(g_file_test(sampleJpg.c_str(), G_FILE_TEST_EXISTS));

    // Place sample test file in source directory
    std::string srcFile = std::string(tmpSrcDir) + "/sample.jpg";
    char* contents = nullptr;
    gsize length = 0;
    REQUIRE(g_file_get_contents(sampleJpg.c_str(), &contents, &length, NULL));
    REQUIRE(g_file_set_contents(srcFile.c_str(), contents, length, NULL));
    g_free(contents);

    gchar* srcUri = g_filename_to_uri(tmpSrcDir, NULL, NULL);
    gchar* dstUri = g_filename_to_uri(tmpDstDir, NULL, NULL);
    REQUIRE(srcUri != nullptr);
    REQUIRE(dstUri != nullptr);

    opts.strSrcDirURI = srcUri;
    opts.strDestDirURI = dstUri;
    opts.strFolderTemplate = "YYYY/MM";
    opts.bRenameFiles = false;

    SECTION("ComputeMappings builds folder destination hierarchy")
    {
        std::vector<FileConflictCheck::Mapping> mappings;
        bool ok = OrganizeTask::ComputeMappings(opts, mappings);
        REQUIRE(ok == true);
        REQUIRE(mappings.size() == 1);

        REQUIRE(mappings[0].strSrcURI.find("sample.jpg") != std::string::npos);
        // Destination should be routed to destDir/YYYY/MM/sample.jpg
        REQUIRE(mappings[0].strDstURI.find(dstUri) == 0);
        REQUIRE(mappings[0].strDstURI.find("sample.jpg") != std::string::npos);
    }

    SECTION("ComputeMappings with file rename template")
    {
        opts.bRenameFiles = true;
        opts.strFileTemplate = "Photo_###";

        std::vector<FileConflictCheck::Mapping> mappings;
        bool ok = OrganizeTask::ComputeMappings(opts, mappings);
        REQUIRE(ok == true);
        REQUIRE(mappings.size() == 1);
        REQUIRE(mappings[0].strDstURI.find("Photo_001.jpg") != std::string::npos);
    }

    SECTION("ComputeMappings respects GCancellable")
    {
        GCancellable* cancellable = g_cancellable_new();
        g_cancellable_cancel(cancellable);

        std::vector<FileConflictCheck::Mapping> mappings;
        bool ok = OrganizeTask::ComputeMappings(opts, mappings, cancellable);
        REQUIRE(ok == false);
        g_object_unref(cancellable);
    }

    // Cleanup
    g_free(srcUri);
    g_free(dstUri);
    g_unlink(srcFile.c_str());
    g_rmdir(tmpSrcDir);
    g_rmdir(tmpDstDir);
    g_free(tmpSrcDir);
    g_free(tmpDstDir);
}

TEST_CASE("OrganizeTask Lifecycle and Interface", "[unit][organize][task]")
{
    boost::shared_ptr<OrganizeTask> task(new OrganizeTask());

    REQUIRE(task->GetDescription() == "Organizing Images");
    REQUIRE(task->GetIterationTypeName(true, false) == "img");
    REQUIRE(task->GetIterationTypeName(true, true) == "imgs");
    REQUIRE(task->GetIterationTypeName(false, true) == "images");

    REQUIRE(task->GetTotalIterations() == 0);
    REQUIRE(task->GetCurrentIteration() == 0);
    REQUIRE(task->GetProgress() == 0.0);

    // Cancel before run should be safe
    task->Cancel();
}
