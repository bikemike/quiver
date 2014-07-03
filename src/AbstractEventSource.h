#ifndef FILE_ABSTRACT_EVENT_SOURCE_H
#define FILE_ABSTRACT_EVENT_SOURCE_H

#include <map>
#include <boost/signals2/signal.hpp>
#include <boost/signals2/shared_connection_block.hpp>
#include "IEventSource.h"

class AbstractEventSource : public IEventSource
{

public:
	AbstractEventSource();
	virtual ~AbstractEventSource();
	//typedef boost::signals2<void (IEventPtr)> EventSignal;

	typedef std::multimap<IEventHandlerPtr,boost::signals2::connection> HandlerConnectionMap;
	typedef boost::shared_ptr<HandlerConnectionMap> HandlerConnectionMapPtr;
	typedef std::pair<IEventHandlerPtr,boost::signals2::connection> HandlerConnectionPair;
	typedef std::pair<HandlerConnectionMap::iterator,HandlerConnectionMap::iterator> HandlerConnectionMapIteratorPair;

	typedef boost::shared_ptr<boost::signals2::shared_connection_block> SharedConnectionBlockPtr;
	typedef std::map<boost::signals2::connection, SharedConnectionBlockPtr> ConnectionBlockerMap;

	virtual void AddEventHandler(IEventHandlerPtr handler) = 0;
	virtual void BlockHandler(IEventHandlerPtr handler);
	virtual void UnblockHandler(IEventHandlerPtr handler);
	virtual void RemoveEventHandler(IEventHandlerPtr handler);
protected:
	//virtual void Emit();
	void MapConnection(IEventHandlerPtr h,boost::signals2::connection c)
	{
		m_mapConnectionsPtr->insert(HandlerConnectionPair(h,c));
		SharedConnectionBlockPtr blockerPtr(new boost::signals2::shared_connection_block(c));
		blockerPtr->unblock();
		m_mapConnectionBlockerMap.insert(std::make_pair(c,blockerPtr));
	}

private:
	HandlerConnectionMapPtr m_mapConnectionsPtr;
	ConnectionBlockerMap m_mapConnectionBlockerMap;
};

#endif

