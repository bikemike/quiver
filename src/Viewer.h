#ifndef FILE_VIEWER_H
#define FILE_VIEWER_H

#include <gtk/gtk.h>

#include <boost/shared_ptr.hpp>

#include "ViewerEventSource.h"

class ImageList;


class ViewerImpl;
typedef boost::shared_ptr<ViewerImpl> ViewerImplPtr;

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

	void Show();
	void Hide();
	
	void SetUIManager(GtkUIManager *ui_manager);



private:
	static void event_nav_button_clicked (GtkWidget *widget, GdkEventButton *event, void *data);
	
	ViewerImplPtr m_ViewerImplPtr;
	
};

#endif
