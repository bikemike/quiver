#include "RenameTask.h"

#include "ImageSaveManager.h"
#include "MessageBox.h"
#include "ImageList.h"

#include <map>
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include <sstream>

#include <gio/gio.h>

class RenameTask::PrivateImpl
{
public:
	PrivateImpl(RenameTask* parent) : 
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

	RenameTask* m_pParent;
	int           m_iLastXFerRVal;
	int           m_iLastVFSErrorRVal;
	GCancellable* m_pCancellable;

};

RenameTask::RenameTask()
	: m_PrivateImplPtr(new PrivateImpl(this)), m_iCurrentFile(0), m_iStartNumber(1), m_eSortBy(ImageList::SORT_BY_DATE)
{
	//SetOutputFolder("~");
}
RenameTask::~RenameTask()
{
}

std::string RenameTask::GetDescription() const
{
	return "Renaming Images";
}

// quantity type may be kb, items, images, files, 
// or anything else the task iterates over
std::string RenameTask::GetIterationTypeName(bool shortname, bool plural ) const
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
int RenameTask::GetTotalIterations() const
{
	return m_vectQuiverFiles.size();
}

int RenameTask::GetCurrentIteration() const
{
	return m_iCurrentFile;
}

double RenameTask::GetProgress() const
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

void RenameTask::AddFile(QuiverFile quiverFile)
{
	m_vectQuiverFiles.push_back(quiverFile);
}

void RenameTask::AddFiles(std::vector<QuiverFile> vectQuiverFiles)
{
	m_vectQuiverFiles.insert(m_vectQuiverFiles.end(), vectQuiverFiles.begin(), vectQuiverFiles.end());
}

void RenameTask::SetInputFolder(std::string strSrcURI)
{
	m_strSrcDirURI = strSrcURI;
}

// if not set, the default is an empty template
void RenameTask::SetTemplate(std::string strTemplate)
{
	m_strTemplate = strTemplate;
}

void RenameTask::SetStartNumber(int number)
{
	m_iStartNumber = number;
}

void RenameTask::SetSortBy(ImageList::SortBy sortBy)
{
	m_eSortBy = sortBy;
}

std::string  RenameTask::DoVariableSubstitution(std::string strTemplate, GDateTime* datetime, int count)
{
	std::map<std::string, std::string> mapSubstFields;

	std::string subItem;
	subItem = boost::str(boost::format("%04d") % g_date_time_get_year(datetime));
	mapSubstFields.insert(std::make_pair("%Y", subItem));

	subItem = boost::str(boost::format("%02d") % g_date_time_get_month(datetime));
	mapSubstFields.insert(std::make_pair("%m", subItem));

	subItem = boost::str(boost::format("%02d") % g_date_time_get_day_of_month(datetime));
	mapSubstFields.insert(std::make_pair("%d", subItem));

	subItem = boost::str(boost::format("%02d") % g_date_time_get_hour(datetime));
	mapSubstFields.insert(std::make_pair("%H", subItem));

	subItem = boost::str(boost::format("%02d") % g_date_time_get_minute(datetime));
	mapSubstFields.insert(std::make_pair("%M", subItem));

	subItem = boost::str(boost::format("%02d") % g_date_time_get_second(datetime));
	mapSubstFields.insert(std::make_pair("%S", subItem));

	subItem = boost::str(boost::format("%d") % count);
	mapSubstFields.insert(std::make_pair("#", subItem));
	subItem = boost::str(boost::format("%02d") % count);
	mapSubstFields.insert(std::make_pair("##", subItem));
	subItem = boost::str(boost::format("%03d") % count);
	mapSubstFields.insert(std::make_pair("###", subItem));
	subItem = boost::str(boost::format("%04d") % count);
	mapSubstFields.insert(std::make_pair("####", subItem));
	subItem = boost::str(boost::format("%05d") % count);
	mapSubstFields.insert(std::make_pair("#####", subItem));
	subItem = boost::str(boost::format("%06d") % count);
	mapSubstFields.insert(std::make_pair("######", subItem));
	subItem = boost::str(boost::format("%07d") % count);
	mapSubstFields.insert(std::make_pair("#######", subItem));
	subItem = boost::str(boost::format("%08d") % count);
	mapSubstFields.insert(std::make_pair("########", subItem));

	std::map<std::string, std::string>::reverse_iterator itr;
	for (itr = mapSubstFields.rbegin(); mapSubstFields.rend() != itr; ++itr)
	{
		strTemplate = boost::replace_all_copy( strTemplate, itr->first, itr->second);
	}
	return strTemplate;
}

