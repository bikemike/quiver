#ifndef FILE_FOLDER_TREE_H
#define FILE_FOLDER_TREE_H

#include <string>
#include <list>
#include <boost/shared_ptr.hpp>

#include "FolderTreeEventSource.h"

typedef struct _GtkWidget GtkWidget;
typedef struct _GtkUIManager GtkUIManager;

class FolderTree : public virtual FolderTreeEventSource
{

public:
	FolderTree();
	~FolderTree();

	GtkWidget* GetWidget();

	void SetUIManager(GtkUIManager *ui_manager);

	std::list<std::string> GetSelectedFolders();

	class FolderTreeImpl;
	typedef boost::shared_ptr<FolderTreeImpl> FolderTreeImplPtr;

private:
	FolderTreeImplPtr m_FolderTreeImplPtr;
};


#endif

