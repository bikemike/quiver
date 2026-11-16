#include "QuiverMapView.h"

#include <config.h>

#include <glib.h>
#include <gtk/gtk.h>
#include <json-glib/json-glib.h>
#include <libsoup/soup.h>
#include <shumate/shumate.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <condition_variable>
#include <deque>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#include "IImageListEventHandler.h"
#include "Preferences.h"
#include "QuiverFile.h"
#include "QuiverGpsIndex.h"
#include "QuiverOfflineTileSource.h"
#include "QuiverPrefs.h"
#include "QuiverRasterMapLayer.h"
#include "QuiverRasterTileSource.h"

namespace
{
constexpr double kClusterRadius = 48.0;
constexpr double kTileWorldSize = 256.0;
constexpr double kFitPaddingPx = 56.0;
constexpr double kWrapCullMargin = 96.0;
constexpr double kSingleItemZoom = 12.0;

bool MapDebugEnabled()
{
	static const bool enabled = g_getenv("QUIVER_DEBUG_MAP") != NULL;
	return enabled;
}

bool MapDebugVerbose()
{
	static const bool enabled = g_getenv("QUIVER_DEBUG_MAP_VERBOSE") != NULL;
	return enabled;
}

double LonToWorldX(double lon, double worldSize)
{
	return (lon + 180.0) / 360.0 * worldSize;
}

double LatToWorldY(double lat, double worldSize)
{
	const double clamped = std::clamp(lat, SHUMATE_MIN_LATITUDE, SHUMATE_MAX_LATITUDE);
	const double rad = clamped * M_PI / 180.0;
	const double merc = std::log(std::tan(M_PI / 4.0 + rad / 2.0));
	return (1.0 - merc / M_PI) / 2.0 * worldSize;
}

double WorldXToLon(double x, double worldSize)
{
	double lon = x / worldSize * 360.0 - 180.0;
	while (lon < -180.0)
		lon += 360.0;
	while (lon > 180.0)
		lon -= 360.0;
	return lon;
}

double WorldYToLat(double y, double worldSize)
{
	const double n = M_PI - 2.0 * M_PI * y / worldSize;
	const double lat = 180.0 / M_PI * std::atan(0.5 * (std::exp(n) - std::exp(-n)));
	return std::clamp(lat, SHUMATE_MIN_LATITUDE, SHUMATE_MAX_LATITUDE);
}

constexpr const char* kOpenFreeMapFallbackTileUrl =
	"https://tiles.openfreemap.org/planet/20261101_080013_pt/{z}/{x}/{y}.pbf";

std::string ResolveOpenFreeMapTileUrl()
{
	constexpr const char* kTileJsonUrl =
		"https://tiles.openfreemap.org/planet/latest";
	constexpr const char* kFallback = kOpenFreeMapFallbackTileUrl;

	SoupSession* session = soup_session_new();
	soup_session_set_timeout(session, 2);
	soup_session_set_user_agent(session, "Quiver/" PACKAGE_VERSION);
	SoupMessage* message = soup_message_new(SOUP_METHOD_GET, kTileJsonUrl);
	GError* error = nullptr;
	GBytes* bytes = message
		? soup_session_send_and_read(session, message, nullptr, &error)
		: nullptr;
	if (!bytes || soup_message_get_status(message) != SOUP_STATUS_OK)
	{
		g_clear_error(&error);
		if (bytes)
			g_bytes_unref(bytes);
		g_clear_object(&message);
		g_object_unref(session);
		return kFallback;
	}

	std::string result = kFallback;
	gsize length = 0;
	const char* data = static_cast<const char*>(
		g_bytes_get_data(bytes, &length));
	JsonParser* parser = json_parser_new();
	if (data && json_parser_load_from_data(parser, data, (gssize)length,
		&error))
	{
		JsonNode* root = json_parser_get_root(parser);
		JsonObject* object = root && JSON_NODE_HOLDS_OBJECT(root)
			? json_node_get_object(root) : nullptr;
		JsonArray* tiles = object
			? json_object_get_array_member(object, "tiles") : nullptr;
		if (tiles && json_array_get_length(tiles) > 0)
		{
			const char* url = json_array_get_string_element(tiles, 0);
			if (url && std::strstr(url, "{z}") && std::strstr(url, "{x}") &&
				std::strstr(url, "{y}"))
			{
				result = url;
			}
		}
	}
	g_clear_error(&error);
	g_object_unref(parser);
	g_bytes_unref(bytes);
	g_clear_object(&message);
	g_object_unref(session);
	return result;
}

ShumateMapSource* CreateOsmDarkSource()
{
	if (!shumate_vector_renderer_is_supported())
		return nullptr;

	const std::string path = quiver_get_resource_path("osm-dark-style.json");
	gchar* style = nullptr;
	GError* error = nullptr;
	if (!g_file_get_contents(path.c_str(), &style, nullptr, &error))
	{
		g_clear_error(&error);
		return nullptr;
	}

	const std::string fallback = kOpenFreeMapFallbackTileUrl;
	std::string styleText(style);
	g_free(style);
	const std::string tileUrl = ResolveOpenFreeMapTileUrl();
	if (tileUrl != fallback)
	{
		const std::string::size_type offset = styleText.find(fallback);
		if (offset != std::string::npos)
			styleText.replace(offset, fallback.size(), tileUrl);
	}

	ShumateVectorRenderer* renderer = shumate_vector_renderer_new(
		"quiver-osm-dark", styleText.c_str(), &error);
	if (!renderer)
	{
		g_clear_error(&error);
		return nullptr;
	}

	shumate_map_source_set_license(SHUMATE_MAP_SOURCE(renderer),
		"© OpenStreetMap contributors, OpenFreeMap, OpenMapTiles");
	return SHUMATE_MAP_SOURCE(renderer);
}
}

namespace
{
struct WrapMarkerItem
{
	double lat;
	double lon;
	std::vector<GtkWidget*> widgets;
};
}

struct QuiverWrapMarkerLayer
{
	ShumateLayer parent_instance;
	std::vector<WrapMarkerItem>* items;
};

struct QuiverWrapMarkerLayerClass
{
	ShumateLayerClass parent_class;
};

G_DEFINE_TYPE(QuiverWrapMarkerLayer, quiver_wrap_marker_layer,
	SHUMATE_TYPE_LAYER)

static void quiver_wrap_marker_layer_viewport_changed(GObject*,
	GParamSpec*, gpointer data)
{
	gtk_widget_queue_allocate(GTK_WIDGET(data));
}

static void quiver_wrap_marker_layer_clear(QuiverWrapMarkerLayer* self)
{
	GtkWidget* child = NULL;
	while ((child = gtk_widget_get_first_child(GTK_WIDGET(self))) != NULL)
		gtk_widget_unparent(child);
	self->items->clear();
}

static void quiver_wrap_marker_layer_add(QuiverWrapMarkerLayer* self,
	double lat, double lon, std::vector<GtkWidget*> widgets)
{
	for (GtkWidget* widget : widgets)
	{
		if (!widget)
			continue;
		gtk_widget_set_parent(widget, GTK_WIDGET(self));
		gtk_widget_set_child_visible(widget, FALSE);
	}
	self->items->push_back({lat, lon, std::move(widgets)});
	gtk_widget_queue_allocate(GTK_WIDGET(self));
}

static void quiver_wrap_marker_layer_size_allocate(GtkWidget* widget,
	int width, int height, int baseline)
{
	(void)baseline;
	QuiverWrapMarkerLayer* self = (QuiverWrapMarkerLayer*)widget;
	ShumateViewport* viewport = shumate_layer_get_viewport(SHUMATE_LAYER(self));
	if (!viewport || !self->items)
		return;

	const double zoom = shumate_viewport_get_zoom_level(viewport);
	const double worldSize = kTileWorldSize * std::pow(2.0, zoom);
	if (worldSize <= 0.0)
		return;

	for (const WrapMarkerItem& item : *self->items)
	{
		double baseX = 0.0, baseY = 0.0;
		shumate_viewport_location_to_widget_coords(viewport, widget,
			item.lat, item.lon, &baseX, &baseY);
		const int firstCopy = (int)std::ceil(
			(-kWrapCullMargin - baseX) / worldSize);
		const int lastCopy = (int)std::floor(
			((double)width + kWrapCullMargin - baseX) / worldSize);
		for (std::size_t i = 0; i < item.widgets.size(); i++)
		{
			GtkWidget* child = item.widgets[i];
			if (!child)
				continue;
			int childWidth = 0, childHeight = 0;
			gtk_widget_measure(child, GTK_ORIENTATION_HORIZONTAL, -1,
				&childWidth, NULL, NULL, NULL);
			gtk_widget_measure(child, GTK_ORIENTATION_VERTICAL, -1,
				&childHeight, NULL, NULL, NULL);
			const int copy = firstCopy + (int)i;
			const double x = baseX + (double)copy * worldSize;
			const int allocX = (int)std::floor(
				x - (double)childWidth / 2.0);
			const int allocY = (int)std::floor(
				baseY - (double)childHeight / 2.0);
			const bool visible = copy <= lastCopy &&
				allocX > -childWidth && allocX <= width &&
				allocY > -childHeight && allocY <= height &&
				childWidth < width && childHeight < height;
			gtk_widget_set_child_visible(child, visible);
			if (visible)
			{
				GtkAllocation allocation = {
					allocX, allocY, childWidth, childHeight};
				gtk_widget_size_allocate(child, &allocation, -1);
			}
		}
	}
}

static void quiver_wrap_marker_layer_constructed(GObject* object)
{
	G_OBJECT_CLASS(quiver_wrap_marker_layer_parent_class)->constructed(object);
	QuiverWrapMarkerLayer* self = (QuiverWrapMarkerLayer*)object;
	ShumateViewport* viewport = shumate_layer_get_viewport(
		SHUMATE_LAYER(self));
	if (viewport)
	{
		g_signal_connect_object(viewport, "notify",
			G_CALLBACK(quiver_wrap_marker_layer_viewport_changed),
			self, (GConnectFlags)0);
	}
}

static void quiver_wrap_marker_layer_dispose(GObject* object)
{
	QuiverWrapMarkerLayer* self = (QuiverWrapMarkerLayer*)object;
	GtkWidget* child = NULL;
	while ((child = gtk_widget_get_first_child(GTK_WIDGET(self))) != NULL)
		gtk_widget_unparent(child);
	delete self->items;
	self->items = nullptr;
	G_OBJECT_CLASS(quiver_wrap_marker_layer_parent_class)->dispose(object);
}

static void quiver_wrap_marker_layer_class_init(
	QuiverWrapMarkerLayerClass* klass)
{
	GObjectClass* objectClass = G_OBJECT_CLASS(klass);
	GtkWidgetClass* widgetClass = GTK_WIDGET_CLASS(klass);
	objectClass->constructed = quiver_wrap_marker_layer_constructed;
	objectClass->dispose = quiver_wrap_marker_layer_dispose;
	widgetClass->size_allocate = quiver_wrap_marker_layer_size_allocate;
}

static void quiver_wrap_marker_layer_init(QuiverWrapMarkerLayer* self)
{
	self->items = new std::vector<WrapMarkerItem>();
}

static QuiverWrapMarkerLayer* quiver_wrap_marker_layer_new(
	ShumateViewport* viewport)
{
	return (QuiverWrapMarkerLayer*)g_object_new(
		quiver_wrap_marker_layer_get_type(), "viewport", viewport, NULL);
}

