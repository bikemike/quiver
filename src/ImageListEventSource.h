#ifndef FILE_IMAGE_LIST_EVENT_SOURCE_H
#define FILE_IMAGE_LIST_EVENT_SOURCE_H

#include "AbstractEventSource.h"
#include "EventBase.h"
#include "BrowserEvent.h"

//#include "ImageListEvent.h"

class ImageListEventSource : public virtual AbstractEventSource
{

public:
	virtual ~ImageListEventSource(){};

//	typedef boost::signal<void (ImageListEventPtr)> ImageListSignal;
	typedef boost::signal<void (EventBasePtr)> ImageListSignal;
	typedef boost::shared_ptr<ImageListSignal> ImageListSignalPtr;

	void AddEventHandler(IEventHandlerPtr handler);

	void EmitCurrentItemChangedEvent();
	void EmitItemsAddedEvent();
	void EmitItemsRemovedEvent();
	
private:
	ImageListSignalPtr m_sigCurrentItemChangedPtr;
	ImageListSignalPtr m_sigItemsAddedPtr; // affected range
	ImageListSignalPtr m_sigItemsRemovedPtr; // affected range

};


#endif /*FILE_IMAGE_LIST_EVENT_SOURCE_H*/
