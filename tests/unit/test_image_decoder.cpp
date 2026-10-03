#include <config.h>
#include <catch2/catch_test_macros.hpp>
#include "ImageDecoder.h"
#include "test_helpers.h"
#include <glib.h>
#include <glib/gstdio.h>
#if HAVE_GDK_PIXBUF
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <cstring>
#include "QuiverUtils.h"
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

    std::vector<ImageDecoder::Backend> backends;
    backends.push_back(ImageDecoder::Backend::AUTO);
    if (ImageDecoder::IsBackendSupported(ImageDecoder::Backend::GLYCIN))
        backends.push_back(ImageDecoder::Backend::GLYCIN);
    if (ImageDecoder::IsBackendSupported(ImageDecoder::Backend::PIXBUF))
        backends.push_back(ImageDecoder::Backend::PIXBUF);

    ImageDecoder::Backend originalBackend = ImageDecoder::GetBackend();

    for (auto backend : backends)
    {
        std::string name = "AUTO";
        if (backend == ImageDecoder::Backend::GLYCIN) name = "GLYCIN";
        else if (backend == ImageDecoder::Backend::PIXBUF) name = "PIXBUF";

        DYNAMIC_SECTION("Backend: " << name)
        {
            ImageDecoder::SetBackend(backend);

#if HAVE_GDK_PIXBUF
            int probe_w = 0, probe_h = 0;
            REQUIRE(ImageDecoder::GetDimensions(file, "image/jpeg", &probe_w, &probe_h));

            GdkPixbuf* pb = ImageDecoder::DecodeFilePixbuf(file, "image/jpeg");
            REQUIRE(pb != nullptr);
            REQUIRE(gdk_pixbuf_get_width(pb) == probe_w);
            REQUIRE(gdk_pixbuf_get_height(pb) == probe_h);
            g_object_unref(pb);
#endif

            GdkTexture* tex = ImageDecoder::DecodeFileTexture(file, "image/jpeg");
            REQUIRE(tex != nullptr);
            REQUIRE(gdk_texture_get_width(tex) > 0);
            REQUIRE(gdk_texture_get_height(tex) > 0);
            g_object_unref(tex);

            char* contents = nullptr;
            gsize length = 0;
            REQUIRE(g_file_load_contents(file, NULL, &contents, &length, NULL, NULL));
            GBytes* bytes = g_bytes_new_take(contents, length);

#if HAVE_GDK_PIXBUF
            GdkPixbuf* mem_pb = ImageDecoder::DecodeBytesPixbuf(bytes, "image/jpeg");
            REQUIRE(mem_pb != nullptr);
            REQUIRE(gdk_pixbuf_get_width(mem_pb) > 0);
            REQUIRE(gdk_pixbuf_get_height(mem_pb) > 0);
            g_object_unref(mem_pb);
#endif

            GdkTexture* mem_tex = ImageDecoder::DecodeBytesTexture(bytes, "image/jpeg");
            REQUIRE(mem_tex != nullptr);
            REQUIRE(gdk_texture_get_width(mem_tex) > 0);
            REQUIRE(gdk_texture_get_height(mem_tex) > 0);
            g_object_unref(mem_tex);

            g_bytes_unref(bytes);
        }
    }

    ImageDecoder::SetBackend(originalBackend);
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

    bool saved = ImageDecoder::TextureSaveThumbnail(tex, tmpThumb, "file:///path/to/test.jpg", 1234567890, 4096, 1920, 1080, 6);
    REQUIRE(saved);

    // Reopen texture-saved thumbnail and verify specification keys
    GdkPixbuf* checkTex = gdk_pixbuf_new_from_file(tmpThumb, NULL);
    REQUIRE(checkTex != nullptr);
    const char* texUriTag = gdk_pixbuf_get_option(checkTex, "tEXt::Thumb::URI");
    const char* texMtimeTag = gdk_pixbuf_get_option(checkTex, "tEXt::Thumb::MTime");
    const char* texOriTag = gdk_pixbuf_get_option(checkTex, "tEXt::Thumb::Image::Orientation");
    const char* texWTag = gdk_pixbuf_get_option(checkTex, "tEXt::Thumb::Image::Width");
    const char* texHTag = gdk_pixbuf_get_option(checkTex, "tEXt::Thumb::Image::Height");

    REQUIRE(texUriTag != nullptr);
    REQUIRE(std::string(texUriTag) == "file:///path/to/test.jpg");
    REQUIRE(texMtimeTag != nullptr);
    REQUIRE(std::string(texMtimeTag) == "1234567890");
    REQUIRE(texOriTag != nullptr);
    REQUIRE(std::string(texOriTag) == "6");
    REQUIRE(texWTag != nullptr);
    REQUIRE(std::string(texWTag) == "1920");
    REQUIRE(texHTag != nullptr);
    REQUIRE(std::string(texHTag) == "1080");
    g_object_unref(checkTex);

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

    saved = ImageDecoder::SaveThumbnail(thumb, tmpThumbPb, "file:///path/to/test.jpg", 1234567890, 4096, 1920, 1080, 6);
    REQUIRE(saved);

    // Reopen thumbnail and verify specification keys
    GdkPixbuf* check = gdk_pixbuf_new_from_file(tmpThumbPb, NULL);
    REQUIRE(check != nullptr);

    const char* uriTag = gdk_pixbuf_get_option(check, "tEXt::Thumb::URI");
    const char* mtimeTag = gdk_pixbuf_get_option(check, "tEXt::Thumb::MTime");
    const char* oriTag = gdk_pixbuf_get_option(check, "tEXt::Thumb::Image::Orientation");
    const char* wTag = gdk_pixbuf_get_option(check, "tEXt::Thumb::Image::Width");
    const char* hTag = gdk_pixbuf_get_option(check, "tEXt::Thumb::Image::Height");

    REQUIRE(uriTag != nullptr);
    REQUIRE(std::string(uriTag) == "file:///path/to/test.jpg");
    REQUIRE(mtimeTag != nullptr);
    REQUIRE(std::string(mtimeTag) == "1234567890");
    REQUIRE(oriTag != nullptr);
    REQUIRE(std::string(oriTag) == "6");
    REQUIRE(wTag != nullptr);
    REQUIRE(std::string(wTag) == "1920");
    REQUIRE(hTag != nullptr);
    REQUIRE(std::string(hTag) == "1080");

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

