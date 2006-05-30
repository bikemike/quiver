#ifndef FILE_QUIVER_PIXBUF_UTILS_H
#define FILE_QUIVER_PIXBUF_UTILS_H

void pixbuf_set_alpha(const GdkPixbuf *src, guchar alpha);
void pixbuf_set_grayscale(const GdkPixbuf *src);
void pixbuf_brighten(const GdkPixbuf *src, GdkPixbuf *dest, gint amount);


#endif
