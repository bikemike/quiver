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
    static bool GetDimensions(GFile *file, const char *mimetype, int *width, int *height);

#if HAVE_GDK_PIXBUF
    // Decode full image from file to GdkPixbuf
    static GdkPixbuf* DecodeFilePixbuf(GFile *file, const char *mimetype,
                                       int max_width = 0, int max_height = 0,
                                       GCancellable *cancellable = NULL,
                                       GError **error = NULL);
#endif

    // Decode full image from file to GdkTexture
    static GdkTexture* DecodeFileTexture(GFile *file, const char *mimetype,
                                         GCancellable *cancellable = NULL,
                                         GError **error = NULL);

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
                                         gpointer abort_data = NULL);
#endif

    // Decode still-frame preview from video container as modern GdkTexture
    static GdkTexture* DecodeVideoTexture(const gchar *uri,
                                          gint *aspect_n = NULL,
                                          gint *aspect_d = NULL,
                                          gint64 position_ns = -1,
                                          gint target_width = 0,
                                          gint target_height = 0,
                                          QuiverVideoOps::VideoAbortFn abort_fn = NULL,
                                          gpointer abort_data = NULL);

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
