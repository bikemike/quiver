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
	HandlerConnectionMapIteratorPair p;
	HandlerConnectionMap::iterator itr;
	p = m_mapConnectionsPtr->equal_range(handler);
	for (itr = p.first; itr != p.second; ++itr)
	{	
		m_mapConnectionBlockerMap[itr->second]->unblock();
	}
}
void AbstractEventSource::BlockHandler(IEventHandlerPtr handler)
{
	HandlerConnectionMapIteratorPair p;
	HandlerConnectionMap::iterator itr;
	p = m_mapConnectionsPtr->equal_range(handler);
	for (itr = p.first; itr != p.second; ++itr)
	{	
		m_mapConnectionBlockerMap[itr->second]->block();

	}
}
void AbstractEventSource::RemoveEventHandler(IEventHandlerPtr handler)
{
	HandlerConnectionMapIteratorPair p;
	HandlerConnectionMap::iterator itr;
	p = m_mapConnectionsPtr->equal_range(handler);
	for (itr = p.first; itr != p.second; ++itr)
	{	
		itr->second.disconnect();
	}
	m_mapConnectionsPtr->erase(p.first, p.second);
}


