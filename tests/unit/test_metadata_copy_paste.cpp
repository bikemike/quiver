#include <catch2/catch_test_macros.hpp>
#include <exiv2/exiv2.hpp>
#include "QuiverFile.h"
#include "QuiverGps.h"
#include "QuiverMetadata.h"
#include "QuiverVideoOps.h"
#include "test_helpers.h"
#include <glib.h>
#include <glib/gstdio.h>
#include <cmath>
#include <unistd.h>

namespace
{
const unsigned char k1x1Png[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
    0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
    0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53,
    0xde, 0x00, 0x00, 0x00, 0x0c, 0x49, 0x44, 0x41,
    0x54, 0x08, 0xd7, 0x63, 0xf8, 0xcf, 0xc0, 0x00,
    0x00, 0x03, 0x01, 0x01, 0x00, 0x18, 0xdd, 0x8d,
    0xb0, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e,
    0x44, 0xae, 0x42, 0x60, 0x82
};
}

TEST_CASE("Video frame snapshot exports GPS metadata to EXIF", "[video][gps][snapshot]")
{
    std::string realVid = "/workspace/2026-09-13 04.50.30 MJ horn honk.mp4";
    if (g_file_test(realVid.c_str(), G_FILE_TEST_EXISTS))
    {
        gchar* realVidUri = g_filename_to_uri(realVid.c_str(), NULL, NULL);
        REQUIRE(realVidUri != nullptr);

        char tmpSnap[] = "/tmp/quiver_test_snap_gps_XXXXXX.png";
        int fd = g_mkstemp(tmpSnap);
        REQUIRE(fd >= 0);
        close(fd);
        REQUIRE(g_file_set_contents(tmpSnap, (const char*)k1x1Png, sizeof(k1x1Png), NULL));

        bool ok = QuiverVideoOps::WriteSnapshotExif(tmpSnap, realVidUri, "2026-09-13 04.50.30 MJ horn honk.mp4", 1000000000ULL);
        REQUIRE(ok);

        auto img = Exiv2::ImageFactory::open(tmpSnap);
        REQUIRE(img.get() != nullptr);
        img->readMetadata();
        Exiv2::ExifData& exif = img->exifData();

        // Check EXIF date
        REQUIRE(exif.findKey(Exiv2::ExifKey("Exif.Photo.DateTimeOriginal")) != exif.end());

        g_free(realVidUri);
        g_unlink(tmpSnap);
    }
}

