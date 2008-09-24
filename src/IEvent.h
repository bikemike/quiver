#ifndef FILE_IEVENT_H
#define FILE_IEVENT_H

#include <boost/shared_ptr.hpp>

#include "IEventSource.h"

class IEvent
{
public:
	virtual ~IEvent(){};
	virtual IEventSourcePtr GetSource() const = 0;
};


typedef boost::shared_ptr<IEvent> IEventPtr;

#endif

