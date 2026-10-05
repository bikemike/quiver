#include "AbstractEventSource.h"

class HandlerConnectionMapDeleter
{
public:
	void operator()(AbstractEventSource::HandlerConnectionMap *m_pMapConnections);
};


void HandlerConnectionMapDeleter::operator()(AbstractEventSource::HandlerConnectionMap *m_pMapConnections)
{
	AbstractEventSource::HandlerConnectionMap::iterator itr;

	for (itr = m_pMapConnections->begin(); itr != m_pMapConnections->end(); ++itr)
	{
		itr->second.disconnect();
	}
	m_pMapConnections->clear();

	delete m_pMapConnections;
	
}



AbstractEventSource::AbstractEventSource()
{
	HandlerConnectionMapDeleter d;
	HandlerConnectionMapPtr ptr (new HandlerConnectionMap(),d);
	m_mapConnectionsPtr = ptr;
}

AbstractEventSource::~AbstractEventSource()
{

}

void AbstractEventSource::UnblockHandler(IEventHandlerPtr handler)
{
	std::lock_guard<std::recursive_mutex> lock(m_eventSourceMutex);
	HandlerConnectionMapIteratorPair p;
	HandlerConnectionMap::iterator itr;
	p = m_mapConnectionsPtr->equal_range(handler);
	for (itr = p.first; itr != p.second; ++itr)
	{	
		auto blockerItr = m_mapConnectionBlockerMap.find(itr->second);
		if (blockerItr != m_mapConnectionBlockerMap.end())
		{
			blockerItr->second->unblock();
		}
	}
}
void AbstractEventSource::BlockHandler(IEventHandlerPtr handler)
{
	std::lock_guard<std::recursive_mutex> lock(m_eventSourceMutex);
	HandlerConnectionMapIteratorPair p;
	HandlerConnectionMap::iterator itr;
	p = m_mapConnectionsPtr->equal_range(handler);
	for (itr = p.first; itr != p.second; ++itr)
	{	
		auto blockerItr = m_mapConnectionBlockerMap.find(itr->second);
		if (blockerItr != m_mapConnectionBlockerMap.end())
		{
			blockerItr->second->block();
		}
	}
}
void AbstractEventSource::RemoveEventHandler(IEventHandlerPtr handler)
{
	std::lock_guard<std::recursive_mutex> lock(m_eventSourceMutex);
	HandlerConnectionMapIteratorPair p;
	HandlerConnectionMap::iterator itr;
	p = m_mapConnectionsPtr->equal_range(handler);
	for (itr = p.first; itr != p.second; ++itr)
	{	
		m_mapConnectionBlockerMap.erase(itr->second);
		itr->second.disconnect();
	}
	m_mapConnectionsPtr->erase(p.first, p.second);
}


