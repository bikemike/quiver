#include "catch2/catch_test_macros.hpp"
#include "test_helpers.h"

#include "QuiverMapView.h"
#include "QuiverOfflineTileSource.h"
#include "ImageList.h"
#include "Preferences.h"
#include "QuiverPrefs.h"

#include <cairo.h>
#include <exiv2/exiv2.hpp>
#include <gdk/gdk.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <gsk/gsk.h>
#include <shumate/shumate.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <list>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

namespace
{
struct MapViewTestConfigFixture
{
	char m_tmpPath[256] = {};
	char m_prevConfig[256] = {};

	MapViewTestConfigFixture()
	{
		strncpy(m_prevConfig, g_szConfigFilePath, sizeof(m_prevConfig) - 1);
		strncpy(m_tmpPath, "/tmp/quiver_map_view_cfg_XXXXXX.ini",
			sizeof(m_tmpPath) - 1);
		int fd = g_mkstemp(m_tmpPath);
		if (fd >= 0)
			close(fd);
		strncpy(g_szConfigFilePath, m_tmpPath, sizeof(m_tmpPath) - 1);
		g_szConfigFilePath[sizeof(m_tmpPath) - 1] = '\0';
		Preferences::Reset();
	}

	~MapViewTestConfigFixture()
	{
		Preferences::Reset();
		strncpy(g_szConfigFilePath, m_prevConfig, sizeof(m_prevConfig) - 1);
		g_szConfigFilePath[sizeof(m_prevConfig) - 1] = '\0';
		g_unlink(m_tmpPath);
	}
};

GtkWidget* FindDropDown(GtkWidget* root)
{
	if (GTK_IS_DROP_DOWN(root))
		return root;
	for (GtkWidget* child = gtk_widget_get_first_child(root);
		child != nullptr; child = gtk_widget_get_next_sibling(child))
	{
		if (GtkWidget* found = FindDropDown(child))
			return found;
	}
	return nullptr;
}

struct GpsTempJpeg
{
	std::string path;

	GpsTempJpeg()
	{
		std::string tmpl = "/tmp/quiver_map_gps_XXXXXX.jpg";
		std::vector<char> buffer(tmpl.begin(), tmpl.end());
		buffer.push_back('\0');
		int fd = g_mkstemp(buffer.data());
		REQUIRE(fd >= 0);
		close(fd);
		path = buffer.data();
	}

	GpsTempJpeg(const GpsTempJpeg&) = delete;
	GpsTempJpeg& operator=(const GpsTempJpeg&) = delete;

	GpsTempJpeg(GpsTempJpeg&& other) noexcept : path(std::move(other.path))
	{
		other.path.clear();
	}

	~GpsTempJpeg()
	{
		if (!path.empty())
			g_unlink(path.c_str());
	}

	std::string URI() const
	{
		gchar* uri = g_filename_to_uri(path.c_str(), NULL, NULL);
		std::string result = uri ? uri : "";
		g_free(uri);
		return result;
	}
};

GpsTempJpeg MakeGpsJpeg(double lat, double lon)
{
	GpsTempJpeg file;
	const std::string source = QuiverTest_GetImagesDir() + "/sample_4k.jpg";
	char* contents = nullptr;
	gsize length = 0;
	REQUIRE(g_file_get_contents(source.c_str(), &contents, &length, NULL));
	REQUIRE(g_file_set_contents(file.path.c_str(), contents, length, NULL));
	g_free(contents);

	auto image = Exiv2::ImageFactory::open(file.path);
	REQUIRE(image.get() != nullptr);
	image->readMetadata();

	auto coordinate = [](double value, char* buffer, std::size_t size) {
		const double absolute = std::fabs(value);
		const int degrees = (int)std::floor(absolute);
		const int minutes = (int)std::floor((absolute - degrees) * 60.0);
		const double seconds = (absolute - degrees - minutes / 60.0) * 3600.0;
		g_snprintf(buffer, size, "%d/1 %d/1 %.0f/100",
			degrees, minutes, seconds * 100.0);
	};
	char latitude[64] = {};
	char longitude[64] = {};
	coordinate(lat, latitude, sizeof(latitude));
	coordinate(lon, longitude, sizeof(longitude));

	image->exifData()["Exif.GPSInfo.GPSLatitudeRef"] = lat < 0.0 ? "S" : "N";
	image->exifData()["Exif.GPSInfo.GPSLatitude"] = latitude;
	image->exifData()["Exif.GPSInfo.GPSLongitudeRef"] = lon < 0.0 ? "W" : "E";
	image->exifData()["Exif.GPSInfo.GPSLongitude"] = longitude;
	image->writeMetadata();
	return file;
}
}