#define MAP_DEBUG(...) \
	do { if (MapDebugEnabled()) g_print("[map-debug] " __VA_ARGS__); } while (0)

class QuiverMapView::Impl : public std::enable_shared_from_this<QuiverMapView::Impl>
{
public:
	Impl();
	~Impl();

	GtkWidget* GetWidget() const { return m_Root; }
	void SetImageList(IImageListViewPtr pList);
	IImageListViewPtr GetImageList();
	void Refresh();
	void ScheduleRefresh();
	void CancelBuild();
	void ClearIndex();
	void ResetMetadataProgress();
	void ApplyIndex(std::shared_ptr<QuiverGpsIndex> index, guint64 generation);

	void UpdateMapSource();
	void ApplySelectedSource();
	ShumateMapSource* GetSelectedSource() const;
	void ScheduleClusterRebuild(bool recompute, guint delayMs);
	void RebuildClusterMarkers(bool recompute);
	void RebuildVisibleMarkers();
	std::size_t FindClusterAt(double x, double y) const;
	void ScheduleViewportFilter();
	void CancelViewportFilter();
	void ScheduleWorldFitEnforcement();
	void ZoomIn() { ZoomStep(true, true); }
	void ZoomOut() { ZoomStep(false, true); }
	void ZoomToWorld();
	void ResetViewForImageList();
	void GrabFocus();

	void HighlightURI(const std::string& uri);
	bool HasActiveRegionFilter() const { return m_bHasRegionFilter; }
	void ClearRegionFilterVisual();

	class ListHandler : public IImageListEventHandler
	{
	public:
		explicit ListHandler(Impl* impl) : m_Impl(impl) {}
		void HandleContentsChanged(ImageListEventPtr) override { m_Impl->ScheduleRefresh(); }
		void HandleCurrentIndexChanged(ImageListEventPtr) override {}
		void HandleItemAdded(ImageListEventPtr) override { m_Impl->ScheduleRefresh(); }
		void HandleItemRemoved(ImageListEventPtr) override { m_Impl->ScheduleRefresh(); }
		void HandleItemChanged(ImageListEventPtr) override { m_Impl->ScheduleRefresh(); }
	private:
		Impl* m_Impl;
	};

	UriCallback m_SelectCb;
	UriCallback m_ActivateCb;
	RegionCallback m_RegionCb;
	MetadataProgressCallback m_MetadataProgressCb;
	MetadataReadyCallback m_MetadataReadyCb;

private:
	struct BuildPayload
	{
		std::weak_ptr<Impl>* impl;
		std::shared_ptr<QuiverGpsIndex> index;
		guint64 generation;
	};

	struct ThumbnailPayload
	{
		std::weak_ptr<Impl>* impl;
		std::string uri;
		GdkTexture* texture;
	};

	struct MetadataProgressPayload
	{
		std::shared_ptr<std::weak_ptr<Impl>> impl;
		guint64 generation;
		std::size_t done;
		std::size_t total;
	};

	static gboolean IdleRefresh(gpointer data);
	static gboolean IdleApplyBuild(gpointer data);
	static gboolean IdleMetadataProgress(gpointer data);
	static gboolean IdleRebuildClusters(gpointer data);
	static gboolean IdleApplyViewportFilter(gpointer data);
	static gboolean IdleEnforceWorldFit(gpointer data);
	static gboolean IdleRetryFit(gpointer data);
	static gboolean IdleApplyThumbnail(gpointer data);
	static gboolean ThumbnailAbort(gpointer data);

	static void OnViewportNotify(GObject* object, GParamSpec* pspec, gpointer data);
	static void OnNetworkChanged(GNetworkMonitor* monitor, gboolean available, gpointer data);
	static void OnSourceChanged(GObject* object, GParamSpec* pspec, gpointer data);
	static void OnMapPressed(GtkGestureClick* gesture, int n_press, double x, double y, gpointer data);
	static gboolean OnInputEvent(GtkEventControllerLegacy* controller,
		GdkEvent* event, gpointer data);
	static gboolean OnKeyPressed(GtkEventControllerKey* controller, guint keyval,
		guint keycode, GdkModifierType state, gpointer data);
	static void OnMapMapped(GtkWidget* widget, gpointer data);
	static void OnMapSizeChanged(GObject* object, GParamSpec* pspec, gpointer data);
	void FilterRectangle(double lat1, double lon1, double lat2, double lon2);
	void ZoomStep(bool zoomIn, bool atPointer);
	void ZoomAtPoint(bool zoomIn, double x, double y);
	bool GetPointerInMap(double& x, double& y);
	double GetWorldFitZoom() const;
	void EnforceWorldFit();
	void ScheduleFitRetry();
	bool FitToItems();
	void ClampViewport();
	void ResetToWorldView(bool userInitiated);

	void RequestThumbnail(const std::string& uri);
	void UpdateSelectionVisuals();
	void ThumbnailWorker(std::weak_ptr<Impl> self);
	void ApplyThumbnail(const std::string& uri, GdkTexture* texture);

	GtkWidget* m_Root = nullptr;
	GtkWidget* m_Map = nullptr;
	GtkWidget* m_Controls = nullptr;
	ShumateViewport* m_Viewport = nullptr;
	ShumateLayer* m_TileLayer = nullptr;
	QuiverWrapMarkerLayer* m_Layer = nullptr;
	ShumateMapSourceRegistry* m_Registry = nullptr;
	std::vector<ShumateMapSource*> m_SourceOptions;
	std::vector<std::string> m_SourceLabels;
	GtkWidget* m_SourceDropdown = nullptr;
	int m_iSelectedSource = 0;
	bool m_bApplyingSource = false;
	GNetworkMonitor* m_NetworkMonitor = nullptr;

	std::mutex m_ListMutex;
	IImageListViewPtr m_List;
	IImageListEventHandlerPtr m_ListHandler;

	std::shared_ptr<QuiverGpsIndex> m_Index;
	std::vector<QuiverGpsIndex::Cluster> m_AllClusters;
	std::vector<std::size_t> m_VisibleClusters;
	std::multimap<std::size_t, GtkWidget*> m_MarkerWidgetsByCluster;
	std::map<GtkWidget*, std::size_t> m_ClusterByMarkerWidget;
	double m_LastClusterZoom = -1.0;
	bool m_bClustersDirty = true;
	std::multimap<std::string, GtkWidget*> m_MarkerImages;

	std::thread m_ThumbnailThread;
	std::mutex m_ThumbnailMutex;
	std::condition_variable m_ThumbnailCv;
	std::deque<std::string> m_ThumbnailQueue;
	std::set<std::string> m_ThumbnailQueued;
	std::set<std::string> m_ThumbnailAttempted;
	std::atomic<bool> m_bThumbnailStop{false};

	guint64 m_BuildGeneration = 0;
	std::shared_ptr<std::atomic<bool>> m_BuildCancel;
	std::atomic<bool> m_bBuildRunning{false};
	std::atomic<bool> m_bBuildPending{false};
	guint m_RefreshSourceId = 0;
	guint m_ClusterSourceId = 0;
	guint m_ViewportFilterSourceId = 0;
	guint m_WorldFitSourceId = 0;
	guint m_FitRetrySourceId = 0;
	int m_FitRetryAttempts = 0;
	bool m_bRefreshScheduled = false;
	bool m_bFitToItemsPending = false;
	bool m_bFitToItemsWaitingForIndex = false;
	bool m_bUserViewportChange = false;
	bool m_bPointerPressed = false;
	bool m_bPointerDragged = false;
	double m_PressX = 0.0;
	double m_PressY = 0.0;
	bool m_bEnforcingWorldFit = false;
	bool m_bClampingViewport = false;
	bool m_bClusterRecompute = false;
	GtkEventController* m_KeyController = nullptr;
	GtkWidget* m_KeyControllerWidget = nullptr;

	bool m_bHasRegionFilter = false;
	std::string m_SelectedURI;

	GtkCssProvider* m_CssProvider = nullptr;
};

