#include <config.h>
#include <catch2/catch_test_macros.hpp>
#include "ImageDecoder.h"
#include "test_helpers.h"
#include <glib.h>
#include <glib/gstdio.h>
#if HAVE_GDK_PIXBUF
#include <gdk-pixbuf/gdk-pixbuf.h>
#endif
#include <string>
#include <vector>

TEST_CASE("ImageDecoder Dimensions Probing and Backend Selection", "[unit][decoder][fast]")
{
    std::string imagesDir = QuiverTest_GetImagesDir();
    std::string sampleJpg = imagesDir + "/sample_4k.jpg";
    GFile* file = g_file_new_for_path(sampleJpg.c_str());
    REQUIRE(file != nullptr);

    std::vector<ImageDecoder::Backend> backends;
    backends.push_back(ImageDecoder::Backend::AUTO);
    if (ImageDecoder::IsBackendSupported(ImageDecoder::Backend::PIXBUF))
        backends.push_back(ImageDecoder::Backend::PIXBUF);
    if (ImageDecoder::IsBackendSupported(ImageDecoder::Backend::GLYCIN))
        backends.push_back(ImageDecoder::Backend::GLYCIN);

    REQUIRE_FALSE(backends.empty());

    for (auto backend : backends)
    {
        std::string name = "AUTO";
        if (backend == ImageDecoder::Backend::GLYCIN) name = "GLYCIN";
        else if (backend == ImageDecoder::Backend::PIXBUF) name = "PIXBUF";

        DYNAMIC_SECTION("Backend: " << name)
        {
            ImageDecoder::SetBackend(backend);
            REQUIRE(ImageDecoder::GetBackend() == backend);

            int w = 0, h = 0;
            bool ok = ImageDecoder::GetDimensions(file, "image/jpeg", &w, &h);
            REQUIRE(ok == true);
            REQUIRE(w > 0);
            REQUIRE(h > 0);
        }
    }

    g_object_unref(file);
}

TEST_CASE("ImageDecoder Full Pixbuf and Texture Decoding", "[unit][decoder]")
{
    std::string imagesDir = QuiverTest_GetImagesDir();
    std::string sampleJpg = imagesDir + "/sample_4k.jpg";
    GFile* file = g_file_new_for_path(sampleJpg.c_str());
    REQUIRE(file != nullptr);

#if HAVE_GDK_PIXBUF
    SECTION("DecodeFilePixbuf returns valid pixbuf matching probe dimensions")
    {
        int probe_w = 0, probe_h = 0;
        REQUIRE(ImageDecoder::GetDimensions(file, "image/jpeg", &probe_w, &probe_h));

        GdkPixbuf* pb = ImageDecoder::DecodeFilePixbuf(file, "image/jpeg");
        REQUIRE(pb != nullptr);
        REQUIRE(gdk_pixbuf_get_width(pb) == probe_w);
        REQUIRE(gdk_pixbuf_get_height(pb) == probe_h);

        g_object_unref(pb);
    }
#endif

    SECTION("DecodeFileTexture returns valid GdkTexture")
    {
        GdkTexture* tex = ImageDecoder::DecodeFileTexture(file, "image/jpeg");
        REQUIRE(tex != nullptr);
        REQUIRE(gdk_texture_get_width(tex) > 0);
        REQUIRE(gdk_texture_get_height(tex) > 0);

        g_object_unref(tex);
    }

#if HAVE_GDK_PIXBUF
    SECTION("DecodeBytesPixbuf in-memory buffer decode")
    {
        char* contents = nullptr;
        gsize length = 0;
        REQUIRE(g_file_load_contents(file, NULL, &contents, &length, NULL, NULL));
        GBytes* bytes = g_bytes_new_take(contents, length);

        GdkPixbuf* mem_pb = ImageDecoder::DecodeBytesPixbuf(bytes, "image/jpeg");
        REQUIRE(mem_pb != nullptr);
        REQUIRE(gdk_pixbuf_get_width(mem_pb) > 0);
        REQUIRE(gdk_pixbuf_get_height(mem_pb) > 0);

        g_object_unref(mem_pb);
        g_bytes_unref(bytes);
    }
#endif

    SECTION("DecodeBytesTexture in-memory buffer decode to GdkTexture")
    {
        char* contents = nullptr;
        gsize length = 0;
        REQUIRE(g_file_load_contents(file, NULL, &contents, &length, NULL, NULL));
        GBytes* bytes = g_bytes_new_take(contents, length);

        GdkTexture* mem_tex = ImageDecoder::DecodeBytesTexture(bytes, "image/jpeg");
        REQUIRE(mem_tex != nullptr);
        REQUIRE(gdk_texture_get_width(mem_tex) > 0);
        REQUIRE(gdk_texture_get_height(mem_tex) > 0);

        g_object_unref(mem_tex);
        g_bytes_unref(bytes);
    }

    g_object_unref(file);
}

