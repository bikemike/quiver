#include "catch2/catch_test_macros.hpp"

#include "QuiverSummary.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{

bool Has(const std::vector<std::string>& v, const std::string& s)
{
	return std::find(v.begin(), v.end(), s) != v.end();
}

size_t IndexOf(const std::vector<std::string>& v, const std::string& s)
{
	const auto it = std::find(v.begin(), v.end(), s);
	return (v.end() == it) ? v.size() : (size_t)(it - v.begin());
}

} // anonymous namespace

TEST_CASE("Summary layout shares one order for images and videos", "[summary]")
{
	const std::vector<std::string> common = QuiverSummary::CommonRows();
	const std::vector<std::string> image = QuiverSummary::Layout(false);
	const std::vector<std::string> video = QuiverSummary::Layout(true);

	/* the properties an image and a video have in common must come out in
	 * the same order, so those rows line up when the user flips between
	 * media types */
	REQUIRE(std::equal(common.begin(), common.end(), image.begin()));
	REQUIRE(std::equal(common.begin(), common.end(), video.begin()));
}

TEST_CASE("Summary layout puts media specific properties last", "[summary]")
{
	const std::vector<std::string> image = QuiverSummary::Layout(false);
	const std::vector<std::string> video = QuiverSummary::Layout(true);
	const size_t common = QuiverSummary::CommonRows().size();

	SECTION("the photo only properties follow the shared ones")
	{
		const std::vector<std::string> only = QuiverSummary::ImageRows();
		REQUIRE(image.size() == common + only.size());
		REQUIRE(std::equal(only.begin(), only.end(), image.begin() + common));
		REQUIRE(Has(image, "Software"));
		REQUIRE(Has(image, "Artist"));
		REQUIRE(!Has(image, "Duration"));
		REQUIRE(!Has(image, "Codecs"));
	}

	SECTION("the video only properties follow the shared ones")
	{
		const std::vector<std::string> only = QuiverSummary::VideoRows();
		REQUIRE(video.size() == common + only.size());
		REQUIRE(std::equal(only.begin(), only.end(), video.begin() + common));
		REQUIRE(Has(video, "Duration"));
		REQUIRE(Has(video, "Codecs"));
		REQUIRE(Has(video, "Container"));
		REQUIRE(!Has(video, "Software"));
		REQUIRE(!Has(video, "Artist"));
	}
}

TEST_CASE("Summary layout ranks the most useful properties first", "[summary]")
{
	const std::vector<std::string> common = QuiverSummary::CommonRows();

	/* what the user compares at a glance comes before the bookkeeping */
	REQUIRE(common[0] == "File Name");
	const std::vector<std::string> key = { "Date Taken", "Dimensions",
		"GPS Location" };
	for (const std::string& k : key)
	{
		REQUIRE(Has(common, k));
		REQUIRE(IndexOf(common, k) < IndexOf(common, "File Size"));
	}
	REQUIRE(IndexOf(common, "File Size") <
		IndexOf(common, "Last Modified"));
	REQUIRE(IndexOf(common, "Last Modified") < IndexOf(common, "Type"));

	/* camera is often missing, so it goes below the file properties */
	REQUIRE(Has(common, "Camera"));
	REQUIRE(IndexOf(common, "Type") < IndexOf(common, "Camera"));
	REQUIRE(IndexOf(common, "Camera") == common.size() - 1);
}
