#ifndef FILE_VIEWER_EVENT_SOURCE_H
#define FILE_VIEWER_EVENT_SOURCE_H

#include "AbstractEventSource.h"
#include "ViewerEvent.h"

class ViewerEventSource : public virtual AbstractEventSource
{
public:
	virtual ~ViewerEventSource(){};

	typedef boost::signal<void (ViewerEventPtr)> ViewerSignal;

	void AddEventHandler(IEventHandlerPtr handler);

	void EmitItemActivatedEvent();
	void EmitCursorChangedEvent();
private:
	ViewerSignal m_sigCursorChanged;
	ViewerSignal m_sigItemActivated;
	
	
};

#endif
