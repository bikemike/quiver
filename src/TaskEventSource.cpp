#include "TaskEventSource.h"
#include "ITaskEventHandler.h"

void TaskEventSource::AddEventHandler(IEventHandlerPtr handler)
{
	ITaskEventHandlerPtr h = boost::dynamic_pointer_cast<ITaskEventHandler>(handler);

	assert (NULL != h.get());

	if (NULL == h.get())
		return;

	boost::signals::connection c = m_sigTaskStarted.connect( boost::bind(&ITaskEventHandler::HandleTaskStarted,h,_1) );
	MapConnection(handler,c);

	c = m_sigTaskResumed.connect( boost::bind(&ITaskEventHandler::HandleTaskResumed,h,_1) );
	MapConnection(handler,c);

	c = m_sigTaskPaused.connect( boost::bind(&ITaskEventHandler::HandleTaskPaused,h,_1) );
	MapConnection(handler,c);

	c = m_sigTaskUnpaused.connect( boost::bind(&ITaskEventHandler::HandleTaskUnpaused,h,_1) );
	MapConnection(handler,c);

	c = m_sigTaskFinished.connect( boost::bind(&ITaskEventHandler::HandleTaskFinished,h,_1) );
	MapConnection(handler,c);

	c = m_sigTaskCancelled.connect( boost::bind(&ITaskEventHandler::HandleTaskCancelled,h,_1) );
	MapConnection(handler,c);

	c = m_sigTaskProgressUpdated.connect( boost::bind(&ITaskEventHandler::HandleTaskProgressUpdated,h,_1) );
	MapConnection(handler,c);

	c = m_sigTaskMessage.connect( boost::bind(&ITaskEventHandler::HandleTaskMessage,h,_1) );
	MapConnection(handler,c);
}

void TaskEventSource::EmitTaskStartedEvent()
{
	TaskEventPtr n( new TaskEvent(shared_from_this()) );
	m_sigTaskStarted(n);
}

void TaskEventSource::EmitTaskResumedEvent()
{
	TaskEventPtr n( new TaskEvent(shared_from_this()) );
	m_sigTaskResumed(n);
}

void TaskEventSource::EmitTaskPausedEvent()
{
	TaskEventPtr n( new TaskEvent(shared_from_this()) );
	m_sigTaskPaused(n);
	
}

void TaskEventSource::EmitTaskUnpausedEvent()
{
	TaskEventPtr n( new TaskEvent(shared_from_this()) );
	m_sigTaskUnpaused(n);
}

void TaskEventSource::EmitTaskFinishedEvent()
{
	TaskEventPtr n( new TaskEvent(shared_from_this()) );
	m_sigTaskFinished(n);
}

void TaskEventSource::EmitTaskCancelledEvent()
{
	TaskEventPtr n( new TaskEvent(shared_from_this()) );
	m_sigTaskCancelled(n);
}

void TaskEventSource::EmitTaskProgressUpdatedEvent()
{
	TaskEventPtr n( new TaskEvent(shared_from_this()) );
	m_sigTaskProgressUpdated(n);
}

void TaskEventSource::EmitTaskMessageEvent()
{
	TaskEventPtr n( new TaskEvent(shared_from_this()) );
	m_sigTaskMessage(n);
}

