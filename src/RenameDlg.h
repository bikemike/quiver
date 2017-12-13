#ifndef FILE_RENAME_DLG_H
#define FILE_RENAME_DLG_H

#include <gtk/gtk.h>
#include <string>
#include <boost/shared_ptr.hpp>

class RenameDlg
{
public:
	//constructor
	            RenameDlg();
	//~RenameDlg();
	
	//member functions
	GtkWidget*  GetWidget() const;
	bool        Run();

	std::string GetTemplate() const;

	std::string GetInputFolder() const;
	void        SetInputFolder(std::string folder);

	class       RenameDlgPriv;
	typedef boost::shared_ptr<RenameDlgPriv> RenameDlgPrivPtr;

private:
	RenameDlgPrivPtr m_PrivPtr;
};



#endif // FILE_RENAME_DLG_H

