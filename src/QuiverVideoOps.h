#ifndef QUIVER_VIDEO_OPS_H
#define QUIVER_VIDEO_OPS_H

#include <gdk-pixbuf/gdk-pixbuf.h>

typedef enum {
  GST_PLAY_FLAG_VIDEO         = (1 << 0),
  GST_PLAY_FLAG_AUDIO         = (1 << 1),
  GST_PLAY_FLAG_TEXT          = (1 << 2),
  GST_PLAY_FLAG_VIS           = (1 << 3),
  GST_PLAY_FLAG_SOFT_VOLUME   = (1 << 4),
  GST_PLAY_FLAG_NATIVE_AUDIO  = (1 << 5),
  GST_PLAY_FLAG_NATIVE_VIDEO  = (1 << 6),
  GST_PLAY_FLAG_DOWNLOAD      = (1 << 7),
  GST_PLAY_FLAG_BUFFERING     = (1 << 8),
  GST_PLAY_FLAG_DEINTERLACE   = (1 << 9)
} GstPlayFlags;

namespace QuiverVideoOps
{
	/* Called periodically while a frame is being decoded.  Return nonzero
	 * (TRUE) to abort the grab and return NULL as soon as possible.  Used so a
	 * slow video frame never holds up later requests when the user has
	 * navigated away. */
	typedef gboolean (*VideoAbortFn)(gpointer user_data);

	/* Grab a still frame from a video using libavformat/libavcodec.
	 *
	 * uri           - video file URI.
	 * pixel_aspect_ratio_numerator/denominator - if non-NULL, filled with the
	 *               pixel aspect ratio of the frame.
	 * position_ns   - if >= 0, seek to this timestamp and grab the frame there;
	 *               if < 0 (default), grab the first decodable frame (fastest).
	 * target_width/target_height - if both > 0, the returned pixbuf is scaled
	 *               down to fit within these bounds (aspect preserved).  0 means
	 *               natural (full) size.
	 * abort_fn/abort_data - optional cancellation predicate checked during
	 *               decode; return NULL early if it becomes true.
	 *
	 * Returns a newly-referenced GdkPixbuf, or NULL on failure/cancel.
	 */
	GdkPixbuf* LoadPixbuf(const gchar *uri,
		gint* pixel_aspect_ratio_numerator = NULL,
		gint* pixel_aspect_ratio_denominator = NULL,
		gint64 position_ns = -1,
		gint target_width = 0,
		gint target_height = 0,
		VideoAbortFn abort_fn = NULL,
		gpointer abort_data = NULL);

	/* Cheap metadata-only query (does not decode a frame).
	 * Fills duration_ns (may be NULL), width/height (may be NULL), and the
	 * pixel aspect ratio (may be NULL).  Returns TRUE on success. */
	gboolean Probe(const gchar *uri,
		gint64* duration_ns = NULL,
		gint* width = NULL,
		gint* height = NULL,
		gint* pixel_aspect_ratio_numerator = NULL,
		gint* pixel_aspect_ratio_denominator = NULL);
}

#endif
