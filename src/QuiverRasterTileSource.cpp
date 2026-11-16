#include "QuiverRasterTileSource.h"

#include <gtk/gtk.h>
#include <libsoup/soup.h>

#include <cstring>

struct _QuiverRasterTileSource
{
	ShumateMapSource parent_instance;

	SoupSession* session;
	gchar* uri_format;
};

G_DEFINE_TYPE(QuiverRasterTileSource, quiver_raster_tile_source,
	SHUMATE_TYPE_MAP_SOURCE)

namespace
{
struct RasterRequest
{
	ShumateTile* tile;
	gchar* uri;
};

void DestroyRequest(gpointer data)
{
	RasterRequest* request = static_cast<RasterRequest*>(data);
	if (!request)
		return;
	if (request->tile)
		g_object_unref(request->tile);
	g_free(request->uri);
	g_free(request);
}

gchar* ReplacePlaceholder(const gchar* input, const gchar* token, guint value)
{
	GString* result = g_string_new(NULL);
	const gsize tokenLen = std::strlen(token);
	const gchar* cursor = input;
	while (true)
	{
		const gchar* match = g_strstr_len(cursor, -1, token);
		if (!match)
		{
			g_string_append(result, cursor);
			break;
		}
		g_string_append_len(result, cursor, match - cursor);
		g_string_append_printf(result, "%u", value);
		cursor = match + tokenLen;
	}
	return g_string_free(result, FALSE);
}

gchar* BuildUri(const gchar* format, guint zoom, guint x, guint y)
{
	gchar* withX = ReplacePlaceholder(format, "{x}", x);
	gchar* withY = ReplacePlaceholder(withX, "{y}", y);
	gchar* withZ = ReplacePlaceholder(withY, "{z}", zoom);
	g_free(withX);
	g_free(withY);
	return withZ;
}

void OnTileRead(GObject* sourceObject, GAsyncResult* result, gpointer userData)
{
	GTask* task = G_TASK(userData);
	SoupSession* session = SOUP_SESSION(sourceObject);
	RasterRequest* request = static_cast<RasterRequest*>(
		g_task_get_task_data(task));

	GError* error = NULL;
	GBytes* bytes = soup_session_send_and_read_finish(session, result, &error);

	if (!bytes)
	{
		if (request && request->tile)
			shumate_tile_set_state(request->tile, SHUMATE_STATE_DONE);
		g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_FAILED,
			"%s", error ? error->message : "Tile download failed");
		g_clear_error(&error);
		g_object_unref(task);
		return;
	}

	GdkTexture* texture = gdk_texture_new_from_bytes(bytes, &error);
	g_bytes_unref(bytes);

	if (!texture)
	{
		if (request && request->tile)
			shumate_tile_set_state(request->tile, SHUMATE_STATE_DONE);
		g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_FAILED,
			"%s", error ? error->message : "Could not decode tile");
		g_clear_error(&error);
		g_object_unref(task);
		return;
	}

	if (request && request->tile)
	{
		shumate_tile_set_paintable(request->tile, GDK_PAINTABLE(texture));
		shumate_tile_set_state(request->tile, SHUMATE_STATE_LOADED);
	}
	g_object_unref(texture);
	g_task_return_boolean(task, TRUE);
	g_object_unref(task);
}

void quiver_raster_tile_source_fill_tile_async(ShumateMapSource* source,
	ShumateTile* tile, GCancellable* cancellable, GAsyncReadyCallback callback,
	gpointer userData)
{
	QuiverRasterTileSource* self = QUIVER_RASTER_TILE_SOURCE(source);
	GTask* task = g_task_new(source, cancellable, callback, userData);

	if (!self->uri_format)
	{
		shumate_tile_set_state(tile, SHUMATE_STATE_DONE);
		g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_FAILED,
			"No tile URI format configured");
		g_object_unref(task);
		return;
	}

	if (g_cancellable_is_cancelled(cancellable))
	{
		shumate_tile_set_state(tile, SHUMATE_STATE_DONE);
		g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_CANCELLED,
			"Tile request cancelled");
		g_object_unref(task);
		return;
	}

	RasterRequest* request = g_new0(RasterRequest, 1);
	request->tile = SHUMATE_TILE(g_object_ref(tile));
	request->uri = BuildUri(self->uri_format,
		shumate_tile_get_zoom_level(tile),
		shumate_tile_get_x(tile),
		shumate_tile_get_y(tile));
	g_task_set_task_data(task, request, DestroyRequest);

	SoupMessage* message = soup_message_new("GET", request->uri);
	if (!message)
	{
		shumate_tile_set_state(tile, SHUMATE_STATE_DONE);
		g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
			"Invalid tile URI: %s", request->uri);
		g_object_unref(task);
		return;
	}

	soup_message_headers_replace(soup_message_get_request_headers(message),
		"User-Agent", "Quiver/0.3 (Libshumate)");
	soup_session_send_and_read_async(self->session, message,
		G_PRIORITY_DEFAULT, cancellable, OnTileRead, task);
	g_object_unref(message);
}

} // namespace

gboolean quiver_raster_tile_source_fill_tile_finish(ShumateMapSource*,
	GAsyncResult* result, GError** error)
{
	return g_task_propagate_boolean(G_TASK(result), error);
}

void quiver_raster_tile_source_finalize(GObject* object)
{
	QuiverRasterTileSource* self = QUIVER_RASTER_TILE_SOURCE(object);
	g_clear_object(&self->session);
	g_clear_pointer(&self->uri_format, g_free);
	G_OBJECT_CLASS(quiver_raster_tile_source_parent_class)->finalize(object);
}

void quiver_raster_tile_source_class_init(QuiverRasterTileSourceClass* klass)
{
	GObjectClass* objectClass = G_OBJECT_CLASS(klass);
	objectClass->finalize = quiver_raster_tile_source_finalize;

	ShumateMapSourceClass* sourceClass = SHUMATE_MAP_SOURCE_CLASS(klass);
	sourceClass->fill_tile_async = quiver_raster_tile_source_fill_tile_async;
	sourceClass->fill_tile_finish = quiver_raster_tile_source_fill_tile_finish;
}

void quiver_raster_tile_source_init(QuiverRasterTileSource* self)
{
	self->session = soup_session_new();
	shumate_map_source_set_projection(SHUMATE_MAP_SOURCE(self),
		SHUMATE_MAP_PROJECTION_MERCATOR);
	shumate_map_source_set_tile_size(SHUMATE_MAP_SOURCE(self), 256);
}

ShumateMapSource* quiver_raster_tile_source_new(const char* id,
	const char* name, const char* license, const char* uriFormat,
	guint minZoom, guint maxZoom)
{
	QuiverRasterTileSource* self = QUIVER_RASTER_TILE_SOURCE(
		g_object_new(QUIVER_TYPE_RASTER_TILE_SOURCE, NULL));
	self->uri_format = g_strdup(uriFormat ? uriFormat : "");
	ShumateMapSource* source = SHUMATE_MAP_SOURCE(self);
	shumate_map_source_set_id(source, id);
	shumate_map_source_set_name(source, name);
	shumate_map_source_set_license(source, license);
	shumate_map_source_set_min_zoom_level(source, minZoom);
	shumate_map_source_set_max_zoom_level(source, maxZoom);
	return source;
}
