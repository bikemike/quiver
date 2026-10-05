#include <catch2/catch_test_macros.hpp>
#include "AdjustDateTask.h"
#include "QuiverFile.h"
#include "test_helpers.h"

#include <exiv2/exiv2.hpp>
#include <glib.h>
#include <glib/gstdio.h>
#include <sys/stat.h>
#include <utime.h>
#include <string>
#include <ctime>

TEST_CASE("AdjustDateTask Construction and Field Flag Bitmasks", "[unit][adjust_date][task]")
{
    boost::shared_ptr<AdjustDateTask> taskRel(new AdjustDateTask(1, 2, 3, 4, 5));

    REQUIRE(taskRel->GetDescription() == "Adjust Exif Date");
    REQUIRE(taskRel->GetIterationTypeName(true, false) == "img");
    REQUIRE(taskRel->GetIterationTypeName(true, true) == "imgs");
    REQUIRE(taskRel->GetIterationTypeName(false, true) == "images");

    REQUIRE(taskRel->GetTotalIterations() == 0);
    REQUIRE(taskRel->GetCurrentIteration() == 0);
    REQUIRE(taskRel->GetProgress() == 0.0);

    // Bitmask field selection
    taskRel->SetAdjustDateFields(AdjustDateTask::DATE_FIELD_EXIF_DATE_TIME_ORIG);
    taskRel->AddAdjustDateFields(AdjustDateTask::DATE_FIELD_MODIFICATION_TIME);
    taskRel->AddAdjustDateFields(AdjustDateTask::DATE_FIELD_EXIF_DATE_TIME_DIGITIZED);

    struct tm tmAbs = {};
    tmAbs.tm_year = 124; // 2024
    tmAbs.tm_mon = 8;    // September
    tmAbs.tm_mday = 6;
    tmAbs.tm_hour = 12;
    boost::shared_ptr<AdjustDateTask> taskAbs(new AdjustDateTask(tmAbs));
    REQUIRE(taskAbs->GetDescription() == "Adjust Exif Date");
}

TEST_CASE("AdjustDateTask Relative Date Adjustment on Real File", "[unit][adjust_date][task]")
{
    // Prepare a temporary copy of sample_4k.jpg
    char tmpPath[] = "/tmp/quiver_test_adjust_date_XXXXXX.jpg";
    int fd = g_mkstemp(tmpPath);
    REQUIRE(fd >= 0);
    close(fd);

    std::string sampleJpg = QuiverTest_GetImagesDir() + "/sample_4k.jpg";
    REQUIRE(g_file_test(sampleJpg.c_str(), G_FILE_TEST_EXISTS));

    char* contents = nullptr;
    gsize length = 0;
    REQUIRE(g_file_get_contents(sampleJpg.c_str(), &contents, &length, NULL));
    REQUIRE(g_file_set_contents(tmpPath, contents, length, NULL));
    g_free(contents);

    // Set initial known EXIF date
    {
        auto image = Exiv2::ImageFactory::open(tmpPath);
        REQUIRE(image.get() != nullptr);
        image->readMetadata();
        Exiv2::ExifData& exif = image->exifData();
        exif["Exif.Photo.DateTimeOriginal"] = "2024:06:15 10:20:30";
        exif["Exif.Image.DateTime"] = "2024:06:15 10:20:30";
        image->writeMetadata();
    }

    gchar* fileUri = g_filename_to_uri(tmpPath, NULL, NULL);
    REQUIRE(fileUri != nullptr);

    SECTION("Relative adjustment (+1 yr, +5 days, +2 hrs, +10 mins, +15 secs)")
    {
        boost::shared_ptr<AdjustDateTask> task(new AdjustDateTask(1, 5, 2, 10, 15));
        task->SetAdjustDateFields(AdjustDateTask::DATE_FIELD_EXIF_DATE_TIME_ORIG);

        QuiverFile qf(fileUri);
        task->AddFile(qf);
        REQUIRE(task->GetTotalIterations() == 1);

        task->RunTask();
        REQUIRE(task->IsFinished());

        // Verify with Exiv2
        auto image = Exiv2::ImageFactory::open(tmpPath);
        REQUIRE(image.get() != nullptr);
        image->readMetadata();
        std::string newDate = image->exifData()["Exif.Photo.DateTimeOriginal"].toString();

        // 2024:06:15 10:20:30 + 1y 5d 2h 10m 15s = 2025:06:20 12:30:45
        REQUIRE(newDate == "2025:06:20 12:30:45");
    }

    SECTION("Relative adjustment with calendar rollover across months and days")
    {
        // Set date to Jan 31 at 23:45
        {
            auto image = Exiv2::ImageFactory::open(tmpPath);
            image->readMetadata();
            image->exifData()["Exif.Photo.DateTimeOriginal"] = "2024:01:31 23:45:00";
            image->writeMetadata();
        }

        // Add 0 years, 1 day, 1 hour, 30 minutes
        // 23:45 + 1h 30m = 01:15 on Feb 1 + 1 day = Feb 2
        boost::shared_ptr<AdjustDateTask> task(new AdjustDateTask(0, 1, 1, 30, 0));
        task->SetAdjustDateFields(AdjustDateTask::DATE_FIELD_EXIF_DATE_TIME_ORIG);

        QuiverFile qf(fileUri);
        task->AddFile(qf);
        task->RunTask();

        auto image = Exiv2::ImageFactory::open(tmpPath);
        image->readMetadata();
        std::string newDate = image->exifData()["Exif.Photo.DateTimeOriginal"].toString();
        REQUIRE(newDate == "2024:02:02 01:15:00");
    }

    g_free(fileUri);
    g_unlink(tmpPath);
}

