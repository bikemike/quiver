#include <catch2/catch_test_macros.hpp>
#include "QuiverVideoOps.h"
#include "test_helpers.h"
#include <glib.h>
#include <string>

TEST_CASE("FFmpeg Video Decoding and Probing via QuiverVideoOps", "[lib][ffmpeg][video]")
{
    std::string imagesDir = QuiverTest_GetImagesDir();
    std::string videoPath = imagesDir + "/sample_video.mp4";

    REQUIRE(g_file_test(videoPath.c_str(), G_FILE_TEST_EXISTS));
    gchar* videoUri = g_filename_to_uri(videoPath.c_str(), NULL, NULL);
    REQUIRE(videoUri != NULL);

    SECTION("Probe video metadata")
    {
        gint64 duration_ns = 0;
        gint width = 0, height = 0;
        gint num = 0, den = 0;

        gboolean ok = QuiverVideoOps::Probe(videoUri, &duration_ns, &width, &height, &num, &den);
        REQUIRE(ok == TRUE);
        REQUIRE(duration_ns > 0);
        REQUIRE(width > 0);
        REQUIRE(height > 0);
        REQUIRE(num > 0);
        REQUIRE(den > 0);
    }

    SECTION("Decode video preview frame at natural size")
    {
        gint num = 0, den = 0;
        GdkPixbuf* pb = QuiverVideoOps::LoadPixbuf(videoUri, &num, &den, -1, 0, 0);
        REQUIRE(pb != nullptr);

        int w = gdk_pixbuf_get_width(pb);
        int h = gdk_pixbuf_get_height(pb);
        REQUIRE(w > 0);
        REQUIRE(h > 0);

        g_object_unref(pb);
    }

    SECTION("Decode video preview frame scaled to thumbnail dimensions")
    {
        gint num = 0, den = 0;
        GdkPixbuf* thumb = QuiverVideoOps::LoadPixbuf(videoUri, &num, &den, -1, 160, 120);
        REQUIRE(thumb != nullptr);

        int w = gdk_pixbuf_get_width(thumb);
        int h = gdk_pixbuf_get_height(thumb);
        REQUIRE(w <= 160);
        REQUIRE(h <= 120);

        g_object_unref(thumb);
    }

    SECTION("Seek to specific timestamp")
    {
        // 0.5 seconds in nanoseconds
        gint64 pos_ns = 500000000;
        GdkPixbuf* frame = QuiverVideoOps::LoadPixbuf(videoUri, nullptr, nullptr, pos_ns, 320, 240);
        REQUIRE(frame != nullptr);
        g_object_unref(frame);
    }

    SECTION("Decode video preview frame with arbitrary target bounds and aspect scaling")
    {
        GdkPixbuf* frame = QuiverVideoOps::LoadPixbuf(videoUri, nullptr, nullptr, -1, 925, 585);
        REQUIRE(frame != nullptr);
        int w = gdk_pixbuf_get_width(frame);
        int h = gdk_pixbuf_get_height(frame);
        REQUIRE(w <= 925);
        REQUIRE(h <= 585);
        g_object_unref(frame);
    }

    SECTION("Error handling on invalid or non-video URI")
    {
        gboolean ok = QuiverVideoOps::Probe("file:///does/not/exist.mp4");
        REQUIRE(ok == FALSE);

        GdkPixbuf* pb = QuiverVideoOps::LoadPixbuf("file:///does/not/exist.mp4");
        REQUIRE(pb == nullptr);
    }

    g_free(videoUri);
}