QuiverMapView::Impl::Impl()
{
	// Marker / control styling is self-contained so the map does not depend on
	// the application's global stylesheet.
	m_CssProvider = gtk_css_provider_new();
	gtk_css_provider_load_from_string(m_CssProvider,
		".quiver-map-marker {"
		"  background-color: #3584e4;"
		"  color: #ffffff;"
		"  border-radius: 999px;"
		"  border: 2px solid #ffffff;"
		"  padding: 1px 7px;"
		"  font-weight: bold;"
		"  font-size: 12px;"
		"  box-shadow: 0 1px 3px rgba(0,0,0,0.45);"
		"}"
		".quiver-map-marker-single {"
		"  background-color: #e01b24;"
		"  min-width: 12px;"
		"  min-height: 12px;"
		"  padding: 0;"
		"  font-size: 0;"
		"}"
		".quiver-map-thumb {"
		"  background-color: #202020;"
		"  border: 2px solid #ffffff;"
		"  border-radius: 7px;"
		"  box-shadow: 0 1px 4px rgba(0,0,0,0.55);"
		"}"
		".quiver-map-thumb-stack {"
		"  box-shadow: 3px 3px 0 rgba(255,255,255,0.90),"
		"              6px 6px 0 rgba(255,255,255,0.45);"
		"}"
		".quiver-map-thumb.quiver-map-selected {"
		"  border-color: #3584e4;"
		"  box-shadow: 0 0 0 3px rgba(53,132,228,0.70),"
		"              0 1px 4px rgba(0,0,0,0.55);"
		"}"
		".quiver-map-count {"
		"  background-color: #3584e4;"
		"  color: #ffffff;"
		"  border: 1px solid #ffffff;"
		"  border-radius: 999px;"
		"  padding: 0 5px;"
		"  font-weight: bold;"
		"  font-size: 11px;"
		"  box-shadow: 0 1px 3px rgba(0,0,0,0.55);"
		"}"
		".quiver-map-controls {"
		"  background-color: rgba(24,24,24,0.55);"
		"  border-radius: 10px;"
		"  padding: 6px;"
		"}"
		".quiver-map-controls button {"
		"  margin: 2px;"
		"}");
	GdkDisplay* display = gdk_display_get_default();
	if (display)
	{
		gtk_style_context_add_provider_for_display(display,
			GTK_STYLE_PROVIDER(m_CssProvider),
			GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	}

	m_Map = GTK_WIDGET(shumate_map_new());
	gtk_widget_set_hexpand(m_Map, TRUE);
	gtk_widget_set_vexpand(m_Map, TRUE);

	m_Viewport = shumate_map_get_viewport(SHUMATE_MAP(m_Map));
	shumate_viewport_set_min_zoom_level(m_Viewport, 0);
	shumate_viewport_set_max_zoom_level(m_Viewport, 18);

	m_Registry = shumate_map_source_registry_new_with_defaults();

	// Offline is first and the default: it is bundled with Quiver, so the map
	// is never blank just because the network is unavailable or a tile server
	// is unreachable.
	auto addSource = [this](const char* label, ShumateMapSource* source,
		bool takeOwnership) {
		if (!source)
			return;
		if (!takeOwnership)
			g_object_ref(source);
		m_SourceOptions.push_back(source);
		m_SourceLabels.push_back(label);
	};
	addSource("Offline (bundled)", quiver_offline_tile_source_new(), true);
	if (m_Registry)
	{
		addSource("OpenStreetMap", shumate_map_source_registry_get_by_id(m_Registry,
			SHUMATE_MAP_SOURCE_OSM_MAPNIK), false);
		addSource("OSM Cycle Map", shumate_map_source_registry_get_by_id(m_Registry,
			SHUMATE_MAP_SOURCE_OSM_CYCLE_MAP), false);
		addSource("OSM Transport Map", shumate_map_source_registry_get_by_id(m_Registry,
			SHUMATE_MAP_SOURCE_OSM_TRANSPORT_MAP), false);
	}
	addSource("OSM Dark (OpenFreeMap)", CreateOsmDarkSource(), true);
	addSource("Google Maps (unofficial)", quiver_raster_tile_source_new(
		"quiver-google", "Google Maps (unofficial)", "Google",
		"https://mt1.google.com/vt/lyrs=m&x={x}&y={y}&z={z}", 0, 20), true);

	const std::string savedSource = Preferences::GetInstance()->GetString(
		QUIVER_PREFS_MAP, QUIVER_PREFS_MAP_SOURCE, "");
	if (!savedSource.empty())
	{
		for (std::size_t i = 0; i < m_SourceOptions.size(); ++i)
		{
			const char* id = shumate_map_source_get_id(m_SourceOptions[i]);
			if (id && savedSource == id)
			{
				m_iSelectedSource = (int)i;
				break;
			}
		}
	}

	m_NetworkMonitor = g_network_monitor_get_default();
	if (m_NetworkMonitor)
	{
		g_signal_connect_data(m_NetworkMonitor, "network-changed",
			G_CALLBACK(OnNetworkChanged), this, NULL, (GConnectFlags)0);
	}

	m_Layer = quiver_wrap_marker_layer_new(m_Viewport);
	shumate_map_add_layer(SHUMATE_MAP(m_Map), SHUMATE_LAYER(m_Layer));

	/* ShumateMap itself is just a container: without a ShumateMapLayer it
	 * draws no tiles at all.  The marker layer above is why markers appeared
	 * even though the map background stayed blank. */
	ApplySelectedSource();

	shumate_map_go_to_full_with_duration(SHUMATE_MAP(m_Map), 20.0, 0.0, 2.0, 0);
	shumate_map_set_zoom_on_double_click(SHUMATE_MAP(m_Map), FALSE);

	/* One gesture for all markers: hit-testing against the cluster screen
	 * positions is cheaper and much more reliable than 15k child gestures. */
	GtkGesture* mapClick = gtk_gesture_click_new();
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(mapClick), GDK_BUTTON_PRIMARY);
	gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(mapClick),
		GTK_PHASE_CAPTURE);
	g_signal_connect(mapClick, "pressed", G_CALLBACK(OnMapPressed), this);
	gtk_widget_add_controller(m_Map, GTK_EVENT_CONTROLLER(mapClick));

	GtkEventController* inputObserver = gtk_event_controller_legacy_new();
	gtk_event_controller_set_propagation_phase(inputObserver, GTK_PHASE_CAPTURE);
	g_signal_connect(inputObserver, "event", G_CALLBACK(OnInputEvent), this);
	gtk_widget_add_controller(m_Map, inputObserver);

	m_Root = gtk_overlay_new();
	g_object_ref_sink(m_Root);
	gtk_widget_set_focusable(m_Map, TRUE);
	gtk_overlay_set_child(GTK_OVERLAY(m_Root), m_Map);
	g_signal_connect(m_Root, "map", G_CALLBACK(OnMapMapped), this);

	m_Controls = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
	gtk_widget_add_css_class(m_Controls, "quiver-map-controls");
	gtk_widget_set_halign(m_Controls, GTK_ALIGN_END);
	gtk_widget_set_valign(m_Controls, GTK_ALIGN_START);
	gtk_widget_set_margin_top(m_Controls, 12);
	gtk_widget_set_margin_end(m_Controls, 12);

	if (!m_SourceLabels.empty())
	{
		std::vector<const char*> labels;
		for (const std::string& label : m_SourceLabels)
			labels.push_back(label.c_str());
		labels.push_back(NULL);
		m_SourceDropdown = gtk_drop_down_new_from_strings(labels.data());
		gtk_widget_set_tooltip_text(m_SourceDropdown,
			"Choose the map provider (offline data is always available)");
		gtk_drop_down_set_selected(GTK_DROP_DOWN(m_SourceDropdown),
			(guint)m_iSelectedSource);
		g_signal_connect(m_SourceDropdown, "notify::selected",
			G_CALLBACK(OnSourceChanged), this);
		gtk_box_append(GTK_BOX(m_Controls), m_SourceDropdown);
	}

	gtk_overlay_add_overlay(GTK_OVERLAY(m_Root), m_Controls);

	g_signal_connect_data(m_Viewport, "notify::zoom-level",
		G_CALLBACK(OnViewportNotify), this, NULL, (GConnectFlags)0);
	g_signal_connect_data(m_Viewport, "notify::latitude",
		G_CALLBACK(OnViewportNotify), this, NULL, (GConnectFlags)0);
	g_signal_connect_data(m_Viewport, "notify::longitude",
		G_CALLBACK(OnViewportNotify), this, NULL, (GConnectFlags)0);
	g_signal_connect(m_Map, "notify::width", G_CALLBACK(OnMapSizeChanged), this);
	g_signal_connect(m_Map, "notify::height", G_CALLBACK(OnMapSizeChanged), this);
}

QuiverMapView::Impl::~Impl()
{
	m_bThumbnailStop = true;
	m_ThumbnailCv.notify_all();
	if (m_ThumbnailThread.joinable())
		m_ThumbnailThread.join();

	if (m_KeyController)
	{
		g_object_remove_weak_pointer(G_OBJECT(m_KeyController),
			(gpointer*)&m_KeyController);
		g_signal_handlers_disconnect_by_data(m_KeyController, this);
		if (m_KeyControllerWidget)
		{
			g_object_remove_weak_pointer(G_OBJECT(m_KeyControllerWidget),
				(gpointer*)&m_KeyControllerWidget);
			gtk_widget_remove_controller(m_KeyControllerWidget, m_KeyController);
		}
		m_KeyController = nullptr;
		m_KeyControllerWidget = nullptr;
	}
	else if (m_KeyControllerWidget)
	{
		g_object_remove_weak_pointer(G_OBJECT(m_KeyControllerWidget),
			(gpointer*)&m_KeyControllerWidget);
		m_KeyControllerWidget = nullptr;
	}

	g_signal_handlers_disconnect_by_data(m_Viewport, this);
	if (m_NetworkMonitor)
		g_signal_handlers_disconnect_by_data(m_NetworkMonitor, this);

	if (m_RefreshSourceId != 0)
	{
		g_source_remove(m_RefreshSourceId);
		m_RefreshSourceId = 0;
	}
	if (m_ClusterSourceId != 0)
	{
		g_source_remove(m_ClusterSourceId);
		m_ClusterSourceId = 0;
	}
	if (m_ViewportFilterSourceId != 0)
	{
		g_source_remove(m_ViewportFilterSourceId);
		m_ViewportFilterSourceId = 0;
	}
	if (m_WorldFitSourceId != 0)
	{
		g_source_remove(m_WorldFitSourceId);
		m_WorldFitSourceId = 0;
	}
	if (m_FitRetrySourceId != 0)
	{
		g_source_remove(m_FitRetrySourceId);
		m_FitRetrySourceId = 0;
	}

	{
		std::lock_guard<std::mutex> lock(m_ListMutex);
		if (m_List && m_ListHandler)
			m_List->RemoveEventHandler(m_ListHandler);
		m_List.reset();
	}
	m_ListHandler.reset();

	if (m_TileLayer && m_Map)
	{
		shumate_map_remove_layer(SHUMATE_MAP(m_Map), SHUMATE_LAYER(m_TileLayer));
		g_object_unref(m_TileLayer);
		m_TileLayer = nullptr;
	}

	for (ShumateMapSource* source : m_SourceOptions)
	{
		if (source)
			g_object_unref(source);
	}
	m_SourceOptions.clear();
	if (m_Registry)
		g_object_unref(m_Registry);

	/* Signals are gone, so it is safe to let GTK tear down the map widgets
	 * now.  Doing this here (rather than leaving the overlay parented) keeps
	 * the map's Shumate objects from outliving this Impl. */
	if (m_Root)
	{
		if (gtk_widget_get_parent(m_Root))
			gtk_widget_unparent(m_Root);
		g_object_unref(m_Root);
		m_Root = nullptr;
	}

	if (m_CssProvider)
	{
		GdkDisplay* display = gdk_display_get_default();
		if (display)
		{
			gtk_style_context_remove_provider_for_display(display,
				GTK_STYLE_PROVIDER(m_CssProvider));
		}
		g_object_unref(m_CssProvider);
	}
}

void QuiverMapView::Impl::SetImageList(IImageListViewPtr pList)
{
	bool changed = false;
	{
		std::lock_guard<std::mutex> lock(m_ListMutex);
		if (m_List != pList)
		{
			if (m_List && m_ListHandler)
				m_List->RemoveEventHandler(m_ListHandler);
			m_List = pList;
			if (m_List)
			{
				if (!m_ListHandler)
					m_ListHandler = IImageListEventHandlerPtr(new ListHandler(this));
				m_List->AddEventHandler(m_ListHandler);
			}
			changed = true;
		}
	}
	MAP_DEBUG("SetImageList list=%p size=%u changed=%d index=%p\n",
		(void*)pList.get(), pList ? pList->GetSize() : 0u, changed ? 1 : 0,
		(void*)m_Index.get());
	if (changed || !m_Index)
	{
		m_bFitToItemsPending = true;
		m_bFitToItemsWaitingForIndex = true;
		if (changed)
			ClearIndex();
		Refresh();
	}
}

void QuiverMapView::Impl::ResetViewForImageList()
{
	MAP_DEBUG("ResetViewForImageList build=%" G_GUINT64_FORMAT "\n",
		m_BuildGeneration + 1);
	m_bFitToItemsPending = true;
	m_bFitToItemsWaitingForIndex = true;
	++m_BuildGeneration;
	/* The new list has a different definition, so the old markers no longer
	 * describe it.  Drop them now rather than showing stale pins until the
	 * replacement scan finishes. */
	CancelBuild();
	ClearIndex();
	ResetMetadataProgress();
	ScheduleRefresh();
}

void QuiverMapView::Impl::CancelBuild()
{
	if (m_BuildCancel)
		m_BuildCancel->store(true, std::memory_order_relaxed);
}

void QuiverMapView::Impl::ClearIndex()
{
	m_Index.reset();
	m_AllClusters.clear();
	m_VisibleClusters.clear();
	m_LastClusterZoom = -1.0;
	m_bClustersDirty = true;
	RebuildVisibleMarkers();
}

void QuiverMapView::Impl::ResetMetadataProgress()
{
	if (m_MetadataProgressCb)
		m_MetadataProgressCb(0, 0);
}

IImageListViewPtr QuiverMapView::Impl::GetImageList()
{
	std::lock_guard<std::mutex> lock(m_ListMutex);
	return m_List;
}

void QuiverMapView::Impl::ScheduleRefresh()
{
	/* A large list emits many add/change events while it is loading.  Wait
	 * for the burst to settle before rescanning every file for GPS data. */
	MAP_DEBUG("ScheduleRefresh build=%" G_GUINT64_FORMAT " (was pending=%d)\n",
		m_BuildGeneration, m_bRefreshScheduled ? 1 : 0);
	if (m_RefreshSourceId != 0)
		g_source_remove(m_RefreshSourceId);
	m_bRefreshScheduled = true;
	m_RefreshSourceId = g_timeout_add_full(G_PRIORITY_DEFAULT_IDLE, 500,
		&Impl::IdleRefresh, this, NULL);
}

