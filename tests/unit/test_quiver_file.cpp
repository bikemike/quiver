#include <catch2/catch_test_macros.hpp>
#include "QuiverFile.h"
#include "ImageDecoder.h"
#include "test_helpers.h"
#include <exiv2/exiv2.hpp>
#include <glib.h>
#include <glib/gstdio.h>
#include <string>
#include <unistd.h>
#include <vector>

TEST_CASE("QuiverFile URI and Media Detection", "[unit][file]")
{
    std::string imagesDir = QuiverTest_GetImagesDir();
    std::string videoPath = imagesDir + "/sample_video.mp4";
    std::string imagePath = imagesDir + "/sample_4k.jpg";

    gchar* videoUri = g_filename_to_uri(videoPath.c_str(), NULL, NULL);
    gchar* imageUri = g_filename_to_uri(imagePath.c_str(), NULL, NULL);

    REQUIRE(videoUri != NULL);
    REQUIRE(imageUri != NULL);

    SECTION("URI construction and retrieval")
    {
        QuiverFile qf(videoUri);
        REQUIRE(std::string(qf.GetURI()) == std::string(videoUri));
    }

    SECTION("Video identification")
    {
        QuiverFile qfVideo(videoUri);
        REQUIRE(qfVideo.IsVideo() == true);

        QuiverFile qfImage(imageUri);
        REQUIRE(qfImage.IsVideo() == false);
    }

    SECTION("Folder vs File identification")
    {
        gchar* dirUri = g_filename_to_uri(imagesDir.c_str(), NULL, NULL);
        REQUIRE(dirUri != NULL);

        QuiverFile qfDir(dirUri);
        REQUIRE(qfDir.IsFolder() == true);

        QuiverFile qfFile(imageUri);
        REQUIRE(qfFile.IsFolder() == false);

        g_free(dirUri);
    }

    SECTION("Thumbnail cache management")
    {
        QuiverFile::ClearThumbnailCache();
        QuiverFile qf(imageUri);
        // Cache was just cleared, in-memory thumb should be false or queryable without crash
        qf.RemoveCachedThumbnail();
    }

    SECTION("Rotated image thumbnail orientation for either backend")
    {
        std::string sampleRotated = imagesDir + "/sample_rotated.jpg";
        REQUIRE(g_file_test(sampleRotated.c_str(), G_FILE_TEST_EXISTS));

        gchar *rotUri = g_filename_to_uri(sampleRotated.c_str(), NULL, NULL);
        REQUIRE(rotUri != NULL);

        std::vector<ImageDecoder::Backend> backends;
        backends.push_back(ImageDecoder::Backend::AUTO);
        if (ImageDecoder::IsBackendSupported(ImageDecoder::Backend::GLYCIN))
            backends.push_back(ImageDecoder::Backend::GLYCIN);
        if (ImageDecoder::IsBackendSupported(ImageDecoder::Backend::PIXBUF))
            backends.push_back(ImageDecoder::Backend::PIXBUF);

        ImageDecoder::Backend originalBackend = ImageDecoder::GetBackend();

        for (auto backend : backends)
        {
            std::string bname = "AUTO";
            if (backend == ImageDecoder::Backend::GLYCIN) bname = "GLYCIN";
            else if (backend == ImageDecoder::Backend::PIXBUF) bname = "PIXBUF";

            DYNAMIC_SECTION("Backend: " << bname)
            {
                ImageDecoder::SetBackend(backend);

                QuiverFile::ClearThumbnailCache();
                QuiverFile qf(rotUri);
                qf.RemoveCachedThumbnail();

                REQUIRE(qf.GetOrientation() == 6);
                REQUIRE(qf.GetWidth() == 320);
                REQUIRE(qf.GetHeight() == 180);

                // Embedded EXIF thumbnail exists and is raw unrotated (160x90)
                GdkTexture *exifThumb = qf.GetExifThumbnailTexture();
                REQUIRE(exifThumb != NULL);
                CHECK(gdk_texture_get_width(exifThumb) == 160);
                CHECK(gdk_texture_get_height(exifThumb) == 90);
                g_object_unref(exifThumb);

                // Size 128 uses EXIF thumbnail and reorients to upright portrait (72x128)
                GdkTexture *t128 = qf.GetThumbnailTexture(128);
                REQUIRE(t128 != NULL);
                CHECK(gdk_texture_get_width(t128) == 72);
                CHECK(gdk_texture_get_height(t128) == 128);
                g_object_unref(t128);

                // Size 256 uses full image decode and produces upright portrait (144x256)
                GdkTexture *t256 = qf.GetThumbnailTexture(256);
                REQUIRE(t256 != NULL);
                CHECK(gdk_texture_get_width(t256) == 144);
                CHECK(gdk_texture_get_height(t256) == 256);
                g_object_unref(t256);

                // Re-open in a fresh QuiverFile object to test cache hit from ~/.cache/thumbnails
                QuiverFile qf2(rotUri);
                GdkTexture *t128_cached = qf2.GetThumbnailTexture(128);
                REQUIRE(t128_cached != NULL);
                CHECK(gdk_texture_get_width(t128_cached) == 72);
                CHECK(gdk_texture_get_height(t128_cached) == 128);
                g_object_unref(t128_cached);

                GdkTexture *t256_cached = qf2.GetThumbnailTexture(256);
                REQUIRE(t256_cached != NULL);
                CHECK(gdk_texture_get_width(t256_cached) == 144);
                CHECK(gdk_texture_get_height(t256_cached) == 256);
                g_object_unref(t256_cached);

                // Verify that Quiver wrote Thumb::Image::Orientation to the cached file on disk
                gchar *hash = g_compute_checksum_for_string(G_CHECKSUM_MD5, rotUri, -1);
                gchar *thumbFilename = g_strdup_printf("%s.png", hash);
                gchar *thumbPath = g_build_filename(g_get_user_cache_dir(), "thumbnails", "large", thumbFilename, NULL);
                g_free(thumbFilename);
                g_free(hash);

                if (g_file_test(thumbPath, G_FILE_TEST_EXISTS))
                {
                    GdkPixbuf *pbDisk = gdk_pixbuf_new_from_file(thumbPath, NULL);
                    REQUIRE(pbDisk != nullptr);
                    const char *diskOri = gdk_pixbuf_get_option(pbDisk, "tEXt::Thumb::Image::Orientation");
                    REQUIRE(diskOri != nullptr);
                    CHECK(std::string(diskOri) == "6");
                    g_object_unref(pbDisk);
                }
                g_free(thumbPath);

                qf2.RemoveCachedThumbnail();
            }
        }

        ImageDecoder::SetBackend(originalBackend);
        g_free(rotUri);
    }

    SECTION("Orientation stored as SLONG is honoured even when the loader drops it")
    {
        /* The EXIF spec requires Orientation to be SHORT, and both gdk-pixbuf
         * and glycin silently ignore the tag when it arrives as another integer
         * type.  Exiv2 accepts it, so the file below used to arrive as a
         * portrait frame wrapped around unrotated pixels: the geometry came
         * from Exiv2 while the bits came from the loader.  These are the same
         * expectations as the SHORT fixture above, because to the user the only
         * difference that matters is that the picture comes out upright. */
        std::string slongOri = imagesDir + "/sample_rotated_slong_ori.jpg";
        REQUIRE(g_file_test(slongOri.c_str(), G_FILE_TEST_EXISTS));

        gchar *slongUri = g_filename_to_uri(slongOri.c_str(), NULL, NULL);
        REQUIRE(slongUri != NULL);

        std::vector<ImageDecoder::Backend> backends;
        backends.push_back(ImageDecoder::Backend::AUTO);
        if (ImageDecoder::IsBackendSupported(ImageDecoder::Backend::GLYCIN))
            backends.push_back(ImageDecoder::Backend::GLYCIN);
        if (ImageDecoder::IsBackendSupported(ImageDecoder::Backend::PIXBUF))
            backends.push_back(ImageDecoder::Backend::PIXBUF);

        ImageDecoder::Backend originalBackend = ImageDecoder::GetBackend();

        for (auto backend : backends)
        {
            std::string bname = "AUTO";
            if (backend == ImageDecoder::Backend::GLYCIN) bname = "GLYCIN";
            else if (backend == ImageDecoder::Backend::PIXBUF) bname = "PIXBUF";

            DYNAMIC_SECTION("Backend: " << bname)
            {
                ImageDecoder::SetBackend(backend);

                QuiverFile::ClearThumbnailCache();
                QuiverFile qf(slongUri);
                qf.RemoveCachedThumbnail();

                REQUIRE(qf.GetOrientation() == 8);
                REQUIRE(qf.GetWidth() == 320);
                REQUIRE(qf.GetHeight() == 180);

                GdkTexture *t128 = qf.GetThumbnailTexture(128);
                REQUIRE(t128 != NULL);
                CHECK(gdk_texture_get_width(t128) == 72);
                CHECK(gdk_texture_get_height(t128) == 128);
                g_object_unref(t128);

                GdkTexture *t256 = qf.GetThumbnailTexture(256);
                REQUIRE(t256 != NULL);
                CHECK(gdk_texture_get_width(t256) == 144);
                CHECK(gdk_texture_get_height(t256) == 256);
                g_object_unref(t256);

                /* And the cached copy has to survive a fresh object, or the
                 * next session rediscovers the same broken thumbnail. */
                QuiverFile qf2(slongUri);
                GdkTexture *t256_cached = qf2.GetThumbnailTexture(256);
                REQUIRE(t256_cached != NULL);
                CHECK(gdk_texture_get_width(t256_cached) == 144);
                CHECK(gdk_texture_get_height(t256_cached) == 256);
                g_object_unref(t256_cached);

                qf2.RemoveCachedThumbnail();
            }
        }

        ImageDecoder::SetBackend(originalBackend);
        g_free(slongUri);
    }

    g_free(videoUri);
    g_free(imageUri);
}

