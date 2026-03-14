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

	std::string GetFolderTemplate() const;
	std::string GetFileTemplate() const;
	std::string GetOutputFolder() const;
	std::string GetAppendedText() const;

	std::string GetInputFolder() const;
	void        SetInputFolder(std::string dir);
	int         GetDayExtention() const;
	bool        GetIncludeSubfolders() const;
	bool        GetRenameFiles() const;


	class       OrganizeDlgPriv;
	typedef boost::shared_ptr<OrganizeDlgPriv> OrganizeDlgPrivPtr;

private:
	OrganizeDlgPrivPtr m_PrivPtr;
};



#endif // FILE_ORGANIZE_DLG_H

