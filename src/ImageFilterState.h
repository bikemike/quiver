#ifndef IMAGE_FILTER_STATE_H
#define IMAGE_FILTER_STATE_H

#include <string>
#include <time.h>

class QuiverFile;

struct ImageFilterState
{
	enum class MediaType
	{
		Any,
		Photos,
		Videos,
	};

	enum class GpsFilter
	{
		Any,
		HasGps,
		NoGps,
	};

	struct MapArea
	{
		bool active = false;
		double latMin = 0.0;
		double latMax = 0.0;
		double lonMin = 0.0;
		double lonMax = 0.0;
	};

	std::string text;
	bool dateActive = false;
	time_t dateStart = 0;
	time_t dateEnd = 0;
	bool sizeActive = false;
	unsigned long long sizeMin = 0;
	unsigned long long sizeMax = 0;
	MediaType media = MediaType::Any;
	GpsFilter gps = GpsFilter::Any;
	MapArea area;

	bool IsEmpty() const;
	bool HasSpatialFilter() const { return area.active; }

	bool MatchesBase(const QuiverFile& file, bool metadataReady) const;
	bool MatchesSpatial(const QuiverFile& file) const;
	bool MatchesViewer(const QuiverFile& file, bool metadataReady) const;
};

#endif
