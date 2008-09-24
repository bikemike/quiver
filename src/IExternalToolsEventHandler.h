#ifndef FILE_IEXTERNAL_TOOLS_EVENT_HANDLER_H
#define FILE_IEXTERNAL_TOOLS_EVENT_HANDLER_H


#include "IEventHandler.h"
#include "ExternalToolsEvent.h"

class IExternalToolsEventHandler : public IEventHandler
{
public:
	
	virtual void HandleExternalToolChanged(ExternalToolsEventPtr event) = 0;

	virtual ~IExternalToolsEventHandler(){};
};

typedef boost::shared_ptr< IExternalToolsEventHandler> IExternalToolsEventHandlerPtr;

#endif /*FILE_IEXTERNAL_TOOLS_EVENT_HANDLER_H*/
