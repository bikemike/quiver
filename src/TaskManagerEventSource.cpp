#include "TaskManagerEventSource.h"
#include "ITaskManagerEventHandler.h"

void TaskManagerEventSource::AddEventHandler(IEventHandlerPtr handler)
{
	ITaskManagerEventHandlerPtr h = boost::dynamic_pointer_cast<ITaskManagerEventHandler>(handler);

	assert (NULL != h.get());
	if (NULL == h.get())
		return;
	
	boost::signals::connection c = m_sigTaskAdded.connect( boost::bind(&ITaskManagerEventHandler::HandleTaskAdded,h,_1) );
	MapConnection(handler,c);

	c = m_sigTaskRemoved.connect( boost::bind(&ITaskManagerEventHandler::HandleTaskRemoved,h,_1) );
	MapConnection(handler,c);

}


void TaskManagerEventSource::EmitTaskAddedEvent(AbstractTaskPtr taskPtr)
{
	TaskManagerEventPtr n( new TaskManagerEvent(shared_from_this(), taskPtr) );
	m_sigTaskAdded(n);
}
void TaskManagerEventSource::EmitTaskRemovedEvent(AbstractTaskPtr taskPtr)
{
	TaskManagerEventPtr n( new TaskManagerEvent(shared_from_this(), taskPtr) );
	m_sigTaskRemoved(n);
}

