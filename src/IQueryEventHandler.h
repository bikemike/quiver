#ifndef FILE_IQUERY_EVENT_HANDLER_H
#define FILE_IQUERY_EVENT_HANDLER_H

#include "IEventHandler.h"
#include "QueryEvent.h"

class IQueryEventHandler : public IEventHandler
{
public:
	virtual void HandleItemActivated(QueryEventPtr event) = 0;
	virtual void HandleSelectionChanged(QueryEventPtr event) = 0;
	virtual void HandleCursorChanged(QueryEventPtr event) = 0;
	virtual ~IQueryEventHandler(){};
};

typedef boost::shared_ptr<IQueryEventHandler> IQueryEventHandlerPtr;

#endif
