#ifndef FILE_IEVENT_HANDLER_H
#define FILE_IEVENT_HANDLER_H

#include <boost/shared_ptr.hpp>

class IEventHandler
{
public:
	//virtual void HandleEvent(IEventPtr event) = 0;
};

typedef boost::shared_ptr<IEventHandler> IEventHandlerPtr;


#endif


