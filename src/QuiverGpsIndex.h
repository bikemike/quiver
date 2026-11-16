#ifndef FILE_QUIVER_GPS_INDEX_H
#define FILE_QUIVER_GPS_INDEX_H

#include <cstddef>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "IImageListView.h"
#include "QuiverFile.h"

// Spatial index of every GPS-tagged item in an ImageList.
//
// The index is built off the GUI thread (Rebuild() copies the list and reads
// or resolves the combined date/GPS metadata cache) and then queried by
// QuiverMapView whenever the viewport changes.  Clustering is done greedily in Web-Mercator pixel space:
// at a given zoom, points whose pixel distance is under @cluster_radius are
// merged.  Zooming in expands the pixel distance, so the same points naturally
// separate into smaller groups and eventually individual pins.
class QuiverGpsIndex
{
public:
	struct Point
	{
		double lat = 0.0;
		double lon = 0.0;
		std::string uri;
	};

	struct Cluster
	{
		double lat = 0.0;
		double lon = 0.0;
		double minLat = 0.0;
		double maxLat = 0.0;
		double minLon = 0.0;
		double maxLon = 0.0;
		std::size_t count = 0;
		std::string representativeURI;
		std::vector<std::size_t> points;
	};

	QuiverGpsIndex();
	~QuiverGpsIndex();

	typedef std::function<void(std::size_t, std::size_t)> ProgressCallback;
	typedef std::function<bool()> CancelCallback;

	// Replaces the index contents.  Every file is visited so the combined
	// metadata pass fills both GPS and date-taken caches; only GPS-tagged
	// files become Points.
	//
	// @cancelled is polled while scanning.  When it returns true the scan
	// stops without touching the existing index, so a superseded rebuild can
	// be abandoned instead of delaying the next one.
	//
	// The list overload is only safe on the thread that owns the list: it
	// copies the QuiverFiles up front, but that copy must not race with a
	// mutation of the source list.  Code that rebuilds from a worker (the map
	// view) snapshots on the GUI thread and calls the vector overload.
	void Rebuild(IImageListViewPtr pList, ProgressCallback progress = {},
		CancelCallback cancelled = {});
	void Rebuild(const std::vector<QuiverFile>& files,
		ProgressCallback progress = {}, CancelCallback cancelled = {});
	void Clear();

	std::size_t GetPointCount() const;
	std::vector<Point> GetPoints() const;

	// Returns clusters for the given zoom.  @clusterRadius is in screen pixels
	// at that zoom (Web-Mercator tile size 256).
	std::vector<Cluster> ClusterForZoom(double zoom, double clusterRadius = 48.0) const;

	// Pure clustering helper (exposed for tests and for mapping arbitrary
	// point sets).  Point::points in the result index into @points.
	static std::vector<Cluster> ClusterPoints(const std::vector<Point>& points,
		double zoom, double clusterRadius = 48.0);

private:
	mutable std::mutex m_Mutex;
	std::vector<Point> m_Points;
};

#endif // FILE_QUIVER_GPS_INDEX_H
