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
		GFile* trashed_file = g_file_new_for_uri(quiverFile.GetURI());
		char* base_name = g_file_get_basename(trashed_file);

		const char* data_dir = g_get_user_data_dir();
		if (!data_dir) {
			g_object_unref(trashed_file);
			g_free(base_name);
			return false;
		}
        char* trash_dir = g_build_filename(data_dir, "Trash", NULL);
		char* info_path = g_build_filename(trash_dir, "info", base_name, ".trashinfo", NULL);
        g_free(trash_dir);
		GKeyFile* key_file = g_key_file_new();
		bool success = false;

		if (g_key_file_load_from_file(key_file, info_path, G_KEY_FILE_NONE, NULL)) {
			char* original_path = g_key_file_get_string(key_file, "Trash Info", "Path", NULL);
			if (original_path) {
				GFile* original_file_parent = g_file_new_for_path(original_path);
                GFile* dest_dir = g_file_get_parent(original_file_parent);

                if (dest_dir) {
				    success = g_file_move(trashed_file, dest_dir, G_FILE_COPY_NONE, NULL, NULL, NULL, NULL);
				    if (success) {
					    GFile* info_file = g_file_new_for_path(info_path);
					    g_file_delete(info_file, NULL, NULL);
					    g_object_unref(info_file);
				    }
                    g_object_unref(dest_dir);
                }
				g_free(original_path);
                g_object_unref(original_file_parent);
			}
		}

		g_key_file_free(key_file);
		g_free(info_path);
		g_free(base_name);
		g_object_unref(trashed_file);

		return success;
	}

	bool CopyFile(QuiverFile src, QuiverFile dst)
	{
		GFile* src_file = g_file_new_for_uri(src.GetURI());
		GFile* dst_dir = g_file_new_for_uri(dst.GetURI());
		gboolean rval = g_file_copy(src_file, dst_dir, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, NULL);
		g_object_unref(src_file);
		g_object_unref(dst_dir);
		return rval;
	}

	bool MoveFile(QuiverFile src, QuiverFile dst)
	{
		GFile* src_file = g_file_new_for_uri(src.GetURI());
		GFile* dst_dir = g_file_new_for_uri(dst.GetURI());
		GError* error = NULL;
		gboolean rval = g_file_move(src_file, dst_dir, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, &error);
		if (error)
		{
			cout << "Error moving file: " << error->message << endl;
			g_error_free(error);
		}
		g_object_unref(src_file);
		g_object_unref(dst_dir);
		return rval;
	}

}
