#include "QuiverFileOps.h"

#include <iostream>
#include <gio/gio.h>

using namespace std;

namespace QuiverFileOps
{

	class StatusCallback::PrivateImpl
	{
	public:

		PrivateImpl(StatusCallback* parent) : m_pParent(parent)
		{
		}





		GCancellable* cancellable;

		static void progress_callback(
			goffset current_num_bytes,
			goffset total_num_bytes,
			gpointer user_data);

		StatusCallback* m_pParent;

	};

	StatusCallback::StatusCallback()
	{
		m_PrivateImplPtr = PrivateImplPtr(new PrivateImpl(this));
	}

	double StatusCallback::GetProgress() { return 0.0; }
	void StatusCallback::Cancel() {}
	StatusCallback::~StatusCallback()
	{
	}

	bool Delete(QuiverFile quiverFile)
	{
		GFile* file = g_file_new_for_uri(quiverFile.GetURI());
		gboolean rval = g_file_delete(file, NULL, NULL);
		if (rval)
		{
			// delete the thumbnails now
			quiverFile.RemoveCachedThumbnail(false);
			quiverFile.RemoveCachedThumbnail(true);
		}
		g_object_unref(file);
		return rval;
	}

	bool MoveToTrash(QuiverFile quiverFile)
	{
		GFile* gfile = g_file_new_for_uri( quiverFile.GetURI() );
		return g_file_trash(gfile, NULL, NULL);
	}

	bool RestoreFromTrash(QuiverFile quiverFile)
	{
		// GFileInfo
		// G_FILE_ATTRIBUTE_TRASH_DELETION_DATE
		// G_FILE_ATTRIBUTE_TRASH_ORIG_PATH
		// GDateTime *
		// g_file_info_get_deletion_date (GFileInfo *info);
		// need to be able to undo MoveToTrash() operation
		// search through "trash://" using gio gfileinfo for file with
        // metadata trash::orig-path and trash::deletion-date
	}

	bool CopyFile(QuiverFile src, QuiverFile dst)
	{
		// g_file_copy
	}

	bool MoveFile(QuiverFile src, QuiverFile dst)
	{
		// g_file_move
	}

}

