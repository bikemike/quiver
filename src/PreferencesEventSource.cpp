#include "PreferencesEventSource.h"
#include "IPreferencesEventHandler.h"

void PreferencesEventSource::AddEventHandler(IEventHandlerPtr handler)
{

	IPreferencesEventHandlerPtr h = boost::static_pointer_cast<IPreferencesEventHandler>(handler);
	
	boost::signals::connection c = m_sigPreferenceChangedPtr->connect( boost::bind(&IPreferencesEventHandler::HandlePreferenceChanged,h,_1) );
	MapConnection(handler,c);
	
}

void PreferencesEventSource::EmitPreferenceChanged(PreferencesEvent::PreferencesEventType type, std::string strSection, std::string strKey, std::string strOldVal, std::string strNewVal)
{
	PreferencesEventPtr event( new PreferencesEvent(this,type,strSection,strKey,strOldVal,strNewVal) );
	(*m_sigPreferenceChangedPtr)(event);
}

void PreferencesEventSource::EmitPreferenceChanged(PreferencesEvent::PreferencesEventType type, std::string strSection, std::string strKey, std::string strLocale, std::string strOldVal, std::string strNewVal)
{
	PreferencesEventPtr event( new PreferencesEvent(this,type,strSection,strKey,strLocale, strOldVal,strNewVal) );
	(*m_sigPreferenceChangedPtr)(event);
}

void PreferencesEventSource::EmitPreferenceChanged(PreferencesEvent::PreferencesEventType type, std::string strSection, std::string strKey, bool bOldVal, bool bNewVal)
{
	PreferencesEventPtr event( new PreferencesEvent(this,type,strSection,strKey,bOldVal,bNewVal) );
	(*m_sigPreferenceChangedPtr)(event);
}

void PreferencesEventSource::EmitPreferenceChanged(PreferencesEvent::PreferencesEventType type, std::string strSection, std::string strKey, int iOldVal, int iNewVal)
{
	PreferencesEventPtr event( new PreferencesEvent(this,type,strSection,strKey,iOldVal,iNewVal) );
	(*m_sigPreferenceChangedPtr)(event);
}