TEST_CASE("QuiverMapView builds a map widget and indexes a list", "[map_view]")
{
	if (!QuiverTest_HasDisplay())
		SKIP("Display required for the map widget");

	QuiverMapView view;
	GtkWidget* widget = view.GetWidget();
	REQUIRE(widget != nullptr);
	REQUIRE(GTK_IS_WIDGET(widget));

	ImageListPtr list(new ImageList(false));
	std::list<std::string> files = {
		QuiverTest_GetImagesDir() + "/sample_4k.jpg",
		QuiverTest_GetImagesDir() + "/sample_rotated.jpg",
	};
	list->SetImageList(&files);
	REQUIRE(list->GetSize() == 2);

	view.SetImageList(list);
	REQUIRE(view.GetImageList() == list);
	REQUIRE_FALSE(view.HasActiveRegionFilter());

	view.Refresh();
	view.ClearRegionFilterVisual();
	REQUIRE_FALSE(view.HasActiveRegionFilter());
}

TEST_CASE("Map view persists the selected source", "[map_view]")
{
	if (!QuiverTest_HasDisplay())
		SKIP("Display required for the map widget");

	MapViewTestConfigFixture cfg;
	Preferences::GetInstance()->SetString(QUIVER_PREFS_MAP,
		QUIVER_PREFS_MAP_SOURCE, "quiver-google");

	{
		QuiverMapView view;
		GtkWidget* map = gtk_overlay_get_child(GTK_OVERLAY(view.GetWidget()));
		REQUIRE(map != nullptr);
		ShumateViewport* viewport = shumate_map_get_viewport(SHUMATE_MAP(map));
		REQUIRE(viewport != nullptr);
		ShumateMapSource* source =
			shumate_viewport_get_reference_map_source(viewport);
		REQUIRE(source != nullptr);
		CHECK(std::string(shumate_map_source_get_id(source)) ==
			"quiver-google");

		GtkWidget* dropdown = FindDropDown(view.GetWidget());
		REQUIRE(dropdown != nullptr);
		gtk_drop_down_set_selected(GTK_DROP_DOWN(dropdown), 0);
		CHECK(gtk_drop_down_get_selected(GTK_DROP_DOWN(dropdown)) == 0);
		CHECK(Preferences::GetInstance()->GetString(QUIVER_PREFS_MAP,
			QUIVER_PREFS_MAP_SOURCE, "") == "quiver-offline");
	}

	Preferences::Reset();

	{
		QuiverMapView view;
		GtkWidget* map = gtk_overlay_get_child(GTK_OVERLAY(view.GetWidget()));
		REQUIRE(map != nullptr);
		ShumateViewport* viewport = shumate_map_get_viewport(SHUMATE_MAP(map));
		REQUIRE(viewport != nullptr);
		ShumateMapSource* source =
			shumate_viewport_get_reference_map_source(viewport);
		REQUIRE(source != nullptr);
		CHECK(std::string(shumate_map_source_get_id(source)) ==
			"quiver-offline");

		GtkWidget* dropdown = FindDropDown(view.GetWidget());
		REQUIRE(dropdown != nullptr);
		CHECK(gtk_drop_down_get_selected(GTK_DROP_DOWN(dropdown)) == 0);
	}
}

