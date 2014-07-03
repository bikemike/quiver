#include "ImageListEventSource.h"
#include "IImageListEventHandler.h"

void ImageListEventSource::AddEventHandler(IEventHandlerPtr handler)
{

	IImageListEventHandlerPtr h = boost::static_pointer_cast<IImageListEventHandler>(handler);
	
	boost::signals2::connection c = m_sigContentsChangedPtr->connect( boost::bind(&IImageListEventHandler::HandleContentsChanged,h,_1) );
	MapConnection(handler,c);
	
	c = m_sigCurrentIndexChangedPtr->connect( boost::bind(&IImageListEventHandler::HandleCurrentIndexChanged,h,_1) );
	MapConnection(handler,c);

	c = m_sigItemAddedPtr->connect( boost::bind(&IImageListEventHandler::HandleItemAdded,h,_1) );
	MapConnection(handler,c);

	c = m_sigItemRemovedPtr->connect( boost::bind(&IImageListEventHandler::HandleItemRemoved,h,_1) );
	MapConnection(handler,c);

	c = m_sigItemChangedPtr->connect( boost::bind(&IImageListEventHandler::HandleItemChanged,h,_1) );
	MapConnection(handler,c);
}

void ImageListEventSource::EmitContentsChangedEvent()
{
	ImageListEventPtr event( new ImageListEvent(shared_from_this(),ImageListEvent::CONTENTS_CHANGED,0) );
	(*m_sigContentsChangedPtr)(event);
	
}

void ImageListEventSource::EmitCurrentIndexChangedEvent(unsigned int iIndex, unsigned int iOldIndex)
{
	ImageListEventPtr event( new ImageListEvent(shared_from_this(),ImageListEvent::CURRENT_INDEX_CHANGED,iIndex) );
	event->SetOldIndex(iOldIndex);
	(*m_sigCurrentIndexChangedPtr)(event);
	
}

void ImageListEventSource::EmitItemAddedEvent(unsigned int iIndex)
{
	ImageListEventPtr event( new ImageListEvent(shared_from_this(),ImageListEvent::ITEM_ADDED,iIndex) );
	(*m_sigItemAddedPtr)(event);
}

void ImageListEventSource::EmitItemRemovedEvent(unsigned int iIndex)
{
	ImageListEventPtr event( new ImageListEvent(shared_from_this(),ImageListEvent::ITEM_REMOVED,iIndex) );
	(*m_sigItemRemovedPtr)(event);
}

void ImageListEventSource::EmitItemChangedEvent(unsigned int iIndex)
{
	ImageListEventPtr event( new ImageListEvent(shared_from_this(),ImageListEvent::ITEM_CHANGED,iIndex) );
	(*m_sigItemChangedPtr)(event);
}

