#ifndef FILE_BROWSER_H
#define FILE_BROWSER_H

#include <boost/shared_ptr.hpp>

#include "ImageList.h"
#include "QuiverFile.h"
#include "BrowserEventSource.h"

class Statusbar;
typedef boost::shared_ptr<Statusbar> StatusbarPtr;

class FolderTree;
typedef boost::shared_ptr<FolderTree> FolderTreePtr;

class Browser : public virtual BrowserEventSource
{
public:
	Browser();
	~Browser();
	
	GtkWidget* GetWidget();
	
	ImageListPtr GetImageList();
	
	void SetImageList(ImageListPtr list);
	
	void RegisterActions();
	void SetToolbar(GtkWidget* pToolbar);
	void SetStatusbar(StatusbarPtr statusbar);
	
	void GrabFocus();
	
	void Show();
	void Hide();

	std::list<unsigned int> GetSelection();

	std::string GetCurrentFolderChild();

	FolderTreePtr GetFolderTree();

	/* Overlay wrapping the icon view; floating chrome (e.g. the undo-delete
	 * toast) can be parented on top of the image grid. */
	GtkWidget *GetIconViewOverlay();

	class BrowserImpl;
private:
	boost::shared_ptr<BrowserImpl> m_BrowserImplPtr;
};

typedef boost::shared_ptr<Browser> BrowserPtr;

#endif
