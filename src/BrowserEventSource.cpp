#include "BrowserEventSource.h"
#include "IBrowserEventHandler.h"

void BrowserEventSource::AddEventHandler(IEventHandlerPtr handler)
{
	IBrowserEventHandlerPtr h = boost::static_pointer_cast<IBrowserEventHandler>(handler);
	
	boost::signals::connection c = m_sigSelectionChanged.connect( boost::bind(&IBrowserEventHandler::HandleSelectionChanged,h,_1) );
	MapConnection(handler,c);

	c = m_sigCursorChanged.connect( boost::bind(&IBrowserEventHandler::HandleCursorChanged,h,_1) );
	MapConnection(handler,c);

	c = m_sigItemActivated.connect( boost::bind(&IBrowserEventHandler::HandleItemActivated,h,_1) );
	MapConnection(handler,c);
}


void BrowserEventSource::EmitItemActivatedEvent()
{
	BrowserEventPtr n( new BrowserEvent(this) );
	m_sigItemActivated(n);
	
}

void BrowserEventSource::EmitSelectionChangedEvent()
{
	BrowserEventPtr n( new BrowserEvent(this) );
	m_sigSelectionChanged(n);
}

void BrowserEventSource::EmitCursorChangedEvent()
{
	BrowserEventPtr n( new BrowserEvent(this) );
	m_sigCursorChanged(n);
}


