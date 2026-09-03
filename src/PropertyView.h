#ifndef FILE_PROPERTY_VIEW_H
#define FILE_PROPERTY_VIEW_H

#include <boost/shared_ptr.hpp>

#include "QuiverFile.h"

class QuiverFile;

class PropertyView
{

public:
	PropertyView();
	~PropertyView();

	GtkWidget *GetWidget();
	void SetQuiverFile(QuiverFile quiverFile);


	class PropertyViewImpl;
	typedef boost::shared_ptr<PropertyViewImpl> PropertyViewImplPtr;
private:
	PropertyViewImplPtr m_PropertyViewImplPtr;
};


#endif
