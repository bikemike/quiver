#ifndef FILE_FOLDER_TREE_H
#define FILE_FOLDER_TREE_H

#include <string>
#include <list>
#include <set>
#include <boost/shared_ptr.hpp>

#include "FolderTreeEventSource.h"

typedef struct _GtkWidget GtkWidget;
class FolderTree : public virtual FolderTreeEventSource
{

public:
	FolderTree();
	~FolderTree();

	GtkWidget* GetWidget() const;
	GtkWidget* GetTreeWidget() const;
	GtkWidget* GetShortcutsWidget() const;
	GtkWidget* GetBookmarksWidget() const;

	void SetShortcutsExpanded(bool expanded);
	bool GetShortcutsExpanded() const;
	void SetBookmarksExpanded(bool expanded);
	bool GetBookmarksExpanded() const;
	void SetFoldersExpanded(bool expanded);
	bool GetFoldersExpanded() const;

	void SetSelectedFolders(std::list<std::string> &uris);
	std::list<std::string> GetSelectedFolders() const;
	/* True when any checked item of the combined selection requires its
	 * folders to be loaded recursively (currently: a checked bookmark whose
	 * "include subfolders" option is on). */
	bool GetSelectedFoldersRecursive() const;
	/* The subset of selected folder URIs that should be loaded recursively
	 * (e.g. from checked bookmarks with "include subfolders" enabled). */
	std::set<std::string> GetSelectedRecursiveFolders() const;
	/* URIs to bookmark when the user triggers "Add Bookmark" for clicked_uri:
	 * the whole highlighted row SELECTION (folder tree / shortcuts /
	 * bookmarks lists) when the clicked folder is part of it, otherwise just
	 * the clicked folder.  This intentionally follows the selection, not the
	 * checkbox state that drives the combined picture list. */
	std::list<std::string> GetAddBookmarkURIs(const std::string& clicked_uri) const;
	void AddChildFolder(const char *parent_uri, const char *child_uri, const char *folder_name);
	void RemoveFolder(const char *folder_uri);

	class FolderTreeImpl;
	typedef boost::shared_ptr<FolderTreeImpl> FolderTreeImplPtr;

private:
	FolderTreeImplPtr m_FolderTreeImplPtr;
};

typedef boost::shared_ptr<FolderTree> FolderTreePtr;

#endif