TEST_CASE("AdjustDateTask Absolute Date Mode on Real File", "[unit][adjust_date][task]")
{
    // Prepare a temporary copy of sample_4k.jpg
    char tmpPath[] = "/tmp/quiver_test_adjust_date_abs_XXXXXX.jpg";
    int fd = g_mkstemp(tmpPath);
    REQUIRE(fd >= 0);
    close(fd);

    std::string sampleJpg = QuiverTest_GetImagesDir() + "/sample_4k.jpg";
    REQUIRE(g_file_test(sampleJpg.c_str(), G_FILE_TEST_EXISTS));

    char* contents = nullptr;
    gsize length = 0;
    REQUIRE(g_file_get_contents(sampleJpg.c_str(), &contents, &length, NULL));
    REQUIRE(g_file_set_contents(tmpPath, contents, length, NULL));
    g_free(contents);

    gchar* fileUri = g_filename_to_uri(tmpPath, NULL, NULL);
    REQUIRE(fileUri != nullptr);

    SECTION("Set explicit fixed date across EXIF DateTime, DateTimeOriginal, and DateTimeDigitized")
    {
        struct tm tmAbs = {};
        tmAbs.tm_year = 126; // 2026
        tmAbs.tm_mon = 11;   // December (0-based)
        tmAbs.tm_mday = 25;  // 25th
        tmAbs.tm_hour = 10;
        tmAbs.tm_min = 30;
        tmAbs.tm_sec = 45;

        boost::shared_ptr<AdjustDateTask> task(new AdjustDateTask(tmAbs));
        task->SetAdjustDateFields((AdjustDateTask::DateFields)(
            AdjustDateTask::DATE_FIELD_EXIF_DATE_TIME |
            AdjustDateTask::DATE_FIELD_EXIF_DATE_TIME_ORIG |
            AdjustDateTask::DATE_FIELD_EXIF_DATE_TIME_DIGITIZED |
            AdjustDateTask::DATE_FIELD_MODIFICATION_TIME));

        QuiverFile qf(fileUri);
        task->AddFile(qf);
        task->RunTask();
        REQUIRE(task->IsFinished());

        // Verify with Exiv2
        auto image = Exiv2::ImageFactory::open(tmpPath);
        REQUIRE(image.get() != nullptr);
        image->readMetadata();

        std::string orig = image->exifData()["Exif.Photo.DateTimeOriginal"].toString();
        std::string imgDt = image->exifData()["Exif.Image.DateTime"].toString();
        std::string dig = image->exifData()["Exif.Photo.DateTimeDigitized"].toString();

        REQUIRE(orig == "2026:12:25 10:30:45");
        REQUIRE(imgDt == "2026:12:25 10:30:45");
        REQUIRE(dig == "2026:12:25 10:30:45");
    }

    g_free(fileUri);
    g_unlink(tmpPath);
}

