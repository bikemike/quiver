#ifndef FILE_TASK_EVENT_SOURCE_H
#define FILE_TASK_EVENT_SOURCE_H

#include "AbstractEventSource.h"
#include "TaskEvent.h"

class TaskEventSource : public virtual AbstractEventSource
{
public:
	virtual ~TaskEventSource(){};

	typedef boost::signals2::signal<void (TaskEventPtr)> TaskSignal;

	void AddEventHandler(IEventHandlerPtr handler);

	void EmitTaskStartedEvent();
	void EmitTaskResumedEvent();
	void EmitTaskPausedEvent();
	void EmitTaskUnpausedEvent();
	void EmitTaskFinishedEvent();
	void EmitTaskCancelledEvent();
	void EmitTaskProgressUpdatedEvent();
	void EmitTaskMessageEvent();

private:
	TaskSignal m_sigTaskStarted;
	TaskSignal m_sigTaskResumed;
	TaskSignal m_sigTaskPaused;
	TaskSignal m_sigTaskUnpaused;
	TaskSignal m_sigTaskFinished;
	TaskSignal m_sigTaskCancelled;
	TaskSignal m_sigTaskProgressUpdated;
	TaskSignal m_sigTaskMessage;
	
	
};

#endif

