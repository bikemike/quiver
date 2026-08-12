#ifndef FILE_FOLDER_TREE_H
#define FILE_FOLDER_TREE_H

#include <string>
#include <list>
#include <boost/shared_ptr.hpp>

#include "FolderTreeEventSource.h"

typedef struct _GtkWidget GtkWidget;
class FolderTree : public virtual FolderTreeEventSource
{

public:
	FolderTree();
	~FolderTree();

	GtkWidget* GetWidget() const;

	
	void SetSelectedFolders(std::list<std::string> &uris);

	std::list<std::string> GetSelectedFolders() const;

	class FolderTreeImpl;
	typedef boost::shared_ptr<FolderTreeImpl> FolderTreeImplPtr;

private:
	FolderTreeImplPtr m_FolderTreeImplPtr;
};

typedef boost::shared_ptr<FolderTree> FolderTreePtr;

#endif

