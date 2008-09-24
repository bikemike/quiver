#ifndef FILE_EXTERNAL_TOOLS_EVENT_SOURCE_H
#define FILE_EXTERNAL_TOOLS_EVENT_SOURCE_H

#include "AbstractEventSource.h"
#include "EventBase.h"

#include "ExternalToolsEvent.h"

class ExternalToolsEventSource : public virtual AbstractEventSource
{

public:
	typedef boost::signal<void (ExternalToolsEventPtr)> ExternalToolsSignal;
	typedef boost::shared_ptr<ExternalToolsSignal> ExternalToolsSignalPtr;

	ExternalToolsEventSource() : m_sigExternalToolChangedPtr(new ExternalToolsSignal())
	{};
	
	virtual ~ExternalToolsEventSource(){};

	void     AddEventHandler(IEventHandlerPtr handler);

	void     EmitExternalToolChangedEvent(
	             ExternalToolsEvent::ExternalToolsEventType type);
	
private:
	ExternalToolsSignalPtr m_sigExternalToolChangedPtr;

};


#endif /*FILE_EXTERNAL_TOOLS_EVENT_SOURCE_H*/
