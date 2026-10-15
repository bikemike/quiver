#include "QuiverMetadata.h"
#include "QuiverGps.h"
#include "QuiverAvio.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/dict.h>
#include <libavutil/opt.h>
}

#include <glib.h>
#include <glib/gstdio.h>
#include <sys/stat.h>
#include <utime.h>
#include <unistd.h>
#include <cmath>
#include <cstring>
#include <iostream>

namespace QuiverMetadata
{

static void avformat_init_once(void)
{
	static bool s_bInit = false;
	if (!s_bInit) {
		av_log_set_level(AV_LOG_ERROR);
		s_bInit = true;
	}
}

MetadataClipboard& MetadataClipboard::GetInstance()
{
	static MetadataClipboard instance;
	return instance;
}

bool ExtractMetadata(QuiverFile file, MetadataPayload& outPayload)
{
	outPayload = MetadataPayload();
	outPayload.source_filename = file.GetFileName();
	if (NULL != file.GetURI())
		outPayload.source_uri = file.GetURI();
	outPayload.source_is_video = file.IsVideo();

	if (file.IsVideo())
	{
		// 1. Date
		if (file.HasDateMetadata())
		{
			time_t t = file.GetTimeT(true);
			if (t > 0)
			{
				outPayload.has_date = true;
				outPayload.date_epoch = t;
				struct tm tmDate = {};
				localtime_r(&t, &tmDate);
				char szBuf[64];
				snprintf(szBuf, sizeof(szBuf), "%04d:%02d:%02d %02d:%02d:%02d",
					tmDate.tm_year + 1900, tmDate.tm_mon + 1, tmDate.tm_mday,
					tmDate.tm_hour, tmDate.tm_min, tmDate.tm_sec);
				outPayload.date_str = szBuf;
			}
		}

		// 2. Video container tags via FFmpeg
		QuiverAvio::Container container;
		if (container.Open(file.GetURI()))
		{
			AVFormatContext* pFmt = container.fmt();
			if (NULL != pFmt)
			{
				auto get_tag = [&](const char* key) -> const char*
				{
					AVDictionaryEntry* pTag = av_dict_get(pFmt->metadata, key, NULL, 0);
					if (NULL == pTag)
					{
						for (unsigned int i = 0; i < pFmt->nb_streams; i++)
						{
							pTag = av_dict_get(pFmt->streams[i]->metadata, key, NULL, 0);
							if (NULL != pTag) break;
						}
					}
					return (NULL != pTag) ? pTag->value : NULL;
				};

				// GPS
				static const char* const kLocationTags[] = {
					"location",
					"location-eng",
					"com.apple.quicktime.location.ISO6709",
					"com.apple.quicktime.location.iso6709",
					"location.ISO6709",
				};
				const char* pLoc = NULL;
				for (const char* locKey : kLocationTags)
				{
					pLoc = get_tag(locKey);
					if (NULL != pLoc) break;
				}
				if (NULL != pLoc)
				{
					double lat = 0.0, lon = 0.0, alt = 0.0;
					bool hasAlt = false;
					if (QuiverGps::ParseIso6709(pLoc, lat, lon, hasAlt, alt))
					{
						outPayload.has_gps = true;
						outPayload.gps_latitude = lat;
						outPayload.gps_longitude = lon;
						outPayload.has_altitude = hasAlt;
						outPayload.gps_altitude = alt;
					}
				}

				// Camera
				const char* pMake = get_tag("make");
				if (NULL == pMake) pMake = get_tag("com.android.manufacturer");
				const char* pModel = get_tag("model");
				if (NULL == pModel) pModel = get_tag("com.android.model");
				if (NULL != pMake || NULL != pModel)
				{
					outPayload.has_camera = true;
					if (NULL != pMake) outPayload.camera_make = pMake;
					if (NULL != pModel) outPayload.camera_model = pModel;
				}

				// Description / Comment / Title
				const char* pTitle = get_tag("title");
				const char* pComment = get_tag("comment");
				if (NULL == pComment) pComment = get_tag("description");
				if (NULL != pTitle || NULL != pComment)
				{
					outPayload.has_description = true;
					if (NULL != pTitle) outPayload.title = pTitle;
					if (NULL != pComment)
					{
						outPayload.comment = pComment;
						outPayload.description = pComment;
					}
				}

				// Artist / Author & Copyright
				const char* pArtist = get_tag("artist");
				if (NULL == pArtist) pArtist = get_tag("author");
				const char* pCopyright = get_tag("copyright");
				if (NULL != pArtist || NULL != pCopyright)
				{
					outPayload.has_artist_copyright = true;
					if (NULL != pArtist) outPayload.artist = pArtist;
					if (NULL != pCopyright) outPayload.copyright = pCopyright;
				}

				// Software
				const char* pSoft = get_tag("encoder");
				if (NULL == pSoft) pSoft = get_tag("software");
				if (NULL != pSoft)
				{
					outPayload.has_software = true;
					outPayload.software = pSoft;
				}

				// Collect container tags
				const AVDictionaryEntry* pEntry = NULL;
				while (NULL != (pEntry = av_dict_iterate(pFmt->metadata, pEntry)))
				{
					outPayload.video_tags[pEntry->key] = pEntry->value;
				}
			}
		}

		outPayload.valid = true;
		return true;
	}
	else
	{
		// Image file via Exiv2
		std::shared_ptr<Exiv2::ExifData> pExif = file.GetExifData();
		if (NULL == pExif.get())
		{
			try
			{
				auto img = Exiv2::ImageFactory::open(file.GetFilePath());
				if (img.get())
				{
					img->readMetadata();
					pExif = std::make_shared<Exiv2::ExifData>(img->exifData());
				}
			}
			catch (...) {}
		}

		// 1. Date
		if (file.HasDateMetadata())
		{
			time_t t = file.GetTimeT(true);
			if (t > 0)
			{
				outPayload.has_date = true;
				outPayload.date_epoch = t;
				struct tm tmDate = {};
				localtime_r(&t, &tmDate);
				char szBuf[64];
				snprintf(szBuf, sizeof(szBuf), "%04d:%02d:%02d %02d:%02d:%02d",
					tmDate.tm_year + 1900, tmDate.tm_mon + 1, tmDate.tm_mday,
					tmDate.tm_hour, tmDate.tm_min, tmDate.tm_sec);
				outPayload.date_str = szBuf;
			}
		}

		if (NULL != pExif.get())
		{
			// Raw EXIF copy
			outPayload.has_raw_exif = true;
			outPayload.raw_exif = std::make_shared<Exiv2::ExifData>(*pExif);

			auto get_str = [&](const char* key) -> std::string
			{
				auto it = pExif->findKey(Exiv2::ExifKey(key));
				if (it != pExif->end()) return it->toString();
				return "";
			};

			// Subsecond date
			std::string subSec = get_str("Exif.Photo.SubSecTimeOriginal");
			if (subSec.empty()) subSec = get_str("Exif.Photo.SubSecTime");
			if (!subSec.empty()) outPayload.subsecond_str = subSec;

			// Check date string from DateTimeOriginal if not yet set
			if (!outPayload.has_date)
			{
				std::string dtOrig = get_str("Exif.Photo.DateTimeOriginal");
				if (dtOrig.empty()) dtOrig = get_str("Exif.Image.DateTime");
				if (!dtOrig.empty())
				{
					outPayload.has_date = true;
					outPayload.date_str = dtOrig;
					int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
					if (6 == sscanf(dtOrig.c_str(), "%04d:%02d:%02d %02d:%02d:%02d",
						&y, &mo, &d, &h, &mi, &s))
					{
						struct tm tmDate = {};
						tmDate.tm_year = y - 1900;
						tmDate.tm_mon = mo - 1;
						tmDate.tm_mday = d;
						tmDate.tm_hour = h;
						tmDate.tm_min = mi;
						tmDate.tm_sec = s;
						tmDate.tm_isdst = -1;
						outPayload.date_epoch = mktime(&tmDate);
					}
				}
			}

			// 2. GPS
			double lat = 0.0, lon = 0.0, alt = 0.0;
			bool hasAlt = false;
			if (QuiverGps::GetExifGps(*pExif, lat, lon, hasAlt, alt))
			{
				outPayload.has_gps = true;
				outPayload.gps_latitude = lat;
				outPayload.gps_longitude = lon;
				outPayload.has_altitude = hasAlt;
				outPayload.gps_altitude = alt;
			}

			// 3. Camera
			std::string make = get_str("Exif.Image.Make");
			std::string model = get_str("Exif.Image.Model");
			if (!make.empty() || !model.empty())
			{
				outPayload.has_camera = true;
				outPayload.camera_make = make;
				outPayload.camera_model = model;
			}

			// 4. Description / Comment / Title
			std::string title = get_str("Exif.Image.DocumentName");
			std::string desc = get_str("Exif.Image.ImageDescription");
			std::string comment = get_str("Exif.Photo.UserComment");
			if (g_str_has_prefix(comment.c_str(), "charset=Ascii "))
				comment = comment.substr(strlen("charset=Ascii "));
			if (!title.empty() || !desc.empty() || !comment.empty())
			{
				outPayload.has_description = true;
				outPayload.title = title;
				outPayload.description = desc;
				outPayload.comment = comment;
			}

			// 5. Artist & Copyright
			std::string artist = get_str("Exif.Image.Artist");
			std::string copyright = get_str("Exif.Image.Copyright");
			if (!artist.empty() || !copyright.empty())
			{
				outPayload.has_artist_copyright = true;
				outPayload.artist = artist;
				outPayload.copyright = copyright;
			}

			// 6. Software
			std::string sw = get_str("Exif.Image.Software");
			if (!sw.empty())
			{
				outPayload.has_software = true;
				outPayload.software = sw;
			}
		}

		outPayload.valid = true;
		return true;
	}
}

bool ApplyMetadataToImage(const std::string& filePath,
                         const MetadataPayload& payload,
                         const PasteOptions& options,
                         std::string* pError)
{
	try
	{
		auto image = Exiv2::ImageFactory::open(filePath);
		if (!image.get())
		{
			if (pError) *pError = "Cannot open image file: " + filePath;
			return false;
		}

		image->readMetadata();
		Exiv2::ExifData& exif = image->exifData();

		if (options.copy_all_exif && payload.has_raw_exif && payload.raw_exif)
		{
			for (auto it = payload.raw_exif->begin(); it != payload.raw_exif->end(); ++it)
			{
				const std::string key = it->key();
				// Skip image dimensions, compression, and orientation
				if (key == "Exif.Image.ImageWidth" || key == "Exif.Image.ImageLength" ||
					key == "Exif.Photo.PixelXDimension" || key == "Exif.Photo.PixelYDimension" ||
					key == "Exif.Image.Compression" || key == "Exif.Image.JPEGInterchangeFormat" ||
					key == "Exif.Image.JPEGInterchangeFormatLength" ||
					key == "Exif.Image.Orientation")
				{
					continue;
				}
				exif[key] = *it;
			}
		}

		if (options.copy_date && payload.has_date && !payload.date_str.empty())
		{
			exif["Exif.Image.DateTime"] = payload.date_str;
			exif["Exif.Photo.DateTimeOriginal"] = payload.date_str;
			exif["Exif.Photo.DateTimeDigitized"] = payload.date_str;
			if (!payload.subsecond_str.empty())
			{
				exif["Exif.Photo.SubSecTime"] = payload.subsecond_str;
				exif["Exif.Photo.SubSecTimeOriginal"] = payload.subsecond_str;
				exif["Exif.Photo.SubSecTimeDigitized"] = payload.subsecond_str;
			}
		}

		if (options.copy_gps && payload.has_gps)
		{
			QuiverGps::SetExifGps(exif, payload.gps_latitude, payload.gps_longitude,
				payload.has_altitude, payload.gps_altitude);
		}

		if (options.copy_camera && payload.has_camera)
		{
			if (!payload.camera_make.empty()) exif["Exif.Image.Make"] = payload.camera_make;
			if (!payload.camera_model.empty()) exif["Exif.Image.Model"] = payload.camera_model;
		}

		if (options.copy_description && payload.has_description)
		{
			if (!payload.title.empty()) exif["Exif.Image.DocumentName"] = payload.title;
			if (!payload.description.empty()) exif["Exif.Image.ImageDescription"] = payload.description;
			if (!payload.comment.empty()) exif["Exif.Photo.UserComment"] = "charset=Ascii " + payload.comment;
		}

		if (options.copy_artist_copyright && payload.has_artist_copyright)
		{
			if (!payload.artist.empty()) exif["Exif.Image.Artist"] = payload.artist;
			if (!payload.copyright.empty()) exif["Exif.Image.Copyright"] = payload.copyright;
		}

		image->writeMetadata();

		if (options.copy_date && payload.has_date && payload.date_epoch > 0)
		{
			struct utimbuf tb = {};
			tb.actime = payload.date_epoch;
			tb.modtime = payload.date_epoch;
			utime(filePath.c_str(), &tb);
		}

		return true;
	}
	catch (const std::exception& e)
	{
		if (pError) *pError = std::string("Exiv2 error: ") + e.what();
		return false;
	}
	catch (...)
	{
		if (pError) *pError = "Unknown error writing metadata to image";
		return false;
	}
}

bool ApplyMetadataToVideo(const std::string& filePath,
                         const MetadataPayload& payload,
                         const PasteOptions& options,
                         std::function<void(double)> progress_cb,
                         std::function<bool()> should_cancel,
                         std::string* pError)
{
	struct stat origStat = {};
	bool haveOrigStat = (0 == stat(filePath.c_str(), &origStat));

	avformat_init_once();

	std::string strTmp = filePath + ".quivermeta.tmp";
	g_remove(strTmp.c_str());

	AVFormatContext* in = NULL;
	int ret = avformat_open_input(&in, filePath.c_str(), NULL, NULL);
	if (ret < 0)
	{
		if (pError) *pError = "Failed to open input video";
		return false;
	}

	ret = avformat_find_stream_info(in, NULL);
	if (ret < 0)
	{
		avformat_close_input(&in);
		if (pError) *pError = "Failed to read video stream info";
		return false;
	}

	const AVOutputFormat* ofmt = av_guess_format(NULL, filePath.c_str(), NULL);
	if (NULL == ofmt && NULL != in->iformat && NULL != in->iformat->name)
	{
		ofmt = av_guess_format(in->iformat->name, NULL, NULL);
	}

	AVFormatContext* out = NULL;
	ret = avformat_alloc_output_context2(&out, ofmt, NULL, strTmp.c_str());
	if (ret < 0 || NULL == out)
	{
		avformat_close_input(&in);
		if (pError) *pError = "Failed to allocate output format context";
		return false;
	}

	if (NULL != out->priv_data)
	{
		av_opt_set(out->priv_data, "movflags", "use_metadata_tags", AV_OPT_SEARCH_CHILDREN);
	}

	std::string strIsoDate;
	if (options.copy_date && payload.has_date && payload.date_epoch > 0)
	{
		GDateTime* dt = g_date_time_new_from_unix_local(payload.date_epoch);
		if (NULL != dt)
		{
			char* szIso = g_date_time_format(dt, "%Y-%m-%dT%H:%M:%S%z");
			g_date_time_unref(dt);
			if (NULL != szIso)
			{
				strIsoDate = szIso;
				g_free(szIso);
				if (5 <= strIsoDate.size())
					strIsoDate.insert(strIsoDate.size() - 2, ":");
			}
		}
	}

	for (unsigned int i = 0; i < in->nb_streams; i++)
	{
		AVStream* st = avformat_new_stream(out, NULL);
		if (NULL == st)
		{
			avformat_close_input(&in);
			avformat_free_context(out);
			g_remove(strTmp.c_str());
			if (pError) *pError = "Failed to create stream";
			return false;
		}
		ret = avcodec_parameters_copy(st->codecpar, in->streams[i]->codecpar);
		if (ret < 0)
		{
			avformat_close_input(&in);
			avformat_free_context(out);
			g_remove(strTmp.c_str());
			if (pError) *pError = "Failed to copy stream parameters";
			return false;
		}
		st->codecpar->codec_tag = 0;
		st->time_base = in->streams[i]->time_base;
		av_dict_copy(&st->metadata, in->streams[i]->metadata, 0);

		if (!strIsoDate.empty())
		{
			av_dict_set(&st->metadata, "creation_time", strIsoDate.c_str(), 0);
		}
	}

	// Copy existing container metadata
	av_dict_copy(&out->metadata, in->metadata, 0);

	// 1. Date
	if (!strIsoDate.empty())
	{
		av_dict_set(&out->metadata, "creation_time", strIsoDate.c_str(), 0);
		av_dict_set(&out->metadata, "com.apple.quicktime.creationdate", strIsoDate.c_str(), 0);
		av_dict_set(&out->metadata, "creation_date", strIsoDate.c_str(), 0);
		av_dict_set(&out->metadata, "date", strIsoDate.c_str(), 0);
	}

	// 2. GPS
	if (options.copy_gps && payload.has_gps)
	{
		std::string isoLoc = QuiverGps::FormatIso6709FromCoordinates(
			payload.gps_latitude, payload.gps_longitude, payload.has_altitude, payload.gps_altitude);
		av_dict_set(&out->metadata, "com.apple.quicktime.location.ISO6709", isoLoc.c_str(), 0);
		av_dict_set(&out->metadata, "location.ISO6709", isoLoc.c_str(), 0);
		av_dict_set(&out->metadata, "location", isoLoc.c_str(), 0);
		av_dict_set(&out->metadata, "location-eng", isoLoc.c_str(), 0);
	}

	// 3. Camera
	if (options.copy_camera && payload.has_camera)
	{
		if (!payload.camera_make.empty())
		{
			av_dict_set(&out->metadata, "make", payload.camera_make.c_str(), 0);
			av_dict_set(&out->metadata, "com.android.manufacturer", payload.camera_make.c_str(), 0);
		}
		if (!payload.camera_model.empty())
		{
			av_dict_set(&out->metadata, "model", payload.camera_model.c_str(), 0);
			av_dict_set(&out->metadata, "com.android.model", payload.camera_model.c_str(), 0);
		}
	}

	// 4. Description / Comment / Title
	if (options.copy_description && payload.has_description)
	{
		if (!payload.title.empty())
			av_dict_set(&out->metadata, "title", payload.title.c_str(), 0);
		if (!payload.comment.empty())
		{
			av_dict_set(&out->metadata, "comment", payload.comment.c_str(), 0);
			av_dict_set(&out->metadata, "description", payload.comment.c_str(), 0);
		}
		else if (!payload.description.empty())
		{
			av_dict_set(&out->metadata, "comment", payload.description.c_str(), 0);
			av_dict_set(&out->metadata, "description", payload.description.c_str(), 0);
		}
	}

	// 5. Artist / Author & Copyright
	if (options.copy_artist_copyright && payload.has_artist_copyright)
	{
		if (!payload.artist.empty())
		{
			av_dict_set(&out->metadata, "artist", payload.artist.c_str(), 0);
			av_dict_set(&out->metadata, "author", payload.artist.c_str(), 0);
		}
		if (!payload.copyright.empty())
			av_dict_set(&out->metadata, "copyright", payload.copyright.c_str(), 0);
	}

	ret = avio_open(&out->pb, strTmp.c_str(), AVIO_FLAG_WRITE);
	if (ret < 0)
	{
		avformat_close_input(&in);
		avformat_free_context(out);
		g_remove(strTmp.c_str());
		if (pError) *pError = "Failed to open output temp file";
		return false;
	}

	ret = avformat_write_header(out, NULL);
	if (ret < 0)
	{
		avformat_close_input(&in);
		avio_closep(&out->pb);
		avformat_free_context(out);
		g_remove(strTmp.c_str());
		if (pError) *pError = "Failed to write output header";
		return false;
	}

	int64_t total_bytes = in->pb ? avio_size(in->pb) : 0;
	int64_t written = 0;
	bool bOk = true;

	AVPacket* pkt = av_packet_alloc();
	if (NULL == pkt)
	{
		avformat_close_input(&in);
		avio_closep(&out->pb);
		avformat_free_context(out);
		g_remove(strTmp.c_str());
		if (pError) *pError = "Failed to allocate packet";
		return false;
	}

	while (av_read_frame(in, pkt) >= 0)
	{
		if (should_cancel && should_cancel())
		{
			av_packet_unref(pkt);
			bOk = false;
			break;
		}

		unsigned int stream_idx = pkt->stream_index;
		if (stream_idx < out->nb_streams)
		{
			AVStream* in_stream = in->streams[stream_idx];
			AVStream* out_stream = out->streams[stream_idx];
			av_packet_rescale_ts(pkt, in_stream->time_base, out_stream->time_base);
			pkt->pos = -1;

			written += pkt->size;
			ret = av_interleaved_write_frame(out, pkt);
			if (ret < 0)
			{
				av_packet_unref(pkt);
				bOk = false;
				break;
			}
		}
		else
		{
			av_packet_unref(pkt);
		}

		if (progress_cb)
		{
			double p = 0.9 * ((double)written / (double)(total_bytes > 0 ? total_bytes : written + 1));
			progress_cb(p);
		}
	}
	av_packet_free(&pkt);

	if (bOk && av_write_trailer(out) < 0)
		bOk = false;

	avformat_close_input(&in);
	avio_closep(&out->pb);
	avformat_free_context(out);

	if (!bOk)
	{
		g_remove(strTmp.c_str());
		if (pError) *pError = "Failed writing remuxed video";
		return false;
	}

	if (g_rename(strTmp.c_str(), filePath.c_str()) < 0)
	{
		g_remove(strTmp.c_str());
		if (pError) *pError = "Failed to replace original file";
		return false;
	}

	if (haveOrigStat)
	{
		chmod(filePath.c_str(), origStat.st_mode & 07777);
		struct utimbuf tb = {};
		tb.actime = (options.copy_date && payload.has_date && payload.date_epoch > 0) ? payload.date_epoch : origStat.st_atime;
		tb.modtime = (options.copy_date && payload.has_date && payload.date_epoch > 0) ? payload.date_epoch : origStat.st_mtime;
		utime(filePath.c_str(), &tb);
	}

	if (progress_cb) progress_cb(1.0);
	return true;
}

bool ApplyMetadata(QuiverFile file,
                   const MetadataPayload& payload,
                   const PasteOptions& options,
                   std::function<void(double)> progress_cb,
                   std::function<bool()> should_cancel,
                   std::string* pError)
{
	if (file.IsVideo())
	{
		return ApplyMetadataToVideo(file.GetFilePath(), payload, options, progress_cb, should_cancel, pError);
	}
	else
	{
		bool ok = ApplyMetadataToImage(file.GetFilePath(), payload, options, pError);
		if (progress_cb) progress_cb(1.0);
		return ok;
	}
}

} // namespace QuiverMetadata
