#ifndef FILE_DONATE_DLG_H
#define FILE_DONATE_DLG_H

#include <gtk/gtk.h>

#include <boost/shared_ptr.hpp>

class DonateDlg
{
public:
	//constructor
	DonateDlg();
	
	//member functions
	GtkWidget *GetWidget() const;
	void Run();

	class DonateDlgPriv;
	typedef boost::shared_ptr<DonateDlgPriv> DonateDlgPrivPtr;

private:

	DonateDlgPrivPtr m_PrivPtr;
	
};



#endif // FILE_DONATE_DLG_H

