#include "VideoDateEditTask.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/dict.h>
}

#include <glib.h>
#include <glib/gstdio.h>

#include <cstdio>
#include <string>

#include <sys/stat.h>
#include <utime.h>

static void avformat_init_once(void)
{
	static bool s_bInit = false;
	if (!s_bInit) {
		av_log_set_level(AV_LOG_ERROR);
		s_bInit = true;
	}
}

VideoDateEditTask::VideoDateEditTask(QuiverFile f, time_t new_epoch, bool update_mtime, time_t new_mtime)
	: m_QuiverFile(f), m_tNewEpoch(new_epoch), m_bUpdateMtime(update_mtime),
	  m_tNewMtime(new_mtime ? new_mtime : new_epoch), m_dPercent(0)
{
}

std::string VideoDateEditTask::GetDescription() const
{
	return std::string("Setting video date for ") + m_QuiverFile.GetFileName();
}

std::string VideoDateEditTask::GetIterationTypeName(bool /*shortname*/, bool /*plural*/) const
{
	return "videos";
}

int VideoDateEditTask::GetTotalIterations() const
{
	return 1;
}

int VideoDateEditTask::GetCurrentIteration() const
{
	return (m_dPercent >= 1.0) ? 1 : 0;
}

double VideoDateEditTask::GetProgress() const
{
	return m_dPercent;
}

