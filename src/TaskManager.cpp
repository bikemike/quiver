#include "TaskManager.h"

#include <stdio.h>

TaskManagerPtr TaskManager::c_pTaskManagerPtr;

TaskManager::TaskManager()
{
	m_pAllTasksMutex  = g_mutex_new();
	m_pRunningTasksMutex  = g_mutex_new();
	m_pThreadPool = g_thread_pool_new(run_task, this, 2, FALSE, NULL);
}

TaskManager::~TaskManager()
{
	g_thread_pool_free(m_pThreadPool, TRUE, TRUE);
	g_mutex_free(m_pAllTasksMutex);
	g_mutex_free(m_pRunningTasksMutex);
}

TaskManagerPtr TaskManager::GetInstance()
{
	if (NULL == c_pTaskManagerPtr.get())
	{
		TaskManagerPtr prefsPtr(new TaskManager());
		c_pTaskManagerPtr = prefsPtr;
	}
	return c_pTaskManagerPtr;
}

void TaskManager::SetMaxThreads(int nThreads)
{
	g_thread_pool_set_max_threads( m_pThreadPool, nThreads, NULL);
}

void TaskManager::AddTask(AbstractTaskPtr taskPtr)
{
	// lock
	g_mutex_lock(m_pAllTasksMutex);

	m_vectTasks.push_back(taskPtr);
	// unlock
	g_mutex_unlock(m_pAllTasksMutex);

	taskPtr->AddEventHandler(c_pTaskManagerPtr);

	g_mutex_lock(m_pRunningTasksMutex);

	std::pair<std::set<AbstractTaskPtr>::iterator , bool> rval = m_setRunningTasks.insert(taskPtr);
	if (rval.second)
	{
		EmitTaskAddedEvent(taskPtr);
		g_thread_pool_push(m_pThreadPool, taskPtr.get(), NULL);
	}
	g_mutex_unlock(m_pRunningTasksMutex);

}

void TaskManager::RunTask(AbstractTaskPtr taskPtr)
{
	taskPtr->RunTask();

	g_mutex_lock(m_pRunningTasksMutex);
	m_setRunningTasks.erase(taskPtr);
	g_mutex_unlock(m_pRunningTasksMutex);
}

void TaskManager::run_task(gpointer data, gpointer user_data)
{
	TaskManager* taskManager = static_cast<TaskManager*>(user_data);
	AbstractTask* task = (AbstractTask*)data;
	taskManager->RunTask(boost::dynamic_pointer_cast<AbstractTask>(task->shared_from_this()));

}

void TaskManager::RemoveTask(AbstractTaskPtr taskPtr)
{
	// lock
	g_mutex_lock(m_pAllTasksMutex);

	std::vector<AbstractTaskPtr>::iterator itr;
	for (itr = m_vectTasks.begin(); m_vectTasks.end() != itr; ++itr)
	{
		if (taskPtr == *itr)
		{
			m_vectTasks.erase(itr);
			EmitTaskRemovedEvent(taskPtr);
			taskPtr->RemoveEventHandler(c_pTaskManagerPtr);
			break;
		}
	}

	// unlock
	g_mutex_unlock(m_pAllTasksMutex);

}

void TaskManager::HandleTaskStarted(TaskEventPtr taskEventPtr)
{
}

void TaskManager::HandleTaskResumed(TaskEventPtr taskEventPtr)
{
}

void TaskManager::HandleTaskPaused(TaskEventPtr taskEventPtr)
{
}

void TaskManager::HandleTaskUnpaused(TaskEventPtr taskEventPtr)
{
	AbstractTaskPtr taskPtr = boost::dynamic_pointer_cast<AbstractTask>(taskEventPtr->GetSource());

	g_mutex_lock(m_pRunningTasksMutex);
	std::pair<std::set<AbstractTaskPtr>::iterator , bool> rval = m_setRunningTasks.insert(taskPtr);
	if (rval.second)
	{
		if (NULL != taskPtr.get())
		{
			g_thread_pool_push(m_pThreadPool, taskPtr.get(), NULL);
		}
	}

	g_mutex_unlock(m_pRunningTasksMutex);
}

void TaskManager::HandleTaskFinished(TaskEventPtr taskEventPtr)
{
	AbstractTaskPtr taskPtr = boost::dynamic_pointer_cast<AbstractTask>(taskEventPtr->GetSource());
	RemoveTask(taskPtr);
}

void TaskManager::HandleTaskCancelled(TaskEventPtr taskEventPtr)
{
	AbstractTaskPtr taskPtr = boost::dynamic_pointer_cast<AbstractTask>(taskEventPtr->GetSource());
	RemoveTask(taskPtr);
}

void TaskManager::HandleTaskProgressUpdated(TaskEventPtr taskEventPtr)
{
	//AbstractTask* pTask = dynamic_cast<AbstractTask*>(taskEventPtr->GetSource());
}

void TaskManager::HandleTaskMessage(TaskEventPtr taskEventPtr)
{
	AbstractTaskPtr taskPtr = boost::dynamic_pointer_cast<AbstractTask>(taskEventPtr->GetSource());
	AbstractTask::MessageType type = taskPtr->GetMessageType();
	std::string msg = taskPtr->GetMessage();

	switch (type)
	{
		case AbstractTask::MSG_TYPE_INFO:
			printf("INFO: ");
			break;
		case AbstractTask::MSG_TYPE_DEBUG:
			printf("DBG : ");
			break;
		case AbstractTask::MSG_TYPE_WARNING:
			printf("WARN: ");
			break;
		case AbstractTask::MSG_TYPE_ERROR:
			printf("ERR : ");
			break;
	}
	printf("%s\n", msg.c_str());
}


