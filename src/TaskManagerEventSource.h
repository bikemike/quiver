#ifndef FILE_TASK_MANAGER_EVENT_SOURCE_H
#define FILE_TASK_MANAGER_EVENT_SOURCE_H

#include "AbstractEventSource.h"
#include "TaskManagerEvent.h"

#include "AbstractTask.h"

class TaskManagerEventSource : public virtual AbstractEventSource
{
public:
	virtual ~TaskManagerEventSource(){};

	typedef boost::signal<void (TaskManagerEventPtr)> TaskSignal;

	void AddEventHandler(IEventHandlerPtr handler);

	void EmitTaskAddedEvent(AbstractTaskPtr taskPtr);
	void EmitTaskRemovedEvent(AbstractTaskPtr taskPtr);

private:
	TaskSignal m_sigTaskAdded;
	TaskSignal m_sigTaskRemoved;
};

#endif


