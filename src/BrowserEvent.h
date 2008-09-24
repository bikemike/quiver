#ifndef FILE_BROWSER_EVENT_H
#define FILE_BROWSER_EVENT_H

#include "EventBase.h"

class BrowserEvent : public EventBase
{
public:
	BrowserEvent(IEventSourcePtr src) : EventBase(src){};
private:
};

typedef boost::shared_ptr<BrowserEvent> BrowserEventPtr;


#endif

