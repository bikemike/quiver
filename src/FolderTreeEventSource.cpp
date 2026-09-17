#include "FolderTreeEventSource.h"
#include "IFolderTreeEventHandler.h"
using namespace boost::placeholders;

void FolderTreeEventSource::AddEventHandler(IEventHandlerPtr handler)
{
	IFolderTreeEventHandlerPtr h = boost::static_pointer_cast<IFolderTreeEventHandler>(handler);
	
	boost::signals2::connection c = m_sigSelectionChanged.connect( boost::bind(&IFolderTreeEventHandler::HandleSelectionChanged,h,_1) );
	MapConnection(handler,c);

}

void FolderTreeEventSource::EmitSelectionChangedEvent()
{
	FolderTreeEventPtr n( new FolderTreeEvent(shared_from_this()) );
	m_sigSelectionChanged(n);
}

void FolderTreeEventSource::EmitBookmarkOpenEvent(const std::list<std::string>& uris, bool bRecursive)
{
	FolderTreeEventPtr n( new FolderTreeEvent(shared_from_this()) );
	n->SetURIs(uris);
	n->SetRecursive(bRecursive);
	m_sigSelectionChanged(n);
}

