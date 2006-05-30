#ifndef FILE_IIMAGE_LIST_EVENT_HANDLER_H
#define FILE_IIMAGE_LIST_EVENT_HANDLER_H


#include "IEventHandler.h"
#include "EventBase.h"

class IImageListEventHandler : public IEventHandler
{
public:
	virtual void HandleCurrentItemChanged(EventBasePtr event) = 0;
	virtual void HandleItemsAdded(EventBasePtr event) = 0;
	virtual void HandleItemsRemoved(EventBasePtr event) = 0;

	virtual ~IImageListEventHandler(){};
};

typedef boost::shared_ptr< IImageListEventHandler> IImageListEventHandlerPtr;

#endif /*FILE_IIMAGE_LIST_EVENT_HANDLER_H*/
