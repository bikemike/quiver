#include <catch2/catch_test_macros.hpp>
#include "AdjustDateTask.h"
#include "QuiverFile.h"
#include "test_helpers.h"

#include <exiv2/exiv2.hpp>
#include <glib.h>
#include <glib/gstdio.h>
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
