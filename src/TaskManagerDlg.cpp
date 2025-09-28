#include "TaskManagerDlg.h"
#include "IconManager.h"

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
    GtkBuilder* m_pGtkBuilder;
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
			{
			}

			void HandleTaskResumed(TaskEventPtr event) 
			{
			}

			void HandleTaskMessage(TaskEventPtr event) 
			{
			}

			void HandleTaskPaused(TaskEventPtr event) 
			{
			}

			void HandleTaskUnpaused(TaskEventPtr event) 
			{
			}

            static gboolean task_finished_idle(gpointer data)
            {
                TaskProgressGUI* pGUI = static_cast<TaskProgressGUI*>(data);
				gtk_widget_set_sensitive(pGUI->m_btnCancel, TRUE);
                GtkWidget* image = gtk_image_new_from_paintable(IconManager::GetInstance()->GetIcon("edit-clear"));
				gtk_button_set_child(GTK_BUTTON(pGUI->m_btnCancel), image);
				gtk_widget_set_sensitive(pGUI->m_btnPause, FALSE);
				gtk_widget_set_sensitive(pGUI->m_labelDetails, FALSE);
				gtk_widget_set_sensitive(pGUI->m_labelProgDetails, FALSE);
				gtk_widget_set_sensitive(pGUI->m_pbarProgress, FALSE);
                return G_SOURCE_REMOVE;
            }

			void HandleTaskFinished(TaskEventPtr event) 
			{
                g_idle_add(task_finished_idle, this);
			}

			void HandleTaskCancelled(TaskEventPtr event) 
			{
                g_idle_add(task_finished_idle, this);
			}

            static gboolean update_progress_idle(gpointer data)
            {
                TaskProgressGUI* pGUI = static_cast<TaskProgressGUI*>(data);
                pGUI->UpdateTaskGUI();
                return G_SOURCE_REMOVE;
            }

			void HandleTaskProgressUpdated(TaskEventPtr event) 
			{
                g_idle_add(update_progress_idle, this);
			}

		};

		typedef boost::shared_ptr<TaskHandler> TaskHandlerPtr;

		TaskHandlerPtr m_TaskHandlerPtr;

		AbstractTaskPtr m_TaskPtr;

		TaskProgressGUI(TaskManagerDlgPriv* parent, AbstractTaskPtr taskPtr) : 
			m_pParent(parent),
			m_TaskHandlerPtr(new TaskHandler(this))
		{
            GtkBuilder* builder = gtk_builder_new_from_file(QUIVER_DATADIR "/task-progress-widget.ui");
            m_vboxTaskArea = GTK_WIDGET(gtk_builder_get_object(builder, "task_progress_widget"));
			m_labelTitle = GTK_WIDGET(gtk_builder_get_object(builder, "task_title_label"));
			m_imgThumbnail = GTK_WIDGET(gtk_builder_get_object(builder, "task_thumbnail_image"));
			m_labelDetails = GTK_WIDGET(gtk_builder_get_object(builder, "task_details_label"));
			m_pbarProgress = GTK_WIDGET(gtk_builder_get_object(builder, "task_progress_bar"));
			m_btnPause = GTK_WIDGET(gtk_builder_get_object(builder, "task_pause_button"));
			m_btnCancel = GTK_WIDGET(gtk_builder_get_object(builder, "task_cancel_button"));
			m_labelProgDetails = GTK_WIDGET(gtk_builder_get_object(builder, "task_progress_details_label"));
            g_object_unref(builder);

			gtk_label_set_text(GTK_LABEL(m_labelTitle), taskPtr->GetDescription().c_str());
            // TODO: Set thumbnail
			gtk_label_set_text(GTK_LABEL(m_labelDetails), taskPtr->GetProgressText().c_str());

			if (!taskPtr->CanPause())
			{
				gtk_widget_set_sensitive(m_btnPause, FALSE);
			}

			if (!taskPtr->CanCancel())
			{
				gtk_widget_set_sensitive(m_btnCancel, FALSE);
			}

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

                    GtkWidget* image = gtk_image_new_from_paintable(IconManager::GetInstance()->GetIcon("media-playback-start"));
					gtk_button_set_child(GTK_BUTTON(button), image);
				}
				else
				{
					pGUI->m_TaskPtr->Resume();
                    GtkWidget* image = gtk_image_new_from_paintable(IconManager::GetInstance()->GetIcon("media-playback-pause"));
					gtk_button_set_child(GTK_BUTTON(button), image);
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
	{
        m_pGtkBuilder = gtk_builder_new_from_file(QUIVER_DATADIR "/task-manager-dialog.ui");
        m_pWidget = GTK_WIDGET(gtk_builder_get_object(m_pGtkBuilder, "TaskManagerDialog"));
        gtk_window_set_transient_for(GTK_WINDOW(m_pWidget), parent_window);

        GtkWidget* close_button = GTK_WIDGET(gtk_builder_get_object(m_pGtkBuilder, "close_button"));
        g_signal_connect_swapped(close_button, "clicked", G_CALLBACK(gtk_widget_hide), m_pWidget);

		g_signal_connect (G_OBJECT (m_pWidget), "delete_event",
			G_CALLBACK (event_delete), this);
		
		g_signal_connect (G_OBJECT (m_pWidget), "response",
			G_CALLBACK (signal_response), this);
	}

	~TaskManagerDlgPriv()
	{
		if (NULL != m_pWidget)
		{
			gtk_window_destroy(GTK_WINDOW(m_pWidget));
		}
	}

	static gboolean event_delete( GtkWidget *widget,GdkEvent  *event, gpointer   data )
	{
		//TaskManagerDlgPriv* dlgPriv = static_cast<TaskManagerDlgPriv*>(user_data);
		gtk_widget_hide(widget);

		return TRUE; // do not propagate
	}

	static void signal_response( GtkDialog *dlg, gint arg1, gpointer user_data )
	{
		gtk_widget_hide(GTK_WIDGET(dlg));
	}

	void AddTaskGUI(AbstractTaskPtr taskPtr)
	{
		if (!taskPtr->IsHidden())
		{
			TaskProgressGUIPtr taskGUIPtr(new TaskProgressGUI(this, taskPtr));
            GtkWidget* task_list_box = GTK_WIDGET(gtk_builder_get_object(m_pGtkBuilder, "task_list_box"));
			gtk_box_append (GTK_BOX(task_list_box),
				taskGUIPtr->GetWidget());

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
            GtkWidget* task_list_box = GTK_WIDGET(gtk_builder_get_object(m_pGtkBuilder, "task_list_box"));
			gtk_box_remove(GTK_BOX(task_list_box), itr->second->GetWidget());
			m_mapTaskGUI.erase(itr);
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
	{
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
	gtk_widget_set_visible(m_PrivPtr->m_pWidget, TRUE);
}

void TaskManagerDlg::Hide()
{
	gtk_widget_hide(m_PrivPtr->m_pWidget);
}