TEST_CASE("AdjustDateTask Video Absolute Date Mode", "[unit][adjust_date][task][video]")
{
    char tmpPath[] = "/tmp/quiver_test_adjust_video_abs_XXXXXX.mp4";
    int fd = g_mkstemp(tmpPath);
    REQUIRE(fd >= 0);
    close(fd);

    std::string sampleVideo = QuiverTest_GetImagesDir() + "/sample_video.mp4";
    REQUIRE(g_file_test(sampleVideo.c_str(), G_FILE_TEST_EXISTS));

    char* contents = nullptr;
    gsize length = 0;
    REQUIRE(g_file_get_contents(sampleVideo.c_str(), &contents, &length, NULL));
    REQUIRE(g_file_set_contents(tmpPath, contents, length, NULL));
    g_free(contents);

    gchar* fileUri = g_filename_to_uri(tmpPath, NULL, NULL);
    REQUIRE(fileUri != nullptr);

    SECTION("Set explicit fixed date on video container metadata")
    {
        struct tm tmAbs = {};
        tmAbs.tm_year = 126; // 2026
        tmAbs.tm_mon = 11;   // December (0-based)
        tmAbs.tm_mday = 25;  // 25th
        tmAbs.tm_hour = 10;
        tmAbs.tm_min = 30;
        tmAbs.tm_sec = 45;
        tmAbs.tm_isdst = -1;
        time_t expectedEpoch = mktime(&tmAbs);

        boost::shared_ptr<AdjustDateTask> task(new AdjustDateTask(tmAbs));
        task->SetAdjustDateFields(AdjustDateTask::DATE_FIELD_EXIF_DATE_TIME);

        QuiverFile qf(fileUri);
        task->AddFile(qf);
        task->RunTask();
        REQUIRE(task->IsFinished());

        // Verify with a fresh QuiverFile
        QuiverFile result(fileUri);
        REQUIRE(result.HasDateMetadata());
        REQUIRE(result.GetTimeT(true) == expectedEpoch);
    }

    g_free(fileUri);
    g_unlink(tmpPath);
}

TEST_CASE("AdjustDateTask Video Relative Date Adjustment", "[unit][adjust_date][task][video]")
{
    char tmpPath[] = "/tmp/quiver_test_adjust_video_rel_XXXXXX.mp4";
    int fd = g_mkstemp(tmpPath);
    REQUIRE(fd >= 0);
    close(fd);

    std::string sampleVideo = QuiverTest_GetImagesDir() + "/sample_video.mp4";
    REQUIRE(g_file_test(sampleVideo.c_str(), G_FILE_TEST_EXISTS));

    char* contents = nullptr;
    gsize length = 0;
    REQUIRE(g_file_get_contents(sampleVideo.c_str(), &contents, &length, NULL));
    REQUIRE(g_file_set_contents(tmpPath, contents, length, NULL));
    g_free(contents);

    gchar* fileUri = g_filename_to_uri(tmpPath, NULL, NULL);
    REQUIRE(fileUri != nullptr);

    SECTION("Relative adjustment (+1 yr, +5 days, +2 hrs, +10 mins, +15 secs)")
    {
        // First set known base date: 2024:06:15 10:20:30
        struct tm tmBase = {};
        tmBase.tm_year = 124; // 2024
        tmBase.tm_mon = 5;    // June (0-based)
        tmBase.tm_mday = 15;
        tmBase.tm_hour = 10;
        tmBase.tm_min = 20;
        tmBase.tm_sec = 30;
        tmBase.tm_isdst = -1;

        boost::shared_ptr<AdjustDateTask> baseTask(new AdjustDateTask(tmBase));
        baseTask->SetAdjustDateFields(AdjustDateTask::DATE_FIELD_EXIF_DATE_TIME);
        QuiverFile qfBase(fileUri);
        baseTask->AddFile(qfBase);
        baseTask->RunTask();
        REQUIRE(baseTask->IsFinished());

        // Now adjust relative: +1y 5d 2h 10m 15s
        boost::shared_ptr<AdjustDateTask> adjTask(new AdjustDateTask(1, 5, 2, 10, 15));
        adjTask->SetAdjustDateFields(AdjustDateTask::DATE_FIELD_EXIF_DATE_TIME);
        QuiverFile qfAdj(fileUri);
        adjTask->AddFile(qfAdj);
        adjTask->RunTask();
        REQUIRE(adjTask->IsFinished());

        // Expected: 2025:06:20 12:30:45
        struct tm tmExpected = {};
        tmExpected.tm_year = 125; // 2025
        tmExpected.tm_mon = 5;    // June
        tmExpected.tm_mday = 20;
        tmExpected.tm_hour = 12;
        tmExpected.tm_min = 30;
        tmExpected.tm_sec = 45;
        tmExpected.tm_isdst = -1;
        time_t expectedEpoch = mktime(&tmExpected);

        QuiverFile result(fileUri);
        REQUIRE(result.HasDateMetadata());
        REQUIRE(result.GetTimeT(true) == expectedEpoch);
    }

    g_free(fileUri);
    g_unlink(tmpPath);
}

