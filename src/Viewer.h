#ifndef FILE_VIEWER_H
#define FILE_VIEWER_H

#include <gtk/gtk.h>

#include <boost/shared_ptr.hpp>

#include "ViewerEventSource.h"

class ImageList;

class Statusbar;
typedef boost::shared_ptr<Statusbar> StatusbarPtr;

class Viewer : public virtual ViewerEventSource
{
public:
	//constructor
	Viewer();
	~Viewer();
	
	//member functions
	GtkWidget *GetWidget();

	void SetImageList(ImageList imgList);
	int GetCurrentOrientation();

	void SlideShowStart();
	void SlideShowStop();

	void Show();
	void Hide();
	
	void SetUIManager(GtkUIManager *ui_manager);
	void SetStatusbar(StatusbarPtr statusbarPtr);

	double GetMagnification() const;

	class ViewerImpl;
	typedef boost::shared_ptr<ViewerImpl> ViewerImplPtr;

private:

	ViewerImplPtr m_ViewerImplPtr;
	
};

#endif
