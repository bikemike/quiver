#include "ImageDecoder.h"
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib/gstdio.h>
#include <config.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <vector>
#include <utility>
#include <algorithm>
#include <thread>
#include <semaphore>
#include <zlib.h>
#include "QuiverUtils.h"

#if HAVE_GLYCIN
ImageDecoderBackend ImageDecoder::s_backend = ImageDecoderBackend::AUTO;
#elif HAVE_GDK_PIXBUF
ImageDecoderBackend ImageDecoder::s_backend = ImageDecoderBackend::PIXBUF;
#else
ImageDecoderBackend ImageDecoder::s_backend = ImageDecoderBackend::AUTO;
#endif

bool ImageDecoder::IsBackendSupported(ImageDecoderBackend backend)
{
    switch (backend)
    {
    case ImageDecoderBackend::AUTO:
        return true;
    case ImageDecoderBackend::GLYCIN:
        return HAVE_GLYCIN != 0;
    case ImageDecoderBackend::PIXBUF:
        return HAVE_GDK_PIXBUF != 0;
    }
    return false;
}

void ImageDecoder::SetBackend(ImageDecoderBackend backend)
{
    if (backend == ImageDecoderBackend::GLYCIN && !IsBackendSupported(ImageDecoderBackend::GLYCIN))
    {
        g_warning("ImageDecoder: libglycin backend is disabled in build configuration.");
        return;
    }
    if (backend == ImageDecoderBackend::PIXBUF && !IsBackendSupported(ImageDecoderBackend::PIXBUF))
    {
        g_warning("ImageDecoder: GdkPixbuf backend is disabled in build configuration.");
        return;
    }
    s_backend = backend;
}

ImageDecoderBackend ImageDecoder::GetBackend()
{
    return s_backend;
}

static bool is_video_mimetype(const char* mimetype)
{
    if (!mimetype) return false;
    return g_str_has_prefix(mimetype, "video/") ||
           strcmp(mimetype, "application/x-matroska") == 0 ||
           strcmp(mimetype, "application/ogg") == 0 ||
           strcmp(mimetype, "application/vnd.rn-realmedia") == 0 ||
           strcmp(mimetype, "application/vnd.ms-asf") == 0 ||
           strcmp(mimetype, "application/x-ms-wmv") == 0;
}

static bool is_file_too_large(GFile *file, GCancellable *cancellable = NULL)
{
    if (!file) return true;
    GFileInfo *info = g_file_query_info(file, G_FILE_ATTRIBUTE_STANDARD_SIZE, G_FILE_QUERY_INFO_NONE, cancellable, NULL);
    if (info)
    {
        goffset sz = g_file_info_get_size(info);
        g_object_unref(info);
        if (sz > 500LL * 1024 * 1024)
            return true;
    }
    return false;
}

// -------------------------------------------------------------------------
// Decode Concurrency Limiter
// -------------------------------------------------------------------------
// The underlying image libraries (libjxl / libheif / gdk-pixbuf loaders)
// spin up a per-decode worker pool sized to the CPU count, so each full-image
// decode can momentarily spawn nproc threads. A folder of thumbnails being
// generated plus the viewer load can otherwise saturate every core, which
// starves the UI thread and makes the app feel locked up. Cap how many
// full-image decodes run at once; this also serializes concurrent glycin
// loads of the same file, which are not safe against each other.
namespace
{
    std::counting_semaphore<64> g_decode_slots(std::clamp<unsigned int>(
        (std::max(1u, std::thread::hardware_concurrency()) + 1u) / 2u, 1u, 8u));

    class DecodeSlotGuard
    {
    public:
        DecodeSlotGuard() { g_decode_slots.acquire(); }
        ~DecodeSlotGuard() { g_decode_slots.release(); }
        DecodeSlotGuard(const DecodeSlotGuard&) = delete;
        DecodeSlotGuard& operator=(const DecodeSlotGuard&) = delete;
    };
} // namespace

// -------------------------------------------------------------------------
// Fast Decode-Free Dimension Probing
// -------------------------------------------------------------------------
// Reading Width/Height for the common raster formats must not require a full
// image decode. GNOME thumbnail files embed the source dimensions in their
// text metadata (handled in QuiverFile); the formats handled here let us
// answer GetDimensions() from a few dozen bytes of the file header. Anything
// that is not recognized simply falls through to the configured decode
// backend, so behavior is unchanged for exotic formats (HEIF/AVIF/etc.).
namespace
{
    static bool read_file_prefix(GFile *file, std::vector<uint8_t> &buf, size_t max_bytes = 262144)
    {
        GInputStream *in = G_INPUT_STREAM(g_file_read(file, NULL, NULL));
        if (!in)
            return false;
        buf.resize(max_bytes);
        gssize total = 0;
        while (total < (gssize)max_bytes)
        {
            gssize n = g_input_stream_read(in, buf.data() + total,
                                           max_bytes - (size_t)total, NULL, NULL);
            if (n <= 0)
                break;
            total += n;
        }
        buf.resize((size_t)total);
        g_object_unref(in);
        return total > 0;
    }

    static uint16_t rd16be(const uint8_t *p)
    {
        return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
    }

    static uint32_t rd32be(const uint8_t *p)
    {
        return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
               ((uint32_t)p[2] << 8) | (uint32_t)p[3];
    }

    static uint16_t rd16le(const uint8_t *p)
    {
        return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
    }

    static uint32_t rd32le(const uint8_t *p)
    {
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
               ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    }

    static bool fast_header_dimensions_png(const std::vector<uint8_t> &b, int *w, int *h)
    {
        static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
        if (b.size() < 24)
            return false;
        if (memcmp(b.data(), sig, 8) != 0)
            return false;
        if (b[12] != 'I' || b[13] != 'H' || b[14] != 'D' || b[15] != 'R')
            return false;
        *w = (int)rd32be(b.data() + 16);
        *h = (int)rd32be(b.data() + 20);
        return *w > 0 && *h > 0;
    }

