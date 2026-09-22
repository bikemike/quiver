#include "QuiverRotatedPaintable.h"

struct _QuiverRotatedPaintable
{
	GObject parent_instance;
	GdkPaintable *underlying;
	int rotation; // 0, 90, 180, 270
};

static void quiver_rotated_paintable_paintable_init(GdkPaintableInterface *iface);

G_DEFINE_TYPE_WITH_CODE(QuiverRotatedPaintable, quiver_rotated_paintable, G_TYPE_OBJECT,
	G_IMPLEMENT_INTERFACE(GDK_TYPE_PAINTABLE, quiver_rotated_paintable_paintable_init))

static void on_underlying_invalidate_contents(GdkPaintable *underlying, gpointer user_data)
{
	(void)underlying;
	gdk_paintable_invalidate_contents(GDK_PAINTABLE(user_data));
}

static void on_underlying_invalidate_size(GdkPaintable *underlying, gpointer user_data)
{
	(void)underlying;
	gdk_paintable_invalidate_size(GDK_PAINTABLE(user_data));
}

static void quiver_rotated_paintable_snapshot(GdkPaintable *paintable, GdkSnapshot *snapshot, double width, double height)
{
	QuiverRotatedPaintable *self = QUIVER_ROTATED_PAINTABLE(paintable);
	if (!self->underlying || width <= 0.0 || height <= 0.0)
		return;

	if (self->rotation == 0)
	{
		gdk_paintable_snapshot(self->underlying, snapshot, width, height);
	}
	else if (self->rotation == 90)
	{
		gtk_snapshot_save(snapshot);
		graphene_point_t p = GRAPHENE_POINT_INIT((float)width, 0.0f);
		gtk_snapshot_translate(snapshot, &p);
		gtk_snapshot_rotate(snapshot, 90.0f);
		gdk_paintable_snapshot(self->underlying, snapshot, height, width);
		gtk_snapshot_restore(snapshot);
	}
	else if (self->rotation == 180)
	{
		gtk_snapshot_save(snapshot);
		graphene_point_t p = GRAPHENE_POINT_INIT((float)width, (float)height);
		gtk_snapshot_translate(snapshot, &p);
		gtk_snapshot_rotate(snapshot, 180.0f);
		gdk_paintable_snapshot(self->underlying, snapshot, width, height);
		gtk_snapshot_restore(snapshot);
	}
	else if (self->rotation == 270)
	{
		gtk_snapshot_save(snapshot);
		graphene_point_t p = GRAPHENE_POINT_INIT(0.0f, (float)height);
		gtk_snapshot_translate(snapshot, &p);
		gtk_snapshot_rotate(snapshot, 270.0f);
		gdk_paintable_snapshot(self->underlying, snapshot, height, width);
		gtk_snapshot_restore(snapshot);
	}
}

static int quiver_rotated_paintable_get_intrinsic_width(GdkPaintable *paintable)
{
	QuiverRotatedPaintable *self = QUIVER_ROTATED_PAINTABLE(paintable);
	if (!self->underlying)
		return 0;
	if (self->rotation == 90 || self->rotation == 270)
		return gdk_paintable_get_intrinsic_height(self->underlying);
	return gdk_paintable_get_intrinsic_width(self->underlying);
}

static int quiver_rotated_paintable_get_intrinsic_height(GdkPaintable *paintable)
{
	QuiverRotatedPaintable *self = QUIVER_ROTATED_PAINTABLE(paintable);
	if (!self->underlying)
		return 0;
	if (self->rotation == 90 || self->rotation == 270)
		return gdk_paintable_get_intrinsic_width(self->underlying);
	return gdk_paintable_get_intrinsic_height(self->underlying);
}

static double quiver_rotated_paintable_get_intrinsic_aspect_ratio(GdkPaintable *paintable)
{
	QuiverRotatedPaintable *self = QUIVER_ROTATED_PAINTABLE(paintable);
	if (!self->underlying)
		return 0.0;
	double ar = gdk_paintable_get_intrinsic_aspect_ratio(self->underlying);
	if (ar > 0.0 && (self->rotation == 90 || self->rotation == 270))
		return 1.0 / ar;
	return ar;
}

static GdkPaintableFlags quiver_rotated_paintable_get_flags(GdkPaintable *paintable)
{
	(void)paintable;
	// Since rotation can change dynamically, the paintable is neither STATIC_SIZE nor STATIC_CONTENTS.
	return (GdkPaintableFlags)0;
}

