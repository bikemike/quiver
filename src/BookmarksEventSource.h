#ifndef FILE_BOOKMARKS_EVENT_SOURCE_H
#define FILE_BOOKMARKS_EVENT_SOURCE_H

#include "AbstractEventSource.h"
#include "EventBase.h"

#include "BookmarksEvent.h"

class BookmarksEventSource : public virtual AbstractEventSource
{

public:
	typedef boost::signals2::signal<void (BookmarksEventPtr)> BookmarksSignal;
	typedef boost::shared_ptr<BookmarksSignal> BookmarksSignalPtr;

	BookmarksEventSource() : m_sigBookmarkChangedPtr(new BookmarksSignal())
	{};
	
	virtual ~BookmarksEventSource(){};

	void AddEventHandler(IEventHandlerPtr handler);

	void EmitBookmarkChangedEvent(BookmarksEvent::BookmarksEventType type);
	
private:
	BookmarksSignalPtr m_sigBookmarkChangedPtr;

};


#endif /*FILE_BOOKMARKS_EVENT_SOURCE_H*/