    static bool fast_header_dimensions_gif(const std::vector<uint8_t> &b, int *w, int *h)
    {
        if (b.size() < 10)
            return false;
        if (memcmp(b.data(), "GIF87a", 6) != 0 && memcmp(b.data(), "GIF89a", 6) != 0)
            return false;
        *w = (int)rd16le(b.data() + 6);
        *h = (int)rd16le(b.data() + 8);
        return *w > 0 && *h > 0;
    }

    static bool fast_header_dimensions_bmp(const std::vector<uint8_t> &b, int *w, int *h)
    {
        if (b.size() < 26)
            return false;
        if (b[0] != 'B' || b[1] != 'M')
            return false;
        uint32_t width = rd32le(b.data() + 18);
        uint32_t height = rd32le(b.data() + 22);
        if (height & 0x80000000u) // top-down bitmap: height stored as a signed negative
            height = (uint32_t)(-(int32_t)height);
        if (width == 0 || height == 0 || width > 100000 || height > 100000)
            return false;
        *w = (int)width;
        *h = (int)height;
        return true;
    }

    static bool fast_header_dimensions_jpeg(const std::vector<uint8_t> &b, int *w, int *h)
    {
        if (b.size() < 4 || b[0] != 0xFF || b[1] != 0xD8)
            return false;
        size_t off = 2;
        while (off + 4 <= b.size())
        {
            if (b[off] != 0xFF)
            {
                ++off;
                continue;
            }
            uint8_t marker = b[off + 1];
            if (marker == 0xFF)
            {
                ++off;
                continue;
            }
            if (marker == 0xD8 || marker == 0x01)
            {
                off += 2;
                continue;
            }
            // SOF0..SOF15 (start of frame): [len(2)] [prec(1)] [height(2)] [width(2)]
            if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 &&
                marker != 0xC8 && marker != 0xCC)
            {
                if (off + 9 > b.size())
                    return false;
                *h = (int)rd16be(b.data() + off + 5);
                *w = (int)rd16be(b.data() + off + 7);
                return *w > 0 && *h > 0;
            }
            // standalone markers without a length payload
            if (marker >= 0xD0 && marker <= 0xD9)
            {
                off += 2;
                continue;
            }
            if (off + 4 > b.size())
                return false;
            uint16_t seglen = rd16be(b.data() + off + 2);
            if (seglen < 2)
                return false;
            off += 2u + seglen;
        }
        return false;
    }

    static bool fast_header_dimensions_webp(const std::vector<uint8_t> &b, int *w, int *h)
    {
        if (b.size() < 30)
            return false;
        if (memcmp(b.data(), "RIFF", 4) != 0 || memcmp(b.data() + 8, "WEBP", 4) != 0)
            return false;
        if (memcmp(b.data() + 12, "VP8X", 4) == 0)
        {
            if (b.size() < 30)
                return false;
            *w = 1 + (int)((uint32_t)b[24] | ((uint32_t)b[25] << 8) | ((uint32_t)b[26] << 16));
            *h = 1 + (int)((uint32_t)b[27] | ((uint32_t)b[28] << 8) | ((uint32_t)b[29] << 16));
            return *w > 0 && *h > 0;
        }
        if (memcmp(b.data() + 12, "VP8 ", 4) == 0)
        {
            if (b.size() < 30)
                return false;
            *w = (int)rd16le(b.data() + 26) & 0x3FFF;
            *h = (int)rd16le(b.data() + 28) & 0x3FFF;
            return *w > 0 && *h > 0;
        }
        if (memcmp(b.data() + 12, "VP8L", 4) == 0)
        {
            if (b.size() < 25)
                return false;
            uint32_t bits = rd32le(b.data() + 21);
            *w = (int)((bits & 0x3FFFu) + 1u);
            *h = (int)(((bits >> 14) & 0x3FFFu) + 1u);
            return *w > 0 && *h > 0;
        }
        return false;
    }

    static bool fast_header_dimensions_tiff(const std::vector<uint8_t> &b, int *w, int *h)
    {
        if (b.size() < 8)
            return false;
        const bool le = (b[0] == 'I' && b[1] == 'I');
        const bool be = (b[0] == 'M' && b[1] == 'M');
        if (!le && !be)
            return false;
        uint16_t magic = le ? rd16le(b.data() + 2) : rd16be(b.data() + 2);
        if (magic != 42)
            return false;
        uint32_t ifd = le ? rd32le(b.data() + 4) : rd32be(b.data() + 4);
        if (ifd + 2 > b.size())
            return false;
        uint16_t count = le ? rd16le(b.data() + ifd) : rd16be(b.data() + ifd);
        uint32_t wd = 0, ht = 0;
        for (uint16_t i = 0; i < count; ++i)
        {
            size_t e = ifd + 2 + (size_t)i * 12;
            if (e + 12 > b.size())
                return false;
            uint16_t tag = le ? rd16le(b.data() + e) : rd16be(b.data() + e);
            uint16_t type = le ? rd16le(b.data() + e + 2) : rd16be(b.data() + e + 2);
            uint32_t n = le ? rd32le(b.data() + e + 4) : rd32be(b.data() + e + 4);
            uint32_t val = 0;
            if (type == 3 && n == 1) // SHORT
                val = le ? rd16le(b.data() + e + 8) : rd16be(b.data() + e + 8);
            else if (type == 4 && n == 1) // LONG
                val = le ? rd32le(b.data() + e + 8) : rd32be(b.data() + e + 8);
            else
                continue;
            if (tag == 256)
                wd = val;
            else if (tag == 257)
                ht = val;
        }
        if (wd > 0 && ht > 0)
        {
            *w = (int)wd;
            *h = (int)ht;
            return true;
        }
        return false;
    }

    static bool fast_header_dimensions(const std::vector<uint8_t> &b, int *w, int *h)
    {
        return fast_header_dimensions_png(b, w, h) ||
               fast_header_dimensions_jpeg(b, w, h) ||
               fast_header_dimensions_gif(b, w, h) ||
               fast_header_dimensions_bmp(b, w, h) ||
               fast_header_dimensions_webp(b, w, h) ||
               fast_header_dimensions_tiff(b, w, h);
    }
} // namespace

