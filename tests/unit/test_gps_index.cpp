#include "catch2/catch_test_macros.hpp"

#include "ImageList.h"
#include "QuiverGpsIndex.h"
#include "test_helpers.h"

#include <exiv2/exiv2.hpp>
#include <gio/gio.h>
#include <glib/gstdio.h>

#include <list>
#include <string>
#include <unistd.h>
#include <vector>

namespace
{
struct TempJpeg
{
	std::string path;

	TempJpeg()
	{
		std::string tmpl = "/tmp/quiver_gps_XXXXXX.jpg";
		std::vector<char> buffer(tmpl.begin(), tmpl.end());
		buffer.push_back('\0');
		int fd = g_mkstemp(buffer.data());
		REQUIRE(fd >= 0);
		close(fd);
		path = buffer.data();
	}

	TempJpeg(const TempJpeg&) = delete;
	TempJpeg& operator=(const TempJpeg&) = delete;
	TempJpeg(TempJpeg&& other) noexcept : path(std::move(other.path))
	{
		other.path.clear();
	}
	TempJpeg& operator=(TempJpeg&& other) noexcept
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

	~TempJpeg()
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

TempJpeg CopySample(const std::string& name)
{
	TempJpeg file;
	const std::string source = QuiverTest_GetImagesDir() + "/" + name;
	char* contents = nullptr;
	gsize length = 0;
	REQUIRE(g_file_get_contents(source.c_str(), &contents, &length, NULL));
	REQUIRE(g_file_set_contents(file.path.c_str(), contents, length, NULL));
	g_free(contents);
	return file;
}

void WriteExif(const TempJpeg& file, bool withGps)
{
	auto image = Exiv2::ImageFactory::open(file.path);
	REQUIRE(image.get() != nullptr);
	image->readMetadata();
	image->exifData()["Exif.Photo.DateTimeOriginal"] = "2024:06:15 10:20:30";
	if (withGps)
	{
		image->exifData()["Exif.GPSInfo.GPSLatitudeRef"] = "N";
		image->exifData()["Exif.GPSInfo.GPSLatitude"] = "48/1 27/1 1475/100";
		image->exifData()["Exif.GPSInfo.GPSLongitudeRef"] = "W";
		image->exifData()["Exif.GPSInfo.GPSLongitude"] = "123/1 28/1 1859/100";
	}
	image->writeMetadata();
}
} // namespace

TEST_CASE("QuiverGpsIndex clusters nearby points and separates far ones", "[gps_index]")
{
	std::vector<QuiverGpsIndex::Point> points(3);
	points[0].lat = 48.45;
	points[0].lon = -123.47;
	points[1].lat = 48.46;
	points[1].lon = -123.46;
	points[2].lat = 40.71;
	points[2].lon = -74.00;

	const auto z2 = QuiverGpsIndex::ClusterPoints(points, 2.0, 48.0);
	REQUIRE(z2.size() == 2);
	for (const auto& cluster : z2)
	{
		if (cluster.count == 2)
		{
			REQUIRE(cluster.points.size() == 2);
			REQUIRE(cluster.minLat <= 48.45);
			REQUIRE(cluster.maxLat >= 48.46);
		}
		else
		{
			REQUIRE(cluster.count == 1);
		}
	}

	// At a high zoom the two nearby points are clearly separate.
	const auto z14 = QuiverGpsIndex::ClusterPoints(points, 14.0, 48.0);
	REQUIRE(z14.size() == 3);
	for (const auto& cluster : z14)
		REQUIRE(cluster.count == 1);
}

TEST_CASE("QuiverGpsIndex handles empty and single point sets", "[gps_index]")
{
	REQUIRE(QuiverGpsIndex::ClusterPoints({}, 5.0, 48.0).empty());

	std::vector<QuiverGpsIndex::Point> one(1);
	one[0].lat = -33.86;
	one[0].lon = 151.21;
	const auto clusters = QuiverGpsIndex::ClusterPoints(one, 5.0, 48.0);
	REQUIRE(clusters.size() == 1);
	REQUIRE(clusters[0].count == 1);
	REQUIRE(clusters[0].lat == -33.86);
	REQUIRE(clusters[0].lon == 151.21);
}

TEST_CASE("QuiverGpsIndex Rebuild caches date metadata and keeps only GPS points",
	"[gps_index][filter]")
{
	TempJpeg located = CopySample("sample_4k.jpg");
	TempJpeg unlocated = CopySample("sample_4k.jpg");
	WriteExif(located, true);
	WriteExif(unlocated, false);

	const std::string locatedUri = located.URI();
	const std::string unlocatedUri = unlocated.URI();
	REQUIRE_FALSE(locatedUri.empty());
	REQUIRE_FALSE(unlocatedUri.empty());

	std::list<std::string> files = {located.path, unlocated.path};
	ImageListPtr list(new ImageList(false));
	list->SetImageList(&files);
	REQUIRE(list->GetSize() == 2);

	std::size_t lastDone = 0;
	std::size_t lastTotal = 0;
	std::size_t callbacks = 0;
	QuiverGpsIndex index;
	index.Rebuild(list, [&](std::size_t done, std::size_t total) {
		lastDone = done;
		lastTotal = total;
		callbacks++;
	});

	REQUIRE(lastTotal == 2);
	REQUIRE(lastDone == 2);
	REQUIRE(callbacks >= 2);

	REQUIRE(index.GetPointCount() == 1);
	const auto points = index.GetPoints();
	REQUIRE(points.size() == 1);
	REQUIRE(points.front().uri == locatedUri);

	for (unsigned int i = 0; i < list->GetSize(); i++)
	{
		QuiverFile file = list->Get(i);
		REQUIRE(file.HasCachedDateTaken());
		if (std::string(file.GetURI()) == locatedUri)
			REQUIRE(file.HasCachedGPS());
		else
			REQUIRE_FALSE(file.HasCachedGPS());
	}
}

TEST_CASE("QuiverFile combined metadata pass fills date and GPS together",
	"[gps_index][file]")
{
	TempJpeg located = CopySample("sample_4k.jpg");
	TempJpeg unlocated = CopySample("sample_4k.jpg");
	WriteExif(located, true);
	WriteExif(unlocated, false);

	QuiverFile locatedFile(located.URI().c_str());
	REQUIRE_FALSE(locatedFile.IsMetadataResolvedForIndex());
	REQUIRE(locatedFile.ResolveMetadataForIndex());
	REQUIRE(locatedFile.IsMetadataResolvedForIndex());
	REQUIRE(locatedFile.HasCachedDateTaken());
	REQUIRE(locatedFile.HasCachedGPS());

	QuiverFile unlocatedFile(unlocated.URI().c_str());
	REQUIRE_FALSE(unlocatedFile.IsMetadataResolvedForIndex());
	REQUIRE_FALSE(unlocatedFile.ResolveMetadataForIndex());
	REQUIRE(unlocatedFile.IsMetadataResolvedForIndex());
	REQUIRE(unlocatedFile.HasCachedDateTaken());
	REQUIRE_FALSE(unlocatedFile.HasCachedGPS());
}

TEST_CASE("QuiverGpsIndex skips progress when the date pass already cached metadata",
	"[gps_index][filter]")
{
	TempJpeg located = CopySample("sample_4k.jpg");
	TempJpeg unlocated = CopySample("sample_4k.jpg");
	WriteExif(located, true);
	WriteExif(unlocated, false);

	std::list<std::string> files = {located.path, unlocated.path};
	ImageListPtr list(new ImageList(false));
	list->SetImageList(&files);
	REQUIRE(list->GetSize() == 2);

	for (unsigned int i = 0; i < list->GetSize(); i++)
	{
		QuiverFile file = list->Get(i);
		file.ResolveMetadataForIndex();
		REQUIRE(file.IsMetadataResolvedForIndex());
	}

	std::size_t callbacks = 0;
	QuiverGpsIndex index;
	index.Rebuild(list, [&](std::size_t, std::size_t) { callbacks++; });

	REQUIRE(callbacks == 0);
	REQUIRE(index.GetPointCount() == 1);
}

TEST_CASE("QuiverGpsIndex cancelled rebuild leaves the existing index intact",
	"[gps_index][filter]")
{
	TempJpeg located = CopySample("sample_4k.jpg");
	TempJpeg unlocated = CopySample("sample_4k.jpg");
	WriteExif(located, true);
	WriteExif(unlocated, false);

	std::list<std::string> files = {located.path, unlocated.path};
	ImageListPtr list(new ImageList(false));
	list->SetImageList(&files);
	REQUIRE(list->GetSize() == 2);

	QuiverGpsIndex index;
	index.Rebuild(list);
	REQUIRE(index.GetPointCount() == 1);

	std::vector<QuiverFile> replacement;
	replacement.emplace_back(located.URI().c_str());
	index.Rebuild(replacement, {}, []() { return true; });

	REQUIRE(index.GetPointCount() == 1);
	const auto points = index.GetPoints();
	REQUIRE(points.size() == 1);
	REQUIRE(points.front().uri == located.URI());
}
