#include <catch2/catch_test_macros.hpp>
#include <exiv2/exiv2.hpp>
#include "QuiverFile.h"
#include "QuiverExifIo.h"
#include "test_helpers.h"
#include <glib.h>
#include <glib/gstdio.h>
#include <string>
#include <unistd.h>
#include <fcntl.h>

#if EXIV2_TEST_VERSION(0, 28, 0)
inline size_t databuf_size(const Exiv2::DataBuf& b) { return b.size(); }
inline const uint8_t* databuf_data(const Exiv2::DataBuf& b) { return b.c_data(); }
#else
inline size_t databuf_size(const Exiv2::DataBuf& b) { return static_cast<size_t>(b.size_); }
inline const uint8_t* databuf_data(const Exiv2::DataBuf& b) { return b.pData_; }
#endif

TEST_CASE("Exiv2 Library Integration and EXIF Operations", "[lib][exiv2]")
{
    std::string imagesDir = QuiverTest_GetImagesDir();
    std::string sampleJpg = imagesDir + "/sample_4k.jpg";

    SECTION("Exiv2 directly opens and parses sample JPEG")
    {
        REQUIRE(g_file_test(sampleJpg.c_str(), G_FILE_TEST_EXISTS));

        auto image = Exiv2::ImageFactory::open(sampleJpg);
        REQUIRE(image.get() != nullptr);

        image->readMetadata();

        // Sample JPEG has standard image properties
        REQUIRE(image->pixelWidth() > 0);
        REQUIRE(image->pixelHeight() > 0);
        REQUIRE(image->mimeType() == "image/jpeg");
    }

    SECTION("QuiverFile handles images without EXIF gracefully")
    {
        gchar* uri = g_filename_to_uri(sampleJpg.c_str(), NULL, NULL);
        REQUIRE(uri != NULL);

        QuiverFile qf(uri);
        // sample_4k.jpg has no initial EXIF data; QuiverFile returns nullptr without crashing
        auto exifData = qf.GetExifData();
        REQUIRE(exifData == nullptr);

        g_free(uri);
    }

    SECTION("Exiv2 Tag Modification, Serialization, and QuiverFile Readback")
    {
        // Copy sample image to temp file for read/write verification
        char tmpPath[] = "/tmp/quiver_exif_test_XXXXXX.jpg";
        int fd = g_mkstemp(tmpPath);
        REQUIRE(fd >= 0);
        close(fd);

        // Copy bytes from sample_4k.jpg
        char* contents = nullptr;
        gsize length = 0;
        REQUIRE(g_file_get_contents(sampleJpg.c_str(), &contents, &length, NULL));
        REQUIRE(g_file_set_contents(tmpPath, contents, length, NULL));
        g_free(contents);

        // Inject EXIF tags using Exiv2
        {
            auto image = Exiv2::ImageFactory::open(tmpPath);
            REQUIRE(image.get() != nullptr);
            image->readMetadata();

            Exiv2::ExifData& exifData = image->exifData();
            exifData["Exif.Image.Software"] = "Quiver Catch2 Test Suite";
            exifData["Exif.Image.Artist"] = "DeepMind Pair";
            image->writeMetadata();
        }

        // Re-read file with Exiv2 and assert tags were persisted
        {
            auto image = Exiv2::ImageFactory::open(tmpPath);
            REQUIRE(image.get() != nullptr);
            image->readMetadata();

            Exiv2::ExifData& exifData = image->exifData();
            REQUIRE_FALSE(exifData.empty());
            REQUIRE(exifData["Exif.Image.Software"].toString() == "Quiver Catch2 Test Suite");
            REQUIRE(exifData["Exif.Image.Artist"].toString() == "DeepMind Pair");
        }

        // Verify QuiverFile now detects and loads the populated EXIF data
        {
            gchar* tmpUri = g_filename_to_uri(tmpPath, NULL, NULL);
            REQUIRE(tmpUri != NULL);

            QuiverFile qf(tmpUri);
            auto loadedExif = qf.GetExifData();
            REQUIRE(loadedExif != nullptr);
            REQUIRE((*loadedExif)["Exif.Image.Software"].toString() == "Quiver Catch2 Test Suite");
            REQUIRE((*loadedExif)["Exif.Image.Artist"].toString() == "DeepMind Pair");

            g_free(tmpUri);
        }

        g_unlink(tmpPath);
    }
}