// -------------------------------------------------------------------------
// Fast Dimension Probing
// -------------------------------------------------------------------------

bool ImageDecoder::GetDimensions(GFile *file, const char *mimetype, int *width, int *height)
{
    if (!file || !width || !height)
        return false;

    *width = -1;
    *height = -1;

    if (is_video_mimetype(mimetype) || is_file_too_large(file))
        return false;

    // decode-free header probing for the common raster formats
    std::vector<uint8_t> prefix;
    if (read_file_prefix(file, prefix))
    {
        int fw = -1, fh = -1;
        if (fast_header_dimensions(prefix, &fw, &fh))
        {
            *width = fw;
            *height = fh;
            return true;
        }
    }

#if HAVE_GLYCIN
    if (s_backend != ImageDecoderBackend::PIXBUF)
    {
        if (GlycinGetDimensions(file, width, height))
            return true;
        if (s_backend == ImageDecoderBackend::GLYCIN)
            return false;
    }
#endif

#if HAVE_GDK_PIXBUF
    return PixbufGetDimensions(file, mimetype, width, height);
#else
    (void)mimetype;
    GdkTexture *tex = gdk_texture_new_from_file(file, NULL);
    if (tex)
    {
        *width = gdk_texture_get_width(tex);
        *height = gdk_texture_get_height(tex);
        g_object_unref(tex);
        return true;
    }
    return false;
#endif
}

#if HAVE_GLYCIN
bool ImageDecoder::GlycinGetDimensions(GFile *file, int *width, int *height)
{
    DecodeSlotGuard slot;

    GlyLoader *loader = gly_loader_new(file);
    if (!loader)
        return false;

    GError *error = NULL;
    GlyImage *image = gly_loader_load(loader, &error);
    if (!image)
    {
        if (error)
            g_error_free(error);
        g_object_unref(loader);
        return false;
    }

    uint32_t w = gly_image_get_width(image);
    uint32_t h = gly_image_get_height(image);

    g_object_unref(image);
    g_object_unref(loader);

    if (w > 0 && h > 0)
    {
        *width = (int)w;
        *height = (int)h;
        return true;
    }

    return false;
}
#endif

#if HAVE_GDK_PIXBUF
typedef struct _PixbufProbeInfo
{
    gint width;
    gint height;
} PixbufProbeInfo;

static void on_probe_size_prepared(GdkPixbufLoader *loader, gint width, gint height, gpointer user_data)
{
    (void)loader;
    PixbufProbeInfo *info = (PixbufProbeInfo*)user_data;
    info->width = width;
    info->height = height;
}

bool ImageDecoder::PixbufGetDimensions(GFile *file, const char *mimetype, int *width, int *height)
{
    GInputStream *inStream = G_INPUT_STREAM(g_file_read(file, NULL, NULL));
    if (!inStream)
        return false;

    GdkPixbufLoader *loader = NULL;
    if (mimetype && strlen(mimetype) > 0)
        loader = gdk_pixbuf_loader_new_with_mime_type(mimetype, NULL);
    if (!loader)
        loader = gdk_pixbuf_loader_new();

    if (!loader)
    {
        g_object_unref(inStream);
        return false;
    }

    PixbufProbeInfo probe_info = { -1, -1 };
    g_signal_connect(loader, "size-prepared", G_CALLBACK(on_probe_size_prepared), &probe_info);

    const int buffsize = 512;
    guchar buffer[buffsize];
    gssize bytes_read = 0;
    GError *tmp_error = NULL;

    while (0 < (bytes_read = g_input_stream_read(inStream, buffer, buffsize, NULL, NULL)))
    {
        gdk_pixbuf_loader_write(loader, buffer, bytes_read, &tmp_error);
        if (tmp_error)
        {
            g_error_free(tmp_error);
            break;
        }
        if (probe_info.width > 0 && probe_info.height > 0)
            break;
    }

    gdk_pixbuf_loader_close(loader, NULL);
    g_object_unref(loader);
    g_object_unref(inStream);

    if (probe_info.width > 0 && probe_info.height > 0)
    {
        *width = probe_info.width;
        *height = probe_info.height;
        return true;
    }

    return false;
}
#endif

// -------------------------------------------------------------------------
// Helper: Convert between GlyFrame, GdkTexture, and GdkPixbuf
// -------------------------------------------------------------------------

#if HAVE_GLYCIN
static GdkTexture* frame_to_texture(GlyFrame *frame)
{
    if (!frame)
        return NULL;

    uint32_t w = gly_frame_get_width(frame);
    uint32_t h = gly_frame_get_height(frame);
    uint32_t stride = gly_frame_get_stride(frame);
    GlyMemoryFormat gly_fmt = gly_frame_get_memory_format(frame);
    GBytes *bytes = gly_frame_get_buf_bytes(frame);

    return gdk_memory_texture_new(w, h, (GdkMemoryFormat)gly_fmt, bytes, stride);
}

