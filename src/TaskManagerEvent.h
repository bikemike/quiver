#ifndef FILE_TASK_MANAGER_EVENT_H
#define FILE_TASK_MANAGER_EVENT_H

#include "EventBase.h"
#include "AbstractTask.h"

class TaskManagerEvent : public EventBase
{
public:
	TaskManagerEvent(IEventSourcePtr src, AbstractTaskPtr taskPtr) : EventBase(src), m_TaskPtr(taskPtr){}

	AbstractTaskPtr GetTask(){ return m_TaskPtr;}
	
private:
	AbstractTaskPtr m_TaskPtr;
};

typedef boost::shared_ptr<TaskManagerEvent> TaskManagerEventPtr;


#endif

