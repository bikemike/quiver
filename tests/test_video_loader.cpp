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

    if (final_pixbuf) g_object_unref(final_pixbuf);
    g_free(uri);

    std::cout << "\n>>> ALL VIDEO PREVIEW LOAD TIMING TESTS PASSED! <<<\n\n";
    return 0;
}