TEST_CASE("QuiverFile date metadata detection", "[unit][file][date]")
{
    std::string imagesDir = QuiverTest_GetImagesDir();
    std::string sampleJpg = imagesDir + "/sample_4k.jpg";
    REQUIRE(g_file_test(sampleJpg.c_str(), G_FILE_TEST_EXISTS));

    gchar* sampleUri = g_filename_to_uri(sampleJpg.c_str(), NULL, NULL);
    REQUIRE(sampleUri != NULL);

    SECTION("File without EXIF date falls back to the modification time")
    {
        QuiverFile qf(sampleUri);
        // sample_4k.jpg carries no DateTimeOriginal
        REQUIRE(qf.GetTimeT() != 0);
        REQUIRE(qf.HasDateMetadata() == false);
    }

    SECTION("File with EXIF DateTimeOriginal reports date metadata")
    {
        char tmpPath[] = "/tmp/quiver_test_date_meta_XXXXXX.jpg";
        int fd = g_mkstemp(tmpPath);
        REQUIRE(fd >= 0);
        close(fd);

        char* contents = nullptr;
        gsize length = 0;
        REQUIRE(g_file_get_contents(sampleJpg.c_str(), &contents, &length, NULL));
        REQUIRE(g_file_set_contents(tmpPath, contents, length, NULL));
        g_free(contents);

        {
            auto image = Exiv2::ImageFactory::open(tmpPath);
            REQUIRE(image.get() != nullptr);
            image->readMetadata();
            image->exifData()["Exif.Photo.DateTimeOriginal"] = "2024:06:15 10:20:30";
            image->writeMetadata();
        }

        gchar* tmpUri = g_filename_to_uri(tmpPath, NULL, NULL);
        REQUIRE(tmpUri != NULL);

        {
            QuiverFile qf(tmpUri);
            REQUIRE(qf.HasDateMetadata() == true);
            REQUIRE(qf.GetTimeT() != 0);
        }

        // sorting by modification time must not hide the EXIF date
        {
            QuiverFile qf(tmpUri);
            qf.GetTimeT(false);
            REQUIRE(qf.HasDateMetadata() == true);
        }

        g_free(tmpUri);
        g_unlink(tmpPath);
    }

    g_free(sampleUri);
}
