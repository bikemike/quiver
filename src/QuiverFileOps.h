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

	/* Apply the clipboard into the folder `dest_folder_uri` (must end in '/'):
	 * copy mode duplicates the files, cut mode moves them.  Returns the number
	 * of items transferred successfully. */
	int ClipboardTransferTo(const char* dest_folder_uri);

	/* Undo stack for "move to trash" operations.  Main-thread only.
	 * Each batch records the files trashed together so Ctrl+Z or the menu can
	 * restore them wholesale via the trash. */
	bool   UndoStackRecord(const std::list<QuiverFile>& files);
	bool   UndoStackHasItems();
	size_t UndoStackSize();
	const std::list<QuiverFile>* UndoStackPeekTop(); // most recent batch
	const std::list<QuiverFile>* UndoStackAt(size_t pos); // 0 = newest
	int    UndoStackPop();                            // restore + drop the most recent batch
	int    UndoStackRestoreAt(size_t pos);            // restore + drop a specific batch (0 = newest)
	void   UndoStackClear();

	/* UI hook: fires whenever the stack changes so the main window can refresh
	 * its undo toast and the "Recent Deletions" menu.  Registered by QuiverImpl;
	 * NULL when the UI is not up (unit tests, headless). */
	enum TrashUndoChangedReason { TRASH_UNDO_DELETED, TRASH_UNDO_RESTORED };
	typedef void (*TrashUndoChangedCallback)(TrashUndoChangedReason reason, unsigned int count, gpointer user_data);
	void SetTrashUndoChangedCallback(TrashUndoChangedCallback cb, gpointer user_data);
	void NotifyTrashUndoChanged(TrashUndoChangedReason reason, unsigned int count);

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