gboolean QuiverMapView::Impl::IdleRefresh(gpointer data)
{
	Impl* self = static_cast<Impl*>(data);
	self->m_bRefreshScheduled = false;
	self->m_RefreshSourceId = 0;
	self->Refresh();
	return G_SOURCE_REMOVE;
}

void QuiverMapView::Impl::Refresh()
{
	if (m_RefreshSourceId != 0)
	{
		g_source_remove(m_RefreshSourceId);
		m_RefreshSourceId = 0;
		m_bRefreshScheduled = false;
	}

	/* Any progress shown belongs to the scan being superseded.  Clear it so
	 * the pill cannot sit on the previous list's fraction, especially when a
	 * 60000-item scan is still unwinding. */
	ResetMetadataProgress();

	/* One full metadata scan at a time.  Item-change bursts used to start a
	 * new detached Rebuild every 500 ms while the previous one was still
	 * reading EXIF, multiplying both scanning time and peak memory.  Mark
	 * the running build stale, remember that another pass is needed, and let
	 * IdleApplyBuild launch it once the current worker has finished.  The
	 * worker polls the cancel token, so "finished" means it abandons the old
	 * list almost immediately instead of scanning all 60000 files first. */
	if (m_bBuildRunning.load(std::memory_order_acquire))
	{
		CancelBuild();
		++m_BuildGeneration;
		m_bBuildPending.store(true, std::memory_order_release);
		MAP_DEBUG("Refresh deferred: build in progress, next=%" G_GUINT64_FORMAT "\n",
			m_BuildGeneration);
		return;
	}

	IImageListViewPtr list;
	{
		std::lock_guard<std::mutex> lock(m_ListMutex);
		list = m_List;
	}

	const guint64 generation = ++m_BuildGeneration;
	const int width = m_Map ? gtk_widget_get_width(m_Map) : 0;
	const int height = m_Map ? gtk_widget_get_height(m_Map) : 0;
	const bool mapped = m_Map && gtk_widget_get_mapped(m_Map);
	MAP_DEBUG("Refresh build=%" G_GUINT64_FORMAT " list=%p size=%u mapped=%d "
		"map=%dx%d fitPending=%d fitWaiting=%d\n",
		generation, (void*)list.get(), list ? list->GetSize() : 0u,
		mapped ? 1 : 0, width, height,
		m_bFitToItemsPending ? 1 : 0, m_bFitToItemsWaitingForIndex ? 1 : 0);

	if (!list)
	{
		ClearIndex();
		m_bFitToItemsWaitingForIndex = false;
		if (m_bFitToItemsPending)
			FitToItems();
		if (m_MetadataReadyCb)
			m_MetadataReadyCb(true);
		MAP_DEBUG("Refresh build=%" G_GUINT64_FORMAT " empty list -> metadata ready\n",
			generation);
		return;
	}

	/* The list can be rewritten on this thread (an async folder load
	 * committing, a filter change) while the worker scans it.  Take the
	 * QuiverFile snapshot here, where the list is owned, so the worker never
	 * touches the live container. */
	std::vector<QuiverFile> files;
	const unsigned int listSize = list->GetSize();
	files.reserve(listSize);
	for (unsigned int i = 0; i < listSize; i++)
		files.push_back(list->Get(i));

	auto index = std::make_shared<QuiverGpsIndex>();
	BuildPayload* payload = new BuildPayload();
	payload->impl = new std::weak_ptr<Impl>(shared_from_this());
	payload->index = index;
	payload->generation = generation;

	auto weakImpl = std::make_shared<std::weak_ptr<Impl>>(shared_from_this());
	auto cancel = std::make_shared<std::atomic<bool>>(false);
	m_BuildCancel = cancel;
	m_bBuildRunning.store(true, std::memory_order_release);
	std::thread([files = std::move(files), index, payload, weakImpl, generation,
		cancel]() {
		index->Rebuild(files, [weakImpl, generation](std::size_t done,
			std::size_t total) {
			auto* p = new MetadataProgressPayload();
			p->impl = weakImpl;
			p->generation = generation;
			p->done = done;
			p->total = total;
			g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,
				&Impl::IdleMetadataProgress, p, NULL);
		}, [cancel]() {
			return cancel->load(std::memory_order_relaxed);
		});
		g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, &Impl::IdleApplyBuild, payload, NULL);
	}).detach();
}

gboolean QuiverMapView::Impl::IdleApplyBuild(gpointer data)
{
	BuildPayload* payload = static_cast<BuildPayload*>(data);
	std::shared_ptr<Impl> impl = payload->impl->lock();
	delete payload->impl;
	if (impl)
	{
		impl->m_bBuildRunning.store(false, std::memory_order_release);
		const bool pending = impl->m_bBuildPending.exchange(
			false, std::memory_order_acq_rel);
		impl->ApplyIndex(payload->index, payload->generation);
		if (pending)
		{
			/* Let the list event burst settle before scanning again. */
			impl->ScheduleRefresh();
		}
	}
	delete payload;
	return G_SOURCE_REMOVE;
}

gboolean QuiverMapView::Impl::IdleMetadataProgress(gpointer data)
{
	MetadataProgressPayload* payload = static_cast<MetadataProgressPayload*>(data);
	std::shared_ptr<Impl> impl = payload->impl->lock();
	if (impl && payload->generation == impl->m_BuildGeneration &&
		impl->m_MetadataProgressCb)
	{
		impl->m_MetadataProgressCb(payload->done, payload->total);
	}
	delete payload;
	return G_SOURCE_REMOVE;
}

void QuiverMapView::Impl::ApplyIndex(std::shared_ptr<QuiverGpsIndex> index, guint64 generation)
{
	const std::size_t points = index ? index->GetPointCount() : 0;
	MAP_DEBUG("ApplyIndex build=%" G_GUINT64_FORMAT " current=%" G_GUINT64_FORMAT
		" points=%zu\n", generation, m_BuildGeneration, points);
	if (generation != m_BuildGeneration)
	{
		MAP_DEBUG("ApplyIndex build=%" G_GUINT64_FORMAT " discarded (stale)\n",
			generation);
		return;
	}
	m_Index = index;
	m_bClustersDirty = true;
	m_bFitToItemsWaitingForIndex = false;
	RebuildClusterMarkers(true);
	if (m_bFitToItemsPending)
		FitToItems();
	if (m_MetadataReadyCb)
		m_MetadataReadyCb(true);
	MAP_DEBUG("ApplyIndex build=%" G_GUINT64_FORMAT " applied points=%zu "
		"clusters=%zu\n", generation, points, m_AllClusters.size());
}

ShumateMapSource* QuiverMapView::Impl::GetSelectedSource() const
{
	if (m_iSelectedSource < 0 ||
		(std::size_t)m_iSelectedSource >= m_SourceOptions.size())
		return m_SourceOptions.empty() ? nullptr : m_SourceOptions.front();
	return m_SourceOptions[(std::size_t)m_iSelectedSource];
}

void QuiverMapView::Impl::ApplySelectedSource()
{
	if (!m_Map)
		return;
	ShumateMapSource* source = GetSelectedSource();
	if (!source)
		return;

	if (m_Viewport)
		shumate_viewport_set_reference_map_source(m_Viewport, source);

	if (m_TileLayer)
	{
		shumate_map_remove_layer(SHUMATE_MAP(m_Map), SHUMATE_LAYER(m_TileLayer));
		g_object_unref(m_TileLayer);
		m_TileLayer = nullptr;
	}

	m_TileLayer = quiver_raster_map_layer_new(source, m_Viewport);
	g_object_ref_sink(m_TileLayer);
	if (m_Layer)
	{
		shumate_map_insert_layer_behind(SHUMATE_MAP(m_Map),
			SHUMATE_LAYER(m_TileLayer), SHUMATE_LAYER(m_Layer));
	}
	else
	{
		shumate_map_add_layer(SHUMATE_MAP(m_Map), SHUMATE_LAYER(m_TileLayer));
	}
}

void QuiverMapView::Impl::UpdateMapSource()
{
	if (!m_Map)
		return;

	// If the user picked an online provider and the network has gone away,
	// fall back to the bundled source rather than showing an empty map.
	if (m_NetworkMonitor && m_iSelectedSource != 0 &&
		!g_network_monitor_get_network_available(m_NetworkMonitor))
	{
		if (m_SourceDropdown)
		{
			m_bApplyingSource = true;
			gtk_drop_down_set_selected(GTK_DROP_DOWN(m_SourceDropdown), 0);
			m_bApplyingSource = false;
			return;
		}
		m_iSelectedSource = 0;
	}

	ApplySelectedSource();
}

void QuiverMapView::Impl::OnNetworkChanged(GNetworkMonitor*, gboolean, gpointer data)
{
	static_cast<Impl*>(data)->UpdateMapSource();
}

void QuiverMapView::Impl::OnSourceChanged(GObject* object, GParamSpec*, gpointer data)
{
	Impl* self = static_cast<Impl*>(data);
	const guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(object));
	if (selected == GTK_INVALID_LIST_POSITION)
		return;
	self->m_iSelectedSource = (int)selected;
	self->ApplySelectedSource();

	if (!self->m_bApplyingSource)
	{
		ShumateMapSource* source = self->GetSelectedSource();
		const char* id = source ? shumate_map_source_get_id(source) : nullptr;
		if (id && *id)
		{
			Preferences::GetInstance()->SetString(QUIVER_PREFS_MAP,
				QUIVER_PREFS_MAP_SOURCE, id);
		}
	}
}

void QuiverMapView::Impl::OnViewportNotify(GObject*, GParamSpec* pspec, gpointer data)
{
	Impl* self = static_cast<Impl*>(data);
	self->ClampViewport();
	if (MapDebugVerbose())
	{
		MAP_DEBUG("ViewportNotify %s mapped=%d size=%dx%d zoom=%f "
			"center=[%f,%f] userChange=%d fitPending=%d\n",
			pspec ? pspec->name : "(null)",
			self->m_Map && gtk_widget_get_mapped(self->m_Map) ? 1 : 0,
			self->m_Map ? gtk_widget_get_width(self->m_Map) : 0,
			self->m_Map ? gtk_widget_get_height(self->m_Map) : 0,
			self->m_Viewport ? shumate_viewport_get_zoom_level(self->m_Viewport) : 0.0,
			self->m_Viewport ? shumate_location_get_latitude(
				SHUMATE_LOCATION(self->m_Viewport)) : 0.0,
			self->m_Viewport ? shumate_location_get_longitude(
				SHUMATE_LOCATION(self->m_Viewport)) : 0.0,
			self->m_bUserViewportChange ? 1 : 0,
			self->m_bFitToItemsPending ? 1 : 0);
	}
	/* Clustering only depends on zoom.  Panning just changes which markers
	 * are near the viewport, so it can reuse the cached clusters. */
	if (pspec && g_strcmp0(pspec->name, "zoom-level") == 0)
	{
		self->ScheduleClusterRebuild(true, 60);
		const int width = self->m_Map ? gtk_widget_get_width(self->m_Map) : 0;
		const int height = self->m_Map ? gtk_widget_get_height(self->m_Map) : 0;
		const double zoom = self->m_Viewport
			? shumate_viewport_get_zoom_level(self->m_Viewport) : 0.0;
		if (!self->m_bEnforcingWorldFit && width > 0 && height > 0 &&
			zoom < self->GetWorldFitZoom() - 1e-6)
		{
			/* A fast scroll/pinch can undershoot the world-fit zoom before
			 * the idle enforcement runs.  Correct it in the same notify so
			 * a frame with a tiny repeated world is never presented. */
			self->EnforceWorldFit();
		}
		else
		{
			self->ScheduleWorldFitEnforcement();
		}
	}
	else
	{
		self->ScheduleClusterRebuild(false, 100);
	}

	if (self->m_bUserViewportChange)
		self->ScheduleViewportFilter();
}

