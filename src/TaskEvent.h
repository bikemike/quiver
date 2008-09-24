#ifndef FILE_TASK_EVENT_H
#define FILE_TASK_EVENT_H

#include "EventBase.h"

class TaskEvent : public EventBase
{
public:
	TaskEvent(IEventSourcePtr src) : EventBase(src){}
private:
};

typedef boost::shared_ptr<TaskEvent> TaskEventPtr;


#endif

