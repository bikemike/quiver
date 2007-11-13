#ifndef FILE_BOOKMARKS_DLG_H
#define FILE_BOOKMARKS_DLG_H

#include <gtk/gtk.h>

#include <boost/shared_ptr.hpp>

class BookmarksDlg
{
public:
	//constructor
	BookmarksDlg();
	//~BookmarksDlg();
	
	//member functions
	GtkWidget *GetWidget();
	void Run();

	class BookmarksDlgPriv;
	typedef boost::shared_ptr<BookmarksDlgPriv> BookmarksDlgPrivPtr;

private:

	BookmarksDlgPrivPtr m_PrivPtr;
	
};



#endif // FILE_BOOKMARKS_DLG_H

