#ifndef FILE_RENAME_DLG_H
#define FILE_RENAME_DLG_H

#include <gtk/gtk.h>
#include <string>
#include <vector>
#include <boost/shared_ptr.hpp>

#include "QuiverFile.h"

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

	// batch mode over an explicit file list (the browser selection is the
	// usual source); each item is renamed within its own folder.
	std::vector<QuiverFile> GetFiles() const;
	void        SetFiles(std::vector<QuiverFile> vectFiles);

	// whether the dialog is currently renaming the explicit file list
	// (true) or a whole folder (false); the user can flip between the two
	// while the dialog is open, so this must be re-read after Run().
	bool        GetFilesMode() const;

	class       RenameDlgPriv;
	typedef boost::shared_ptr<RenameDlgPriv> RenameDlgPrivPtr;

private:
	RenameDlgPrivPtr m_PrivPtr;
};



#endif // FILE_RENAME_DLG_H