TEST_CASE("Map thumbnails repeat in wrapped world copies", "[map_view]")
{
	if (!QuiverTest_HasDisplay())
		SKIP("Display required for the map widget");

	GpsTempJpeg place = MakeGpsJpeg(0.0, -74.0);
	std::list<std::string> files = {place.path};
	ImageListPtr list(new ImageList(false));
	list->SetImageList(&files);

	QuiverMapView view;
	view.SetImageList(list);
	GtkWidget* window = gtk_window_new();
	gtk_window_set_default_size(GTK_WINDOW(window), 640, 480);
	gtk_window_set_child(GTK_WINDOW(window), view.GetWidget());
	gtk_window_present(GTK_WINDOW(window));

	auto drain = [](int milliseconds) {
		const gint64 deadline = g_get_monotonic_time() +
			(gint64)milliseconds * G_TIME_SPAN_MILLISECOND;
		while (g_get_monotonic_time() < deadline)
		{
			while (g_main_context_iteration(NULL, FALSE))
				;
			g_usleep(10000);
		}
	};
	drain(1500);
	view.ZoomToWorld();
	drain(1500);

	GtkWidget* map = gtk_overlay_get_child(GTK_OVERLAY(view.GetWidget()));
	REQUIRE(map != nullptr);

	int thumbnails = 0;
	auto visit = [&](auto&& self, GtkWidget* parent) -> void {
		for (GtkWidget* child = gtk_widget_get_first_child(parent);
			child != nullptr; child = gtk_widget_get_next_sibling(child))
		{
			if (gtk_widget_has_css_class(child, "quiver-map-thumb"))
				thumbnails++;
			self(self, child);
		}
	};
	visit(visit, map);

	CHECK(thumbnails >= 2);
	gtk_window_destroy(GTK_WINDOW(window));
}

TEST_CASE("Map view drops stale markers when a new list starts indexing",
	"[map_view]")
{
	if (!QuiverTest_HasDisplay())
		SKIP("Display required for the map widget");

	GpsTempJpeg place = MakeGpsJpeg(48.4541, -123.4718);
	std::list<std::string> files = {place.path};
	ImageListPtr list(new ImageList(false));
	list->SetImageList(&files);

	QuiverMapView view;
	view.SetImageList(list);
	GtkWidget* window = gtk_window_new();
	gtk_window_set_default_size(GTK_WINDOW(window), 640, 480);
	gtk_window_set_child(GTK_WINDOW(window), view.GetWidget());
	gtk_window_present(GTK_WINDOW(window));

	auto drain = [](int milliseconds) {
		const gint64 deadline = g_get_monotonic_time() +
			(gint64)milliseconds * G_TIME_SPAN_MILLISECOND;
		while (g_get_monotonic_time() < deadline)
		{
			while (g_main_context_iteration(NULL, FALSE))
				;
			g_usleep(10000);
		}
	};
	drain(1500);

	GtkWidget* map = gtk_overlay_get_child(GTK_OVERLAY(view.GetWidget()));
	REQUIRE(map != nullptr);

	auto count_thumbnails = [&]() {
		int thumbnails = 0;
		auto visit = [&](auto&& self, GtkWidget* parent) -> void {
			for (GtkWidget* child = gtk_widget_get_first_child(parent);
				child != nullptr; child = gtk_widget_get_next_sibling(child))
			{
				if (gtk_widget_has_css_class(child, "quiver-map-thumb"))
					thumbnails++;
				self(self, child);
			}
		};
		visit(visit, map);
		return thumbnails;
	};
	REQUIRE(count_thumbnails() >= 1);

	std::size_t progressResets = 0;
	view.SetMetadataProgressCallback([&](std::size_t, std::size_t total) {
		if (total == 0)
			progressResets++;
	});

	// A new list definition must not leave the previous list's pins on the
	// map while the replacement GPS scan runs.
	view.ResetViewForImageList();
	CHECK(count_thumbnails() == 0);
	CHECK(progressResets == 1);

	drain(2000);
	CHECK(count_thumbnails() >= 1);
	gtk_window_destroy(GTK_WINDOW(window));
}