static GdkPaintable *quiver_rotated_paintable_get_current_image(GdkPaintable *paintable)
{
	QuiverRotatedPaintable *self = QUIVER_ROTATED_PAINTABLE(paintable);
	if (!self->underlying)
		return NULL;
	GdkPaintable *cur = gdk_paintable_get_current_image(self->underlying);
	if (!cur)
		return NULL;
	QuiverRotatedPaintable *res = quiver_rotated_paintable_new(cur);
	quiver_rotated_paintable_set_rotation(res, self->rotation);
	g_object_unref(cur);
	return GDK_PAINTABLE(res);
}

static void quiver_rotated_paintable_dispose(GObject *object)
{
	QuiverRotatedPaintable *self = QUIVER_ROTATED_PAINTABLE(object);
	if (self->underlying)
	{
		g_signal_handlers_disconnect_by_data(self->underlying, self);
		g_clear_object(&self->underlying);
	}
	G_OBJECT_CLASS(quiver_rotated_paintable_parent_class)->dispose(object);
}

static void quiver_rotated_paintable_init(QuiverRotatedPaintable *self)
{
	self->underlying = NULL;
	self->rotation = 0;
}

static void quiver_rotated_paintable_class_init(QuiverRotatedPaintableClass *klass)
{
	G_OBJECT_CLASS(klass)->dispose = quiver_rotated_paintable_dispose;
}

static void quiver_rotated_paintable_paintable_init(GdkPaintableInterface *iface)
{
	iface->snapshot = quiver_rotated_paintable_snapshot;
	iface->get_intrinsic_width = quiver_rotated_paintable_get_intrinsic_width;
	iface->get_intrinsic_height = quiver_rotated_paintable_get_intrinsic_height;
	iface->get_intrinsic_aspect_ratio = quiver_rotated_paintable_get_intrinsic_aspect_ratio;
	iface->get_flags = quiver_rotated_paintable_get_flags;
	iface->get_current_image = quiver_rotated_paintable_get_current_image;
}

QuiverRotatedPaintable *quiver_rotated_paintable_new(GdkPaintable *underlying)
{
	QuiverRotatedPaintable *res = (QuiverRotatedPaintable *)g_object_new(QUIVER_TYPE_ROTATED_PAINTABLE, NULL);
	if (underlying)
	{
		res->underlying = GDK_PAINTABLE(g_object_ref(underlying));
		g_signal_connect(underlying, "invalidate-contents", G_CALLBACK(on_underlying_invalidate_contents), res);
		g_signal_connect(underlying, "invalidate-size", G_CALLBACK(on_underlying_invalidate_size), res);
	}
	return res;
}

void quiver_rotated_paintable_set_underlying(QuiverRotatedPaintable *paintable, GdkPaintable *underlying)
{
	g_return_if_fail(QUIVER_IS_ROTATED_PAINTABLE(paintable));
	if (paintable->underlying == underlying)
		return;

	if (paintable->underlying)
	{
		g_signal_handlers_disconnect_by_data(paintable->underlying, paintable);
		g_clear_object(&paintable->underlying);
	}

	if (underlying)
	{
		paintable->underlying = GDK_PAINTABLE(g_object_ref(underlying));
		g_signal_connect(underlying, "invalidate-contents", G_CALLBACK(on_underlying_invalidate_contents), paintable);
		g_signal_connect(underlying, "invalidate-size", G_CALLBACK(on_underlying_invalidate_size), paintable);
	}

	gdk_paintable_invalidate_size(GDK_PAINTABLE(paintable));
	gdk_paintable_invalidate_contents(GDK_PAINTABLE(paintable));
}

GdkPaintable *quiver_rotated_paintable_get_underlying(QuiverRotatedPaintable *paintable)
{
	g_return_val_if_fail(QUIVER_IS_ROTATED_PAINTABLE(paintable), NULL);
	return paintable->underlying;
}

void quiver_rotated_paintable_set_rotation(QuiverRotatedPaintable *paintable, int rotation_degrees)
{
	g_return_if_fail(QUIVER_IS_ROTATED_PAINTABLE(paintable));
	int rot = ((rotation_degrees % 360) + 360) % 360;
	rot = ((rot + 45) / 90 * 90) % 360;

	if (paintable->rotation != rot)
	{
		bool aspect_swapped = ((paintable->rotation % 180) != (rot % 180));
		paintable->rotation = rot;
		if (aspect_swapped)
		{
			gdk_paintable_invalidate_size(GDK_PAINTABLE(paintable));
		}
		gdk_paintable_invalidate_contents(GDK_PAINTABLE(paintable));
	}
}

int quiver_rotated_paintable_get_rotation(QuiverRotatedPaintable *paintable)
{
	g_return_val_if_fail(QUIVER_IS_ROTATED_PAINTABLE(paintable), 0);
	return paintable->rotation;
}
