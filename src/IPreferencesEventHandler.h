#ifndef FILE_IPREFERENCES_EVENT_HANDLER_H
#define FILE_IPREFERENCES_EVENT_HANDLER_H


#include "IEventHandler.h"
#include "PreferencesEvent.h"

class IPreferencesEventHandler : public IEventHandler
{
public:
	
	virtual void HandlePreferenceChanged(PreferencesEventPtr event) = 0;

	virtual ~IPreferencesEventHandler(){};
};

typedef boost::shared_ptr< IPreferencesEventHandler> IPreferencesEventHandlerPtr;

#endif /*FILE_IPREFERENCES_EVENT_HANDLER_H*/
