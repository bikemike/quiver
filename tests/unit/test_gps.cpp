#include "catch2/catch_test_macros.hpp"

#include "QuiverGps.h"
#include "QuiverFile.h"

#include <cmath>
#include <ctime>
#include <memory>

namespace
{

std::shared_ptr<Exiv2::ExifData> MakeExif()
{
	return std::make_shared<Exiv2::ExifData>();
}

void SetDms(Exiv2::ExifData& exif, const char* refKey, const char* value)
{
	exif[refKey] = std::string(value);
}

constexpr double kEps = 1e-9;

} // anonymous namespace

TEST_CASE("ParseDms converts EXIF rational DMS to decimal degrees",
	"[gps]")
{
	// Pixel 7 style: "48/1 27/1 1475/100" is 48 deg 27' 14.75"
	REQUIRE(std::fabs(QuiverGps::ParseDms("48/1 27/1 1475/100") -
		48.454097222) < kEps);
	REQUIRE(std::fabs(QuiverGps::ParseDms("123/1 28/1 1859/100") -
		123.471830556) < kEps);

	// whole degree / no seconds
	REQUIRE(std::fabs(QuiverGps::ParseDms("37/1 55/1 0/1") -
		37.916666667) < kEps);

	// zero degrees
	REQUIRE(std::fabs(QuiverGps::ParseDms("0/1 27/1 1475/100") -
		0.454097222) < kEps);

	// single decimal and rational-degrees forms
	REQUIRE(std::fabs(QuiverGps::ParseDms("48.4541") - 48.4541) < kEps);
	REQUIRE(std::fabs(QuiverGps::ParseDms("4854/100") - 48.54) < kEps);

	// empty and junk must not blow up
	REQUIRE(QuiverGps::ParseDms("") == 0.0);
	REQUIRE(QuiverGps::ParseDms("nonsense") == 0.0);

	// the historic bug multiplied minutes by 60 and seconds by 3600,
	// which pushed a valid latitude far past the pole
	REQUIRE(QuiverGps::ParseDms("48/1 27/1 1475/100") < 90.0);
	REQUIRE(QuiverGps::ParseDms("123/1 28/1 1859/100") < 180.0);
}

TEST_CASE("LocationString applies hemisphere refs and range checks",
	"[gps]")
{
	auto exif = MakeExif();

	SECTION("missing tags produce nothing")
	{
		REQUIRE(QuiverGps::LocationString(exif).empty());
	}

	SECTION("north west, matching a Pixel 7 capture")
	{
		SetDms(*exif, "Exif.GPSInfo.GPSLatitudeRef", "N");
		SetDms(*exif, "Exif.GPSInfo.GPSLatitude", "48/1 27/1 1475/100");
		SetDms(*exif, "Exif.GPSInfo.GPSLongitudeRef", "W");
		SetDms(*exif, "Exif.GPSInfo.GPSLongitude", "123/1 28/1 1859/100");
		REQUIRE(QuiverGps::LocationString(exif) == "48.454097, -123.471831");
	}

	SECTION("south east is signed negatively and positively")
	{
		SetDms(*exif, "Exif.GPSInfo.GPSLatitudeRef", "S");
		SetDms(*exif, "Exif.GPSInfo.GPSLatitude", "33/1 52/1 0/1");
		SetDms(*exif, "Exif.GPSInfo.GPSLongitudeRef", "E");
		SetDms(*exif, "Exif.GPSInfo.GPSLongitude", "151/1 12/1 0/1");
		REQUIRE(QuiverGps::LocationString(exif) == "-33.866667, 151.200000");
	}

	SECTION("a latitude past the pole is rejected instead of shown")
	{
		SetDms(*exif, "Exif.GPSInfo.GPSLatitudeRef", "N");
		SetDms(*exif, "Exif.GPSInfo.GPSLatitude", "48/1 27/1 1475/100");
		SetDms(*exif, "Exif.GPSInfo.GPSLongitudeRef", "W");
		SetDms(*exif, "Exif.GPSInfo.GPSLongitude", "361/1 28/1 1859/100");
		REQUIRE(QuiverGps::LocationString(exif).empty());
	}

	SECTION("longitude alone is not enough")
	{
		SetDms(*exif, "Exif.GPSInfo.GPSLongitudeRef", "W");
		SetDms(*exif, "Exif.GPSInfo.GPSLongitude", "123/1 28/1 1859/100");
		REQUIRE(QuiverGps::LocationString(exif).empty());
	}

	SECTION("no ref tag leaves the magnitude unsigned")
	{
		SetDms(*exif, "Exif.GPSInfo.GPSLatitude", "48/1 27/1 1475/100");
		SetDms(*exif, "Exif.GPSInfo.GPSLongitude", "123/1 28/1 1859/100");
		REQUIRE(QuiverGps::LocationString(exif) == "48.454097, 123.471831");
	}
}

