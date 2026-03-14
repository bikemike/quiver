#ifndef FILE_IMAGE_LIST_EVENT_H
#define FILE_IMAGE_LIST_EVENT_H

#include "EventBase.h"

class ImageListEvent : public EventBase
{
public:
	typedef enum 
	{
		ITEM_ADDED,
		ITEM_REMOVED,
		ITEM_CHANGED,
		CURRENT_INDEX_CHANGED,
		CONTENTS_CHANGED
	} ImageListEventType;

	ImageListEvent(IEventSourcePtr src, ImageListEventType eventType, unsigned int iIndex) : EventBase(src), m_iIndex(iIndex), m_ImageListEventType(eventType)
	{
		
	};
	
	unsigned int GetIndex(){return m_iIndex;};
	unsigned int GetOldIndex(){return m_iIndexOld;};
	void SetOldIndex(unsigned int old){m_iIndexOld = old;}
	ImageListEventType GetType(){return m_ImageListEventType;};
	

private:
	unsigned int m_iIndex;
	unsigned int m_iIndexOld;
	ImageListEventType m_ImageListEventType;
};

typedef boost::shared_ptr<ImageListEvent> ImageListEventPtr;


#endif