/* GioBasicIo replaced copying a file to a staging directory so Exiv2, which
 * only opens paths, could read a trash:/// entry.  These check the custom
 * BasicIo parses real metadata identically to the path-based route - the
 * read/seek/mmap/size surface Exiv2 leans on is what is easy to get wrong. */
TEST_CASE("GioBasicIo reads EXIF without a local path", "[lib][exiv2]")
{
    const std::string imagesDir = QuiverTest_GetImagesDir();
    const std::string sampleJpg = imagesDir + "/sample_4k.jpg";
    REQUIRE(g_file_test(sampleJpg.c_str(), G_FILE_TEST_EXISTS));

    /* scratch copy to add a tag to; both paths are local file:// so this
     * g_file_copy() never touches gvfs' D-Bus copy interface */
    g_autofree gchar* stagedPath = g_build_filename(g_get_tmp_dir(), "quiver-exif-io-XXXXXX", NULL);
    const int fd = g_mkstemp(stagedPath);
    REQUIRE(fd >= 0);
    g_close(fd, NULL);

    GFile* srcFile = g_file_new_for_path(sampleJpg.c_str());
    GFile* dstFile = g_file_new_for_path(stagedPath);
    REQUIRE(g_file_copy(srcFile, dstFile, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, NULL));
    g_object_unref(srcFile);
    g_object_unref(dstFile);

    {
        auto image = Exiv2::ImageFactory::open(std::string(stagedPath));
        REQUIRE(image.get() != nullptr);
        image->exifData()["Exif.Image.Model"] = "QuiverTestCam";
        image->writeMetadata();
    }

    auto pathImage = Exiv2::ImageFactory::open(std::string(stagedPath));
    REQUIRE(pathImage.get() != nullptr);
    pathImage->readMetadata();
    const std::string viaPath = pathImage->exifData()["Exif.Image.Model"].toString();
    REQUIRE(viaPath == "QuiverTestCam");

    SECTION("same metadata as the path-based open")
    {
        GFile* file = g_file_new_for_path(stagedPath);
        auto io = GioBasicIo::open(file, stagedPath);
        g_object_unref(file);
        REQUIRE(io.get() != nullptr);
#if EXIV2_TEST_VERSION(0, 28, 0)
        auto streamImage = Exiv2::ImageFactory::open(std::move(io));
#else
        auto streamImage = Exiv2::ImageFactory::open(io);
#endif
        REQUIRE(streamImage.get() != nullptr);
        streamImage->readMetadata();

        CHECK(streamImage->mimeType() == pathImage->mimeType());
        CHECK(streamImage->exifData().count() == pathImage->exifData().count());
        CHECK(streamImage->exifData()["Exif.Image.Model"].toString() == viaPath);
        CHECK(streamImage->pixelWidth() == pathImage->pixelWidth());
        CHECK(streamImage->pixelHeight() == pathImage->pixelHeight());
    }

    SECTION("exposes the whole file, and seeks anywhere in it")
    {
        GFile* file = g_file_new_for_path(stagedPath);
        auto io = GioBasicIo::open(file, stagedPath);
        REQUIRE(io.get() != nullptr);
        GFileInfo* info = g_file_query_info(file, G_FILE_ATTRIBUTE_STANDARD_SIZE, G_FILE_QUERY_INFO_NONE, NULL, NULL);
        g_object_unref(file);
        REQUIRE(info != nullptr);
        const goffset onDisk = g_file_info_get_size(info);
        g_object_unref(info);
        CHECK(io->size() == static_cast<size_t>(onDisk));
        CHECK(io->tell() == 0);
        CHECK(io->seek(0, Exiv2::BasicIo::end) == 0);
        CHECK(io->tell() == static_cast<size_t>(onDisk));
        CHECK(io->seek(0, Exiv2::BasicIo::beg) == 0);
        CHECK(io->tell() == 0);
        /* Exiv2's parsers seek backwards constantly, so read then rewind */
        Exiv2::DataBuf head = io->read(16);
        CHECK(databuf_size(head) == 16);
        CHECK(io->tell() == 16);
        CHECK(io->seek(0, Exiv2::BasicIo::beg) == 0);
        Exiv2::DataBuf again = io->read(16);
        CHECK(std::string(reinterpret_cast<const char*>(databuf_data(head)), databuf_size(head)) ==
              std::string(reinterpret_cast<const char*>(databuf_data(again)), databuf_size(again)));
    }

    SECTION("reports a missing file instead of staging one")
    {
        const std::string missing = imagesDir + "/definitely-not-here.jpg";
        GFile* missingFile = g_file_new_for_path(missing.c_str());
        auto io = GioBasicIo::open(missingFile, missing);
        g_object_unref(missingFile);
        CHECK(io.get() == nullptr);
    }

    g_remove(stagedPath);
}

