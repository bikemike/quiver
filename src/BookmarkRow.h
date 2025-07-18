#ifndef BOOKMARK_ROW_H
#define BOOKMARK_ROW_H

#include <glib-object.h>

G_BEGIN_DECLS

#define BOOKMARK_TYPE_ROW (bookmark_row_get_type())
G_DECLARE_FINAL_TYPE(BookmarkRow, bookmark_row, BOOKMARK, ROW, GObject)

BookmarkRow *bookmark_row_new(gint id, const gchar *icon_name, const gchar *name);

gint bookmark_row_get_id(BookmarkRow *self);
const gchar* bookmark_row_get_icon_name(BookmarkRow *self);
const gchar* bookmark_row_get_name(BookmarkRow *self);
void bookmark_row_set_name(BookmarkRow *self, const gchar *name);


G_END_DECLS

#endif // BOOKMARK_ROW_H
