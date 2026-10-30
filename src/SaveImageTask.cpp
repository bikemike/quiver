#include "SaveImageTask.h"

#include "ImageSaveManager.h"

#include <string>


#include <glib.h>

SaveImageTask::SaveImageTask(QuiverFile file, NotifyCallback notifyCb /* = nullptr */)
	: m_dFileSavePercent(0.0), m_QuiverFile(file), m_notifyCb(notifyCb)
{
	m_QuiverFile.SetSaving(true);
}

SaveImageTask::~SaveImageTask()
{
}

std::string SaveImageTask::GetDescription() const
{
	return "Save Image";
}

// quantity type may be kb, items, images, files, 
// or anything else the task iterates over
std::string SaveImageTask::GetIterationTypeName(bool shortname, bool plural ) const
{
	if (shortname)
	{
		if (plural)
			return "imgs";
		return "img";
	}

	if (plural)
		return "images";
	return "image";
}

// get total and current iteration
int SaveImageTask::GetTotalIterations() const
{
	return 1;
}

int SaveImageTask::GetCurrentIteration() const
{
	return 1;
}


double SaveImageTask::GetProgress() const
{
	if (IsFinished())
	{
		return 1.;
	}
	return m_dFileSavePercent;
}

void SaveImageTask::Run()
{
	bool bSuccess = false;
	if (m_QuiverFile.IsWriteable())
	{
		std::string strMimeType = m_QuiverFile.GetMimeType();
		
		ImageSaveManagerPtr saverPtr = ImageSaveManager::GetInstance();
		
		if (saverPtr->IsFormatSupported(strMimeType))
		{
			bSuccess = saverPtr->SaveImage(m_QuiverFile);
		}
	}

	struct DispatchData
	{
		QuiverFile file;
		bool success;
		SaveImageTask::NotifyCallback cb;
	};

	DispatchData* dd = new DispatchData{m_QuiverFile, bSuccess, m_notifyCb};
	g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, +[](gpointer user_data) -> gboolean {
		DispatchData* data = static_cast<DispatchData*>(user_data);
		data->file.SetSaving(false);
		if (data->cb)
		{
			data->cb(data->file, data->success);
		}
		delete data;
		return G_SOURCE_REMOVE;
	}, dd, NULL);

	m_dFileSavePercent = 1.0;
	EmitTaskProgressUpdatedEvent();
}


