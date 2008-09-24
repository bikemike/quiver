#ifndef FILE_EXTERNAL_TOOLS_DLG_H
#define FILE_EXTERNAL_TOOLS_DLG_H

#include <gtk/gtk.h>

#include <boost/shared_ptr.hpp>

class ExternalToolsDlg
{
public:
	//constructor
	ExternalToolsDlg();
	//~ExternalToolsDlg();
	
	//member functions
	GtkWidget* GetWidget();
	void       Run();

	class ExternalToolsDlgPriv;
	typedef boost::shared_ptr<ExternalToolsDlgPriv> ExternalToolsDlgPrivPtr;

private:
	ExternalToolsDlgPrivPtr m_PrivPtr;
};



#endif // FILE_EXTERNAL_TOOLS_DLG_H

