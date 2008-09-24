#ifndef FILE_ITASK_EVENT_HANDLER_H
#define FILE_ITASK_EVENT_HANDLER_H

#include "IEventHandler.h"
#include "TaskEvent.h"

class ITaskEventHandler : public IEventHandler
{
public:
	virtual void HandleTaskStarted(TaskEventPtr event) = 0;
	virtual void HandleTaskResumed(TaskEventPtr event) = 0;
	virtual void HandleTaskPaused(TaskEventPtr event) = 0;
	virtual void HandleTaskUnpaused(TaskEventPtr event) = 0;
	virtual void HandleTaskFinished(TaskEventPtr event) = 0;
	virtual void HandleTaskCancelled(TaskEventPtr event) = 0;
	virtual void HandleTaskProgressUpdated(TaskEventPtr event) = 0;
	virtual void HandleTaskMessage(TaskEventPtr event) = 0;
	virtual ~ITaskEventHandler(){};
};

typedef boost::shared_ptr<ITaskEventHandler> ITaskEventHandlerPtr;

#endif

