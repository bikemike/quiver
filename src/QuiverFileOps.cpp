#include "QuiverFileOps.h"

#include <string.h>

#include <algorithm>
#include <deque>

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

	// Pick a free descendant name for `dest` when a sibling already occupies
	// the original path (Nautilus-style "file (1).jpg" renaming).
	static GFile* free_sibling_name(GFile* desired);

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
				G_FILE_ATTRIBUTE_STANDARD_NAME "," G_FILE_ATTRIBUTE_TRASH_ORIG_PATH,
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
					GFile* dest = free_sibling_name(g_file_new_for_path(orig_path));

					GFile* parent = g_file_get_parent(dest);
					GError* dir_error = NULL;
					if (!g_file_make_directory_with_parents(parent, NULL, &dir_error) &&
						NULL != dir_error)
					{
						g_error_free(dir_error);
					}
					g_object_unref(parent);

					error = NULL;
					rval = g_file_move(trashed, dest, G_FILE_COPY_NONE,
						NULL, NULL, NULL, &error);
					if (!rval && NULL != error)
					{
						warn_op_error("restore from trash", orig_path, error);
						g_error_free(error);
						error = NULL;
					}

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

	//---------------------------------------------------------------------
	// undo stack
	//---------------------------------------------------------------------

	static std::deque<std::list<QuiverFile> > s_undoStack;
	static const size_t kMaxUndoBatches = 32;

	static TrashUndoChangedCallback s_trashUndoChangedCb = NULL;
	static gpointer s_trashUndoChangedData = NULL;

	void SetTrashUndoChangedCallback(TrashUndoChangedCallback cb, gpointer user_data)
	{
		s_trashUndoChangedCb = cb;
		s_trashUndoChangedData = user_data;
	}

	void NotifyTrashUndoChanged(TrashUndoChangedReason reason, unsigned int count)
	{
		if (NULL != s_trashUndoChangedCb)
		{
			s_trashUndoChangedCb(reason, count, s_trashUndoChangedData);
		}
	}

	bool UndoStackRecord(const std::list<QuiverFile>& files)
	{
		if (files.empty())
			return false;
		s_undoStack.push_front(files);
		while (s_undoStack.size() > kMaxUndoBatches)
		{
			s_undoStack.pop_back();
		}
		NotifyTrashUndoChanged(TRASH_UNDO_DELETED, files.size());
		return true;
	}

	bool UndoStackHasItems()
	{
		return !s_undoStack.empty();
	}

	size_t UndoStackSize()
	{
		return s_undoStack.size();
	}

	const std::list<QuiverFile>* UndoStackPeekTop()
	{
		return s_undoStack.empty() ? NULL : &s_undoStack.front();
	}

	const std::list<QuiverFile>* UndoStackAt(size_t pos)
	{
		return (pos >= s_undoStack.size()) ? NULL : &s_undoStack[pos];
	}

	int UndoStackRestoreAt(size_t pos)
	{
		if (pos >= s_undoStack.size())
			return 0;

		std::list<QuiverFile> files = s_undoStack[pos];
		s_undoStack.erase(s_undoStack.begin() + pos);

		// Restore deepest/last-trashed first so earlier restores keep their
		// original folder hierarchy intact.
		int restored = 0;
		for (std::list<QuiverFile>::reverse_iterator ritr = files.rbegin();
			files.rend() != ritr; ++ritr)
		{
			if (RestoreFromTrash(*ritr))
			{
				restored++;
			}
		}
		if (restored > 0)
		{
			NotifyTrashUndoChanged(TRASH_UNDO_RESTORED, restored);
		}
		return restored;
	}

	int UndoStackPop()
	{
		return UndoStackRestoreAt(0);
	}

	void UndoStackClear()
	{
		s_undoStack.clear();
	}

	//---------------------------------------------------------------------
	// trash-browsing helpers
	//---------------------------------------------------------------------

	bool IsTrashURI(const char* uri)
	{
		return (NULL != uri) && (0 == strncmp(uri, "trash://", 8));
	}

	// Pick a free descendant name for `dest` when a sibling already occupies
	// the original path (Nautilus-style "file (1).jpg" renaming).
	static GFile* free_sibling_name(GFile* desired)
	{
		char* path = g_file_get_path(desired);
		if (NULL == path)
		{
			g_object_ref(desired);
			return desired;
		}

		if (!g_file_test(path, G_FILE_TEST_EXISTS))
		{
			g_object_ref(desired);
			g_free(path);
			return desired;
		}

		// strip the extension and spin counters until a free name is found
		char* base = g_path_get_basename(path);
		char* dir = g_path_get_dirname(path);
		char* dot = strrchr(base, '.');
		char* stem = g_strdup(base);
		char* ext = NULL;
		if (NULL != dot && dot != base)
		{
			stem = g_strndup(base, dot - base);
			ext = g_strdup(dot);
		}

		GFile* chosen = NULL;
		for (int n = 1 ; n < 10000 ; n++)
		{
			char* name = g_strdup_printf("%s (%d)%s", stem, n, ext ? ext : "");
			char* candidate = g_build_filename(dir, name, NULL);
			if (!g_file_test(candidate, G_FILE_TEST_EXISTS))
			{
				chosen = g_file_new_for_path(candidate);
				g_free(candidate);
				g_free(name);
				break;
			}
			g_free(candidate);
			g_free(name);
		}

		g_free(ext);
		g_free(stem);
		g_free(base);
		g_free(dir);
		g_free(path);

		if (NULL == chosen)
		{
			g_object_ref(desired);
			return desired;
		}
		return chosen;
	}

	bool RestoreTrashItem(QuiverFile quiverFile)
	{
		const char* uri = quiverFile.GetURI();
		if (!IsTrashURI(uri))
			return false;

		bool rval = false;
		GFile* trashed = g_file_new_for_uri(uri);
		GError* error = NULL;
		GFileInfo* info = g_file_query_info(trashed,
			G_FILE_ATTRIBUTE_TRASH_ORIG_PATH,
			G_FILE_QUERY_INFO_NONE, NULL, &error);
		if (NULL != error)
		{
			g_error_free(error);
			error = NULL;
		}

		if (NULL != info)
		{
			const char* orig = g_file_info_get_attribute_byte_string(
				info, G_FILE_ATTRIBUTE_TRASH_ORIG_PATH);
			if (NULL != orig)
			{
				GFile* dest = free_sibling_name(g_file_new_for_path(orig));

				GFile* parent = g_file_get_parent(dest);
				GError* dir_error = NULL;
				if (!g_file_make_directory_with_parents(parent, NULL, &dir_error) &&
					NULL != dir_error)
				{
					g_error_free(dir_error);
				}
				g_object_unref(parent);

				if (g_file_move(trashed, dest, G_FILE_COPY_NONE,
					NULL, NULL, NULL, &error))
				{
					quiverFile.RemoveCachedThumbnail(-1);
					rval = true;
				}
				else if (NULL != error)
				{
					warn_op_error("restore trash item", uri, error);
					g_error_free(error);
					error = NULL;
				}
				g_object_unref(dest);
			}
			g_object_unref(info);
		}

		g_object_unref(trashed);
		return rval;
	}

	bool PermanentlyDeleteTrashItem(QuiverFile quiverFile)
	{
		const char* uri = quiverFile.GetURI();
		if (!IsTrashURI(uri))
			return false;

		bool rval = false;
		GFile* file = g_file_new_for_uri(uri);
		GError* error = NULL;
		if (g_file_delete(file, NULL, &error))
		{
			quiverFile.RemoveCachedThumbnail(-1);
			rval = true;
		}
		else if (NULL != error)
		{
			warn_op_error("permanently delete trash item", uri, error);
			g_error_free(error);
		}
		g_object_unref(file);
		return rval;
	}

	bool QuiverFolderIsEmpty(const QuiverFile& folder)
	{
		const char* uri = folder.GetURI();
		if (NULL == uri)
			return false;

		GFile* gfile = g_file_new_for_uri(uri);
		GError* error = NULL;
		bool empty = true;
		GFileEnumerator* enumerator = g_file_enumerate_children(gfile,
			"standard::name", G_FILE_QUERY_INFO_NONE, NULL, &error);
		if (NULL != enumerator)
		{
			// dotfiles count as content, so do not filter hidden here
			if (NULL != g_file_enumerator_next_file(enumerator, NULL, &error))
			{
				empty = false;
			}
			if (NULL != error)
			{
				g_error_free(error);
				error = NULL;
			}
			g_object_unref(enumerator);
		}
		g_object_unref(gfile);
		return empty;
	}

}