// -------------------------------------------------------------------------
// Streaming JPEG header probe
// -------------------------------------------------------------------------
//
// The JPEG marker walk skips segment payloads instead of buffering them, so a
// frame header sitting past megabytes of metadata is still found.  These
// build such a file on the fly: a SOI, then a configurable amount of APP1 XMP,
// then a frame header.  A probe that reads a fixed prefix window cannot get
// past the XMP; this one never reads the payload at all.

namespace
{
    /* A JPEG with @xmp_bytes of XMP ahead of the frame header, reported as
     * @w x @h.  @extra_appn splits the XMP across several APP1 segments, which
     * is how phone exports accumulate it. */
    static std::vector<uint8_t> make_jpeg_with_xmp(uint32_t xmp_bytes,
                                                    int w, int h,
                                                    uint32_t extra_appn = 0)
    {
        std::vector<uint8_t> out;
        const uint8_t soi[2] = {0xFF, 0xD8};
        out.insert(out.end(), soi, soi + 2);

        /* A segment length is 16 bits, so a payload cannot exceed 65533 bytes.
         * Real exporters hit that ceiling and start a new APP1, which is why
         * the fixture file carries dozens of segments rather than one huge
         * one.  Chunk the same way here. */
        const uint32_t kMaxPayload = 65533;
        uint32_t chunks = (xmp_bytes + kMaxPayload - 1) / kMaxPayload;
        if (extra_appn > 0)
            chunks += extra_appn;
        uint32_t per_chunk = (0 == chunks) ? 0 : (xmp_bytes + chunks - 1) / chunks;
        if (per_chunk > kMaxPayload)
            per_chunk = kMaxPayload;
        for (uint32_t c = 0; c < chunks; ++c)
        {
            uint32_t payload = per_chunk;
            /* APPn payload: "http" + NUL + filler, length must fit in 16 bits */
            uint16_t seglen = (uint16_t)(payload + 2);
            out.push_back(0xFF);
            out.push_back(0xE1);
            out.push_back((uint8_t)(seglen >> 8));
            out.push_back((uint8_t)(seglen & 0xFF));
            const char *tag = "http";
            for (int i = 0; i < 4; ++i) out.push_back((uint8_t)tag[i]);
            for (uint32_t i = 4; i < payload; ++i) out.push_back((uint8_t)('a' + (i % 26)));
        }

        /* SOF0: len(2)=17, precision(1)=8, height(2), width(2), components */
        out.push_back(0xFF); out.push_back(0xC0);
        out.push_back(0x00); out.push_back(0x11);
        out.push_back(8);
        out.push_back((uint8_t)(h >> 8)); out.push_back((uint8_t)(h & 0xFF));
        out.push_back((uint8_t)(w >> 8)); out.push_back((uint8_t)(w & 0xFF));
        out.push_back(3);   /* components */
        for (int i = 0; i < 9; ++i) out.push_back(0x11);  /* filler for 3 comps */

        /* SOS then arbitrary entropy-coded bytes, so the file looks complete */
        out.push_back(0xFF); out.push_back(0xDA);
        out.push_back(0x00); out.push_back(0x0C);
        for (int i = 0; i < 12; ++i) out.push_back(0x00);
        for (int i = 0; i < 64; ++i) out.push_back((uint8_t)(i * 7));
        out.push_back(0xFF); out.push_back(0xD9);
        return out;
    }

