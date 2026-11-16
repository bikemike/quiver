#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "ImageFilterState.h"
#include "QuiverFile.h"
#include "QuiverGps.h"
#include "test_helpers.h"

#include <exiv2/exiv2.hpp>
#include <gio/gio.h>
#include <glib/gstdio.h>

#include <string>
#include <unistd.h>
#include <vector>

namespace
{
struct TempFile
{
	std::string path;

	TempFile() = default;
	TempFile(const TempFile&) = delete;
	TempFile& operator=(const TempFile&) = delete;
	TempFile(TempFile&& other) noexcept : path(std::move(other.path))
	{
		other.path.clear();
	}
	TempFile& operator=(TempFile&& other) noexcept
	{
		if (this != &other)
		{
			if (!path.empty())
				g_unlink(path.c_str());
			path = std::move(other.path);
			other.path.clear();
		}
		return *this;
	}
	~TempFile()
	{
		if (!path.empty())
			g_unlink(path.c_str());
	}

	std::string URI() const
	{
		gchar* uri = g_filename_to_uri(path.c_str(), NULL, NULL);
		std::string result = uri ? uri : "";
		g_free(uri);
		return result;
	}
};

TempFile MakeTempPath(const char* suffix)
{
	std::string tmpl = std::string("/tmp/quiver_filter_XXXXXX") + suffix;
	std::vector<char> buffer(tmpl.begin(), tmpl.end());
	buffer.push_back('\0');
	int fd = g_mkstemp(buffer.data());
	REQUIRE(fd >= 0);
	close(fd);
	TempFile file;
	file.path = buffer.data();
	return file;
}

TempFile MakeTempBytes(std::size_t size, const char* suffix)
{
	TempFile file = MakeTempPath(suffix);
	std::string data(size, 'x');
	REQUIRE(g_file_set_contents(file.path.c_str(), data.data(),
		(gsize)data.size(), NULL));
	return file;
}

TempFile CopySample(const std::string& name, const char* suffix)
{
	std::string source = QuiverTest_GetImagesDir() + "/" + name;
	REQUIRE(g_file_test(source.c_str(), G_FILE_TEST_EXISTS));
	TempFile file = MakeTempPath(suffix);
	char* contents = nullptr;
	gsize length = 0;
	REQUIRE(g_file_get_contents(source.c_str(), &contents, &length, NULL));
	REQUIRE(g_file_set_contents(file.path.c_str(), contents, length, NULL));
	g_free(contents);
	return file;
}

void WriteGps(const TempFile& file, double lat, double lon)
{
	auto image = Exiv2::ImageFactory::open(file.path);
	REQUIRE(image.get() != nullptr);
	image->readMetadata();
	QuiverGps::SetExifGps(image->exifData(), lat, lon, false, 0.0);
	image->writeMetadata();
}

void WriteDateAndGps(const TempFile& file)
{
	auto image = Exiv2::ImageFactory::open(file.path);
	REQUIRE(image.get() != nullptr);
	image->readMetadata();
	image->exifData()["Exif.Photo.DateTimeOriginal"] = "2024:06:15 10:20:30";
	image->exifData()["Exif.GPSInfo.GPSLatitudeRef"] = "N";
	image->exifData()["Exif.GPSInfo.GPSLatitude"] = "48/1 27/1 1475/100";
	image->exifData()["Exif.GPSInfo.GPSLongitudeRef"] = "W";
	image->exifData()["Exif.GPSInfo.GPSLongitude"] = "123/1 28/1 1859/100";
	image->writeMetadata();
}
} // namespace

TEST_CASE("ImageFilterState defaults and emptiness", "[unit][filter][fast]")
{
	ImageFilterState state;
	REQUIRE(state.IsEmpty());
	REQUIRE_FALSE(state.HasSpatialFilter());

	state.text = "x";
	REQUIRE_FALSE(state.IsEmpty());

	state = ImageFilterState();
	state.area.active = true;
	REQUIRE_FALSE(state.IsEmpty());
	REQUIRE(state.HasSpatialFilter());
}

TEST_CASE("ImageFilterState text matches filename and path case-insensitively",
	"[unit][filter][fast]")
{
	TempFile file = MakeTempBytes(16, ".jpg");
	QuiverFile qf(file.URI().c_str());

	ImageFilterState state;
	state.text = "QUIVER_FILTER";
	REQUIRE(state.MatchesBase(qf, false));
	REQUIRE(state.MatchesViewer(qf, false));

	state.text = "not-present-anywhere";
	REQUIRE_FALSE(state.MatchesBase(qf, false));
	REQUIRE_FALSE(state.MatchesViewer(qf, false));
}

TEST_CASE("ImageFilterState size range is inclusive", "[unit][filter][fast]")
{
	TempFile file = MakeTempBytes(2048, ".bin");
	QuiverFile qf(file.URI().c_str());
	REQUIRE(qf.GetFileSize() == 2048);

	ImageFilterState state;
	state.sizeActive = true;
	state.sizeMin = 2048;
	state.sizeMax = 2048;
	REQUIRE(state.MatchesBase(qf, false));

	state.sizeMin = 2049;
	REQUIRE_FALSE(state.MatchesBase(qf, false));

	state.sizeMin = 0;
	state.sizeMax = 2047;
	REQUIRE_FALSE(state.MatchesBase(qf, false));

	state.sizeMax = ~0ULL;
	REQUIRE(state.MatchesBase(qf, false));
}

