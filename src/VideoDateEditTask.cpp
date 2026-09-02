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

static void avformat_init_once(void)
{
	static bool s_bInit = false;
	if (!s_bInit) {
		av_log_set_level(AV_LOG_ERROR);
		s_bInit = true;
	}
}

VideoDateEditTask::VideoDateEditTask(QuiverFile f, time_t new_epoch)
	: m_QuiverFile(f), m_tNewEpoch(new_epoch), m_dPercent(0)
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

void VideoDateEditTask::Run()
{
	std::string strPath = m_QuiverFile.GetFilePath();

	GDateTime *dt = g_date_time_new_from_unix_local(m_tNewEpoch);
	char *szIso = g_date_time_format(dt, "%Y-%m-%dT%H:%M:%S%z");
	g_date_time_unref(dt);

	if (NULL == szIso) {
		SetMessage(MSG_TYPE_ERROR, "Failed to format date");
		return;
	}
	std::string strIso = szIso;
	g_free(szIso);

	// insert the colon in the UTC offset if needed (%z gives +0000)
	if (5 <= strIso.size())
		strIso.insert(strIso.size() - 2, ":");

	avformat_init_once();

	std::string strTmp = strPath + ".quiversetdate.tmp";

	AVFormatContext *in = NULL;
	int ret = avformat_open_input(&in, strPath.c_str(), NULL, NULL);
	if (ret < 0) {
		SetMessage(MSG_TYPE_ERROR, "Failed to open video");
		return;
	}
	ret = avformat_find_stream_info(in, NULL);
	if (ret < 0) {
		avformat_close_input(&in);
		SetMessage(MSG_TYPE_ERROR, "Failed to read video info");
		return;
	}

	AVFormatContext *out = NULL;
	ret = avformat_alloc_output_context2(&out, NULL, NULL, strTmp.c_str());
	if (ret < 0) {
		avformat_close_input(&in);
		SetMessage(MSG_TYPE_ERROR, "Failed to create output context");
		return;
	}

	for (unsigned i = 0; i < in->nb_streams; i++)
	{
		AVStream *st = avformat_new_stream(out, NULL);
		if (NULL == st) {
			avformat_close_input(&in);
			avformat_free_context(out);
			SetMessage(MSG_TYPE_ERROR, "Failed to copy streams");
			return;
		}
		ret = avcodec_parameters_copy(st->codecpar, in->streams[i]->codecpar);
		st->codecpar->codec_tag = 0;
		if (ret < 0) {
			avformat_close_input(&in);
			avformat_free_context(out);
			SetMessage(MSG_TYPE_ERROR, "Failed to copy stream parameters");
			return;
		}
		st->time_base = in->streams[i]->time_base;
	}

	// carry over all metadata, then override creation_time (ISO 8601)
	av_dict_copy(&out->metadata, in->metadata, 0);
	av_dict_set(&out->metadata, "creation_time", strIso.c_str(), 0);

	ret = avio_open(&out->pb, strTmp.c_str(), AVIO_FLAG_WRITE);
	if (ret < 0) {
		avformat_close_input(&in);
		avformat_free_context(out);
		SetMessage(MSG_TYPE_ERROR, "Failed to create temp file");
		return;
	}

	ret = avformat_write_header(out, NULL);
	if (ret < 0) {
		avformat_close_input(&in);
		avio_closep(&out->pb);
		avformat_free_context(out);
		g_remove(strTmp.c_str());
		SetMessage(MSG_TYPE_ERROR, "Failed to write header");
		return;
	}

	int64_t total_bytes = avio_size(in->pb);
	int64_t written = 0;

	AVPacket pkt;
	while (av_read_frame(in, &pkt) >= 0)
	{
		pkt.stream_index = out->streams[pkt.stream_index]->index;
		written += pkt.size;
		ret = av_interleaved_write_frame(out, &pkt);
		if (ret < 0) {
			break;
		}
		m_dPercent = 0.9 * ((double)written / (double)(total_bytes > 0 ? total_bytes : written + 1));
	}

	bool bOk = true;
	if (av_write_trailer(out) < 0)
		bOk = false;

	avformat_close_input(&in);
	avio_closep(&out->pb);
	avformat_free_context(out);

	if (!bOk) {
		g_remove(strTmp.c_str());
		SetMessage(MSG_TYPE_ERROR, "Failed writing remuxed file");
		return;
	}

	m_dPercent = 0.95;

	// atomic replace on success
	if (g_rename(strTmp.c_str(), strPath.c_str()) < 0) {
		g_remove(strTmp.c_str());
		SetMessage(MSG_TYPE_ERROR, "Failed to replace original");
		return;
	}

	// NOTE: no QuiverFile::Reload() here -- this runs on a TaskManager
	// worker thread and Reload frees/rebuilds shared state the GUI thread
	// reads (m_szURI). The caller's poll refreshes on the main thread.
	m_dPercent = 1.0;
}
