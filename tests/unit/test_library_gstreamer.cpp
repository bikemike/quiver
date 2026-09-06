#include <catch2/catch_test_macros.hpp>
#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>
#include "test_helpers.h"
#include <string>

static std::string GstTimeFormat(gint64 time)
{
    gint64 total_secs = GST_TIME_AS_SECONDS(time);
    gint64 secs  = total_secs % 60;
    gint64 total_mins = total_secs / 60;
    gint64 mins  = total_mins % 60;
    gint64 hours = total_mins / 60;

    gchar* str = nullptr;
    if (0 != hours)
        str = g_strdup_printf("%lld:%02lld:%02lld", (long long)hours, (long long)mins, (long long)secs);
    else
        str = g_strdup_printf("%lld:%02lld", (long long)mins, (long long)secs);

    std::string res(str);
    g_free(str);
    return res;
}

TEST_CASE("GStreamer Library Pipeline and Media Verification", "[lib][gstreamer][video]")
{
    // gst_init_check was already run in main
    REQUIRE(gst_is_initialized() == TRUE);

    SECTION("GStreamer Time Formatting Logic")
    {
        // 0 seconds -> "0:00"
        REQUIRE(GstTimeFormat(0) == "0:00");
        // 65 seconds -> "1:05" (65 * 10^9 ns)
        REQUIRE(GstTimeFormat(65 * GST_SECOND) == "1:05");
        // 3661 seconds -> "1:01:01"
        REQUIRE(GstTimeFormat(3661 * GST_SECOND) == "1:01:01");
    }

    SECTION("GStreamer Pipeline Construction and Caps Creation")
    {
        GstElement* pipeline = gst_pipeline_new("test_pipeline");
        REQUIRE(pipeline != nullptr);

        GstCaps* caps = gst_caps_new_empty_simple("video/x-raw");
        REQUIRE(caps != nullptr);
        REQUIRE(gst_caps_is_fixed(caps) == TRUE);

        gst_caps_set_simple(caps,
            "width", G_TYPE_INT, 640,
            "height", G_TYPE_INT, 480,
            nullptr);

        GstStructure* s = gst_caps_get_structure(caps, 0);
        gint w = 0, h = 0;
        REQUIRE(gst_structure_get_int(s, "width", &w) == TRUE);
        REQUIRE(gst_structure_get_int(s, "height", &h) == TRUE);
        REQUIRE(w == 640);
        REQUIRE(h == 480);

        gst_caps_unref(caps);
        gst_object_unref(pipeline);
    }

    SECTION("GStreamer Discoverer on sample video")
    {
        std::string imagesDir = QuiverTest_GetImagesDir();
        std::string videoPath = imagesDir + "/sample_video.mp4";
        gchar* videoUri = g_filename_to_uri(videoPath.c_str(), NULL, NULL);
        REQUIRE(videoUri != NULL);

        GError* err = nullptr;
        GstDiscoverer* discoverer = gst_discoverer_new(5 * GST_SECOND, &err);
        REQUIRE(discoverer != nullptr);

        GstDiscovererInfo* info = gst_discoverer_discover_uri(discoverer, videoUri, &err);
        REQUIRE(info != nullptr);
        REQUIRE(gst_discoverer_info_get_result(info) == GST_DISCOVERER_OK);

        GstClockTime duration = gst_discoverer_info_get_duration(info);
        REQUIRE(duration > 0);

        GList* videoStreams = gst_discoverer_info_get_video_streams(info);
        REQUIRE(videoStreams != nullptr);

        GstDiscovererVideoInfo* vInfo = (GstDiscovererVideoInfo*)videoStreams->data;
        REQUIRE(gst_discoverer_video_info_get_width(vInfo) > 0);
        REQUIRE(gst_discoverer_video_info_get_height(vInfo) > 0);

        gst_discoverer_stream_info_list_free(videoStreams);
        gst_discoverer_info_unref(info);
        g_object_unref(discoverer);
        g_free(videoUri);
    }
}
