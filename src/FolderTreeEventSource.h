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

	/* bPreserveCurrentIndex defaults to true: the checked set grew, so the
	 * image list should keep the viewer's place.  Pass false from the paths
	 * that clear the other checkboxes first (a plain click on a row), which
	 * navigate to a different folder and should land on its first item. */
	void EmitSelectionChangedEvent(bool bPreserveCurrentIndex = true);
	void EmitBookmarkOpenEvent(const std::list<std::string>& uris, bool bRecursive);
private:
	FolderTreeSignal m_sigSelectionChanged;
	
};

#endif
