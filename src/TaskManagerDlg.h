#ifndef FILE_TASK_MANAGER_DLG_H
#define FILE_TASK_MANAGER_DLG_H

#include <gtk/gtk.h>

#include <boost/shared_ptr.hpp>

class TaskManagerDlg;
typedef boost::shared_ptr<TaskManagerDlg> TaskManagerDlgPtr;

class TaskManagerDlg
{
public:
	~TaskManagerDlg();

	static void Create(GtkWindow* parent);
	static TaskManagerDlgPtr GetInstance();
	
	//member functions
	GtkWidget *GetWidget() const;
	void Show();
	void Hide();

	class TaskManagerDlgPriv;
	typedef boost::shared_ptr<TaskManagerDlgPriv> TaskManagerDlgPrivPtr;

private:
	// private constructor
	TaskManagerDlg(GtkWindow* parent);

	static TaskManagerDlgPtr c_pTaskManagerDlgPtr;

	TaskManagerDlgPrivPtr m_PrivPtr;
	
};



#endif // FILE_TASK_MANAGER_DLG_H