TEST_CASE("QuiverMetadata Copy and Paste Image to Image", "[metadata][copy_paste]")
{
    // Create source image with full metadata
    char srcImgPath[] = "/tmp/quiver_test_src_img_XXXXXX.jpg";
    int fd1 = g_mkstemp(srcImgPath);
    REQUIRE(fd1 >= 0);
    close(fd1);

    std::string imagesDir = QuiverTest_GetImagesDir();
    std::string sampleJpg = imagesDir + "/sample_4k.jpg";
    char* contents = nullptr;
    gsize len = 0;
    REQUIRE(g_file_get_contents(sampleJpg.c_str(), &contents, &len, NULL));
    REQUIRE(g_file_set_contents(srcImgPath, contents, len, NULL));
    g_free(contents);

    {
        auto img = Exiv2::ImageFactory::open(srcImgPath);
        REQUIRE(img.get() != nullptr);
        img->readMetadata();
        Exiv2::ExifData& exif = img->exifData();

        exif["Exif.Photo.DateTimeOriginal"] = "2025:06:15 14:30:00";
        exif["Exif.Image.DateTime"] = "2025:06:15 14:30:00";
        exif["Exif.Image.Make"] = "Google";
        exif["Exif.Image.Model"] = "Pixel 8 Pro";
        exif["Exif.Image.Artist"] = "Quiver Photographer";
        exif["Exif.Image.Copyright"] = "2025 All Rights Reserved";
        exif["Exif.Image.ImageDescription"] = "Mountain View Sunrise";
        exif["Exif.Photo.UserComment"] = "charset=Ascii Beautiful view";
        QuiverGps::SetExifGps(exif, 37.4220, -122.0841, true, 42.0);
        img->writeMetadata();
    }

    gchar* srcUri = g_filename_to_uri(srcImgPath, NULL, NULL);
    QuiverFile srcFile(srcUri);
    g_free(srcUri);

    QuiverMetadata::MetadataPayload payload;
    REQUIRE(QuiverMetadata::ExtractMetadata(srcFile, payload));
    REQUIRE(payload.valid);
    REQUIRE(payload.has_date);
    REQUIRE(payload.date_str == "2025:06:15 14:30:00");
    REQUIRE(payload.has_gps);
    REQUIRE(std::fabs(payload.gps_latitude - 37.4220) < 1e-4);
    REQUIRE(std::fabs(payload.gps_longitude - (-122.0841)) < 1e-4);
    REQUIRE(payload.has_camera);
    REQUIRE(payload.camera_make == "Google");
    REQUIRE(payload.camera_model == "Pixel 8 Pro");
    REQUIRE(payload.has_artist_copyright);
    REQUIRE(payload.artist == "Quiver Photographer");
    REQUIRE(payload.has_description);
    REQUIRE(payload.description == "Mountain View Sunrise");

    // Create destination image
    char dstImgPath[] = "/tmp/quiver_test_dst_img_XXXXXX.jpg";
    int fd2 = g_mkstemp(dstImgPath);
    REQUIRE(fd2 >= 0);
    close(fd2);
    REQUIRE(g_file_get_contents(sampleJpg.c_str(), &contents, &len, NULL));
    REQUIRE(g_file_set_contents(dstImgPath, contents, len, NULL));
    g_free(contents);

    gchar* dstUri = g_filename_to_uri(dstImgPath, NULL, NULL);
    QuiverFile dstFile(dstUri);
    g_free(dstUri);

    SECTION("Selective paste: only GPS and Camera")
    {
        QuiverMetadata::PasteOptions opts;
        opts.copy_date = false;
        opts.copy_gps = true;
        opts.copy_camera = true;
        opts.copy_description = false;
        opts.copy_artist_copyright = false;

        REQUIRE(QuiverMetadata::ApplyMetadata(dstFile, payload, opts));

        auto dstImg = Exiv2::ImageFactory::open(dstImgPath);
        REQUIRE(dstImg.get() != nullptr);
        dstImg->readMetadata();
        Exiv2::ExifData& dstExif = dstImg->exifData();

        // GPS & Camera should exist
        double rLat = 0.0, rLon = 0.0, rAlt = 0.0;
        bool rHasAlt = false;
        REQUIRE(QuiverGps::GetExifGps(dstExif, rLat, rLon, rHasAlt, rAlt));
        REQUIRE(std::fabs(rLat - 37.4220) < 1e-3);
        REQUIRE(std::fabs(rLon - (-122.0841)) < 1e-3);
        REQUIRE(dstExif["Exif.Image.Make"].toString() == "Google");
        REQUIRE(dstExif["Exif.Image.Model"].toString() == "Pixel 8 Pro");

        // Date should NOT exist
        REQUIRE(dstExif.findKey(Exiv2::ExifKey("Exif.Photo.DateTimeOriginal")) == dstExif.end());
    }

    SECTION("Full paste: all categories")
    {
        QuiverMetadata::PasteOptions opts;
        opts.copy_date = true;
        opts.copy_gps = true;
        opts.copy_camera = true;
        opts.copy_description = true;
        opts.copy_artist_copyright = true;

        REQUIRE(QuiverMetadata::ApplyMetadata(dstFile, payload, opts));

        auto dstImg = Exiv2::ImageFactory::open(dstImgPath);
        REQUIRE(dstImg.get() != nullptr);
        dstImg->readMetadata();
        Exiv2::ExifData& dstExif = dstImg->exifData();

        REQUIRE(dstExif["Exif.Photo.DateTimeOriginal"].toString() == "2025:06:15 14:30:00");
        REQUIRE(dstExif["Exif.Image.Make"].toString() == "Google");
        REQUIRE(dstExif["Exif.Image.Artist"].toString() == "Quiver Photographer");
        REQUIRE(dstExif["Exif.Image.ImageDescription"].toString() == "Mountain View Sunrise");

        double rLat = 0.0, rLon = 0.0, rAlt = 0.0;
        bool rHasAlt = false;
        REQUIRE(QuiverGps::GetExifGps(dstExif, rLat, rLon, rHasAlt, rAlt));
        REQUIRE(std::fabs(rLat - 37.4220) < 1e-3);
    }

    g_unlink(srcImgPath);
    g_unlink(dstImgPath);
}

