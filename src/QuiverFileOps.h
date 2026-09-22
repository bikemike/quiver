#ifndef FILE_QUIVER_FILE_OPS_H
#define FILE_QUIVER_FILE_OPS_H

#include "QuiverFile.h"
#include <boost/shared_ptr.hpp>
#include <list>

namespace QuiverFileOps
{
	class StatusCallback
	{
	public:
		StatusCallback();
		virtual ~StatusCallback();
		double GetProgress ();
		void   Cancel();

		class PrivateImpl;
		typedef boost::shared_ptr<PrivateImpl> PrivateImplPtr;
	private:
		PrivateImplPtr m_PrivateImplPtr;
	};

	bool Delete(QuiverFile quiverFile);
	bool MoveToTrash(QuiverFile quiverFile);
	bool RestoreFromTrash(QuiverFile quiverFile);
	bool CopyFile(QuiverFile src, QuiverFile dst);
	bool MoveFile(QuiverFile src, QuiverFile dst);

	/* Internal file clipboard for cut/copy/paste between folders.  Holding the
	 * item URIs lets the context-menu Paste / Paste-Into-Folder actions apply
	 * the same clipboard to any target folder.  Main-thread only. */
	void     ClipboardSet(const std::list<std::string>& uris, bool bCut);
	void     ClipboardClear();
	bool     ClipboardHasItems();
	bool     ClipboardIsCut();
	const std::list<std::string>* ClipboardGetUris();

	/* Conflict resolution for paste/drop: called once per existing-destination
	 * collision.  Return PASTE_SKIP to leave the existing file alone or
	 * PASTE_OVERWRITE to replace it. */
	enum PasteConflictAction { PASTE_SKIP = 0, PASTE_OVERWRITE = 1 };
	typedef PasteConflictAction (*ClipboardConflictFn)(const gchar *src_uri,
		const gchar *dest_uri, gpointer user_data);

	/* Transfer the given URIs into the folder `dest_folder_uri` (must end in
	 * '/'): copy mode duplicates the files, cut mode moves them.  Files that
	 * already exist under the same name are resolved through `conflict_fn`
	 * (NULL => always skip, never overwrite).  Returns the number of items
	 * transferred successfully.  When `moved_out` is non-NULL, the URIs
	 * actually moved (cut mode) are appended to it so the caller can drop
	 * them from a cut clipboard. */
	int TransferFiles(const std::list<std::string>& uris, bool bCut,
		const char* dest_folder_uri,
		ClipboardConflictFn conflict_fn, gpointer conflict_data,
		std::list<std::string>* moved_out = NULL);

	/* Drop the given URIs from the internal clipboard.  Used after a cut is
	 * applied so the source items are not transferred a second time when the
	 * user pastes the (no longer valid) selection again. */
	void ClipboardRemoveURIs(const std::list<std::string>& uris);

	/* Apply the internal clipboard into the folder `dest_folder_uri`: copy
	 * mode duplicates the files, cut mode moves them.  Conflicts are skipped
	 * (never overwritten) and moved cut items are pruned from the clipboard.
	 * Returns the number of items transferred successfully. */
	int ClipboardTransferTo(const char* dest_folder_uri);

	/* Unified Undo stack for operations (delete, move, copy, rotate). Main-thread only. */
	enum UndoType
	{
		UNDO_TYPE_DELETE = 0,
		UNDO_TYPE_MOVE,
		UNDO_TYPE_COPY,
		UNDO_TYPE_ROTATE,
		UNDO_TYPE_NEW_FOLDER
	};

	struct UndoFilePair
	{
		std::string src_uri;
		std::string dst_uri;
	};

	struct UndoEntry
	{
		UndoType type;
		std::string description;
		std::list<QuiverFile> trashed_files;
		std::vector<UndoFilePair> file_pairs;
		std::vector<std::string> copied_dsts;
		std::string rotate_uri;
		int rotate_direction; // +1 = CW, -1 = CCW
		std::string new_folder_uri;
	};

	bool   UndoStackRecord(const std::list<QuiverFile>& files); // backward-compatible alias for delete
	bool   UndoStackRecordDelete(const std::list<QuiverFile>& files);
	bool   UndoStackRecordMove(const std::vector<UndoFilePair>& pairs);
	bool   UndoStackRecordCopy(const std::vector<std::string>& copied_dsts);
	bool   UndoStackRecordRotate(const std::string& uri, int direction);
	void   UndoStackDropRotate(const std::string& uri);
	bool   UndoStackRecordNewFolder(const std::string& folder_uri);

	bool   UndoStackHasItems();
	size_t UndoStackSize();
	UndoType UndoStackTopType();
	const UndoEntry* UndoStackEntryAt(size_t pos);
	const std::list<QuiverFile>* UndoStackPeekTop(); // most recent batch (if delete)
	const std::list<QuiverFile>* UndoStackAt(size_t pos); // 0 = newest (if delete)
	int    UndoStackPop();                            // restore + drop the most recent batch
	int    UndoStackRestoreAt(size_t pos);            // restore + drop a specific batch (0 = newest)
	void   UndoStackClear();

	/* UI hook: fires whenever the stack changes so the main window can refresh
	 * its undo toast and menu.  Registered by QuiverImpl;
	 * NULL when the UI is not up (unit tests, headless). */
	enum TrashUndoChangedReason {
		TRASH_UNDO_DELETED,
		TRASH_UNDO_RESTORED,
		UNDO_RECORDED_MOVE,
		UNDO_RECORDED_COPY,
		UNDO_RECORDED_ROTATE,
		UNDO_RECORDED_NEW_FOLDER,
		UNDO_RESTORED_OP
	};
	typedef void (*TrashUndoChangedCallback)(TrashUndoChangedReason reason, unsigned int count, gpointer user_data);
	void SetTrashUndoChangedCallback(TrashUndoChangedCallback cb, gpointer user_data);
	void NotifyTrashUndoChanged(TrashUndoChangedReason reason, unsigned int count);

	/* Rotate callback for viewer undo */
	typedef void (*RotateUndoCallback)(const char* uri, int undo_direction, gpointer user_data);
	void SetRotateUndoCallback(RotateUndoCallback cb, gpointer user_data);
	void NotifyRotateUndo(const char* uri, int undo_direction);

	/* New folder undo callback */
	typedef void (*NewFolderUndoCallback)(const char* folder_uri, gpointer user_data);
	void SetNewFolderUndoCallback(NewFolderUndoCallback cb, gpointer user_data);
	void NotifyNewFolderUndo(const char* folder_uri);

	/* UI hook: fires with the destination of the most recently restored item
	 * (a file:/// URI).  Emitted for single-item restores so the toast can
	 * offer a "take me there" shortcut. */
	typedef void (*TrashRestoredChangedCallback)(const char* restored_uri, gpointer user_data);
	void SetTrashRestoredChangedCallback(TrashRestoredChangedCallback cb, gpointer user_data);
	void NotifyTrashRestored(const char* restored_uri);

	/* Trash-browsing helpers */
	bool IsTrashURI(const char* uri);
	char* GetTrashItemOrigPath(const char* uri);            // g_strdup'd trash::orig-path, or NULL
	bool RestoreTrashItem(QuiverFile quiverFile);          // restore a trash:/// item to its origin
	bool PermanentlyDeleteTrashItem(QuiverFile quiverFile); // drop a trash:/// item for good
	bool QuiverFolderIsEmpty(const QuiverFile& folder);     // no children at all
}

#endif

