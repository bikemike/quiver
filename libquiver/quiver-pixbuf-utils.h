#ifndef FILE_QUIVER_PIXBUF_UTILS_H
#define FILE_QUIVER_PIXBUF_UTILS_H

#if HAVE_GDK_PIXBUF
#include <gdk-pixbuf/gdk-pixbuf.h>
#endif
#include <gdk/gdk.h>

G_BEGIN_DECLS
void quiver_rect_get_bound_size(guint bound_width,guint bound_height,guint *width,guint *height,gboolean fill_if_smaller);

#if HAVE_GDK_PIXBUF
void pixbuf_set_alpha(GdkPixbuf *src, guchar alpha);
void pixbuf_set_grayscale(GdkPixbuf *src);
void pixbuf_brighten(const GdkPixbuf *src, GdkPixbuf *dest, gint amount);
GdkTexture *quiver_pixbuf_to_texture(GdkPixbuf *pb);
#endif

G_END_DECLS
#endif
