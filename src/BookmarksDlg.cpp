#include "config.h"
#include "BookmarksDlg.h"

class BookmarksDlg::BookmarksDlgPriv
{
public:
    BookmarksDlgPriv(GtkWindow* pParent) {}
    ~BookmarksDlgPriv() {}
};

BookmarksDlg::BookmarksDlg(GtkWindow* pParent) : m_PrivPtr(new BookmarksDlgPriv(pParent))
{
}

void BookmarksDlg::Run()
{
}

GtkWidget* BookmarksDlg::GetWidget()
{
    return NULL;
}
