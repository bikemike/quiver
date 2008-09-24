#ifndef FILE_BROWSER_EVENT_SOURCE_H
#define FILE_BROWSER_EVENT_SOURCE_H

#include "AbstractEventSource.h"
#include "BrowserEvent.h"

class BrowserEventSource : public virtual AbstractEventSource
{
public:
	virtual ~BrowserEventSource(){};

	typedef boost::signal<void (BrowserEventPtr)> BrowserSignal;
	/*
	typedef boost::signal<void (BrowserEventPtr)> BrowserSelectionChangedSignal;
	typedef boost::signal<void (BrowserEventPtr)> BrowserCursorChangedSignal;
	typedef boost::signal<void (BrowserEventPtr)> BrowserItemActivatedSignal;
	*/
	void AddEventHandler(IEventHandlerPtr handler);
	//void AddEventHandler(BrowserEventHandlerPtr handler);

	void EmitItemActivatedEvent();
	void EmitSelectionChangedEvent();
	void EmitCursorChangedEvent();
private:
	BrowserSignal m_sigSelectionChanged;
	BrowserSignal m_sigCursorChanged;
	BrowserSignal m_sigItemActivated;
	
	
};

#endif

