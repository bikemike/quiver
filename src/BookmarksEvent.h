#ifndef FILE_BOOKMARKS_EVENT_H
#define FILE_BOOKMARKS_EVENT_H

#include "EventBase.h"
#include <string>

class BookmarksEvent : public EventBase
{
public:
	typedef enum 
	{
		BOOKMARK_ADDED,
		BOOKMARK_REMOVED,
		BOOKMARK_CHANGED,
	} BookmarksEventType;

	BookmarksEvent(IEventSource* src, BookmarksEventType eventType) : EventBase(src), m_BookmarksEventType(eventType) { };

	
	BookmarksEventType GetEventType(){return m_BookmarksEventType;};
	

private:
	
	BookmarksEventType m_BookmarksEventType;
};

typedef boost::shared_ptr<BookmarksEvent> BookmarksEventPtr;


#endif

