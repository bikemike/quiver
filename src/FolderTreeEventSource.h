#ifndef FILE_FOLDER_TREE_EVENT_SOURCE_H
#define FILE_FOLDER_TREE_EVENT_SOURCE_H

#include "AbstractEventSource.h"
#include "FolderTreeEvent.h"

class FolderTreeEventSource : public virtual AbstractEventSource
{
public:
	virtual ~FolderTreeEventSource(){};

	typedef boost::signals2::signal<void (FolderTreeEventPtr)> FolderTreeSignal;

	void AddEventHandler(IEventHandlerPtr handler);

	void EmitSelectionChangedEvent();
private:
	FolderTreeSignal m_sigSelectionChanged;
	
};

#endif
