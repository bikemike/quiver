#include <catch2/catch_test_macros.hpp>
#include "ImageSaveManager.h"
#include "QuiverFile.h"
#include "test_helpers.h"
#include <exiv2/exiv2.hpp>
#include <gdk/gdk.h>
#if HAVE_GDK_PIXBUF
#include <gdk-pixbuf/gdk-pixbuf.h>
#endif
#include <glib.h>
#include <glib/gstdio.h>
#include <unistd.h>
#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <vector>

extern "C" {
#include <jpeglib.h>
}

namespace {

/* Byte order plus every tag's datatype and value, so a save can be shown to
 * have touched nothing but the tag it was asked to change. */
struct ExifFingerprint {
    std::string byteOrder;
    std::map<std::string, std::pair<int, std::string>> tags;

    const std::pair<int, std::string>* find(const char* pKey) const
    {
        std::map<std::string, std::pair<int, std::string>>::const_iterator it = tags.find(pKey);
        return (it == tags.end()) ? NULL : &it->second;
    }
};

ExifFingerprint fingerprint_exif(const gchar* pPath)
{
    Exiv2::LogMsg::setLevel(Exiv2::LogMsg::mute);

    ExifFingerprint out;
    auto image = Exiv2::ImageFactory::open(pPath);
    image->readMetadata();

    out.byteOrder = (Exiv2::bigEndian == image->byteOrder()) ? "MM" : "II";
    for (Exiv2::ExifData::const_iterator it = image->exifData().begin();
         it != image->exifData().end(); ++it) {
        out.tags[it->key()] =
            std::make_pair(static_cast<int>(it->typeId()), it->value().toString());
    }
    return out;
}

/* Raw RGB straight out of libjpeg, which - unlike gdk-pixbuf - never applies the
 * Exif orientation tag.  Comparing this shows a save left the picture alone
 * while the orientation value itself changed. */
std::vector<guint8> decoded_rgb(const gchar* pPath)
{
    std::vector<guint8> out;

    FILE* fp = fopen(pPath, "rb");
    if (NULL == fp)
        return out;

    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);
    jpeg_stdio_src(&cinfo, fp);

    if (JPEG_HEADER_OK == jpeg_read_header(&cinfo, TRUE))
    {
        cinfo.out_color_space = JCS_RGB;
        jpeg_start_decompress(&cinfo);

        out.resize(static_cast<size_t>(cinfo.output_width) * cinfo.output_height *
                   cinfo.output_components);
        std::vector<guint8> row(static_cast<size_t>(cinfo.output_width) * cinfo.output_components);

        guint8* dest = out.data();
        while (cinfo.output_scanline < cinfo.output_height) {
            JSAMPROW rows[1] = { row.data() };
            if (1 != jpeg_read_scanlines(&cinfo, rows, 1))
                break;
            std::memcpy(dest, row.data(), row.size());
            dest += row.size();
        }
    }

    jpeg_destroy_decompress(&cinfo);
    fclose(fp);
    return out;
}

/* Replace the Exif APP1 of @pPath with @pExif re-encoded big-endian, which is
 * what the old save did to every file.  Used to manufacture a big-endian
 * original, since Exiv2's own writer preserves the endianness it finds and so
 * cannot be asked to produce one from a little-endian source. */
void rewrite_exif_big_endian(const gchar* pPath, Exiv2::ExifData& exif)
{
    gchar* contents = NULL;
    gsize length = 0;
    if (!g_file_get_contents(pPath, &contents, &length, NULL))
        return;

    const std::string in(contents, length);
    g_free(contents);

    Exiv2::Blob blob;
    try {
        Exiv2::ExifParser::encode(blob, Exiv2::bigEndian, exif);
    } catch (...) {
        return;
    }

    std::vector<guchar> payload;
    const unsigned char header[] = { 'E', 'x', 'i', 'f', '\0', '\0' };
    payload.insert(payload.end(), header, header + 6);
    payload.insert(payload.end(), blob.begin(), blob.end());

    const std::string::size_type app1 = in.find("\xff\xe1", 2);
    if (std::string::npos == app1)
        return;

    const unsigned oldLen =
        (static_cast<unsigned char>(in[app1 + 2]) << 8) | static_cast<unsigned char>(in[app1 + 3]);

    std::string out = in.substr(0, app1);
    const unsigned newLen = static_cast<unsigned>(payload.size()) + 2;
    out.push_back(static_cast<char>(0xff));
    out.push_back(static_cast<char>(0xe1));
    out.push_back(static_cast<char>((newLen >> 8) & 0xff));
    out.push_back(static_cast<char>(newLen & 0xff));
    out.append(reinterpret_cast<const char*>(payload.data()), payload.size());
    out.append(in.substr(app1 + 2 + oldLen));

    g_file_set_contents(pPath, out.c_str(), out.size(), NULL);
}

