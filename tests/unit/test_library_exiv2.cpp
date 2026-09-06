#include <catch2/catch_test_macros.hpp>
#include <exiv2/exiv2.hpp>
#include "QuiverFile.h"
#include "test_helpers.h"
#include <glib.h>
#include <glib/gstdio.h>
#include <string>

TEST_CASE("Exiv2 Library Integration and EXIF Operations", "[lib][exiv2]")
{
    std::string imagesDir = QuiverTest_GetImagesDir();
    std::string sampleJpg = imagesDir + "/sample_4k.jpg";

    SECTION("Exiv2 directly opens and parses sample JPEG")
    {
        REQUIRE(g_file_test(sampleJpg.c_str(), G_FILE_TEST_EXISTS));

        auto image = Exiv2::ImageFactory::open(sampleJpg);
        REQUIRE(image.get() != nullptr);

        image->readMetadata();

        // Sample JPEG has standard image properties
        REQUIRE(image->pixelWidth() > 0);
        REQUIRE(image->pixelHeight() > 0);
        REQUIRE(image->mimeType() == "image/jpeg");
    }

    SECTION("QuiverFile handles images without EXIF gracefully")
    {
        gchar* uri = g_filename_to_uri(sampleJpg.c_str(), NULL, NULL);
        REQUIRE(uri != NULL);

        QuiverFile qf(uri);
        // sample_4k.jpg has no initial EXIF data; QuiverFile returns nullptr without crashing
        auto exifData = qf.GetExifData();
        REQUIRE(exifData == nullptr);

        g_free(uri);
    }

    SECTION("Exiv2 Tag Modification, Serialization, and QuiverFile Readback")
    {
        // Copy sample image to temp file for read/write verification
        char tmpPath[] = "/tmp/quiver_exif_test_XXXXXX.jpg";
        int fd = g_mkstemp(tmpPath);
        REQUIRE(fd >= 0);
        close(fd);

        // Copy bytes from sample_4k.jpg
        char* contents = nullptr;
        gsize length = 0;
        REQUIRE(g_file_get_contents(sampleJpg.c_str(), &contents, &length, NULL));
        REQUIRE(g_file_set_contents(tmpPath, contents, length, NULL));
        g_free(contents);

        // Inject EXIF tags using Exiv2
        {
            auto image = Exiv2::ImageFactory::open(tmpPath);
            REQUIRE(image.get() != nullptr);
            image->readMetadata();

            Exiv2::ExifData& exifData = image->exifData();
            exifData["Exif.Image.Software"] = "Quiver Catch2 Test Suite";
            exifData["Exif.Image.Artist"] = "DeepMind Pair";
            image->writeMetadata();
        }

        // Re-read file with Exiv2 and assert tags were persisted
        {
            auto image = Exiv2::ImageFactory::open(tmpPath);
            REQUIRE(image.get() != nullptr);
            image->readMetadata();

            Exiv2::ExifData& exifData = image->exifData();
            REQUIRE_FALSE(exifData.empty());
            REQUIRE(exifData["Exif.Image.Software"].toString() == "Quiver Catch2 Test Suite");
            REQUIRE(exifData["Exif.Image.Artist"].toString() == "DeepMind Pair");
        }

        // Verify QuiverFile now detects and loads the populated EXIF data
        {
            gchar* tmpUri = g_filename_to_uri(tmpPath, NULL, NULL);
            REQUIRE(tmpUri != NULL);

            QuiverFile qf(tmpUri);
            auto loadedExif = qf.GetExifData();
            REQUIRE(loadedExif != nullptr);
            REQUIRE((*loadedExif)["Exif.Image.Software"].toString() == "Quiver Catch2 Test Suite");
            REQUIRE((*loadedExif)["Exif.Image.Artist"].toString() == "DeepMind Pair");

            g_free(tmpUri);
        }

        g_unlink(tmpPath);
    }
}
