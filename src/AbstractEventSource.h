#ifndef FILE_ABSTRACT_EVENT_SOURCE_H
#define FILE_ABSTRACT_EVENT_SOURCE_H

#include <map>
#include <boost/signal.hpp>

#include "IEventSource.h"

class AbstractEventSource : public IEventSource
{

public:
	AbstractEventSource();
	virtual ~AbstractEventSource();
	//typedef boost::signal<void (IEventPtr)> EventSignal;

	typedef std::multimap<IEventHandlerPtr,boost::signals::connection> HandlerConnectionMap;
	typedef boost::shared_ptr<HandlerConnectionMap> HandlerConnectionMapPtr;
	typedef std::pair<IEventHandlerPtr,boost::signals::connection> HandlerConnectionPair;
	typedef std::pair<HandlerConnectionMap::iterator,HandlerConnectionMap::iterator> HandlerConnectionMapIteratorPair;

	virtual void AddEventHandler(IEventHandlerPtr handler) = 0;
	virtual void BlockHandler(IEventHandlerPtr handler);
	virtual void UnblockHandler(IEventHandlerPtr handler);
	virtual void RemoveEventHandler(IEventHandlerPtr handler);
protected:
	//virtual void Emit();
	void MapConnection(IEventHandlerPtr h,boost::signals::connection c){ m_mapConnectionsPtr->insert(HandlerConnectionPair(h,c)); };

private:
	HandlerConnectionMapPtr m_mapConnectionsPtr;
};

#endif

