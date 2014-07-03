#ifndef FILE_IMAGE_LIST_EVENT_SOURCE_H
#define FILE_IMAGE_LIST_EVENT_SOURCE_H

#include "AbstractEventSource.h"
#include "EventBase.h"
#include "BrowserEvent.h"

#include "ImageListEvent.h"

class ImageListEventSource : public virtual AbstractEventSource
{

public:
	typedef boost::signals2::signal<void (ImageListEventPtr)> ImageListSignal;
	typedef boost::shared_ptr<ImageListSignal> ImageListSignalPtr;

	ImageListEventSource() : m_sigContentsChangedPtr(new ImageListSignal()),
									m_sigCurrentIndexChangedPtr(new ImageListSignal()),
									m_sigItemAddedPtr(new ImageListSignal()),
									m_sigItemRemovedPtr(new ImageListSignal()),
									m_sigItemChangedPtr(new ImageListSignal())
	{};
	
	virtual ~ImageListEventSource(){};

	void AddEventHandler(IEventHandlerPtr handler);

	void EmitContentsChangedEvent();
	void EmitCurrentIndexChangedEvent(unsigned int iIndex, unsigned int iOldIndex);
	void EmitItemAddedEvent(unsigned int iIndex);
	void EmitItemRemovedEvent(unsigned int iIndex);
	void EmitItemChangedEvent(unsigned int iIndex);
	
private:
	ImageListSignalPtr m_sigContentsChangedPtr;
	ImageListSignalPtr m_sigCurrentIndexChangedPtr;
	ImageListSignalPtr m_sigItemAddedPtr; // affected range
	ImageListSignalPtr m_sigItemRemovedPtr; // affected range
	ImageListSignalPtr m_sigItemChangedPtr; // affected range

};


#endif /*FILE_IMAGE_LIST_EVENT_SOURCE_H*/
