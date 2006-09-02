
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
	GError *error = NULL;
	m_KeyFile = g_key_file_new();
	g_key_file_load_from_file(m_KeyFile,g_szConfigFilePath,G_KEY_FILE_KEEP_COMMENTS, &error);
}

Preferences::~Preferences()
{

	GError *error = NULL;
	guint clength;
	gchar *contents = g_key_file_to_data (m_KeyFile, &clength, &error);

	error = NULL;
	g_file_set_contents(g_szConfigFilePath,contents,clength,&error);

	g_free(contents);

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
	gsize length;
	gchar** groups =  g_key_file_get_groups (m_KeyFile, &length);

	list<string> sections;
	gsize i = 0;
	for (i = 0; i < length; i++)
	{
		sections.push_back(groups[i]);
	}
	g_strfreev(groups);

	return sections;
}

std::list<std::string> Preferences::GetKeys(std::string section)
{
	GError *error = NULL;
	gsize length;
	gchar** keys =  g_key_file_get_keys (m_KeyFile, section.c_str(),  &length, &error);

	list<string> key_list;
	gsize i = 0;
	for (i = 0; i < length; i++)
	{
		key_list.push_back(keys[i]);
	}
	g_strfreev(keys);

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

std::string Preferences::GetValue(std::string section, std::string key)
{
	GError *error = NULL;
	gchar *value;
	string strValue;

	value = g_key_file_get_value(m_KeyFile,section.c_str(),key.c_str(), &error);

	if (NULL != value)
	{
		strValue = value;
		g_free(value);
	}

	return strValue;
}

std::string Preferences::GetString(std::string section, std::string key)
{
	return GetValue(section,key);
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

	return value;
}

std::list<std::string> Preferences::GetStringList(std::string section, std::string key)
{
	list<string> string_list;
	return string_list;
}

std::list<std::string> Preferences::GetLocaleStringList(std::string section, std::string key, std::string locale)
{
	list<string> string_list;
	return string_list;
}

std::list<bool> Preferences::GetBooleanList(std::string section, std::string key)
{
	list<bool> bool_list;
	return bool_list;
}

std::list<int> Preferences::GetIntegerList(std::string section, std::string key)
{
	list<int> int_list;
	return int_list;
}

void Preferences::SetValue(std::string section, std::string key, std::string value)
{
	g_key_file_set_value(m_KeyFile,section.c_str(),key.c_str(), value.c_str());
}

void Preferences::SetString(std::string section, std::string key, std::string value)
{
	SetValue(section,key,value);
}

void Preferences::SetLocaleString(std::string section, std::string key, std::string locale, std::string value)
{
	g_key_file_set_locale_string(m_KeyFile,section.c_str(),key.c_str(), locale.c_str(), value.c_str());
}

void Preferences::SetBoolean(std::string section, std::string key, bool value)
{
	gboolean val = FALSE;
	if (value)
	{
		val = TRUE;
	}
	g_key_file_set_boolean(m_KeyFile,section.c_str(),key.c_str(), val);
}

void Preferences::SetInteger(std::string section, std::string key, int value)
{
	g_key_file_set_integer(m_KeyFile,section.c_str(),key.c_str(), value);
}

void Preferences::SetStringList(std::string section, std::string key, std::list<std::string> value_list)
{
}

void Preferences::SetStringLocaleList(std::string section, std::string key, std::string locale, std::list<std::string> value_list)
{
}

void Preferences::SetBooleanList(std::string section, std::string key, std::list<bool> value_list)
{
}

void Preferences::SetIntegerList(std::string section, std::string key, std::list<int> value_list)
{
}

void Preferences::RemoveSection(std::string section)
{
	GError *error = NULL;
	g_key_file_remove_group(m_KeyFile,section.c_str(), &error);
}

void Preferences::RemoveKey(std::string section,std::string key)
{
	GError *error = NULL;
	g_key_file_remove_key(m_KeyFile,section.c_str(), key.c_str(), &error);
}

