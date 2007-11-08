#include "QueryEventSource.h"
#include "IQueryEventHandler.h"

void QueryEventSource::AddEventHandler(IEventHandlerPtr handler)
{
	IQueryEventHandlerPtr h = boost::static_pointer_cast<IQueryEventHandler>(handler);
	
	boost::signals::connection c = m_sigSelectionChanged.connect( boost::bind(&IQueryEventHandler::HandleSelectionChanged,h,_1) );
	MapConnection(handler,c);

	c = m_sigCursorChanged.connect( boost::bind(&IQueryEventHandler::HandleCursorChanged,h,_1) );
	MapConnection(handler,c);

	c = m_sigItemActivated.connect( boost::bind(&IQueryEventHandler::HandleItemActivated,h,_1) );
	MapConnection(handler,c);
}


void QueryEventSource::EmitItemActivatedEvent()
{
	QueryEventPtr n( new QueryEvent(this) );
	m_sigItemActivated(n);
	
}

void QueryEventSource::EmitSelectionChangedEvent()
{
	QueryEventPtr n( new QueryEvent(this) );
	m_sigSelectionChanged(n);
}

void QueryEventSource::EmitCursorChangedEvent()
{
	QueryEventPtr n( new QueryEvent(this) );
	m_sigCursorChanged(n);
}


