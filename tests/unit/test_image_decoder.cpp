#include <catch2/catch_test_macros.hpp>
#include "ImageDecoder.h"
#include "test_helpers.h"
#include <glib.h>
#include <glib/gstdio.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <string>
#include <vector>

TEST_CASE("ImageDecoder Dimensions Probing and Backend Selection", "[unit][decoder][fast]")
{
    std::string imagesDir = QuiverTest_GetImagesDir();
    std::string sampleJpg = imagesDir + "/sample_4k.jpg";
    GFile* file = g_file_new_for_path(sampleJpg.c_str());
    REQUIRE(file != nullptr);

    std::vector<ImageDecoder::Backend> backends;
    if (ImageDecoder::IsBackendSupported(ImageDecoder::Backend::PIXBUF))
        backends.push_back(ImageDecoder::Backend::PIXBUF);
    if (ImageDecoder::IsBackendSupported(ImageDecoder::Backend::GLYCIN))
        backends.push_back(ImageDecoder::Backend::GLYCIN);

    REQUIRE_FALSE(backends.empty());

    for (auto backend : backends)
    {
        DYNAMIC_SECTION("Backend: " << (backend == ImageDecoder::Backend::GLYCIN ? "GLYCIN" : "PIXBUF"))
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

    SECTION("DecodeFileTexture returns valid GdkTexture")
    {
        GdkTexture* tex = ImageDecoder::DecodeFileTexture(file, "image/jpeg");
        REQUIRE(tex != nullptr);
        REQUIRE(gdk_texture_get_width(tex) > 0);
        REQUIRE(gdk_texture_get_height(tex) > 0);

        g_object_unref(tex);
    }

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

    g_object_unref(file);
}

TEST_CASE("ImageDecoder FreeDesktop Thumbnail Specification Metadata", "[unit][decoder][fast]")
{
    char tmpThumb[] = "/tmp/quiver_thumb_test_XXXXXX.png";
    int fd = g_mkstemp(tmpThumb);
    REQUIRE(fd >= 0);
    close(fd);

    GdkPixbuf* thumb = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, 64, 64);
    REQUIRE(thumb != nullptr);
    gdk_pixbuf_fill(thumb, 0xFF00FF88);

    bool saved = ImageDecoder::SaveThumbnail(thumb, tmpThumb, "file:///path/to/test.jpg", 1234567890, 4096, 1920, 1080, 1);
    REQUIRE(saved);

    // Reopen thumbnail and verify specification keys
    GdkPixbuf* check = gdk_pixbuf_new_from_file(tmpThumb, NULL);
    REQUIRE(check != nullptr);

    const char* uriTag = gdk_pixbuf_get_option(check, "tEXt::Thumb::URI");
    const char* mtimeTag = gdk_pixbuf_get_option(check, "tEXt::Thumb::MTime");

    REQUIRE(uriTag != nullptr);
    REQUIRE(std::string(uriTag) == "file:///path/to/test.jpg");

    REQUIRE(mtimeTag != nullptr);
    REQUIRE(std::string(mtimeTag) == "1234567890");

    g_object_unref(check);
    g_object_unref(thumb);
    g_unlink(tmpThumb);
}
