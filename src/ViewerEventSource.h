#ifndef FILE_VIEWER_EVENT_SOURCE_H
#define FILE_VIEWER_EVENT_SOURCE_H

#include "AbstractEventSource.h"
#include "ViewerEvent.h"

class ViewerEventSource : public virtual AbstractEventSource
{
public:
	virtual ~ViewerEventSource(){};

	typedef boost::signals2::signal<void (ViewerEventPtr)> ViewerSignal;

	void AddEventHandler(IEventHandlerPtr handler);

	void EmitItemClickedEvent();
	void EmitItemActivatedEvent();
	void EmitCursorChangedEvent();
	void EmitSlideShowStartedEvent();
	void EmitSlideShowStoppedEvent();

private:
	ViewerSignal m_sigCursorChanged;
	ViewerSignal m_sigItemClicked;
	ViewerSignal m_sigItemActivated;
	ViewerSignal m_sigSlideShowStarted;
	ViewerSignal m_sigSlideShowStopped;
	
	
};

#endif
