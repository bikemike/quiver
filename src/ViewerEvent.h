#ifndef FILE_VIEWER_EVENT_H
#define FILE_VIEWER_EVENT_H

#include "EventBase.h"

class ViewerEvent : public EventBase
{
public:
	ViewerEvent(IEventSource* src) : EventBase(src){};
private:
};

typedef boost::shared_ptr<ViewerEvent> ViewerEventPtr;


#endif

