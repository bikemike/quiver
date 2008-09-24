#ifndef FILE_EXTERNAL_TOOL_ADD_EDIT_DLG_H
#define FILE_EXTERNAL_TOOL_ADD_EDIT_DLG_H

#include <gtk/gtk.h>

#include <boost/shared_ptr.hpp>

#include "ExternalTools.h"

class ExternalToolAddEditDlg
{
public:
	//constructor
	ExternalToolAddEditDlg();
	ExternalToolAddEditDlg(ExternalTool bookmark);
	
	//member functions
	GtkWidget*   GetWidget() const;
	ExternalTool GetExternalTool() const;
	void         Run();
	bool         Cancelled() const;


	class ExternalToolAddEditDlgPriv;
	typedef boost::shared_ptr<ExternalToolAddEditDlgPriv> ExternalToolAddEditDlgPrivPtr;
private:

	ExternalToolAddEditDlgPrivPtr m_PrivPtr;
};



#endif // FILE_EXTERNAL_TOOL_ADD_EDIT_DLG_H