TEST_CASE("Map view fits and resets to GPS item bounds", "[map_view]")
{
	if (!QuiverTest_HasDisplay())
		SKIP("Display required for the map widget");

	GpsTempJpeg vancouver = MakeGpsJpeg(48.4541, -123.4718);
	GpsTempJpeg newYork = MakeGpsJpeg(40.7128, -74.0060);
	GpsTempJpeg newYorkSecond = MakeGpsJpeg(40.7128, -74.0060);
	std::list<std::string> bothFiles = {vancouver.path, newYork.path};
	ImageListPtr list(new ImageList(false));
	list->SetImageList(&bothFiles);
	REQUIRE(list->GetSize() == 2);

	QuiverMapView view;
	view.SetImageList(list);
	GtkWidget* window = gtk_window_new();
	gtk_window_set_default_size(GTK_WINDOW(window), 640, 480);
	gtk_window_set_child(GTK_WINDOW(window), view.GetWidget());
	gtk_window_present(GTK_WINDOW(window));

	auto drain = [](int milliseconds) {
		const gint64 deadline = g_get_monotonic_time() +
			(gint64)milliseconds * G_TIME_SPAN_MILLISECOND;
		while (g_get_monotonic_time() < deadline)
		{
			while (g_main_context_iteration(NULL, FALSE))
				;
			g_usleep(10000);
		}
	};
	drain(1000);

	GtkWidget* map = gtk_overlay_get_child(GTK_OVERLAY(view.GetWidget()));
	REQUIRE(map != nullptr);
	ShumateViewport* viewport = shumate_map_get_viewport(SHUMATE_MAP(map));
	REQUIRE(viewport != nullptr);

	auto pump_until = [&](auto predicate) {
		const gint64 deadline = g_get_monotonic_time() +
			5 * G_TIME_SPAN_SECOND;
		while (g_get_monotonic_time() < deadline)
		{
			while (g_main_context_iteration(NULL, FALSE))
				;
			if (predicate())
				return true;
			g_usleep(10000);
		}
		return predicate();
	};

	const double worldZoom = std::max(
		std::log2((double)std::min(gtk_widget_get_width(map),
			gtk_widget_get_height(map)) / 256.0),
		(double)shumate_viewport_get_min_zoom_level(viewport));
	REQUIRE(pump_until([&] {
		return shumate_viewport_get_zoom_level(viewport) > worldZoom + 0.5;
	}));

	const double fittedZoom = shumate_viewport_get_zoom_level(viewport);
	REQUIRE(fittedZoom < 10.0);
	REQUIRE(std::fabs(shumate_location_get_longitude(
		SHUMATE_LOCATION(viewport)) - ((-123.4718 - 74.0060) / 2.0)) < 1e-6);

	const int width = gtk_widget_get_width(map);
	const int height = gtk_widget_get_height(map);
	for (const auto& point : {std::make_pair(48.4541, -123.4718),
		std::make_pair(40.7128, -74.0060)})
	{
		double x = 0.0, y = 0.0;
		shumate_viewport_location_to_widget_coords(viewport, map,
			point.first, point.second, &x, &y);
		CHECK(x >= 0.0);
		CHECK(x <= (double)width);
		CHECK(y >= 0.0);
		CHECK(y <= (double)height);
	}

	view.ZoomToWorld();
	CHECK(shumate_viewport_get_zoom_level(viewport) == worldZoom);
	CHECK(std::fabs(shumate_location_get_longitude(
		SHUMATE_LOCATION(viewport))) < 1e-6);

	std::list<std::string> onePlace = {newYork.path, newYorkSecond.path};
	list->SetImageList(&onePlace);
	REQUIRE(list->GetSize() == 2);
	view.ResetViewForImageList();
	REQUIRE(pump_until([&] {
		return std::fabs(shumate_location_get_longitude(
			SHUMATE_LOCATION(viewport)) - (-74.0060)) < 1e-6 &&
			shumate_viewport_get_zoom_level(viewport) > 10.0;
	}));
	CHECK(std::fabs(shumate_location_get_latitude(
		SHUMATE_LOCATION(viewport)) - 40.7128) < 1e-4);

	gtk_window_destroy(GTK_WINDOW(window));
}

namespace
{
struct TileResult
{
	GMainLoop* loop = nullptr;
	ShumateMapSource* source = nullptr;
	gboolean ok = FALSE;
};

void OnTileFilled(GObject*, GAsyncResult* result, gpointer data)
{
	TileResult* state = static_cast<TileResult*>(data);
	GError* error = NULL;
	state->ok = shumate_map_source_fill_tile_finish(state->source, result, &error);
	g_clear_error(&error);
	g_main_loop_quit(state->loop);
}

bool FillOfflineTile(guint zoom, guint x, guint y, guint size,
	std::vector<guint8>& pixels, int& width, int& height)
{
	ShumateMapSource* source = quiver_offline_tile_source_new();
	if (!source)
		return false;

	ShumateTile* tile = shumate_tile_new_full(x, y, size, zoom);
	if (!tile)
	{
		g_object_unref(source);
		return false;
	}

	TileResult state;
	state.loop = g_main_loop_new(NULL, FALSE);
	state.source = source;
	shumate_map_source_fill_tile_async(source, tile, NULL, OnTileFilled, &state);
	g_main_loop_run(state.loop);
	g_main_loop_unref(state.loop);

	bool ok = state.ok != FALSE;
	if (ok)
	{
		GdkPaintable* paintable = shumate_tile_get_paintable(tile);
		if (!paintable || !GDK_IS_TEXTURE(paintable))
		{
			ok = false;
		}
		else
		{
			GdkTexture* texture = GDK_TEXTURE(paintable);
			width = gdk_texture_get_width(texture);
			height = gdk_texture_get_height(texture);
			pixels.resize((std::size_t)width * height * 4);
			gdk_texture_download(texture, pixels.data(), width * 4);
		}
	}

	g_object_unref(tile);
	g_object_unref(source);
	return ok;
}
}

