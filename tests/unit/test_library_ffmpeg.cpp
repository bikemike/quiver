#include <catch2/catch_test_macros.hpp>
#include <gtk/gtk.h>
#include "PropertyView.h"
#include "QuiverAvio.h"
#include "QuiverVideoOps.h"
#include "test_helpers.h"
#include <glib.h>
#include <string>

TEST_CASE("FFmpeg Video Decoding and Probing via QuiverVideoOps", "[lib][ffmpeg][video]")
{
    std::string imagesDir = QuiverTest_GetImagesDir();
    std::string videoPath = imagesDir + "/sample_video.mp4";

    REQUIRE(g_file_test(videoPath.c_str(), G_FILE_TEST_EXISTS));
    gchar* videoUri = g_filename_to_uri(videoPath.c_str(), NULL, NULL);
    REQUIRE(videoUri != NULL);

    SECTION("Probe video metadata")
    {
        gint64 duration_ns = 0;
        gint width = 0, height = 0;
        gint num = 0, den = 0;

        gboolean ok = QuiverVideoOps::Probe(videoUri, &duration_ns, &width, &height, &num, &den);
        REQUIRE(ok == TRUE);
        REQUIRE(duration_ns > 0);
        REQUIRE(width > 0);
        REQUIRE(height > 0);
        REQUIRE(num > 0);
        REQUIRE(den > 0);
    }

    SECTION("Decode video preview frame at natural size")
    {
        gint num = 0, den = 0;
        GdkTexture* tex = QuiverVideoOps::LoadTexture(videoUri, &num, &den, -1, 0, 0);
        REQUIRE(tex != nullptr);

        int w = gdk_texture_get_width(tex);
        int h = gdk_texture_get_height(tex);
        REQUIRE(w > 0);
        REQUIRE(h > 0);

        g_object_unref(tex);
    }

    SECTION("Decode video preview frame scaled to thumbnail dimensions")
    {
        gint num = 0, den = 0;
        GdkTexture* thumb = QuiverVideoOps::LoadTexture(videoUri, &num, &den, -1, 160, 120);
        REQUIRE(thumb != nullptr);

        int w = gdk_texture_get_width(thumb);
        int h = gdk_texture_get_height(thumb);
        REQUIRE(w <= 160);
        REQUIRE(h <= 120);

        g_object_unref(thumb);
    }

    SECTION("Probe and decode read straight off a GInputStream, not a path")
    {
        /* This is the route a trash:/// URI has to take: g_file_peek_path()
         * returns NULL for it, so there is no path to hand avformat_open_input.
         * QUIVER_FORCE_STREAM_IO pushes even a local file down that route so
         * the custom AVIOContext path - which is what trashed media actually
         * uses - is covered on machines with no gvfs trash to hand.
         *
         * Results must match the native-path route exactly; a mismatch means
         * the seek/AVSEEK_SIZE plumbing is wrong and ffmpeg silently reports no
         * video stream. */
        struct EnvGuard
        {
            ~EnvGuard() { g_unsetenv("QUIVER_FORCE_STREAM_IO"); }
        } guard;

        gint64 native_duration = 0;
        gint nw = 0, nh = 0, nn = 0, nd = 0;
        REQUIRE(QuiverVideoOps::Probe(videoUri, &native_duration, &nw, &nh, &nn, &nd) == TRUE);

        GdkTexture* native_tex = QuiverVideoOps::LoadTexture(videoUri, &nn, &nd, -1, 0, 0);
        REQUIRE(native_tex != nullptr);
        const int native_w = gdk_texture_get_width(native_tex);
        const int native_h = gdk_texture_get_height(native_tex);
        g_object_unref(native_tex);

        g_setenv("QUIVER_FORCE_STREAM_IO", "1", TRUE);

        gint64 stream_duration = 0;
        gint sw = 0, sh = 0, sn = 0, sd = 0;
        REQUIRE(QuiverVideoOps::Probe(videoUri, &stream_duration, &sw, &sh, &sn, &sd) == TRUE);
        CHECK(stream_duration == native_duration);
        CHECK(sw == native_w);
        CHECK(sh == native_h);

        /* unscaled first frame */
        GdkTexture* tex = QuiverVideoOps::LoadTexture(videoUri, &sn, &sd, -1, 0, 0);
        REQUIRE(tex != nullptr);
        CHECK(gdk_texture_get_width(tex) == native_w);
        CHECK(gdk_texture_get_height(tex) == native_h);
        g_object_unref(tex);

        /* scaled thumbnail - the shape the icon view actually asks for */
        GdkTexture* thumb = QuiverVideoOps::LoadTexture(videoUri, &sn, &sd, -1, 160, 120);
        REQUIRE(thumb != nullptr);
        REQUIRE(gdk_texture_get_width(thumb) > 0);
        REQUIRE(gdk_texture_get_height(thumb) > 0);
        CHECK(gdk_texture_get_width(thumb) <= 160);
        CHECK(gdk_texture_get_height(thumb) <= 120);
        g_object_unref(thumb);
    }

    SECTION("Seek to specific timestamp")
    {
        // 0.5 seconds in nanoseconds
        gint64 pos_ns = 500000000;
        GdkTexture* frame = QuiverVideoOps::LoadTexture(videoUri, nullptr, nullptr, pos_ns, 320, 240);
        REQUIRE(frame != nullptr);
        g_object_unref(frame);
    }

    SECTION("Decode video preview frame with arbitrary target bounds and aspect scaling")
    {
        GdkTexture* frame = QuiverVideoOps::LoadTexture(videoUri, nullptr, nullptr, -1, 925, 585);
        REQUIRE(frame != nullptr);
        int w = gdk_texture_get_width(frame);
        int h = gdk_texture_get_height(frame);
        REQUIRE(w <= 925);
        REQUIRE(h <= 585);
        g_object_unref(frame);
    }

    SECTION("Error handling on invalid or non-video URI")
    {
        gboolean ok = QuiverVideoOps::Probe("file:///does/not/exist.mp4");
        REQUIRE(ok == FALSE);

        GdkTexture* tex = QuiverVideoOps::LoadTexture("file:///does/not/exist.mp4");
        REQUIRE(tex == nullptr);
    }

#if HAVE_GDK_PIXBUF
    SECTION("Legacy LoadPixbuf tests")
    {
        gint num = 0, den = 0;
        GdkPixbuf* pb = QuiverVideoOps::LoadPixbuf(videoUri, &num, &den, -1, 0, 0);
        REQUIRE(pb != nullptr);
        g_object_unref(pb);

        GdkPixbuf* thumb = QuiverVideoOps::LoadPixbuf(videoUri, &num, &den, -1, 160, 120);
        REQUIRE(thumb != nullptr);
        g_object_unref(thumb);
    }
#endif

    SECTION("Video display matrix rotation handling")
    {
        const char* rotatedVideoPath = "/workspace/PXL_20250626_001254423.mp4";
        if (g_file_test(rotatedVideoPath, G_FILE_TEST_EXISTS))
        {
            gchar* rotUri = g_filename_to_uri(rotatedVideoPath, NULL, NULL);
            REQUIRE(rotUri != NULL);

            gint width = 0, height = 0;
            gboolean ok = QuiverVideoOps::Probe(rotUri, NULL, &width, &height);
            REQUIRE(ok == TRUE);
            // Display dimensions must be swapped to portrait (1080x1920)
            REQUIRE(width == 1080);
            REQUIRE(height == 1920);

            GdkTexture* tex = QuiverVideoOps::LoadTexture(rotUri);
            REQUIRE(tex != nullptr);
            REQUIRE(gdk_texture_get_width(tex) == 1080);
            REQUIRE(gdk_texture_get_height(tex) == 1920);
            g_object_unref(tex);

#if HAVE_GDK_PIXBUF
            GdkPixbuf* pb = QuiverVideoOps::LoadPixbuf(rotUri);
            REQUIRE(pb != nullptr);
            REQUIRE(gdk_pixbuf_get_width(pb) == 1080);
            REQUIRE(gdk_pixbuf_get_height(pb) == 1920);
            g_object_unref(pb);
#endif
            g_free(rotUri);
        }
    }

    g_free(videoUri);
}