TEST_CASE("ImageFilterState media filter separates photos and videos",
	"[unit][filter][fast]")
{
	TempFile photo = CopySample("sample_4k.jpg", ".jpg");
	TempFile video = CopySample("sample_video.mp4", ".mp4");
	QuiverFile photoFile(photo.URI().c_str());
	QuiverFile videoFile(video.URI().c_str());

	ImageFilterState state;
	state.media = ImageFilterState::MediaType::Photos;
	REQUIRE(state.MatchesBase(photoFile, false));
	REQUIRE_FALSE(state.MatchesBase(videoFile, false));

	state.media = ImageFilterState::MediaType::Videos;
	REQUIRE_FALSE(state.MatchesBase(photoFile, false));
	REQUIRE(state.MatchesBase(videoFile, false));
}

TEST_CASE("ImageFilterState date filter uses cached date taken only",
	"[unit][filter][date]")
{
	TempFile file = CopySample("sample_4k.jpg", ".jpg");
	WriteDateAndGps(file);
	QuiverFile qf(file.URI().c_str());

	REQUIRE_FALSE(qf.HasCachedDateTaken());

	ImageFilterState state;
	state.dateActive = true;
	state.dateStart = 0;
	state.dateEnd = ~(time_t)0;
	REQUIRE(state.MatchesBase(qf, false));
	REQUIRE_FALSE(state.MatchesBase(qf, true));

	REQUIRE(qf.ResolveDateTaken());
	time_t taken = 0;
	REQUIRE(qf.GetCachedDateTaken(taken));

	state.dateStart = taken - 1;
	state.dateEnd = taken + 1;
	REQUIRE(state.MatchesBase(qf, true));

	state.dateStart = taken + 2;
	state.dateEnd = taken + 3;
	REQUIRE_FALSE(state.MatchesBase(qf, true));
}

TEST_CASE("ImageFilterState GPS filter uses cached coordinates only",
	"[unit][filter][gps]")
{
	TempFile file = CopySample("sample_4k.jpg", ".jpg");
	WriteDateAndGps(file);
	QuiverFile qf(file.URI().c_str());

	REQUIRE_FALSE(qf.HasCachedGPS());

	double lat = 0.0, lon = 0.0, alt = 0.0;
	bool hasAlt = false;
	REQUIRE(qf.GetGPSCoordinates(lat, lon, hasAlt, alt));
	REQUIRE(lat > 48.4);
	REQUIRE(lat < 48.5);
	REQUIRE(lon < -123.4);
	REQUIRE(lon > -123.5);

	ImageFilterState state;
	state.gps = ImageFilterState::GpsFilter::HasGps;
	REQUIRE(state.MatchesBase(qf, false));
	REQUIRE(state.MatchesBase(qf, true));

	state.gps = ImageFilterState::GpsFilter::NoGps;
	REQUIRE_FALSE(state.MatchesBase(qf, true));
}

TEST_CASE("ImageFilterState spatial filter excludes folders and no-GPS files",
	"[unit][filter][map]")
{
	TempFile located = CopySample("sample_4k.jpg", ".jpg");
	WriteDateAndGps(located);
	QuiverFile locatedFile(located.URI().c_str());

	double lat = 0.0, lon = 0.0, alt = 0.0;
	bool hasAlt = false;
	REQUIRE(locatedFile.GetGPSCoordinates(lat, lon, hasAlt, alt));

	TempFile unlocated = CopySample("sample_4k.jpg", ".jpg");
	QuiverFile unlocatedFile(unlocated.URI().c_str());

	gchar* folderUri = g_filename_to_uri("/tmp", NULL, NULL);
	REQUIRE(folderUri != NULL);
	QuiverFile folder(folderUri);
	g_free(folderUri);
	REQUIRE(folder.IsFolder());

	ImageFilterState state;
	state.area.active = true;
	state.area.latMin = 48.0;
	state.area.latMax = 49.0;
	state.area.lonMin = -124.0;
	state.area.lonMax = -123.0;

	REQUIRE(state.MatchesSpatial(locatedFile));
	REQUIRE_FALSE(state.MatchesSpatial(unlocatedFile));
	REQUIRE_FALSE(state.MatchesSpatial(folder));
	REQUIRE_FALSE(state.MatchesViewer(folder, false));

	state.area.latMin = 10.0;
	state.area.latMax = 11.0;
	REQUIRE_FALSE(state.MatchesSpatial(locatedFile));
}

TEST_CASE("ImageFilterState spatial filter supports antimeridian wrap",
	"[unit][filter][fast]")
{
	TempFile westFile = CopySample("sample_4k.jpg", ".jpg");
	WriteGps(westFile, 37.4220, -122.0841);
	QuiverFile west(westFile.URI().c_str());

	TempFile eastFile = CopySample("sample_4k.jpg", ".jpg");
	WriteGps(eastFile, 10.0, 175.0);
	QuiverFile east(eastFile.URI().c_str());

	double lat = 0.0, lon = 0.0, alt = 0.0;
	bool hasAlt = false;
	REQUIRE(west.GetGPSCoordinates(lat, lon, hasAlt, alt));
	REQUIRE(east.GetGPSCoordinates(lat, lon, hasAlt, alt));

	ImageFilterState state;
	state.area.active = true;
	state.area.latMin = 30.0;
	state.area.latMax = 50.0;
	state.area.lonMin = -130.0;
	state.area.lonMax = -110.0;
	REQUIRE(state.MatchesSpatial(west));
	REQUIRE_FALSE(state.MatchesSpatial(east));

	state.area.latMin = 0.0;
	state.area.latMax = 20.0;
	state.area.lonMin = 170.0;
	state.area.lonMax = -170.0;
	REQUIRE(state.MatchesSpatial(east));
	REQUIRE_FALSE(state.MatchesSpatial(west));
}
