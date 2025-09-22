#ifndef FILE_BOOKMARK_ADD_EDIT_DLG_H
#define FILE_BOOKMARK_ADD_EDIT_DLG_H

#include <gtk/gtk.h>

#include <boost/shared_ptr.hpp>

#include "Bookmarks.h"
#include <functional>

class BookmarkAddEditDlg
{
public:
	//constructor
	BookmarkAddEditDlg(GtkWindow* pParent);
	BookmarkAddEditDlg(GtkWindow* pParent, Bookmark bookmark);
	
	//member functions
	GtkWidget *GetWidget() const;
	Bookmark GetBookmark() const;
	void Run(std::function<void(int)> callback);


	class BookmarkAddEditDlgPriv;
	typedef boost::shared_ptr<BookmarkAddEditDlgPriv> BookmarkAddEditDlgPrivPtr;

private:

	BookmarkAddEditDlgPrivPtr m_PrivPtr;
	
};



#endif // FILE_BOOKMARK_ADD_EDIT_DLG_H
