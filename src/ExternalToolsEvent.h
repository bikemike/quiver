#ifndef FILE_EXTERNAL_TOOLS_EVENT_H
#define FILE_EXTERNAL_TOOLS_EVENT_H

#include "EventBase.h"
#include <string>

class ExternalToolsEvent : public EventBase
{
public:
	typedef enum 
	{
		EXTERNAL_TOOL_ADDED,
		EXTERNAL_TOOL_REMOVED,
		EXTERNAL_TOOL_CHANGED,
	} ExternalToolsEventType;

	ExternalToolsEvent(IEventSourcePtr src, ExternalToolsEventType eventType) : EventBase(src), m_ExternalToolsEventType(eventType) { };

	
	ExternalToolsEventType GetEventType(){return m_ExternalToolsEventType;};
	

private:
	
	ExternalToolsEventType m_ExternalToolsEventType;
};

typedef boost::shared_ptr<ExternalToolsEvent> ExternalToolsEventPtr;


#endif

