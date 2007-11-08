#ifndef QUERYOPTS_EVENT_H
#define QUERYOPTS_EVENT_H

#include "EventBase.h"

class QueryOptsEvent : public EventBase
{
public:
	QueryOptsEvent(IEventSource* src) : EventBase(src){};
private:
};

typedef boost::shared_ptr<QueryOptsEvent> QueryOptsEventPtr;


#endif