#if HAVE_GDK_PIXBUF
static GdkPixbuf* frame_to_pixbuf(GlyFrame *frame)
{
    if (!frame)
        return NULL;

    uint32_t w = gly_frame_get_width(frame);
    uint32_t h = gly_frame_get_height(frame);
    uint32_t stride = gly_frame_get_stride(frame);
    GlyMemoryFormat gly_fmt = gly_frame_get_memory_format(frame);
    GBytes *bytes = gly_frame_get_buf_bytes(frame);

    // Fast-path: RGBA or RGB
    if (gly_fmt == GLY_MEMORY_R8G8B8A8)
    {
        return gdk_pixbuf_new_from_bytes(bytes, GDK_COLORSPACE_RGB, TRUE, 8, w, h, stride);
    }
    if (gly_fmt == GLY_MEMORY_R8G8B8)
    {
        return gdk_pixbuf_new_from_bytes(bytes, GDK_COLORSPACE_RGB, FALSE, 8, w, h, stride);
    }

    // General fallback: wrap into GdkTexture and convert to GdkPixbuf
    GdkTexture *tex = gdk_memory_texture_new(w, h, (GdkMemoryFormat)gly_fmt, bytes, stride);
    if (!tex)
        return NULL;

    GdkPixbuf *pb = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, w, h);
    if (pb)
    {
        GdkTextureDownloader *dl = gdk_texture_downloader_new(tex);
        gdk_texture_downloader_set_format(dl, GDK_MEMORY_R8G8B8A8);
        gdk_texture_downloader_download_into(dl, gdk_pixbuf_get_pixels(pb), (gsize)gdk_pixbuf_get_rowstride(pb));
        gdk_texture_downloader_free(dl);
    }
    g_object_unref(tex);
    return pb;
}
#endif
#endif

// -------------------------------------------------------------------------
// File Decoding
// -------------------------------------------------------------------------

#if HAVE_GDK_PIXBUF
GdkPixbuf* ImageDecoder::DecodeFilePixbuf(GFile *file, const char *mimetype,
                                         int max_width, int max_height,
                                         GCancellable *cancellable,
                                         GError **error)
{
    if (!file)
        return NULL;

    if (is_video_mimetype(mimetype) || is_file_too_large(file, cancellable))
        return NULL;

#if HAVE_GLYCIN
    if (s_backend != ImageDecoderBackend::PIXBUF)
    {
        GError *err = NULL;
        GdkPixbuf *pb = GlycinDecodeFilePixbuf(file, cancellable, &err);
        if (pb)
            return pb;
        if (err)
            g_error_free(err);
        if (s_backend == ImageDecoderBackend::GLYCIN)
            return NULL;
    }
#endif

    return PixbufDecodeFilePixbuf(file, mimetype, max_width, max_height, cancellable, error);
}
#endif

GdkTexture* ImageDecoder::DecodeFileTexture(GFile *file, const char *mimetype,
                                           GCancellable *cancellable,
                                           GError **error)
{
    if (!file)
        return NULL;

    if (is_video_mimetype(mimetype) || is_file_too_large(file, cancellable))
        return NULL;

#if HAVE_GLYCIN
    if (s_backend != ImageDecoderBackend::PIXBUF)
    {
        GError *err = NULL;
        GdkTexture *tex = GlycinDecodeFileTexture(file, cancellable, &err);
        if (tex)
            return tex;
        if (err)
            g_error_free(err);
        if (s_backend == ImageDecoderBackend::GLYCIN)
            return NULL;
    }
#endif

#if HAVE_GDK_PIXBUF
    GdkPixbuf *pb = DecodeFilePixbuf(file, mimetype, 0, 0, cancellable, error);
    if (pb)
    {
        GdkTexture *tex = QuiverUtils::PixbufToTexture(pb);
        g_object_unref(pb);
        return tex;
    }
#endif

    GError *err = NULL;
    GdkTexture *tex = gdk_texture_new_from_file(file, &err);
    if (tex)
        return tex;
    if (err)
    {
        if (error)
            *error = err;
        else
            g_error_free(err);
    }
    return NULL;
}

#if HAVE_GLYCIN && HAVE_GDK_PIXBUF
GdkPixbuf* ImageDecoder::GlycinDecodeFilePixbuf(GFile *file, GCancellable *cancellable, GError **error)
{
    DecodeSlotGuard slot;

    GlyLoader *loader = gly_loader_new(file);
    if (!loader)
        return NULL;

    gly_loader_set_apply_transformations(loader, FALSE);

    GlyImage *image = gly_loader_load(loader, error);
    if (!image)
    {
        g_object_unref(loader);
        return NULL;
    }

    (void)cancellable;
    GlyFrame *frame = gly_image_next_frame(image, error);
    GdkPixbuf *pb = NULL;
    if (frame)
    {
        pb = frame_to_pixbuf(frame);
        g_object_unref(frame);
    }

    g_object_unref(image);
    g_object_unref(loader);
    return pb;
}
#endif

#if HAVE_GLYCIN
GdkTexture* ImageDecoder::GlycinDecodeFileTexture(GFile *file, GCancellable *cancellable, GError **error)
{
    DecodeSlotGuard slot;

    GlyLoader *loader = gly_loader_new(file);
    if (!loader)
        return NULL;

    gly_loader_set_apply_transformations(loader, TRUE);

    GlyImage *image = gly_loader_load(loader, error);
    if (!image)
    {
        g_object_unref(loader);
        return NULL;
    }

    (void)cancellable;
    GlyFrame *frame = gly_image_next_frame(image, error);
    GdkTexture *tex = NULL;
    if (frame)
    {
        tex = frame_to_texture(frame);
        g_object_unref(frame);
    }

    g_object_unref(image);
    g_object_unref(loader);
    if (tex)
    {
        g_object_set_data(G_OBJECT(tex), "glycin-transformed", GINT_TO_POINTER(1));
    }
    return tex;
}

// -------------------------------------------------------------------------
// Animated image decode (glycin frame iterator)
// -------------------------------------------------------------------------
static const gsize      kMaxAnimationFrames  = 512;
static const guint64    kMaxAnimationPixels  = 64u * 1024 * 1024;