    struct XmpCase { uint32_t xmp; uint32_t appn; int w; int h; const char *what; };
}

TEST_CASE("JPEG probe finds a frame header past megabytes of XMP", "[unit][decoder][jpeg]")
{
    std::string dir = QuiverTest_GetImagesDir() + "/probe_xmp";
    g_mkdir_with_parents(dir.c_str(), 0755);

    std::vector<XmpCase> cases = {
        {0,        0,  64,  48, "no metadata"},
        {100,      0,  64,  48, "tiny metadata"},
        {200000,   0, 800, 600, "metadata under the old 256 KB prefix"},
        {300000,   0, 640, 480, "metadata just over the old prefix"},
        {1900000,  0, 4032, 3024, "1.9 MB in one segment (phone export shape)"},
        {1900000, 33, 4032, 3024, "1.9 MB across 33 segments"},
        {4000000, 40, 1024, 768, "4 MB across many segments"},
    };

    for (const auto &c : cases)
    {
        DYNAMIC_SECTION(c.what)
        {
            std::string path = dir + "/x.jpg";
            std::vector<uint8_t> bytes = make_jpeg_with_xmp(c.xmp, c.w, c.h, c.appn);
            write_bytes(path.c_str(), bytes);

            GFile *file = g_file_new_for_path(path.c_str());
            REQUIRE(file != nullptr);

            int w = -1, h = -1;
            REQUIRE(ImageDecoder::ProbeJpegDimensions(file, &w, &h) == true);
            CHECK(w == c.w);
            CHECK(h == c.h);
            g_object_unref(file);
            g_unlink(path.c_str());
        }
    }
    g_rmdir(dir.c_str());
}

TEST_CASE("GetDimensions agrees with the JPEG probe on XMP-heavy files", "[unit][decoder][jpeg]")
{
    std::string dir = QuiverTest_GetImagesDir() + "/probe_xmp2";
    g_mkdir_with_parents(dir.c_str(), 0755);
    std::string path = dir + "/x.jpg";

    /* Past the old window, so this only passes if the streaming probe runs
     * before the prefix read. */
    std::vector<uint8_t> bytes = make_jpeg_with_xmp(1900000, 4032, 3024, 33);
    write_bytes(path.c_str(), bytes);

    GFile *file = g_file_new_for_path(path.c_str());
    REQUIRE(file != nullptr);

    int w = -1, h = -1;
    REQUIRE(ImageDecoder::GetDimensions(file, "image/jpeg", &w, &h) == true);
    CHECK(w == 4032);
    CHECK(h == 3024);
    g_object_unref(file);
    g_unlink(path.c_str());
    g_rmdir(dir.c_str());
}

