#ifndef FILE_IEVENT_H
#define FILE_IEVENT_H

#include <boost/shared_ptr.hpp>

class IEvent
{
public:
	virtual ~IEvent(){};
	virtual IEventSource * GetSource() = 0;
};


typedef boost::shared_ptr<IEvent> IEventPtr;

#endif

