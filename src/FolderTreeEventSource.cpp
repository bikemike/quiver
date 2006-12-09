#include "FolderTreeEventSource.h"
#include "IFolderTreeEventHandler.h"

void FolderTreeEventSource::AddEventHandler(IEventHandlerPtr handler)
{
	IFolderTreeEventHandlerPtr h = boost::static_pointer_cast<IFolderTreeEventHandler>(handler);
	
	boost::signals::connection c = m_sigSelectionChanged.connect( boost::bind(&IFolderTreeEventHandler::HandleSelectionChanged,h,_1) );
	MapConnection(handler,c);

}

void FolderTreeEventSource::EmitSelectionChangedEvent()
{
	FolderTreeEventPtr n( new FolderTreeEvent(this) );
	m_sigSelectionChanged(n);
}

