#include "QuiverFileOps.h"

#include <string.h>

#include <algorithm>
#include <deque>

#include <gio/gio.h>
#include <glib/gstdio.h>

namespace QuiverFileOps
{
	static bool dest_is_inside_or_equal(const char* src_uri, const char* dest_dir_uri)
	{
		if (!src_uri || !dest_dir_uri)
			return false;
		GFile *sf = g_file_new_for_uri(src_uri);
		GFile *df = g_file_new_for_uri(dest_dir_uri);
		bool result = g_file_equal(sf, df) || g_file_has_prefix(df, sf);
		g_object_unref(sf);
		g_object_unref(df);
		return result;
	}

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

	/* Destination of the most recently completed single-item restore (a
	 * file:/// URI).  Only meaningful right after a successful restore; used
	 * to notify the UI once per restore (see UndoStackRestoreAt()). */
	static std::string s_strLastRestoredURI;

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
					else if (rval)
					{
						s_strLastRestoredURI.clear();
						char* dest_uri = g_file_get_uri(dest);
						if (NULL != dest_uri)
						{
							s_strLastRestoredURI = dest_uri;
							g_free(dest_uri);
						}
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

	/* May run from the main thread only (GUI feedback / dialogs). */
	static bool file_copy_uri(const char *src_uri, const char *dst_uri, bool bOverwrite)
	{
		bool rval = false;
		GFile* gsrc = g_file_new_for_uri(src_uri);
		GFile* gdst = g_file_new_for_uri(dst_uri);
		GError* error = NULL;
		GFileCopyFlags flags = bOverwrite ? G_FILE_COPY_OVERWRITE : G_FILE_COPY_NONE;
		rval = g_file_copy(gsrc, gdst, (GFileCopyFlags)flags, NULL, NULL, NULL, &error);
		if (!rval && NULL != error)
		{
			warn_op_error("copy file", dst_uri, error);
			g_error_free(error);
		}
		g_object_unref(gdst);
		g_object_unref(gsrc);
		return rval;
	}

	static bool file_move_uri(const char *src_uri, const char *dst_uri, bool bOverwrite)
	{
		bool rval = false;
		GFile* gsrc = g_file_new_for_uri(src_uri);
		GFile* gdst = g_file_new_for_uri(dst_uri);
		GError* error = NULL;
		GFileCopyFlags flags = bOverwrite ? G_FILE_COPY_OVERWRITE : G_FILE_COPY_NONE;
		rval = g_file_move(gsrc, gdst, (GFileCopyFlags)flags, NULL, NULL, NULL, &error);
		if (!rval && NULL != error)
		{
			warn_op_error("move file", dst_uri, error);
			g_error_free(error);
		}
		g_object_unref(gdst);
		g_object_unref(gsrc);
		return rval;
	}

	bool CopyFile(QuiverFile src, QuiverFile dst)
	{
		return file_copy_uri(src.GetURI(), dst.GetURI(), false);
	}

	bool MoveFile(QuiverFile src, QuiverFile dst)
	{
		return file_move_uri(src.GetURI(), dst.GetURI(), false);
	}

	//---------------------------------------------------------------------
	// internal file clipboard (cut / copy / paste)
	//---------------------------------------------------------------------

	static std::list<std::string> s_clipboardUris;
	static bool s_clipboardCut = false;

	void ClipboardSet(const std::list<std::string>& uris, bool bCut)
	{
		s_clipboardUris = uris;
		s_clipboardCut = bCut;
	}

	void ClipboardClear()
	{
		s_clipboardUris.clear();
		s_clipboardCut = false;
	}

	bool ClipboardHasItems()
	{
		return !s_clipboardUris.empty();
	}

	bool ClipboardIsCut()
	{
		return s_clipboardCut;
	}

	const std::list<std::string>* ClipboardGetUris()
	{
		return &s_clipboardUris;
	}

	/* Transfer the given URIs into `dest_folder_uri`.  See the header for the
	 * conflict semantics. */
	int TransferFiles(const std::list<std::string>& uris, bool bCut,
		const char* dest_folder_uri,
		ClipboardConflictFn conflict_fn, gpointer conflict_data,
		std::list<std::string>* moved_out)
	{
		if (uris.empty() || dest_folder_uri == NULL || '\0' == dest_folder_uri[0])
			return 0;

		/* folder_uri is a dir URI; make sure it ends with '/' for concat */
		std::string prefix = dest_folder_uri;
		if (prefix.empty() || prefix[prefix.size() - 1] != '/')
			prefix += "/";

		int count = 0;
		std::vector<UndoFilePair> moved_pairs;
		std::vector<std::string> copied_dsts;

		for (std::list<std::string>::const_iterator it = uris.begin();
			uris.end() != it; ++it)
		{
			if (it->empty())
				continue;

			/* Don't transfer a folder into itself or into its own subtree */
			if (dest_is_inside_or_equal(it->c_str(), dest_folder_uri))
				continue;

			/* A cut of a file into its own directory is a no-op: already there */
			if (bCut)
			{
				gchar *src_dir = g_path_get_dirname(it->c_str());
				gchar *dst_path = g_strdup(dest_folder_uri);
				size_t dlen = dst_path ? strlen(dst_path) : 0;
				if (dlen > 1 && dst_path[dlen - 1] == '/')
					dst_path[dlen - 1] = '\0';
				bool same_dir = (NULL != src_dir && NULL != dst_path &&
					0 == g_strcmp0(src_dir, dst_path));
				g_free(dst_path);
				g_free(src_dir);
				if (same_dir)
				{
					if (moved_out != NULL)
						moved_out->push_back(*it);
					continue;
				}
			}

			/* destination name keeps the (URI-escaped) source basename. */
			gchar *base = g_path_get_basename(it->c_str());
			if (NULL == base || '\0' == base[0])
			{
				g_free(base);
				continue;
			}
			const std::string dst_uri = prefix + base;
			g_free(base);

			bool bOverwrite = false;
			{
				GFile *dstf = g_file_new_for_uri(dst_uri.c_str());
				gboolean exists = g_file_query_exists(dstf, NULL);
				g_object_unref(dstf);
				if (exists)
				{
					PasteConflictAction action = PASTE_SKIP;
					if (conflict_fn != NULL)
						action = conflict_fn(it->c_str(), dst_uri.c_str(), conflict_data);
					if (action != PASTE_OVERWRITE)
						continue; /* skip: leave the existing file alone */
					bOverwrite = true;
				}
			}

			bool ok = bCut
				? file_move_uri(it->c_str(), dst_uri.c_str(), bOverwrite)
				: file_copy_uri(it->c_str(), dst_uri.c_str(), bOverwrite);
			if (ok)
			{
				count++;
				if (bCut)
				{
					UndoFilePair pair;
					pair.src_uri = *it;
					pair.dst_uri = dst_uri;
					moved_pairs.push_back(pair);
					if (moved_out != NULL)
						moved_out->push_back(*it);
				}
				else
				{
					copied_dsts.push_back(dst_uri);
				}
			}
		}

		if (count > 0)
		{
			if (bCut)
				UndoStackRecordMove(moved_pairs);
			else
				UndoStackRecordCopy(copied_dsts);
		}

		return count;
	}

	void ClipboardRemoveURIs(const std::list<std::string>& uris)
	{
		if (uris.empty())
			return;
		std::list<std::string> remaining;
		for (std::list<std::string>::const_iterator it = s_clipboardUris.begin();
			it != s_clipboardUris.end(); ++it)
		{
			bool gone = false;
			for (std::list<std::string>::const_iterator m = uris.begin(); m != uris.end(); ++m)
				if (*m == *it) { gone = true; break; }
			if (!gone)
				remaining.push_back(*it);
		}
		s_clipboardUris.swap(remaining);
	}

	int ClipboardTransferTo(const char* dest_folder_uri)
	{
		if (s_clipboardUris.empty())
			return 0;

		std::list<std::string> moved;
		int count = TransferFiles(s_clipboardUris, s_clipboardCut,
			dest_folder_uri, NULL, NULL, &moved);
		if (s_clipboardCut)
			ClipboardRemoveURIs(moved);
		return count;
	}

	//---------------------------------------------------------------------
	// undo stack
	//---------------------------------------------------------------------

	static std::deque<UndoEntry> s_undoStack;
	static const size_t kMaxUndoBatches = 32;

	static TrashUndoChangedCallback s_trashUndoChangedCb = NULL;
	static gpointer s_trashUndoChangedData = NULL;

	static TrashRestoredChangedCallback s_trashRestoredChangedCb = NULL;
	static gpointer s_trashRestoredChangedData = NULL;

	static RotateUndoCallback s_rotateUndoCb = NULL;
	static gpointer s_rotateUndoData = NULL;

	static NewFolderUndoCallback s_newFolderUndoCb = NULL;
	static gpointer s_newFolderUndoData = NULL;

	void SetTrashUndoChangedCallback(TrashUndoChangedCallback cb, gpointer user_data)
	{
		s_trashUndoChangedCb = cb;
		s_trashUndoChangedData = user_data;
	}

	void SetTrashRestoredChangedCallback(TrashRestoredChangedCallback cb, gpointer user_data)
	{
		s_trashRestoredChangedCb = cb;
		s_trashRestoredChangedData = user_data;
	}

	void SetRotateUndoCallback(RotateUndoCallback cb, gpointer user_data)
	{
		s_rotateUndoCb = cb;
		s_rotateUndoData = user_data;
	}

	void SetNewFolderUndoCallback(NewFolderUndoCallback cb, gpointer user_data)
	{
		s_newFolderUndoCb = cb;
		s_newFolderUndoData = user_data;
	}

	void NotifyTrashUndoChanged(TrashUndoChangedReason reason, unsigned int count)
	{
		if (NULL != s_trashUndoChangedCb)
		{
			s_trashUndoChangedCb(reason, count, s_trashUndoChangedData);
		}
	}

	void NotifyTrashRestored(const char* restored_uri)
	{
		if (NULL != restored_uri && 0 != restored_uri[0] && NULL != s_trashRestoredChangedCb)
		{
			s_trashRestoredChangedCb(restored_uri, s_trashRestoredChangedData);
		}
	}

	void NotifyRotateUndo(const char* uri, int undo_direction)
	{
		if (NULL != s_rotateUndoCb)
		{
			s_rotateUndoCb(uri, undo_direction, s_rotateUndoData);
		}
	}

	void NotifyNewFolderUndo(const char* folder_uri)
	{
		if (NULL != s_newFolderUndoCb)
		{
			s_newFolderUndoCb(folder_uri, s_newFolderUndoData);
		}
	}

	bool UndoStackRecord(const std::list<QuiverFile>& files)
	{
		return UndoStackRecordDelete(files);
	}

	bool UndoStackRecordDelete(const std::list<QuiverFile>& files)
	{
		if (files.empty())
			return false;
		UndoEntry entry;
		entry.type = UNDO_TYPE_DELETE;
		entry.trashed_files = files;
		s_undoStack.push_front(entry);
		while (s_undoStack.size() > kMaxUndoBatches)
		{
			s_undoStack.pop_back();
		}
		NotifyTrashUndoChanged(TRASH_UNDO_DELETED, files.size());
		return true;
	}

	bool UndoStackRecordMove(const std::vector<UndoFilePair>& pairs)
	{
		if (pairs.empty())
			return false;
		UndoEntry entry;
		entry.type = UNDO_TYPE_MOVE;
		entry.file_pairs = pairs;
		s_undoStack.push_front(entry);
		while (s_undoStack.size() > kMaxUndoBatches)
		{
			s_undoStack.pop_back();
		}
		NotifyTrashUndoChanged(UNDO_RECORDED_MOVE, pairs.size());
		return true;
	}

	bool UndoStackRecordCopy(const std::vector<std::string>& copied_dsts)
	{
		if (copied_dsts.empty())
			return false;
		UndoEntry entry;
		entry.type = UNDO_TYPE_COPY;
		entry.copied_dsts = copied_dsts;
		s_undoStack.push_front(entry);
		while (s_undoStack.size() > kMaxUndoBatches)
		{
			s_undoStack.pop_back();
		}
		NotifyTrashUndoChanged(UNDO_RECORDED_COPY, copied_dsts.size());
		return true;
	}

	bool UndoStackRecordRotate(const std::string& uri, int direction)
	{
		if (uri.empty())
			return false;
		UndoEntry entry;
		entry.type = UNDO_TYPE_ROTATE;
		entry.rotate_uri = uri;
		entry.rotate_direction = direction;
		s_undoStack.push_front(entry);
		while (s_undoStack.size() > kMaxUndoBatches)
		{
			s_undoStack.pop_back();
		}
		NotifyTrashUndoChanged(UNDO_RECORDED_ROTATE, 1);
		return true;
	}

	bool UndoStackRecordNewFolder(const std::string& folder_uri)
	{
		if (folder_uri.empty())
			return false;
		UndoEntry entry;
		entry.type = UNDO_TYPE_NEW_FOLDER;
		entry.new_folder_uri = folder_uri;
		s_undoStack.push_front(entry);
		while (s_undoStack.size() > kMaxUndoBatches)
		{
			s_undoStack.pop_back();
		}
		NotifyTrashUndoChanged(UNDO_RECORDED_NEW_FOLDER, 1);
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

	UndoType UndoStackTopType()
	{
		return s_undoStack.empty() ? UNDO_TYPE_DELETE : s_undoStack.front().type;
	}

	const UndoEntry* UndoStackEntryAt(size_t pos)
	{
		return (pos >= s_undoStack.size()) ? NULL : &s_undoStack[pos];
	}

	const std::list<QuiverFile>* UndoStackPeekTop()
	{
		return (s_undoStack.empty() || s_undoStack.front().type != UNDO_TYPE_DELETE)
			? NULL : &s_undoStack.front().trashed_files;
	}

	const std::list<QuiverFile>* UndoStackAt(size_t pos)
	{
		return (pos >= s_undoStack.size() || s_undoStack[pos].type != UNDO_TYPE_DELETE)
			? NULL : &s_undoStack[pos].trashed_files;
	}

	int UndoStackRestoreAt(size_t pos)
	{
		if (pos >= s_undoStack.size())
			return 0;

		UndoEntry entry = s_undoStack[pos];
		s_undoStack.erase(s_undoStack.begin() + pos);

		if (entry.type == UNDO_TYPE_DELETE)
		{
			int restored = 0;
			for (std::list<QuiverFile>::reverse_iterator ritr = entry.trashed_files.rbegin();
				entry.trashed_files.rend() != ritr; ++ritr)
			{
				if (RestoreFromTrash(*ritr))
				{
					restored++;
				}
			}
			if (restored == 1)
			{
				NotifyTrashRestored(s_strLastRestoredURI.c_str());
			}
			else
			{
				NotifyTrashUndoChanged(TRASH_UNDO_RESTORED, restored);
			}
			return restored;
		}
		else if (entry.type == UNDO_TYPE_MOVE)
		{
			int restored = 0;
			std::string last_src;
			for (std::vector<UndoFilePair>::reverse_iterator ritr = entry.file_pairs.rbegin();
				entry.file_pairs.rend() != ritr; ++ritr)
			{
				if (file_move_uri(ritr->dst_uri.c_str(), ritr->src_uri.c_str(), false))
				{
					restored++;
					last_src = ritr->src_uri;
				}
			}
			if (restored == 1 && !last_src.empty())
			{
				NotifyTrashRestored(last_src.c_str());
			}
			else
			{
				NotifyTrashUndoChanged(UNDO_RESTORED_OP, restored);
			}
			return restored;
		}
		else if (entry.type == UNDO_TYPE_COPY)
		{
			int removed = 0;
			for (size_t i = 0; i < entry.copied_dsts.size(); ++i)
			{
				QuiverFile f(entry.copied_dsts[i].c_str());
				if (MoveToTrash(f))
				{
					removed++;
				}
				else
				{
					char* path = g_filename_from_uri(entry.copied_dsts[i].c_str(), NULL, NULL);
					if (path)
					{
						if (g_remove(path) == 0)
							removed++;
						g_free(path);
					}
				}
			}
			NotifyTrashUndoChanged(UNDO_RESTORED_OP, removed);
			return removed;
		}
		else if (entry.type == UNDO_TYPE_ROTATE)
		{
			NotifyRotateUndo(entry.rotate_uri.c_str(), -entry.rotate_direction);
			NotifyTrashUndoChanged(UNDO_RESTORED_OP, 1);
			return 1;
		}
		else if (entry.type == UNDO_TYPE_NEW_FOLDER)
		{
			QuiverFile f(entry.new_folder_uri.c_str());
			bool removed = MoveToTrash(f);
			if (!removed)
			{
				char* path = g_filename_from_uri(entry.new_folder_uri.c_str(), NULL, NULL);
				if (path)
				{
					if (g_rmdir(path) == 0 || g_remove(path) == 0)
						removed = true;
					g_free(path);
				}
			}
			NotifyNewFolderUndo(entry.new_folder_uri.c_str());
			NotifyTrashUndoChanged(UNDO_RESTORED_OP, removed ? 1 : 0);
			return removed ? 1 : 0;
		}
		return 0;
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

	char* GetTrashItemOrigPath(const char* uri)
	{
		if (!IsTrashURI(uri))
			return NULL;

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
		g_object_unref(trashed);

		if (NULL == info)
			return NULL;

		char* orig = g_strdup(g_file_info_get_attribute_byte_string(
			info, G_FILE_ATTRIBUTE_TRASH_ORIG_PATH));
		g_object_unref(info);
		return orig;
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

					/* Tell the UI where the item landed so the toast can offer
					 * "take me there". */
					s_strLastRestoredURI.clear();
					char* dest_uri = g_file_get_uri(dest);
					if (NULL != dest_uri)
					{
						s_strLastRestoredURI = dest_uri;
						g_free(dest_uri);
					}
					NotifyTrashRestored(s_strLastRestoredURI.c_str());
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
