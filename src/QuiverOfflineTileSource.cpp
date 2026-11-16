#include "QuiverOfflineTileSource.h"

#include <config.h>

#include <cairo.h>
#include <gdk/gdk.h>
#include <glib.h>
#include <math.h>

#include <mutex>
#include <string>
#include <vector>

namespace
{
constexpr double kTileSize = 256.0;
constexpr double kMinLatitude = -85.0511287798;
constexpr double kMaxLatitude = 85.0511287798;

struct WorldFeature
{
	int type = 0; // 0 = land polygon, 1 = border polyline, 2 = admin-1 line
	std::vector<std::pair<double, double>> points; // lat, lon
	double minLat = 0.0;
	double maxLat = 0.0;
	double minLon = 0.0;
	double maxLon = 0.0;
};

struct WorldData
{
	std::mutex mutex;
	bool loaded = false;
	std::vector<WorldFeature> features;
};

WorldData& GetWorldData()
{
	static WorldData data;
	return data;
}

bool ReadU16(const unsigned char* p, std::size_t size, std::size_t& offset, guint16& out)
{
	if (offset + 2 > size)
		return false;
	out = (guint16)((guint16)p[offset] | ((guint16)p[offset + 1] << 8));
	offset += 2;
	return true;
}

bool ReadU32(const unsigned char* p, std::size_t size, std::size_t& offset, guint32& out)
{
	if (offset + 4 > size)
		return false;
	out = (guint32)p[offset] | ((guint32)p[offset + 1] << 8) |
		((guint32)p[offset + 2] << 16) | ((guint32)p[offset + 3] << 24);
	offset += 4;
	return true;
}

bool ReadI32(const unsigned char* p, std::size_t size, std::size_t& offset, gint32& out)
{
	guint32 v = 0;
	if (!ReadU32(p, size, offset, v))
		return false;
	out = (gint32)v;
	return true;
}

void LoadWorldData(WorldData& data)
{
	const std::string path = quiver_get_resource_path("world_boundaries.bin");
	gchar* contents = NULL;
	gsize length = 0;
	if (path.empty() || !g_file_get_contents(path.c_str(), &contents, &length, NULL))
	{
		data.loaded = true;
		return;
	}

	const unsigned char* p = (const unsigned char*)contents;
	std::size_t offset = 0;
	if (length < 12 || memcmp(p, "QWVB", 4) != 0)
	{
		g_free(contents);
		data.loaded = true;
		return;
	}
	offset = 4;
	guint16 version = 0, reserved = 0;
	guint32 count = 0;
	if (!ReadU16(p, length, offset, version) ||
		!ReadU16(p, length, offset, reserved) ||
		!ReadU32(p, length, offset, count) || version != 1)
	{
		g_free(contents);
		data.loaded = true;
		return;
	}

	data.features.reserve(count);
	for (guint32 i = 0; i < count; i++)
	{
		if (offset + 4 > length)
			break;
		WorldFeature feature;
		feature.type = p[offset];
		offset += 4;
		guint32 pointCount = 0;
		if (!ReadU32(p, length, offset, pointCount))
			break;
		feature.points.reserve(pointCount);
		for (guint32 j = 0; j < pointCount; j++)
		{
			gint32 lat = 0, lon = 0;
			if (!ReadI32(p, length, offset, lat) || !ReadI32(p, length, offset, lon))
				break;
			const double latDeg = lat / 1000000.0;
			const double lonDeg = lon / 1000000.0;
			if (feature.points.empty())
			{
				feature.minLat = feature.maxLat = latDeg;
				feature.minLon = feature.maxLon = lonDeg;
			}
			else
			{
				feature.minLat = std::min(feature.minLat, latDeg);
				feature.maxLat = std::max(feature.maxLat, latDeg);
				feature.minLon = std::min(feature.minLon, lonDeg);
				feature.maxLon = std::max(feature.maxLon, lonDeg);
			}
			feature.points.emplace_back(latDeg, lonDeg);
		}
		data.features.push_back(std::move(feature));
	}

	g_free(contents);
	data.loaded = true;
}

double LonToWorldX(double lon, double worldWidth)
{
	return (lon + 180.0) / 360.0 * worldWidth;
}

double LatToWorldY(double lat, double worldWidth)
{
	double clamped = lat;
	if (clamped < kMinLatitude) clamped = kMinLatitude;
	if (clamped > kMaxLatitude) clamped = kMaxLatitude;
	const double rad = clamped * M_PI / 180.0;
	const double merc = std::log(std::tan(M_PI / 4.0 + rad / 2.0));
	return (1.0 - merc / M_PI) / 2.0 * worldWidth;
}

double WorldYToLat(double y, double worldWidth)
{
	const double n = M_PI - 2.0 * M_PI * y / worldWidth;
	return 180.0 / M_PI * std::atan(0.5 * (std::exp(n) - std::exp(-n)));
}

bool IsDarkTheme()
{
	GtkSettings* settings = gtk_settings_get_default();
	if (settings)
	{
		gboolean preferDark = FALSE;
		g_object_get(settings, "gtk-application-prefer-dark-theme", &preferDark, NULL);
		if (preferDark)
			return true;

		if (g_object_class_find_property(G_OBJECT_GET_CLASS(settings), "gtk-interface-color-scheme"))
		{
			gint scheme = 0;
			g_object_get(settings, "gtk-interface-color-scheme", &scheme, NULL);
			if (scheme == 2) // GTK_INTERFACE_COLOR_SCHEME_DARK
				return true;
		}
	}
	const char* theme = g_getenv("GTK_THEME");
	return theme != NULL && strstr(theme, "dark") != NULL;
}

GdkTexture* RenderTile(guint zoom, guint x, guint y, guint pixelSize)
{
	if (pixelSize == 0)
		pixelSize = (guint)kTileSize;

	cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
		(int)pixelSize, (int)pixelSize);
	if (!surface || cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS)
	{
		if (surface)
			cairo_surface_destroy(surface);
		return NULL;
	}

