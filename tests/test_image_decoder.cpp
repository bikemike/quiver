#include "ImageDecoder.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <vector>
#include <string>
#include <cassert>
#include <cstring>
#include <glib.h>
#include <glib/gstdio.h>
#if HAVE_GDK_PIXBUF
#include <gdk-pixbuf/gdk-pixbuf.h>
#endif

GtkApplication *g_pApp = nullptr;

using Clock = std::chrono::high_resolution_clock;

struct BenchmarkResult {
    std::string test_name;
    std::string image_name;
    double glycin_ms = -1.0;
    double pixbuf_ms = -1.0;
};

static std::vector<BenchmarkResult> s_results;

static void run_benchmarks_for_image(const char* filepath, const char* mimetype, const char* label)
{
    gchar *content_type = NULL;
    gchar *guessed_mime = NULL;
    const char *actual_mime = mimetype;
    if (!actual_mime || strlen(actual_mime) == 0)
    {
        content_type = g_content_type_guess(filepath, NULL, 0, NULL);
        guessed_mime = content_type ? g_content_type_get_mime_type(content_type) : NULL;
        actual_mime = guessed_mime ? guessed_mime : "image/jpeg";
    }

    std::cout << "\n============================================================\n";
    std::cout << "Testing & Benchmarking: " << label << "\n  Path: " << filepath << "\n  MIME: " << actual_mime << "\n";
    std::cout << "============================================================\n";

    GFile *file = g_file_new_for_path(filepath);
    assert(file != NULL);

    // Warm-up and test backends
    std::vector<ImageDecoder::Backend> backends;
    if (ImageDecoder::IsBackendSupported(ImageDecoder::Backend::GLYCIN))
        backends.push_back(ImageDecoder::Backend::GLYCIN);
    if (ImageDecoder::IsBackendSupported(ImageDecoder::Backend::PIXBUF))
        backends.push_back(ImageDecoder::Backend::PIXBUF);

    BenchmarkResult probe_res = {"Dimension Probe", label, -1.0, -1.0};
    BenchmarkResult texture_res = {"Decode Texture", label, -1.0, -1.0};
    BenchmarkResult bytes_res = {"Decode Bytes (EXIF)", label, -1.0, -1.0};
    BenchmarkResult thumb_res = {"Save Thumbnail", label, -1.0, -1.0};

    for (auto backend : backends)
    {
        bool is_glycin = (backend == ImageDecoder::Backend::GLYCIN);
        const char *name = is_glycin ? "GLYCIN" : "PIXBUF";
        ImageDecoder::SetBackend(backend);
        std::cout << "\n>>> Backend: " << name << "\n";

        // 1. Dimension Probing
        int probe_w = 0, probe_h = 0;
        auto t0 = Clock::now();
        bool ok_probe = ImageDecoder::GetDimensions(file, mimetype, &probe_w, &probe_h);
        auto t1 = Clock::now();
        double ms_probe = std::chrono::duration<double, std::milli>(t1 - t0).count();
        if (is_glycin) probe_res.glycin_ms = ms_probe; else probe_res.pixbuf_ms = ms_probe;

        std::cout << "  [Probe]        " << probe_w << "x" << probe_h
                  << " in " << std::fixed << std::setprecision(3) << ms_probe << " ms"
                  << " -> " << (ok_probe ? "PASS" : "FAIL") << "\n";
        assert(ok_probe && probe_w > 0 && probe_h > 0);

        // 2. Full Texture Decode
        t0 = Clock::now();
        GdkTexture *tex = ImageDecoder::DecodeFileTexture(file, mimetype);
        t1 = Clock::now();
        double ms_tex = std::chrono::duration<double, std::milli>(t1 - t0).count();
        if (is_glycin) texture_res.glycin_ms = ms_tex; else texture_res.pixbuf_ms = ms_tex;

        assert(tex != NULL);
        std::cout << "  [Texture]      " << gdk_texture_get_width(tex) << "x" << gdk_texture_get_height(tex)
                  << " in " << std::fixed << std::setprecision(3) << ms_tex << " ms"
                  << " -> PASS\n";

        // 3. Memory Bytes / EXIF Thumbnail Decode
        char *contents = NULL;
        gsize length = 0;
        g_file_load_contents(file, NULL, &contents, &length, NULL, NULL);
        assert(contents != NULL && length > 0);
        GBytes *bytes = g_bytes_new_take(contents, length);

        t0 = Clock::now();
        GdkTexture *mem_tex = ImageDecoder::DecodeBytesTexture(bytes, mimetype);
        t1 = Clock::now();
        double ms_mem = std::chrono::duration<double, std::milli>(t1 - t0).count();
        if (is_glycin) bytes_res.glycin_ms = ms_mem; else bytes_res.pixbuf_ms = ms_mem;

        assert(mem_tex != NULL);
        std::cout << "  [Bytes/EXIF]   " << gdk_texture_get_width(mem_tex) << "x" << gdk_texture_get_height(mem_tex)
                  << " in " << std::fixed << std::setprecision(3) << ms_mem << " ms"
                  << " -> PASS\n";
        g_object_unref(mem_tex);
        g_bytes_unref(bytes);

        // 4. FreeDesktop Thumbnail Persistence
        char tmp_thumb_path[] = "/tmp/quiver_test_XXXXXX.png";
        int fd = g_mkstemp(tmp_thumb_path);
        close(fd);

        t0 = Clock::now();
        bool saved = ImageDecoder::TextureSaveThumbnail(tex, tmp_thumb_path, "file:///test/img.png", 123456789, -1, 128, 128, 1);
        t1 = Clock::now();
        double ms_save = std::chrono::duration<double, std::milli>(t1 - t0).count();
        if (is_glycin) thumb_res.glycin_ms = ms_save; else thumb_res.pixbuf_ms = ms_save;

        std::cout << "  [Thumbnail]    Saved in " << std::fixed << std::setprecision(3) << ms_save << " ms"
                  << " -> " << (saved ? "PASS" : "FAIL") << "\n";
        assert(saved);

        g_unlink(tmp_thumb_path);
        g_object_unref(tex);
    }

    g_object_unref(file);
    if (guessed_mime) g_free(guessed_mime);
    if (content_type) g_free(content_type);

    s_results.push_back(probe_res);
    s_results.push_back(texture_res);
    s_results.push_back(bytes_res);
    s_results.push_back(thumb_res);
    s_results.push_back(bytes_res);
    s_results.push_back(thumb_res);
}

