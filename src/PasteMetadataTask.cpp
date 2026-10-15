#include "PasteMetadataTask.h"
#include <glib.h>

PasteMetadataTask::PasteMetadataTask(const std::vector<QuiverFile>& files,
                                     const QuiverMetadata::MetadataPayload& payload,
                                     const QuiverMetadata::PasteOptions& options,
                                     NotifyCallback notifyCb)
	: m_files(files), m_payload(payload), m_options(options), m_notifyCb(notifyCb),
	  m_currentIdx(0), m_fileSavePercent(0.0)
{
}

PasteMetadataTask::~PasteMetadataTask()
{
}

std::string PasteMetadataTask::GetDescription() const
{
	return "Pasting metadata...";
}

std::string PasteMetadataTask::GetIterationTypeName(bool /*shortname*/, bool plural) const
{
	return plural ? "files" : "file";
}

int PasteMetadataTask::GetTotalIterations() const
{
	return (int)m_files.size();
}

int PasteMetadataTask::GetCurrentIteration() const
{
	return (int)m_currentIdx;
}

double PasteMetadataTask::GetProgress() const
{
	if (m_files.empty()) return 1.0;
	return ((double)m_currentIdx + m_fileSavePercent) / (double)m_files.size();
}

void PasteMetadataTask::AddFile(const QuiverFile& f)
{
	m_files.push_back(f);
}

void PasteMetadataTask::Run()
{
	while (m_currentIdx < m_files.size())
	{
		if (ShouldCancel()) break;
		while (ShouldPause() && !ShouldCancel())
		{
			g_usleep(100000);
		}
		if (ShouldCancel()) break;

		QuiverFile f = m_files[m_currentIdx];
		auto progress_cb = [this](double p) {
			m_fileSavePercent = p;
			EmitTaskProgressUpdatedEvent();
		};
		auto cancel_cb = [this]() {
			return ShouldCancel() || ShouldPause();
		};

		std::string err;
		bool ok = QuiverMetadata::ApplyMetadata(f, m_payload, m_options, progress_cb, cancel_cb, &err);
		if (!ok)
		{
			SetMessage(MSG_TYPE_ERROR, err.empty() ? "Failed to apply metadata" : err.c_str());
		}

		struct DispatchData
		{
			QuiverFile file;
			bool success;
			NotifyCallback cb;
		};
		DispatchData* dd = new DispatchData{f, ok, m_notifyCb};
		g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, +[](gpointer user_data) -> gboolean {
			DispatchData* data = static_cast<DispatchData*>(user_data);
			if (data->success)
			{
				data->file.Reload();
			}
			if (data->cb)
			{
				data->cb(data->file, data->success);
			}
			delete data;
			return G_SOURCE_REMOVE;
		}, dd, NULL);

		m_fileSavePercent = 0.0;
		m_currentIdx++;
		EmitTaskProgressUpdatedEvent();
	}
}
