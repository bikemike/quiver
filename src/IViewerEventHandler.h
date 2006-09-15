#ifndef FILE_IVIEWER_EVENT_HANDLER_H
#define FILE_IVIEWER_EVENT_HANDLER_H

#include "IEventHandler.h"
#include "ViewerEvent.h"

class IViewerEventHandler : public IEventHandler
{
public:
	virtual void HandleItemActivated(ViewerEventPtr event) = 0;
	virtual void HandleCursorChanged(ViewerEventPtr event) = 0;
	virtual void HandleSlideShowStarted(ViewerEventPtr event) = 0;
	virtual void HandleSlideShowStopped(ViewerEventPtr event) = 0;
	virtual ~IViewerEventHandler(){};
};

typedef boost::shared_ptr<IViewerEventHandler> IViewerEventHandlerPtr;

#endif
