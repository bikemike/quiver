#ifndef QUIVER_GPS_H
#define QUIVER_GPS_H

// GPS coordinate formatting shared by the property view and the unit tests.
#include <config.h>
#include <glib.h>
#include <exiv2/exiv2.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>

#include "zonedetect.h"

namespace QuiverGps
{

struct ZoneLocationInfo
{
	std::string timezone;          // e.g. "America/Los_Angeles (UTC-07:00)"
	std::string timezoneId;        // e.g. "America/Los_Angeles"
	std::string timezoneLocation;  // e.g. "Los Angeles"
	std::string countryName;       // e.g. "United States"
	std::string countryCode;       // e.g. "US"
	std::string country;           // e.g. "United States (US)"
};

// Queries ZoneDetect for timezone and geographic metadata for the given coordinates.
inline ZoneLocationInfo GetZoneLocationInfo(double lat, double lon, time_t timeContext = 0)
{
	ZoneLocationInfo info;
	static std::mutex s_zdMutex;
	static ZoneDetect* s_pZoneDetect = NULL;
	static bool s_bTriedOpen = false;

	std::lock_guard<std::mutex> lock(s_zdMutex);
	if (!s_bTriedOpen)
	{
		s_bTriedOpen = true;
		std::string dbPath = quiver_get_resource_path("timezone21.bin");
		if (!dbPath.empty() && g_file_test(dbPath.c_str(), G_FILE_TEST_EXISTS))
		{
			s_pZoneDetect = ZDOpenDatabase(dbPath.c_str());
		}
	}

	if (NULL == s_pZoneDetect)
	{
		return info;
	}

	float safezone = 0.0f;
	ZoneDetectResult* pResults = ZDLookup(s_pZoneDetect, (float)lat, (float)lon, &safezone);
	if (!pResults)
	{
		return info;
	}

	if (pResults[0].lookupResult != ZD_LOOKUP_END &&
	    pResults[0].lookupResult != ZD_LOOKUP_NOT_IN_ZONE &&
	    pResults[0].lookupResult != ZD_LOOKUP_PARSE_ERROR)
	{
		std::string prefix, id, cAlpha2, cName;
		for (unsigned int f = 0; f < pResults[0].numFields; ++f)
		{
			if (!pResults[0].fieldNames[f] || !pResults[0].data[f])
				continue;
			if (strcmp(pResults[0].fieldNames[f], "TimezoneIdPrefix") == 0)
				prefix = pResults[0].data[f];
			else if (strcmp(pResults[0].fieldNames[f], "TimezoneId") == 0)
				id = pResults[0].data[f];
			else if (strcmp(pResults[0].fieldNames[f], "CountryAlpha2") == 0)
				cAlpha2 = pResults[0].data[f];
			else if (strcmp(pResults[0].fieldNames[f], "CountryName") == 0)
				cName = pResults[0].data[f];
		}

		if (!prefix.empty() || !id.empty())
		{
			info.timezoneId = prefix + id;
			info.timezone = info.timezoneId;

			// Format timezone with UTC offset
			GTimeZone* gtz = g_time_zone_new_identifier(info.timezoneId.c_str());
			if (gtz)
			{
				GDateTime* gdtUtc = (timeContext > 0)
					? g_date_time_new_from_unix_utc(timeContext)
					: g_date_time_new_now_utc();

				if (gdtUtc)
				{
					GDateTime* gdtLocal = g_date_time_to_timezone(gdtUtc, gtz);
					if (gdtLocal)
					{
						gchar* offsetStr = g_date_time_format(gdtLocal, "UTC%:z");
						if (offsetStr)
						{
							info.timezone = info.timezoneId + " (" + offsetStr + ")";
							g_free(offsetStr);
						}
						g_date_time_unref(gdtLocal);
					}
					g_date_time_unref(gdtUtc);
				}
				g_time_zone_unref(gtz);
			}
		}

		if (!id.empty())
		{
			// Clean up id: replace '_' with ' ' and '/' with ", "
			std::string loc = id;
			size_t slash = loc.find('/');
			if (slash != std::string::npos)
			{
				std::string sub = loc.substr(slash + 1);
				std::string parent = loc.substr(0, slash);
				loc = sub + ", " + parent;
			}
			for (size_t i = 0; i < loc.size(); ++i)
			{
				if (loc[i] == '_') loc[i] = ' ';
			}
			info.timezoneLocation = loc;
		}

		info.countryName = cName;
		info.countryCode = cAlpha2;
		if (!cName.empty() && !cAlpha2.empty())
			info.country = cName + " (" + cAlpha2 + ")";
		else if (!cName.empty())
			info.country = cName;
		else if (!cAlpha2.empty())
			info.country = cAlpha2;
	}

	ZDFreeResults(pResults);
	return info;
}

// Returns the IANA timezone name (e.g. "Asia/Bangkok") for the given coordinates,
// or empty string if not found or database is missing.
inline std::string GetTimezoneName(double lat, double lon)
{
	return GetZoneLocationInfo(lat, lon).timezoneId;
}

// Returns a new GTimeZone for the given coordinates, or NULL if unavailable.
// Caller owns the returned GTimeZone (must unref with g_time_zone_unref()).
inline GTimeZone* GetGTimeZone(double lat, double lon)
{
	std::string tzName = GetTimezoneName(lat, lon);
	if (!tzName.empty())
	{
		GTimeZone* tz = g_time_zone_new_identifier(tzName.c_str());
		if (NULL != tz)
		{
			return tz;
		}
	}
	return NULL;
}

// Converts an EXIF GPS coordinate to decimal degrees.  exiv2 hands the
// DMS array over as space separated rationals ("48/1 27/1 1475/100" for
// 48 deg 27' 14.75"), so minutes and seconds are *fractions* of a degree.
// A plain decimal is accepted as already being expressed in degrees.
inline double ParseDms(const std::string& str)
{
	const double factors[3] = {1., 1. / 60., 1. / 3600.};

	double degrees = 0.;
	size_t pos = 0;
	for (int i = 0; i < 3 && std::string::npos != pos; i++)
	{
		size_t next = str.find(' ', pos);
		std::string token = (std::string::npos == next) ?
			str.substr(pos) : str.substr(pos, next - pos);
		pos = (std::string::npos == next) ? std::string::npos : next + 1;
		if (token.empty()) continue;

		size_t slash = token.find('/');
		if (std::string::npos != slash)
		{
			double num = atof(token.substr(0, slash).c_str());
			double den = atof(token.substr(slash + 1).c_str());
			if (0. != den)
				degrees += (num / den) * factors[i];
		}
		else
		{
			degrees += atof(token.c_str()) * factors[i];
		}
	}
	return degrees;
}

/* Reads an EXIF GPS coordinate as signed decimal degrees.  The magnitude
 * lives in GPSLatitude/GPSLongitude and the hemisphere in the matching
 * *Ref tag ("N"/"S"/"E"/"W"), so the ref has to be applied to get a
 * coordinate that can be pasted into a map.  Returns false when the
 * coordinate is missing or falls outside +/-dMaxAbs, so a mis-encoded
 * tag shows nothing rather than a bogus position.  refKey may be NULL
 * for tags that carry no hemisphere (e.g. altitude). */
inline bool Degrees(const std::shared_ptr<const Exiv2::ExifData>& pExifData,
	const char* coordKey, const char* refKey, double dMaxAbs,
	double& outDegrees)
{
	if (NULL == pExifData.get()) return false;

	try
	{
		auto itCoord = pExifData->findKey(Exiv2::ExifKey(coordKey));
		if (pExifData->end() == itCoord) return false;
		double degrees = ParseDms(itCoord->toString());

		if (NULL != refKey)
		{
			auto itRef = pExifData->findKey(Exiv2::ExifKey(refKey));
			if (pExifData->end() != itRef)
			{
				std::string ref = itRef->toString();
				if ('S' == ref[0] || 'W' == ref[0])
					degrees = -degrees;
			}
		}

		if (!(degrees >= -dMaxAbs && dMaxAbs >= degrees))
			return false;

		outDegrees = degrees;
		return true;
	}
	catch (...) { return false; }
}

// "lat, lon" in decimal degrees -- one line, signed, ready to paste into
// a map.  Empty when either half is missing or out of range.
inline std::string LocationString(
	const std::shared_ptr<const Exiv2::ExifData>& pExifData)
{
	double lat = 0.;
	double lon = 0.;
	if (!Degrees(pExifData, "Exif.GPSInfo.GPSLatitude",
			"Exif.GPSInfo.GPSLatitudeRef", 90.0, lat))
		return "";
	if (!Degrees(pExifData, "Exif.GPSInfo.GPSLongitude",
			"Exif.GPSInfo.GPSLongitudeRef", 180.0, lon))
		return "";

	char buf[64];
	snprintf(buf, sizeof(buf), "%.6f, %.6f", lat, lon);
	return buf;
}

// A single coordinate in decimal degrees, empty when missing.
inline std::string CoordinateString(
	const std::shared_ptr<const Exiv2::ExifData>& pExifData,
	const char* coordKey, const char* refKey)
{
	double degrees = 0.;
	// no practical bound for altitude, so effectively no range check
	if (!Degrees(pExifData, coordKey, refKey, 1.e9, degrees))
		return "";

	char buf[64];
	snprintf(buf, sizeof(buf), "%.6f", degrees);
	return buf;
}

/* Formats ISO 6709 coordinates used in a video container's "location"
 * tag (e.g. "+48.4541-123.4718/") into "lat, lon" text. */
inline std::string FormatIso6709(const std::string& raw)
{
	if (raw.empty() || ('+' != raw[0] && '-' != raw[0]))
		return raw;

	size_t split = std::string::npos;
	for (size_t j = 1; j < raw.size(); j++)
	{
		if (('+' == raw[j] || '-' == raw[j]) && j > 1)
		{
			split = j;
			break;
		}
	}
	if (std::string::npos == split)
		return raw;

	std::string lat = raw.substr(0, split);
	std::string rem = raw.substr(split);
	size_t slash = rem.find('/');
	if (std::string::npos != slash)
		rem = rem.substr(0, slash);

	size_t altStart = std::string::npos;
	for (size_t j = 1; j < rem.size(); j++)
	{
		if ('+' == rem[j] || '-' == rem[j])
		{
			altStart = j;
			break;
		}
	}

	std::string lon = (std::string::npos != altStart) ? rem.substr(0, altStart) : rem;
	return lat + ", " + lon;
}

// Normalizes an ISO 6709 container location into the same signed decimal
// degrees the EXIF path produces, so a video and a photo show the row the
// same way.  Falls back to FormatIso6709() when the numbers are not
// decimal degrees.
inline std::string LocationFromIso6709(const std::string& raw)
{
	const std::string split = FormatIso6709(raw);
	if (std::string::npos != split.find(','))
	{
		const size_t comma = split.find(',');
		const std::string lat = split.substr(0, comma);
		const std::string lon = split.substr(comma + 1);
		char* latEnd = NULL;
		char* lonEnd = NULL;
		const double dLat = strtod(lat.c_str(), &latEnd);
		const double dLon = strtod(lon.c_str(), &lonEnd);
		// both halves have to be plain numbers, and they have to be in
		// range, otherwise the raw text is all we can show
		if (latEnd != lat.c_str() && *latEnd == '\0' &&
			lonEnd != lon.c_str() && *lonEnd == '\0' &&
			dLat >= -90.0 && dLat <= 90.0 && dLon >= -180.0 && dLon <= 180.0)
		{
			char buf[64];
			snprintf(buf, sizeof(buf), "%.6f, %.6f", dLat, dLon);
			return buf;
		}
	}
	return split;
}

// Parses an ISO 6709 coordinate string into decimal degrees (and optional altitude in meters).
inline bool ParseIso6709(const std::string& raw, double& outLat, double& outLon, bool& outHasAlt, double& outAlt)
{
	outHasAlt = false;
	outAlt = 0.0;
	if (raw.empty() || ('+' != raw[0] && '-' != raw[0]))
		return false;

	size_t split = std::string::npos;
	for (size_t j = 1; j < raw.size(); j++)
	{
		if (('+' == raw[j] || '-' == raw[j]) && j > 1)
		{
			split = j;
			break;
		}
	}
	if (std::string::npos == split)
		return false;

	std::string latStr = raw.substr(0, split);
	std::string rem = raw.substr(split);
	size_t slash = rem.find('/');
	if (std::string::npos != slash)
		rem = rem.substr(0, slash);

	size_t altStart = std::string::npos;
	for (size_t j = 1; j < rem.size(); j++)
	{
		if ('+' == rem[j] || '-' == rem[j])
		{
			altStart = j;
			break;
		}
	}

	std::string lonStr = (std::string::npos != altStart) ? rem.substr(0, altStart) : rem;
	char* endP = NULL;
	double dLat = strtod(latStr.c_str(), &endP);
	if (endP == latStr.c_str() || *endP != '\0') return false;

	double dLon = strtod(lonStr.c_str(), &endP);
	if (endP == lonStr.c_str() || *endP != '\0') return false;

	if (dLat < -90.0 || dLat > 90.0 || dLon < -180.0 || dLon > 180.0)
		return false;

	outLat = dLat;
	outLon = dLon;

	if (std::string::npos != altStart)
	{
		std::string altStr = rem.substr(altStart);
		double dAlt = strtod(altStr.c_str(), &endP);
		if (endP != altStr.c_str() && *endP == '\0')
		{
			outHasAlt = true;
			outAlt = dAlt;
		}
	}

	return true;
}

// Writes GPS tags into Exiv2::ExifData in standard EXIF rational format.
inline void SetExifGps(Exiv2::ExifData& exifData, double dLat, double dLon, bool hasAlt = false, double dAlt = 0.0)
{
	exifData["Exif.GPSInfo.GPSVersionID"] = "2 2 0 0";
	exifData["Exif.GPSInfo.GPSMapDatum"] = "WGS-84";
	exifData["Exif.GPSInfo.GPSLatitudeRef"] = (dLat >= 0.0) ? "N" : "S";
	exifData["Exif.GPSInfo.GPSLongitudeRef"] = (dLon >= 0.0) ? "E" : "W";

	double absLat = std::fabs(dLat);
	int latDeg = (int)absLat;
	int latMin = (int)((absLat - latDeg) * 60.0);
	double latSec = ((absLat - latDeg) * 60.0 - latMin) * 60.0;
	char szLat[64];
	snprintf(szLat, sizeof(szLat), "%d/1 %d/1 %d/1000",
		latDeg, latMin, (int)(latSec * 1000.0 + 0.5));
	exifData["Exif.GPSInfo.GPSLatitude"] = szLat;

	double absLon = std::fabs(dLon);
	int lonDeg = (int)absLon;
	int lonMin = (int)((absLon - lonDeg) * 60.0);
	double lonSec = ((absLon - lonDeg) * 60.0 - lonMin) * 60.0;
	char szLon[64];
	snprintf(szLon, sizeof(szLon), "%d/1 %d/1 %d/1000",
		lonDeg, lonMin, (int)(lonSec * 1000.0 + 0.5));
	exifData["Exif.GPSInfo.GPSLongitude"] = szLon;

	if (hasAlt)
	{
		exifData["Exif.GPSInfo.GPSAltitudeRef"] = (dAlt >= 0.0) ? "0" : "1";
		double absAlt = std::fabs(dAlt);
		char szAlt[64];
		snprintf(szAlt, sizeof(szAlt), "%d/1000", (int)(absAlt * 1000.0 + 0.5));
		exifData["Exif.GPSInfo.GPSAltitude"] = szAlt;
	}
}

// Reads GPS coordinates and altitude from Exiv2::ExifData.
inline bool GetExifGps(const Exiv2::ExifData& exifData, double& outLat, double& outLon, bool& outHasAlt, double& outAlt)
{
	auto pExif = std::make_shared<const Exiv2::ExifData>(exifData);
	outHasAlt = false;
	outAlt = 0.0;
	if (!Degrees(pExif, "Exif.GPSInfo.GPSLatitude", "Exif.GPSInfo.GPSLatitudeRef", 90.0, outLat))
		return false;
	if (!Degrees(pExif, "Exif.GPSInfo.GPSLongitude", "Exif.GPSInfo.GPSLongitudeRef", 180.0, outLon))
		return false;
	if (Degrees(pExif, "Exif.GPSInfo.GPSAltitude", "Exif.GPSInfo.GPSAltitudeRef", 1.e9, outAlt))
	{
		outHasAlt = true;
	}
	return true;
}

// Formats latitude and longitude (and optional altitude) into ISO 6709 string.
inline std::string FormatIso6709FromCoordinates(double lat, double lon, bool hasAlt = false, double alt = 0.0)
{
	char buf[128];
	if (hasAlt)
	{
		snprintf(buf, sizeof(buf), "%+.4f%+.4f%+.3f/", lat, lon, alt);
	}
	else
	{
		snprintf(buf, sizeof(buf), "%+.4f%+.4f/", lat, lon);
	}
	return buf;
}

} // namespace QuiverGps

#endif // QUIVER_GPS_H