TEST_CASE("Offline tile source renders land pixels", "[map_view][offline_tile]")
{
	if (!QuiverTest_HasDisplay())
		SKIP("Display required for tile rendering");

	std::vector<guint8> pixels;
	int width = 0, height = 0;
	REQUIRE(FillOfflineTile(0, 0, 0, 256, pixels, width, height));
	REQUIRE(width == 256);
	REQUIRE(height == 256);

	// Water is a flat background; count pixels that differ from the corner.
	const guint8* corner = pixels.data();
	std::size_t landPixels = 0;
	// Count the actual land fill colour too.  Graticule lines also differ from
	// the water, so the old "not water" check passed even when the world data
	// failed to load and only the grid was drawn.
	std::size_t landFillPixels = 0;
	for (std::size_t i = 0; i < pixels.size(); i += 4)
	{
		const guint8 b = pixels[i];
		const guint8 g = pixels[i + 1];
		const guint8 r = pixels[i + 2];
		if (b != corner[0] || g != corner[1] || r != corner[2])
			landPixels++;
		const bool lightLand = std::abs((int)b - 209) <= 12 &&
			std::abs((int)g - 235) <= 12 && std::abs((int)r - 230) <= 12;
		const bool darkLand = std::abs((int)b - 56) <= 12 &&
			std::abs((int)g - 66) <= 12 && std::abs((int)r - 51) <= 12;
		if (lightLand || darkLand)
			landFillPixels++;
	}
	REQUIRE(landPixels > 500);
	REQUIRE(landFillPixels > 1000);
}

TEST_CASE("Offline tile source renders state and province borders",
	"[map_view][offline_tile]")
{
	if (!QuiverTest_HasDisplay())
		SKIP("Display required for tile rendering");

	// z5/x7/y12 covers the central United States, which has many state lines.
	// Render at 1024 px so the thin lines have solid core pixels.
	std::vector<guint8> pixels;
	int width = 0, height = 0;
	REQUIRE(FillOfflineTile(5, 7, 12, 1024, pixels, width, height));

	std::size_t admin1Pixels = 0;
	for (std::size_t i = 0; i < pixels.size(); i += 4)
	{
		const guint8 b = pixels[i];
		const guint8 g = pixels[i + 1];
		const guint8 r = pixels[i + 2];
		const bool lightLine = std::abs((int)b - 158) <= 14 &&
			std::abs((int)g - 173) <= 14 && std::abs((int)r - 166) <= 14;
		const bool darkLine = std::abs((int)b - 89) <= 14 &&
			std::abs((int)g - 97) <= 14 && std::abs((int)r - 82) <= 14;
		if (lightLine || darkLine)
			admin1Pixels++;
	}
	REQUIRE(admin1Pixels > 50);
}

