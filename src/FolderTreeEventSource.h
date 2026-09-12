#ifndef FILE_FOLDER_TREE_EVENT_SOURCE_H
#define FILE_FOLDER_TREE_EVENT_SOURCE_H

#include <list>
#include <string>

#include "AbstractEventSource.h"
#include "FolderTreeEvent.h"

class FolderTreeEventSource : public virtual AbstractEventSource
{
public:
	virtual ~FolderTreeEventSource(){};

	typedef boost::signals2::signal<void (FolderTreeEventPtr)> FolderTreeSignal;

	void AddEventHandler(IEventHandlerPtr handler);

	void EmitSelectionChangedEvent();
	void EmitBookmarkOpenEvent(const std::list<std::string>& uris, bool bRecursive);
private:
	FolderTreeSignal m_sigSelectionChanged;
	
};

#endif