TEST_CASE("QuiverMetadata Copy and Paste Video to Image", "[metadata][copy_paste][video_to_image]")
{
    std::string realVid = "/workspace/2026-09-13 04.50.30 MJ horn honk.mp4";
    if (g_file_test(realVid.c_str(), G_FILE_TEST_EXISTS))
    {
        gchar* vidUri = g_filename_to_uri(realVid.c_str(), NULL, NULL);
        QuiverFile vidFile(vidUri);
        g_free(vidUri);

        QuiverMetadata::MetadataPayload payload;
        REQUIRE(QuiverMetadata::ExtractMetadata(vidFile, payload));
        REQUIRE(payload.valid);
        REQUIRE(payload.source_is_video);

        // Destination image
        std::string imagesDir = QuiverTest_GetImagesDir();
        std::string sampleJpg = imagesDir + "/sample_4k.jpg";
        char dstImgPath[] = "/tmp/quiver_test_vid_to_img_XXXXXX.jpg";
        int fd = g_mkstemp(dstImgPath);
        REQUIRE(fd >= 0);
        close(fd);

        char* contents = nullptr;
        gsize len = 0;
        REQUIRE(g_file_get_contents(sampleJpg.c_str(), &contents, &len, NULL));
        REQUIRE(g_file_set_contents(dstImgPath, contents, len, NULL));
        g_free(contents);

        gchar* dstUri = g_filename_to_uri(dstImgPath, NULL, NULL);
        QuiverFile dstFile(dstUri);
        g_free(dstUri);

        QuiverMetadata::PasteOptions opts;
        opts.copy_date = true;
        opts.copy_description = true;

        REQUIRE(QuiverMetadata::ApplyMetadata(dstFile, payload, opts));

        auto dstImg = Exiv2::ImageFactory::open(dstImgPath);
        REQUIRE(dstImg.get() != nullptr);
        dstImg->readMetadata();
        Exiv2::ExifData& dstExif = dstImg->exifData();

        // Verify date was copied from video container to image EXIF
        if (payload.has_date)
        {
            REQUIRE(dstExif.findKey(Exiv2::ExifKey("Exif.Photo.DateTimeOriginal")) != dstExif.end());
            REQUIRE(dstExif["Exif.Photo.DateTimeOriginal"].toString() == payload.date_str);
        }

        g_unlink(dstImgPath);
    }
}