	cairo_t* cr = cairo_create(surface);
	const bool dark = IsDarkTheme();

	// Water background.
	if (dark)
		cairo_set_source_rgb(cr, 0.09, 0.13, 0.18);
	else
		cairo_set_source_rgb(cr, 0.78, 0.87, 0.95);
	cairo_paint(cr);

	const double worldWidth = kTileSize * std::pow(2.0, (double)zoom);
	const double originX = (double)x * (double)pixelSize;
	const double originY = (double)y * (double)pixelSize;
	const double scale = (double)pixelSize / kTileSize;

	const double lonLeft = 360.0 * (double)x / std::pow(2.0, (double)zoom) - 180.0;
	const double lonRight = 360.0 * (double)(x + 1) / std::pow(2.0, (double)zoom) - 180.0;
	const double latTop = WorldYToLat((double)y * kTileSize, worldWidth);
	const double latBottom = WorldYToLat((double)(y + 1) * kTileSize, worldWidth);
	auto intersectsTile = [&](const WorldFeature& feature) {
		return !(feature.maxLon < lonLeft || feature.minLon > lonRight ||
			feature.maxLat < latBottom || feature.minLat > latTop);
	};

	auto project = [&](double lat, double lon, double& px, double& py) {
		px = LonToWorldX(lon, worldWidth) * scale - originX;
		py = LatToWorldY(lat, worldWidth) * scale - originY;
	};