/* Seek emulation is the reason this io works on files that do not fit in
 * memory.  These drive it the way Exiv2's parsers do - forward scans, then
 * jumps back to the start of a segment - plus a large sparse file to show the
 * window stays bounded no matter how big the file is. */
TEST_CASE("GioBasicIo emulates seeks without buffering the file", "[lib][exiv2]")
{
    /* 64 MB sparse file: allocation is instant, contents are known by position */
    g_autofree gchar* bigPath = g_build_filename(g_get_tmp_dir(), "quiver-exif-seek-XXXXXX", NULL);
    const int fd = g_mkstemp(bigPath);
    REQUIRE(fd >= 0);
    const goffset kBig = 64 * 1024 * 1024;
    const guchar marker = 0xA7;
    REQUIRE(ftruncate(fd, kBig) == 0);
    REQUIRE(pwrite(fd, &marker, 1, kBig - 1) == 1);
    g_close(fd, NULL);

    GFile* bigFile = g_file_new_for_path(bigPath);
    auto owned = GioBasicIo::open(bigFile, bigPath);
    REQUIRE(owned.get() != nullptr);
    /* Exiv2 sees it as a plain BasicIo; the window size is ours to inspect. */
#if EXIV2_TEST_VERSION(0, 28, 0)
    Exiv2::BasicIo::UniquePtr ioBase = std::move(owned);
#else
    Exiv2::BasicIo::AutoPtr ioBase = owned;
#endif
    GioBasicIo* io = static_cast<GioBasicIo*>(ioBase.get());

    SECTION("reports the full size without holding it")
    {
        CHECK(io->size() == static_cast<size_t>(kBig));
        CHECK(io->buffered_bytes() < 1024 * 1024);
    }

    SECTION("seek from end, then back to start")
    {
        REQUIRE(io->seek(0, Exiv2::BasicIo::end) == 0);
        CHECK(io->tell() == static_cast<size_t>(kBig));
        REQUIRE(io->seek(0, Exiv2::BasicIo::beg) == 0);
        CHECK(io->tell() == 0);
    }

    SECTION("reads the last byte from a 64 MB offset")
    {
        REQUIRE(io->seek(-1, Exiv2::BasicIo::end) == 0);
        CHECK(io->getb() == static_cast<int>(marker));
        CHECK(io->eof());
    }

    SECTION("a backwards seek past the window still lands correctly")
    {
        /* consume the last byte, so we sit at EOF... */
        REQUIRE(io->seek(-1, Exiv2::BasicIo::end) == 0);
        CHECK(io->getb() == static_cast<int>(marker));
        REQUIRE(io->tell() == static_cast<size_t>(kBig));

        /* ...then jump back to the start, which the window cannot help with,
         * and read the last byte again */
        REQUIRE(io->seek(-static_cast<int64_t>(kBig), Exiv2::BasicIo::cur) == 0);
        CHECK(io->tell() == 0);
        REQUIRE(io->seek(-1, Exiv2::BasicIo::end) == 0);
        CHECK(io->getb() == static_cast<int>(marker));
    }

    SECTION("memory stays bounded across repeated wide jumps")
    {
        /* A parse that walks the whole file in big strides.  With whole-file
         * buffering this is where memory would run away. */
        for (goffset off = 0; off < kBig; off += 8 * 1024 * 1024)
        {
            REQUIRE(io->seek(off, Exiv2::BasicIo::beg) == 0);
            Exiv2::DataBuf chunk = io->read(4096);
            CHECK(databuf_size(chunk) == 4096);
            /* jumps back to the start, exactly like re-reading a header */
            REQUIRE(io->seek(0, Exiv2::BasicIo::beg) == 0);
        }
        CHECK(io->buffered_bytes() <= 256 * 1024);
    }

    SECTION("a read larger than the window streams instead of allocating")
    {
        REQUIRE(io->seek(0, Exiv2::BasicIo::beg) == 0);
        /* 4 MB read through a 256 KB window: the excess must not be buffered */
        Exiv2::DataBuf big = io->read(4 * 1024 * 1024);
        CHECK(databuf_size(big) == 4 * 1024 * 1024);
        CHECK(io->buffered_bytes() <= 256 * 1024);
    }

    SECTION("reading at EOF reports short reads")
    {
        REQUIRE(io->seek(-1, Exiv2::BasicIo::end) == 0);
        CHECK(io->getb() == static_cast<int>(marker));
        Exiv2::DataBuf past = io->read(1024);
        CHECK(databuf_size(past) == 0);
    }

    SECTION("mmap declines rather than exposing a partial buffer")
    {
        CHECK(io->mmap() == nullptr);
        CHECK(io->munmap() == 0);
    }

    g_object_unref(bigFile);
    g_remove(bigPath);
}