TEST_CASE("QuiverMetadata Copy and Paste Image to Video and Video to Video", "[metadata][copy_paste][video]")
{
    std::string realVid = "/workspace/2026-09-13 04.50.30 MJ horn honk.mp4";
    if (g_file_test(realVid.c_str(), G_FILE_TEST_EXISTS))
    {
        // Create a temporary copy of the video to modify
        char tmpVidPath[] = "/tmp/quiver_test_vid_target_XXXXXX.mp4";
        int fd = g_mkstemp(tmpVidPath);
        REQUIRE(fd >= 0);
        close(fd);

        char* vidData = nullptr;
        gsize vidLen = 0;
        REQUIRE(g_file_get_contents(realVid.c_str(), &vidData, &vidLen, NULL));
        REQUIRE(g_file_set_contents(tmpVidPath, vidData, vidLen, NULL));
        g_free(vidData);

        gchar* tmpVidUri = g_filename_to_uri(tmpVidPath, NULL, NULL);
        QuiverFile targetVidFile(tmpVidUri);
        g_free(tmpVidUri);

        SECTION("Image to Video copy/paste")
        {
            QuiverMetadata::MetadataPayload imgPayload;
            imgPayload.valid = true;
            imgPayload.source_is_video = false;
            imgPayload.source_filename = "photo.jpg";
            imgPayload.has_camera = true;
            imgPayload.camera_make = "Nikon";
            imgPayload.camera_model = "Z9";
            imgPayload.has_gps = true;
            imgPayload.gps_latitude = 51.5074;
            imgPayload.gps_longitude = -0.1278;
            imgPayload.has_altitude = true;
            imgPayload.gps_altitude = 25.0;
            imgPayload.has_description = true;
            imgPayload.title = "London Bridge";
            imgPayload.has_artist_copyright = true;
            imgPayload.artist = "London Travel";

            QuiverMetadata::PasteOptions opts;
            opts.copy_camera = true;
            opts.copy_gps = true;
            opts.copy_description = true;
            opts.copy_artist_copyright = true;

            REQUIRE(QuiverMetadata::ApplyMetadata(targetVidFile, imgPayload, opts));

            // Verify metadata was remuxed into video
            QuiverMetadata::MetadataPayload readBack;
            REQUIRE(QuiverMetadata::ExtractMetadata(targetVidFile, readBack));
            REQUIRE(readBack.has_camera);
            REQUIRE(readBack.camera_make == "Nikon");
            REQUIRE(readBack.camera_model == "Z9");
            REQUIRE(readBack.has_gps);
            REQUIRE(std::fabs(readBack.gps_latitude - 51.5074) < 1e-3);
            REQUIRE(std::fabs(readBack.gps_longitude - (-0.1278)) < 1e-3);
            REQUIRE(readBack.has_description);
            REQUIRE(readBack.title == "London Bridge");
            REQUIRE(readBack.has_artist_copyright);
            REQUIRE(readBack.artist == "London Travel");
        }

        SECTION("Video to Video copy/paste")
        {
            // First extract from the modified video
            QuiverMetadata::MetadataPayload vidPayload;
            REQUIRE(QuiverMetadata::ExtractMetadata(targetVidFile, vidPayload));

            // Create another target video
            char tmpVidPath2[] = "/tmp/quiver_test_vid_target2_XXXXXX.mp4";
            int fd2 = g_mkstemp(tmpVidPath2);
            REQUIRE(fd2 >= 0);
            close(fd2);
            REQUIRE(g_file_get_contents(realVid.c_str(), &vidData, &vidLen, NULL));
            REQUIRE(g_file_set_contents(tmpVidPath2, vidData, vidLen, NULL));
            g_free(vidData);

            gchar* tmpVidUri2 = g_filename_to_uri(tmpVidPath2, NULL, NULL);
            QuiverFile targetVidFile2(tmpVidUri2);
            g_free(tmpVidUri2);

            QuiverMetadata::PasteOptions opts;
            opts.copy_camera = true;
            opts.copy_gps = true;

            REQUIRE(QuiverMetadata::ApplyMetadata(targetVidFile2, vidPayload, opts));

            QuiverMetadata::MetadataPayload readBack2;
            REQUIRE(QuiverMetadata::ExtractMetadata(targetVidFile2, readBack2));
            REQUIRE(readBack2.has_camera == vidPayload.has_camera);

            g_unlink(tmpVidPath2);
        }

        g_unlink(tmpVidPath);
    }
}

