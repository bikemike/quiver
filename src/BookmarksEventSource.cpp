#include "BookmarksEventSource.h"
#include "IBookmarksEventHandler.h"

void BookmarksEventSource::AddEventHandler(IEventHandlerPtr handler)
{

	IBookmarksEventHandlerPtr h = boost::static_pointer_cast<IBookmarksEventHandler>(handler);
	
	boost::signals::connection c = m_sigBookmarkChangedPtr->connect( boost::bind(&IBookmarksEventHandler::HandleBookmarkChanged,h,_1) );
	MapConnection(handler,c);
	
}

void BookmarksEventSource::EmitBookmarkChangedEvent(BookmarksEvent::BookmarksEventType type)
{
	BookmarksEventPtr event( new BookmarksEvent(shared_from_this(),type) );
	(*m_sigBookmarkChangedPtr)(event);
}


