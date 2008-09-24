#include "SaveImageTask.h"

#include "ImageSaveManager.h"

#include <string>


SaveImageTask::SaveImageTask(QuiverFile file)
	: m_QuiverFile(file)
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


	if (m_QuiverFile.Modified() && m_QuiverFile.IsWriteable())
	{
		std::string strMimeType = m_QuiverFile.GetMimeType();
		
		ImageSaveManagerPtr saverPtr = ImageSaveManager::GetInstance();
		
		  
		if (saverPtr->IsFormatSupported(strMimeType))
		{
			saverPtr->SaveImage(m_QuiverFile);
		}
	}

	EmitTaskProgressUpdatedEvent();

	/*
	if (ShouldPause())
	{
		break;
	}

	if (ShouldCancel())
	{
		break;
	}
	*/
}


