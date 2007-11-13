#ifndef FILE_IBOOKMARKS_EVENT_HANDLER_H
#define FILE_IBOOKMARKS_EVENT_HANDLER_H


#include "IEventHandler.h"
#include "BookmarksEvent.h"

class IBookmarksEventHandler : public IEventHandler
{
public:
	
	virtual void HandleBookmarkChanged(BookmarksEventPtr event) = 0;

	virtual ~IBookmarksEventHandler(){};
};

typedef boost::shared_ptr< IBookmarksEventHandler> IBookmarksEventHandlerPtr;

#endif /*FILE_IBOOKMARKS_EVENT_HANDLER_H*/
