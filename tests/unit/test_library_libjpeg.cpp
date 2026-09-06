#include <catch2/catch_test_macros.hpp>
#include "ImageSaveManager.h"
#include "QuiverFile.h"
#include "test_helpers.h"
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <string>

extern "C" {
#include <jpeglib.h>
}

TEST_CASE("libjpeg and ImageSaveManager Integration", "[lib][libjpeg]")
{
    ImageSaveManager::Reset();
    ImageSaveManagerPtr sm = ImageSaveManager::GetInstance();
    REQUIRE(sm != nullptr);

    SECTION("JPEG format is registered and supported")
    {
        REQUIRE(sm->IsFormatSupported("image/jpeg"));
    }

    SECTION("Saving pixbuf as JPEG in-place via ImageSaveManager")
    {
        std::string imagesDir = QuiverTest_GetImagesDir();
        std::string sampleJpg = imagesDir + "/sample_4k.jpg";

        // Create temporary copy of sample_4k.jpg
        char tmpCopy[] = "/tmp/quiver_save_test_XXXXXX.jpg";
        int fd = g_mkstemp(tmpCopy);
        REQUIRE(fd >= 0);
        close(fd);

        char* contents = nullptr;
        gsize length = 0;
        REQUIRE(g_file_get_contents(sampleJpg.c_str(), &contents, &length, NULL));
        REQUIRE(g_file_set_contents(tmpCopy, contents, length, NULL));
        g_free(contents);

        gchar* tmpUri = g_filename_to_uri(tmpCopy, NULL, NULL);
        REQUIRE(tmpUri != NULL);

        QuiverFile qf(tmpUri);

        // Create 120x80 test pixbuf (RGB 3-channel, no alpha for JPEG)
        GdkPixbuf* pb = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 120, 80);
        REQUIRE(pb != nullptr);
        gdk_pixbuf_fill(pb, 0x00FF0000); // Green

        bool saved = sm->SaveImage(qf, pb, nullptr, nullptr);
        REQUIRE(saved);

        // Verify the saved file is a valid JPEG with 120x80 dimensions
        FILE* fp = fopen(tmpCopy, "rb");
        REQUIRE(fp != nullptr);

        struct jpeg_decompress_struct cinfo;
        struct jpeg_error_mgr jerr;
        cinfo.err = jpeg_std_error(&jerr);
        jpeg_create_decompress(&cinfo);
        jpeg_stdio_src(&cinfo, fp);
        int header_res = jpeg_read_header(&cinfo, TRUE);
        REQUIRE(header_res == JPEG_HEADER_OK);
        REQUIRE(cinfo.image_width == 120);
        REQUIRE(cinfo.image_height == 80);

        jpeg_destroy_decompress(&cinfo);
        fclose(fp);

        g_unlink(tmpCopy);
        g_object_unref(pb);
        g_free(tmpUri);
    }

    SECTION("Direct libjpeg compress and decompress pipeline")
    {
        char tmpDirect[] = "/tmp/quiver_direct_jpeg_XXXXXX.jpg";
        int fd = g_mkstemp(tmpDirect);
        REQUIRE(fd >= 0);
        close(fd);

        FILE* fp = fopen(tmpDirect, "wb");
        REQUIRE(fp != nullptr);

        struct jpeg_compress_struct cinfo;
        struct jpeg_error_mgr jerr;
        cinfo.err = jpeg_std_error(&jerr);
        jpeg_create_compress(&cinfo);
        jpeg_stdio_dest(&cinfo, fp);

        cinfo.image_width = 64;
        cinfo.image_height = 48;
        cinfo.input_components = 3;
        cinfo.in_color_space = JCS_RGB;
        jpeg_set_defaults(&cinfo);
        jpeg_set_quality(&cinfo, 90, TRUE);
        jpeg_start_compress(&cinfo, TRUE);

        std::vector<JSAMPLE> row(64 * 3, 200);
        JSAMPROW row_pointer[1] = { row.data() };
        while (cinfo.next_scanline < cinfo.image_height) {
            jpeg_write_scanlines(&cinfo, row_pointer, 1);
        }

        jpeg_finish_compress(&cinfo);
        jpeg_destroy_compress(&cinfo);
        fclose(fp);

        // Verify decompression
        fp = fopen(tmpDirect, "rb");
        REQUIRE(fp != nullptr);

        struct jpeg_decompress_struct dinfo;
        struct jpeg_error_mgr djerr;
        dinfo.err = jpeg_std_error(&djerr);
        jpeg_create_decompress(&dinfo);
        jpeg_stdio_src(&dinfo, fp);
        REQUIRE(jpeg_read_header(&dinfo, TRUE) == JPEG_HEADER_OK);
        REQUIRE(dinfo.image_width == 64);
        REQUIRE(dinfo.image_height == 48);

        jpeg_destroy_decompress(&dinfo);
        fclose(fp);

        g_unlink(tmpDirect);
    }

    ImageSaveManager::Reset();
}