TEST_CASE("CoordinateString reads single tags such as altitude", "[gps]")
{
	auto exif = MakeExif();
	SetDms(*exif, "Exif.GPSInfo.GPSAltitude", "4070/100");
	REQUIRE(QuiverGps::CoordinateString(exif,
		"Exif.GPSInfo.GPSAltitude", NULL) == "40.700000");

	REQUIRE(QuiverGps::CoordinateString(exif,
		"Exif.GPSInfo.GPSSpeed", NULL).empty());
}

TEST_CASE("FormatIso6709 normalizes container location tags", "[gps]")
{
	REQUIRE(QuiverGps::FormatIso6709("+48.4541-123.4718/") ==
		"+48.4541, -123.4718");
	REQUIRE(QuiverGps::FormatIso6709("+48.4541+123.4718/") ==
		"+48.4541, +123.4718");
	REQUIRE(QuiverGps::FormatIso6709(
		"-33.8688+151.2093/CRSWGS_84/") == "-33.8688, +151.2093");
	REQUIRE(QuiverGps::FormatIso6709("+48.4541-123.4718") ==
		"+48.4541, -123.4718");
	// coordinates with altitude (e.g. Google Pixel / iPhone QuickTime atoms)
	REQUIRE(QuiverGps::FormatIso6709("+37.7749-122.4194+015.000/") ==
		"+37.7749, -122.4194");
	REQUIRE(QuiverGps::FormatIso6709("+37.7749-122.4194-015.000/") ==
		"+37.7749, -122.4194");
	REQUIRE(QuiverGps::FormatIso6709("+37.7749-122.4194+015.000/CRSWGS_84/") ==
		"+37.7749, -122.4194");
	// no leading sign means it is not an ISO 6709 string: pass through
	REQUIRE(QuiverGps::FormatIso6709("somewhere") == "somewhere");
	REQUIRE(QuiverGps::FormatIso6709("") == "");
}

TEST_CASE("LocationFromIso6709 matches the EXIF row format", "[gps]")
{
	/* a video location has to read like the photo location so the summary
	 * row lines up between the two media types */
	REQUIRE(QuiverGps::LocationFromIso6709("+48.4541-123.4718/") ==
		"48.454100, -123.471800");
	REQUIRE(QuiverGps::LocationFromIso6709("+48.4541+123.4718/") ==
		"48.454100, 123.471800");
	REQUIRE(QuiverGps::LocationFromIso6709("-33.8688+151.2093/") ==
		"-33.868800, 151.209300");
	REQUIRE(QuiverGps::LocationFromIso6709("+48.4541-123.4718/CRSWGS_84/") ==
		"48.454100, -123.471800");
	REQUIRE(QuiverGps::LocationFromIso6709("+37.7749-122.4194+015.000/") ==
		"37.774900, -122.419400");
	REQUIRE(QuiverGps::LocationFromIso6709("+37.7749-122.4194-015.000/") ==
		"37.774900, -122.419400");
	REQUIRE(QuiverGps::LocationFromIso6709("+0.0+0.0/") ==
		"0.000000, 0.000000");

	/* out of range or non numeric values fall back to the split text */
	REQUIRE(QuiverGps::LocationFromIso6709("+948.4541-123.4718/") ==
		"+948.4541, -123.4718");
	REQUIRE(QuiverGps::LocationFromIso6709("here") == "here");
	REQUIRE(QuiverGps::LocationFromIso6709("") == "");
}

TEST_CASE("ParseIso6709 and SetExifGps/GetExifGps round-trip GPS data", "[gps]")
{
	double lat = 0.0, lon = 0.0, alt = 0.0;
	bool hasAlt = false;

	SECTION("Google Pixel video with altitude")
	{
		REQUIRE(QuiverGps::ParseIso6709("+37.7749-122.4194+015.000/", lat, lon, hasAlt, alt));
		REQUIRE(std::fabs(lat - 37.7749) < 1e-4);
		REQUIRE(std::fabs(lon - (-122.4194)) < 1e-4);
		REQUIRE(hasAlt);
		REQUIRE(std::fabs(alt - 15.0) < 1e-3);

		Exiv2::ExifData exif;
		QuiverGps::SetExifGps(exif, lat, lon, hasAlt, alt);

		double rLat = 0.0, rLon = 0.0, rAlt = 0.0;
		bool rHasAlt = false;
		REQUIRE(QuiverGps::GetExifGps(exif, rLat, rLon, rHasAlt, rAlt));
		REQUIRE(std::fabs(rLat - 37.7749) < 1e-3);
		REQUIRE(std::fabs(rLon - (-122.4194)) < 1e-3);
		REQUIRE(rHasAlt);
		REQUIRE(std::fabs(rAlt - 15.0) < 1e-2);

		std::string isoOut = QuiverGps::FormatIso6709FromCoordinates(rLat, rLon, rHasAlt, rAlt);
		REQUIRE(isoOut.find("+37.7749") != std::string::npos);
		REQUIRE(isoOut.find("-122.4194") != std::string::npos);
	}

	SECTION("Southern and eastern hemispheres with negative altitude")
	{
		REQUIRE(QuiverGps::ParseIso6709("-33.8688+151.2093-005.500/", lat, lon, hasAlt, alt));
		REQUIRE(std::fabs(lat - (-33.8688)) < 1e-4);
		REQUIRE(std::fabs(lon - 151.2093) < 1e-4);
		REQUIRE(hasAlt);
		REQUIRE(std::fabs(alt - (-5.5)) < 1e-3);

		Exiv2::ExifData exif;
		QuiverGps::SetExifGps(exif, lat, lon, hasAlt, alt);

		double rLat = 0.0, rLon = 0.0, rAlt = 0.0;
		bool rHasAlt = false;
		REQUIRE(QuiverGps::GetExifGps(exif, rLat, rLon, rHasAlt, rAlt));
		REQUIRE(std::fabs(rLat - (-33.8688)) < 1e-3);
		REQUIRE(std::fabs(rLon - 151.2093) < 1e-3);
	}
}