TEST_CASE("AdjustDateTask Video Modification Time Preservation vs Update", "[unit][adjust_date][task][video]")
{
    char tmpPath[] = "/tmp/quiver_test_adjust_video_mtime_XXXXXX.mp4";
    int fd = g_mkstemp(tmpPath);
    REQUIRE(fd >= 0);
    close(fd);

    std::string sampleVideo = QuiverTest_GetImagesDir() + "/sample_video.mp4";
    REQUIRE(g_file_test(sampleVideo.c_str(), G_FILE_TEST_EXISTS));

    char* contents = nullptr;
    gsize length = 0;
    REQUIRE(g_file_get_contents(sampleVideo.c_str(), &contents, &length, NULL));
    REQUIRE(g_file_set_contents(tmpPath, contents, length, NULL));
    g_free(contents);

    // Set known historical mtime on the file: 2020-01-01 00:00:00 UTC = 1577836800
    const time_t fixedMTime = 1577836800;
    struct utimbuf tb = {};
    tb.actime = fixedMTime;
    tb.modtime = fixedMTime;
    REQUIRE(utime(tmpPath, &tb) == 0);

    gchar* fileUri = g_filename_to_uri(tmpPath, NULL, NULL);
    REQUIRE(fileUri != nullptr);

    SECTION("Preserve modification time when DATE_FIELD_MODIFICATION_TIME is not set")
    {
        struct tm tmAbs = {};
        tmAbs.tm_year = 126; // 2026
        tmAbs.tm_mon = 5;
        tmAbs.tm_mday = 10;
        tmAbs.tm_hour = 14;
        tmAbs.tm_min = 0;
        tmAbs.tm_sec = 0;
        tmAbs.tm_isdst = -1;
        time_t expectedMetaEpoch = mktime(&tmAbs);

        boost::shared_ptr<AdjustDateTask> task(new AdjustDateTask(tmAbs));
        // ONLY EXIF_DATE_TIME, no MODIFICATION_TIME
        task->SetAdjustDateFields(AdjustDateTask::DATE_FIELD_EXIF_DATE_TIME);

        QuiverFile qf(fileUri);
        task->AddFile(qf);
        task->RunTask();
        REQUIRE(task->IsFinished());

        QuiverFile result(fileUri);
        REQUIRE(result.HasDateMetadata());
        REQUIRE(result.GetTimeT(true) == expectedMetaEpoch);

        // Check mtime on disk is preserved
        struct stat st = {};
        REQUIRE(stat(tmpPath, &st) == 0);
        REQUIRE(st.st_mtime == fixedMTime);
    }

    SECTION("Update modification time when DATE_FIELD_MODIFICATION_TIME is set")
    {
        struct tm tmAbs = {};
        tmAbs.tm_year = 126; // 2026
        tmAbs.tm_mon = 5;
        tmAbs.tm_mday = 10;
        tmAbs.tm_hour = 14;
        tmAbs.tm_min = 0;
        tmAbs.tm_sec = 0;
        tmAbs.tm_isdst = -1;
        time_t expectedEpoch = mktime(&tmAbs);

        boost::shared_ptr<AdjustDateTask> task(new AdjustDateTask(tmAbs));
        task->SetAdjustDateFields((AdjustDateTask::DateFields)(
            AdjustDateTask::DATE_FIELD_EXIF_DATE_TIME |
            AdjustDateTask::DATE_FIELD_MODIFICATION_TIME));

        QuiverFile qf(fileUri);
        task->AddFile(qf);
        task->RunTask();
        REQUIRE(task->IsFinished());

        QuiverFile result(fileUri);
        REQUIRE(result.HasDateMetadata());
        REQUIRE(result.GetTimeT(true) == expectedEpoch);

        // Check mtime on disk was updated to expectedEpoch
        struct stat st = {};
        REQUIRE(stat(tmpPath, &st) == 0);
        REQUIRE(st.st_mtime == expectedEpoch);
    }

    g_free(fileUri);
    g_unlink(tmpPath);
}