/* Put an opaque blob of nBytes into pKey, standing in for the vendor data
 * cameras keep in the Exif IFD.  Nothing here understands its contents, which
 * is the point: a save must hand back whatever bytes it was given. */
void inject_blob_tag(const gchar* pPath, const char* pKey, size_t nBytes)
{
    Exiv2::LogMsg::setLevel(Exiv2::LogMsg::mute);

    auto image = Exiv2::ImageFactory::open(pPath);
    image->readMetadata();

    std::vector<unsigned char> bytes(nBytes);
    for (size_t i = 0; i < nBytes; ++i)
        bytes[i] = static_cast<unsigned char>((i * 7 + 13) & 0xff);

    /* operator[] hands back an "undefined" placeholder, so the type has to be
     * asked for explicitly rather than read back off the new entry */
    Exiv2::Exifdatum& datum = image->exifData()[pKey];
    Exiv2::Value::UniquePtr value = Exiv2::Value::create(Exiv2::undefined);
    value->read(&bytes[0], nBytes, Exiv2::invalidByteOrder);
    datum.setValue(value.get());

    image->writeMetadata();
}

/* Differing bytes, so a failure reports a number instead of dumping a whole
 * picture into the log. */
size_t byte_differences(const std::vector<guint8>& a, const std::vector<guint8>& b)
{
    if (a.size() != b.size())
        return std::max(a.size(), b.size());

    size_t n = 0;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i] != b[i])
            ++n;
    return n;
}

} // namespace

TEST_CASE("libjpeg and ImageSaveManager Integration", "[lib][libjpeg]")
{
    ImageSaveManager::Reset();
    ImageSaveManagerPtr sm = ImageSaveManager::GetInstance();
    REQUIRE(sm != nullptr);

    SECTION("JPEG format is registered and supported")
    {
        REQUIRE(sm->IsFormatSupported("image/jpeg"));
    }

    SECTION("Saving texture as JPEG in-place via ImageSaveManager")
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

        // Create 120x80 test texture (RGBA)
        std::vector<guint8> pixels(120 * 80 * 4, 0);
        for (size_t i = 0; i < 120 * 80; ++i) {
            pixels[i * 4 + 1] = 255; // Green
            pixels[i * 4 + 3] = 255; // Alpha
        }
        GBytes* bytes = g_bytes_new(pixels.data(), pixels.size());
        GdkTexture* tex = gdk_memory_texture_new(120, 80, GDK_MEMORY_R8G8B8A8, bytes, 120 * 4);
        g_bytes_unref(bytes);
        REQUIRE(tex != nullptr);

        bool saved = sm->SaveImage(qf, tex, nullptr, nullptr);
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
        g_object_unref(tex);
        g_free(tmpUri);
    }

#if HAVE_GDK_PIXBUF
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
#endif

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

