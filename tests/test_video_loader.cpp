#include "QuiverFile.h"
#include "QuiverVideoOps.h"
#include "ImageDecoder.h"
#include "Timer.h"
#include <iostream>
#include <iomanip>
#include <cassert>
#include <cstring>
#include <glib.h>
#include <gtk/gtk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gst/gst.h>

// QuiverUtils references g_pApp
GtkApplication *g_pApp = NULL;

int main(int argc, char** argv)
{
    const char* video_path = "/workspace/tests/images/sample_video.mp4";
    if (argc > 1)
        video_path = argv[1];

    if (!g_file_test(video_path, G_FILE_TEST_EXISTS))
    {
        std::cerr << "Test video not found: " << video_path << "\n";
        return 1;
    }

    gchar* uri = g_filename_to_uri(video_path, NULL, NULL);
    assert(uri != NULL);

    std::cout << "============================================================\n";
    std::cout << "Testing Video Preview Load Timing\n";
    std::cout << "  Path: " << video_path << "\n";
    std::cout << "  URI:  " << uri << "\n";
    std::cout << "============================================================\n";

    QuiverFile qf(uri);

    // 1. Verify file type identification
    std::cout << "Checking if QuiverFile recognizes video...\n";
    assert(qf.IsVideo());
    std::cout << "  [IsVideo] -> PASS\n";

    // 2. Verify initial load time is uninitialized (< 0.0) so status bar is empty
    double initial_load_time = qf.GetLoadTimeInSeconds();
    std::cout << "Initial Load Time: " << initial_load_time << " s (expected < 0.0)\n";
    assert(initial_load_time < 0.0);
    std::cout << "  [Initial Load Time < 0] -> PASS\n";

    // 3. Measure extraction + decode time via ImageDecoder backend abstraction
    Timer loadTimer;
    gint n = 1, d = 1;
    GdkPixbuf* video_pixbuf = ImageDecoder::DecodeVideoPreview(uri, &n, &d, -1, 640, 480);
    assert(video_pixbuf != NULL);

    guint pixbuf_width = gdk_pixbuf_get_width(video_pixbuf);
    guint pixbuf_height = gdk_pixbuf_get_height(video_pixbuf);

    if (n > d)
        pixbuf_width = (guint)((pixbuf_width * n) / float(d) + .5);
    else
        pixbuf_height = (guint)((pixbuf_height * d) / float(n) + .5);

    qf.SetWidth(pixbuf_width);
    qf.SetHeight(pixbuf_height);

    GdkPixbuf* final_pixbuf = NULL;
    if (n != d)
    {
        final_pixbuf = gdk_pixbuf_scale_simple(
            video_pixbuf,
            pixbuf_width,
            pixbuf_height,
            GDK_INTERP_BILINEAR);
        g_object_unref(video_pixbuf);
    }
    else
    {
        final_pixbuf = video_pixbuf;
    }

    double elapsed_seconds = loadTimer.GetRunningTimeInSeconds();
    qf.SetLoadTimeInSeconds(elapsed_seconds);

    std::cout << "  [Extracted Frame] " << pixbuf_width << "x" << pixbuf_height
              << " in " << std::fixed << std::setprecision(4)
              << elapsed_seconds << " s\n";

    assert(elapsed_seconds > 0.0);
    assert(qf.GetLoadTimeInSeconds() == elapsed_seconds);
    std::cout << "  [Set/Get Load Time] -> PASS\n";

    // 4. Verify status bar formatting logic
    char statusbar_text[32];
    double seconds = qf.GetLoadTimeInSeconds();
    assert(seconds >= 0.0);
    g_snprintf(statusbar_text, sizeof(statusbar_text), "%0.3fs", seconds);
    std::cout << "  [Status Bar Label Text] \"" << statusbar_text << "\"\n";
    assert(strcmp(statusbar_text, "0.000s") != 0);
    std::cout << "  [Non-Zero Status Bar Output] -> PASS\n";

    // 5. Test thumbnail generation
    std::cout << "Testing video thumbnail generation and caching...\n";
    GdkPixbuf* thumb = qf.GetThumbnail(256);
    assert(thumb != NULL);
    std::cout << "  [Thumbnail] " << gdk_pixbuf_get_width(thumb) << "x"
              << gdk_pixbuf_get_height(thumb) << " -> PASS\n";
    g_object_unref(thumb);

    // 6. Test modern GdkTexture video preview decoding
    std::cout << "Testing modern GdkTexture video preview decoding...\n";
    GdkTexture* video_texture = ImageDecoder::DecodeVideoTexture(uri);
    assert(video_texture != NULL);
    std::cout << "  [GdkTexture] " << gdk_texture_get_width(video_texture) << "x"
              << gdk_texture_get_height(video_texture) << " -> PASS\n";
    g_object_unref(video_texture);

    // 7. Test video playback rate preservation during seeking (SkipForward / SkipBack)
    std::cout << "Testing video playback speed preservation during seeking...\n";
    if (!gst_is_initialized())
    {
        gst_init(&argc, &argv);
    }

    GstElement *pipeline = gst_element_factory_make("playbin", "test_player");
    assert(pipeline != NULL);

    GstElement *vsink = gst_element_factory_make("fakesink", "vsink");
    GstElement *asink = gst_element_factory_make("fakesink", "asink");
    g_object_set(G_OBJECT(pipeline), "video-sink", vsink, "audio-sink", asink, "uri", uri, NULL);

    GstStateChangeReturn sret = gst_element_set_state(pipeline, GST_STATE_PAUSED);
    assert(sret != GST_STATE_CHANGE_FAILURE);
    sret = gst_element_get_state(pipeline, NULL, NULL, 5 * GST_SECOND);
    assert(sret == GST_STATE_CHANGE_SUCCESS);

    auto query_playback_rate = [](GstElement* pipe) -> gdouble {
        GstQuery *q = gst_query_new_segment(GST_FORMAT_TIME);
        gdouble rate = -1.0;
        if (gst_element_query(pipe, q)) {
            gst_query_parse_segment(q, &rate, NULL, NULL, NULL);
        }
        gst_query_unref(q);
        return rate;
    };

    gdouble current_rate = query_playback_rate(pipeline);
    std::cout << "  [Initial Playback Rate] " << current_rate << " -> PASS\n";
    assert(current_rate == 1.0);

    // Set playback speed to 2.0x
    gdouble target_speed = 2.0;
    gint64 current_pos = 0;
    gst_element_query_position(pipeline, GST_FORMAT_TIME, &current_pos);
    gboolean seek_res = gst_element_seek(pipeline, target_speed, GST_FORMAT_TIME,
        static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE),
        GST_SEEK_TYPE_SET, current_pos, GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);
    assert(seek_res);
    gst_element_get_state(pipeline, NULL, NULL, 5 * GST_SECOND);

    current_rate = query_playback_rate(pipeline);
    std::cout << "  [Rate After Setting 2.0x] " << current_rate << " -> PASS\n";
    assert(current_rate == target_speed);

    // Simulate SkipForward with target_speed
    gint64 forward_pos = current_pos + GST_SECOND * 5;
    seek_res = gst_element_seek(pipeline, target_speed, GST_FORMAT_TIME,
        static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE),
        GST_SEEK_TYPE_SET, forward_pos, GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);
    assert(seek_res);
    gst_element_get_state(pipeline, NULL, NULL, 5 * GST_SECOND);

    current_rate = query_playback_rate(pipeline);
    std::cout << "  [Rate After SkipForward] " << current_rate << " (expected 2.0) -> PASS\n";
    assert(current_rate == target_speed);

    // Simulate SkipBack with target_speed
    gint64 back_pos = std::max((gint64)0, forward_pos - GST_SECOND * 2);
    seek_res = gst_element_seek(pipeline, target_speed, GST_FORMAT_TIME,
        static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE),
        GST_SEEK_TYPE_SET, back_pos, GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);
    assert(seek_res);
    gst_element_get_state(pipeline, NULL, NULL, 5 * GST_SECOND);

    current_rate = query_playback_rate(pipeline);
    std::cout << "  [Rate After SkipBack] " << current_rate << " (expected 2.0) -> PASS\n";
    assert(current_rate == target_speed);

    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);

    if (final_pixbuf) g_object_unref(final_pixbuf);
    g_free(uri);

    std::cout << "\n>>> ALL VIDEO PREVIEW LOAD TIMING TESTS PASSED! <<<\n\n";
    return 0;
}
