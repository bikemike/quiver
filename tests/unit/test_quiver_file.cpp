#include <catch2/catch_test_macros.hpp>
#include "QuiverFile.h"
#include "test_helpers.h"
#include <glib.h>
#include <string>

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

    g_free(videoUri);
    g_free(imageUri);
}
