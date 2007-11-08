#ifndef QUERYOPTS_EVENT_SOURCE_H
#define QUERYOPTS_EVENT_SOURCE_H

#include "AbstractEventSource.h"
#include "QueryOptsEvent.h"

class QueryOptsEventSource : public virtual AbstractEventSource
{
public:
	virtual ~QueryOptsEventSource(){};

	typedef boost::signal<void (QueryOptsEventPtr)> QueryOptsSignal;

	void AddEventHandler(IEventHandlerPtr handler);

//	void EmitSelectionChangedEvent();
private:
	QueryOptsSignal m_sigSelectionChanged;
	
};

#endif