void QuiverMapView::Impl::ScheduleViewportFilter()
{
	const bool mapped = m_Map && gtk_widget_get_mapped(m_Map);
	if (MapDebugVerbose())
	{
		MAP_DEBUG("ScheduleViewportFilter cb=%d map=%p mapped=%d pending=%d\n",
			m_RegionCb ? 1 : 0, (void*)m_Map, mapped ? 1 : 0,
			m_ViewportFilterSourceId != 0 ? 1 : 0);
	}
	if (!m_RegionCb || !m_Map || !mapped)
		return;
	if (m_ViewportFilterSourceId != 0)
		g_source_remove(m_ViewportFilterSourceId);
	m_ViewportFilterSourceId = g_timeout_add_full(G_PRIORITY_DEFAULT, 250,
		&Impl::IdleApplyViewportFilter, this, NULL);
}

void QuiverMapView::Impl::CancelViewportFilter()
{
	if (m_ViewportFilterSourceId != 0)
	{
		g_source_remove(m_ViewportFilterSourceId);
		m_ViewportFilterSourceId = 0;
	}
	m_bUserViewportChange = false;
}

gboolean QuiverMapView::Impl::IdleApplyViewportFilter(gpointer data)
{
	Impl* self = static_cast<Impl*>(data);
	self->m_ViewportFilterSourceId = 0;
	if (!self->m_bUserViewportChange)
		return G_SOURCE_REMOVE;

	const int width = self->m_Map ? gtk_widget_get_width(self->m_Map) : 0;
	const int height = self->m_Map ? gtk_widget_get_height(self->m_Map) : 0;
	if (!self->m_Map || !self->m_Viewport || !self->m_RegionCb ||
		width <= 0 || height <= 0)
	{
		if (MapDebugVerbose())
		{
			MAP_DEBUG("IdleApplyViewportFilter skipped map=%p viewport=%p cb=%d "
				"size=%dx%d\n", (void*)self->m_Map, (void*)self->m_Viewport,
				self->m_RegionCb ? 1 : 0, width, height);
		}
		return G_SOURCE_REMOVE;
	}

	double latTop = 0.0, lonLeft = 0.0, latBottom = 0.0, lonRight = 0.0;
	shumate_viewport_widget_coords_to_location(self->m_Viewport, self->m_Map,
		0.0, 0.0, &latTop, &lonLeft);
	shumate_viewport_widget_coords_to_location(self->m_Viewport, self->m_Map,
		(double)width, (double)height, &latBottom, &lonRight);
	if (MapDebugVerbose())
	{
		MAP_DEBUG("IdleApplyViewportFilter raw top=%f left=%f bottom=%f right=%f "
			"size=%dx%d zoom=%f worldFit=%f\n",
			latTop, lonLeft, latBottom, lonRight, width, height,
			shumate_viewport_get_zoom_level(self->m_Viewport),
			self->GetWorldFitZoom());
	}

	self->m_bUserViewportChange = false;
	self->FilterRectangle(latTop, lonLeft, latBottom, lonRight);
	return G_SOURCE_REMOVE;
}

void QuiverMapView::Impl::ScheduleWorldFitEnforcement()
{
	if (m_WorldFitSourceId != 0)
		g_source_remove(m_WorldFitSourceId);
	m_WorldFitSourceId = g_timeout_add_full(G_PRIORITY_HIGH_IDLE, 0,
		&Impl::IdleEnforceWorldFit, this, NULL);
}

gboolean QuiverMapView::Impl::IdleEnforceWorldFit(gpointer data)
{
	Impl* self = static_cast<Impl*>(data);
	self->m_WorldFitSourceId = 0;
	self->EnforceWorldFit();
	return G_SOURCE_REMOVE;
}

void QuiverMapView::Impl::ScheduleFitRetry()
{
	if (!m_bFitToItemsPending || m_FitRetrySourceId != 0)
		return;
	m_FitRetryAttempts = 0;
	m_FitRetrySourceId = g_timeout_add_full(G_PRIORITY_DEFAULT, 16,
		&Impl::IdleRetryFit, this, NULL);
	MAP_DEBUG("ScheduleFitRetry pending=%d\n", m_bFitToItemsPending ? 1 : 0);
}

gboolean QuiverMapView::Impl::IdleRetryFit(gpointer data)
{
	Impl* self = static_cast<Impl*>(data);
	self->m_FitRetrySourceId = 0;
	if (!self->m_bFitToItemsPending)
		return G_SOURCE_REMOVE;

	self->m_FitRetryAttempts++;
	if (self->FitToItems())
	{
		self->m_FitRetryAttempts = 0;
		return G_SOURCE_REMOVE;
	}
	if (self->m_FitRetryAttempts >= 40)
	{
		MAP_DEBUG("ScheduleFitRetry giving up attempts=%d\n",
			self->m_FitRetryAttempts);
		return G_SOURCE_REMOVE;
	}
	self->m_FitRetrySourceId = g_timeout_add_full(G_PRIORITY_DEFAULT, 50,
		&Impl::IdleRetryFit, self, NULL);
	return G_SOURCE_REMOVE;
}

void QuiverMapView::Impl::ClampViewport()
{
	if (m_bClampingViewport || !m_bUserViewportChange ||
		!m_Map || !m_Viewport)
	{
		return;
	}
	const int width = gtk_widget_get_width(m_Map);
	const int height = gtk_widget_get_height(m_Map);
	if (width <= 0 || height <= 0)
		return;

	const double zoom = shumate_viewport_get_zoom_level(m_Viewport);
	const double worldSize = kTileWorldSize * std::pow(2.0, zoom);
	const double lat = shumate_location_get_latitude(
		SHUMATE_LOCATION(m_Viewport));
	const double lon = shumate_location_get_longitude(
		SHUMATE_LOCATION(m_Viewport));
	const double y = LatToWorldY(lat, worldSize);
	const double halfHeight = (double)height / 2.0;
	const double clampedY = worldSize <= (double)height
		? worldSize / 2.0
		: std::clamp(y, halfHeight, worldSize - halfHeight);
	const double clampedLat = WorldYToLat(clampedY, worldSize);
	if (std::fabs(clampedLat - lat) < 1e-6)
		return;

	m_bClampingViewport = true;
	if (m_bUserViewportChange)
		shumate_map_stop_go_to(SHUMATE_MAP(m_Map));
	shumate_location_set_location(SHUMATE_LOCATION(m_Viewport),
		clampedLat, lon);
	m_bClampingViewport = false;
	if (MapDebugVerbose())
	{
		MAP_DEBUG("ClampViewport center=[%f,%f] -> [%f,%f] zoom=%f "
			"size=%dx%d\n", lat, lon, clampedLat, lon, zoom,
			width, height);
	}
}

double QuiverMapView::Impl::GetWorldFitZoom() const
{
	if (!m_Map || !m_Viewport)
		return 0.0;
	const int width = gtk_widget_get_width(m_Map);
	const int height = gtk_widget_get_height(m_Map);
	if (width <= 0)
		return (double)shumate_viewport_get_min_zoom_level(m_Viewport);
	const int limitingSize = height > 0 ? std::min(width, height) : width;
	const double fit = std::log2((double)limitingSize / kTileWorldSize);
	return std::max(fit, (double)shumate_viewport_get_min_zoom_level(m_Viewport));
}

bool QuiverMapView::Impl::FitToItems()
{
	if (!m_Map || !m_Viewport || m_bFitToItemsWaitingForIndex)
	{
		MAP_DEBUG("FitToItems deferred map=%p viewport=%p waitingForIndex=%d\n",
			(void*)m_Map, (void*)m_Viewport,
			m_bFitToItemsWaitingForIndex ? 1 : 0);
		return false;
	}

	bool hasList = false;
	{
		std::lock_guard<std::mutex> lock(m_ListMutex);
		hasList = (bool)m_List;
	}
	if (!m_Index && !hasList)
	{
		m_bFitToItemsPending = false;
		MAP_DEBUG("FitToItems no list/index -> done\n");
		return true;
	}

	const int width = gtk_widget_get_width(m_Map);
	const int height = gtk_widget_get_height(m_Map);
	if (width <= 0 || height <= 0)
	{
		MAP_DEBUG("FitToItems deferred map size=%dx%d\n", width, height);
		return false;
	}

	const double minZoom = GetWorldFitZoom();
	const double maxZoom = (double)shumate_viewport_get_max_zoom_level(m_Viewport);
	double targetZoom = minZoom;
	double centerLat = 0.0;
	double centerLon = 0.0;

	if (m_Index)
	{
		const std::vector<QuiverGpsIndex::Point> points = m_Index->GetPoints();
		if (!points.empty())
		{
			double minLat = 90.0;
			double maxLat = -90.0;
			double minLon = 180.0;
			double maxLon = -180.0;
			std::vector<double> lons;
			lons.reserve(points.size());
			for (const auto& point : points)
			{
				const double lat = std::clamp(point.lat,
					SHUMATE_MIN_LATITUDE, SHUMATE_MAX_LATITUDE);
				const double lon = std::clamp(point.lon, -180.0, 180.0);
				minLat = std::min(minLat, lat);
				maxLat = std::max(maxLat, lat);
				minLon = std::min(minLon, lon);
				maxLon = std::max(maxLon, lon);
				lons.push_back(lon);
			}

			std::sort(lons.begin(), lons.end());
			double lonSpan = 0.0;
			if (lons.size() == 1)
			{
				centerLon = lons.front();
			}
			else
			{
				double maxGap = -1.0;
				std::size_t maxGapIndex = 0;
				for (std::size_t i = 0; i + 1 < lons.size(); i++)
				{
					const double gap = lons[i + 1] - lons[i];
					if (gap > maxGap)
					{
						maxGap = gap;
						maxGapIndex = i;
					}
				}
				const double wrapGap = lons.front() + 360.0 - lons.back();
				if (wrapGap > maxGap)
				{
					maxGap = wrapGap;
					maxGapIndex = lons.size() - 1;
				}
				lonSpan = 360.0 - maxGap;
				const double left = maxGapIndex + 1 < lons.size()
					? lons[maxGapIndex + 1] : lons.front();
				centerLon = left + lonSpan / 2.0;
				while (centerLon < -180.0)
					centerLon += 360.0;
				while (centerLon > 180.0)
					centerLon -= 360.0;
			}

			const double yTop = LatToWorldY(maxLat, kTileWorldSize);
			const double yBottom = LatToWorldY(minLat, kTileWorldSize);
			const double ySpan = std::max(0.0, yBottom - yTop);
			const double xSpan = lonSpan / 360.0 * kTileWorldSize;
			const double usableWidth = std::max(1.0,
				(double)width - 2.0 * kFitPaddingPx);
			const double usableHeight = std::max(1.0,
				(double)height - 2.0 * kFitPaddingPx);
			const double fitX = xSpan > 1e-9
				? std::log2(usableWidth / xSpan)
				: std::numeric_limits<double>::infinity();
			const double fitY = ySpan > 1e-9
				? std::log2(usableHeight / ySpan)
				: std::numeric_limits<double>::infinity();
			double fitZoom = std::min(fitX, fitY);
			if (!std::isfinite(fitZoom))
				fitZoom = kSingleItemZoom;
			targetZoom = std::clamp(fitZoom, minZoom, maxZoom);
			centerLat = WorldYToLat((yTop + yBottom) / 2.0, kTileWorldSize);
			if (targetZoom <= minZoom + 1e-6)
				centerLon = 0.0;

			MAP_DEBUG("FitToItems points=%zu lat=[%f,%f] lon=[%f,%f] "
				"lonSpan=%f center=[%f,%f] size=%dx%d usable=%fx%f "
				"zoom=%f (fit=%f min=%f max=%f)\n",
				points.size(), minLat, maxLat, minLon, maxLon, lonSpan,
				centerLat, centerLon, width, height, usableWidth,
				usableHeight, targetZoom, fitZoom, minZoom, maxZoom);
		}
		else
		{
			MAP_DEBUG("FitToItems index has no GPS points size=%dx%d\n",
				width, height);
		}
	}
	else
	{
		MAP_DEBUG("FitToItems no index yet size=%dx%d\n", width, height);
	}

	CancelViewportFilter();
	m_bUserViewportChange = false;
	m_bFitToItemsPending = false;
	/* Set the zoom before the centre.  go_to_full() applies location first,
	 * and the latitude clamp can then run at the old zoom level and pull the
	 * fitted centre towards the equator before the zoom arrives. */
	m_bClampingViewport = true;
	shumate_map_stop_go_to(SHUMATE_MAP(m_Map));
	shumate_viewport_set_zoom_level(m_Viewport, targetZoom);
	shumate_location_set_location(SHUMATE_LOCATION(m_Viewport),
		centerLat, centerLon);
	m_bClampingViewport = false;
	gtk_widget_queue_draw(m_Map);
	return true;
}

