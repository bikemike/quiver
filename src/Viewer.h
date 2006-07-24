#ifndef FILE_VIEWER_H
#define FILE_VIEWER_H

#include <gtk/gtk.h>
//#include <gdk/gdkx.h>

#include <fstream>
#include <iostream>
#include <string>

#include <boost/shared_ptr.hpp>

#include "PixbufLoaderObserver.h"
#include "NavigationControl.h"
#include "QuiverFile.h"
#include "ImageList.h"

class ViewerImpl;
typedef boost::shared_ptr<ViewerImpl> ViewerImplPtr;

class Viewer
{
public:
	//constructor
	Viewer();
	~Viewer();
	
	//member functions
	GtkWidget *GetWidget();

	void SetImageList(ImageList imgList);

	void EventNavButtonClicked (GtkWidget *widget, GdkEventButton *event, void *data);
	
	int GetCurrentOrientation();

	void Show();
	void Hide();
	
	void SetUIManager(GtkUIManager *ui_manager);



private:
	static void event_nav_button_clicked (GtkWidget *widget, GdkEventButton *event, void *data);
	
	ViewerImplPtr m_ViewerImplPtr;
	
};

#endif
