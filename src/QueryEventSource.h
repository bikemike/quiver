#ifndef FILE_QUERY_EVENT_SOURCE_H
#define FILE_QUERY_EVENT_SOURCE_H

#include "AbstractEventSource.h"
#include "QueryEvent.h"

class QueryEventSource : public virtual AbstractEventSource
{
public:
	virtual ~QueryEventSource(){};

	typedef boost::signal<void (QueryEventPtr)> QuerySignal;
	/*
	typedef boost::signal<void (QueryEventPtr)> QuerySelectionChangedSignal;
	typedef boost::signal<void (QueryEventPtr)> QueryCursorChangedSignal;
	typedef boost::signal<void (QueryEventPtr)> QueryItemActivatedSignal;
	*/
	void AddEventHandler(IEventHandlerPtr handler);
	//void AddEventHandler(QueryEventHandlerPtr handler);

	void EmitItemActivatedEvent();
	void EmitSelectionChangedEvent();
	void EmitCursorChangedEvent();
private:
	QuerySignal m_sigSelectionChanged;
	QuerySignal m_sigCursorChanged;
	QuerySignal m_sigItemActivated;
	
	
};

#endif
