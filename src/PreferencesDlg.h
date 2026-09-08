#ifndef FILE_PREFERENCES_DLG_H
#define FILE_PREFERENCES_DLG_H

#include <gtk/gtk.h>

#include <boost/shared_ptr.hpp>

class PreferencesDlg
{
public:
	//constructor
	PreferencesDlg();
	//~PreferencesDlg();
	
	//member functions
	GtkWidget *GetWidget();
	void Run();
	/* Opens the (single, shared) preferences dialog; reuses the existing
	 * window if one is already open. */
	static void ShowDialog();

	class PreferencesDlgPriv;
	typedef boost::shared_ptr<PreferencesDlgPriv> PreferencesDlgPrivPtr;

private:

	PreferencesDlgPrivPtr m_PrivPtr;
	
};



#endif // FILE_PREFERENCES_DLG_H

