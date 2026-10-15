#ifndef QUIVER_METADATA_H
#define QUIVER_METADATA_H

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>
#include <ctime>
#include <exiv2/exiv2.hpp>
#include "QuiverFile.h"

namespace QuiverMetadata
{

struct MetadataPayload
{
	bool valid = false;
	std::string source_filename;
	std::string source_uri;
	bool source_is_video = false;

	// 1. Date and Time
	bool has_date = false;
	time_t date_epoch = 0;
	std::string date_str;       // "YYYY:MM:DD HH:MM:SS"
	std::string subsecond_str;  // e.g. "042"

	// 2. GPS Location
	bool has_gps = false;
	double gps_latitude = 0.0;
	double gps_longitude = 0.0;
	bool has_altitude = false;
	double gps_altitude = 0.0;

	// 3. Camera Make and Model
	bool has_camera = false;
	std::string camera_make;
	std::string camera_model;

	// 4. Description, Title, Comment
	bool has_description = false;
	std::string title;
	std::string description;
	std::string comment;

	// 5. Artist and Copyright
	bool has_artist_copyright = false;
	std::string artist;
	std::string copyright;

	// 6. Software / Encoder
	bool has_software = false;
	std::string software;

	// 7. Full raw EXIF (populated when source is an image)
	bool has_raw_exif = false;
	std::shared_ptr<Exiv2::ExifData> raw_exif;

	// 8. Container tags (populated when source is a video)
	std::map<std::string, std::string> video_tags;
};

struct PasteOptions
{
	bool copy_date = true;
	bool copy_gps = true;
	bool copy_camera = true;
	bool copy_description = true;
	bool copy_artist_copyright = true;
	bool copy_all_exif = false; // applicable only image->image
};

class MetadataClipboard
{
public:
	static MetadataClipboard& GetInstance();

	bool HasData() const { return m_payload.valid; }
	const MetadataPayload& GetPayload() const { return m_payload; }
	void SetPayload(const MetadataPayload& payload) { m_payload = payload; }
	void Clear() { m_payload = MetadataPayload(); }

private:
	MetadataClipboard() = default;
	MetadataPayload m_payload;
};

// Extracts metadata from either an image or a video file.
bool ExtractMetadata(QuiverFile file, MetadataPayload& outPayload);

// Writes metadata to an image file via Exiv2.
bool ApplyMetadataToImage(const std::string& filePath,
                         const MetadataPayload& payload,
                         const PasteOptions& options,
                         std::string* pError = nullptr);

// Writes metadata to a video file via FFmpeg lossless stream copy.
bool ApplyMetadataToVideo(const std::string& filePath,
                         const MetadataPayload& payload,
                         const PasteOptions& options,
                         std::function<void(double)> progress_cb = nullptr,
                         std::function<bool()> should_cancel = nullptr,
                         std::string* pError = nullptr);

// Dispatcher for any QuiverFile (image or video).
bool ApplyMetadata(QuiverFile file,
                   const MetadataPayload& payload,
                   const PasteOptions& options,
                   std::function<void(double)> progress_cb = nullptr,
                   std::function<bool()> should_cancel = nullptr,
                   std::string* pError = nullptr);

} // namespace QuiverMetadata

#endif // QUIVER_METADATA_H
