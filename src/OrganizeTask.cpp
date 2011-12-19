#include "OrganizeTask.h"

#include "ImageSaveManager.h"
#include "MessageBox.h"
#include "ImageList.h"

#include <map>
#include <boost/algorithm/string.hpp>
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

OrganizeTask::OrganizeTask()
	: m_PrivateImplPtr(new PrivateImpl(this)), m_iCurrentFile(0), m_bIncludeSubfolders(false), m_iDayExtension(0)
{
	SetDateTemplate("YYYY-MM-DD");
	//SetOutputFolder("~");
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
void OrganizeTask::SetDateTemplate(std::string strTemplate)
{
	m_strDateTemplate = strTemplate;
}

void OrganizeTask::SetAppendedText(std::string strAppend)
{
	m_strAppendedText = strAppend;
}

void OrganizeTask::SetDayExtension(int extension)
{
	m_iDayExtension = extension;
}

std::string OrganizeTask::DoVariableSubstitution(QuiverFile f, std::string strTemplate)
{
	std::map<std::string, std::string> mapSubstFields;
	ExifData *pExifData = f.GetExifData();

	time_t date = 0;
	if (NULL != pExifData)
	{
		// use date_time_original
		ExifEntry* pEntry;
		pEntry = exif_data_get_entry(pExifData,EXIF_TAG_DATE_TIME);
		if (NULL != pEntry)
		{
			char szDate[20];
			exif_entry_get_value(pEntry,szDate,20);

			tm tm_exif_time;
			int num_substs = sscanf(szDate,"%04d:%02d:%02d %02d:%02d:%02d",
				&tm_exif_time.tm_year,
				&tm_exif_time.tm_mon,
				&tm_exif_time.tm_mday,
				&tm_exif_time.tm_hour,
				&tm_exif_time.tm_min,
				&tm_exif_time.tm_sec);
			tm_exif_time.tm_year -= 1900;
			tm_exif_time.tm_hour -= m_iDayExtension;
			tm_exif_time.tm_mon -= 1;
			tm_exif_time.tm_isdst = -1;
			if (6 == num_substs)
			{
				
				// successfully parsed date
				date = mktime(&tm_exif_time);

				g_snprintf(szDate, 20, "%04d",
					tm_exif_time.tm_year+1900);
				mapSubstFields.insert(std::pair<std::string, std::string>("YYYY", szDate));

				g_snprintf(szDate, 20, "%02d",
					tm_exif_time.tm_mon+1);
				mapSubstFields.insert(std::pair<std::string, std::string>("MM", szDate));

				g_snprintf(szDate, 20, "%02d",
					tm_exif_time.tm_mday);
				mapSubstFields.insert(std::pair<std::string, std::string>("DD", szDate));
				
			}
			
		}
	}
	exif_data_unref(pExifData);

	std::map<std::string, std::string>::iterator itr;
	for (itr = mapSubstFields.begin(); mapSubstFields.end() != itr; ++itr)
	{
		strTemplate = boost::replace_all_copy( strTemplate, itr->first, itr->second);
	}
	return strTemplate;
}

#ifdef FIXME
static gint gnome_vfs_xfer_callback (GnomeVFSXferProgressInfo *info, gpointer user_data)
{
	OrganizeTask::PrivateImpl* pImpl = static_cast<OrganizeTask::PrivateImpl*>(user_data);

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


void OrganizeTask::Cancelled()
{
	g_cancellable_cancel(m_PrivateImplPtr->m_pCancellable);
}

static void
organize_task_gfile_progress
	(goffset current_num_bytes, goffset total_num_bytes, gpointer user_data)
{
	//OrganizeTask::PrivateImpl* pImpl = static_cast<OrganizeTask::PrivateImpl*>(user_data);
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
	char szText[256];
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
		QuiverFile f = m_vectQuiverFiles[m_iCurrentFile];
		++m_iCurrentFile;
		std::string strOutput = DoVariableSubstitution(f, m_strDateTemplate);
		
		std::string strDstDir = m_strDestDirURI + G_DIR_SEPARATOR_S + strOutput + m_strAppendedText;

		GFile* dstdir = g_file_new_for_uri(strDstDir.c_str());
		gboolean made_dir = 
			g_file_make_directory_with_parents(
				dstdir, m_PrivateImplPtr->m_pCancellable, &error);

		g_object_unref(dstdir);

		if (!made_dir && G_IO_ERROR_EXISTS != error->code)
		{
			printf("error creating directory %s\n", strDstDir.c_str());
			continue;
		}

		strOutput = strDstDir + G_DIR_SEPARATOR_S + f.GetFileName();

		GFile* src = g_file_new_for_uri(f.GetURI());
		GFile* dst = g_file_new_for_uri(strOutput.c_str());

		// FIXME: should use g_file_query for display name
		gchar* shortname = g_file_get_basename(dst);
		g_snprintf(szText, 256, "Copying %s to %s", shortname, strDstDir.c_str());
		//SetMessage(MSG_TYPE_INFO, szText);
		SetProgressText(szText);
		EmitTaskProgressUpdatedEvent();
		g_free(shortname);

		GFileCopyFlags flags = G_FILE_COPY_NONE;
		// G_FILE_COPY_OVERWRITE
		// FIXME: have an option to overwrite files 
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
		if (!copied)
		{
			printf("Error copying file! %s to %s\n", f.GetURI(), strOutput.c_str()); 
			// message box asking if they want to skip, skip all, retry, cancel
			if (NULL != error)
			{
			}
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


