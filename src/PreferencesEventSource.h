#ifndef FILE_PREFERENCES_EVENT_SOURCE_H
#define FILE_PREFERENCES_EVENT_SOURCE_H

#include "AbstractEventSource.h"
#include "EventBase.h"

#include "PreferencesEvent.h"

class PreferencesEventSource : public virtual AbstractEventSource
{

public:
	typedef boost::signal<void (PreferencesEventPtr)> PreferencesSignal;
	typedef boost::shared_ptr<PreferencesSignal> PreferencesSignalPtr;

	PreferencesEventSource() : m_sigPreferenceChangedPtr(new PreferencesSignal())
	{};
	
	virtual ~PreferencesEventSource(){};

	void AddEventHandler(IEventHandlerPtr handler);

	void EmitPreferenceChanged(PreferencesEvent::PreferencesEventType type, std::string strSection, std::string strKey, std::string strOldVal, std::string strNewVal);
	void EmitPreferenceChanged(PreferencesEvent::PreferencesEventType type, std::string strSection, std::string strKey, std::string strLocale, std::string strOldVal, std::string strNewVal);
	void EmitPreferenceChanged(PreferencesEvent::PreferencesEventType type, std::string strSection, std::string strKey, bool bOldVal, bool bNewVal);
	void EmitPreferenceChanged(PreferencesEvent::PreferencesEventType type, std::string strSection, std::string strKey, int iOldVal, int iNewVal);
	
private:
	PreferencesSignalPtr m_sigPreferenceChangedPtr;

};


#endif /*FILE_PREFERENCES_EVENT_SOURCE_H*/
