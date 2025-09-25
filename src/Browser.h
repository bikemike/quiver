#ifndef FILE_BROWSER_H
#define FILE_BROWSER_H

#include <boost/shared_ptr.hpp>

#include "ImageList.h"
#include "QuiverFile.h"
#include "BrowserEventSource.h"

class Statusbar;
typedef boost::shared_ptr<Statusbar> StatusbarPtr;

class Browser : public virtual BrowserEventSource
{
public:
	Browser(GtkApplication* app);
	~Browser();
	
	GtkWidget* GetWidget();
	
	ImageListPtr GetImageList();
	
	void SetImageList(ImageListPtr list);
	
	// void SetUIManager(GtkUIManager *ui_manager); // GtkUIManager is deprecated
	void SetStatusbar(StatusbarPtr statusbarPtr);
	
	void GrabFocus();
	void Show();
	void Hide();

	std::list<unsigned int> GetSelection();

	std::string GetCurrentFolderChild();

    class BrowserHistory& GetBrowserHistory();

	class BrowserImpl;
private:
	boost::shared_ptr<BrowserImpl> m_BrowserImplPtr;
};

typedef boost::shared_ptr<Browser> BrowserPtr;

#endif
