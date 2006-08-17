#include "ViewerEventSource.h"
#include "IViewerEventHandler.h"

void ViewerEventSource::AddEventHandler(IEventHandlerPtr handler)
{
	IViewerEventHandlerPtr h = boost::static_pointer_cast<IViewerEventHandler>(handler);
	
	boost::signals::connection c = m_sigCursorChanged.connect( boost::bind(&IViewerEventHandler::HandleCursorChanged,h,_1) );
	MapConnection(handler,c);

	c = m_sigItemActivated.connect( boost::bind(&IViewerEventHandler::HandleItemActivated,h,_1) );
	MapConnection(handler,c);
}


void ViewerEventSource::EmitItemActivatedEvent()
{
	ViewerEventPtr n( new ViewerEvent(this) );
	m_sigItemActivated(n);
	
}

void ViewerEventSource::EmitCursorChangedEvent()
{
	ViewerEventPtr n( new ViewerEvent(this) );
	m_sigCursorChanged(n);
}