/* A GInputStream that is not GSeekable and whose skip always fails - what a
 * gvfs-backed stream looks like over a backend that only offers sequential
 * reads.  Files on disk are GSeekable, so without this the skip-and-reopen
 * fallback in GioBasicIo would never execute in any test.
 *
 * GInputStream's vtable is a C vtable, so a real GType subclass is registered
 * rather than a C++ one.  The descriptor rides along as qdata. */
namespace {

GQuark seq_fd_quark()
{
    static GQuark q = g_quark_from_static_string("quiver-seq-fd");
    return q;
}

GQuark seq_closed_quark()
{
    static GQuark q = g_quark_from_static_string("quiver-seq-closed");
    return q;
}

GType seq_stream_get_type()
{
    static GType t = 0;
    if (0 == t)
    {
        static gssize (*read_fn)(GInputStream*, void*, gsize, GCancellable*, GError**);
        static gboolean (*close_fn)(GInputStream*, GCancellable*, GError**);
        static gssize (*skip_fn)(GInputStream*, gsize, GCancellable*, GError**);
        read_fn = [](GInputStream* s, void* buf, gsize n, GCancellable*, GError** e) -> gssize
        {
            const gpointer qd = g_object_get_qdata(G_OBJECT(s), seq_fd_quark());
            const int fd = GPOINTER_TO_INT(qd);
            if (fd < 0 || GPOINTER_TO_INT(g_object_get_qdata(G_OBJECT(s), seq_closed_quark())))
            {
                g_set_error_literal(e, G_IO_ERROR, G_IO_ERROR_CLOSED, "closed");
                return -1;
            }
            const gssize got = read(fd, buf, n);
            if (got < 0)
            {
                g_set_error_literal(e, G_IO_ERROR, G_IO_ERROR_FAILED, "read failed");
                return -1;
            }
            return got;
        };
        close_fn = [](GInputStream* s, GCancellable*, GError**) -> gboolean
        {
            g_object_set_qdata(G_OBJECT(s), seq_closed_quark(), GINT_TO_POINTER(1));
            return TRUE;
        };
        /* Refuse to skip, forcing GioBasicIo down its reopen path. */
        skip_fn = [](GInputStream*, gsize, GCancellable*, GError** e) -> gssize
        {
            g_set_error_literal(e, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "not seekable");
            return -1;
        };

        GTypeInfo info = {};
        info.class_size = sizeof(GInputStreamClass);
        info.class_init = [](gpointer klass, gpointer)
        {
            auto* k = static_cast<GInputStreamClass*>(klass);
            k->read_fn = read_fn;
            k->close_fn = close_fn;
            k->skip = skip_fn;
        };
        info.instance_size = sizeof(GInputStream);
        t = g_type_register_static(G_TYPE_INPUT_STREAM, "QuiverSeqStream", &info, GTypeFlags(0));
    }
    return t;
}

GInputStream *seq_stream_open(const char *path)
{
    const int fd = g_open(path, O_RDONLY, 0);
    GInputStream* s = G_INPUT_STREAM(g_object_new(seq_stream_get_type(), nullptr));
    g_object_set_qdata(G_OBJECT(s), seq_fd_quark(), GINT_TO_POINTER(fd));
    return s;
}

}  // namespace