TEST_CASE("JPEG probe rejects things that are not JPEGs or have no frame", "[unit][decoder][jpeg]")
{
    std::string dir = QuiverTest_GetImagesDir() + "/probe_bad";
    g_mkdir_with_parents(dir.c_str(), 0755);

    DYNAMIC_SECTION("empty file")
    {
        std::string path = dir + "/empty.jpg";
        write_bytes(path.c_str(), std::vector<uint8_t>());
        GFile *file = g_file_new_for_path(path.c_str());
        REQUIRE(file != nullptr);
        int w = -1, h = -1;
        CHECK(ImageDecoder::ProbeJpegDimensions(file, &w, &h) == false);
        g_object_unref(file);
    }

    DYNAMIC_SECTION("not a JPEG")
    {
        std::string path = dir + "/notjpeg.jpg";
        const uint8_t png_bytes[] = {'G', 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
        write_bytes(path.c_str(), std::vector<uint8_t>(png_bytes, png_bytes + sizeof(png_bytes)));
        GFile *file = g_file_new_for_path(path.c_str());
        REQUIRE(file != nullptr);
        int w = -1, h = -1;
        CHECK(ImageDecoder::ProbeJpegDimensions(file, &w, &h) == false);
        g_object_unref(file);
    }

    DYNAMIC_SECTION("truncated before any frame header")
    {
        std::string path = dir + "/trunc.jpg";
        /* XMP first so the frame header is well past the truncation point,
         * then cut inside the metadata. */
        std::vector<uint8_t> full = make_jpeg_with_xmp(200000, 64, 48, 0);
        REQUIRE(full.size() > 12);
        full.resize(12);
        write_bytes(path.c_str(), full);
        GFile *file = g_file_new_for_path(path.c_str());
        REQUIRE(file != nullptr);
        int w = -1, h = -1;
        CHECK(ImageDecoder::ProbeJpegDimensions(file, &w, &h) == false);
        g_object_unref(file);
    }

    DYNAMIC_SECTION("segment length of zero must not loop forever")
    {
        std::string path = dir + "/zerolen.jpg";
        /* SOI, then an APPn claiming a zero length: invalid, and a walker that
         * trusted it would either stall or run off the end. */
        std::vector<uint8_t> bad = {0xFF, 0xD8, 0xFF, 0xE1, 0x00, 0x00};
        write_bytes(path.c_str(), bad);
        GFile *file = g_file_new_for_path(path.c_str());
        REQUIRE(file != nullptr);
        int w = -1, h = -1;
        CHECK(ImageDecoder::ProbeJpegDimensions(file, &w, &h) == false);
        g_object_unref(file);
    }

    DYNAMIC_SECTION("null arguments")
    {
        int w = -1, h = -1;
        CHECK(ImageDecoder::ProbeJpegDimensions(NULL, &w, &h) == false);
        GFile *file = g_file_new_for_path(dir.c_str());
        REQUIRE(file != nullptr);
        CHECK(ImageDecoder::ProbeJpegDimensions(file, NULL, &h) == false);
        CHECK(ImageDecoder::ProbeJpegDimensions(file, &w, NULL) == false);
        g_object_unref(file);
    }

    DYNAMIC_SECTION("missing file")
    {
        GFile *file = g_file_new_for_path((dir + "/nope.jpg").c_str());
        REQUIRE(file != nullptr);
        int w = -1, h = -1;
        CHECK(ImageDecoder::ProbeJpegDimensions(file, &w, &h) == false);
        g_object_unref(file);
    }

    g_rmdir(dir.c_str());
}

TEST_CASE("JPEG probe handles progressive and restart-marker files", "[unit][decoder][jpeg]")
{
    std::string dir = QuiverTest_GetImagesDir() + "/probe_progressive";
    g_mkdir_with_parents(dir.c_str(), 0755);
    std::string path = dir + "/prog.jpg";

    /* SOI, XMP, SOF2 (progressive) behind a restart interval, then SOS.
     * SOF2 must be accepted and DRI/C4 must not be mistaken for a frame. */
    std::vector<uint8_t> out;
    const uint8_t hdr[] = {0xFF, 0xD8};
    out.insert(out.end(), hdr, hdr + 2);
    out.push_back(0xFF); out.push_back(0xE1);
    out.push_back(0x00); out.push_back(0x10);
    for (int i = 0; i < 14; ++i) out.push_back((uint8_t)'x');
    /* DRI with a restart interval */
    out.push_back(0xFF); out.push_back(0xDD);
    out.push_back(0x00); out.push_back(0x04);
    out.push_back(0x00); out.push_back(0x04);
    /* SOF2 */
    out.push_back(0xFF); out.push_back(0xC2);
    out.push_back(0x00); out.push_back(0x11);
    out.push_back(8);
    out.push_back((uint8_t)(2048 >> 8)); out.push_back((uint8_t)(2048 & 0xFF));
    out.push_back((uint8_t)(4096 >> 8)); out.push_back((uint8_t)(4096 & 0xFF));
    for (int i = 0; i < 10; ++i) out.push_back(0x11);
    /* SOS */
    out.push_back(0xFF); out.push_back(0xDA);
    out.push_back(0x00); out.push_back(0x0C);
    for (int i = 0; i < 12; ++i) out.push_back(0x00);
    out.push_back(0xFF); out.push_back(0xD9);

    write_bytes(path.c_str(), out);
    GFile *file = g_file_new_for_path(path.c_str());
    REQUIRE(file != nullptr);
    int w = -1, h = -1;
    REQUIRE(ImageDecoder::ProbeJpegDimensions(file, &w, &h) == true);
    CHECK(w == 4096);
    CHECK(h == 2048);
    g_object_unref(file);
    g_unlink(path.c_str());
    g_rmdir(dir.c_str());
}

TEST_CASE("Real-world JPEG fixtures with large XMP headers probe correctly", "[unit][decoder][jpeg][fixture]")
{
    /* A phone export can put megabytes of XMP ahead of the frame header.  The
     * fixed-prefix probe misses those and falls back to a full decode; the
     * streaming walk finds the header regardless.  Fixtures are supplied by
     * path, since the workspace ones are not part of the repo. */
    const char *paths[] = {
        "PXL_20241012_185450150.PORTRAIT.jpg",
        "\\run\\media\\mike\\T7\\.Trash-1000\\files\\2023-02-22%2014.58.17-01.jpg",
        "2014-01-16 10.23.21-01.JPG",
    };

    for (const char *rel : paths)
    {
        DYNAMIC_SECTION(rel)
        {
            std::string path = std::string(QUIVER_TEST_IMAGES_DIR_DEF) + "/../../" + rel;
            if (!g_file_test(path.c_str(), G_FILE_TEST_EXISTS))
            {
                path = rel;   /* also try relative to the working directory */
                if (!g_file_test(path.c_str(), G_FILE_TEST_EXISTS))
                {
                    WARN("fixture not present: " << rel);
                    continue;
                }
            }

            GFile *file = g_file_new_for_path(path.c_str());
            REQUIRE(file != nullptr);

            int w = -1, h = -1;
            REQUIRE(ImageDecoder::ProbeJpegDimensions(file, &w, &h) == true);
            REQUIRE(w > 0);
            REQUIRE(h > 0);

#if HAVE_GDK_PIXBUF
            /* The probe must agree with what an actual decode produces. */
            GError *err = NULL;
            GdkPixbuf *pb = gdk_pixbuf_new_from_file(path.c_str(), &err);
            if (pb)
            {
                CHECK(w == gdk_pixbuf_get_width(pb));
                CHECK(h == gdk_pixbuf_get_height(pb));
                g_object_unref(pb);
            }
            else
            {
                WARN("could not decode fixture for comparison");
                if (err) g_error_free(err);
            }
#endif
            g_object_unref(file);
        }
    }
}

/* Regression guard: the decode path used to call g_file_query_info() itself to
 * find the file size.  GetWidth() runs on the icon view's thumbnail worker
 * thread, and for a trash:// URI that query went into libgvfs, which crashed
 * serialising the mount spec - strlen() on a garbage pointer in
 * g_mount_spec_to_dbus_with_path(), reached from GetDimensions() on the worker
 * that loads a thumbnail.  It is now handed the size the listing already had.
 *
 * These check the two halves of that change: an oversized file is still
 * refused when the size is supplied, and an unknown size (-1, the default) no
 * longer means "query it".
 */
TEST_CASE("Oversized input is refused from a caller-supplied size", "[unit][decoder]")
{
    std::string dir = QuiverTest_GetImagesDir() + "/probe_size";
    g_mkdir_with_parents(dir.c_str(), 0755);
    std::string path = dir + "/s.jpg";

    std::vector<uint8_t> bytes = make_jpeg_with_xmp(2000, 640, 480, 0);
    write_bytes(path.c_str(), bytes);

    GFile *file = g_file_new_for_path(path.c_str());
    REQUIRE(file != nullptr);

    int w = -1, h = -1;
    /* Over the 500 MB cap: refused without reading the file. */
    CHECK(ImageDecoder::GetDimensions(file, "image/jpeg", &w, &h, 600LL * 1024 * 1024) == false);

    /* Under it: probed normally. */
    CHECK(ImageDecoder::GetDimensions(file, "image/jpeg", &w, &h, 1024) == true);
    CHECK(w == 640);
    CHECK(h == 480);

    /* Unknown size means "no opinion", not "query the file" - this is the case
     * that used to issue the g_file_query_info() and crash. */
    CHECK(ImageDecoder::GetDimensions(file, "image/jpeg", &w, &h, -1) == true);
    CHECK(w == 640);
    CHECK(h == 480);

    /* And the full decode entry points take the size the same way.  A real
     * fixture, since the synthetic JPEG above is only parseable by the probe. */
    std::string realPath = QuiverTest_GetImagesDir() + "/sample_4k.jpg";
    GFile *real = g_file_new_for_path(realPath.c_str());
    REQUIRE(real != nullptr);

    GError *err = NULL;
    GdkTexture *big = ImageDecoder::DecodeFileTexture(real, "image/jpeg", NULL, &err, 600LL * 1024 * 1024);
    CHECK(big == nullptr);
    if (big) g_object_unref(big);

    GdkTexture *ok = ImageDecoder::DecodeFileTexture(real, "image/jpeg", NULL, &err, -1);
    REQUIRE(ok != nullptr);
    g_object_unref(ok);

    g_object_unref(real);
    g_object_unref(file);
    g_unlink(path.c_str());
    g_rmdir(dir.c_str());
}

#if HAVE_GDK_PIXBUF
/* An asymmetric texture, so a rotation is visible in the pixels rather than
 * only in the dimensions - which is the whole point: orientations 1-4 keep the
 * frame size, so a dimension check cannot tell a turned texture from an
 * unturned one. */
static GdkTexture* QuiverTest_AsymmetricTexture(int w, int h)
{
    GBytes *bytes = g_bytes_new_take(g_malloc0((gsize)w * h * 4), (gsize)w * h * 4);
    guint32 *px = (guint32 *)g_bytes_get_data(bytes, NULL);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            px[y * w + x] = 0xff000000u | ((guint32)(x * 8) << 16) |
                            ((guint32)(y * 8) << 8) | (guint32)(x + y);
    GdkTexture *tex = gdk_memory_texture_new(w, h, GDK_MEMORY_R8G8B8A8, bytes,
                                             (gsize)w * 4);
    g_bytes_unref(bytes);
    return tex;
}

static bool QuiverTest_PixelsMatch(GdkTexture *a, GdkTexture *b)
{
    if (a == nullptr || b == nullptr) return false;
    if (gdk_texture_get_width(a) != gdk_texture_get_width(b) ||
        gdk_texture_get_height(a) != gdk_texture_get_height(b))
        return false;
    GBytes *ba = gdk_texture_save_to_png_bytes(a);
    GBytes *bb = gdk_texture_save_to_png_bytes(b);
    gsize sa = 0, sb = 0;
    const guchar *da = (const guchar *)g_bytes_get_data(ba, &sa);
    const guchar *db = (const guchar *)g_bytes_get_data(bb, &sb);
    bool same = (sa == sb) && (memcmp(da, db, sa) == 0);
    g_bytes_unref(ba);
    g_bytes_unref(bb);
    return same;
}

TEST_CASE("EnsureExifOrientation turns the pixels whatever the frame size says",
          "[unit][decoder][orientation]")
{
    const int w = 16, h = 8;

    /* The decoders hand back the pixels as stored, so this is the one place the
     * orientation is applied.  There is no dimension check here on purpose: a
     * 180 degree turn, and the two mirrors, leave the frame size untouched, so
     * a size comparison cannot tell a texture somebody has already turned from
     * one nobody has - which is how a plain upside-down photo slips through. */
    for (int orientation = 2; orientation <= 8; orientation++)
    {
        GdkTexture *raw = QuiverTest_AsymmetricTexture(w, h);
        GdkTexture *expected = QuiverUtils::TextureExifReorientate(raw, orientation);
        REQUIRE(expected != nullptr);

        GdkTexture *got = ImageDecoder::EnsureExifOrientation(raw, orientation);
        CHECK(QuiverTest_PixelsMatch(got, expected));
    }

    /* Orientation 1 is what unrotated already means, so it is left alone and
     * the caller keeps its texture rather than getting a copy. */
    SECTION("orientation 1 is returned untouched")
    {
        GdkTexture *raw = QuiverTest_AsymmetricTexture(w, h);
        GdkTexture *got = ImageDecoder::EnsureExifOrientation(raw, 1);
        CHECK(got == raw);
    }

    SECTION("a missing texture is not invented")
    {
        CHECK(ImageDecoder::EnsureExifOrientation(NULL, 6) == NULL);
    }
}
#endif

TEST_CASE("Decoders hand back un-oriented pixels so the loader owns orientation",
          "[unit][decoder][orientation]")
{
    /* The loader applies the EXIF orientation itself, which only works if the
     * decoders hand the pixels back exactly as they were stored.  A backend
     * that quietly turned them makes the loader turn them a second time, and
     * that is invisible for orientation 1 while it is a plainly wrong picture
     * for everything else - so it is pinned here rather than left to
     * inspection.  Both fixtures are rotated in the file; sample_rotated has a
     * spec-legal SHORT tag and sample_rotated_slong_ori an SLONG one that the
     * backends ignore anyway. */
    std::string imagesDir = QuiverTest_GetImagesDir();
    const char *names[] = {"/sample_rotated.jpg", "/sample_rotated_slong_ori.jpg"};

    std::vector<ImageDecoder::Backend> backends;
    backends.push_back(ImageDecoder::Backend::AUTO);
    if (ImageDecoder::IsBackendSupported(ImageDecoder::Backend::GLYCIN))
        backends.push_back(ImageDecoder::Backend::GLYCIN);
    if (ImageDecoder::IsBackendSupported(ImageDecoder::Backend::PIXBUF))
        backends.push_back(ImageDecoder::Backend::PIXBUF);

    ImageDecoder::Backend originalBackend = ImageDecoder::GetBackend();

    for (const char *name : names)
    {
        std::string path = imagesDir + name;
        GFile *file = g_file_new_for_path(path.c_str());
        REQUIRE(file != nullptr);

        /* The size on disk: what the pixels must still look like here. */
        int stored_width = 0, stored_height = 0;
        REQUIRE(ImageDecoder::GetDimensions(file, "image/jpeg", &stored_width,
                                            &stored_height));
        REQUIRE(stored_width > 0);

        for (auto backend : backends)
        {
            ImageDecoder::SetBackend(backend);
            GError *err = NULL;
            GdkTexture *tex = ImageDecoder::DecodeFileTexture(file, "image/jpeg",
                                                              NULL, &err, -1);
            CAPTURE(std::string(name) + " backend " + std::to_string((int)backend));
            CHECK(tex != nullptr);
            if (tex)
            {
                CHECK(gdk_texture_get_width(tex) == stored_width);
                CHECK(gdk_texture_get_height(tex) == stored_height);
                g_object_unref(tex);
            }
            if (err) g_error_free(err);
        }

        g_object_unref(file);
    }

    ImageDecoder::SetBackend(originalBackend);
}

TEST_CASE("A cached texture records where its pixels are, not what was asked for",
          "[unit][decoder][orientation][cache]")
{
    /* Reproduces the sequence the loader runs for a background cache preload
     * and for a direct open, both of which ask for orientation 1 while the
     * pixels sit at the file's own orientation.  Recording the request instead
     * is what made a file open correctly and then appear upside down or 90
     * degrees out the moment it was reached any other way: the next load read
     * the stamp, believed the pixels were unrotated, and turned them again. */
    const int w = 16, h = 8;
    const int file_orientation = 6;

    GdkTexture *raw = QuiverTest_AsymmetricTexture(w, h);
    GdkTexture *at_file = ImageDecoder::EnsureExifOrientation(raw, file_orientation);
    REQUIRE(at_file != nullptr);

    SECTION("a preload that asks for nothing records the file's own orientation")
    {
        const int requested = 1;          /* what a background preload asks for */
        int pixels_at = file_orientation; /* where the pixels actually are */

        int turn = 1;
        pixels_at = QuiverUtils::ExifOrientationApplied(pixels_at, requested, &turn);

        /* Nothing was asked to move, so the pixels are still where the decode
         * left them and the stamp has to say so. */
        CHECK(turn == 1);
        CHECK(pixels_at == file_orientation);
        /* And the stamp therefore matches the pixels, so the next load is a
         * no-op instead of a second turn. */
        CHECK(QuiverUtils::ExifOrientationTurn(pixels_at, file_orientation) == 1);
    }

    SECTION("asking for the orientation already shown is not a turn")
    {
        /* The file that started this: stored 180 degrees out (orientation 3).
         * Opened at its own orientation it needs no turn at all, and the stamp
         * has to come back as 3.  Stamping the turn - which is 1, because
         * nothing had to move - made the next cache hit believe the pixels
         * were upright and rotate them by 3, putting the picture back the way
         * it was stored. */
        int turn = 99;
        int pixels_at = QuiverUtils::ExifOrientationApplied(3, 3, &turn);
        CHECK(turn == 1);
        CHECK(pixels_at == 3);
        /* Reaching it again from the cache at the same orientation is a no-op. */
        int hit_turn = 99;
        int hit_at = QuiverUtils::ExifOrientationApplied(pixels_at, 3, &hit_turn);
        CHECK(hit_turn == 1);
        CHECK(hit_at == 3);
    }

    SECTION("the stamp is where the pixels land, for every orientation")
    {
        /* Whatever the file holds and whatever is asked for, applying the turn
         * leaves the pixels at what was asked for, so a later read of the stamp
         * followed by the same request never moves them. */
        for (int file_ori = 1; file_ori <= 8; file_ori++)
        {
            for (int requested = 1; requested <= 8; requested++)
            {
                int turn = 99;
                const int landed = QuiverUtils::ExifOrientationApplied(file_ori, requested, &turn);
                /* A turn moves the pixels to what was asked for; no turn leaves
                 * them where the decode put them. */
                CHECK(landed == (turn > 1 ? requested : file_ori));

                /* Reading the stamp back and asking for the same orientation
				 * again must not move them a second time. */
                int again_turn = 99;
                const int again = QuiverUtils::ExifOrientationApplied(landed, requested, &again_turn);
                CHECK(again_turn == 1);
                CHECK(again == landed);
            }
        }
    }

    SECTION("a cache hit for the orientation already shown turns nothing")
    {
        for (int orientation = 1; orientation <= 8; orientation++)
        {
            CHECK(QuiverUtils::ExifOrientationTurn(orientation, orientation) == 1);
        }
    }

    SECTION("turning from where the pixels are lands on what was asked for")
    {
        /* Whatever the cache was stamped with, one turn has to reach the
         * requested orientation and no more. */
        for (int cached = 1; cached <= 8; cached++)
        {
            GdkTexture *start = QuiverUtils::TextureExifReorientate(
                QuiverTest_AsymmetricTexture(w, h), cached);
            REQUIRE(start != nullptr);
            for (int wanted = 1; wanted <= 8; wanted++)
            {
                GdkTexture *expect = QuiverUtils::TextureExifReorientate(
                    QuiverTest_AsymmetricTexture(w, h), wanted);
                REQUIRE(expect != nullptr);

                int turn = QuiverUtils::ExifOrientationTurn(cached, wanted);
                /* Hand it its own reference: it takes ownership, and the loop
                 * goes round @cached again afterwards. */
                GdkTexture *got = ImageDecoder::EnsureExifOrientation(g_object_ref(start),
                                                                      turn);
                CHECK(QuiverTest_PixelsMatch(got, expect));
                g_object_unref(got);
                g_object_unref(expect);
            }
            g_object_unref(start);
        }
    }

    SECTION("a nonsense stamp cannot index off the end of the table")
    {
        CHECK(QuiverUtils::ExifOrientationTurn(0, 6) ==
              QuiverUtils::ExifOrientationTurn(1, 6));
        CHECK(QuiverUtils::ExifOrientationTurn(6, 99) ==
              QuiverUtils::ExifOrientationTurn(6, 1));
        CHECK(QuiverUtils::ExifOrientationTurn(-3, -3) == 1);
    }
}
