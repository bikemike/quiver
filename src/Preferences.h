#ifndef FILE_PREFERENCES_H
#define FILE_PREFERENCES_H

#include <boost/shared_ptr.hpp>
#include <list>
#include <string>
#include "PreferencesEventSource.h"

class Preferences;
class INIFile;

extern gchar g_szConfigFilePath[];

typedef boost::shared_ptr<Preferences> PreferencesPtr;
typedef boost::shared_ptr<INIFile> INIFilePtr;

class Preferences : public virtual PreferencesEventSource
{
public:

	static PreferencesPtr GetInstance();

	~Preferences();

	std::list<std::string> GetSections();
	std::list<std::string> GetKeys(std::string section);
	bool HasSection(std::string section);
	bool HasKey(std::string section, std::string key);
	std::string GetValue(std::string section, std::string key);
	std::string GetString(std::string section, std::string key);
	std::string GetLocaleString(std::string section, std::string key, std::string locale);
	bool GetBoolean(std::string section, std::string key, bool default_value = false);
	int GetInteger(std::string section, std::string key, int default_value = 0);
	std::list<std::string> GetStringList(std::string section, std::string key);
	std::list<std::string> GetLocaleStringList(std::string section, std::string key, std::string locale);
	std::list<bool> GetBooleanList(std::string section, std::string key);
	std::list<int> GetIntegerList(std::string section, std::string key);
	void SetValue(std::string section, std::string key, std::string value);
	void SetString(std::string section, std::string key, std::string value);
	void SetLocaleString(std::string section, std::string key, std::string locale, std::string value);
	void SetBoolean(std::string section, std::string key, bool value);
	void SetInteger(std::string section, std::string key, int value);
	void SetStringList(std::string section, std::string key, std::list<std::string> value_list);
	void SetStringLocaleList(std::string section, std::string key, std::string locale, std::list<std::string> value_list);
	void SetBooleanList(std::string section, std::string key, std::list<bool> value_list);
	void SetIntegerList(std::string section, std::string key, std::list<int> value_list);
	void RemoveSection(std::string section);
	void RemoveKey(std::string section,std::string key);

private:
	Preferences();

	static PreferencesPtr c_pPreferencesPtr;
	bool m_bModified;
	GKeyFile *m_KeyFile;
};


#endif // FILE_PREFERENCES_H