bool VideoDateEditTask::SetVideoDate(const std::string& strPath, time_t new_epoch,
                                     bool update_mtime, time_t new_mtime,
                                     std::function<void(double)> progress_cb,
                                     std::function<bool()> should_cancel,
                                     std::string* pErrorMsg)
{
	if (0 == new_mtime)
	{
		new_mtime = new_epoch;
	}

	GDateTime *dt = g_date_time_new_from_unix_local(new_epoch);
	if (NULL == dt)
	{
		if (pErrorMsg) *pErrorMsg = "Failed to create local date time";
		return false;
	}
	char *szIso = g_date_time_format(dt, "%Y-%m-%dT%H:%M:%S%z");
	g_date_time_unref(dt);

	if (NULL == szIso) {
		if (pErrorMsg) *pErrorMsg = "Failed to format date";
		return false;
	}
	std::string strIso = szIso;
	g_free(szIso);

	// insert the colon in the UTC offset if needed (%z gives +0000)
	if (5 <= strIso.size())
		strIso.insert(strIso.size() - 2, ":");

	struct stat origStat = {};
	bool haveOrigStat = (0 == stat(strPath.c_str(), &origStat));

	avformat_init_once();

	std::string strTmp = strPath + ".quiversetdate.tmp";
	g_remove(strTmp.c_str());

	AVFormatContext *in = NULL;
	int ret = avformat_open_input(&in, strPath.c_str(), NULL, NULL);
	if (ret < 0) {
		if (pErrorMsg) *pErrorMsg = "Failed to open video";
		return false;
	}
	ret = avformat_find_stream_info(in, NULL);
	if (ret < 0) {
		avformat_close_input(&in);
		if (pErrorMsg) *pErrorMsg = "Failed to read video info";
		return false;
	}

	const AVOutputFormat *ofmt = av_guess_format(NULL, strPath.c_str(), NULL);
	if (NULL == ofmt && NULL != in->iformat && NULL != in->iformat->name)
	{
		ofmt = av_guess_format(in->iformat->name, NULL, NULL);
	}

	AVFormatContext *out = NULL;
	ret = avformat_alloc_output_context2(&out, ofmt, NULL, strTmp.c_str());
	if (ret < 0 || NULL == out) {
		avformat_close_input(&in);
		if (pErrorMsg) *pErrorMsg = "Failed to create output context";
		return false;
	}

	for (unsigned i = 0; i < in->nb_streams; i++)
	{
		AVStream *st = avformat_new_stream(out, NULL);
		if (NULL == st) {
			avformat_close_input(&in);
			avformat_free_context(out);
			g_remove(strTmp.c_str());
			if (pErrorMsg) *pErrorMsg = "Failed to copy streams";
			return false;
		}
		ret = avcodec_parameters_copy(st->codecpar, in->streams[i]->codecpar);
		if (ret < 0) {
			avformat_close_input(&in);
			avformat_free_context(out);
			g_remove(strTmp.c_str());
			if (pErrorMsg) *pErrorMsg = "Failed to copy stream parameters";
			return false;
		}
		st->codecpar->codec_tag = 0;
		st->time_base = in->streams[i]->time_base;
		av_dict_copy(&st->metadata, in->streams[i]->metadata, 0);
		av_dict_set(&st->metadata, "creation_time", strIso.c_str(), 0);
	}

	// carry over all metadata, then override container date tags
	av_dict_copy(&out->metadata, in->metadata, 0);
	av_dict_set(&out->metadata, "creation_time", strIso.c_str(), 0);
	av_dict_set(&out->metadata, "com.apple.quicktime.creationdate", strIso.c_str(), 0);
	av_dict_set(&out->metadata, "creation_date", strIso.c_str(), 0);
	av_dict_set(&out->metadata, "date", strIso.c_str(), 0);

	ret = avio_open(&out->pb, strTmp.c_str(), AVIO_FLAG_WRITE);
	if (ret < 0) {
		avformat_close_input(&in);
		avformat_free_context(out);
		g_remove(strTmp.c_str());
		if (pErrorMsg) *pErrorMsg = "Failed to create temp file";
		return false;
	}

	ret = avformat_write_header(out, NULL);
	if (ret < 0) {
		avformat_close_input(&in);
		avio_closep(&out->pb);
		avformat_free_context(out);
		g_remove(strTmp.c_str());
		if (pErrorMsg) *pErrorMsg = "Failed to write header";
		return false;
	}

	int64_t total_bytes = in->pb ? avio_size(in->pb) : 0;
	int64_t written = 0;
	bool bOk = true;

	AVPacket *pkt = av_packet_alloc();
	if (NULL == pkt) {
		avformat_close_input(&in);
		avio_closep(&out->pb);
		avformat_free_context(out);
		g_remove(strTmp.c_str());
		if (pErrorMsg) *pErrorMsg = "Failed to allocate packet";
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
			AVStream *in_stream = in->streams[stream_idx];
			AVStream *out_stream = out->streams[stream_idx];
			av_packet_rescale_ts(pkt, in_stream->time_base, out_stream->time_base);
			pkt->pos = -1;

			written += pkt->size;
			ret = av_interleaved_write_frame(out, pkt);
			if (ret < 0) {
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

	if (!bOk) {
		g_remove(strTmp.c_str());
		if (pErrorMsg) *pErrorMsg = "Failed writing remuxed file";
		return false;
	}

	// atomic replace on success
	if (g_rename(strTmp.c_str(), strPath.c_str()) < 0) {
		g_remove(strTmp.c_str());
		if (pErrorMsg) *pErrorMsg = "Failed to replace original";
		return false;
	}

	if (haveOrigStat)
	{
		chmod(strPath.c_str(), origStat.st_mode & 07777);
		struct utimbuf tb = {};
		if (update_mtime)
		{
			tb.actime = new_mtime;
			tb.modtime = new_mtime;
		}
		else
		{
			tb.actime = origStat.st_atime;
			tb.modtime = origStat.st_mtime;
		}
		utime(strPath.c_str(), &tb);
	}

	if (progress_cb)
	{
		progress_cb(1.0);
	}

	return true;
}

void VideoDateEditTask::Run()
{
	std::string strErr;
	auto progress_cb = [this](double p) {
		m_dPercent = p;
		EmitTaskProgressUpdatedEvent();
	};
	auto cancel_cb = [this]() {
		return ShouldCancel() || ShouldPause();
	};

	bool ok = SetVideoDate(m_QuiverFile.GetFilePath(), m_tNewEpoch,
	                       m_bUpdateMtime, m_tNewMtime,
	                       progress_cb, cancel_cb, &strErr);
	if (!ok)
	{
		SetMessage(MSG_TYPE_ERROR, strErr.empty() ? "Failed to set video date" : strErr.c_str());
		return;
	}

	// NOTE: no QuiverFile::Reload() here -- this runs on a TaskManager
	// worker thread and Reload frees/rebuilds shared state the GUI thread
	// reads (m_szURI). The caller's poll refreshes on the main thread.
	m_dPercent = 1.0;
}
