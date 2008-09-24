#ifndef FILE_PREFERENCES_EVENT_H
#define FILE_PREFERENCES_EVENT_H

#include "EventBase.h"
#include <string>

class PreferencesEvent : public EventBase
{
public:
	typedef enum 
	{
		PREFERENCE_STRING,
		PREFERENCE_LOCALE_STRING,
		PREFERENCE_BOOL,
		PREFERENCE_INT,
	} PreferenceType;

	typedef enum 
	{
		PREFERENCE_ADDED,
		PREFERENCE_REMOVED,
		PREFERENCE_CHANGED,
	} PreferencesEventType;

	PreferencesEvent(IEventSourcePtr src, PreferencesEventType eventType, std::string s, std::string k, std::string o, std::string n) : EventBase(src), m_strSection(s), m_strKey(k), m_strOldString(o), m_strNewString(n), m_PreferencesEventType(eventType)
	{
		m_PrefType = PREFERENCE_STRING;
		
	};

	PreferencesEvent(IEventSourcePtr src, PreferencesEventType eventType, std::string s, std::string k, std::string l, std::string o, std::string n) : EventBase(src), m_strSection(s), m_strKey(k), m_strLocale(l), m_strOldString(o), m_strNewString(n), m_PreferencesEventType(eventType)
	{
		m_PrefType = PREFERENCE_LOCALE_STRING;
	};
	
	PreferencesEvent(IEventSourcePtr src, PreferencesEventType eventType, std::string s, std::string k, bool o, bool n) : EventBase(src), m_strSection(s), m_strKey(k), m_bOldBool(o), m_bNewBool(n), m_PreferencesEventType(eventType)
	{
		m_PrefType = PREFERENCE_BOOL;
		
	};

	PreferencesEvent(IEventSourcePtr src, PreferencesEventType eventType, std::string s, std::string k, int o, int n) : EventBase(src), m_strSection(s), m_strKey(k), m_iOldInt(o), m_iNewInt(n), m_PreferencesEventType(eventType)
	{
		m_PrefType = PREFERENCE_INT;
		
	};

	const std::string& GetSection() const {return m_strSection;};
	const std::string& GetKey() const {return m_strKey;};
	const std::string& GetLocale() const {return m_strLocale;};

	const std::string& GetOldString() const {return m_strOldString;};
	const std::string& GetNewString() const {return m_strNewString;};

	int GetOldInteger() const {return m_iOldInt;};
	int GetNewInteger() const {return m_iNewInt;};

	bool GetOldBoolean() const {return m_bOldBool;};
	bool GetNewBoolean() const {return m_bNewBool;};
	
	PreferencesEventType GetEventType(){return m_PreferencesEventType;};
	PreferenceType GetPrefType(){return m_PrefType;};
	

private:
	std::string m_strSection;
	std::string m_strKey;
	std::string m_strLocale;
	std::string m_strOldString;
	std::string m_strNewString;
	bool m_bOldBool;
	bool m_bNewBool;
	int  m_iOldInt;
	int m_iNewInt;
	PreferenceType m_PrefType;
	
	PreferencesEventType m_PreferencesEventType;
};

typedef boost::shared_ptr<PreferencesEvent> PreferencesEventPtr;


#endif

