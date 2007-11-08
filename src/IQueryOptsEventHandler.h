#ifndef IQUERYOPTS_EVENT_HANDLER_H
#define IQUERYOPTS_EVENT_HANDLER_H

#include "IEventHandler.h"
#include "QueryOptsEvent.h"

class IQueryOptsEventHandler : public IEventHandler
{
public:
	//virtual void HandleSelectionChanged(QueryOptsEventPtr event) = 0;
	virtual ~IQueryOptsEventHandler(){};
};

typedef boost::shared_ptr<IQueryOptsEventHandler> IQueryOptsEventHandlerPtr;

#endif
