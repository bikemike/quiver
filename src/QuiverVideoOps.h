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
	GdkPixbuf* LoadPixbuf(const gchar *uri, gint* pixel_aspect_ratio_numerator = NULL, gint* pixel_aspect_ratio_denominator = NULL);
}


#endif


