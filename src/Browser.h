#ifndef FILE_BROWSER_H
#define FILE_BROWSER_H

#include <boost/shared_ptr.hpp>

#include "QuiverFile.h"
#include "BrowserEventSource.h"

class ImageList;

class Browser : public virtual BrowserEventSource
{
public:
	Browser();
	~Browser();
	
	GtkWidget* GetWidget();
	
	ImageList GetImageList();
	
	void SetImageList(ImageList list);
	
	void SetUIManager(GtkUIManager *ui_manager);
	
	void Show();
	void Hide();
	std::list<int> GetSelection();

	class BrowserImpl;
private:
	boost::shared_ptr<BrowserImpl> m_BrowserImplPtr;
};


#endif
