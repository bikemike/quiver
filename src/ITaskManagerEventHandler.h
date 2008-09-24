#ifndef FILE_ITASK_MANAGER_EVENT_HANDLER_H
#define FILE_ITASK_MANAGER_EVENT_HANDLER_H

#include "IEventHandler.h"
#include "TaskManagerEvent.h"

class ITaskManagerEventHandler : public IEventHandler
{
public:
	virtual void HandleTaskAdded(TaskManagerEventPtr event) = 0;
	virtual void HandleTaskRemoved(TaskManagerEventPtr event) = 0;
	virtual ~ITaskManagerEventHandler(){};
};

typedef boost::shared_ptr<ITaskManagerEventHandler> ITaskManagerEventHandlerPtr;

#endif // FILE_ITASK_MANAGER_EVENT_HANDLER_H

