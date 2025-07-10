#ifndef FILE_FOLDER_TREE_H
#define FILE_FOLDER_TREE_H

#include <string>
#include <list>
#include <boost/shared_ptr.hpp>

#include "FolderTreeEventSource.h"

#include <gtk/gtk.h> // Add GTK4 includes

// typedef struct _GtkWidget GtkWidget; // GtkWidget is now defined in gtk/gtk.h
// typedef struct _GtkUIManager GtkUIManager; // GtkUIManager is deprecated and removed

class FolderTree : public virtual FolderTreeEventSource
{

public:
	FolderTree();
	~FolderTree();

	GtkWidget* GetWidget() const;

	// void SetUIManager(GtkUIManager *ui_manager); // GtkUIManager is deprecated
	
	void SetSelectedFolders(std::list<std::string> &uris);

	std::list<std::string> GetSelectedFolders() const;

	class FolderTreeImpl;
	typedef boost::shared_ptr<FolderTreeImpl> FolderTreeImplPtr;

private:
	FolderTreeImplPtr m_FolderTreeImplPtr;
};

typedef boost::shared_ptr<FolderTree> FolderTreePtr;

#endif

