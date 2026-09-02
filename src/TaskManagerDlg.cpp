#include "TaskManagerDlg.h"

#include "ITaskManagerEventHandler.h"
//#include "ITaskEventHandler.h"

#include "TaskManager.h"

#include "ThreadUtil.h"

using namespace std;

TaskManagerDlgPtr TaskManagerDlg::c_pTaskManagerDlgPtr;



class TaskManagerDlg::TaskManagerDlgPriv
	: public ITaskManagerEventHandler
{
public:
	TaskManagerDlg* m_pParent;
	GtkWidget* m_pWidget;
	GtkWidget* m_boxContent;
	TaskManagerPtr m_TaskMgrPtr;

	class TaskProgressGUI
	{
	public:
		TaskManagerDlgPriv* m_pParent;
		GtkWidget*    m_vboxTaskArea;
		GtkWidget*    m_labelTitle;
		GtkWidget*    m_hboxTaskDetails;
		GtkWidget*    m_imgThumbnail;
		GtkWidget*    m_vboxDetails;
		GtkWidget*    m_labelDetails;
		GtkWidget*    m_hboxProgress;
		GtkWidget*    m_pbarProgress;
		GtkWidget*    m_btnPause;
		GtkWidget*    m_btnCancel;
		GtkWidget*    m_labelProgDetails;


				static gboolean idle_task_finished(gpointer data) {
			TaskProgressGUI* pParent = (TaskProgressGUI*)data;
			gtk_widget_set_sensitive(pParent->m_btnCancel, TRUE);
			gtk_button_set_icon_name(GTK_BUTTON(pParent->m_btnCancel), "edit-clear");
			gtk_widget_set_sensitive(pParent->m_btnPause, FALSE);
			gtk_widget_set_sensitive(pParent->m_labelDetails, FALSE);
			gtk_widget_set_sensitive(pParent->m_labelProgDetails, FALSE);
			gtk_widget_set_sensitive(pParent->m_pbarProgress, FALSE);
			return G_SOURCE_REMOVE;
		}

		static gboolean idle_task_progress(gpointer data) {
			TaskProgressGUI* pParent = (TaskProgressGUI*)data;
			pParent->UpdateTaskGUI();
			return G_SOURCE_REMOVE;
		}

class TaskHandler :
			public ITaskEventHandler
		{
		public:
			TaskProgressGUI* m_pParent;

			TaskHandler(TaskProgressGUI* parent)
				: m_pParent(parent)
			{
			}
			// ITaskEventHandler methods
			void HandleTaskStarted(TaskEventPtr event) 
			{ (void)event; 
			}

			void HandleTaskResumed(TaskEventPtr event) 
			{ (void)event; 
			}

			void HandleTaskMessage(TaskEventPtr event) 
			{ (void)event; 
			}

			void HandleTaskPaused(TaskEventPtr event) 
			{ (void)event; 
			}

			void HandleTaskUnpaused(TaskEventPtr event) 
			{ (void)event; 
			}

			void HandleTaskFinished(TaskEventPtr event) 
			{ (void)event; 
				g_idle_add(idle_task_finished, m_pParent);
			}

			void HandleTaskCancelled(TaskEventPtr event) 
			{ (void)event; 
				g_idle_add(idle_task_finished, m_pParent);
			}

			void HandleTaskProgressUpdated(TaskEventPtr event) 
			{ (void)event; 
				g_idle_add(idle_task_progress, m_pParent);
			}

		};

		typedef boost::shared_ptr<TaskHandler> TaskHandlerPtr;

		TaskHandlerPtr m_TaskHandlerPtr;

		AbstractTaskPtr m_TaskPtr;

		TaskProgressGUI(TaskManagerDlgPriv* parent, AbstractTaskPtr taskPtr) : 
			m_pParent(parent),
			m_vboxTaskArea(NULL),
			m_labelTitle(NULL),
			m_hboxTaskDetails(NULL),
			m_imgThumbnail(NULL),
			m_vboxDetails(NULL),
			m_labelDetails(NULL),
			m_hboxProgress(NULL),
			m_pbarProgress(NULL),
			m_btnPause(NULL),
			m_btnCancel(NULL),
			m_labelProgDetails(NULL),
			m_TaskHandlerPtr(new TaskHandler(this))
		{
			// vbox to hold everything
			m_vboxTaskArea = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);

			// title lable to vbox
			m_labelTitle = gtk_label_new( taskPtr->GetDescription().c_str() );

			PangoAttrList* attrs = pango_attr_list_new();
			PangoAttribute* attr = pango_attr_weight_new (PANGO_WEIGHT_BOLD);
			pango_attr_list_insert(attrs,attr);
			gtk_label_set_attributes(GTK_LABEL(m_labelTitle), attrs);
			pango_attr_list_unref(attrs);

			gtk_label_set_xalign(GTK_LABEL(m_labelTitle), 0.);
			gtk_label_set_yalign(GTK_LABEL(m_labelTitle), 0.);

			//hbox for image / task details
			m_hboxTaskDetails = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);

			//image
			// taskPtr->GetPixbuf();
			m_imgThumbnail = gtk_image_new();

			// task details (text, progress, time stuff)
			m_vboxDetails = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
			
			// text
			m_labelDetails = gtk_label_new( taskPtr->GetProgressText().c_str() );
			gtk_label_set_ellipsize(GTK_LABEL(m_labelDetails), PANGO_ELLIPSIZE_MIDDLE);
			gtk_label_set_xalign(GTK_LABEL(m_labelDetails), 0.);
			gtk_label_set_yalign(GTK_LABEL(m_labelDetails), 0.);

			// progress
			m_hboxProgress = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
			m_pbarProgress    = gtk_progress_bar_new();

			m_btnPause    = gtk_button_new();
			gtk_button_set_icon_name(GTK_BUTTON(m_btnPause), "media-pause");

			if (!taskPtr->CanPause())
			{
				gtk_widget_set_sensitive(m_btnPause, FALSE);
			}

			m_btnCancel    = gtk_button_new();
			gtk_button_set_icon_name(GTK_BUTTON(m_btnCancel), "process-stop");

			if (!taskPtr->CanCancel())
			{
				gtk_widget_set_sensitive(m_btnCancel, FALSE);
			}

			// time stuff
			m_labelProgDetails = gtk_label_new( "" );
			gtk_label_set_ellipsize(GTK_LABEL(m_labelProgDetails), PANGO_ELLIPSIZE_MIDDLE);

			attrs = pango_attr_list_new();
			attr = pango_attr_scale_new (PANGO_SCALE_SMALL);
			pango_attr_list_insert(attrs,attr);

			gtk_label_set_attributes(GTK_LABEL(m_labelProgDetails), attrs);
			pango_attr_list_unref(attrs);

			gtk_label_set_xalign(GTK_LABEL(m_labelProgDetails), 0.);
			gtk_label_set_yalign(GTK_LABEL(m_labelProgDetails), 0.);

			gtk_box_append(GTK_BOX(m_vboxTaskArea), m_labelTitle);
			gtk_box_append(GTK_BOX(m_vboxTaskArea), m_hboxTaskDetails);

			gtk_box_append(GTK_BOX(m_hboxTaskDetails), m_imgThumbnail);
			gtk_box_append(GTK_BOX(m_hboxTaskDetails), m_vboxDetails);

			gtk_box_append(GTK_BOX(m_vboxDetails), m_labelDetails);
			gtk_box_append(GTK_BOX(m_vboxDetails), m_hboxProgress);
			gtk_box_append(GTK_BOX(m_vboxDetails), m_labelProgDetails);

			gtk_box_append(GTK_BOX(m_hboxProgress), m_pbarProgress);
			gtk_box_append(GTK_BOX(m_hboxProgress), m_btnPause);
			gtk_box_append(GTK_BOX(m_hboxProgress), m_btnCancel);


			g_signal_connect(m_btnPause,
				"clicked",(GCallback)on_clicked,this);

			g_signal_connect(m_btnCancel,
				"clicked",(GCallback)on_clicked,this);

			m_TaskPtr = taskPtr;

			UpdateTaskGUI();

			m_TaskPtr->AddEventHandler(m_TaskHandlerPtr);
		}

		void UpdateTaskGUI()
		{
			double prog = m_TaskPtr->GetProgress();
			double secs = m_TaskPtr->GetRunningTimeSeconds();
			int i =       m_TaskPtr->GetCurrentIteration();
			int n =       m_TaskPtr->GetTotalIterations();
			string shrt  =  m_TaskPtr->GetIterationTypeName(true, true);
			string lng   =  m_TaskPtr->GetIterationTypeName(false, true);
			string strProg = m_TaskPtr->GetProgressText();

			gchar text[20] = "";
			g_snprintf(text,20,"%.0f %%",prog*100);

			gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(m_pbarProgress), prog);

			gtk_progress_bar_set_text(GTK_PROGRESS_BAR(m_pbarProgress), text);

			gchar labelText[256];

			g_snprintf(labelText,256,"%.0f seconds elapsed, %.0f remaining (%d of %d %s, %.2f %s/second)", secs, secs/prog - secs, i, n, lng.c_str(), i/secs, shrt.c_str());

			gtk_label_set_text(GTK_LABEL(m_labelProgDetails),labelText);
			gtk_label_set_text(GTK_LABEL(m_labelDetails), strProg.c_str() );

		}
		
		static void on_clicked (GtkButton *button, gpointer user_data)
		{
			TaskProgressGUI * pGUI = static_cast<TaskProgressGUI*>(user_data);
			if (GTK_BUTTON(pGUI->m_btnPause) == button)
			{
				if (!pGUI->m_TaskPtr->IsPaused())
				{
					pGUI->m_TaskPtr->Pause();

					gtk_button_set_icon_name(GTK_BUTTON(button), "media-play");
				}
				else
				{
					pGUI->m_TaskPtr->Resume();
					gtk_button_set_icon_name(GTK_BUTTON(button), "media-pause");
				}
			}
			else if (GTK_BUTTON(pGUI->m_btnCancel) == button)
			{
				if (pGUI->m_TaskPtr->IsFinished())
				{
					// remove from gui
					pGUI->m_pParent->RemoveTaskGUI(pGUI->m_TaskPtr);

				}
				else
				{
					pGUI->m_TaskPtr->Cancel();
				}

			}

		}

		~TaskProgressGUI()
		{
			m_TaskPtr->RemoveEventHandler(m_TaskHandlerPtr);
		}

		GtkWidget* GetWidget()
		{
			return m_vboxTaskArea;
		}
	};

	typedef boost::shared_ptr<TaskProgressGUI> TaskProgressGUIPtr ;

	map<AbstractTaskPtr, TaskProgressGUIPtr> m_mapTaskGUI;

