#ifndef FILE_ORGANIZE_DLG_H
#define FILE_ORGANIZE_DLG_H

#include <gtk/gtk.h>
#include <string>
#include <boost/shared_ptr.hpp>

class OrganizeDlg
{
public:
	//constructor
	            OrganizeDlg();
	//~OrganizeDlg();
	
	//member functions
	GtkWidget*  GetWidget() const;
	bool        Run();

	std::string GetDateTemplate() const;
	std::string GetOutputFolder() const;
	std::string GetAppendedText() const;

	std::string GetInputFolder() const;
	int         GetDayExtention() const;
	bool        GetIncludeSubfolders() const;


	class       OrganizeDlgPriv;
	typedef boost::shared_ptr<OrganizeDlgPriv> OrganizeDlgPrivPtr;

private:
	OrganizeDlgPrivPtr m_PrivPtr;
};



#endif // FILE_ORGANIZE_DLG_H