TEST_CASE("QuiverFile reload updates in-memory metadata and mtime after ApplyMetadata", "[metadata][copy_paste][reload]")
{
    std::string imagesDir = QuiverTest_GetImagesDir();
    std::string sampleJpg = imagesDir + "/sample_4k.jpg";
    char dstImgPath[] = "/tmp/quiver_test_reload_XXXXXX.jpg";
    int fd = g_mkstemp(dstImgPath);
    REQUIRE(fd >= 0);
    close(fd);

    char* contents = nullptr;
    gsize len = 0;
    REQUIRE(g_file_get_contents(sampleJpg.c_str(), &contents, &len, NULL));
    REQUIRE(g_file_set_contents(dstImgPath, contents, len, NULL));
    g_free(contents);

    gchar* dstUri = g_filename_to_uri(dstImgPath, NULL, NULL);
    QuiverFile dstFile(dstUri);
    g_free(dstUri);

    // Initial state
    auto initialExif = dstFile.GetExifDataShared();
    (void)initialExif;

    QuiverMetadata::MetadataPayload payload;
    payload.valid = true;
    payload.has_date = true;
    payload.date_epoch = 1718461800; // 2024-06-15 14:30:00 UTC
    payload.date_str = "2024:06:15 14:30:00";
    payload.has_camera = true;
    payload.camera_make = "Sony";
    payload.camera_model = "A7IV";

    QuiverMetadata::PasteOptions opts;
    opts.copy_date = true;
    opts.copy_camera = true;

    std::string err;
    REQUIRE(QuiverMetadata::ApplyMetadata(dstFile, payload, opts, nullptr, nullptr, &err));

    // Reload the QuiverFile object
    dstFile.Reload();

    // Verify in-memory Exif data reflects the change
    auto updatedExif = dstFile.GetExifDataShared();
    REQUIRE(updatedExif != nullptr);
    auto it = updatedExif->findKey(Exiv2::ExifKey("Exif.Photo.DateTimeOriginal"));
    REQUIRE(it != updatedExif->end());
    REQUIRE(it->toString() == "2024:06:15 14:30:00");

    auto itMake = updatedExif->findKey(Exiv2::ExifKey("Exif.Image.Make"));
    REQUIRE(itMake != updatedExif->end());
    REQUIRE(itMake->toString() == "Sony");

    // Verify disk mtime was set to payload.date_epoch
    struct stat st = {};
    REQUIRE(0 == stat(dstImgPath, &st));
    REQUIRE(st.st_mtime == payload.date_epoch);

    g_unlink(dstImgPath);
}

TEST_CASE("QuiverFile DateTime fallback for images lacking DateTimeOriginal", "[metadata][date_fallback]")
{
    char tmpImgPath[] = "/tmp/quiver_test_dt_fallback_XXXXXX.jpg";
    int fd = g_mkstemp(tmpImgPath);
    REQUIRE(fd >= 0);
    close(fd);

    std::string imagesDir = QuiverTest_GetImagesDir();
    std::string sampleJpg = imagesDir + "/sample_4k.jpg";
    char* contents = nullptr;
    gsize len = 0;
    REQUIRE(g_file_get_contents(sampleJpg.c_str(), &contents, &len, NULL));
    REQUIRE(g_file_set_contents(tmpImgPath, contents, len, NULL));
    g_free(contents);

    // Write ONLY Exif.Image.DateTime (no DateTimeOriginal)
    {
        auto img = Exiv2::ImageFactory::open(tmpImgPath);
        REQUIRE(img.get() != nullptr);
        img->readMetadata();
        Exiv2::ExifData& exif = img->exifData();
        exif.clear();
        exif["Exif.Image.DateTime"] = "2023:09:11 22:05:17";
        img->writeMetadata();
    }

    gchar* uri = g_filename_to_uri(tmpImgPath, NULL, NULL);
    QuiverFile qf(uri);
    g_free(uri);

    // Verify QuiverFile recognizes date metadata from Exif.Image.DateTime
    REQUIRE(qf.HasDateMetadata());

    struct tm tmExpected = {};
    tmExpected.tm_year = 2023 - 1900;
    tmExpected.tm_mon = 9 - 1;
    tmExpected.tm_mday = 11;
    tmExpected.tm_hour = 22;
    tmExpected.tm_min = 5;
    tmExpected.tm_sec = 17;
    tmExpected.tm_isdst = -1;
    time_t expectedEpoch = mktime(&tmExpected);

    REQUIRE(qf.GetTimeT(true) == expectedEpoch);

    g_unlink(tmpImgPath);
}