GdkTexture* ImageDecoder::DecodeFileAnimation(GFile *file, GdkTexture ***frames,
                                              gint **delays_ms, gsize *n_frames,
                                              GError **error)
{
    *frames = NULL;
    *delays_ms = NULL;
    *n_frames = 0;

    if (!file)
        return NULL;

    DecodeSlotGuard slot;

    GlyLoader *loader = gly_loader_new(file);
    if (!loader)
        return NULL;

    gly_loader_set_apply_transformations(loader, TRUE);

    GlyImage *image = gly_loader_load(loader, error);
    if (!image)
    {
        g_object_unref(loader);
        return NULL;
    }

    std::vector<GdkTexture*> vec_frames;
    std::vector<gint> vec_delays;
    GBytes *first_bytes = NULL;
    guint64 total_pixels = 0;
    GdkTexture *first_texture = NULL;

    while (vec_frames.size() < kMaxAnimationFrames)
    {
        GError *ferr = NULL;
        GlyFrame *frame = gly_image_next_frame(image, &ferr);
        if (!frame)
        {
            if (ferr)
                g_error_free(ferr);
            break;
        }

        guint32 w = gly_frame_get_width(frame);
        guint32 h = gly_frame_get_height(frame);
        int64_t delay_us = gly_frame_get_delay(frame);

        /* glycin loops a resumed animation back to the first frame, so detect
         * the end by comparing every later frame against the first one.  The
         * first frame of a still image carries a zero delay and a single
         * frame is all we capture. */
        if (vec_frames.empty())
        {
            GBytes *b = gly_frame_get_buf_bytes(frame);
            first_bytes = (NULL != b) ? g_bytes_ref(b) : NULL;
        }
        else
        {
            GBytes *b = gly_frame_get_buf_bytes(frame);
            if (NULL == b || (NULL != first_bytes && g_bytes_compare(first_bytes, b) == 0))
            {
                /* Empty buffer, or looped back to the first frame: end. */
                g_object_unref(frame);
                break;
            }
        }

        GdkTexture *tex = frame_to_texture(frame);
        g_object_unref(frame);
        if (!tex)
            break;

        total_pixels += (guint64)w * (guint64)h;
        if (total_pixels > kMaxAnimationPixels)
        {
            g_object_unref(tex);
            break;
        }

        if (vec_frames.empty())
        {
            first_texture = tex;
            g_object_set_data(G_OBJECT(tex), "glycin-transformed", GINT_TO_POINTER(1));
        }

        gint delay_ms = (delay_us <= 0) ? 0 : (gint)((delay_us + 500) / 1000);
        if (delay_ms < 0)
            delay_ms = 0;
        vec_frames.push_back(tex);
        vec_delays.push_back(delay_ms);
    }

    if (NULL != first_bytes)
        g_bytes_unref(first_bytes);

    g_object_unref(image);
    g_object_unref(loader);

    if (vec_frames.empty())
        return NULL;

    *frames = (GdkTexture**)g_new0(GdkTexture*, vec_frames.size());
    *delays_ms = (gint*)g_new0(gint, vec_frames.size());
    for (size_t i = 0; i < vec_frames.size(); i++)
    {
        (*frames)[i] = vec_frames[i];
        (*delays_ms)[i] = vec_delays[i];
    }
    *n_frames = vec_frames.size();
    /* The first frame is also the still texture returned to the caller, so it
     * carries its own reference in addition to the one held by the frame
     * array (the caller unrefs both independently). */
    return (GdkTexture*)g_object_ref(first_texture);
}
#endif

#if HAVE_GDK_PIXBUF
typedef struct _PixbufLoadInfo
{
    int max_width;
    int max_height;
} PixbufLoadInfo;

static void on_pixbuf_load_size_prepared(GdkPixbufLoader *loader, gint width, gint height, gpointer user_data)
{
    PixbufLoadInfo *info = (PixbufLoadInfo*)user_data;
    if (info->max_width > 0 && info->max_height > 0)
    {
        if (width > info->max_width || height > info->max_height)
        {
            double w_ratio = (double)info->max_width / width;
            double h_ratio = (double)info->max_height / height;
            double ratio = (w_ratio < h_ratio) ? w_ratio : h_ratio;
            gint new_w = (gint)(width * ratio);
            gint new_h = (gint)(height * ratio);
            if (new_w > 0 && new_h > 0)
            {
                gdk_pixbuf_loader_set_size(loader, new_w, new_h);
            }
        }
    }
}

GdkPixbuf* ImageDecoder::PixbufDecodeFilePixbuf(GFile *file, const char *mimetype,
                                               int max_width, int max_height,
                                               GCancellable *cancellable, GError **error)
{
    GInputStream *inStream = G_INPUT_STREAM(g_file_read(file, cancellable, error));
    if (!inStream)
        return NULL;

    GdkPixbufLoader *loader = NULL;
    if (mimetype && strlen(mimetype) > 0)
        loader = gdk_pixbuf_loader_new_with_mime_type(mimetype, NULL);
    if (!loader)
        loader = gdk_pixbuf_loader_new();

    if (!loader)
    {
        g_object_unref(inStream);
        return NULL;
    }

    PixbufLoadInfo info = { max_width, max_height };
    if (max_width > 0 && max_height > 0)
    {
        g_signal_connect(loader, "size-prepared", G_CALLBACK(on_pixbuf_load_size_prepared), &info);
    }

    const int buffsize = 65536;
    guchar buffer[buffsize];
    gssize bytes_read = 0;
    GError *tmp_error = NULL;

    while (0 < (bytes_read = g_input_stream_read(inStream, buffer, buffsize, cancellable, &tmp_error)))
    {
        gdk_pixbuf_loader_write(loader, buffer, bytes_read, &tmp_error);
        if (tmp_error)
            break;
    }

    if (tmp_error)
    {
        if (error)
            *error = tmp_error;
        else
            g_error_free(tmp_error);
        gdk_pixbuf_loader_close(loader, NULL);
        g_object_unref(loader);
        g_object_unref(inStream);
        return NULL;
    }

    gdk_pixbuf_loader_close(loader, error);
    GdkPixbuf *pb = gdk_pixbuf_loader_get_pixbuf(loader);
    if (pb)
        g_object_ref(pb);

    g_object_unref(loader);
    g_object_unref(inStream);
    return pb;
}
#endif


// -------------------------------------------------------------------------
// Bytes Decoding (EXIF Thumbnails)
// -------------------------------------------------------------------------

