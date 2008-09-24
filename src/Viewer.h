#ifndef FILE_VIEWER_H
#define FILE_VIEWER_H

#include <gtk/gtk.h>

#include <boost/shared_ptr.hpp>

#include "ViewerEventSource.h"
#include "ImageList.h"

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

	void SetImageList(ImageListPtr imgList);
	int GetCurrentOrientation();

	void SlideShowStart();
	void SlideShowStop();

	// returns true if the view mode was reset, false if it did not need to be reset
	bool ResetViewMode();

	void GrabFocus();
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

typedef boost::shared_ptr<Viewer> ViewerPtr;

#endif