void QuiverMapView::Impl::EnforceWorldFit()
{
	if (!m_Map || !m_Viewport || m_bEnforcingWorldFit)
		return;

	const double fitZoom = GetWorldFitZoom();
	const double zoom = shumate_viewport_get_zoom_level(m_Viewport);
	const double lon = shumate_location_get_longitude(SHUMATE_LOCATION(m_Viewport));
	if (zoom >= fitZoom - 1e-6 && std::fabs(lon) < 1e-6)
		return;

	if (zoom < fitZoom - 1e-6)
	{
		shumate_map_stop_go_to(SHUMATE_MAP(m_Map));
		m_bEnforcingWorldFit = true;
		shumate_viewport_set_zoom_level(m_Viewport, fitZoom);
		shumate_location_set_location(SHUMATE_LOCATION(m_Viewport), 0.0, 0.0);
		m_bEnforcingWorldFit = false;
	}
	else if (zoom <= fitZoom + 1e-6)
	{
		m_bEnforcingWorldFit = true;
		shumate_location_set_location(SHUMATE_LOCATION(m_Viewport), 0.0, 0.0);
		m_bEnforcingWorldFit = false;
	}
}

void QuiverMapView::Impl::ScheduleClusterRebuild(bool recompute, guint delayMs)
{
	if (recompute)
		m_bClusterRecompute = true;
	if (m_ClusterSourceId != 0)
		g_source_remove(m_ClusterSourceId);
	m_ClusterSourceId = g_timeout_add_full(G_PRIORITY_DEFAULT_IDLE, delayMs,
		&Impl::IdleRebuildClusters, this, NULL);
}

gboolean QuiverMapView::Impl::IdleRebuildClusters(gpointer data)
{
	Impl* self = static_cast<Impl*>(data);
	self->m_ClusterSourceId = 0;
	const bool recompute = self->m_bClusterRecompute;
	self->m_bClusterRecompute = false;
	self->RebuildClusterMarkers(recompute);
	return G_SOURCE_REMOVE;
}

void QuiverMapView::Impl::RebuildClusterMarkers(bool recompute)
{
	if (!m_Layer)
		return;

	const double zoom = m_Viewport ? shumate_viewport_get_zoom_level(m_Viewport) : 2.0;
	/* Recluster only when the zoom bucket changes (or the index changed).
	 * Shumate already repositions markers during a pan, so a pan does not
	 * need the O(n) clustering pass at all. */
	const double clusterZoom = std::round(zoom * 2.0) / 2.0;
	if (recompute && (m_bClustersDirty || m_LastClusterZoom < 0.0 ||
		std::fabs(clusterZoom - m_LastClusterZoom) > 1e-6))
	{
		if (m_Index)
			m_AllClusters = m_Index->ClusterForZoom(clusterZoom, kClusterRadius);
		else
			m_AllClusters.clear();
		m_LastClusterZoom = clusterZoom;
		m_bClustersDirty = false;
	}

	RebuildVisibleMarkers();
}

void QuiverMapView::Impl::RebuildVisibleMarkers()
{
	if (!m_Layer)
		return;
	quiver_wrap_marker_layer_clear(m_Layer);
	m_MarkerImages.clear();
	m_VisibleClusters.clear();
	m_MarkerWidgetsByCluster.clear();
	m_ClusterByMarkerWidget.clear();

	if (!m_Index || m_AllClusters.empty() || !m_Map || !m_Viewport)
		return;

	const int width = gtk_widget_get_width(m_Map);
	const int height = gtk_widget_get_height(m_Map);
	if (width <= 0 || height <= 0)
		return;

	const double zoom = shumate_viewport_get_zoom_level(m_Viewport);
	const double worldSize = kTileWorldSize * std::pow(2.0, zoom);
	if (worldSize <= 0.0)
		return;

	/* A thumbnail is created for every world copy that can be on screen.  The
	 * layer assigns those widgets to copies as the viewport moves, so panning
	 * between rebuilds still follows the repeated map. */
	const double wrapSpan = (double)width + 2.0 * kWrapCullMargin;
	const int maxCopies = std::max(1,
		(int)std::floor(wrapSpan / worldSize) + 1);

	auto makeThumbnail = [this](const std::string& uri, bool stacked) -> GtkWidget* {
		QuiverFile file(uri.c_str());
		GtkWidget* image = gtk_image_new();
		gtk_widget_add_css_class(image, "quiver-map-thumb");
		if (stacked)
			gtk_widget_add_css_class(image, "quiver-map-thumb-stack");
		gtk_widget_set_size_request(image, 46, 46);
		gtk_image_set_pixel_size(GTK_IMAGE(image), 40);
		GdkTexture* cached = file.GetCachedThumbnailAtMost(64);
		if (cached)
		{
			gtk_image_set_from_paintable(GTK_IMAGE(image),
				GDK_PAINTABLE(cached));
		}
		else
		{
			gtk_image_set_from_icon_name(GTK_IMAGE(image),
				"image-x-generic-symbolic");
			RequestThumbnail(uri);
		}
		m_MarkerImages.emplace(uri, image);
		return image;
	};

	auto makeMarker = [&](const QuiverGpsIndex::Cluster& cluster) -> GtkWidget* {
		gchar* tooltip = NULL;
		GtkWidget* child = NULL;
		if (cluster.count > 1)
		{
			GtkWidget* image = makeThumbnail(cluster.representativeURI, true);
			GtkWidget* overlay = gtk_overlay_new();
			gtk_overlay_set_child(GTK_OVERLAY(overlay), image);

			gchar* countText = g_strdup_printf("%zu", cluster.count);
			GtkWidget* badge = gtk_label_new(countText);
			gtk_widget_add_css_class(badge, "quiver-map-count");
			gtk_widget_set_halign(badge, GTK_ALIGN_END);
			gtk_widget_set_valign(badge, GTK_ALIGN_START);
			gtk_widget_set_margin_top(badge, 1);
			gtk_widget_set_margin_end(badge, 1);
			gtk_overlay_add_overlay(GTK_OVERLAY(overlay), badge);
			g_free(countText);

			tooltip = g_strdup_printf("%zu items in this area", cluster.count);
			child = overlay;
		}
		else
		{
			QuiverFile file(cluster.representativeURI.c_str());
			std::string name = file.GetFileName();
			tooltip = g_strdup_printf("%s\n%.5f, %.5f", name.c_str(),
				cluster.lat, cluster.lon);
			child = makeThumbnail(cluster.representativeURI, false);
		}

		if (tooltip && tooltip[0])
			gtk_widget_set_tooltip_text(child, tooltip);
		g_free(tooltip);
		return child;
	};

	for (std::size_t i = 0; i < m_AllClusters.size(); i++)
	{
		const QuiverGpsIndex::Cluster& cluster = m_AllClusters[i];
		double screenX = 0.0, screenY = 0.0;
		shumate_viewport_location_to_widget_coords(m_Viewport, m_Map,
			cluster.lat, cluster.lon, &screenX, &screenY);
		if (screenY < -kWrapCullMargin ||
			screenY > (double)height + kWrapCullMargin)
		{
			continue;
		}

		const int firstCopy = (int)std::ceil(
			(-kWrapCullMargin - screenX) / worldSize);
		const int lastCopy = (int)std::floor(
			((double)width + kWrapCullMargin - screenX) / worldSize);
		if (lastCopy < firstCopy)
			continue;

		m_VisibleClusters.push_back(i);
		std::vector<GtkWidget*> copyWidgets;
		copyWidgets.reserve(maxCopies);
		for (int copy = 0; copy < maxCopies; copy++)
		{
			GtkWidget* marker = makeMarker(cluster);
			copyWidgets.push_back(marker);
			m_MarkerWidgetsByCluster.emplace(i, marker);
			m_ClusterByMarkerWidget[marker] = i;
		}
		quiver_wrap_marker_layer_add(m_Layer, cluster.lat, cluster.lon,
			std::move(copyWidgets));
	}

	UpdateSelectionVisuals();
}

void QuiverMapView::Impl::RequestThumbnail(const std::string& uri)
{
	bool bStartThread = false;
	{
		std::lock_guard<std::mutex> lock(m_ThumbnailMutex);
		if (m_bThumbnailStop || m_ThumbnailAttempted.count(uri) ||
			m_ThumbnailQueued.count(uri))
			return;
		m_ThumbnailQueued.insert(uri);
		m_ThumbnailQueue.push_back(uri);
		bStartThread = !m_ThumbnailThread.joinable();
	}
	if (bStartThread)
	{
		std::weak_ptr<Impl> self = shared_from_this();
		m_ThumbnailThread = std::thread([this, self]() { ThumbnailWorker(self); });
	}
	m_ThumbnailCv.notify_one();
}

gboolean QuiverMapView::Impl::ThumbnailAbort(gpointer data)
{
	return static_cast<Impl*>(data)->m_bThumbnailStop ? TRUE : FALSE;
}

