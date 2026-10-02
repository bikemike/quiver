#ifndef FILE_IMAGEDECODER_H
#define FILE_IMAGEDECODER_H

#include <config.h>
#include <gtk/gtk.h>
#include <gio/gio.h>
#include <gdk/gdk.h>
#if HAVE_GLYCIN
#include <glycin.h>
#endif
#include <string>
#include "QuiverVideoOps.h"

enum class ImageDecoderBackend {
    AUTO,
    GLYCIN,
    PIXBUF
};

class ImageDecoder {
public:
    using Backend = ImageDecoderBackend;

    static void SetBackend(ImageDecoderBackend backend);
    static ImageDecoderBackend GetBackend();
    static bool IsBackendSupported(ImageDecoderBackend backend);

    // Probe image dimensions (header only, fast short-circuit)
    /* `known_size` is the file's size in bytes when the caller already knows it,
     * or a negative value when it does not.  It is passed in rather than looked
     * up: see the note on is_size_too_large() in ImageDecoder.cpp for why this
     * must not become a g_file_query_info() on the caller's GFile. */
    static bool GetDimensions(GFile *file, const char *mimetype, int *width, int *height, goffset known_size = -1);

    // Probe JPEG dimensions by walking the marker chain, skipping the payload
    // of each segment rather than buffering it.
    //
    // A fixed-size prefix read is not enough for every JPEG.  A phone export
    // can carry megabytes of XMP across dozens of APP1 segments before the
    // frame header, so a prefix that stops at 256 KB never reaches SOF and the
    // caller falls back to a full decode just to read two integers.  Here the
    // only bytes actually read are the markers and length fields, so the
    // amount of metadata ahead of the frame header stops mattering.
    //
    // Returns false for anything that is not a JPEG with a readable frame
    // header, leaving the caller to its usual fallbacks.
    static bool ProbeJpegDimensions(GFile *file, int *width, int *height);

    /* `known_size` is the file's size in bytes when the caller already has it,
     * or a negative value when it does not.  These decode on the icon view's
     * thumbnail worker thread, so the size is passed in rather than looked up:
     * a g_file_query_info() there crashed inside libgvfs for trash:// URIs.
     * See is_size_too_large() in ImageDecoder.cpp. */

#if HAVE_GDK_PIXBUF
    // Decode full image from file to GdkPixbuf
    static GdkPixbuf* DecodeFilePixbuf(GFile *file, const char *mimetype,
                                       int max_width = 0, int max_height = 0,
                                       GCancellable *cancellable = NULL,
                                       GError **error = NULL,
                                       goffset known_size = -1);
#endif

    /* Decode full image from file to GdkTexture.
     *
     * The result is NOT necessarily in EXIF orientation: gdk-pixbuf and glycin
     * apply it when they can, but each silently drops an Orientation tag they
     * cannot parse, so the same call returns oriented or unrotated pixels
     * depending on the file.  Callers that display the texture should pass it
     * through EnsureExifOrientation() to get a defined state. */
    static GdkTexture* DecodeFileTexture(GFile *file, const char *mimetype,
                                         GCancellable *cancellable = NULL,
                                         GError **error = NULL,
                                         goffset known_size = -1);

    /* Make sure @texture is in @orientation, taking ownership of it.
     *
     * Both gdk-pixbuf and glycin parse EXIF orientation with their own reader,
     * and both silently drop the tag when it is not the SHORT the spec asks
     * for - the decode then comes back unrotated while still looking like a
     * success.  Exiv2, which the rest of the app uses for geometry, accepts any
     * integer type, so such a file ends up with an Exif-derived portrait frame
     * wrapped around landscape pixels.
     *
     * Rather than believe a loader's "I transformed this" flag, which is set
     * whenever a texture was produced, compare the texture against the
     * dimensions @orientation implies for @stored_width x @stored_height and
     * rotate when they disagree.  Self-checking, so a texture the loader did
     * orient is left alone and cannot be rotated twice, and it behaves the
     * same for every backend.
     *
     * @stored_width / @stored_height are the unrotated pixel dimensions; pass
     * 0 if unknown, in which case the texture is returned untouched.
     *
     * Dimensions cannot distinguish a mirror (orientations 2 and 4), which
     * leaves the frame unchanged; a dropped mirror is therefore not corrected
     * here.  The transpose family (5-8), the one that actually breaks the
     * layout, is. */
    static GdkTexture* EnsureExifOrientation(GdkTexture *texture, int orientation,
                                             int stored_width, int stored_height);

#if HAVE_GLYCIN
    /* Decode an animated image through glycin into backend-neutral frame
     * textures.  Returns the first-frame texture (transfer full) on success
     * and hands back *frames / *delays_ms (each texture owns one reference);
     * the caller must release them with quiver_animation_frames_free().
     * *n_frames = 1 for a still image; returns NULL on failure.
     *
     * As with DecodeFileTexture, the frames are not guaranteed to be in EXIF
     * orientation. */
    static GdkTexture* DecodeFileAnimation(GFile *file, GdkTexture ***frames,
                                           gint **delays_ms, gsize *n_frames,
                                           GError **error);
#endif

#if HAVE_GDK_PIXBUF
    // Decode image from memory bytes (e.g. EXIF embedded thumbnail) to GdkPixbuf
    static GdkPixbuf* DecodeBytesPixbuf(GBytes *bytes, const char *mimetype = NULL,
                                        GCancellable *cancellable = NULL,
                                        GError **error = NULL);
#endif

