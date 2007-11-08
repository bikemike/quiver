#ifndef QUERY_H
#define QUERY_H

#include <boost/shared_ptr.hpp>

#include "QuiverFile.h"
#include "QueryEventSource.h"

class Statusbar;
typedef boost::shared_ptr<Statusbar> StatusbarPtr;

class ImageList;

class Query : public virtual QueryEventSource
{
public:
	Query();
	~Query();
	
	GtkWidget* GetWidget();
	
	ImageList GetImageList();
	
	void SetImageList(std::list<std::string> *file_list);
	
	void SetUIManager(GtkUIManager *ui_manager);
	void SetStatusbar(StatusbarPtr statusbarPtr);
	
	void GrabFocus();
	void Show();
	void Hide();

	std::list<unsigned int> GetSelection();

	class QueryImpl;
private:
	boost::shared_ptr<QueryImpl> m_QueryImplPtr;
};


#endif
