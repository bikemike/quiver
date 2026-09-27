#include "catch2/catch_test_macros.hpp"

#include "QuiverGps.h"

#include <cmath>
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
	REQUIRE(QuiverGps::LocationFromIso6709("+0.0+0.0/") ==
		"0.000000, 0.000000");

	/* out of range or non numeric values fall back to the split text */
	REQUIRE(QuiverGps::LocationFromIso6709("+948.4541-123.4718/") ==
		"+948.4541, -123.4718");
	REQUIRE(QuiverGps::LocationFromIso6709("here") == "here");
	REQUIRE(QuiverGps::LocationFromIso6709("") == "");
}
