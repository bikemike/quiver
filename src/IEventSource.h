#ifndef FILE_IEVENT_SOURCE_H
#define FILE_IEVENT_SOURCE_H

#include <boost/bind.hpp>

#include "IEventHandler.h"

class IEventSource
{
public:
	virtual ~IEventSource() {};
	virtual void AddEventHandler(IEventHandlerPtr handler) = 0;
	virtual void BlockHandler(IEventHandlerPtr handler) = 0;
	virtual void UnblockHandler(IEventHandlerPtr handler) = 0;
	virtual void RemoveEventHandler(IEventHandlerPtr handler) = 0;

private:

};


#endif


