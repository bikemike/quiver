#ifndef FILE_IIMAGE_LIST_EVENT_HANDLER_H
#define FILE_IIMAGE_LIST_EVENT_HANDLER_H


#include "IEventHandler.h"
#include "ImageListEvent.h"

class IImageListEventHandler : public IEventHandler
{
public:
	
	virtual void HandleContentsChanged(ImageListEventPtr event) = 0;
	virtual void HandleCurrentIndexChanged(ImageListEventPtr event) = 0;
	virtual void HandleItemAdded(ImageListEventPtr event) = 0;
	virtual void HandleItemRemoved(ImageListEventPtr event) = 0;
	virtual void HandleItemChanged(ImageListEventPtr event) = 0;

	virtual ~IImageListEventHandler(){};
};

typedef boost::shared_ptr< IImageListEventHandler> IImageListEventHandlerPtr;

#endif /*FILE_IIMAGE_LIST_EVENT_HANDLER_H*/
