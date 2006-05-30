#ifndef FILE_EVENT_BASE_H
#define FILE_EVENT_BASE_H

#include "IEvent.h"
#include "IEventSource.h"

class EventBase : public IEvent
{
public:
	EventBase(IEventSource* src) { source = src;};
	virtual ~EventBase(){};
	virtual IEventSource* GetSource(){return source;};

private:
	IEventSource* source;

};

typedef boost::shared_ptr<EventBase> EventBasePtr;

#endif