#if HAVE_GDK_PIXBUF
GdkPixbuf* ImageDecoder::DecodeBytesPixbuf(GBytes *bytes, const char *mimetype,
                                          GCancellable *cancellable, GError **error)
{
    if (!bytes)
        return NULL;

#if HAVE_GLYCIN
    if (s_backend != ImageDecoderBackend::PIXBUF)
    {
        GError *err = NULL;
        GdkPixbuf *pb = GlycinDecodeBytesPixbuf(bytes, cancellable, &err);
        if (pb)
            return pb;
        if (err)
            g_error_free(err);
        if (s_backend == ImageDecoderBackend::GLYCIN)
            return NULL;
    }
#endif

    return PixbufDecodeBytesPixbuf(bytes, mimetype, cancellable, error);
}
#endif

GdkTexture* ImageDecoder::DecodeBytesTexture(GBytes *bytes, const char *mimetype,
                                            GCancellable *cancellable, GError **error)
{
    (void)mimetype;
    (void)cancellable;
    if (!bytes)
        return NULL;

#if HAVE_GLYCIN
    if (s_backend != ImageDecoderBackend::PIXBUF)
    {
        GError *err = NULL;
        GdkTexture *tex = GlycinDecodeBytesTexture(bytes, cancellable, &err);
        if (tex)
            return tex;
        if (err)
            g_error_free(err);
        if (s_backend == ImageDecoderBackend::GLYCIN)
            return NULL;
    }
#endif

#if HAVE_GDK_PIXBUF
    GdkPixbuf *pb = DecodeBytesPixbuf(bytes, mimetype, cancellable, error);
    if (pb)
    {
        GdkTexture *tex = QuiverUtils::PixbufToTexture(pb);
        g_object_unref(pb);
        return tex;
    }
#endif

    GError *err = NULL;
    GdkTexture *tex = gdk_texture_new_from_bytes(bytes, &err);
    if (tex)
        return tex;
    if (err)
    {
        if (error)
            *error = err;
        else
            g_error_free(err);
    }
    return NULL;
}

#if HAVE_GLYCIN && HAVE_GDK_PIXBUF
GdkPixbuf* ImageDecoder::GlycinDecodeBytesPixbuf(GBytes *bytes, GCancellable *cancellable, GError **error)
{
    DecodeSlotGuard slot;

    GlyLoader *loader = gly_loader_new_for_bytes(bytes);
    if (!loader)
        return NULL;

    gly_loader_set_apply_transformations(loader, FALSE);

    GlyImage *image = gly_loader_load(loader, error);
    if (!image)
    {
        g_object_unref(loader);
        return NULL;
    }

    (void)cancellable;
    GlyFrame *frame = gly_image_next_frame(image, error);
    GdkPixbuf *pb = NULL;
    if (frame)
    {
        pb = frame_to_pixbuf(frame);
        g_object_unref(frame);
    }

    g_object_unref(image);
    g_object_unref(loader);
    return pb;
}
#endif

#if HAVE_GLYCIN
GdkTexture* ImageDecoder::GlycinDecodeBytesTexture(GBytes *bytes, GCancellable *cancellable, GError **error)
{
    DecodeSlotGuard slot;

    GlyLoader *loader = gly_loader_new_for_bytes(bytes);
    if (!loader)
        return NULL;

    gly_loader_set_apply_transformations(loader, TRUE);

    GlyImage *image = gly_loader_load(loader, error);
    if (!image)
    {
        g_object_unref(loader);
        return NULL;
    }

    (void)cancellable;
    GlyFrame *frame = gly_image_next_frame(image, error);
    GdkTexture *tex = NULL;
    if (frame)
    {
        tex = frame_to_texture(frame);
        g_object_unref(frame);
    }

    g_object_unref(image);
    g_object_unref(loader);
    return tex;
}
#endif

#if HAVE_GDK_PIXBUF
GdkPixbuf* ImageDecoder::PixbufDecodeBytesPixbuf(GBytes *bytes, const char *mimetype,
                                                GCancellable *cancellable, GError **error)
{
    (void)cancellable;
    GdkPixbufLoader *loader = NULL;
    if (mimetype && strlen(mimetype) > 0)
        loader = gdk_pixbuf_loader_new_with_mime_type(mimetype, NULL);
    if (!loader)
        loader = gdk_pixbuf_loader_new();

    if (!loader)
        return NULL;

    gsize size = 0;
    gconstpointer data = g_bytes_get_data(bytes, &size);
    if (data && size > 0)
    {
        gdk_pixbuf_loader_write(loader, (const guchar*)data, size, error);
    }
    gdk_pixbuf_loader_close(loader, error);

    GdkPixbuf *pb = gdk_pixbuf_loader_get_pixbuf(loader);
    if (pb)
        g_object_ref(pb);

    g_object_unref(loader);
    return pb;
}
#endif

// -------------------------------------------------------------------------
// FreeDesktop Thumbnail Saving
// -------------------------------------------------------------------------

static void write_be32(std::vector<uint8_t> &out, uint32_t val)
{
    out.push_back((val >> 24) & 0xFF);
    out.push_back((val >> 16) & 0xFF);
    out.push_back((val >> 8) & 0xFF);
    out.push_back(val & 0xFF);
}

static void append_text_chunk(std::vector<uint8_t> &out, const char *key, const char *value)
{
    if (!key || !value) return;
    size_t key_len = strlen(key);
    size_t val_len = strlen(value);
    uint32_t data_len = (uint32_t)(key_len + 1 + val_len);

    write_be32(out, data_len);

    size_t crc_start = out.size();
    out.push_back('t');
    out.push_back('E');
    out.push_back('X');
    out.push_back('t');

    out.insert(out.end(), key, key + key_len);
    out.push_back('\0');
    out.insert(out.end(), value, value + val_len);

    uLong crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, out.data() + crc_start, data_len + 4);
    write_be32(out, (uint32_t)crc);
}