void QuiverMapView::Impl::ThumbnailWorker(std::weak_ptr<Impl> self)
{
	for (;;)
	{
		std::string uri;
		{
			std::unique_lock<std::mutex> lock(m_ThumbnailMutex);
			m_ThumbnailCv.wait(lock, [this]() {
				return m_bThumbnailStop || !m_ThumbnailQueue.empty();
			});
			if (m_ThumbnailQueue.empty())
			{
				if (m_bThumbnailStop)
					break;
				continue;
			}
			uri = std::move(m_ThumbnailQueue.front());
			m_ThumbnailQueue.pop_front();
		}
		if (m_bThumbnailStop)
			break;

		QuiverFile file(uri.c_str());
		GdkTexture* texture = file.GetThumbnailTexture(64, ThumbnailAbort, this);
		if (texture)
			g_object_ref(texture);
		if (m_bThumbnailStop)
		{
			if (texture)
				g_object_unref(texture);
			break;
		}

		ThumbnailPayload* payload = new ThumbnailPayload();
		payload->impl = new std::weak_ptr<Impl>(self);
		payload->uri = uri;
		payload->texture = texture;
		g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,
			&Impl::IdleApplyThumbnail, payload, NULL);
	}
}

gboolean QuiverMapView::Impl::IdleApplyThumbnail(gpointer data)
{
	ThumbnailPayload* payload = static_cast<ThumbnailPayload*>(data);
	std::shared_ptr<Impl> impl = payload->impl->lock();
	delete payload->impl;
	if (impl)
	{
		impl->ApplyThumbnail(payload->uri, payload->texture);
	}
	else if (payload->texture)
	{
		g_object_unref(payload->texture);
	}
	delete payload;
	return G_SOURCE_REMOVE;
}

void QuiverMapView::Impl::ApplyThumbnail(const std::string& uri,
	GdkTexture* texture)
{
	{
		std::lock_guard<std::mutex> lock(m_ThumbnailMutex);
		m_ThumbnailQueued.erase(uri);
		if (!texture)
			m_ThumbnailAttempted.insert(uri);
	}

	if (!texture)
		return;

	auto range = m_MarkerImages.equal_range(uri);
	for (auto it = range.first; it != range.second; ++it)
	{
		if (GTK_IS_IMAGE(it->second))
		{
			gtk_image_set_from_paintable(GTK_IMAGE(it->second),
				GDK_PAINTABLE(texture));
		}
	}
	g_object_unref(texture);
}

std::size_t QuiverMapView::Impl::FindClusterAt(double x, double y) const
{
	if (!m_Map || m_MarkerWidgetsByCluster.empty())
		return std::numeric_limits<std::size_t>::max();

	/* Ask GTK what is actually under the pointer.  The target may be the
	 * marker's child image, an overlay, or the count badge, so walk up to
	 * the marker itself. */
	GtkWidget* target = gtk_widget_pick(m_Map, x, y, GTK_PICK_DEFAULT);
	for (GtkWidget* widget = target;
		widget != NULL && widget != m_Map;
		widget = gtk_widget_get_parent(widget))
	{
		auto it = m_ClusterByMarkerWidget.find(widget);
		if (it != m_ClusterByMarkerWidget.end())
			return it->second;
	}

	/* Fall back to the marker allocation so clicks on the transparent edge
	 * of a thumbnail still select it. */
	constexpr double kHitPadding = 2.0;
	std::size_t best = std::numeric_limits<std::size_t>::max();
	double bestDistance = std::numeric_limits<double>::max();

	for (const auto& [index, marker] : m_MarkerWidgetsByCluster)
	{
		if (!marker || !gtk_widget_get_child_visible(marker))
			continue;
		graphene_rect_t bounds;
		if (!gtk_widget_compute_bounds(marker, m_Map, &bounds))
			continue;
		if (x < bounds.origin.x - kHitPadding ||
			x > bounds.origin.x + bounds.size.width + kHitPadding ||
			y < bounds.origin.y - kHitPadding ||
			y > bounds.origin.y + bounds.size.height + kHitPadding)
		{
			continue;
		}
		const double centerX = bounds.origin.x + bounds.size.width / 2.0;
		const double centerY = bounds.origin.y + bounds.size.height / 2.0;
		const double dx = centerX - x;
		const double dy = centerY - y;
		const double distance = dx * dx + dy * dy;
		if (distance < bestDistance)
		{
			bestDistance = distance;
			best = index;
		}
	}

	return best;
}

void QuiverMapView::Impl::OnMapPressed(GtkGestureClick* gesture, int n_press,
	double x, double y, gpointer data)
{
	Impl* self = static_cast<Impl*>(data);
	const std::size_t index = self->FindClusterAt(x, y);

	if (index == std::numeric_limits<std::size_t>::max())
	{
		/* Not a marker: let Shumate's own pan/zoom gestures handle it. */
		gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_DENIED);
		return;
	}

	gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
	self->CancelViewportFilter();
	const std::string uri = self->m_AllClusters[index].representativeURI;
	self->m_SelectedURI = uri;
	self->UpdateSelectionVisuals();
	if (self->m_SelectCb)
		self->m_SelectCb(uri);
	if (n_press >= 2 && self->m_ActivateCb)
		self->m_ActivateCb(uri);
}

void QuiverMapView::Impl::UpdateSelectionVisuals()
{
	for (auto& entry : m_MarkerImages)
	{
		GtkWidget* image = entry.second;
		if (!image || !GTK_IS_IMAGE(image))
			continue;
		if (entry.first == m_SelectedURI)
			gtk_widget_add_css_class(image, "quiver-map-selected");
		else
			gtk_widget_remove_css_class(image, "quiver-map-selected");
	}
}

void QuiverMapView::Impl::HighlightURI(const std::string& uri)
{
	if (!m_Index)
		return;
	const std::vector<QuiverGpsIndex::Point> points = m_Index->GetPoints();
	for (const auto& point : points)
	{
		if (point.uri == uri)
		{
			shumate_map_center_on(SHUMATE_MAP(m_Map), point.lat, point.lon);
			return;
		}
	}
}

void QuiverMapView::Impl::ClearRegionFilterVisual()
{
	CancelViewportFilter();
	m_bHasRegionFilter = false;
}

void QuiverMapView::Impl::OnMapMapped(GtkWidget*, gpointer data)
{
	Impl* self = static_cast<Impl*>(data);
	/* The index is often built while the map is hidden, when it has no
	 * allocation yet, so the initial marker cull finds nothing.  Re-run it
	 * once the map is actually mapped and laid out. */
	MAP_DEBUG("OnMapMapped size=%dx%d points=%zu fitPending=%d fitWaiting=%d "
		"zoom=%f\n",
		self->m_Map ? gtk_widget_get_width(self->m_Map) : 0,
		self->m_Map ? gtk_widget_get_height(self->m_Map) : 0,
		self->m_Index ? self->m_Index->GetPointCount() : 0u,
		self->m_bFitToItemsPending ? 1 : 0,
		self->m_bFitToItemsWaitingForIndex ? 1 : 0,
		self->m_Viewport ? shumate_viewport_get_zoom_level(self->m_Viewport) : 0.0);
	self->ScheduleClusterRebuild(false, 0);
	if (self->m_bFitToItemsPending)
	{
		if (!self->FitToItems())
			self->ScheduleFitRetry();
	}
	else
	{
		self->ScheduleWorldFitEnforcement();
	}
	if (self->m_KeyController || !self->m_Root)
		return;

	GtkWidget* root = GTK_WIDGET(gtk_widget_get_root(self->m_Root));
	if (!root)
		return;

	self->m_KeyController = gtk_event_controller_key_new();
	self->m_KeyControllerWidget = root;
	g_object_add_weak_pointer(G_OBJECT(self->m_KeyController),
		(gpointer*)&self->m_KeyController);
	g_object_add_weak_pointer(G_OBJECT(self->m_KeyControllerWidget),
		(gpointer*)&self->m_KeyControllerWidget);
	gtk_event_controller_set_propagation_phase(self->m_KeyController,
		GTK_PHASE_CAPTURE);
	g_signal_connect(self->m_KeyController, "key-pressed",
		G_CALLBACK(OnKeyPressed), self);
	gtk_widget_add_controller(root, self->m_KeyController);
}

gboolean QuiverMapView::Impl::OnInputEvent(GtkEventControllerLegacy*,
	GdkEvent* event, gpointer data)
{
	Impl* self = static_cast<Impl*>(data);
	switch (gdk_event_get_event_type(event))
	{
	case GDK_BUTTON_PRESS:
	{
		self->m_bPointerPressed = true;
		self->m_bPointerDragged = false;
		gdk_event_get_position(event, &self->m_PressX, &self->m_PressY);
		break;
	}
	case GDK_BUTTON_RELEASE:
	{
		/* A click (and a double-click) must not filter.  Only motion past a
		 * small threshold counts as a pan, so a press/release on a marker or
		 * on empty map space leaves the current region filter untouched. */
		self->m_bPointerPressed = false;
		self->m_bPointerDragged = false;
		break;
	}
	case GDK_MOTION_NOTIFY:
	{
		const GdkModifierType state = gdk_event_get_modifier_state(event);
		if (state & (GDK_BUTTON1_MASK | GDK_BUTTON2_MASK | GDK_BUTTON3_MASK))
		{
			double x = 0.0, y = 0.0;
			gdk_event_get_position(event, &x, &y);
			if (!self->m_bPointerPressed)
			{
				/* The drag started before the pointer entered this widget. */
				self->m_bPointerPressed = true;
				self->m_bPointerDragged = true;
				self->m_PressX = x;
				self->m_PressY = y;
			}
			else if (!self->m_bPointerDragged)
			{
				const double dx = x - self->m_PressX;
				const double dy = y - self->m_PressY;
				self->m_bPointerDragged = dx * dx + dy * dy >= 16.0;
			}
			if (self->m_bPointerDragged)
				self->m_bUserViewportChange = true;
		}
		break;
	}
	case GDK_SCROLL:
	{
		double dx = 0.0, dy = 0.0;
		gdk_scroll_event_get_deltas(event, &dx, &dy);
		(void)dx;
		const bool scrollDown = dy > 0.0 ||
			gdk_scroll_event_get_direction(event) == GDK_SCROLL_DOWN;
		if (scrollDown && self->m_Viewport && self->m_Map)
		{
			const double zoom = shumate_viewport_get_zoom_level(
				self->m_Viewport);
			const double worldFit = self->GetWorldFitZoom();
			if (zoom <= worldFit + 1e-6)
			{
				shumate_map_stop_go_to(SHUMATE_MAP(self->m_Map));
				return GDK_EVENT_STOP;
			}

			/* Mirror Shumate's own scroll-to-zoom calculation.  If this
			 * single event would land below the world-fit zoom, stop it
			 * here and clamp to the fit instead of letting the map shrink
			 * to a tiny repeated world before the idle correction runs. */
			double target;
			if (gdk_scroll_event_get_unit(event) == GDK_SCROLL_UNIT_SURFACE)
			{
				target = zoom - dy / 50.0;
			}
			else
			{
				target = zoom - dy / 5.0;
				if (std::fabs(dy) == 1.0)
					target = std::round(target * 5.0) / 5.0;
			}
			if (target < worldFit - 1e-6)
			{
				shumate_map_stop_go_to(SHUMATE_MAP(self->m_Map));
				shumate_viewport_set_zoom_level(self->m_Viewport,
					worldFit);
				self->m_bUserViewportChange = true;
				return GDK_EVENT_STOP;
			}
		}
		self->m_bUserViewportChange = true;
		break;
	}
	case GDK_TOUCHPAD_SWIPE:
	case GDK_TOUCHPAD_PINCH:
	case GDK_TOUCH_UPDATE:
		self->m_bUserViewportChange = true;
		break;
	default:
		break;
	}
	return GDK_EVENT_PROPAGATE;
}

