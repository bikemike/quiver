#include "ViewerEventSource.h"
#include "IViewerEventHandler.h"

void ViewerEventSource::AddEventHandler(IEventHandlerPtr handler)
{
	IViewerEventHandlerPtr h = boost::static_pointer_cast<IViewerEventHandler>(handler);
	
	boost::signals::connection c = m_sigCursorChanged.connect( boost::bind(&IViewerEventHandler::HandleCursorChanged,h,_1) );
	MapConnection(handler,c);

	c = m_sigItemActivated.connect( boost::bind(&IViewerEventHandler::HandleItemActivated,h,_1) );
	MapConnection(handler,c);

	c = m_sigItemClicked.connect( boost::bind(&IViewerEventHandler::HandleItemClicked,h,_1) );
	MapConnection(handler,c);

	c = m_sigSlideShowStarted.connect( boost::bind(&IViewerEventHandler::HandleSlideShowStarted,h,_1) );
	MapConnection(handler,c);
	
	c = m_sigSlideShowStopped.connect( boost::bind(&IViewerEventHandler::HandleSlideShowStopped,h,_1) );
	MapConnection(handler,c);
}


void ViewerEventSource::EmitItemActivatedEvent()
{
	ViewerEventPtr n( new ViewerEvent(shared_from_this()) );
	m_sigItemActivated(n);
}

void ViewerEventSource::EmitItemClickedEvent()
{
	ViewerEventPtr n( new ViewerEvent(shared_from_this()) );
	m_sigItemClicked(n);
}

void ViewerEventSource::EmitCursorChangedEvent()
{
	ViewerEventPtr n( new ViewerEvent(shared_from_this()) );
	m_sigCursorChanged(n);
}

void ViewerEventSource::EmitSlideShowStartedEvent()
{
	ViewerEventPtr n( new ViewerEvent(shared_from_this()) );
	m_sigSlideShowStarted(n);
}

void ViewerEventSource::EmitSlideShowStoppedEvent()
{
	ViewerEventPtr n( new ViewerEvent(shared_from_this()) );
	m_sigSlideShowStopped(n);
}

