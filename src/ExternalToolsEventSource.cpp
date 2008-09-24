#include "ExternalToolsEventSource.h"
#include "IExternalToolsEventHandler.h"

void ExternalToolsEventSource::AddEventHandler(IEventHandlerPtr handler)
{

	IExternalToolsEventHandlerPtr h = boost::static_pointer_cast<IExternalToolsEventHandler>(handler);
	
	boost::signals::connection c = m_sigExternalToolChangedPtr->connect( boost::bind(&IExternalToolsEventHandler::HandleExternalToolChanged,h,_1) );
	MapConnection(handler,c);
	
}

void ExternalToolsEventSource::EmitExternalToolChangedEvent(ExternalToolsEvent::ExternalToolsEventType type)
{
	ExternalToolsEventPtr event( new ExternalToolsEvent(shared_from_this(),type) );
	(*m_sigExternalToolChangedPtr)(event);
}


