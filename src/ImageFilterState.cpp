#include "ImageFilterState.h"

#include "QuiverFile.h"

#include <algorithm>
#include <cctype>

namespace
{
std::string ToLower(std::string value)
{
	std::transform(value.begin(), value.end(), value.begin(),
		[](unsigned char c) { return (char)std::tolower(c); });
	return value;
}
}

bool ImageFilterState::IsEmpty() const
{
	return text.empty() && !dateActive && !sizeActive &&
		media == MediaType::Any && gps == GpsFilter::Any && !area.active;
}

bool ImageFilterState::MatchesBase(const QuiverFile& file,
	bool metadataReady) const
{
	if (file.IsFolder())
		return true;

	if (!text.empty())
	{
		const std::string needle = ToLower(text);
		const std::string name = ToLower(file.GetFileName());
		const std::string path = ToLower(file.GetFilePath());
		if (name.find(needle) == std::string::npos &&
			path.find(needle) == std::string::npos)
		{
			return false;
		}
	}

	if (dateActive && metadataReady)
	{
		time_t taken = 0;
		if (!file.GetCachedDateTaken(taken))
			return false;
		if (taken < dateStart || taken > dateEnd)
			return false;
	}

	if (sizeActive)
	{
		const unsigned long long size = file.GetFileSize();
		if (size < sizeMin || size > sizeMax)
			return false;
	}

	if (media == MediaType::Photos && const_cast<QuiverFile&>(file).IsVideo())
		return false;
	if (media == MediaType::Videos && !const_cast<QuiverFile&>(file).IsVideo())
		return false;

	if (gps != GpsFilter::Any && metadataReady)
	{
		const bool hasGps = file.HasCachedGPS();
		if ((gps == GpsFilter::HasGps && !hasGps) ||
			(gps == GpsFilter::NoGps && hasGps))
		{
			return false;
		}
	}

	return true;
}

bool ImageFilterState::MatchesSpatial(const QuiverFile& file) const
{
	if (!area.active)
		return true;
	if (file.IsFolder())
		return false;

	double lat = 0.0;
	double lon = 0.0;
	double alt = 0.0;
	bool hasAlt = false;
	if (!file.GetCachedGPSCoordinates(lat, lon, hasAlt, alt))
		return false;

	const bool lonInRange = area.lonMin <= area.lonMax
		? lon >= area.lonMin && lon <= area.lonMax
		: lon >= area.lonMin || lon <= area.lonMax;
	return lat >= area.latMin && lat <= area.latMax && lonInRange;
}

bool ImageFilterState::MatchesViewer(const QuiverFile& file,
	bool metadataReady) const
{
	if (file.IsFolder())
		return false;
	return MatchesBase(file, metadataReady) && MatchesSpatial(file);
}