static std::string
get_display_name(GFile* file)
{
	std::string strDisplayName;
	GFileInfo* info = g_file_query_info(file, G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME, G_FILE_QUERY_INFO_NONE, NULL, NULL);
	if (NULL != info)
	{
		strDisplayName = g_file_info_get_display_name(info);
		g_object_unref(info);
	}
	return strDisplayName;
}


static void
organize_task_gfile_progress_callback(
	goffset current_num_bytes,
	goffset total_num_bytes,
	gpointer user_data)
{
	//RenameTask::PrivateImpl* pImpl = static_cast<RenameTask::PrivateImpl*>(user_data);

}


void RenameTask::Run()
{
	char szText[256];
	// populate list:

	// FIXME: this should be a class variable so it isn't
	// updated on pause/resume
	ImageListPtr imgListPtr(new ImageList(false));

	std::list<std::string> listFiles;
	listFiles.push_back(m_strSrcDirURI);

	imgListPtr->SetImageList(&listFiles, false);
	imgListPtr->Sort(m_eSortBy);

	m_vectQuiverFiles = imgListPtr->GetQuiverFiles();	

	std::map<std::string, int> mapFileCounter;

	// FIXME: this should be a class variable so it isn't
	// updated on pause/resume
	std::vector<std::string> m_vectSrc;
	//for () // go through the list and make s
	{
		//  src = 
		//  dst = getDestName()

	}

	while (m_iCurrentFile < m_vectQuiverFiles.size() )
	{

		QuiverFile f = m_vectQuiverFiles[m_iCurrentFile];

		GFile* src    = g_file_new_for_uri(f.GetURI());
		GFile* srcdir = g_file_get_parent(src);
		char* basename = g_file_get_basename(src);

		std::string strBaseName(basename);
		std::string strExtension;

		std::string::size_type pos = strBaseName.find_last_of(".");
		if (std::string::npos != pos)
		{
			strExtension = strBaseName.substr(pos+1);
		}

		GDateTime* datetime = g_date_time_new_from_unix_local(f.GetTimeT());

		std::string strDstNameTmp = DoVariableSubstitution(m_strTemplate, datetime, 0);
		int count = 0;
		if (mapFileCounter.end() != mapFileCounter.find(strDstNameTmp))
		{
			count = mapFileCounter[strDstNameTmp];
		}
		std::string strDstName = DoVariableSubstitution(m_strTemplate, datetime, ++count);
		mapFileCounter[strDstNameTmp] = count;

		g_date_time_unref(datetime);

		gchar* dstname = NULL;
		if (!strExtension.empty())
			dstname = g_strdup_printf("%s.%s", strDstName.c_str(), strExtension.c_str());
		else
			dstname = g_strdup_printf("%s", strDstName.c_str());

		GFile* dst = g_file_get_child(srcdir, dstname);

		char* dst_display_name = g_file_get_parse_name(dst);

		GFileInfo* info = g_file_query_info(src,
			G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME,
			G_FILE_QUERY_INFO_NONE,
			m_PrivateImplPtr->m_pCancellable,
			NULL);

		if ( NULL != info)
		{
			const char* display_name = g_file_info_get_display_name(info);
			g_snprintf(szText, 256, "Renaming %s to %s", display_name, dst_display_name);
			g_object_unref(info);
		}
		else
		{
			gchar* shortname = g_file_get_basename(src);
			g_snprintf(szText, 256, "Renaming %s to %s", shortname, dst_display_name);
			g_free(shortname);
		}

		printf(szText);
		printf("\n");

		g_free(dst_display_name);

		SetProgressText(szText);
		EmitTaskProgressUpdatedEvent();

		GFileCopyFlags flags = G_FILE_COPY_NONE;

		GError* error = NULL;
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
			printf("Error renaming file! %s -> %s : %s\n", f.GetURI(), dst_display_name, error->message); 
			g_error_free(error);
			error = NULL;
			// message box asking if they want to skip, skip all, retry, cancel
		}



		g_object_unref(dst);

		g_free(dstname);
		g_free(basename);
		g_object_unref(srcdir);
		g_object_unref(src);

	

		/* FIXME: add msgbox for file exists prompt

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

		++m_iCurrentFile;
		EmitTaskProgressUpdatedEvent();


		if (ShouldPause())
		{
			//break;
		}

		if (ShouldCancel())
		{
			//break;
		}
	}				
}

void RenameTask::Cancelled()
{
	g_cancellable_cancel(m_PrivateImplPtr->m_pCancellable);
}



