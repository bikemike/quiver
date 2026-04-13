#ifndef FILE_EXIF_VIEW_H
#define FILE_EXIF_VIEW_H

#include <boost/shared_ptr.hpp>
#include <gtk/gtk.h>
#include "QuiverFile.h"

class QuiverFile;

class ExifView
{

public:
	ExifView();
	~ExifView();

	GtkWidget *GetWidget();
	void SetQuiverFile(QuiverFile quiverFile);

	void SetUIManager(GtkUIManager *ui_manager);

	void Update();
	void Clear();

	class ExifViewImpl;
	typedef boost::shared_ptr<ExifViewImpl> ExifViewImplPtr;
private:
	ExifViewImplPtr m_ExifViewImplPtr;
};


#endif
