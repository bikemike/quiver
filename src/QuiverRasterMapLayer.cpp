#include "QuiverRasterMapLayer.h"

#include <algorithm>
#include <cmath>

struct QuiverRasterMapLayer;

namespace
{
struct TileKey
{
	int x;
	int y;
	int zoom;
};

struct TileEntry
{
	TileKey key;
	ShumateTile* current_tile;
	ShumateTile* loading_tile;
	GCancellable* cancellable;
	gulong paint_handler;
	gboolean failed;
};

struct FillData
{
	QuiverRasterMapLayer* layer;
	TileKey key;
	ShumateTile* tile;
};
}

struct QuiverRasterMapLayer
{
	ShumateLayer parent_instance;

	ShumateMapSource* source;
	GHashTable* tiles;
};

struct QuiverRasterMapLayerClass
{
	ShumateLayerClass parent_class;
};

G_DEFINE_TYPE(QuiverRasterMapLayer, quiver_raster_map_layer,
	SHUMATE_TYPE_LAYER)

#define QUIVER_TYPE_RASTER_MAP_LAYER (quiver_raster_map_layer_get_type())
#define QUIVER_RASTER_MAP_LAYER(object) \
	(G_TYPE_CHECK_INSTANCE_CAST((object), QUIVER_TYPE_RASTER_MAP_LAYER, \
		QuiverRasterMapLayer))

