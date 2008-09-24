#ifndef FILE_EVENT_BASE_H
#define FILE_EVENT_BASE_H

#include "IEvent.h"
#include "IEventSource.h"

class EventBase : public IEvent
{
public:
	EventBase(IEventSourcePtr src) : m_IEventSourcePtr(src) { }
	virtual ~EventBase(){}
	virtual IEventSourcePtr GetSource() const {return m_IEventSourcePtr;}

private:
	IEventSourcePtr m_IEventSourcePtr;

};

typedef boost::shared_ptr<EventBase> EventBasePtr;

#endif

