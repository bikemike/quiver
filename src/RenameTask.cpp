#include "RenameTask.h"

#include "ImageSaveManager.h"
#include "MessageBox.h"
#include "ImageList.h"

#include <map>
#include <boost/algorithm/string.hpp>
#include <sstream>

#include <gio/gio.h>

#ifdef FIXME
#include <libgnomevfs/gnome-vfs.h>
#endif

class RenameTask::PrivateImpl
{
public:
	PrivateImpl(RenameTask* parent) : 
		m_pParent(parent), 
		m_iLastXFerRVal(-1),
		m_iLastVFSErrorRVal(-1)
	{

	}

	RenameTask* m_pParent;
	int           m_iLastXFerRVal;
	int           m_iLastVFSErrorRVal;

};

RenameTask::RenameTask()
	: m_PrivateImplPtr(new PrivateImpl(this)), m_iCurrentFile(0), m_bIncludeSubfolders(false), m_iStartNumber(1), m_eSortBy(ImageList::SORT_BY_DATE)
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
	double progress = 0;
	if (IsFinished() || 0 == m_vectQuiverFiles.size())
	{
		progress = 1.;
	}
	else 
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

void RenameTask::SetIncludeSubfolders(bool bIncludeSubfolders)
{
	m_bIncludeSubfolders = bIncludeSubfolders;
}


// the output directory must be specified
void RenameTask::SetOutputFolder(std::string strDestDirURI)
{
	m_strDestDirURI = strDestDirURI;
}

// if not set, the default format is: YYYY-MM-DD
// pulled from the exif data
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

std::string RenameTask::DoVariableSubstitution(QuiverFile f, std::string strTemplate)
{
	return std::string();
}

#ifdef FIXME
static gint gnome_vfs_xfer_callback (GnomeVFSXferProgressInfo *info, gpointer user_data)
{
	RenameTask::PrivateImpl* pImpl = static_cast<RenameTask::PrivateImpl*>(user_data);

	//printf("status: %d\n", (int)info->status);
	//printf("vfs result: %s\n", gnome_vfs_result_to_string(info->vfs_status));
	//printf("phase: %d\n", (int)info->phase);
	/* do progress */
	/*
	gulong file_index;
		The index of the currently processed file.

	gulong files_total;
		The total number of processed files.

	GnomeVFSFileSize bytes_total;
		The total size of all files to transfer in bytes.

	GnomeVFSFileSize file_size;
		The size of the currently processed file in bytes.

	GnomeVFSFileSize bytes_copied;
		The number of bytes that has been transferred from the current file.

	GnomeVFSFileSize total_bytes_copied;
		The total number of bytes that has been transferred. 
	*/

	if (GNOME_VFS_XFER_PROGRESS_STATUS_OK == info->status)
	{
		return 1;
	}

	if (GNOME_VFS_XFER_PROGRESS_STATUS_OVERWRITE == info->status)
	{
		/*
		GNOME_VFS_XFER_OVERWRITE_ACTION_ABORT = 0,
		GNOME_VFS_XFER_OVERWRITE_ACTION_REPLACE = 1,
		GNOME_VFS_XFER_OVERWRITE_ACTION_REPLACE_ALL = 2,
		GNOME_VFS_XFER_OVERWRITE_ACTION_SKIP = 3,
		GNOME_VFS_XFER_OVERWRITE_ACTION_SKIP_ALL = 4
		*/

		if (GNOME_VFS_XFER_OVERWRITE_ACTION_SKIP_ALL != pImpl->m_iLastXFerRVal && 
			GNOME_VFS_XFER_OVERWRITE_ACTION_REPLACE_ALL != pImpl->m_iLastXFerRVal)
		{
			
			GnomeVFSURI* vuri_dest = gnome_vfs_uri_new(info->target_name);
			gchar* shortname = gnome_vfs_uri_extract_short_name(vuri_dest);
			gchar* dirname = gnome_vfs_uri_extract_dirname (vuri_dest);
			gnome_vfs_uri_unref(vuri_dest);

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
		}

		if (GNOME_VFS_XFER_OVERWRITE_ACTION_ABORT == pImpl->m_iLastXFerRVal)
		{
			// cancel the task
			pImpl->m_pParent->Cancel();
		}

		return pImpl->m_iLastXFerRVal;
	}
	else if (GNOME_VFS_XFER_PROGRESS_STATUS_VFSERROR == info->status)
	{
		/*
		return GNOME_VFS_XFER_ERROR_ACTION_ABORT;
		GNOME_VFS_XFER_ERROR_ACTION_RETRY = 1,
		GNOME_VFS_XFER_ERROR_ACTION_SKIP = 2
		*/
		if (GNOME_VFS_ERROR_FILE_EXISTS != info->vfs_status)
		{
			MessageBox box(
					MessageBox::ICON_TYPE_INFO, 
					MessageBox::BUTTON_TYPE_NONE, 
					"Error", 
					gnome_vfs_result_to_string(info->vfs_status));

			box.AddButton(MessageBox::BUTTON_ICON_INFO, "Abort", MessageBox::RESPONSE_TYPE_CUSTOM1);
			box.AddButton(MessageBox::BUTTON_ICON_NONE, "Retry", MessageBox::RESPONSE_TYPE_CUSTOM2);
			box.AddButton(MessageBox::BUTTON_ICON_NONE, "Skip", MessageBox::RESPONSE_TYPE_CUSTOM3);

			MessageBox::ResponseType responseType = box.Run();

			return (int)(responseType - MessageBox::RESPONSE_TYPE_CUSTOM1);
		}
		return GNOME_VFS_XFER_ERROR_ACTION_ABORT;

	}

	return GNOME_VFS_XFER_ERROR_ACTION_ABORT;
}
#endif

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