TEST_CASE("GioBasicIo seeks on a stream that cannot seek", "[lib][exiv2]")
{
    const std::string imagesDir = QuiverTest_GetImagesDir();
    const std::string sampleJpg = imagesDir + "/sample_4k.jpg";
    REQUIRE(g_file_test(sampleJpg.c_str(), G_FILE_TEST_EXISTS));

    GInputStream* raw = seq_stream_open(sampleJpg.c_str());
    REQUIRE(raw != nullptr);
    REQUIRE_FALSE(G_IS_SEEKABLE(raw));   /* the whole point */
    {
        /* and it really cannot skip either, so reopen is the only way back */
        GError* skip_error = nullptr;
        REQUIRE(g_input_stream_skip(raw, 1, nullptr, &skip_error) == -1);
        g_clear_error(&skip_error);
    }

    GFile* file = g_file_new_for_path(sampleJpg.c_str());
    auto owned = GioBasicIo::open_stream(file, raw, sampleJpg);
    g_object_unref(file);
    REQUIRE(owned.get() != nullptr);
    GioBasicIo* io = static_cast<GioBasicIo*>(owned.get());

    SECTION("reads the same EXIF as a seekable file would")
    {
        auto pathImage = Exiv2::ImageFactory::open(sampleJpg);
        REQUIRE(pathImage.get() != nullptr);
        pathImage->readMetadata();

#if EXIV2_TEST_VERSION(0, 28, 0)
        auto image = Exiv2::ImageFactory::open(std::move(owned));
#else
        auto image = Exiv2::ImageFactory::open(owned);
#endif
        REQUIRE(image.get() != nullptr);
        image->readMetadata();
        CHECK(image->exifData().count() == pathImage->exifData().count());
        CHECK(image->pixelWidth() == pathImage->pixelWidth());
        CHECK(image->pixelHeight() == pathImage->pixelHeight());
    }

    SECTION("forward jumps go through skip, backward jumps reopen")
    {
        const size_t total = io->size();
        REQUIRE(total > 0);

        /* forward: skip past the middle */
        REQUIRE(io->seek(static_cast<int64_t>(total / 2), Exiv2::BasicIo::beg) == 0);
        CHECK(io->tell() == total / 2);
        Exiv2::DataBuf mid = io->read(16);
        CHECK(databuf_size(mid) == 16);

        /* backward: only reopen-and-skip can get here */
        REQUIRE(io->seek(0, Exiv2::BasicIo::beg) == 0);
        CHECK(io->tell() == 0);
        Exiv2::DataBuf start = io->read(16);
        CHECK(databuf_size(start) == 16);

        /* and back forward again, still without a seek */
        REQUIRE(io->seek(-1, Exiv2::BasicIo::end) == 0);
        CHECK(io->getb() != EOF);
    }

    g_object_unref(raw);
}