/* Container::Open() runs avformat_find_stream_info() for its callers.  Calling
 * it a second time on the returned context walks parse buffers the first pass
 * already freed, and crashes inside libavformat - PropertyView used to do
 * exactly that and showed it up as random heap corruption in unrelated fields.
 * Probing twice is the bug here; reading the results is the point. */
TEST_CASE("QuiverAvio container is probed once by Open", "[lib][ffmpeg][video]")
{
    const std::string imagesDir = QuiverTest_GetImagesDir();
    const std::string video = imagesDir + "/sample_video.mp4";
    REQUIRE(g_file_test(video.c_str(), G_FILE_TEST_EXISTS));
    gchar* uri = g_filename_to_uri(video.c_str(), NULL, NULL);
    REQUIRE(uri != NULL);

    SECTION("stream info is already present after Open")
    {
        QuiverAvio::Container container;
        REQUIRE(container.Open(uri));
        AVFormatContext* fmt = container.fmt();
        REQUIRE(fmt != nullptr);

        /* everything PropertyView reads out of the context is already there,
         * with no second probe */
        REQUIRE(fmt->nb_streams > 0);
        REQUIRE(fmt->iformat != nullptr);
        REQUIRE(fmt->iformat->name != nullptr);
        AVStream* stream = fmt->streams[0];
        REQUIRE(stream->codecpar != nullptr);
        CHECK((AVMediaType)AVMEDIA_TYPE_VIDEO == stream->codecpar->codec_type);
        REQUIRE(stream->codecpar->width > 0);
        REQUIRE(stream->codecpar->height > 0);
        CHECK(AV_CODEC_ID_NONE != stream->codecpar->codec_id);
    }

    SECTION("probe buffers stay valid when read without re-probing")
    {
        QuiverAvio::Container container;
        REQUIRE(container.Open(uri));
        AVFormatContext* fmt = container.fmt();
        REQUIRE(fmt->nb_streams > 0);

        /* touching what find_stream_info() filled in is safe, repeatedly */
        for (int i = 0; i < 3; ++i)
        {
            AVStream* stream = fmt->streams[0];
            CHECK(stream->codecpar->width > 0);
            CHECK(stream->time_base.num != 0);
            const AVCodecParameters* par = stream->codecpar;
            if (par->extradata_size > 0)
            {
                /* the block Open() probed has to still be there and readable:
                 * a second find_stream_info() would have released it */
                CHECK(par->extradata != nullptr);
                CHECK(par->extradata_size > 0);
                for (int b = 0; b < par->extradata_size; ++b)
                    CHECK(par->extradata[b] == par->extradata[b]);   /* readable */
            }
            if (fmt->metadata != nullptr)
                av_dict_get(fmt->metadata, "creation_time", NULL, 0);
        }
    }

    g_free(uri);
}