void RenameTask::Run()
{
	char szText[256];
	// populate list:
	ImageListPtr imgListPtr(new ImageList(false));
	std::list<std::string> listFiles;
	listFiles.push_back(m_strSrcDirURI);
	imgListPtr->SetImageList(&listFiles, m_bIncludeSubfolders);
	imgListPtr->Sort(m_eSortBy);

	m_vectQuiverFiles = imgListPtr->GetQuiverFiles();	
	// adjust exif date
	int iNumber = m_iStartNumber;
	gchar szNewFilename[256] = "";
	while (m_iCurrentFile < m_vectQuiverFiles.size() )
	{
		QuiverFile f = m_vectQuiverFiles[m_iCurrentFile];
		//std::string strOutput = DoVariableSubstitution(f, m_strDateTemplate);
		std::string strExt;
		std::string srcFile = f.GetFilePath();
		std::string::size_type pos = srcFile.find(".");
		if (std::string::npos != pos)
		{
			std::string::size_type slash_pos = srcFile.find("/");
			if (-1 != slash_pos && slash_pos < pos)
			{
				strExt = srcFile.substr(pos);
			}
		}

        g_snprintf(szNewFilename, 256, "%s%04d%s", m_strTemplate.c_str(), iNumber++, strExt.c_str());
	
		std::string strOutput = m_strDestDirURI;
		
#ifdef FIXME
		gnome_vfs_make_directory(strOutput.c_str(),0700);
		strOutput += "/" + std::string(szNewFilename);

		GnomeVFSURI* src = gnome_vfs_uri_new(f.GetFilePath().c_str());
		GnomeVFSURI* dst = gnome_vfs_uri_new(strOutput.c_str());

		gchar* shortname = gnome_vfs_uri_extract_short_name(src);
		gchar* dirname = gnome_vfs_uri_extract_dirname (dst);
		g_snprintf(szText, 256, "Copying %s to %s", shortname, dirname);
		//SetMessage(MSG_TYPE_INFO, szText);
		SetProgressText(szText);
		EmitTaskProgressUpdatedEvent();
		g_free(dirname);
		g_free(shortname);

/* new
		GFile* dir_output = g_file_new_for_uri(strOutput.c_str());
		g_file_make_directory(dir_output, NULL, NULL);

		GFile* file_output = g_file_get_child(dir_output, szNewFilename);
		GFile* file_input  = g_file_new_for_uri(f.GetURI());

		std::string shortname = gnome_vfs_uri_extract_short_name(file_input);
		std::string dirname = gnome_vfs_uri_extract_dirname (dir_output);

		g_snprintf(szText, 256, "Copying %s to %s", shortname.c_str(), dirname.c_str());
*/

		GnomeVFSResult res = gnome_vfs_xfer_uri (
			src,
			dst,
			GNOME_VFS_XFER_DEFAULT,
			GNOME_VFS_XFER_ERROR_MODE_QUERY,
			GNOME_VFS_XFER_OVERWRITE_MODE_QUERY,
			gnome_vfs_xfer_callback,
			m_PrivateImplPtr.get());

		//printf("vfs result: %s\n", gnome_vfs_result_to_string(res));

		gnome_vfs_uri_unref(src);
		gnome_vfs_uri_unref(dst);
#endif

		++m_iCurrentFile;
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