void QuiverMapView::Impl::OnMapSizeChanged(GObject*, GParamSpec*, gpointer data)
{
	Impl* self = static_cast<Impl*>(data);
	MAP_DEBUG("OnMapSizeChanged size=%dx%d fitPending=%d fitWaiting=%d\n",
		self->m_Map ? gtk_widget_get_width(self->m_Map) : 0,
		self->m_Map ? gtk_widget_get_height(self->m_Map) : 0,
		self->m_bFitToItemsPending ? 1 : 0,
		self->m_bFitToItemsWaitingForIndex ? 1 : 0);
	self->ClampViewport();
	if (self->m_bFitToItemsPending)
	{
		if (self->FitToItems())
			gtk_widget_queue_draw(self->m_Map);
		else
			self->ScheduleFitRetry();
	}
	else
	{
		self->ScheduleWorldFitEnforcement();
	}
}

gboolean QuiverMapView::Impl::OnKeyPressed(GtkEventControllerKey*, guint keyval,
	guint, GdkModifierType state, gpointer data)
{
	Impl* self = static_cast<Impl*>(data);
	if (!self->m_Root || !gtk_widget_get_mapped(self->m_Root))
		return FALSE;

	if (state & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SUPER_MASK | GDK_META_MASK))
		return FALSE;

	if (keyval == GDK_KEY_plus || keyval == GDK_KEY_equal ||
		keyval == GDK_KEY_KP_Add || keyval == GDK_KEY_KP_Equal)
	{
		self->ZoomStep(true, true);
		return TRUE;
	}
	if (keyval == GDK_KEY_minus || keyval == GDK_KEY_KP_Subtract)
	{
		self->ZoomStep(false, true);
		return TRUE;
	}
	return FALSE;
}

bool QuiverMapView::Impl::GetPointerInMap(double& x, double& y)
{
	if (!m_Map || !gtk_widget_get_mapped(m_Map))
		return false;

	GtkNative* native = gtk_widget_get_native(m_Map);
	if (!native)
		return false;
	GdkSurface* surface = gtk_native_get_surface(native);
	GdkDisplay* display = gtk_widget_get_display(m_Map);
	GdkSeat* seat = display ? gdk_display_get_default_seat(display) : nullptr;
	GdkDevice* pointer = seat ? gdk_seat_get_pointer(seat) : nullptr;
	if (!surface || !pointer)
		return false;

	double surfaceX = 0.0, surfaceY = 0.0;
	if (gdk_device_get_surface_at_position(pointer, &surfaceX, &surfaceY) != surface)
		return false;

	GtkWidget* rootWidget = GTK_WIDGET(gtk_widget_get_root(m_Map));
	if (!rootWidget)
		return false;

	graphene_point_t surfacePoint = GRAPHENE_POINT_INIT((float)surfaceX, (float)surfaceY);
	graphene_point_t mapPoint;
	if (!gtk_widget_compute_point(rootWidget, m_Map, &surfacePoint, &mapPoint))
		return false;

	const int width = gtk_widget_get_width(m_Map);
	const int height = gtk_widget_get_height(m_Map);
	if (mapPoint.x < 0.0f || mapPoint.y < 0.0f ||
		mapPoint.x >= (float)width || mapPoint.y >= (float)height)
		return false;

	x = mapPoint.x;
	y = mapPoint.y;
	return true;
}

void QuiverMapView::Impl::GrabFocus()
{
	if (m_Map)
		gtk_widget_grab_focus(m_Map);
}

void QuiverMapView::Impl::ResetToWorldView(bool userInitiated)
{
	if (!m_Map || !m_Viewport)
		return;
	if (!userInitiated)
		CancelViewportFilter();
	m_bUserViewportChange = userInitiated;
	shumate_map_go_to_full_with_duration(SHUMATE_MAP(m_Map), 0.0, 0.0,
		GetWorldFitZoom(), 0);
}

void QuiverMapView::Impl::ZoomToWorld()
{
	ResetToWorldView(false);
}

void QuiverMapView::Impl::ZoomStep(bool zoomIn, bool atPointer)
{
	if (!m_Viewport)
		return;

	double x = 0.0, y = 0.0;
	if (atPointer && GetPointerInMap(x, y))
	{
		ZoomAtPoint(zoomIn, x, y);
		return;
	}

	const double oldZoom = shumate_viewport_get_zoom_level(m_Viewport);
	const double minZoom = GetWorldFitZoom();
	const double maxZoom = (double)shumate_viewport_get_max_zoom_level(m_Viewport);
	m_bUserViewportChange = true;
	if (!zoomIn && oldZoom <= minZoom + 1e-6)
	{
		ResetToWorldView(true);
		return;
	}
	const double newZoom = std::clamp(oldZoom + (zoomIn ? 1.0 : -1.0),
		minZoom, maxZoom);
	if (!zoomIn && newZoom <= minZoom + 1e-6)
	{
		ResetToWorldView(true);
		return;
	}
	if (std::fabs(newZoom - oldZoom) > 1e-9)
		shumate_viewport_set_zoom_level(m_Viewport, newZoom);
}

void QuiverMapView::Impl::ZoomAtPoint(bool zoomIn, double x, double y)
{
	if (!m_Viewport || !m_Map)
		return;

	const double oldZoom = shumate_viewport_get_zoom_level(m_Viewport);
	const double minZoom = GetWorldFitZoom();
	const double maxZoom = (double)shumate_viewport_get_max_zoom_level(m_Viewport);
	m_bUserViewportChange = true;
	if (!zoomIn && oldZoom + (zoomIn ? 1.0 : -1.0) <= minZoom + 1e-6)
	{
		ResetToWorldView(true);
		return;
	}
	const double newZoom = std::clamp(oldZoom + (zoomIn ? 1.0 : -1.0),
		minZoom, maxZoom);
	if (std::fabs(newZoom - oldZoom) < 1e-9)
		return;

	double cursorLat = 0.0, cursorLon = 0.0;
	shumate_viewport_widget_coords_to_location(m_Viewport, m_Map, x, y,
		&cursorLat, &cursorLon);

	const int width = gtk_widget_get_width(m_Map);
	const int height = gtk_widget_get_height(m_Map);
	if (width <= 0 || height <= 0)
		return;

	const double offsetX = x - (double)width / 2.0;
	const double offsetY = y - (double)height / 2.0;
	const double oldWorld = kTileWorldSize * std::pow(2.0, oldZoom);
	const double newWorld = kTileWorldSize * std::pow(2.0, newZoom);
	const double scale = newWorld / oldWorld;

	const double cursorX = LonToWorldX(cursorLon, oldWorld);
	const double cursorY = LatToWorldY(cursorLat, oldWorld);
	const double centerX = cursorX * scale - offsetX;
	const double centerY = cursorY * scale - offsetY;

	shumate_viewport_set_zoom_level(m_Viewport, newZoom);
	shumate_location_set_location(SHUMATE_LOCATION(m_Viewport),
		WorldYToLat(centerY, newWorld), WorldXToLon(centerX, newWorld));
}

void QuiverMapView::Impl::FilterRectangle(double lat1, double lon1,
	double lat2, double lon2)
{
	double top = std::max(lat1, lat2);
	double bottom = std::min(lat1, lat2);
	double left = lon1;
	double span = lon2 - lon1;
	if (span < 0.0)
		span += 360.0;

	const double zoom = m_Viewport
		? shumate_viewport_get_zoom_level(m_Viewport) : 0.0;
	const int width = m_Map ? gtk_widget_get_width(m_Map) : 0;
	const int height = m_Map ? gtk_widget_get_height(m_Map) : 0;
	const double worldSize = kTileWorldSize * std::pow(2.0, zoom);
	const bool viewportCoversWorldLon = width > 0 &&
		(double)width >= worldSize - 1e-6;
	const bool viewportCoversWorldLat = height > 0 &&
		(double)height >= worldSize - 1e-6;
	if (span >= 360.0 - 1e-6 || zoom <= GetWorldFitZoom() + 1e-6 ||
		viewportCoversWorldLon)
	{
		left = -180.0;
		span = 360.0;
	}
	else
	{
		left = std::fmod(left + 180.0, 360.0);
		if (left < 0.0)
			left += 360.0;
		left -= 180.0;
	}
	if (viewportCoversWorldLat)
	{
		top = SHUMATE_MAX_LATITUDE;
		bottom = SHUMATE_MIN_LATITUDE;
	}
	double right = left + span;
	if (right > 180.0)
		right -= 360.0;

	if (MapDebugVerbose())
	{
		MAP_DEBUG("FilterRectangle raw top=%f left=%f bottom=%f right=%f -> "
			"top=%f left=%f bottom=%f right=%f zoom=%f worldFit=%f "
			"worldSize=%f widget=%dx%d coveredLon=%d coveredLat=%d\n",
			lat1, lon1, lat2, lon2, top, left, bottom, right, zoom,
			GetWorldFitZoom(), worldSize, width, height,
			viewportCoversWorldLon ? 1 : 0, viewportCoversWorldLat ? 1 : 0);
	}

	m_bHasRegionFilter = true;
	if (m_RegionCb)
		m_RegionCb(top, left, bottom, right);
}

QuiverMapView::QuiverMapView()
	: m_Impl(std::make_shared<Impl>())
{
}

QuiverMapView::~QuiverMapView() = default;

GtkWidget* QuiverMapView::GetWidget() const
{
	return m_Impl->GetWidget();
}

void QuiverMapView::SetImageList(IImageListViewPtr pList)
{
	m_Impl->SetImageList(pList);
}

IImageListViewPtr QuiverMapView::GetImageList() const
{
	return m_Impl->GetImageList();
}

void QuiverMapView::SetMetadataProgressCallback(MetadataProgressCallback cb)
{
	m_Impl->m_MetadataProgressCb = std::move(cb);
}

void QuiverMapView::SetMetadataReadyCallback(MetadataReadyCallback cb)
{
	m_Impl->m_MetadataReadyCb = std::move(cb);
}

void QuiverMapView::Refresh()
{
	m_Impl->Refresh();
}

void QuiverMapView::SetSelectCallback(UriCallback cb)
{
	m_Impl->m_SelectCb = std::move(cb);
}

void QuiverMapView::SetActivateCallback(UriCallback cb)
{
	m_Impl->m_ActivateCb = std::move(cb);
}

void QuiverMapView::SetRegionFilterCallback(RegionCallback cb)
{
	m_Impl->m_RegionCb = std::move(cb);
}

bool QuiverMapView::HasActiveRegionFilter() const
{
	return m_Impl->HasActiveRegionFilter();
}

void QuiverMapView::ClearRegionFilterVisual()
{
	m_Impl->ClearRegionFilterVisual();
}

void QuiverMapView::HighlightURI(const std::string& uri)
{
	m_Impl->HighlightURI(uri);
}

void QuiverMapView::ZoomIn()
{
	m_Impl->ZoomIn();
}

void QuiverMapView::ZoomOut()
{
	m_Impl->ZoomOut();
}

void QuiverMapView::ZoomToWorld()
{
	m_Impl->ZoomToWorld();
}

void QuiverMapView::ResetViewForImageList()
{
	m_Impl->ResetViewForImageList();
}

void QuiverMapView::GrabFocus()
{
	m_Impl->GrabFocus();
}