TEST_CASE("Map zoom hotkey changes the viewport zoom", "[map_view]")
{
	if (!QuiverTest_HasDisplay())
		SKIP("Display required for the map widget");

	QuiverMapView view;
	int regionCalls = 0;
	view.SetRegionFilterCallback([&regionCalls](double, double, double, double) {
		regionCalls++;
	});
	GtkWidget* window = gtk_window_new();
	gtk_window_set_default_size(GTK_WINDOW(window), 640, 480);
	gtk_window_set_child(GTK_WINDOW(window), view.GetWidget());
	gtk_window_present(GTK_WINDOW(window));

	auto drain = [](int milliseconds) {
		const gint64 deadline = g_get_monotonic_time() +
			(gint64)milliseconds * G_TIME_SPAN_MILLISECOND;
		while (g_get_monotonic_time() < deadline)
		{
			while (g_main_context_iteration(NULL, FALSE))
				;
			g_usleep(10000);
		}
	};
	drain(1000);

	GtkWidget* map = gtk_overlay_get_child(GTK_OVERLAY(view.GetWidget()));
	REQUIRE(map != nullptr);
	REQUIRE(SHUMATE_IS_MAP(map));
	ShumateViewport* viewport = shumate_map_get_viewport(SHUMATE_MAP(map));
	REQUIRE(viewport != nullptr);

	const double before = shumate_viewport_get_zoom_level(viewport);
	GtkWidget* root = GTK_WIDGET(gtk_widget_get_root(map));
	REQUIRE(root != nullptr);
	GListModel* controllers = gtk_widget_observe_controllers(root);
	REQUIRE(controllers != nullptr);
	gboolean handled = FALSE;
	for (guint i = 0; i < g_list_model_get_n_items(controllers); i++)
	{
		GtkEventController* controller =
			GTK_EVENT_CONTROLLER(g_list_model_get_item(controllers, i));
		if (controller && GTK_IS_EVENT_CONTROLLER_KEY(controller))
		{
			gboolean result = FALSE;
			g_signal_emit_by_name(controller, "key-pressed",
				GDK_KEY_plus, 0, (GdkModifierType)0, &result);
			handled = handled || result;
		}
		g_clear_object(&controller);
	}
	g_object_unref(controllers);

	REQUIRE(handled);
	const double afterIn = shumate_viewport_get_zoom_level(viewport);
	REQUIRE(afterIn > before);

	view.ZoomOut();
	REQUIRE(shumate_viewport_get_zoom_level(viewport) < afterIn);

	drain(500);
	REQUIRE(regionCalls > 0);
	const int appliedCalls = regionCalls;

	view.ZoomToWorld();
	const double expectedWorldZoom = std::max(
		std::log2((double)std::min(gtk_widget_get_width(map),
			gtk_widget_get_height(map)) / 256.0),
		(double)shumate_viewport_get_min_zoom_level(viewport));
	CHECK(shumate_viewport_get_zoom_level(viewport) == expectedWorldZoom);
	CHECK(std::abs(shumate_location_get_latitude(SHUMATE_LOCATION(viewport))) < 1e-6);
	CHECK(std::abs(shumate_location_get_longitude(SHUMATE_LOCATION(viewport))) < 1e-6);

	drain(500);
	CHECK(regionCalls == appliedCalls);

	gtk_window_destroy(GTK_WINDOW(window));
}

TEST_CASE("Map viewport filter wraps across the antimeridian", "[map_view]")
{
	if (!QuiverTest_HasDisplay())
		SKIP("Display required for the map widget");

	QuiverMapView view;
	double latTop = 0.0, lonLeft = 0.0, latBottom = 0.0, lonRight = 0.0;
	int regionCalls = 0;
	view.SetRegionFilterCallback([&](double top, double left, double bottom,
		double right) {
		latTop = top;
		lonLeft = left;
		latBottom = bottom;
		lonRight = right;
		regionCalls++;
	});

	GtkWidget* window = gtk_window_new();
	gtk_window_set_default_size(GTK_WINDOW(window), 640, 480);
	gtk_window_set_child(GTK_WINDOW(window), view.GetWidget());
	gtk_window_present(GTK_WINDOW(window));

	auto drain = [](int milliseconds) {
		const gint64 deadline = g_get_monotonic_time() +
			(gint64)milliseconds * G_TIME_SPAN_MILLISECOND;
		while (g_get_monotonic_time() < deadline)
		{
			while (g_main_context_iteration(NULL, FALSE))
				;
			g_usleep(10000);
		}
	};
	drain(1000);

	GtkWidget* map = gtk_overlay_get_child(GTK_OVERLAY(view.GetWidget()));
	REQUIRE(map != nullptr);
	ShumateViewport* viewport = shumate_map_get_viewport(SHUMATE_MAP(map));
	REQUIRE(viewport != nullptr);

	view.ZoomIn();
	shumate_location_set_location(SHUMATE_LOCATION(viewport), 0.0, -179.9);
	drain(500);

	REQUIRE(regionCalls > 0);
	REQUIRE(latTop > latBottom);
	REQUIRE(lonLeft > lonRight);
	REQUIRE(lonLeft > 100.0);
	REQUIRE(lonRight < -100.0);

	gtk_window_destroy(GTK_WINDOW(window));
}