namespace
{
guint TileKeyHash(gconstpointer data)
{
	const TileKey* key = static_cast<const TileKey*>(data);
	const guint x = static_cast<guint>(key->x);
	const guint y = static_cast<guint>(key->y);
	const guint z = static_cast<guint>(key->zoom);
	return x * 73856093u ^ y * 19349663u ^ z * 83492791u;
}

gboolean TileKeyEqual(gconstpointer first, gconstpointer second)
{
	const TileKey* a = static_cast<const TileKey*>(first);
	const TileKey* b = static_cast<const TileKey*>(second);
	return a->x == b->x && a->y == b->y && a->zoom == b->zoom;
}

int PositiveMod(int value, int modulus)
{
	if (modulus <= 0)
		return 0;
	return (value % modulus + modulus) % modulus;
}

int EffectiveTileZoom(QuiverRasterMapLayer* self, double zoom)
{
	const int minZoom = static_cast<int>(
		shumate_map_source_get_min_zoom_level(self->source));
	const int maxZoom = static_cast<int>(
		shumate_map_source_get_max_zoom_level(self->source));
	const int tileZoom = static_cast<int>(std::floor(zoom));
	return std::clamp(tileZoom, minZoom, maxZoom);
}

void TileEntryFree(gpointer data)
{
	TileEntry* entry = static_cast<TileEntry*>(data);
	if (!entry)
		return;
	if (entry->loading_tile)
	{
		if (entry->paint_handler != 0)
			g_signal_handler_disconnect(entry->loading_tile,
				entry->paint_handler);
		if (entry->cancellable)
			g_cancellable_cancel(entry->cancellable);
		if (entry->cancellable)
			g_object_unref(entry->cancellable);
		g_object_unref(entry->loading_tile);
	}
	if (entry->current_tile)
		g_object_unref(entry->current_tile);
	g_free(entry);
}

void FillDataFree(FillData* data)
{
	if (!data)
		return;
	if (data->tile)
		g_object_unref(data->tile);
	if (data->layer)
		g_object_unref(data->layer);
	g_free(data);
}

void OnTilePaintable(GObject*, GParamSpec*, gpointer data)
{
	gtk_widget_queue_draw(GTK_WIDGET(data));
}

void PruneOldZoomTiles(QuiverRasterMapLayer* self);

void OnTileFilled(GObject* source, GAsyncResult* result, gpointer userData)
{
	FillData* data = static_cast<FillData*>(userData);
	GError* error = nullptr;
	const gboolean success = shumate_map_source_fill_tile_finish(
		SHUMATE_MAP_SOURCE(source), result, &error);

	if (!success && error &&
		g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
	{
		g_clear_error(&error);
		FillDataFree(data);
		return;
	}

	if (data->layer->tiles)
	{
		TileEntry* entry = static_cast<TileEntry*>(
			g_hash_table_lookup(data->layer->tiles, &data->key));
		if (entry && entry->loading_tile == data->tile)
		{
			if (success)
			{
				if (entry->current_tile)
					g_object_unref(entry->current_tile);
				entry->current_tile = SHUMATE_TILE(
					g_object_ref(data->tile));
				entry->failed = FALSE;
			}
			else
			{
				entry->failed = TRUE;
			}
			PruneOldZoomTiles(data->layer);
			gtk_widget_queue_draw(GTK_WIDGET(data->layer));
		}
	}

	g_clear_error(&error);
	FillDataFree(data);
}

void LoadTile(QuiverRasterMapLayer* self, TileEntry* entry)
{
	const guint zoom = static_cast<guint>(entry->key.zoom);
	const guint columns = shumate_map_source_get_column_count(
		self->source, zoom);
	if (columns == 0)
		return;

	const guint x = static_cast<guint>(PositiveMod(
		entry->key.x, static_cast<int>(columns)));
	const guint y = static_cast<guint>(entry->key.y);
	const guint size = shumate_map_source_get_tile_size(self->source);
	ShumateTile* tile = shumate_tile_new_full(x, y, size, zoom);
	shumate_tile_set_scale_factor(tile,
		gtk_widget_get_scale_factor(GTK_WIDGET(self)));

	if (entry->loading_tile)
	{
		if (entry->paint_handler != 0)
			g_signal_handler_disconnect(entry->loading_tile,
				entry->paint_handler);
		if (entry->cancellable)
			g_cancellable_cancel(entry->cancellable);
		if (entry->cancellable)
			g_object_unref(entry->cancellable);
		g_object_unref(entry->loading_tile);
	}

	entry->loading_tile = tile;
	entry->cancellable = g_cancellable_new();
	entry->paint_handler = g_signal_connect(tile, "notify::paintable",
		G_CALLBACK(OnTilePaintable), self);

	FillData* data = g_new0(FillData, 1);
	data->layer = QUIVER_RASTER_MAP_LAYER(g_object_ref(self));
	data->key = entry->key;
	data->tile = SHUMATE_TILE(g_object_ref(tile));

	shumate_map_source_fill_tile_async(self->source, tile,
		entry->cancellable, OnTileFilled, data);
}

void PruneOldZoomTiles(QuiverRasterMapLayer* self)
{
	if (!self->tiles || !self->source)
		return;

	ShumateViewport* viewport = shumate_layer_get_viewport(
		SHUMATE_LAYER(self));
	if (!viewport)
		return;

	const int tileZoom = EffectiveTileZoom(self,
		shumate_viewport_get_zoom_level(viewport));
	gboolean hasCurrent = FALSE;
	gboolean currentReady = TRUE;
	GHashTableIter iter;
	gpointer key = nullptr;
	gpointer value = nullptr;

	g_hash_table_iter_init(&iter, self->tiles);
	while (g_hash_table_iter_next(&iter, &key, &value))
	{
		TileEntry* entry = static_cast<TileEntry*>(value);
		if (entry->key.zoom != tileZoom)
			continue;
		hasCurrent = TRUE;
		if (!entry->current_tile)
		{
			currentReady = FALSE;
			break;
		}
	}

	if (!hasCurrent || !currentReady)
		return;

	g_hash_table_iter_init(&iter, self->tiles);
	while (g_hash_table_iter_next(&iter, &key, &value))
	{
		TileEntry* entry = static_cast<TileEntry*>(value);
		if (entry->key.zoom != tileZoom)
			g_hash_table_iter_remove(&iter);
	}
}

void RecomputeGrid(QuiverRasterMapLayer* self)
{
	if (!self->source || !self->tiles)
		return;

	GtkWidget* widget = GTK_WIDGET(self);
	ShumateViewport* viewport = shumate_layer_get_viewport(
		SHUMATE_LAYER(self));
	if (!viewport)
		return;

	const int width = gtk_widget_get_width(widget);
	const int height = gtk_widget_get_height(widget);
	if (width <= 0 || height <= 0)
		return;

	const int tileZoom = EffectiveTileZoom(self,
		shumate_viewport_get_zoom_level(viewport));
	const double centerX = shumate_map_source_get_x(self->source,
		tileZoom, shumate_location_get_longitude(
			SHUMATE_LOCATION(viewport)));
	const double centerY = shumate_map_source_get_y(self->source,
		tileZoom, shumate_location_get_latitude(
			SHUMATE_LOCATION(viewport)));
	const guint tileSize = shumate_map_source_get_tile_size(self->source);
	const guint rows = shumate_map_source_get_row_count(self->source,
		static_cast<guint>(tileZoom));
	if (rows == 0 || tileSize == 0)
		return;

	const double halfWidth = static_cast<double>(width) / 2.0;
	const double halfHeight = static_cast<double>(height) / 2.0;
	const double size = static_cast<double>(tileSize);

	long long firstColumn = static_cast<long long>(std::floor(
		(centerX - halfWidth) / size)) - 1;
	long long lastColumn = static_cast<long long>(std::ceil(
		(centerX + halfWidth) / size)) + 1;
	long long firstRow = static_cast<long long>(std::floor(
		(centerY - halfHeight) / size)) - 1;
	long long lastRow = static_cast<long long>(std::ceil(
		(centerY + halfHeight) / size)) + 1;

	firstRow = std::max(0LL, firstRow);
	lastRow = std::min(static_cast<long long>(rows) - 1, lastRow);

	if (firstRow > lastRow)
	{
		g_hash_table_remove_all(self->tiles);
		gtk_widget_queue_draw(widget);
		return;
	}

	GHashTableIter iter;
	gpointer key = nullptr;
	gpointer value = nullptr;
	g_hash_table_iter_init(&iter, self->tiles);
	while (g_hash_table_iter_next(&iter, &key, &value))
	{
		TileEntry* entry = static_cast<TileEntry*>(value);
		if (entry->key.zoom == tileZoom)
		{
			if (entry->key.x < firstColumn || entry->key.x > lastColumn ||
				entry->key.y < firstRow || entry->key.y > lastRow)
			{
				g_hash_table_iter_remove(&iter);
			}
		}
		else if (std::abs(entry->key.zoom - tileZoom) > 1)
		{
			g_hash_table_iter_remove(&iter);
		}
	}

	for (long long column = firstColumn; column <= lastColumn; column++)
	{
		for (long long row = firstRow; row <= lastRow; row++)
		{
			TileKey tileKey;
			tileKey.x = static_cast<int>(column);
			tileKey.y = static_cast<int>(row);
			tileKey.zoom = tileZoom;
			if (g_hash_table_lookup(self->tiles, &tileKey))
				continue;

			TileKey* storedKey = g_new(TileKey, 1);
			*storedKey = tileKey;
			TileEntry* entry = g_new0(TileEntry, 1);
			entry->key = tileKey;
			g_hash_table_insert(self->tiles, storedKey, entry);
			LoadTile(self, entry);
		}
	}

	PruneOldZoomTiles(self);
	gtk_widget_queue_draw(widget);
}

void OnViewportChanged(GObject*, GParamSpec*, gpointer data)
{
	RecomputeGrid(QUIVER_RASTER_MAP_LAYER(data));
}

void QuiverRasterMapLayerSizeAllocate(GtkWidget* widget, int width,
	int height, int baseline)
{
	GTK_WIDGET_CLASS(quiver_raster_map_layer_parent_class)->size_allocate(
		widget, width, height, baseline);
	RecomputeGrid(QUIVER_RASTER_MAP_LAYER(widget));
}

void QuiverRasterMapLayerMeasure(GtkWidget*, GtkOrientation, int,
	int* minimum, int* natural, int* minimumBaseline, int* naturalBaseline)
{
	if (minimum)
		*minimum = 0;
	if (natural)
		*natural = 0;
	if (minimumBaseline)
		*minimumBaseline = -1;
	if (naturalBaseline)
		*naturalBaseline = -1;
}

void QuiverRasterMapLayerSnapshot(GtkWidget* widget, GtkSnapshot* snapshot)
{
	QuiverRasterMapLayer* self = QUIVER_RASTER_MAP_LAYER(widget);
	if (!self->source || !self->tiles)
		return;

	ShumateViewport* viewport = shumate_layer_get_viewport(
		SHUMATE_LAYER(self));
	if (!viewport)
		return;

	const int width = gtk_widget_get_width(widget);
	const int height = gtk_widget_get_height(widget);
	if (width <= 0 || height <= 0)
		return;

	const double zoom = shumate_viewport_get_zoom_level(viewport);
	const int tileZoom = EffectiveTileZoom(self, zoom);
	const double centerX = shumate_map_source_get_x(self->source, zoom,
		shumate_location_get_longitude(SHUMATE_LOCATION(viewport)));
	const double centerY = shumate_map_source_get_y(self->source, zoom,
		shumate_location_get_latitude(SHUMATE_LOCATION(viewport)));
	const double baseTileSize = static_cast<double>(
		shumate_map_source_get_tile_size(self->source));

	GHashTableIter iter;
	gpointer key = nullptr;
	gpointer value = nullptr;

	for (int pass = 0; pass < 2; pass++)
	{
		g_hash_table_iter_init(&iter, self->tiles);
		while (g_hash_table_iter_next(&iter, &key, &value))
		{
			TileEntry* entry = static_cast<TileEntry*>(value);
			const bool currentZoom = entry->key.zoom == tileZoom;
			if ((pass == 0) == currentZoom)
				continue;
			if (!entry->current_tile)
				continue;

			GdkPaintable* paintable = shumate_tile_get_paintable(
				entry->current_tile);
			if (!paintable)
				continue;

			const double tileSize = baseTileSize * std::pow(2.0,
				zoom - static_cast<double>(entry->key.zoom));
			const double x = -(centerX - static_cast<double>(width) / 2.0) +
				tileSize * static_cast<double>(entry->key.x);
			const double y = -(centerY - static_cast<double>(height) / 2.0) +
				tileSize * static_cast<double>(entry->key.y);

			graphene_point_t point;
			point.x = static_cast<float>(x);
			point.y = static_cast<float>(y);

			gtk_snapshot_save(snapshot);
			gtk_snapshot_translate(snapshot, &point);
			gdk_paintable_snapshot(paintable, snapshot, tileSize, tileSize);
			gtk_snapshot_restore(snapshot);
		}
	}
}

void QuiverRasterMapLayerConstructed(GObject* object)
{
	G_OBJECT_CLASS(quiver_raster_map_layer_parent_class)->constructed(object);
	QuiverRasterMapLayer* self = QUIVER_RASTER_MAP_LAYER(object);
	ShumateViewport* viewport = shumate_layer_get_viewport(
		SHUMATE_LAYER(self));
	if (viewport)
	{
		g_signal_connect_object(viewport, "notify",
			G_CALLBACK(OnViewportChanged), self, (GConnectFlags)0);
	}
}

void QuiverRasterMapLayerDispose(GObject* object)
{
	QuiverRasterMapLayer* self = QUIVER_RASTER_MAP_LAYER(object);
	if (self->tiles)
	{
		g_hash_table_unref(self->tiles);
		self->tiles = nullptr;
	}
	if (self->source)
	{
		g_object_unref(self->source);
		self->source = nullptr;
	}
	G_OBJECT_CLASS(quiver_raster_map_layer_parent_class)->dispose(object);
}
}

static void quiver_raster_map_layer_class_init(
	QuiverRasterMapLayerClass* klass)
{
	GObjectClass* objectClass = G_OBJECT_CLASS(klass);
	GtkWidgetClass* widgetClass = GTK_WIDGET_CLASS(klass);

	objectClass->constructed = QuiverRasterMapLayerConstructed;
	objectClass->dispose = QuiverRasterMapLayerDispose;
	widgetClass->size_allocate = QuiverRasterMapLayerSizeAllocate;
	widgetClass->measure = QuiverRasterMapLayerMeasure;
	widgetClass->snapshot = QuiverRasterMapLayerSnapshot;
}

static void quiver_raster_map_layer_init(QuiverRasterMapLayer* self)
{
	self->source = nullptr;
	self->tiles = g_hash_table_new_full(TileKeyHash, TileKeyEqual,
		g_free, TileEntryFree);
}

ShumateLayer* quiver_raster_map_layer_new(ShumateMapSource* source,
	ShumateViewport* viewport)
{
	QuiverRasterMapLayer* self = QUIVER_RASTER_MAP_LAYER(
		g_object_new(quiver_raster_map_layer_get_type(),
			"viewport", viewport, NULL));
	if (source)
		self->source = SHUMATE_MAP_SOURCE(g_object_ref(source));
	return SHUMATE_LAYER(self);
}
