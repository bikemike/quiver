#ifndef QUIVER_ROTATED_PAINTABLE_H
#define QUIVER_ROTATED_PAINTABLE_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define QUIVER_TYPE_ROTATED_PAINTABLE (quiver_rotated_paintable_get_type())
G_DECLARE_FINAL_TYPE(QuiverRotatedPaintable, quiver_rotated_paintable, QUIVER, ROTATED_PAINTABLE, GObject)

QuiverRotatedPaintable *quiver_rotated_paintable_new(GdkPaintable *underlying);
void quiver_rotated_paintable_set_underlying(QuiverRotatedPaintable *paintable, GdkPaintable *underlying);
GdkPaintable *quiver_rotated_paintable_get_underlying(QuiverRotatedPaintable *paintable);
void quiver_rotated_paintable_set_rotation(QuiverRotatedPaintable *paintable, int rotation_degrees);
int quiver_rotated_paintable_get_rotation(QuiverRotatedPaintable *paintable);

G_END_DECLS

#endif // QUIVER_ROTATED_PAINTABLE_H