TEST_CASE("Map widget paints tiles", "[map_render]")
{
	if (!QuiverTest_HasDisplay())
		SKIP("Display required for map rendering");

	QuiverMapView view;
	GtkWidget* window = gtk_window_new();
	gtk_window_set_default_size(GTK_WINDOW(window), 640, 480);
	gtk_window_set_child(GTK_WINDOW(window), view.GetWidget());
	gtk_window_present(GTK_WINDOW(window));

	const gint64 deadline = g_get_monotonic_time() + 2 * G_TIME_SPAN_SECOND;
	while (g_get_monotonic_time() < deadline)
	{
		while (g_main_context_iteration(NULL, FALSE))
			;
		g_usleep(10000);
	}

	GdkPaintable* paintable = gtk_widget_paintable_new(view.GetWidget());
	GtkSnapshot* snapshot = gtk_snapshot_new();
	gdk_paintable_snapshot(paintable, snapshot, 640, 480);
	GskRenderNode* node = gtk_snapshot_free_to_node(snapshot);
	REQUIRE(node != nullptr);

	cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 640, 480);
	cairo_t* cr = cairo_create(surface);
	gsk_render_node_draw(node, cr);
	cairo_destroy(cr);
	cairo_surface_flush(surface);
	cairo_surface_write_to_png(surface, "/tmp/quiver-map-render.png");

	unsigned char* data = cairo_image_surface_get_data(surface);
	const int stride = cairo_image_surface_get_stride(surface);
	std::size_t opaqueMapPixels = 0;
	for (int y = 240; y < 480; y += 4)
	{
		for (int x = 0; x < 320; x += 4)
		{
			const guint32 pixel = *(guint32*)(data + y * stride + x * 4);
			if ((pixel >> 24) == 0xff)
				opaqueMapPixels++;
		}
	}
	cairo_surface_destroy(surface);
	gsk_render_node_unref(node);
	g_object_unref(paintable);
	gtk_window_destroy(GTK_WINDOW(window));

	// The bottom-left quadrant is away from the controls and markers; the
	// tile layer must have painted an opaque map background there.
	REQUIRE(opaqueMapPixels > 1000);
}


TEST_CASE("Map view fits wide latitude spans inside a short window", "[map_view]")
{
	if (!QuiverTest_HasDisplay())
		SKIP("Display required for the map widget");

	GpsTempJpeg south = MakeGpsJpeg(-55.0, -10.0);
	GpsTempJpeg north = MakeGpsJpeg(65.0, 10.0);
	std::list<std::string> files = {south.path, north.path};
	ImageListPtr list(new ImageList(false));
	list->SetImageList(&files);
	REQUIRE(list->GetSize() == 2);

	QuiverMapView view;
	view.SetImageList(list);
	GtkWidget* window = gtk_window_new();
	gtk_window_set_default_size(GTK_WINDOW(window), 900, 320);
	gtk_window_set_child(GTK_WINDOW(window), view.GetWidget());
	gtk_window_present(GTK_WINDOW(window));

	auto drain = [](int milliseconds) {
		const gint64 deadline = g_get_monotonic_time() +
			(gint64)milliseconds * G_TIME_SPAN_MILLISECOND;
		while (g_get_monotonic_time() < deadline)
		{
			while (g_main_context_iteration(NULL, FALSE))
				;
			g_usleep(10000);
		}
	};
	drain(1200);

	GtkWidget* map = gtk_overlay_get_child(GTK_OVERLAY(view.GetWidget()));
	REQUIRE(map != nullptr);
	ShumateViewport* viewport = shumate_map_get_viewport(SHUMATE_MAP(map));
	REQUIRE(viewport != nullptr);

	const double fittedZoom = shumate_viewport_get_zoom_level(viewport);
	const int width = gtk_widget_get_width(map);
	const int height = gtk_widget_get_height(map);
	for (const auto& point : {std::make_pair(-55.0, -10.0),
		std::make_pair(65.0, 10.0)})
	{
		double x = 0.0, y = 0.0;
		shumate_viewport_location_to_widget_coords(viewport, map,
			point.first, point.second, &x, &y);
		CAPTURE(fittedZoom, width, height, x, y);
		CHECK(x >= 0.0);
		CHECK(x <= (double)width);
		CHECK(y >= 0.0);
		CHECK(y <= (double)height);
	}

	gtk_window_destroy(GTK_WINDOW(window));
}
