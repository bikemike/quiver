#include "OrganizeTask.h"

#include "ImageSaveManager.h"
#include "MessageBox.h"
#include "ImageList.h"
#include "RenameTask.h"

#include <map>
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include <sstream>

#include <gio/gio.h>

class OrganizeTask::PrivateImpl
{
public:
	PrivateImpl(OrganizeTask* parent) : 
		m_pParent(parent), 
		m_iLastXFerRVal(-1),
		m_iLastVFSErrorRVal(-1)
	{
		m_pCancellable = g_cancellable_new();
	}

	~PrivateImpl()
	{
		g_object_unref(m_pCancellable);
	}

	OrganizeTask* m_pParent;
	int           m_iLastXFerRVal;
	int           m_iLastVFSErrorRVal;
	GCancellable* m_pCancellable;

};

OrganizeTask::OrganizeTask() :
	m_PrivateImplPtr(new PrivateImpl(this)),
	m_iCurrentFile(0),
	m_bIncludeSubfolders(false),
	m_bRenameFiles(true),
	m_iDayExtension(0)
{
	SetFolderTemplate("YYYY-MM-DD");
	SetFileTemplate("%Y-%m-%d %H.%M-##");
}
OrganizeTask::~OrganizeTask()
{
}

std::string OrganizeTask::GetDescription() const
{
	return "Organizing Images";
}

// quantity type may be kb, items, images, files, 
// or anything else the task iterates over
std::string OrganizeTask::GetIterationTypeName(bool shortname, bool plural ) const
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
int OrganizeTask::GetTotalIterations() const
{
	return m_vectQuiverFiles.size();
}

int OrganizeTask::GetCurrentIteration() const
{
	return m_iCurrentFile;
}

double OrganizeTask::GetProgress() const
{
	double progress = 0.;
	if (IsFinished())
	{
		progress = 1.;
	}
	else if (!m_vectQuiverFiles.empty())
	{
		progress =  m_iCurrentFile / (double)m_vectQuiverFiles.size();
	}
	return progress;
}

void OrganizeTask::AddFile(QuiverFile quiverFile)
{
	m_vectQuiverFiles.push_back(quiverFile);
}

void OrganizeTask::AddFiles(std::vector<QuiverFile> vectQuiverFiles)
{
	m_vectQuiverFiles.insert(m_vectQuiverFiles.end(), vectQuiverFiles.begin(), vectQuiverFiles.end());
}

void OrganizeTask::SetInputFolder(std::string strSrcURI)
{
	m_strSrcDirURI = strSrcURI;
}

void OrganizeTask::SetIncludeSubfolders(bool bIncludeSubfolders)
{
	m_bIncludeSubfolders = bIncludeSubfolders;
}


// the output directory must be specified
void OrganizeTask::SetOutputFolder(std::string strDestDirURI)
{
	m_strDestDirURI = strDestDirURI;
}

// if not set, the default format is: YYYY-MM-DD
// pulled from the exif data
void OrganizeTask::SetFolderTemplate(std::string strTemplate)
{
	m_strFolderTemplate = strTemplate;
}

void OrganizeTask::SetRenameFiles(bool bRenameFiles)
{
	m_bRenameFiles = bRenameFiles;
}

void OrganizeTask::SetFileTemplate(std::string strTemplate)
{
	m_strFileTemplate = strTemplate;
}

void OrganizeTask::SetAppendedText(std::string strAppend)
{
	m_strAppendedText = strAppend;
}

void OrganizeTask::SetDayExtension(int extension)
{
	m_iDayExtension = extension;
}

std::string OrganizeTask::DoVariableSubstitution(std::string strTemplate, GDateTime* datetime)
{
	std::map<std::string, std::string> mapSubstFields;

	gchar szDate[20];

	g_snprintf(szDate, 20, "%04d",
		g_date_time_get_year(datetime));
	mapSubstFields.insert(std::pair<std::string, std::string>("YYYY", szDate));

	g_snprintf(szDate, 20, "%02d",
		(guint)g_date_time_get_month(datetime));
	mapSubstFields.insert(std::pair<std::string, std::string>("MM", szDate));

	g_snprintf(szDate, 20, "%02d",
		g_date_time_get_day_of_month(datetime));
	mapSubstFields.insert(std::pair<std::string, std::string>("DD", szDate));

	std::map<std::string, std::string>::iterator itr;
	for (itr = mapSubstFields.begin(); mapSubstFields.end() != itr; ++itr)
	{
		strTemplate = boost::replace_all_copy( strTemplate, itr->first, itr->second);
	}
	return strTemplate;
}

void OrganizeTask::Cancelled()
{
	g_cancellable_cancel(m_PrivateImplPtr->m_pCancellable);
}

static void
organize_task_gfile_progress_callback(
	goffset current_num_bytes,
	goffset total_num_bytes,
	gpointer user_data)
{
	//OrganizeTask::PrivateImpl* pImpl = static_cast<OrganizeTask::PrivateImpl*>(user_data);

}