TEST_CASE("AdjustDateTask Mixed Batch of Images and Videos", "[unit][adjust_date][task][video]")
{
    char tmpImgPath[] = "/tmp/quiver_test_adjust_mixed_XXXXXX.jpg";
    int fdImg = g_mkstemp(tmpImgPath);
    REQUIRE(fdImg >= 0);
    close(fdImg);

    char tmpVidPath[] = "/tmp/quiver_test_adjust_mixed_XXXXXX.mp4";
    int fdVid = g_mkstemp(tmpVidPath);
    REQUIRE(fdVid >= 0);
    close(fdVid);

    std::string sampleJpg = QuiverTest_GetImagesDir() + "/sample_4k.jpg";
    std::string sampleVid = QuiverTest_GetImagesDir() + "/sample_video.mp4";
    REQUIRE(g_file_test(sampleJpg.c_str(), G_FILE_TEST_EXISTS));
    REQUIRE(g_file_test(sampleVid.c_str(), G_FILE_TEST_EXISTS));

    char* cImg = nullptr;
    gsize lenImg = 0;
    REQUIRE(g_file_get_contents(sampleJpg.c_str(), &cImg, &lenImg, NULL));
    REQUIRE(g_file_set_contents(tmpImgPath, cImg, lenImg, NULL));
    g_free(cImg);

    char* cVid = nullptr;
    gsize lenVid = 0;
    REQUIRE(g_file_get_contents(sampleVid.c_str(), &cVid, &lenVid, NULL));
    REQUIRE(g_file_set_contents(tmpVidPath, cVid, lenVid, NULL));
    g_free(cVid);

    gchar* uriImg = g_filename_to_uri(tmpImgPath, NULL, NULL);
    gchar* uriVid = g_filename_to_uri(tmpVidPath, NULL, NULL);
    REQUIRE(uriImg != nullptr);
    REQUIRE(uriVid != nullptr);

    struct tm tmAbs = {};
    tmAbs.tm_year = 127; // 2027
    tmAbs.tm_mon = 2;    // March
    tmAbs.tm_mday = 14;
    tmAbs.tm_hour = 9;
    tmAbs.tm_min = 26;
    tmAbs.tm_sec = 53;
    tmAbs.tm_isdst = -1;
    time_t expectedEpoch = mktime(&tmAbs);

    boost::shared_ptr<AdjustDateTask> task(new AdjustDateTask(tmAbs));
    task->SetAdjustDateFields((AdjustDateTask::DateFields)(
        AdjustDateTask::DATE_FIELD_EXIF_DATE_TIME |
        AdjustDateTask::DATE_FIELD_EXIF_DATE_TIME_ORIG |
        AdjustDateTask::DATE_FIELD_MODIFICATION_TIME));

    QuiverFile qfImg(uriImg);
    QuiverFile qfVid(uriVid);
    task->AddFile(qfImg);
    task->AddFile(qfVid);
    REQUIRE(task->GetTotalIterations() == 2);

    task->RunTask();
    REQUIRE(task->IsFinished());

    // Verify both files
    QuiverFile resImg(uriImg);
    QuiverFile resVid(uriVid);

    REQUIRE(resImg.GetTimeT(true) == expectedEpoch);
    REQUIRE(resVid.GetTimeT(true) == expectedEpoch);

    g_free(uriImg);
    g_free(uriVid);
    g_unlink(tmpImgPath);
    g_unlink(tmpVidPath);
}