    // Decode image from memory bytes to GdkTexture
    static GdkTexture* DecodeBytesTexture(GBytes *bytes, const char *mimetype = NULL,
                                          GCancellable *cancellable = NULL,
                                          GError **error = NULL);

#if HAVE_GDK_PIXBUF
    // FreeDesktop Thumbnail saving
    static bool SaveThumbnail(GdkPixbuf *pixbuf, const char *dest_path,
                             const char *uri, time_t mtime, gint64 file_size,
                             int orig_w, int orig_h, int orientation);
#endif

    static bool SaveThumbnail(GdkTexture *texture, const char *dest_path,
                             const char *uri, time_t mtime, gint64 file_size,
                             int orig_w, int orig_h, int orientation);

    static bool TextureSaveThumbnail(GdkTexture *texture, const char *dest_path,
                                    const char *uri, time_t mtime, gint64 file_size,
                                    int orig_w, int orig_h, int orientation);

#if HAVE_GDK_PIXBUF
    // Decode still-frame preview from video container (demux + frame decode + colorspace conversion)
    static GdkPixbuf* DecodeVideoPreview(const gchar *uri,
                                         gint *aspect_n = NULL,
                                         gint *aspect_d = NULL,
                                         gint64 position_ns = -1,
                                         gint target_width = 0,
                                         gint target_height = 0,
                                         QuiverVideoOps::VideoAbortFn abort_fn = NULL,
                                         gpointer abort_data = NULL,
                                         gint *natural_width = NULL,
                                         gint *natural_height = NULL);
#endif

    // Decode still-frame preview from video container as modern GdkTexture
    static GdkTexture* DecodeVideoTexture(const gchar *uri,
                                          gint *aspect_n = NULL,
                                          gint *aspect_d = NULL,
                                          gint64 position_ns = -1,
                                          gint target_width = 0,
                                          gint target_height = 0,
                                          QuiverVideoOps::VideoAbortFn abort_fn = NULL,
                                          gpointer abort_data = NULL,
                                          gint *natural_width = NULL,
                                          gint *natural_height = NULL);

private:
    static ImageDecoderBackend s_backend;

#if HAVE_GLYCIN
    // Glycin implementations
    static bool GlycinGetDimensions(GFile *file, int *width, int *height);
#if HAVE_GDK_PIXBUF
    static GdkPixbuf* GlycinDecodeFilePixbuf(GFile *file, GCancellable *cancellable, GError **error);
#endif
    static GdkTexture* GlycinDecodeFileTexture(GFile *file, GCancellable *cancellable, GError **error);
#if HAVE_GDK_PIXBUF
    static GdkPixbuf* GlycinDecodeBytesPixbuf(GBytes *bytes, GCancellable *cancellable, GError **error);
    static bool GlycinSaveThumbnail(GdkPixbuf *pixbuf, const char *dest_path,
                                   const char *uri, time_t mtime, gint64 file_size,
                                   int orig_w, int orig_h, int orientation);
#endif
    static GdkTexture* GlycinDecodeBytesTexture(GBytes *bytes, GCancellable *cancellable, GError **error);
#endif

#if HAVE_GDK_PIXBUF
    // GdkPixbuf implementations
    static bool PixbufGetDimensions(GFile *file, const char *mimetype, int *width, int *height);
    static GdkPixbuf* PixbufDecodeFilePixbuf(GFile *file, const char *mimetype,
                                             int max_width, int max_height,
                                             GCancellable *cancellable, GError **error);
    static GdkPixbuf* PixbufDecodeBytesPixbuf(GBytes *bytes, const char *mimetype,
                                              GCancellable *cancellable, GError **error);
    static bool PixbufSaveThumbnail(GdkPixbuf *pixbuf, const char *dest_path,
                                   const char *uri, time_t mtime, gint64 file_size,
                                   int orig_w, int orig_h, int orientation);
#endif
};

#endif
