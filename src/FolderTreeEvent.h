#ifndef FILE_FOLDER_TREE_EVENT_H
#define FILE_FOLDER_TREE_EVENT_H

#include "EventBase.h"

class FolderTreeEvent : public EventBase
{
public:
	FolderTreeEvent(IEventSourcePtr src) : EventBase(src){};
private:
};

typedef boost::shared_ptr<FolderTreeEvent> FolderTreeEventPtr;


#endif