/* Every bug below was found by the same symptom: Exiv2::ImageFactory::open()
 * returning null for a file that is perfectly valid.  Image::good() ->
 * ImageFactory::checkType() -> getType() re-opens the io and probes it byte by
 * byte, consulting eof() between probes, so the io has to behave like a normal
 * seekable file at every step rather than just return the right bytes.
 *
 * These assertions pin the specific contracts that were broken:
 *  - the constructor opens the io (a directly-constructed one used to be dead)
 *  - eof() reflects the position, not a sticky flag left by the window fill
 *  - open() genuinely rewinds the underlying stream
 *  - close() leaves the io reusable, because Exiv2 closes and reopens mid-read
 */
TEST_CASE("GioBasicIo survives the open/close/probe cycle Exiv2 uses", "[lib][exiv2]")
{
    const std::string image = QuiverTest_GetImagesDir() + "/sample_rotated.jpg";
    REQUIRE(g_file_test(image.c_str(), G_FILE_TEST_EXISTS));

    GFile* file = g_file_new_for_path(image.c_str());
    /* Constructed directly, not via the factory: the factory used to be the only
     * place that opened the stream, so anything else built a dead io. */
    GioBasicIo io(file, image);
    CHECK(io.isopen());

    /* A file smaller than the 256 KiB window fills short.  eof() must not latch
     * on that, or Exiv2's sniffer sees EOF after the first two bytes and refuses
     * to identify the format. */
    io.open();
    CHECK_FALSE(io.eof());
    Exiv2::DataBuf first = io.read(2);
    REQUIRE(databuf_size(first) == 2);
    CHECK_FALSE(io.eof());
    CHECK(0xff == databuf_data(first)[0]);
    CHECK(0xd8 == databuf_data(first)[1]);

    /* getType() is what decides the format, so ask it directly. */
    io.open();
    REQUIRE(io.seek(0, Exiv2::BasicIo::beg) == 0);
    CHECK(Exiv2::ImageType::jpeg == Exiv2::ImageFactory::getType(io));

    /* close() must leave the io readable again - Exiv2 closes and reopens while
     * walking a file, and a close that tore the stream down lost the handle. */
    REQUIRE(io.close() == 0);
    REQUIRE(io.open() == 0);
    Exiv2::DataBuf again = io.read(16);
    CHECK(databuf_size(again) == 16);
    REQUIRE(io.seek(-2, Exiv2::BasicIo::end) == 0);
    CHECK(io.size() > 2);
    CHECK(Exiv2::ImageType::jpeg == Exiv2::ImageFactory::getType(io));

    /* And end to end, which is what actually regressed: no image at all. */
    GFile* file2 = g_file_new_for_path(image.c_str());
    auto io2 = GioBasicIo::open(file2, image);
    g_object_unref(file2);
    REQUIRE(io2.get() != nullptr);
#if EXIV2_TEST_VERSION(0, 28, 0)
    auto exif = Exiv2::ImageFactory::open(std::move(io2));
#else
    auto exif = Exiv2::ImageFactory::open(io2);
#endif
    REQUIRE(exif.get() != nullptr);            /* was null: no crash, just no metadata */
    exif->readMetadata();
    CHECK(0 < exif->exifData().count());
}