	WorldData& data = GetWorldData();
	{
		std::lock_guard<std::mutex> lock(data.mutex);
		if (!data.loaded)
			LoadWorldData(data);

		// Land polygons.
		for (const WorldFeature& feature : data.features)
		{
			if (feature.type != 0 || feature.points.size() < 3 ||
				!intersectsTile(feature))
				continue;
			for (std::size_t i = 0; i < feature.points.size(); i++)
			{
				double px = 0.0, py = 0.0;
				project(feature.points[i].first, feature.points[i].second, px, py);
				if (0 == i)
					cairo_move_to(cr, px, py);
				else
					cairo_line_to(cr, px, py);
			}
			cairo_close_path(cr);
		}
		if (dark)
			cairo_set_source_rgb(cr, 0.20, 0.26, 0.22);
		else
			cairo_set_source_rgb(cr, 0.90, 0.92, 0.82);
		cairo_fill_preserve(cr);
		cairo_set_line_width(cr, 0.7 * scale);
		if (dark)
			cairo_set_source_rgb(cr, 0.55, 0.62, 0.58);
		else
			cairo_set_source_rgb(cr, 0.45, 0.50, 0.45);
		cairo_stroke(cr);

		// State/province borders are a lighter, thinner overlay.  Skip them at
		// world-overview zooms where they collapse into visual noise.
		bool hasAdmin1 = false;
		if (zoom >= 3)
			cairo_new_path(cr);
		for (const WorldFeature& feature : data.features)
		{
			if (zoom < 3 || feature.type != 2 || feature.points.size() < 2 ||
				!intersectsTile(feature))
				continue;
			hasAdmin1 = true;
			for (std::size_t i = 0; i < feature.points.size(); i++)
			{
				double px = 0.0, py = 0.0;
				project(feature.points[i].first, feature.points[i].second, px, py);
				if (0 == i)
					cairo_move_to(cr, px, py);
				else
					cairo_line_to(cr, px, py);
			}
		}
		if (hasAdmin1)
		{
			cairo_set_line_width(cr, 0.4 * scale);
			if (dark)
				cairo_set_source_rgb(cr, 0.32, 0.38, 0.35);
			else
				cairo_set_source_rgb(cr, 0.65, 0.68, 0.62);
			cairo_stroke(cr);
		}
	}
	cairo_destroy(cr);

	// Graticule (10/30 degree spacing depending on zoom).
	double step = 30.0;
	if (zoom >= 4) step = 10.0;
	if (zoom >= 7) step = 5.0;
	if (zoom >= 10) step = 1.0;

	cairo_t* grid = cairo_create(surface);
	cairo_set_line_width(grid, 0.5 * scale);
	if (dark)
		cairo_set_source_rgba(grid, 0.7, 0.8, 0.9, 0.12);
	else
		cairo_set_source_rgba(grid, 0.2, 0.3, 0.45, 0.15);

	const int stepDegrees = (int)step;
	for (int lonDegrees = -180; lonDegrees <= 180; lonDegrees += stepDegrees)
	{
		const double lon = (double)lonDegrees;
		double px = 0.0, py = 0.0;
		project(0.0, lon, px, py);
		cairo_move_to(grid, px, 0.0);
		cairo_line_to(grid, px, (double)pixelSize);
	}
	for (int latDegrees = -80; latDegrees <= 80; latDegrees += stepDegrees)
	{
		const double lat = (double)latDegrees;
		double px = 0.0, py = 0.0;
		project(lat, -180.0, px, py);
		cairo_move_to(grid, 0.0, py);
		cairo_line_to(grid, (double)pixelSize, py);
	}
	cairo_stroke(grid);
	cairo_destroy(grid);

	cairo_surface_flush(surface);
	const int width = cairo_image_surface_get_width(surface);
	const int height = cairo_image_surface_get_height(surface);
	const int stride = cairo_image_surface_get_stride(surface);
	const gsize dataSize = (gsize)stride * (gsize)height;
	GBytes* bytes = g_bytes_new(cairo_image_surface_get_data(surface), dataSize);
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
	const GdkMemoryFormat format = GDK_MEMORY_B8G8R8A8_PREMULTIPLIED;
#else
	const GdkMemoryFormat format = GDK_MEMORY_A8R8G8B8_PREMULTIPLIED;
#endif
	GdkTexture* texture = gdk_memory_texture_new(width, height, format, bytes, stride);
	g_bytes_unref(bytes);
	cairo_surface_destroy(surface);
	return texture;
}
} // namespace