void OrganizeTask::Run()
{
	std::string strText;
	std::map<std::string, int> mapFileCounter;

	// populate list:
	ImageListPtr imgListPtr(new ImageList(false));
	std::list<std::string> listFiles;
	listFiles.push_back(m_strSrcDirURI);
	imgListPtr->SetImageList(&listFiles, m_bIncludeSubfolders);

	// adjust exif date
	m_vectQuiverFiles = imgListPtr->GetQuiverFiles();	
	while (m_iCurrentFile < m_vectQuiverFiles.size() )
	{
		GError* error = NULL;
		QuiverFile f = m_vectQuiverFiles[m_iCurrentFile++];

		GDateTime* datetime = g_date_time_new_from_unix_local(f.GetTimeT());

		std::string strFolder = DoVariableSubstitution(m_strFolderTemplate, datetime);
		std::string strDstDir = m_strDestDirURI + G_DIR_SEPARATOR_S + strFolder + m_strAppendedText;

		GFile* dstdir = g_file_new_for_uri(strDstDir.c_str());
		gboolean made_dir = 
			g_file_make_directory_with_parents(
				dstdir, m_PrivateImplPtr->m_pCancellable, &error);

		g_object_unref(dstdir);

		if (NULL != error)
		{ 
			if (G_IO_ERROR_EXISTS != error->code)
			{
				printf("error creating directory %s\n", strDstDir.c_str());
				g_error_free(error);
				continue;
			}
			else
			{
				g_error_free(error);
				error = NULL;
			}

		}

		std::string strFilename = f.GetFileName();

		if (m_bRenameFiles)
		{
			std::string strExtension;
			std::string::size_type pos = strFilename.find_last_of(".");
			if (std::string::npos != pos)
			{
				strExtension = strFilename.substr(pos+1);
			}

			std::string strDstNameTmp = RenameTask::DoVariableSubstitution(m_strFileTemplate, datetime, 0);
			int count = 0;
			if (mapFileCounter.end() != mapFileCounter.find(strDstNameTmp))
			{
				count = mapFileCounter[strDstNameTmp];
			}
			strFilename = RenameTask::DoVariableSubstitution(m_strFileTemplate, datetime, ++count);
			strFilename += "." + strExtension;

			mapFileCounter[strDstNameTmp] = count;
		}
		g_date_time_unref(datetime);


		std::string strDstPath = strDstDir + G_DIR_SEPARATOR_S + strFilename;

		GFile* src = g_file_new_for_uri(f.GetURI());
		GFile* dst = g_file_new_for_uri(strDstPath.c_str());

		char* dstname = g_file_get_parse_name(dst);

		GFileInfo* info = g_file_query_info(src,
			G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME,
			G_FILE_QUERY_INFO_NONE,
			m_PrivateImplPtr->m_pCancellable,
			NULL);

		if ( NULL != info)
		{
			const char* display_name = g_file_info_get_display_name(info);
			strText = boost::str(boost::format("Copying %s to %s") % display_name % dstname);
			g_object_unref(info);
		}
		else
		{
			gchar* shortname = g_file_get_basename(src);
			strText = boost::str(boost::format("Copying %s to %s") % shortname % dstname);
			g_free(shortname);
		}
		
		SetProgressText(strText);
		EmitTaskProgressUpdatedEvent();
		g_free(dstname);

		GFileCopyFlags flags = G_FILE_COPY_NONE;
		// G_FILE_COPY_OVERWRITE
		// FIXME: have an option to overwrite files 
		/*
		gchar buffer[512] = "";
		g_snprintf(buffer, 512, "A file named \"%s\" already exists. Do you want to replace it?",shortname);
		std::string msg(buffer);
		g_snprintf(buffer, 512, "The file already exists in \"%s\". Replacing it will overwrite its content.",dirname);
		std::string details(buffer);

		g_free(dirname);
		g_free(shortname);

		MessageBox box(MessageBox::ICON_TYPE_INFO, MessageBox::BUTTON_TYPE_NONE, msg, details);
		box.AddButton(MessageBox::BUTTON_ICON_CANCEL, "_Cancel", MessageBox::RESPONSE_TYPE_CUSTOM1);
		box.AddButton(MessageBox::BUTTON_ICON_NONE, "S_kip All", MessageBox::RESPONSE_TYPE_CUSTOM5);
		box.AddButton(MessageBox::BUTTON_ICON_NONE, "Replace _All", MessageBox::RESPONSE_TYPE_CUSTOM3);
		box.AddButton(MessageBox::BUTTON_ICON_NONE, "_Skip", MessageBox::RESPONSE_TYPE_CUSTOM4);
		box.AddButton(MessageBox::BUTTON_ICON_NONE, "_Replace", MessageBox::RESPONSE_TYPE_CUSTOM2);
		box.SetDefaultResponseType(MessageBox::RESPONSE_TYPE_CUSTOM2);

		MessageBox::ResponseType responseType = box.Run();

		pImpl->m_iLastXFerRVal = (int)(responseType - MessageBox::RESPONSE_TYPE_CUSTOM1);
		*/
		error = NULL;
		gboolean copied = 
			g_file_copy(src,
				dst,
				flags,
				m_PrivateImplPtr->m_pCancellable,
				organize_task_gfile_progress_callback,
				m_PrivateImplPtr.get(),
				&error);
		// if there was an error, 
		if (NULL != error)
		{
			printf("Error copying file! %s to %s: %s\n", f.GetURI(), strDstPath.c_str(), error->message); 
			// message box asking if they want to skip, skip all, retry, cancel
			g_error_free(error);
			error = NULL;
		}

		g_object_unref(dst);
		g_object_unref(src);


		EmitTaskProgressUpdatedEvent();


		if (ShouldPause())
		{
			break;
		}

		if (ShouldCancel())
		{
			break;
		}
	}
}


