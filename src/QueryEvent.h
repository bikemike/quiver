#ifndef FILE_QUERY_EVENT_H
#define FILE_QUERY_EVENT_H

#include "EventBase.h"

class QueryEvent : public EventBase
{
public:
	QueryEvent(IEventSource* src) : EventBase(src){};
private:
};

typedef boost::shared_ptr<QueryEvent> QueryEventPtr;


#endif

