#ifndef FILE_ADJUST_DATE_DLG_H
#define FILE_ADJUST_DATE_DLG_H

#include <gtk/gtk.h>
#include <string>
#include <boost/shared_ptr.hpp>

class AdjustDateDlg
{
public:
	//constructor
	AdjustDateDlg();
	//~AdjustDateDlg();
	
	//member functions
	GtkWidget *GetWidget() const;
	bool Run();

	bool IsAdjustDate() const;
	bool IsSetDate() const;

	bool ModifyModificationTime() const;
	bool ModifyExifDate() const;
	bool ModifyExifDateOrig() const;
	bool ModifyExifDateDig() const;

	std::string GetDateString() const;
	int GetAdjustmentYears() const;
	int GetAdjustmentDays() const;
	int GetAdjustmentHours() const;
	int GetAdjustmentMinutes() const;
	int GetAdjustmentSeconds() const;

	class AdjustDateDlgPriv;
	typedef boost::shared_ptr<AdjustDateDlgPriv> AdjustDateDlgPrivPtr;

private:
	AdjustDateDlgPrivPtr m_PrivPtr;
};



#endif // FILE_ADJUST_DATE_DLG_H

