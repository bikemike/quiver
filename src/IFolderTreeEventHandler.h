#ifndef FILE_IBROWSER_EVENT_HANDLER_H
#define FILE_IBROWSER_EVENT_HANDLER_H

#include "IEventHandler.h"
#include "FolderTreeEvent.h"

class IFolderTreeEventHandler : public IEventHandler
{
public:
	virtual void HandleSelectionChanged(FolderTreeEventPtr event) = 0;
	virtual ~IFolderTreeEventHandler(){};
};

typedef boost::shared_ptr<IFolderTreeEventHandler> IFolderTreeEventHandlerPtr;

#endif
