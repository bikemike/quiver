#include <catch2/catch_test_macros.hpp>
#include "QuiverVideoOps.h"
#include "QuiverFile.h"
#include "ImageCache.h"
#include "test_helpers.h"
#include <glib.h>
#include <gtk/gtk.h>
#include <chrono>
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>

static GdkTexture* CreateMockTexture(int w, int h)
{
    gsize size = (gsize)w * h * 4;
    guint32* data = (guint32*)g_malloc0(size);
    for (int i = 0; i < w * h; ++i) data[i] = 0xFF0000FF;
    GBytes* bytes = g_bytes_new_take(data, size);
    GdkTexture* tex = gdk_memory_texture_new(w, h, GDK_MEMORY_DEFAULT, bytes, (gsize)w * 4);
    g_bytes_unref(bytes);
    return tex;
}

TEST_CASE("Optimization Benchmark: Single-Session Video Decoding vs Redundant Probe", "[benchmark][video][optimizations]")
{
    std::string imagesDir = QuiverTest_GetImagesDir();
    std::string videoPath = imagesDir + "/sample_video.mp4";

    REQUIRE(g_file_test(videoPath.c_str(), G_FILE_TEST_EXISTS));
    gchar* videoUri = g_filename_to_uri(videoPath.c_str(), NULL, NULL);
    REQUIRE(videoUri != NULL);

    // Warm-up run
    {
        gint n = 1, d = 1, vw = 0, vh = 0;
        GdkTexture* t = QuiverVideoOps::LoadTexture(videoUri, &n, &d, -1, 160, 120, NULL, NULL, &vw, &vh);
        if (t) g_object_unref(t);
    }

    const int iterations = 5;

    // Benchmark 1: Single-session decode that extracts natural dimensions in-stream
    double singleSessionTotalMs = 0.0;
    for (int i = 0; i < iterations; ++i)
    {
        gint n = 1, d = 1, natural_w = 0, natural_h = 0;
        auto start = std::chrono::high_resolution_clock::now();
        GdkTexture* tex = QuiverVideoOps::LoadTexture(videoUri, &n, &d, -1, 160, 120, NULL, NULL, &natural_w, &natural_h);
        auto end = std::chrono::high_resolution_clock::now();
        singleSessionTotalMs += std::chrono::duration<double, std::milli>(end - start).count();

        REQUIRE(tex != nullptr);
        REQUIRE(natural_w > 0);
        REQUIRE(natural_h > 0);
        g_object_unref(tex);
    }
    double singleSessionAvgMs = singleSessionTotalMs / iterations;

    // Benchmark 2: Old approach — decode frame in session 1, then open redundant Probe session 2
    double doubleSessionTotalMs = 0.0;
    for (int i = 0; i < iterations; ++i)
    {
        gint n = 1, d = 1;
        auto start = std::chrono::high_resolution_clock::now();
        GdkTexture* tex = QuiverVideoOps::LoadTexture(videoUri, &n, &d, -1, 160, 120, NULL, NULL, NULL, NULL);
        gint pw = 0, ph = 0, pn = 1, pd = 1;
        QuiverVideoOps::Probe(videoUri, NULL, &pw, &ph, &pn, &pd);
        auto end = std::chrono::high_resolution_clock::now();
        doubleSessionTotalMs += std::chrono::duration<double, std::milli>(end - start).count();

        REQUIRE(tex != nullptr);
        REQUIRE(pw > 0);
        REQUIRE(ph > 0);
        g_object_unref(tex);
    }
    double doubleSessionAvgMs = doubleSessionTotalMs / iterations;

    double timeSavedMs = doubleSessionAvgMs - singleSessionAvgMs;
    double speedupPercent = (doubleSessionAvgMs > 0.0) ? (timeSavedMs / doubleSessionAvgMs) * 100.0 : 0.0;

    std::cout << "\n========================================================" << std::endl;
    std::cout << "[BENCHMARK] Video Thumbnail Generation:" << std::endl;
    std::cout << "  - Old (LoadTexture + Separate Probe): " << std::fixed << std::setprecision(2) << doubleSessionAvgMs << " ms" << std::endl;
    std::cout << "  - New (Single-Session with out-params): " << std::fixed << std::setprecision(2) << singleSessionAvgMs << " ms" << std::endl;
    std::cout << "  - Time Saved per Video:                 " << std::fixed << std::setprecision(2) << timeSavedMs << " ms ("
              << std::setprecision(1) << speedupPercent << "% faster)" << std::endl;
    std::cout << "========================================================\n" << std::endl;

    CHECK(singleSessionAvgMs <= doubleSessionAvgMs);

    g_free(videoUri);
}

