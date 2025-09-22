#ifndef FILE_ADJUST_DATE_DLG_H
#define FILE_ADJUST_DATE_DLG_H

#include <gtk/gtk.h>
#include <string>
#include <functional> // For std::function
#include <boost/shared_ptr.hpp>

class AdjustDateDlg
{
public:
	//constructor
	AdjustDateDlg(GtkWindow* parent);
	//~AdjustDateDlg();
	
	//member functions
	GtkWidget *GetWidget() const;
	void Run(); // Changed to void, will not be blocking
    void set_on_result_callback(std::function<void(int)> callback);

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