public:
		TaskManagerDlgPriv(TaskManagerDlg* parent, GtkWindow* parent_window) :
		m_pParent(parent), m_TaskMgrPtr(TaskManager::GetInstance())
	{ (void)parent_window; 
		m_boxContent = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
		m_pWidget = gtk_window_new();
		gtk_window_set_title(GTK_WINDOW(m_pWidget), "Task Manager");
		gtk_window_set_child(GTK_WINDOW(m_pWidget), m_boxContent);

		gtk_window_set_default_size(GTK_WINDOW(m_pWidget), 500,-1);

		g_signal_connect (G_OBJECT (m_pWidget), "close-request",
			G_CALLBACK (event_close_request), this);
	}

	~TaskManagerDlgPriv()
	{
		if (NULL != m_pWidget)
		{
			gtk_window_destroy(GTK_WINDOW(m_pWidget));
		}
	}

	static gboolean event_close_request( GtkWindow *widget, gpointer data )
	{ (void)data;
		//TaskManagerDlgPriv* dlgPriv = static_cast<TaskManagerDlgPriv*>(user_data);
		gtk_widget_set_visible(GTK_WIDGET(widget), FALSE);

		return TRUE; // do not propagate
	}

	void AddTaskGUI(AbstractTaskPtr taskPtr)
	{
		if (!taskPtr->IsHidden())
		{
			TaskProgressGUIPtr taskGUIPtr(new TaskProgressGUI(this, taskPtr));

			gtk_box_append(GTK_BOX(m_boxContent), taskGUIPtr->GetWidget());

			m_mapTaskGUI.insert(pair<AbstractTaskPtr, TaskProgressGUIPtr>(taskPtr, taskGUIPtr));

			m_pParent->Show();
		}
	}

	void RemoveTaskGUI(AbstractTaskPtr taskPtr)
	{
		map<AbstractTaskPtr,TaskProgressGUIPtr>::iterator itr;
		itr = m_mapTaskGUI.find(taskPtr);
		if (m_mapTaskGUI.end() != itr)
		{
			gtk_box_remove(GTK_BOX(m_boxContent), itr->second->GetWidget());
			m_mapTaskGUI.erase(itr);

			int w = gtk_widget_get_width(GTK_WIDGET(m_pWidget));
			gtk_window_set_default_size(GTK_WINDOW(m_pWidget), w, 100);
		}

		if (0 == m_mapTaskGUI.size())
		{
			m_pParent->Hide();
		}

	}

	// ITaskManagerEventHandler methods
	void HandleTaskAdded(TaskManagerEventPtr event) 
	{
		AddTaskGUI(event->GetTask());
	}

	void HandleTaskRemoved(TaskManagerEventPtr event) 
	{ (void)event; 
		// TaskManagerPtr mgrPtr = TaskManager::GetInstance();
	}


};

TaskManagerDlg::TaskManagerDlg(GtkWindow* parent)
	: m_PrivPtr(new TaskManagerDlg::TaskManagerDlgPriv(this, parent))
{
	m_PrivPtr->m_TaskMgrPtr->AddEventHandler(m_PrivPtr);
}

TaskManagerDlg::~TaskManagerDlg()
{
	m_PrivPtr->m_TaskMgrPtr->RemoveEventHandler(m_PrivPtr);
}

void TaskManagerDlg::Create(GtkWindow* parent)
{
	if (NULL == c_pTaskManagerDlgPtr.get())
	{
		TaskManagerDlgPtr dlgPtr(new TaskManagerDlg(parent));
		c_pTaskManagerDlgPtr = dlgPtr;
	}
}

TaskManagerDlgPtr TaskManagerDlg::GetInstance()
{
	return c_pTaskManagerDlgPtr;
}

void TaskManagerDlg::Show()
{
}

void TaskManagerDlg::Hide()
{
	gtk_widget_set_visible(m_PrivPtr->m_pWidget, FALSE);
}
