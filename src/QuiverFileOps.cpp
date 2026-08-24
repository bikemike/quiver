#include "QuiverFileOps.h"

#include <string.h>

#include <gio/gio.h>

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

	static void warn_op_error(const char* op, const char* name, GError* error)
	{
		g_warning("%s failed for %s: %s", op, name, error->message);
	}

	bool Delete(QuiverFile quiverFile)
	{
		bool rval = false;
		GFile* file = g_file_new_for_uri(quiverFile.GetURI());
		GError* error = NULL;
		if (g_file_delete(file, NULL, &error))
		{
			rval = true;
			quiverFile.RemoveCachedThumbnail(-1);
		}
		else if (NULL != error)
		{
			warn_op_error("delete", quiverFile.GetURI(), error);
			g_error_free(error);
		}
		g_object_unref(file);
		return rval;
	}

	bool MoveToTrash(QuiverFile quiverFile)
	{
		bool rval = false;
		GFile* file = g_file_new_for_uri(quiverFile.GetURI());
		GError* error = NULL;
		if (g_file_trash(file, NULL, &error))
		{
			rval = true;
			quiverFile.RemoveCachedThumbnail(-1);
		}
		else if (NULL != error)
		{
			warn_op_error("move to trash", quiverFile.GetURI(), error);
			g_error_free(error);
		}
		g_object_unref(file);
		return rval;
	}

	bool RestoreFromTrash(QuiverFile quiverFile)
	{
		bool rval = false;

		char* orig_path = g_filename_from_uri(quiverFile.GetURI(), NULL, NULL);
		if (NULL == orig_path)
		{
			return false;
		}

		GFile* trash = g_file_new_for_uri("trash:///");
		GError* error = NULL;
		GFileEnumerator* enumerator =
			g_file_enumerate_children(trash,
				G_FILE_ATTRIBUTE_TRASH_ORIG_PATH,
				G_FILE_QUERY_INFO_NONE, NULL, &error);

		if (NULL != enumerator)
		{
			for (;;)
			{
				GFileInfo* info =
					g_file_enumerator_next_file(enumerator, NULL, &error);
				if (NULL != error || NULL == info)
					break;

				const char* candidate =
					g_file_info_get_attribute_byte_string(
						info, G_FILE_ATTRIBUTE_TRASH_ORIG_PATH);

				if (NULL != candidate && 0 == strcmp(candidate, orig_path))
				{
					GFile* trashed = g_file_enumerator_get_child(enumerator, info);
					GFile* dest = g_file_new_for_path(orig_path);

					GFile* parent = g_file_get_parent(dest);
					GError* dir_error = NULL;
					if (!g_file_make_directory_with_parents(parent, NULL, &dir_error) &&
						NULL != dir_error)
					{
						g_error_free(dir_error);
					}
					g_object_unref(parent);

					rval = g_file_move(trashed, dest, G_FILE_COPY_NONE,
						NULL, NULL, NULL, &error);

					g_object_unref(dest);
					g_object_unref(trashed);
				}

				g_object_unref(info);
				if (rval)
					break;
			}

			g_object_unref(enumerator);
		}

		if (NULL != error)
		{
			warn_op_error("restore from trash", orig_path, error);
			g_error_free(error);
		}

		g_object_unref(trash);
		g_free(orig_path);
		return rval;
	}

	bool CopyFile(QuiverFile src, QuiverFile dst)
	{
		bool rval = false;
		GFile* gsrc = g_file_new_for_uri(src.GetURI());
		GFile* gdst = g_file_new_for_uri(dst.GetURI());
		GError* error = NULL;
		rval = g_file_copy(gsrc, gdst, G_FILE_COPY_NONE, NULL, NULL, NULL, &error);
		if (!rval && NULL != error)
		{
			warn_op_error("copy file", dst.GetURI(), error);
			g_error_free(error);
		}
		g_object_unref(gdst);
		g_object_unref(gsrc);
		return rval;
	}

	bool MoveFile(QuiverFile src, QuiverFile dst)
	{
		bool rval = false;
		GFile* gsrc = g_file_new_for_uri(src.GetURI());
		GFile* gdst = g_file_new_for_uri(dst.GetURI());
		GError* error = NULL;
		rval = g_file_move(gsrc, gdst, G_FILE_COPY_NONE, NULL, NULL, NULL, &error);
		if (!rval && NULL != error)
		{
			warn_op_error("move file", dst.GetURI(), error);
			g_error_free(error);
		}
		g_object_unref(gdst);
		g_object_unref(gsrc);
		return rval;
	}

}