bool ImageDecoder::TextureSaveThumbnail(GdkTexture *texture, const char *dest_path,
                                        const char *uri, time_t mtime, gint64 file_size,
                                        int orig_w, int orig_h, int orientation)
{
    if (!texture || !dest_path)
        return false;

    GBytes *png_bytes = gdk_texture_save_to_png_bytes(texture);
    if (!png_bytes)
        return false;

    gsize raw_png_size = 0;
    const uint8_t *raw_png = (const uint8_t*)g_bytes_get_data(png_bytes, &raw_png_size);
    if (!raw_png || raw_png_size < 33)
    {
        g_bytes_unref(png_bytes);
        return false;
    }

    gchar *thumb_dir = g_path_get_dirname(dest_path);
    g_mkdir_with_parents(thumb_dir, S_IRUSR | S_IWUSR | S_IXUSR);
    g_free(thumb_dir);

    gchar *temp_file_name = g_strconcat(dest_path, ".XXXXXX", NULL);
    gint fhandle = g_mkstemp(temp_file_name);
    if (fhandle == -1)
    {
        g_free(temp_file_name);
        g_bytes_unref(png_bytes);
        return false;
    }

    const size_t ihdr_end = 8 + 4 + 4 + 13 + 4; // 33 bytes
    std::vector<uint8_t> enriched_png;
    enriched_png.reserve(raw_png_size + 512);
    enriched_png.insert(enriched_png.end(), raw_png, raw_png + ihdr_end);

    char str_mtime[32];
    char str_size[32];
    char str_width[32];
    char str_height[32];
    char str_orientation[4];

    g_snprintf(str_mtime, sizeof(str_mtime), "%lu", (unsigned long)mtime);
    g_snprintf(str_size, sizeof(str_size), "%" G_GINT64_FORMAT, file_size);
    g_snprintf(str_width, sizeof(str_width), "%d", orig_w);
    g_snprintf(str_height, sizeof(str_height), "%d", orig_h);
    g_snprintf(str_orientation, sizeof(str_orientation), "%d", orientation);

    append_text_chunk(enriched_png, "Thumb::URI", uri);
    append_text_chunk(enriched_png, "Thumb::MTime", str_mtime);
    append_text_chunk(enriched_png, "Thumb::Size", str_size);
    append_text_chunk(enriched_png, "Thumb::Image::Width", str_width);
    append_text_chunk(enriched_png, "Thumb::Image::Height", str_height);
    append_text_chunk(enriched_png, "Thumb::Image::Orientation", str_orientation);
    append_text_chunk(enriched_png, "Software", PACKAGE_STRING);

    enriched_png.insert(enriched_png.end(), raw_png + ihdr_end, raw_png + raw_png_size);
    g_bytes_unref(png_bytes);

    ssize_t bytes_written = write(fhandle, enriched_png.data(), enriched_png.size());
    bool written = (bytes_written == (ssize_t)enriched_png.size());
    close(fhandle);

    if (written)
    {
        g_chmod(temp_file_name, 0600);
        g_rename(temp_file_name, dest_path);
    }
    else
    {
        unlink(temp_file_name);
    }

    g_free(temp_file_name);
    return written;
}

bool ImageDecoder::SaveThumbnail(GdkTexture *texture, const char *dest_path,
                                const char *uri, time_t mtime, gint64 file_size,
                                int orig_w, int orig_h, int orientation)
{
    return TextureSaveThumbnail(texture, dest_path, uri, mtime, file_size, orig_w, orig_h, orientation);
}

#if HAVE_GDK_PIXBUF
bool ImageDecoder::SaveThumbnail(GdkPixbuf *pixbuf, const char *dest_path,
                                const char *uri, time_t mtime, gint64 file_size,
                                int orig_w, int orig_h, int orientation)
{
    if (!pixbuf || !dest_path)
        return false;

#if HAVE_GLYCIN
    if (s_backend != ImageDecoderBackend::PIXBUF)
    {
        if (GlycinSaveThumbnail(pixbuf, dest_path, uri, mtime, file_size, orig_w, orig_h, orientation))
            return true;
        if (s_backend == ImageDecoderBackend::GLYCIN)
            return false;
    }
#endif

    return PixbufSaveThumbnail(pixbuf, dest_path, uri, mtime, file_size, orig_w, orig_h, orientation);
}

