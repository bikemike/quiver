#ifndef FILE_IEVENT_SOURCE_H
#define FILE_IEVENT_SOURCE_H

#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>

#include "IEventHandler.h"

class IEventSource : public boost::enable_shared_from_this<IEventSource>
{
public:
	virtual ~IEventSource() {};
	virtual void AddEventHandler(IEventHandlerPtr handler) = 0;
	virtual void BlockHandler(IEventHandlerPtr handler) = 0;
	virtual void UnblockHandler(IEventHandlerPtr handler) = 0;
	virtual void RemoveEventHandler(IEventHandlerPtr handler) = 0;

private:

};

typedef boost::shared_ptr<IEventSource> IEventSourcePtr;

#endif