static void print_comparison_table()
{
    std::cout << "\n==========================================================================================\n";
    std::cout << "                         IMAGE DECODER BACKEND BENCHMARK SUMMARY\n";
    std::cout << "==========================================================================================\n";
    std::cout << std::left << std::setw(22) << "Image"
              << std::setw(22) << "Operation"
              << std::right << std::setw(14) << "Glycin (ms)"
              << std::setw(14) << "Pixbuf (ms)"
              << "  " << std::setw(18) << "Delta / Ratio"
              << "\n";
    std::cout << "------------------------------------------------------------------------------------------\n";

    for (const auto& r : s_results)
    {
        std::cout << std::left << std::setw(22) << r.image_name
                  << std::setw(22) << r.test_name
                  << std::right;

        if (r.glycin_ms >= 0)
            std::cout << std::setw(14) << std::fixed << std::setprecision(3) << r.glycin_ms;
        else
            std::cout << std::setw(14) << "N/A";

        if (r.pixbuf_ms >= 0)
            std::cout << std::setw(14) << std::fixed << std::setprecision(3) << r.pixbuf_ms;
        else
            std::cout << std::setw(14) << "N/A";

        if (r.glycin_ms >= 0 && r.pixbuf_ms >= 0)
        {
            double diff = r.glycin_ms - r.pixbuf_ms;
            double ratio = (r.pixbuf_ms > 0) ? (r.glycin_ms / r.pixbuf_ms) : 1.0;
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(2) << ratio << "x ("
               << (diff > 0 ? "+" : "") << std::setprecision(2) << diff << "ms)";
            std::cout << "  " << std::setw(18) << ss.str();
        }
        else
        {
            std::cout << "  " << std::setw(18) << "-";
        }
        std::cout << "\n";
    }
    std::cout << "==========================================================================================\n";
}

int main(int argc, char **argv)
{
    gtk_init();

    std::cout << "============================================================\n";
    std::cout << "Running ImageDecoder Unit Test Suite & Benchmark Comparison\n";
    std::cout << "============================================================\n";

    std::vector<std::string> test_files;

    if (argc > 1)
    {
        for (int i = 1; i < argc; ++i)
        {
            if (g_file_test(argv[i], G_FILE_TEST_EXISTS))
            {
                test_files.push_back(argv[i]);
            }
            else
            {
                std::cerr << "Warning: file not found: " << argv[i] << "\n";
            }
        }
    }
    else
    {
        // 1. Scan tests/images directory
        const char *images_dir = "/workspace/tests/images";
        if (g_file_test(images_dir, G_FILE_TEST_IS_DIR))
        {
            GDir *dir = g_dir_open(images_dir, 0, NULL);
            if (dir)
            {
                const gchar *entry = NULL;
                while ((entry = g_dir_read_name(dir)) != NULL)
                {
                    if (entry[0] == '.') continue;
                    gchar *full_path = g_build_filename(images_dir, entry, NULL);
                    gchar *ct = g_content_type_guess(full_path, NULL, 0, NULL);
                    gchar *mime = ct ? g_content_type_get_mime_type(ct) : NULL;
                    if (mime && g_str_has_prefix(mime, "image/"))
                    {
                        test_files.push_back(full_path);
                    }
                    g_free(ct);
                    g_free(mime);
                    g_free(full_path);
                }
                g_dir_close(dir);
            }
        }

        // 2. Default baseline icons
        const char *defaults[] = {
            "/workspace/data/icons/128x128/quiver-icon-app.png",
            "/workspace/data/pause.png",
            "/workspace/data/icons/64x64/quiver-icon-app.png"
        };
        for (const char *path : defaults)
        {
            if (g_file_test(path, G_FILE_TEST_EXISTS))
                test_files.push_back(path);
        }
    }

    for (const auto &file : test_files)
    {
        gchar *basename = g_path_get_basename(file.c_str());
        run_benchmarks_for_image(file.c_str(), NULL, basename);
        g_free(basename);
    }

    print_comparison_table();

    std::cout << "\n>>> ALL IMAGEDECODER UNIT TESTS AND BENCHMARKS PASSED! <<<\n";
    std::cout << "Tip: Drop full-size photos into /workspace/tests/images/ or pass them as args:\n";
    std::cout << "     xvfb-run -a ./build/tests/test_image_decoder /path/to/large_photo.jpg\n\n";
    return 0;
}

