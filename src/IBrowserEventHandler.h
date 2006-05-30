#ifndef FILE_IBROWSER_EVENT_HANDLER_H
#define FILE_IBROWSER_EVENT_HANDLER_H

#include "IEventHandler.h"
#include "BrowserEvent.h"

class IBrowserEventHandler : public IEventHandler
{
public:
	virtual void HandleItemActivated(BrowserEventPtr event) = 0;
	virtual void HandleSelectionChanged(BrowserEventPtr event) = 0;
	virtual void HandleCursorChanged(BrowserEventPtr event) = 0;
	virtual ~IBrowserEventHandler(){};
};

typedef boost::shared_ptr<IBrowserEventHandler> IBrowserEventHandlerPtr;

#endif
