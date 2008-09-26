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

			void HandleTaskFinished(TaskEventPtr event) 
			{
				bool bIsGUIThread = ThreadUtil::IsGUIThread();
				if (!bIsGUIThread)
				{
					gdk_threads_enter();
				}
				gtk_widget_set_sensitive(m_pParent->m_btnCancel, TRUE);
				gtk_button_set_image(GTK_BUTTON(m_pParent->m_btnCancel), 
					gtk_image_new_from_stock(GTK_STOCK_CLEAR, GTK_ICON_SIZE_BUTTON));
				gtk_widget_set_sensitive(m_pParent->m_btnPause, FALSE);
				//gtk_widget_set_sensitive(m_pParent->m_labelTitle, FALSE);
				gtk_widget_set_sensitive(m_pParent->m_labelDetails, FALSE);
				gtk_widget_set_sensitive(m_pParent->m_labelProgDetails, FALSE);
				gtk_widget_set_sensitive(m_pParent->m_pbarProgress, FALSE);
				if (!bIsGUIThread)
				{
					gdk_threads_leave();
				}
			}

			void HandleTaskCancelled(TaskEventPtr event) 
			{
				bool bIsGUIThread = ThreadUtil::IsGUIThread();
				if (!bIsGUIThread)
				{
					gdk_threads_enter();
				}
				gtk_widget_set_sensitive(m_pParent->m_btnCancel, TRUE);
				gtk_button_set_image(GTK_BUTTON(m_pParent->m_btnCancel), 
					gtk_image_new_from_stock(GTK_STOCK_CLEAR, GTK_ICON_SIZE_BUTTON));
				gtk_widget_set_sensitive(m_pParent->m_btnPause, FALSE);
				//gtk_widget_set_sensitive(m_pParent->m_labelTitle, FALSE);
				gtk_widget_set_sensitive(m_pParent->m_labelDetails, FALSE);
				gtk_widget_set_sensitive(m_pParent->m_labelProgDetails, FALSE);
				gtk_widget_set_sensitive(m_pParent->m_pbarProgress, FALSE);
				if (!bIsGUIThread)
				{
					gdk_threads_leave();
				}
			}

			void HandleTaskProgressUpdated(TaskEventPtr event) 
			{
				
				gdk_threads_enter();

				m_pParent->UpdateTaskGUI();

				gdk_threads_leave();
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
			m_vboxTaskArea = gtk_vbox_new(FALSE, 3);

			// title lable to vbox
			m_labelTitle = gtk_label_new( taskPtr->GetDescription().c_str() );

			PangoAttrList* attrs = pango_attr_list_new();
			PangoAttribute* attr = pango_attr_weight_new (PANGO_WEIGHT_BOLD);
			pango_attr_list_insert(attrs,attr);
			gtk_label_set_attributes(GTK_LABEL(m_labelTitle), attrs);
			pango_attr_list_unref(attrs);

			gtk_misc_set_alignment(GTK_MISC(m_labelTitle), 0., 0.);

			//hbox for image / task details
			m_hboxTaskDetails = gtk_hbox_new(FALSE, 5);

			//image
			GdkPixbuf* pixbuf = NULL;
			// taskPtr->GetPixbuf();
			m_imgThumbnail = gtk_image_new_from_pixbuf(pixbuf);

			// task details (text, progress, time stuff)
			m_vboxDetails = gtk_vbox_new(FALSE, 3);
			
			// text
			m_labelDetails = gtk_label_new( taskPtr->GetProgressText().c_str() );
			gtk_label_set_ellipsize(GTK_LABEL(m_labelDetails), PANGO_ELLIPSIZE_MIDDLE);
			gtk_misc_set_alignment(GTK_MISC(m_labelDetails), 0., 0.);

			// progress
			m_hboxProgress = gtk_hbox_new(FALSE, 2);
			m_pbarProgress    = gtk_progress_bar_new();

			m_btnPause    = gtk_button_new();
			gtk_button_set_image(GTK_BUTTON(m_btnPause), 
					gtk_image_new_from_stock(GTK_STOCK_MEDIA_PAUSE, GTK_ICON_SIZE_BUTTON));

			if (!taskPtr->CanPause())
			{
				gtk_widget_set_sensitive(m_btnPause, FALSE);
			}

			m_btnCancel    = gtk_button_new();
			gtk_button_set_image(GTK_BUTTON(m_btnCancel), 
					gtk_image_new_from_stock(GTK_STOCK_CANCEL, GTK_ICON_SIZE_BUTTON));

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

			gtk_misc_set_alignment(GTK_MISC(m_labelProgDetails), 0., 0.);

			gtk_box_pack_start (GTK_BOX(m_vboxTaskArea),
				m_labelTitle, FALSE, TRUE, 0);
			gtk_box_pack_start (GTK_BOX(m_vboxTaskArea),
				m_hboxTaskDetails, TRUE, TRUE, 0);

			gtk_box_pack_start (GTK_BOX(m_hboxTaskDetails),
				m_imgThumbnail, FALSE, TRUE, 0);
			gtk_box_pack_start (GTK_BOX(m_hboxTaskDetails),
				m_vboxDetails, TRUE, TRUE, 0);

			gtk_box_pack_start (GTK_BOX(m_vboxDetails),
				m_labelDetails, FALSE, TRUE, 0);
			gtk_box_pack_start (GTK_BOX(m_vboxDetails),
				m_hboxProgress, TRUE, TRUE, 0);
			gtk_box_pack_start (GTK_BOX(m_vboxDetails),
				m_labelProgDetails, FALSE, TRUE, 0);

			gtk_box_pack_start (GTK_BOX(m_hboxProgress),
				m_pbarProgress, TRUE, TRUE, 0);
			gtk_box_pack_start (GTK_BOX(m_hboxProgress),
				m_btnPause, FALSE, TRUE, 0);
			gtk_box_pack_start (GTK_BOX(m_hboxProgress),
				m_btnCancel, FALSE, TRUE, 0);

			gtk_widget_show_all(m_vboxTaskArea);

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

					gtk_button_set_image(GTK_BUTTON(button), 
							gtk_image_new_from_stock(GTK_STOCK_MEDIA_PLAY, GTK_ICON_SIZE_BUTTON));
				}
				else
				{
					pGUI->m_TaskPtr->Resume();
					gtk_button_set_image(GTK_BUTTON(button), 
							gtk_image_new_from_stock(GTK_STOCK_MEDIA_PAUSE, GTK_ICON_SIZE_BUTTON));
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
		m_pWidget = gtk_dialog_new();
		gtk_dialog_set_has_separator(GTK_DIALOG(m_pWidget),FALSE);
		/*
		m_pWidget = gtk_dialog_new_with_buttons ("Task Manager",
			parent_window,
			GTK_DIALOG_DESTROY_WITH_PARENT,
			GTK_STOCK_CLOSE,
			GTK_RESPONSE_CLOSE,
			NULL);
			*/

		gtk_window_set_default_size(GTK_WINDOW(m_pWidget), 500,-1);
	//		gtk_widget_hide_on_delete (m_pWidget);

		g_signal_connect (G_OBJECT (m_pWidget), "delete_event",
			G_CALLBACK (event_delete), this);
		
		g_signal_connect (G_OBJECT (m_pWidget), "response",
			G_CALLBACK (signal_response), this);
		
		gtk_box_set_spacing(GTK_BOX(GTK_DIALOG(m_pWidget)->vbox), 10);

		//spacing 10 between each item
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

			gtk_box_pack_start (GTK_BOX(GTK_DIALOG(m_pWidget)->vbox),
				taskGUIPtr->GetWidget(),
				FALSE,
				TRUE,
				0);

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
			gtk_container_remove(GTK_CONTAINER(GTK_DIALOG(m_pWidget)->vbox), itr->second->GetWidget());
			m_mapTaskGUI.erase(itr);

			int w, h;
			gtk_window_get_size(GTK_WINDOW(m_pWidget), &w,&h);
			gtk_window_resize(GTK_WINDOW(m_pWidget), w, 100);
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
	gtk_widget_show(m_PrivPtr->m_pWidget);
}

void TaskManagerDlg::Hide()
{
	gtk_widget_hide(m_PrivPtr->m_pWidget);
}
