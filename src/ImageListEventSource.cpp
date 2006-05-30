#include "ImageListEventSource.h"
#include "IImageListEventHandler.h"

void ImageListEventSource::AddEventHandler(IEventHandlerPtr handler)
{

	IImageListEventHandlerPtr h = boost::static_pointer_cast<IImageListEventHandler>(handler);
	
	boost::signals::connection c = m_sigCurrentItemChangedPtr->connect( boost::bind(&IImageListEventHandler::HandleCurrentItemChanged,h,_1) );
	MapConnection(handler,c);

	c = m_sigItemsAddedPtr->connect( boost::bind(&IImageListEventHandler::HandleItemsAdded,h,_1) );
	MapConnection(handler,c);

	c = m_sigItemsRemovedPtr->connect( boost::bind(&IImageListEventHandler::HandleItemsRemoved,h,_1) );
	MapConnection(handler,c);
}

void ImageListEventSource::EmitCurrentItemChangedEvent()
{
	EventBasePtr n( new EventBase(this) );
	(*m_sigCurrentItemChangedPtr)(n);
	
}

void ImageListEventSource::EmitItemsAddedEvent()
{
	EventBasePtr n( new EventBase(this) );
	(*m_sigItemsAddedPtr)(n);
}

void ImageListEventSource::EmitItemsRemovedEvent()
{
	EventBasePtr n( new EventBase(this) );
	(*m_sigItemsRemovedPtr)(n);
}

