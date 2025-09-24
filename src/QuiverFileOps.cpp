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
		gboolean rval = g_file_trash(gfile, NULL, NULL);
        if (rval)
        {
            quiverFile.RemoveCachedThumbnail(false);
            quiverFile.RemoveCachedThumbnail(true);
        }
        g_object_unref(gfile);
		return rval;
	}

	bool RestoreFromTrash(QuiverFile quiverFile)
	{
		GFile* trash_dir = g_file_new_for_uri("trash:///");
		if (!trash_dir) {
			return false;
		}

		GFileEnumerator* enumerator = g_file_enumerate_children(
			trash_dir,
			"standard::name," G_FILE_ATTRIBUTE_TRASH_ORIG_PATH,
			G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
			NULL, NULL);

		if (!enumerator) {
			g_object_unref(trash_dir);
			return false;
		}

		GFileInfo* info;
		bool success = false;
		const char* original_uri_to_find = quiverFile.GetURI();

		while ((info = g_file_enumerator_next_file(enumerator, NULL, NULL)) != NULL) {
			const char* orig_path_str = g_file_info_get_attribute_string(info, G_FILE_ATTRIBUTE_TRASH_ORIG_PATH);
			if (orig_path_str) {
				GFile* orig_gfile = g_file_new_for_path(orig_path_str);
				char* orig_uri = g_file_get_uri(orig_gfile);

				if (g_strcmp0(orig_uri, original_uri_to_find) == 0) {
					// Found the file in the trash.
					GFile* trashed_file_to_restore = g_file_enumerator_get_child(enumerator, info);
					if (trashed_file_to_restore) {
						GError* error = NULL;
                        // Manually move the file since g_file_untrash() is not available
                        GFile* destination = g_file_new_for_path(orig_path_str);
						if (g_file_move(trashed_file_to_restore, destination, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, &error)) {
							success = true;
						} else {
							if (error) {
								cerr << "Error restoring file from trash: " << error->message << endl;
								g_error_free(error);
							}
						}
                        g_object_unref(destination);
						g_object_unref(trashed_file_to_restore);
					}
					g_free(orig_uri);
					g_object_unref(orig_gfile);
					g_object_unref(info);
					break; // Stop searching
				}

				g_free(orig_uri);
				g_object_unref(orig_gfile);
			}
			g_object_unref(info);
		}

		g_file_enumerator_close(enumerator, NULL, NULL);
		g_object_unref(enumerator);
		g_object_unref(trash_dir);

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