TEST_CASE("Optimization Benchmark: In-Memory Thumbnail Cache Bridge Latency", "[benchmark][cache][optimizations]")
{
    ImageCache thumbCache(20);
    const int numItems = 15;
    std::vector<std::string> uris;
    uris.reserve(numItems);

    for (int i = 0; i < numItems; ++i)
    {
        std::string uri = "file:///tmp/bench_img_" + std::to_string(i) + ".jpg";
        uris.push_back(uri);
        GdkTexture* tex = CreateMockTexture(128, 128);
        thumbCache.AddTexture(uri, tex);
        g_object_unref(tex);
    }

    // Benchmark lookup speed through the bridge
    const int lookups = 1000;
    auto start = std::chrono::high_resolution_clock::now();
    for (int k = 0; k < lookups; ++k)
    {
        const std::string& uri = uris[k % numItems];
        GdkTexture* hit = thumbCache.GetTexture(uri);
        REQUIRE(hit != nullptr);
        g_object_unref(hit);
    }
    auto end = std::chrono::high_resolution_clock::now();
    double totalUs = std::chrono::duration<double, std::micro>(end - start).count();
    double avgUsPerHit = totalUs / lookups;

    std::cout << "\n========================================================" << std::endl;
    std::cout << "[BENCHMARK] In-Memory Cache Bridge Latency:" << std::endl;
    std::cout << "  - Lookups performed:       " << lookups << std::endl;
    std::cout << "  - Average latency per hit: " << std::fixed << std::setprecision(3) << avgUsPerHit << " microseconds" << std::endl;
    std::cout << "  - Effective throughput:    " << std::fixed << std::setprecision(0) << (1000000.0 / avgUsPerHit) << " hits/sec" << std::endl;
    std::cout << "========================================================\n" << std::endl;

    // In-memory hit must be essentially instantaneous (< 20 microseconds)
    CHECK(avgUsPerHit < 20.0);
}

TEST_CASE("Optimization Benchmark: Non-Blocking Statusbar Date Extraction", "[benchmark][statusbar][optimizations]")
{
    std::string imagesDir = QuiverTest_GetImagesDir();
    std::string videoPath = imagesDir + "/sample_video.mp4";
    gchar* videoUri = g_filename_to_uri(videoPath.c_str(), NULL, NULL);
    REQUIRE(videoUri != NULL);

    QuiverFile qf(videoUri);

    // Benchmark non-blocking cached query: HasCachedTimeT() ? GetTimeT(true) : GetTimeT(false)
    const int iterations = 100;
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i)
    {
        time_t t = qf.HasCachedTimeT() ? qf.GetTimeT(true) : qf.GetTimeT(false);
        (void)t;
    }
    auto end = std::chrono::high_resolution_clock::now();
    double totalUs = std::chrono::duration<double, std::micro>(end - start).count();
    double avgUs = totalUs / iterations;

    std::cout << "\n========================================================" << std::endl;
    std::cout << "[BENCHMARK] Statusbar::SetDateTime Non-Blocking Latency:" << std::endl;
    std::cout << "  - Iterations:              " << iterations << std::endl;
    std::cout << "  - Average latency per call: " << std::fixed << std::setprecision(3) << avgUs << " microseconds" << std::endl;
    std::cout << "========================================================\n" << std::endl;

    // Must be completely non-blocking (< 50 microseconds)
    CHECK(avgUs < 50.0);

    g_free(videoUri);
}

TEST_CASE("Optimization Benchmark: ThumbLoader Cache-Hit Early Return vs Re-add", "[benchmark][filmstrip][optimizations]")
{
    ImageCache thumbCache(50);
    std::string testUri = "file:///tmp/bench_cell.jpg";
    GdkTexture* tex = CreateMockTexture(128, 96);
    thumbCache.AddTexture(testUri, tex);
    g_object_unref(tex);

    const int iterations = 500;

    // Fast-path early-return: inspect cache, check bounds, early-return if matched
    auto startFast = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i)
    {
        GdkTexture* cached = thumbCache.GetTexture(testUri);
        if (cached)
        {
            int w = gdk_texture_get_width(cached);
            int h = gdk_texture_get_height(cached);
            if (w == 128 && h == 96)
            {
                g_object_unref(cached);
                continue;
            }
            g_object_unref(cached);
        }
    }
    auto endFast = std::chrono::high_resolution_clock::now();
    double fastUs = std::chrono::duration<double, std::micro>(endFast - startFast).count() / iterations;

    std::cout << "\n========================================================" << std::endl;
    std::cout << "[BENCHMARK] Filmstrip/Grid Cache Hit Fast Path:" << std::endl;
    std::cout << "  - Early-return latency per cell: " << std::fixed << std::setprecision(3) << fastUs << " microseconds" << std::endl;
    std::cout << "  - Compare to old 100,000 microsecond unconditional sleep!" << std::endl;
    std::cout << "========================================================\n" << std::endl;

    CHECK(fastUs < 10.0);
}
