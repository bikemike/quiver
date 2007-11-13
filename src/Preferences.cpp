
#include <iostream>

#include <glib.h>
#include <glib/gstdio.h>
// stat is needed for mkdir modes
#include <sys/stat.h>
#include "Preferences.h"

using namespace std;

PreferencesPtr Preferences::c_pPreferencesPtr;

Preferences::Preferences()
{
	m_bModified = false;
	GError *error = NULL;
	m_KeyFile = g_key_file_new();
	g_key_file_load_from_file(m_KeyFile,g_szConfigFilePath,(GKeyFileFlags)0/*(G_KEY_FILE_KEEP_COMMENTS|G_KEY_FILE_KEEP_TRANSLATIONS)*/, &error);
}

Preferences::~Preferences()
{
	if (m_bModified)
	{
		GError *error = NULL;
		guint clength;
		gchar *contents = g_key_file_to_data (m_KeyFile, &clength, &error);
	
		error = NULL;
		g_file_set_contents(g_szConfigFilePath,contents,clength,&error);
	
		g_free(contents);
	}
	g_key_file_free(m_KeyFile);


}

PreferencesPtr Preferences::GetInstance()
{
	if (NULL == c_pPreferencesPtr.get())
	{
		PreferencesPtr prefsPtr(new Preferences());
		c_pPreferencesPtr = prefsPtr;
	}
	return c_pPreferencesPtr;
}

std::list<std::string> Preferences::GetSections()
{
	gsize length = 0;
	gchar** groups =  g_key_file_get_groups (m_KeyFile, &length);

	list<string> sections;
	
	if (NULL != groups)
	{
		gsize i = 0;
		for (i = 0; i < length; i++)
		{
			sections.push_back(groups[i]);
		}
		g_strfreev(groups);
	}
	return sections;
}

std::list<std::string> Preferences::GetKeys(std::string section)
{
	GError *error = NULL;
	gsize length = 0;
	gchar** keys =  g_key_file_get_keys (m_KeyFile, section.c_str(),  &length, &error);

	list<string> key_list;
	if (NULL != keys)
	{
		gsize i = 0;
		for (i = 0; i < length; i++)
		{
			key_list.push_back(keys[i]);
		}
		g_strfreev(keys);
	}
	return key_list;
}

bool Preferences::HasSection(std::string section)
{
	bool rval = false;
	if (g_key_file_has_group(m_KeyFile,section.c_str()))
	{
		rval = true;
	}
	return rval;
}

bool Preferences::HasKey(std::string section, std::string key)
{
	GError *error = NULL;
	bool rval = false;
	if (g_key_file_has_key(m_KeyFile,section.c_str(),key.c_str(), &error))
	{
		rval = true;
	}
	return rval;
}

std::string Preferences::GetValue(std::string section, std::string key, std::string default_val /* = "" */)
{
	GError *error = NULL;
	gchar *value;
	string strValue = default_val;

	value = g_key_file_get_value(m_KeyFile,section.c_str(),key.c_str(), &error);

	if (NULL != value)
	{
		strValue = value;
		g_free(value);
	}
	else
	{
		g_key_file_set_value(m_KeyFile,section.c_str(),key.c_str(), default_val.c_str());
	}

	return strValue;
}

std::string Preferences::GetString(std::string section, std::string key, std::string default_val /* = ""*/)
{
	return GetValue(section,key,default_val);
}

std::string Preferences::GetLocaleString(std::string section, std::string key, std::string locale)
{
	GError *error = NULL;
	gchar *value;
	string strValue;

	value = g_key_file_get_locale_string(m_KeyFile,section.c_str(),key.c_str(), locale.c_str(), &error);

	if (NULL != value)
	{
		strValue = value;
		g_free(value);
	}

	return strValue;
}

bool Preferences::GetBoolean(std::string section, std::string key, bool default_value /*= false*/)
{
	GError *error = NULL;
	gboolean value;
	bool bValue = default_value;

	value = g_key_file_get_boolean(m_KeyFile,section.c_str(),key.c_str(), &error);
	
	if (NULL == error)
	{
		bValue = value ? true : false;
	}
	else
	{
		g_key_file_set_boolean(m_KeyFile,section.c_str(),key.c_str(), bValue ? TRUE : FALSE);
	}

	return bValue;
}

