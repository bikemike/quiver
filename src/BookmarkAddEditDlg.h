#ifndef FILE_BOOKMARK_ADD_EDIT_DLG_H
#define FILE_BOOKMARK_ADD_EDIT_DLG_H

#include <gtk/gtk.h>

#include <boost/shared_ptr.hpp>

#include "Bookmarks.h"

class BookmarkAddEditDlg
{
public:
	//constructor
	BookmarkAddEditDlg();
	BookmarkAddEditDlg(Bookmark bookmark);
	
	//member functions
	GtkWidget *GetWidget() const;
	Bookmark GetBookmark() const;
	void Run();
	bool Cancelled() const;

	class BookmarkAddEditDlgPriv;
	typedef boost::shared_ptr<BookmarkAddEditDlgPriv> BookmarkAddEditDlgPrivPtr;

private:

	BookmarkAddEditDlgPrivPtr m_PrivPtr;
	
};



#endif // FILE_BOOKMARK_ADD_EDIT_DLG_H