TEST_CASE("ZoneDetect coordinate to timezone lookup and video time resolution", "[gps][timezone]")
{
	SECTION("GetTimezoneName resolves coordinates accurately")
	{
		// Ninh Binh, Vietnam
		std::string tzVietnam = QuiverGps::GetTimezoneName(20.2261, 105.9188);
		REQUIRE(tzVietnam == "Asia/Bangkok");

		// Paris, France
		std::string tzParis = QuiverGps::GetTimezoneName(48.8566, 2.3522);
		REQUIRE(tzParis == "Europe/Paris");

		// New York, USA
		std::string tzNY = QuiverGps::GetTimezoneName(40.7128, -74.0060);
		REQUIRE(tzNY == "America/New_York");
	}

	SECTION("GetZoneLocationInfo resolves detailed location and country metadata")
	{
		// San Francisco / Bay Area, USA
		QuiverGps::ZoneLocationInfo sf = QuiverGps::GetZoneLocationInfo(37.7749, -122.4194);
		REQUIRE(sf.timezoneId == "America/Los_Angeles");
		REQUIRE(sf.timezoneLocation == "Los Angeles");
		REQUIRE(sf.countryCode == "US");
		REQUIRE(sf.countryName == "United States");
		REQUIRE(sf.country == "United States (US)");
		REQUIRE(sf.timezone.rfind("America/Los_Angeles (UTC", 0) == 0);

		// Paris, France
		QuiverGps::ZoneLocationInfo paris = QuiverGps::GetZoneLocationInfo(48.8566, 2.3522);
		REQUIRE(paris.timezoneId == "Europe/Paris");
		REQUIRE(paris.timezoneLocation == "Paris");
		REQUIRE(paris.countryCode == "FR");
		REQUIRE(paris.countryName == "France");
		REQUIRE(paris.country == "France (FR)");

		// Victoria BC, Canada
		QuiverGps::ZoneLocationInfo victoria = QuiverGps::GetZoneLocationInfo(48.4541, -123.4718);
		REQUIRE(victoria.timezoneId == "America/Vancouver");
		REQUIRE(victoria.timezoneLocation == "Vancouver");
		REQUIRE(victoria.countryCode == "CA");
		REQUIRE(victoria.countryName == "Canada");
		REQUIRE(victoria.country == "Canada (CA)");

		// Ninh Binh, Vietnam (maps to Asia/Bangkok zone)
		QuiverGps::ZoneLocationInfo vn = QuiverGps::GetZoneLocationInfo(20.2261, 105.9188);
		REQUIRE(vn.timezoneId == "Asia/Bangkok");
		REQUIRE(vn.timezoneLocation == "Bangkok");
		REQUIRE(vn.countryCode == "TH");
	}

	SECTION("Video with GPS resolves creation time in location timezone")
	{
		const char* videoPath = "/workspace/2024-04-03 01.12.21-01.pxl.mp4";
		if (g_file_test(videoPath, G_FILE_TEST_EXISTS))
		{
			gchar* uri = g_filename_to_uri(videoPath, NULL, NULL);
			REQUIRE(uri != NULL);

			QuiverFile f(uri);
			time_t t = f.GetTimeT(true);
			REQUIRE(t > 0);

			struct tm tm_local = {};
			localtime_r(&t, &tm_local);

			// In Vietnam (UTC+7), 08:12:21 UTC is 15:12:21 local time (3:12 PM)
			REQUIRE(tm_local.tm_hour == 15);
			REQUIRE(tm_local.tm_min == 12);
			REQUIRE(tm_local.tm_sec == 21);
			REQUIRE(tm_local.tm_year == 124); // 2024 - 1900
			REQUIRE(tm_local.tm_mon == 3);    // April (0-indexed)
			REQUIRE(tm_local.tm_mday == 3);

			g_free(uri);
		}
	}
}