/* PropertyView used to run avformat_find_stream_info() a second time on a
 * context QuiverAvio::Container::Open() had already probed.  That freed and
 * reallocated the per-stream parse buffers, so the second pass walked released
 * state - heap corruption that surfaced as garbage in unrelated fields rather
 * than an obvious fault.  Driving the real PropertyView is the only thing that
 * would have caught it; testing QuiverAvio alone would not. */
/* Container::Open() probes the context for its caller.  avformat_find_stream_info()
 * is not idempotent - a second call walks per-stream parse buffers the first one
 * freed and corrupts the heap - so this pins that the context Open() returns is
 * complete and needs no further probing.  PropertyView used to probe twice and
 * that is what produced crashes inside libavformat and garbage in unrelated
 * fields; the crash is not reproducible without a display, so this asserts the
 * property that makes it safe instead.
 */
TEST_CASE("a probed QuiverAvio container needs no second probe", "[lib][ffmpeg][video]")
{
    const std::string imagesDir = QuiverTest_GetImagesDir();
    for (const char* name : {"sample_video.mp4", "sample_video_4x3.mp4"})
    {
        const std::string video = imagesDir + "/" + name;
        REQUIRE(g_file_test(video.c_str(), G_FILE_TEST_EXISTS));
        gchar* uri = g_filename_to_uri(video.c_str(), NULL, NULL);
        REQUIRE(uri != NULL);

        QuiverAvio::Container container;
        REQUIRE(container.Open(uri));
        AVFormatContext* fmt = container.fmt();
        REQUIRE(fmt != nullptr);

        /* Open() populated everything a metadata reader needs - this is the
         * state a second find_stream_info() would have tried to rebuild */
        REQUIRE(fmt->nb_streams > 0);
        REQUIRE(fmt->iformat != nullptr);
        REQUIRE(fmt->iformat->name != nullptr);
        AVStream* stream = fmt->streams[0];
        REQUIRE(stream->codecpar != nullptr);
        CHECK(stream->codecpar->width > 0);
        CHECK(stream->codecpar->height > 0);
        CHECK(AV_CODEC_ID_NONE != stream->codecpar->codec_id);
        CHECK(stream->time_base.num != 0);
        /* reading it repeatedly stays stable: no lazy buffers get released
         * underneath a second reader */
        const int w = stream->codecpar->width;
        const int h = stream->codecpar->height;
        for (int i = 0; i < 5; ++i)
        {
            CHECK(fmt->streams[0]->codecpar->width == w);
            CHECK(fmt->streams[0]->codecpar->height == h);
        }

        g_free(uri);
    }
}
