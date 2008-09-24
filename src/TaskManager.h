#ifndef FILE_TASK_MANAGER_H
#define FILE_TASK_MANAGER_H

#include "AbstractTask.h"
#include "TaskManagerEventSource.h"
#include "ITaskEventHandler.h"
#include <glib.h>
#include <vector>
#include <set>

class TaskManager;

typedef boost::shared_ptr<TaskManager> TaskManagerPtr;

class TaskManager : public TaskManagerEventSource, public ITaskEventHandler
{
public:
	virtual ~TaskManager();

	static TaskManagerPtr GetInstance();

	void SetMaxThreads(int nThreads);
	void AddTask(AbstractTaskPtr taskPtr);

protected:
	void HandleTaskStarted(TaskEventPtr taskEventPtr);
	void HandleTaskResumed(TaskEventPtr taskEventPtr);
	void HandleTaskPaused(TaskEventPtr taskEventPtr);
	void HandleTaskUnpaused(TaskEventPtr taskEventPtr);
	void HandleTaskFinished(TaskEventPtr taskEventPtr);
	void HandleTaskCancelled(TaskEventPtr taskEventPtr);
	void HandleTaskProgressUpdated(TaskEventPtr taskEventPtr);
	void HandleTaskMessage(TaskEventPtr taskEventPtr);

private:
	TaskManager();

	void RunTask(AbstractTaskPtr pTask);
	void RemoveTask(AbstractTaskPtr pTask);

	static void run_task(gpointer data, gpointer user_data);

	static TaskManagerPtr c_pTaskManagerPtr;

	std::vector<AbstractTaskPtr> m_vectTasks;

	std::set<AbstractTaskPtr> m_setRunningTasks;

	GThreadPool* m_pThreadPool;
	GMutex*     m_pAllTasksMutex;
	GMutex*     m_pRunningTasksMutex;

};

#endif // FILE_TASK_MANAGER_H