TEST_CASE("ImageDecoder FreeDesktop Thumbnail Specification Metadata", "[unit][decoder][fast]")
{
    char tmpThumb[] = "/tmp/quiver_thumb_test_XXXXXX.png";
    int fd = g_mkstemp(tmpThumb);
    REQUIRE(fd >= 0);
    close(fd);

    guint32 *buf = (guint32*)g_malloc(64 * 64 * 4);
    for (int i = 0; i < 64 * 64; i++) buf[i] = 0xFF00FF88;
    GBytes *b = g_bytes_new_take(buf, 64 * 64 * 4);
    GdkTexture *tex = gdk_memory_texture_new(64, 64, GDK_MEMORY_R8G8B8A8, b, 64 * 4);
    g_bytes_unref(b);
    REQUIRE(tex != nullptr);

    bool saved = ImageDecoder::TextureSaveThumbnail(tex, tmpThumb, "file:///path/to/test.jpg", 1234567890, 4096, 1920, 1080, 1);
    REQUIRE(saved);

    gchar *content = nullptr;
    gsize len = 0;
    REQUIRE(g_file_get_contents(tmpThumb, &content, &len, NULL));
    REQUIRE(len > 0);
    g_free(content);

    g_object_unref(tex);
    g_unlink(tmpThumb);

#if HAVE_GDK_PIXBUF
    char tmpThumbPb[] = "/tmp/quiver_thumb_pb_XXXXXX.png";
    fd = g_mkstemp(tmpThumbPb);
    REQUIRE(fd >= 0);
    close(fd);

    GdkPixbuf* thumb = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, 64, 64);
    REQUIRE(thumb != nullptr);
    gdk_pixbuf_fill(thumb, 0xFF00FF88);

    saved = ImageDecoder::SaveThumbnail(thumb, tmpThumbPb, "file:///path/to/test.jpg", 1234567890, 4096, 1920, 1080, 1);
    REQUIRE(saved);

    // Reopen thumbnail and verify specification keys
    GdkPixbuf* check = gdk_pixbuf_new_from_file(tmpThumbPb, NULL);
    REQUIRE(check != nullptr);

    const char* uriTag = gdk_pixbuf_get_option(check, "tEXt::Thumb::URI");
    const char* mtimeTag = gdk_pixbuf_get_option(check, "tEXt::Thumb::MTime");

    REQUIRE(uriTag != nullptr);
    REQUIRE(std::string(uriTag) == "file:///path/to/test.jpg");
    REQUIRE(mtimeTag != nullptr);
    REQUIRE(std::string(mtimeTag) == "1234567890");

    g_object_unref(check);
    g_object_unref(thumb);
    g_unlink(tmpThumbPb);
#endif
}

static void write_bytes(const char* path, const std::vector<uint8_t>& bytes)
{
    FILE* f = fopen(path, "wb");
    REQUIRE(f != nullptr);
    size_t wrote = fwrite(bytes.data(), 1, bytes.size(), f);
    REQUIRE(wrote == bytes.size());
    fclose(f);
}

