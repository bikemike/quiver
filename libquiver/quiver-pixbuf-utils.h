#ifndef FILE_QUIVER_PIXBUF_UTILS_H
#define FILE_QUIVER_PIXBUF_UTILS_H
G_BEGIN_DECLS
void pixbuf_set_alpha(GdkPixbuf *src, guchar alpha);
void pixbuf_set_grayscale(GdkPixbuf *src);
void pixbuf_brighten(const GdkPixbuf *src, GdkPixbuf *dest, gint amount);
void quiver_rect_get_bound_size(guint bound_width,guint bound_height,guint *width,guint *height,gboolean fill_if_smaller);

G_END_DECLS
#endif
