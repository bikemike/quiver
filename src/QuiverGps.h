#ifndef QUIVER_GPS_H
#define QUIVER_GPS_H

// GPS coordinate formatting shared by the property view and the unit tests.
// Everything here is a pure string/number transform over Exiv2 metadata.

#include <exiv2/exiv2.hpp>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

namespace QuiverGps
{

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
inline bool Degrees(const std::shared_ptr<Exiv2::ExifData>& pExifData,
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
	const std::shared_ptr<Exiv2::ExifData>& pExifData)
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
	const std::shared_ptr<Exiv2::ExifData>& pExifData,
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
	std::string lon = raw.substr(split);
	size_t slash = lon.find('/');
	if (std::string::npos != slash)
		lon = lon.substr(0, slash);
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

} // namespace QuiverGps

#endif // QUIVER_GPS_H
