#include "BookmarkRow.h"

struct _BookmarkRow
{
    GObject parent_instance;
    gint id;
    gchar *icon_name;
    gchar *name;
};

enum {
    PROP_0,
    PROP_ID,
    PROP_ICON_NAME,
    PROP_NAME,
    N_PROPERTIES
};

static GParamSpec *obj_properties[N_PROPERTIES] = { NULL, };

G_DEFINE_TYPE(BookmarkRow, bookmark_row, G_TYPE_OBJECT)

static void
bookmark_row_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
    BookmarkRow *self = BOOKMARK_ROW(object);

    switch (property_id)
    {
    case PROP_ID:
        self->id = g_value_get_int(value);
        break;
    case PROP_ICON_NAME:
        g_free(self->icon_name);
        self->icon_name = g_value_dup_string(value);
        break;
    case PROP_NAME:
        g_free(self->name);
        self->name = g_value_dup_string(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static void
bookmark_row_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
    BookmarkRow *self = BOOKMARK_ROW(object);

    switch (property_id)
    {
    case PROP_ID:
        g_value_set_int(value, self->id);
        break;
    case PROP_ICON_NAME:
        g_value_set_string(value, self->icon_name);
        break;
    case PROP_NAME:
        g_value_set_string(value, self->name);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static void
bookmark_row_finalize(GObject *object)
{
    BookmarkRow *self = BOOKMARK_ROW(object);
    g_free(self->icon_name);
    g_free(self->name);
    G_OBJECT_CLASS(bookmark_row_parent_class)->finalize(object);
}

static void
bookmark_row_class_init(BookmarkRowClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->set_property = bookmark_row_set_property;
    object_class->get_property = bookmark_row_get_property;
    object_class->finalize = bookmark_row_finalize;

    obj_properties[PROP_ID] = g_param_spec_int("id", "ID", "Bookmark ID", G_MININT, G_MAXINT, 0, G_PARAM_READWRITE);
    obj_properties[PROP_ICON_NAME] = g_param_spec_string("icon-name", "Icon Name", "Bookmark Icon Name", NULL, G_PARAM_READWRITE);
    obj_properties[PROP_NAME] = g_param_spec_string("name", "Name", "Bookmark Name", NULL, G_PARAM_READWRITE);

    g_object_class_install_properties(object_class, N_PROPERTIES, obj_properties);
}

static void
bookmark_row_init(BookmarkRow *self)
{
}

BookmarkRow *
bookmark_row_new(gint id, const gchar *icon_name, const gchar *name)
{
    return BOOKMARK_ROW(g_object_new(BOOKMARK_TYPE_ROW, "id", id, "icon-name", icon_name, "name", name, NULL));
}

gint bookmark_row_get_id(BookmarkRow *self)
{
    g_return_val_if_fail(BOOKMARK_IS_ROW(self), 0);
    return self->id;
}

const gchar* bookmark_row_get_icon_name(BookmarkRow *self)
{
    g_return_val_if_fail(BOOKMARK_IS_ROW(self), NULL);
    return self->icon_name;
}

const gchar* bookmark_row_get_name(BookmarkRow *self)
{
    g_return_val_if_fail(BOOKMARK_IS_ROW(self), NULL);
    return self->name;
}

void bookmark_row_set_name(BookmarkRow *self, const gchar *name)
{
    g_return_if_fail(BOOKMARK_IS_ROW(self));
    g_object_set(G_OBJECT(self), "name", name, NULL);
}