TEST_CASE("ImageDecoder Fast Header Dimensions", "[unit][decoder][fast]")
{
    const std::string dir = "/tmp/quiver-dimtest";

    const uint8_t png[] = {
        0x89,'P','N','G',0x0D,0x0A,0x1A,0x0A,
        0x00,0x00,0x00,0x0D, 'I','H','D','R',
        0x00,0x00,0x02,0x80, 0x00,0x00,0x01,0xE0, /*** 640 x 480 ***/
        0x08,0x06,0x00,0x00,0x00
    };
    const uint8_t gif[] = {
        'G','I','F','8','9','a', 0x40,0x01, 0xC8,0x00, 0x00,0x00,0x00,0x00
    };
    const uint8_t bmp[] = {
        'B','M', 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
        0x20,0x03,0x00,0x00,  0xA8,0xFD,0xFF,0xFF     /* 800 x -600 (top-down) */
    };
    const uint8_t jpg[] = {
        0xFF,0xD8,
        0xFF,0xE0,0x00,0x10,'J','F','I','F',0x00,0x01,0x01,0x00,0x00,0x01,0x00,0x01,0x00,0x00,
        0xFF,0xC0,0x00,0x11,0x08, 0x01,0xE0, 0x02,0x80, 0x03,0x01,0x22,0x00,0x02,0x11,0x01,0x03,0x11,0x01,
        0xFF,0xD9
    };
    const uint8_t webp[] = {
        'R','I','F','F',0x00,0x00,0x00,0x00,'W','E','B','P',
        'V','P','8','X',0x0A,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x7F,0x02,0x00, 0xDF,0x01,0x00                    /* 640 x 480 */
    };
    const uint8_t tiff[] = {
        'I','I',0x2A,0x00, 0x08,0x00,0x00,0x00, 0x02,0x00,
        0x00,0x01,0x04,0x00, 0x01,0x00,0x00,0x00, 0x80,0x07,0x00,0x00,  /* tag256 -> 1920 */
        0x01,0x01,0x04,0x00, 0x01,0x00,0x00,0x00, 0x38,0x04,0x00,0x00   /* tag257 -> 1080 */
    };

    struct Fixture { std::string file; std::vector<uint8_t> data; int w; int h; };
    std::vector<Fixture> fixtures = {
        { dir + "/test.png", std::vector<uint8_t>(png, png + sizeof(png)), 640, 480 },
        { dir + "/test.gif", std::vector<uint8_t>(gif, gif + sizeof(gif)), 320, 200 },
        { dir + "/test.bmp", std::vector<uint8_t>(bmp, bmp + sizeof(bmp)), 800, 600 },
        { dir + "/test.jpg", std::vector<uint8_t>(jpg, jpg + sizeof(jpg)), 640, 480 },
        { dir + "/test.webp", std::vector<uint8_t>(webp, webp + sizeof(webp)), 640, 480 },
        { dir + "/test.tiff", std::vector<uint8_t>(tiff, tiff + sizeof(tiff)), 1920, 1080 },
    };

    g_mkdir_with_parents(dir.c_str(), 0700);

    for (auto& fix : fixtures)
    {
        DYNAMIC_SECTION(fix.file)
        {
            write_bytes(fix.file.c_str(), fix.data);
            GFile* file = g_file_new_for_path(fix.file.c_str());
            REQUIRE(file != nullptr);

            int w = -9, h = -9;
            bool ok = ImageDecoder::GetDimensions(file, "application/octet-stream", &w, &h);

            REQUIRE(ok == true);
            REQUIRE(w == fix.w);
            REQUIRE(h == fix.h);
            g_object_unref(file);
            g_unlink(fix.file.c_str());
        }
    }

    g_rmdir(dir.c_str());
}