TEST_CASE("Rotate-and-save leaves the rest of the EXIF alone", "[lib][libjpeg][exif]")
{
    ImageSaveManager::Reset();
    ImageSaveManagerPtr sm = ImageSaveManager::GetInstance();
    REQUIRE(sm != nullptr);

    struct Case {
        const char* fixture;
        int stored;
        int rotatedTo;
    };
    /* sample_rotated.jpg is a well-formed little-endian SHORT; the other is
     * the SLONG an older save left behind, which must be repaired rather than
     * propagated. */
    const Case cases[] = {
        { "sample_rotated.jpg", 6, 8 },
        { "sample_rotated_slong_ori.jpg", 8, 3 },
    };

    for (const Case& c : cases) {
        DYNAMIC_SECTION(c.fixture << ": Orientation " << c.stored << " -> " << c.rotatedTo)
        {
            const std::string source = QuiverTest_GetImagesDir() + "/" + c.fixture;

            char tmpCopy[] = "/tmp/quiver_exif_save_XXXXXX.jpg";
            const int fd = g_mkstemp(tmpCopy);
            REQUIRE(fd >= 0);
            close(fd);

            gchar* contents = NULL;
            gsize length = 0;
            REQUIRE(g_file_get_contents(source.c_str(), &contents, &length, NULL));
            REQUIRE(g_file_set_contents(tmpCopy, contents, length, NULL));
            g_free(contents);

            /* the temp file the save writes is 0600, so anything other than
             * carrying the mode across flattens the original's permissions */
            REQUIRE(0 == g_chmod(tmpCopy, 0644));

            const ExifFingerprint before = fingerprint_exif(tmpCopy);
            REQUIRE("II" == before.byteOrder);
            REQUIRE(before.find("Exif.Image.Orientation") != NULL);
            const std::vector<guint8> rgbBefore = decoded_rgb(tmpCopy);
            REQUIRE(!rgbBefore.empty());

            gchar* tmpUri = g_filename_to_uri(tmpCopy, NULL, NULL);
            REQUIRE(tmpUri != NULL);

            QuiverFile qf(tmpUri);

            /* the path a viewer rotation takes, so the SHORT/SLong
             * regression is covered by something real */
            REQUIRE(qf.GetExifData() != NULL);
            REQUIRE(qf.SetOrientation(c.rotatedTo));
            REQUIRE(c.rotatedTo == qf.GetOrientation());

            /* conformant in memory too, not merely once the save pass has
             * pinned it - this is the assignment that used to store type 9 */
            {
                const std::shared_ptr<const Exiv2::ExifData> staged = qf.GetExifDataShared();
                REQUIRE(staged != nullptr);
                const Exiv2::ExifData::const_iterator it =
                    staged->findKey(Exiv2::ExifKey("Exif.Image.Orientation"));
                REQUIRE(it != staged->end());
                CHECK(Exiv2::unsignedShort == it->typeId());
                CHECK(std::to_string(c.rotatedTo) == it->value().toString());
            }

            /* no texture: the coefficient-copy path, i.e. what a rotate-and-save
             * actually does - the pixels are never re-encoded */
            REQUIRE(sm->SaveImage(qf, static_cast<GdkTexture*>(NULL), NULL, NULL));

            const ExifFingerprint after = fingerprint_exif(tmpCopy);

            /* the point of the fix: conformant SHORT, and the file's own
             * endianness instead of a forced big-endian rewrite */
            const std::pair<int, std::string>* orientation =
                after.find("Exif.Image.Orientation");
            REQUIRE(orientation != NULL);
            CHECK(static_cast<Exiv2::TypeId>(orientation->first) == Exiv2::unsignedShort);
            CHECK(std::to_string(c.rotatedTo) == orientation->second);
            CHECK(before.byteOrder == after.byteOrder);

            /* nothing else moved: same keys, same datatypes, same values */
            CHECK(before.tags.size() == after.tags.size());
            for (std::map<std::string, std::pair<int, std::string>>::const_iterator it =
                     before.tags.begin();
                 it != before.tags.end(); ++it) {
                if ("Exif.Image.Orientation" == it->first)
                    continue;
                INFO("tag " << it->first);
                const std::pair<int, std::string>* now = after.find(it->first.c_str());
                REQUIRE(now != NULL);
                CHECK(it->second == *now);
            }

            /* still the same picture, and the mode it was saved with */
            const std::vector<guint8> rgbAfter = decoded_rgb(tmpCopy);
            CHECK(0 == byte_differences(rgbBefore, rgbAfter));

            GStatBuf st;
            REQUIRE(0 == g_stat(tmpCopy, &st));
            CHECK(0644 == (st.st_mode & 07777));

            g_unlink(tmpCopy);
            g_free(tmpUri);
        }
    }

    /* The camera files that set this off are big-endian, and MM is legal TIFF,
     * so a save has no business flattening them either. */
    DYNAMIC_SECTION("a big-endian original keeps its endianness")
    {
        const std::string source = QuiverTest_GetImagesDir() + "/sample_rotated.jpg";

        char tmpCopy[] = "/tmp/quiver_exif_mm_XXXXXX.jpg";
        const int fd = g_mkstemp(tmpCopy);
        REQUIRE(fd >= 0);
        close(fd);

        gchar* contents = NULL;
        gsize length = 0;
        REQUIRE(g_file_get_contents(source.c_str(), &contents, &length, NULL));
        REQUIRE(g_file_set_contents(tmpCopy, contents, length, NULL));
        g_free(contents);

        gchar* tmpUri = g_filename_to_uri(tmpCopy, NULL, NULL);
        REQUIRE(tmpUri != NULL);

        QuiverFile qf(tmpUri);

        std::shared_ptr<Exiv2::ExifData> exif = qf.GetExifData();
        REQUIRE(exif != NULL);
        rewrite_exif_big_endian(tmpCopy, *exif);

        const ExifFingerprint before = fingerprint_exif(tmpCopy);
        REQUIRE("MM" == before.byteOrder);
        const std::vector<guint8> rgbBefore = decoded_rgb(tmpCopy);
        REQUIRE(!rgbBefore.empty());

        REQUIRE(qf.SetOrientation(1));
        REQUIRE(sm->SaveImage(qf, static_cast<GdkTexture*>(NULL), NULL, NULL));

        const ExifFingerprint after = fingerprint_exif(tmpCopy);
        CHECK("MM" == after.byteOrder);

        const std::pair<int, std::string>* orientation = after.find("Exif.Image.Orientation");
        REQUIRE(orientation != NULL);
        CHECK(Exiv2::unsignedShort == static_cast<Exiv2::TypeId>(orientation->first));
        CHECK("1" == orientation->second);

        CHECK(before.tags.size() == after.tags.size());
        const std::vector<guint8> rgbAfter = decoded_rgb(tmpCopy);
        CHECK(0 == byte_differences(rgbBefore, rgbAfter));

        g_unlink(tmpCopy);
        g_free(tmpUri);
    }

    /* Cameras keep a large opaque vendor blob in the Exif IFD and plenty of
     * software keyed off it, so a save has to hand it back byte for byte. */
    DYNAMIC_SECTION("an opaque vendor blob in the Exif IFD survives a save")
    {
        const std::string source = QuiverTest_GetImagesDir() + "/sample_rotated.jpg";

        char tmpCopy[] = "/tmp/quiver_exif_blob_XXXXXX.jpg";
        const int fd = g_mkstemp(tmpCopy);
        REQUIRE(fd >= 0);
        close(fd);

        gchar* contents = NULL;
        gsize length = 0;
        REQUIRE(g_file_get_contents(source.c_str(), &contents, &length, NULL));
        REQUIRE(g_file_set_contents(tmpCopy, contents, length, NULL));
        g_free(contents);

        const size_t kBlobBytes = 4096;
        inject_blob_tag(tmpCopy, "Exif.Photo.MakerNote", kBlobBytes);

        const ExifFingerprint before = fingerprint_exif(tmpCopy);
        const std::pair<int, std::string>* blobBefore =
            before.find("Exif.Photo.MakerNote");
        REQUIRE(blobBefore != NULL);
        const std::vector<guint8> rgbBefore = decoded_rgb(tmpCopy);
        REQUIRE(!rgbBefore.empty());

        gchar* tmpUri = g_filename_to_uri(tmpCopy, NULL, NULL);
        REQUIRE(tmpUri != NULL);

        QuiverFile qf(tmpUri);

        REQUIRE(qf.SetOrientation(1));
        REQUIRE(sm->SaveImage(qf, static_cast<GdkTexture*>(NULL), NULL, NULL));

        const ExifFingerprint after = fingerprint_exif(tmpCopy);

        /* still there, still opaque bytes, still exactly the same bytes */
        const std::pair<int, std::string>* blobAfter = after.find("Exif.Photo.MakerNote");
        REQUIRE(blobAfter != NULL);
        CHECK(blobBefore->first == blobAfter->first);
        CHECK(blobBefore->second == blobAfter->second);

        CHECK(before.tags.size() == after.tags.size());
        for (std::map<std::string, std::pair<int, std::string>>::const_iterator it =
                 before.tags.begin();
             it != before.tags.end(); ++it) {
            if ("Exif.Image.Orientation" == it->first)
                continue;
            INFO("tag " << it->first);
            const std::pair<int, std::string>* now = after.find(it->first.c_str());
            REQUIRE(now != NULL);
            CHECK(it->second == *now);
        }

        const std::vector<guint8> rgbAfter = decoded_rgb(tmpCopy);
        CHECK(0 == byte_differences(rgbBefore, rgbAfter));

        g_unlink(tmpCopy);
        g_free(tmpUri);
    }

    ImageSaveManager::Reset();
}