struct _QuiverOfflineTileSource
{
	ShumateMapSource parent_instance;
};

G_DEFINE_TYPE(QuiverOfflineTileSource, quiver_offline_tile_source, SHUMATE_TYPE_MAP_SOURCE)

static void quiver_offline_tile_source_fill_tile_async(ShumateMapSource* self,
	ShumateTile* tile, GCancellable* cancellable, GAsyncReadyCallback callback,
	gpointer user_data)
{
	GTask* task = g_task_new(self, cancellable, callback, user_data);

	if (g_cancellable_is_cancelled(cancellable))
	{
		shumate_tile_set_state(tile, SHUMATE_STATE_DONE);
		g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_CANCELLED,
			"Tile request cancelled");
		g_object_unref(task);
		return;
	}

	const guint zoom = shumate_tile_get_zoom_level(tile);
	const guint x = shumate_tile_get_x(tile);
	const guint y = shumate_tile_get_y(tile);
	guint size = shumate_tile_get_size(tile);
	if (size == 0)
		size = 256;

	GdkTexture* texture = RenderTile(zoom, x, y, size);
	if (texture)
	{
		shumate_tile_set_paintable(tile, GDK_PAINTABLE(texture));
		shumate_tile_set_state(tile, SHUMATE_STATE_LOADED);
		g_object_unref(texture);
		g_task_return_boolean(task, TRUE);
	}
	else
	{
		shumate_tile_set_state(tile, SHUMATE_STATE_DONE);
		g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_FAILED,
			"Could not render offline tile");
	}
	g_object_unref(task);
}

static gboolean quiver_offline_tile_source_fill_tile_finish(ShumateMapSource* self,
	GAsyncResult* result, GError** error)
{
	(void)self;
	return g_task_propagate_boolean(G_TASK(result), error);
}

static void quiver_offline_tile_source_class_init(QuiverOfflineTileSourceClass* klass)
{
	ShumateMapSourceClass* sourceClass = SHUMATE_MAP_SOURCE_CLASS(klass);
	sourceClass->fill_tile_async = quiver_offline_tile_source_fill_tile_async;
	sourceClass->fill_tile_finish = quiver_offline_tile_source_fill_tile_finish;
}

static void quiver_offline_tile_source_init(QuiverOfflineTileSource* self)
{
	shumate_map_source_set_projection(SHUMATE_MAP_SOURCE(self),
		SHUMATE_MAP_PROJECTION_MERCATOR);
	shumate_map_source_set_id(SHUMATE_MAP_SOURCE(self), "quiver-offline");
	shumate_map_source_set_name(SHUMATE_MAP_SOURCE(self), "Quiver Offline World");
	shumate_map_source_set_license(SHUMATE_MAP_SOURCE(self), "Public Domain");
	shumate_map_source_set_license_uri(SHUMATE_MAP_SOURCE(self), "https://www.naturalearthdata.com/");
	shumate_map_source_set_min_zoom_level(SHUMATE_MAP_SOURCE(self), 0);
	shumate_map_source_set_max_zoom_level(SHUMATE_MAP_SOURCE(self), 18);
	shumate_map_source_set_tile_size(SHUMATE_MAP_SOURCE(self), 256);
}

ShumateMapSource* quiver_offline_tile_source_new(void)
{
	ShumateMapSource* source = SHUMATE_MAP_SOURCE(
		g_object_new(QUIVER_TYPE_OFFLINE_TILE_SOURCE, NULL));
	shumate_map_source_set_id(source, "quiver-offline");
	shumate_map_source_set_name(source, "Quiver Offline World");
	shumate_map_source_set_license(source, "Public Domain");
	shumate_map_source_set_license_uri(source,
		"https://www.naturalearthdata.com/");
	shumate_map_source_set_min_zoom_level(source, 0);
	shumate_map_source_set_max_zoom_level(source, 18);
	return source;
}