int Preferences::GetInteger(std::string section, std::string key, int default_value /*= 0*/)
{
	GError *error = NULL;
	gint value = default_value;

	gint tmpvalue = g_key_file_get_integer(m_KeyFile,section.c_str(),key.c_str(), &error);
	
	if (NULL == error)
	{
		value = tmpvalue;
	}
	else
	{
		g_key_file_set_integer(m_KeyFile,section.c_str(),key.c_str(), value);
	}

	return value;
}

std::list<std::string> Preferences::GetStringList(std::string section, std::string key)
{
	GError *error = NULL;
	gsize nstrings = 0;
	gchar **value;
	list<string> string_list;

	value = g_key_file_get_string_list(m_KeyFile,section.c_str(),key.c_str(), &nstrings, &error);

	if (NULL != value)
	{
		for (unsigned int i = 0; i < nstrings ; ++i)
		{
			string_list.push_back(value[i]);
		}
		g_strfreev(value);
	}

	return string_list;
}

std::list<std::string> Preferences::GetLocaleStringList(std::string section, std::string key, std::string locale)
{
	list<string> string_list;
	return string_list;
}

std::list<bool> Preferences::GetBooleanList(std::string section, std::string key)
{
	GError *error = NULL;
	gsize nvals = 0;
	gboolean* value;
	list<bool> bool_list;

	value = g_key_file_get_boolean_list(m_KeyFile,section.c_str(),key.c_str(), &nvals, &error);

	if (NULL != value)
	{
		for (unsigned int i = 0; i < nvals ; ++i)
		{
			bool_list.push_back(TRUE == value[i]);
		}
		g_free(value);
	}

	return bool_list;
}

std::list<int> Preferences::GetIntegerList(std::string section, std::string key)
{
	GError *error = NULL;
	gsize nvals = 0;
	int* value;
	list<int> int_list;

	value = g_key_file_get_integer_list(m_KeyFile,section.c_str(),key.c_str(), &nvals, &error);

	if (NULL != value)
	{
		for (unsigned int i = 0; i < nvals ; ++i)
		{
			int_list.push_back(value[i]);
		}
		g_free(value);
	}

	return int_list;
}

void Preferences::SetValue(std::string section, std::string key, std::string value)
{
	string strOldValue = GetValue(section,key);
	bool bHasKey = HasKey(section,key);
	if (!bHasKey || strOldValue != value)
	{
		g_key_file_set_value(m_KeyFile,section.c_str(),key.c_str(), value.c_str());
		m_bModified = true;
		
		PreferencesEvent::PreferencesEventType type;
		if (!bHasKey)
		{
			type = PreferencesEvent::PREFERENCE_ADDED;
		}
		else
		{
			type = PreferencesEvent::PREFERENCE_CHANGED;
		}
		EmitPreferenceChanged(type,section,key, strOldValue, value);
	}
}

void Preferences::SetString(std::string section, std::string key, std::string value)
{
	SetValue(section,key,value);
}

void Preferences::SetLocaleString(std::string section, std::string key, std::string locale, std::string value)
{
	std::string strOldValue = GetLocaleString(section,key,locale);
	if (strOldValue != value)
	{
		g_key_file_set_locale_string(m_KeyFile,section.c_str(),key.c_str(), locale.c_str(), value.c_str());
		m_bModified = true;
		
		EmitPreferenceChanged(PreferencesEvent::PREFERENCE_CHANGED,
			section,key,locale, strOldValue, value);
		
	}
}

void Preferences::SetBoolean(std::string section, std::string key, bool value)
{
	bool bHasKey = HasKey(section,key);
	bool bOldVal = GetBoolean(section,key);
	
	if (!bHasKey || bOldVal != value)
	{
		g_key_file_set_boolean(m_KeyFile,section.c_str(),key.c_str(), value ? TRUE : FALSE);
		m_bModified = true;
		
		PreferencesEvent::PreferencesEventType type;
		if (!bHasKey)
		{
			type = PreferencesEvent::PREFERENCE_ADDED;
		}
		else
		{
			type = PreferencesEvent::PREFERENCE_CHANGED;
		}
		EmitPreferenceChanged(type,section,key, bOldVal, value);
	}
}

void Preferences::SetInteger(std::string section, std::string key, int value)
{
	bool bHasKey = HasKey(section,key);
	int iOldVal  = GetInteger(section, key);
	if (!bHasKey || iOldVal != value)
	{
		g_key_file_set_integer(m_KeyFile,section.c_str(),key.c_str(), value);
		m_bModified = true;
		
		PreferencesEvent::PreferencesEventType type;
		if (!bHasKey)
		{
			type = PreferencesEvent::PREFERENCE_ADDED;
		}
		else
		{
			type = PreferencesEvent::PREFERENCE_CHANGED;
		}
		EmitPreferenceChanged(type,section,key, iOldVal, value);
	}
}

