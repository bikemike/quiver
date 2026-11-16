#include "QuiverGpsIndex.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

namespace
{
constexpr double kTileSize = 256.0;

double LonToWorldX(double lon, double worldWidth)
{
	return (lon + 180.0) / 360.0 * worldWidth;
}

double LatToWorldY(double lat, double worldWidth)
{
	const double clamped = std::max(-85.0511287798, std::min(85.0511287798, lat));
	const double rad = clamped * M_PI / 180.0;
	const double merc = std::log(std::tan(M_PI / 4.0 + rad / 2.0));
	return (1.0 - merc / M_PI) / 2.0 * worldWidth;
}
} // namespace

QuiverGpsIndex::QuiverGpsIndex() = default;
QuiverGpsIndex::~QuiverGpsIndex() = default;

void QuiverGpsIndex::Clear()
{
	std::lock_guard<std::mutex> lock(m_Mutex);
	m_Points.clear();
}

void QuiverGpsIndex::Rebuild(IImageListViewPtr pList, ProgressCallback progress,
	CancelCallback cancelled)
{
	std::vector<QuiverFile> files;
	if (pList)
	{
		const std::size_t count = pList->GetSize();
		files.reserve(count);
		for (std::size_t i = 0; i < count; i++)
			files.push_back(pList->Get((unsigned int)i));
	}
	Rebuild(files, std::move(progress), std::move(cancelled));
}

void QuiverGpsIndex::Rebuild(const std::vector<QuiverFile>& files,
	ProgressCallback progress, CancelCallback cancelled)
{
	std::vector<Point> points;
	const std::size_t count = files.size();

	/* Date sorting already runs the combined pass for the whole list.
	 * In that case rebuilding the map index only reads caches, so do not
	 * flash a second "Indexing dates and GPS" progress pill for it. */
	bool needsMetadata = false;
	for (std::size_t i = 0; i < count; i++)
	{
		if (cancelled && cancelled())
			return;
		const QuiverFile& file = files[i];
		if (!file.IsFolder() && !file.IsMetadataResolvedForIndex())
		{
			needsMetadata = true;
			break;
		}
	}
	const ProgressCallback report = needsMetadata ? progress : ProgressCallback{};

	points.reserve(count);
	for (std::size_t i = 0; i < count; i++)
	{
		if (cancelled && cancelled())
			return;

		const QuiverFile& file = files[i];
		if (file.IsFolder())
		{
			if (report && (i % 64 == 0 || i + 1 == count))
				report(i + 1, count);
			continue;
		}

		/* One combined metadata pass fills both the date and GPS caches
		 * and releases the parsed EXIF afterwards. */
		double lat = 0.0, lon = 0.0, alt = 0.0;
		bool hasAlt = false;
		const bool hasGps = file.ResolveMetadataForIndex() &&
			file.GetCachedGPSCoordinates(lat, lon, hasAlt, alt);

		if (report && (i % 64 == 0 || i + 1 == count))
			report(i + 1, count);

		if (!hasGps)
			continue;

		const char* uri = file.GetURI();
		if (NULL == uri || '\0' == uri[0])
			continue;

		Point p;
		p.lat = lat;
		p.lon = lon;
		p.uri = uri;
		points.push_back(std::move(p));
	}

	std::lock_guard<std::mutex> lock(m_Mutex);
	m_Points.swap(points);
}

std::size_t QuiverGpsIndex::GetPointCount() const
{
	std::lock_guard<std::mutex> lock(m_Mutex);
	return m_Points.size();
}

std::vector<QuiverGpsIndex::Point> QuiverGpsIndex::GetPoints() const
{
	std::lock_guard<std::mutex> lock(m_Mutex);
	return m_Points;
}

std::vector<QuiverGpsIndex::Cluster> QuiverGpsIndex::ClusterForZoom(double zoom,
	double clusterRadius) const
{
	std::lock_guard<std::mutex> lock(m_Mutex);
	return ClusterPoints(m_Points, zoom, clusterRadius);
}

std::vector<QuiverGpsIndex::Cluster> QuiverGpsIndex::ClusterPoints(
	const std::vector<Point>& points, double zoom, double clusterRadius)
{
	std::vector<Cluster> result;
	if (points.empty())
	{
		return result;
	}

	const double worldWidth = kTileSize * std::pow(2.0, std::max(0.0, zoom));
	const double cell = std::max(1.0, clusterRadius);

	// bucket key -> index into result
	std::map<std::pair<long long, long long>, std::size_t> cells;

	for (std::size_t i = 0; i < points.size(); i++)
	{
		const Point& p = points[i];
		const double px = LonToWorldX(p.lon, worldWidth);
		const double py = LatToWorldY(p.lat, worldWidth);

		const long long cx = (long long)std::floor(px / cell);
		const long long cy = (long long)std::floor(py / cell);

		std::size_t clusterIndex = 0;
		bool found = false;

		// Look in the 3x3 neighbourhood so points on opposite sides of a cell
		// boundary still merge when they are within the radius.
		for (long long dx = -1; !found && dx <= 1; dx++)
		{
			for (long long dy = -1; !found && dy <= 1; dy++)
			{
				auto it = cells.find(std::make_pair(cx + dx, cy + dy));
				if (cells.end() == it)
				{
					continue;
				}
				const Cluster& c = result[it->second];
				const double ccx = LonToWorldX(c.lon, worldWidth);
				const double ccy = LatToWorldY(c.lat, worldWidth);
				const double dist = std::hypot(px - ccx, py - ccy);
				if (dist <= clusterRadius)
				{
					clusterIndex = it->second;
					found = true;
				}
			}
		}

		if (!found)
		{
			Cluster c;
			c.lat = p.lat;
			c.lon = p.lon;
			c.minLat = c.maxLat = p.lat;
			c.minLon = c.maxLon = p.lon;
			c.count = 0;
			c.representativeURI = p.uri;
			clusterIndex = result.size();
			result.push_back(std::move(c));
			cells[std::make_pair(cx, cy)] = clusterIndex;
		}

		Cluster& c = result[clusterIndex];
		const std::size_t oldCount = c.count;
		c.count++;
		// running centroid
		c.lat = (c.lat * oldCount + p.lat) / (double)c.count;
		c.lon = (c.lon * oldCount + p.lon) / (double)c.count;
		c.minLat = std::min(c.minLat, p.lat);
		c.maxLat = std::max(c.maxLat, p.lat);
		c.minLon = std::min(c.minLon, p.lon);
		c.maxLon = std::max(c.maxLon, p.lon);
		if (c.representativeURI.empty())
		{
			c.representativeURI = p.uri;
		}
		c.points.push_back(i);

		/* Remember this cell too, so later points that fall just inside it
		 * (but outside the seed cell) still find the merged cluster. */
		cells.emplace(std::make_pair(cx, cy), clusterIndex);
	}

	return result;
}
