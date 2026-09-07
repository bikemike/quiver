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
	GtkWidget* m_labelEmpty;
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
		GtkWidget*    m_expanderDetails;
		GtkWidget*    m_textviewDetails;
		GtkTextBuffer* m_textbufferDetails;

		static gboolean idle_task_finished(gpointer data) {
			TaskProgressGUI* pParent = (TaskProgressGUI*)data;
			gtk_widget_set_sensitive(pParent->m_btnCancel, TRUE);
			gtk_button_set_icon_name(GTK_BUTTON(pParent->m_btnCancel), "edit-clear");
			gtk_widget_set_sensitive(pParent->m_btnPause, FALSE);
			gtk_widget_set_sensitive(pParent->m_labelDetails, TRUE);
			gtk_widget_set_sensitive(pParent->m_labelProgDetails, TRUE);
			gtk_widget_set_sensitive(pParent->m_pbarProgress, TRUE);
			pParent->UpdateTaskGUI();
			if (pParent->m_expanderDetails && pParent->m_TaskPtr->GetMessageType() == AbstractTask::MSG_TYPE_ERROR)
			{
				gtk_expander_set_expanded(GTK_EXPANDER(pParent->m_expanderDetails), TRUE);
			}
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
			m_expanderDetails(NULL),
			m_textviewDetails(NULL),
			m_textbufferDetails(NULL),
			m_TaskHandlerPtr(new TaskHandler(this))
		{
			// vbox to hold everything
			m_vboxTaskArea = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
			gtk_widget_set_hexpand(m_vboxTaskArea, TRUE);
			gtk_widget_set_halign(m_vboxTaskArea, GTK_ALIGN_FILL);

			// title lable to vbox
			m_labelTitle = gtk_label_new( taskPtr->GetDescription().c_str() );
			gtk_label_set_xalign(GTK_LABEL(m_labelTitle), 0.);
			gtk_label_set_yalign(GTK_LABEL(m_labelTitle), 0.);
			gtk_widget_set_hexpand(m_labelTitle, TRUE);
			gtk_widget_set_halign(m_labelTitle, GTK_ALIGN_FILL);

			PangoAttrList* attrs = pango_attr_list_new();
			PangoAttribute* attr = pango_attr_weight_new (PANGO_WEIGHT_BOLD);
			pango_attr_list_insert(attrs,attr);
			gtk_label_set_attributes(GTK_LABEL(m_labelTitle), attrs);
			pango_attr_list_unref(attrs);

			//hbox for image / task details
			m_hboxTaskDetails = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
			gtk_widget_set_hexpand(m_hboxTaskDetails, TRUE);
			gtk_widget_set_halign(m_hboxTaskDetails, GTK_ALIGN_FILL);

			//image
			// taskPtr->GetPixbuf();
			m_imgThumbnail = gtk_image_new();

			// task details (text, progress, time stuff)
			m_vboxDetails = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
			gtk_widget_set_hexpand(m_vboxDetails, TRUE);
			gtk_widget_set_halign(m_vboxDetails, GTK_ALIGN_FILL);
			
			// text
			m_labelDetails = gtk_label_new( taskPtr->GetProgressText().c_str() );
			gtk_label_set_ellipsize(GTK_LABEL(m_labelDetails), PANGO_ELLIPSIZE_MIDDLE);
			gtk_label_set_xalign(GTK_LABEL(m_labelDetails), 0.);
			gtk_label_set_yalign(GTK_LABEL(m_labelDetails), 0.);
			gtk_widget_set_hexpand(m_labelDetails, TRUE);
			gtk_widget_set_halign(m_labelDetails, GTK_ALIGN_FILL);

			// progress
			m_hboxProgress = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
			gtk_widget_set_hexpand(m_hboxProgress, TRUE);
			gtk_widget_set_halign(m_hboxProgress, GTK_ALIGN_FILL);

			m_pbarProgress    = gtk_progress_bar_new();
			gtk_widget_set_hexpand(m_pbarProgress, TRUE);
			gtk_widget_set_halign(m_pbarProgress, GTK_ALIGN_FILL);

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
			gtk_widget_set_hexpand(m_labelProgDetails, TRUE);
			gtk_widget_set_halign(m_labelProgDetails, GTK_ALIGN_FILL);

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

			if (taskPtr->HasDetails())
			{
				m_expanderDetails = gtk_expander_new("Command Details");
				gtk_widget_set_margin_top(m_expanderDetails, 4);
				gtk_widget_set_hexpand(m_expanderDetails, TRUE);
				gtk_widget_set_halign(m_expanderDetails, GTK_ALIGN_FILL);

				GtkWidget* scrolled = gtk_scrolled_window_new();
				gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scrolled), 120);
				gtk_scrolled_window_set_has_frame(GTK_SCROLLED_WINDOW(scrolled), TRUE);
				gtk_widget_set_hexpand(scrolled, TRUE);
				gtk_widget_set_vexpand(scrolled, TRUE);
				gtk_widget_set_halign(scrolled, GTK_ALIGN_FILL);
				gtk_widget_set_valign(scrolled, GTK_ALIGN_FILL);

				m_textviewDetails = gtk_text_view_new();
				gtk_text_view_set_editable(GTK_TEXT_VIEW(m_textviewDetails), FALSE);
				gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(m_textviewDetails), FALSE);
				gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(m_textviewDetails), GTK_WRAP_WORD_CHAR);
				gtk_text_view_set_monospace(GTK_TEXT_VIEW(m_textviewDetails), TRUE);
				gtk_widget_set_margin_start(m_textviewDetails, 6);
				gtk_widget_set_margin_end(m_textviewDetails, 6);
				gtk_widget_set_margin_top(m_textviewDetails, 4);
				gtk_widget_set_margin_bottom(m_textviewDetails, 4);
				gtk_widget_set_hexpand(m_textviewDetails, TRUE);
				gtk_widget_set_vexpand(m_textviewDetails, TRUE);

				m_textbufferDetails = gtk_text_view_get_buffer(GTK_TEXT_VIEW(m_textviewDetails));

				gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), m_textviewDetails);
				gtk_expander_set_child(GTK_EXPANDER(m_expanderDetails), scrolled);

				g_signal_connect(m_expanderDetails, "notify::expanded",
					G_CALLBACK(+[](GObject* gobject, GParamSpec* pspec, gpointer user_data) {
						(void)pspec;
						GtkExpander* exp = GTK_EXPANDER(gobject);
						TaskProgressGUI* pGUI = static_cast<TaskProgressGUI*>(user_data);
						gboolean expanded = gtk_expander_get_expanded(exp);
						gtk_widget_set_vexpand(GTK_WIDGET(exp), expanded);
						gtk_widget_set_vexpand(pGUI->m_vboxTaskArea, expanded);
					}), this);

				gtk_box_append(GTK_BOX(m_vboxTaskArea), m_expanderDetails);
			}

			GtkWidget* sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
			gtk_widget_set_hexpand(sep, TRUE);
			gtk_widget_set_halign(sep, GTK_ALIGN_FILL);
			gtk_widget_set_margin_top(sep, 8);
			gtk_widget_set_margin_bottom(sep, 4);
			gtk_box_append(GTK_BOX(m_vboxTaskArea), sep);

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

			if (prog > 0.001 && secs > 0.001)
			{
				g_snprintf(labelText,256,"%.0f seconds elapsed, %.0f remaining (%d of %d %s, %.2f %s/second)", secs, secs/prog - secs, i, n, lng.c_str(), i/secs, shrt.c_str());
			}
			else
			{
				g_snprintf(labelText,256,"%.0f seconds elapsed (%d of %d %s)", secs, i, n, lng.c_str());
			}

			gtk_label_set_text(GTK_LABEL(m_labelProgDetails),labelText);
			gtk_label_set_text(GTK_LABEL(m_labelDetails), strProg.c_str() );

			if (m_expanderDetails && m_textbufferDetails)
			{
				std::string details = m_TaskPtr->GetDetails();
				gtk_text_buffer_set_text(m_textbufferDetails, details.c_str(), -1);
			}
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
	{
		m_pWidget = gtk_window_new();
		gtk_window_set_title(GTK_WINDOW(m_pWidget), "Task Manager");
		if (parent_window)
		{
			gtk_window_set_transient_for(GTK_WINDOW(m_pWidget), parent_window);
		}

		GtkWidget* header_bar = gtk_header_bar_new();
		gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(header_bar), TRUE);
		GtkWidget* title_label = gtk_label_new("Task Manager");
		gtk_widget_add_css_class(title_label, "title");
		gtk_header_bar_set_title_widget(GTK_HEADER_BAR(header_bar), title_label);
		gtk_window_set_titlebar(GTK_WINDOW(m_pWidget), header_bar);

		m_boxContent = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
		gtk_widget_set_hexpand(m_boxContent, TRUE);
		gtk_widget_set_vexpand(m_boxContent, TRUE);
		gtk_widget_set_halign(m_boxContent, GTK_ALIGN_FILL);
		gtk_widget_set_valign(m_boxContent, GTK_ALIGN_FILL);
		gtk_widget_set_margin_start(m_boxContent, 16);
		gtk_widget_set_margin_end(m_boxContent, 16);
		gtk_widget_set_margin_top(m_boxContent, 16);
		gtk_widget_set_margin_bottom(m_boxContent, 16);

		m_labelEmpty = gtk_label_new("No active tasks");
		gtk_widget_set_margin_top(m_labelEmpty, 24);
		gtk_widget_set_margin_bottom(m_labelEmpty, 24);
		gtk_widget_add_css_class(m_labelEmpty, "dim-label");
		gtk_box_append(GTK_BOX(m_boxContent), m_labelEmpty);

		GtkWidget* scrolled = gtk_scrolled_window_new();
		gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
		gtk_widget_set_hexpand(scrolled, TRUE);
		gtk_widget_set_vexpand(scrolled, TRUE);
		gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(scrolled), 520);
		gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scrolled), 200);
		gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), m_boxContent);

		gtk_window_set_child(GTK_WINDOW(m_pWidget), scrolled);
		gtk_window_set_default_size(GTK_WINDOW(m_pWidget), 620, 380);

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
		gtk_widget_set_visible(GTK_WIDGET(widget), FALSE);
		return TRUE; // do not propagate
	}

	void AddTaskGUI(AbstractTaskPtr taskPtr)
	{
		if (!taskPtr->IsHidden())
		{
			if (m_labelEmpty)
			{
				gtk_widget_set_visible(m_labelEmpty, FALSE);
			}

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
		}

		if (0 == m_mapTaskGUI.size())
		{
			if (m_labelEmpty)
			{
				gtk_widget_set_visible(m_labelEmpty, TRUE);
			}
			m_pParent->Hide();
		}

	}

	static gboolean idle_add_task(gpointer data)
	{
		std::pair<TaskManagerDlgPriv*, AbstractTaskPtr>* pData = 
			static_cast<std::pair<TaskManagerDlgPriv*, AbstractTaskPtr>*>(data);
		pData->first->AddTaskGUI(pData->second);
		delete pData;
		return G_SOURCE_REMOVE;
	}

	// ITaskManagerEventHandler methods
	void HandleTaskAdded(TaskManagerEventPtr event) 
	{
		auto* pData = new std::pair<TaskManagerDlgPriv*, AbstractTaskPtr>(this, event->GetTask());
		g_idle_add(idle_add_task, pData);
	}

	void HandleTaskRemoved(TaskManagerEventPtr event) 
	{ (void)event; 
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

GtkWidget* TaskManagerDlg::GetWidget() const
{
	return m_PrivPtr ? m_PrivPtr->m_pWidget : NULL;
}

void TaskManagerDlg::Show()
{
	if (m_PrivPtr && m_PrivPtr->m_pWidget)
	{
		gtk_widget_set_visible(m_PrivPtr->m_pWidget, TRUE);
		gtk_window_present(GTK_WINDOW(m_PrivPtr->m_pWidget));
	}
}

void TaskManagerDlg::Hide()
{
	if (m_PrivPtr && m_PrivPtr->m_pWidget)
	{
		gtk_widget_set_visible(m_PrivPtr->m_pWidget, FALSE);
	}
}
