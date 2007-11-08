#include "QueryOptsEventSource.h"
#include "IQueryOptsEventHandler.h"

void QueryOptsEventSource::AddEventHandler(IEventHandlerPtr handler)
{
	//IQueryOptsEventHandlerPtr h = boost::static_pointer_cast<IQueryOptsEventHandler>(handler);
	
	//boost::signals::connection c = m_sigSelectionChanged.connect( boost::bind(&IQueryOptsEventHandler::HandleSelectionChanged,h,_1) );
	//MapConnection(handler,c);

}

//void QueryOptsEventSource::EmitSelectionChangedEvent()
//{
//	QueryOptsEventPtr n( new QueryOptsEvent(this) );
//	m_sigSelectionChanged(n);
//}
//