void Preferences::SetStringList(std::string section, std::string key, std::list<std::string> value_list)
{
	bool bHasKey = HasKey(section,key);
	std::list<std::string> old_list = GetStringList(section, key);
	if (!bHasKey || old_list != value_list)
	{
		gsize nstrings = value_list.size();
		char** strings = NULL;

		if (0 != nstrings)
		{
			strings = new char*[nstrings];
			std::list<std::string>::iterator itr;
			int i = 0;
			for (itr = value_list.begin(); value_list.end() != itr; ++itr)
			{
				strings[i] = (char*)itr->c_str();
				++i;
			}
		}
		
		if (NULL != strings)
		{
			g_key_file_set_string_list (m_KeyFile, section.c_str(), key.c_str(), strings, nstrings);
			free(strings);
		}

		m_bModified = true;
		PreferencesEvent::PreferencesEventType type;
		if (!bHasKey)
		{
			type = PreferencesEvent::PREFERENCE_ADDED;
		}
		else
		{
			type = PreferencesEvent::PREFERENCE_CHANGED;
		}
		// FIXME -this needs to send the old/new list?
		EmitPreferenceChanged(type,section,key, 0, 0);
	}
}

void Preferences::SetStringLocaleList(std::string section, std::string key, std::string locale, std::list<std::string> value_list)
{
	//m_bModified = true;
}

void Preferences::SetBooleanList(std::string section, std::string key, std::list<bool> value_list)
{
	bool bHasKey = HasKey(section,key);
	std::list<bool> old_list = GetBooleanList(section, key);
	if (!bHasKey || old_list != value_list)
	{
		gsize nvals = value_list.size();
		gboolean* values = NULL;

		if (0 != nvals)
		{
			values = new gboolean[nvals];
			std::list<bool>::iterator itr;
			int i = 0;
			for (itr = value_list.begin(); value_list.end() != itr; ++itr)
			{
				values[i] = (*itr) ? TRUE : FALSE;
				++i;
			}
		}
		
		if (NULL != values)
		{
			g_key_file_set_boolean_list (m_KeyFile, section.c_str(), key.c_str(), values, nvals);
			free(values);
		}

		m_bModified = true;
		PreferencesEvent::PreferencesEventType type;
		if (!bHasKey)
		{
			type = PreferencesEvent::PREFERENCE_ADDED;
		}
		else
		{
			type = PreferencesEvent::PREFERENCE_CHANGED;
		}
		// FIXME -this needs to send the old/new list?
		EmitPreferenceChanged(type,section,key, 0, 0);
	}
}

void Preferences::SetIntegerList(std::string section, std::string key, std::list<int> value_list)
{
	bool bHasKey = HasKey(section,key);
	std::list<int> old_list = GetIntegerList(section, key);
	if (!bHasKey || old_list != value_list)
	{
		gsize nvals = value_list.size();
		int* values = NULL;

		if (0 != nvals)
		{
			values = new int[nvals];
			std::list<int>::iterator itr;
			int i = 0;
			for (itr = value_list.begin(); value_list.end() != itr; ++itr)
			{
				values[i] = *itr;
				++i;
			}
		}
		
		if (NULL != values)
		{
			g_key_file_set_integer_list (m_KeyFile, section.c_str(), key.c_str(), values, nvals);
			free(values);
		}

		m_bModified = true;
		PreferencesEvent::PreferencesEventType type;
		if (!bHasKey)
		{
			type = PreferencesEvent::PREFERENCE_ADDED;
		}
		else
		{
			type = PreferencesEvent::PREFERENCE_CHANGED;
		}
		// FIXME -this needs to send the old/new list?
		EmitPreferenceChanged(type,section,key, 0, 0);
	}
}

void Preferences::RemoveSection(std::string section)
{
	if (HasSection(section))
	{
		GError *error = NULL;
		g_key_file_remove_group(m_KeyFile,section.c_str(), &error);
		m_bModified = true;
	}
}

void Preferences::RemoveKey(std::string section,std::string key)
{
	if (HasKey(section,key))
	{
		GError *error = NULL;
		g_key_file_remove_key(m_KeyFile,section.c_str(), key.c_str(), &error);
		m_bModified = true;
	}
}