#if HAVE_GLYCIN
bool ImageDecoder::GlycinSaveThumbnail(GdkPixbuf *pixbuf, const char *dest_path,
                                      const char *uri, time_t mtime, gint64 file_size,
                                      int orig_w, int orig_h, int orientation)
{
    gchar *thumb_dir = g_path_get_dirname(dest_path);
    g_mkdir_with_parents(thumb_dir, S_IRUSR | S_IWUSR | S_IXUSR);
    g_free(thumb_dir);

    gchar *temp_file_name = g_strconcat(dest_path, ".XXXXXX", NULL);
    gint fhandle = g_mkstemp(temp_file_name);
    if (fhandle == -1)
    {
        g_free(temp_file_name);
        return false;
    }

    GError *error = NULL;
    GlyCreator *creator = gly_creator_new("image/png", &error);
    if (!creator)
    {
        close(fhandle);
        unlink(temp_file_name);
        g_free(temp_file_name);
        if (error) g_error_free(error);
        return false;
    }

    int w = gdk_pixbuf_get_width(pixbuf);
    int h = gdk_pixbuf_get_height(pixbuf);
    int stride = gdk_pixbuf_get_rowstride(pixbuf);
    gboolean has_alpha = gdk_pixbuf_get_has_alpha(pixbuf);
    GlyMemoryFormat fmt = has_alpha ? GLY_MEMORY_R8G8B8A8 : GLY_MEMORY_R8G8B8;
    guchar *pixels = gdk_pixbuf_get_pixels(pixbuf);
    GBytes *texture_bytes = g_bytes_new(pixels, (gsize)stride * h);

    GlyNewFrame *new_frame = gly_creator_add_frame_with_stride(creator, w, h, stride, fmt, texture_bytes, &error);
    g_bytes_unref(texture_bytes);

    if (!new_frame)
    {
        close(fhandle);
        unlink(temp_file_name);
        g_free(temp_file_name);
        g_object_unref(creator);
        if (error) g_error_free(error);
        return false;
    }

    char str_mtime[32];
    char str_size[32];
    char str_width[32];
    char str_height[32];
    char str_orientation[4];

    g_snprintf(str_mtime, sizeof(str_mtime), "%lu", (unsigned long)mtime);
    g_snprintf(str_size, sizeof(str_size), "%" G_GINT64_FORMAT, file_size);
    g_snprintf(str_width, sizeof(str_width), "%d", orig_w);
    g_snprintf(str_height, sizeof(str_height), "%d", orig_h);
    g_snprintf(str_orientation, sizeof(str_orientation), "%d", orientation);

    gly_creator_add_metadata_key_value(creator, "Thumb::URI", uri);
    gly_creator_add_metadata_key_value(creator, "Thumb::MTime", str_mtime);
    gly_creator_add_metadata_key_value(creator, "Thumb::Size", str_size);
    gly_creator_add_metadata_key_value(creator, "Thumb::Image::Width", str_width);
    gly_creator_add_metadata_key_value(creator, "Thumb::Image::Height", str_height);
    gly_creator_add_metadata_key_value(creator, "Thumb::Image::Orientation", str_orientation);
    gly_creator_add_metadata_key_value(creator, "Software", PACKAGE_STRING);

    GlyEncodedImage *encoded = gly_creator_create(creator, &error);
    if (!encoded)
    {
        close(fhandle);
        unlink(temp_file_name);
        g_free(temp_file_name);
        g_object_unref(creator);
        if (error) g_error_free(error);
        return false;
    }

    GBytes *png_data = gly_encoded_image_get_data(encoded);
    gsize png_size = 0;
    gconstpointer png_ptr = g_bytes_get_data(png_data, &png_size);

    bool written = false;
    if (png_ptr && png_size > 0)
    {
        ssize_t bytes_written = write(fhandle, png_ptr, png_size);
        written = (bytes_written == (ssize_t)png_size);
    }
    close(fhandle);

    g_bytes_unref(png_data);
    g_object_unref(encoded);
    g_object_unref(creator);

    if (written)
    {
        g_chmod(temp_file_name, 0600);
        g_rename(temp_file_name, dest_path);
    }
    else
    {
        unlink(temp_file_name);
    }

    g_free(temp_file_name);
    return written;
}
#endif

#if HAVE_GDK_PIXBUF
bool ImageDecoder::PixbufSaveThumbnail(GdkPixbuf *pixbuf, const char *dest_path,
                                      const char *uri, time_t mtime, gint64 file_size,
                                      int orig_w, int orig_h, int orientation)
{
    gchar *thumb_dir = g_path_get_dirname(dest_path);
    g_mkdir_with_parents(thumb_dir, S_IRUSR | S_IWUSR | S_IXUSR);
    g_free(thumb_dir);

    gchar *temp_file_name = g_strconcat(dest_path, ".XXXXXX", NULL);
    gint fhandle = g_mkstemp(temp_file_name);
    if (fhandle == -1)
    {
        g_free(temp_file_name);
        return false;
    }
    close(fhandle);

    gchar str_mtime[32];
    gchar str_size[32];
    gchar str_width[32];
    gchar str_height[32];
    gchar str_orientation[4];

    g_snprintf(str_mtime, sizeof(str_mtime), "%lu", (unsigned long)mtime);
    g_snprintf(str_size, sizeof(str_size), "%" G_GINT64_FORMAT, file_size);
    g_snprintf(str_width, sizeof(str_width), "%d", orig_w);
    g_snprintf(str_height, sizeof(str_height), "%d", orig_h);
    g_snprintf(str_orientation, sizeof(str_orientation), "%d", orientation);

    gboolean saved = gdk_pixbuf_save(pixbuf,
                                     temp_file_name,
                                     "png", NULL,
                                     "tEXt::Thumb::URI", uri,
                                     "tEXt::Thumb::MTime", str_mtime,
                                     "tEXt::Thumb::Size", str_size,
                                     "tEXt::Thumb::Image::Orientation", str_orientation,
                                     "tEXt::Thumb::Image::Width", str_width,
                                     "tEXt::Thumb::Image::Height", str_height,
                                     "tEXt::Software", PACKAGE_STRING,
                                     NULL);
    if (saved)
    {
        g_chmod(temp_file_name, 0600);
        g_rename(temp_file_name, dest_path);
    }
    else
    {
        unlink(temp_file_name);
    }

    g_free(temp_file_name);
    return saved;
}
#endif

GdkPixbuf* ImageDecoder::DecodeVideoPreview(const gchar *uri,
                                            gint *aspect_n,
                                            gint *aspect_d,
                                            gint64 position_ns,
                                            gint target_width,
                                            gint target_height,
                                            QuiverVideoOps::VideoAbortFn abort_fn,
                                            gpointer abort_data,
                                            gint *natural_width,
                                            gint *natural_height)
{
    return QuiverVideoOps::LoadPixbuf(uri, aspect_n, aspect_d, position_ns,
                                      target_width, target_height,
                                      abort_fn, abort_data,
                                      natural_width, natural_height);
}
#endif

GdkTexture* ImageDecoder::DecodeVideoTexture(const gchar *uri,
                                             gint *aspect_n,
                                             gint *aspect_d,
                                             gint64 position_ns,
                                             gint target_width,
                                             gint target_height,
                                             QuiverVideoOps::VideoAbortFn abort_fn,
                                             gpointer abort_data,
                                             gint *natural_width,
                                             gint *natural_height)
{
    return QuiverVideoOps::LoadTexture(uri, aspect_n, aspect_d, position_ns,
                                       target_width, target_height,
                                       abort_fn, abort_data,
                                       natural_width, natural_height);
}



