#include <ExternalTools.h>
#include "Preferences.h"
#include <gtk/gtk.h>
#include <sstream>
#include <algorithm>
#include <cctype>

#include "QuiverStockIcons.h"

#define EXTERNAL_TOOLS_SECTION               "ExternalTools"
#define EXTERNAL_TOOLS_CATEGORY_DEFAULT      "default"
#define EXTERNAL_TOOL_SECTION_PREFIX         "ExternalTool_"
#define EXTERNAL_TOOL_KEY_NAME               "name"
#define EXTERNAL_TOOL_KEY_TOOLTIP            "tooltip"
#define EXTERNAL_TOOL_KEY_ICON               "icon"
#define EXTERNAL_TOOL_KEY_COMMAND            "command"
#define EXTERNAL_TOOL_KEY_ICON               "icon"
#define EXTERNAL_TOOL_KEY_SUPPORT_MULTIPLE   "supports_multiple_files"
#define EXTERNAL_TOOL_KEY_ALLOW_MULTIPLE     "allow_multiple_selection"
#define EXTERNAL_TOOL_KEY_SEPARATE_PROCESS   "separate_process_per_file"
#define EXTERNAL_TOOL_KEY_TARGET_IMAGES      "target_images"
#define EXTERNAL_TOOL_KEY_TARGET_VIDEOS      "target_videos"
#define EXTERNAL_TOOL_KEY_EXTENSIONS         "extensions"
#define EXTERNAL_TOOL_KEY_SHOW_OUTPUT        "show_output"
#define EXTERNAL_TOOL_KEY_SHOW_ERRORS        "show_errors"
#define EXTERNAL_TOOL_KEY_SHOW_ONLY_ON_ERROR "show_only_on_error"
#define EXTERNAL_TOOL_KEY_SHORTCUT           "shortcut"

using namespace std;

ExternalToolsPtr ExternalTools::c_ExternalToolsPtr;

class ExternalTools::ExternalToolsImpl


{
public:
	ExternalToolsImpl(ExternalTools* bm) : m_pParent(bm) 
	{ 
		m_PrefPtr = Preferences::GetInstance();
	};

	ExternalTools* m_pParent;
	PreferencesPtr m_PrefPtr;

	typedef std::map<int, int> ExternalToolOrderMap;
	ExternalToolOrderMap m_mapExternalToolOrder;

	void SimplifySortOrder()
	{
		vector<int> v;

		ExternalToolMap::iterator itr;
		for (itr = m_pParent->m_mapExternalTools.begin(); m_pParent->m_mapExternalTools.end() != itr; ++itr)
		{
			v.push_back(itr->second.GetID());
		}
		
		ExternalTools::ExternalToolsImpl::SortBySortOrder sortOrder(this);
		std::sort (v.begin(),v.end(), sortOrder);
		
		m_mapExternalToolOrder.clear();

		for (unsigned int i = 0; i < v.size(); ++i)
		{
			m_mapExternalToolOrder[v[i]] = i;
		}
	}

	int GetNewSortOrderValue()
	{
		int i = 0;
		SimplifySortOrder();
		ExternalToolOrderMap::iterator itr;
		for (itr = m_mapExternalToolOrder.begin(); m_mapExternalToolOrder.end() != itr; ++itr)
		{
			if (i <= itr->second)
			{
				i = itr->second + 1;
			}
		}
		return i;
	}

	class SortBySortOrder
	{
	public:
		typedef int first_argument_type;
		typedef int second_argument_type;
		typedef bool result_type;

		SortBySortOrder(ExternalToolsImpl *parent):m_pParent(parent){};
		bool operator()(const int &a, const int &b) const
		{
			return (m_pParent->m_mapExternalToolOrder[a] < m_pParent->m_mapExternalToolOrder[b]);
		}

		bool operator()(const ExternalTool &a, const ExternalTool &b) const
		{
			return (m_pParent->m_mapExternalToolOrder[a.GetID()] < m_pParent->m_mapExternalToolOrder[b.GetID()]);
		}
	private: 
		ExternalToolsImpl* m_pParent;
	};


};

ExternalToolsPtr ExternalTools::GetInstance()
{
	if (NULL == c_ExternalToolsPtr.get())
	{
		ExternalToolsPtr external_toolsPtr(new ExternalTools());
		c_ExternalToolsPtr = external_toolsPtr;
	}
	return c_ExternalToolsPtr;
}

ExternalTools::ExternalTools() : m_ExternalToolsImplPtr (new ExternalToolsImpl(this))
{
	LoadFromPreferences(); 
}

void ExternalTools::LoadFromPreferences()
{
	PreferencesPtr prefs = m_ExternalToolsImplPtr->m_PrefPtr;
	list<string> categories = prefs->GetKeys(EXTERNAL_TOOLS_SECTION);
	list<string>::iterator itr;
	for (itr = categories.begin(); categories.end() != itr; ++itr)
	{
		list<int> external_tool_ids = prefs->GetIntegerList(EXTERNAL_TOOLS_SECTION,*itr);
		list<int>::iterator itr2;
		int sort_order = 0;
		
		for ( itr2 = external_tool_ids.begin() ; external_tool_ids.end() != itr2; ++itr2)
		{
			m_ExternalToolsImplPtr->m_mapExternalToolOrder[*itr2] = sort_order;
			++sort_order;
			
			stringstream ss;
 			ss << EXTERNAL_TOOL_SECTION_PREFIX << *itr2;
			string section = ss.str();
			if (prefs->HasSection(section))
			{
				string name, tooltip, icon, cmd;
				bool multi, output, errors, only_on_error;
				string shortcut;

				name    = prefs->GetString(section,EXTERNAL_TOOL_KEY_NAME);
				tooltip = prefs->GetString(section,EXTERNAL_TOOL_KEY_TOOLTIP);
				icon = prefs->GetString(section,EXTERNAL_TOOL_KEY_ICON);
				cmd  = prefs->GetString(section,EXTERNAL_TOOL_KEY_COMMAND);
				multi = prefs->GetBoolean(section,EXTERNAL_TOOL_KEY_SUPPORT_MULTIPLE);
				output = prefs->GetBoolean(section,EXTERNAL_TOOL_KEY_SHOW_OUTPUT);
				errors = prefs->GetBoolean(section,EXTERNAL_TOOL_KEY_SHOW_ERRORS);
				only_on_error = prefs->GetBoolean(section,EXTERNAL_TOOL_KEY_SHOW_ONLY_ON_ERROR, false);
				shortcut = prefs->GetString(section,EXTERNAL_TOOL_KEY_SHORTCUT, "");

				bool allow_multi = false;
				if (prefs->HasKey(section, EXTERNAL_TOOL_KEY_ALLOW_MULTIPLE))
				{
					allow_multi = prefs->GetBoolean(section, EXTERNAL_TOOL_KEY_ALLOW_MULTIPLE);
				}
				else
				{
					// For legacy configs without this key: if it supported multiple files, allow; otherwise default to false (safe)
					allow_multi = multi;
				}

				bool sep_process = false;
				if (prefs->HasKey(section, EXTERNAL_TOOL_KEY_SEPARATE_PROCESS))
				{
					sep_process = prefs->GetBoolean(section, EXTERNAL_TOOL_KEY_SEPARATE_PROCESS);
				}
				else
				{
					sep_process = !multi;
				}

				bool target_images = prefs->GetBoolean(section, EXTERNAL_TOOL_KEY_TARGET_IMAGES, true);
				bool target_videos = prefs->GetBoolean(section, EXTERNAL_TOOL_KEY_TARGET_VIDEOS, true);
				string extensions = prefs->GetString(section, EXTERNAL_TOOL_KEY_EXTENSIONS, "");
				
				if (icon.empty())
				{
					icon = QUIVER_STOCK_EXECUTE;
				}
				
				ExternalTool b(name, tooltip, icon, cmd, multi, output, errors, only_on_error, shortcut);
				b.SetAllowMultiple(allow_multi);
				b.SetSeparateProcess(sep_process);
				b.SetTargetImages(target_images);
				b.SetTargetVideos(target_videos);
				b.SetExtensions(extensions);
				b.SetCategory(*itr);
				b.SetID(*itr2);
				m_mapExternalTools[*itr2] = b;
			}
		}
	}
}

void ExternalTools::SaveToPreferences()
{
	ExternalToolMap::iterator itr;

	PreferencesPtr prefs = m_ExternalToolsImplPtr->m_PrefPtr;

	map<string,vector<int> > categories;
	map<string,vector<int> >::iterator cat_itr;

	for (itr = m_mapExternalTools.begin(); m_mapExternalTools.end() != itr; ++itr)
	{
		stringstream ss;
		ss << EXTERNAL_TOOL_SECTION_PREFIX << itr->first;
		string section = ss.str();

		prefs->SetString(section,EXTERNAL_TOOL_KEY_NAME, itr->second.GetName());
		prefs->SetString(section,EXTERNAL_TOOL_KEY_TOOLTIP, itr->second.GetTooltip());
		prefs->SetString(section,EXTERNAL_TOOL_KEY_COMMAND, itr->second.GetCmd());

		if (itr->second.GetIcon().empty())
		{
			itr->second.SetIcon(QUIVER_STOCK_EXECUTE);
		}
		prefs->SetString(section,EXTERNAL_TOOL_KEY_ICON, itr->second.GetIcon());

		prefs->SetBoolean(section,EXTERNAL_TOOL_KEY_SUPPORT_MULTIPLE, itr->second.GetSupportsMultiple());
		prefs->SetBoolean(section,EXTERNAL_TOOL_KEY_ALLOW_MULTIPLE, itr->second.GetAllowMultiple());
		prefs->SetBoolean(section,EXTERNAL_TOOL_KEY_SEPARATE_PROCESS, itr->second.GetSeparateProcess());
		prefs->SetBoolean(section,EXTERNAL_TOOL_KEY_TARGET_IMAGES, itr->second.GetTargetImages());
		prefs->SetBoolean(section,EXTERNAL_TOOL_KEY_TARGET_VIDEOS, itr->second.GetTargetVideos());
		prefs->SetString(section,EXTERNAL_TOOL_KEY_EXTENSIONS, itr->second.GetExtensions());
		prefs->SetBoolean(section,EXTERNAL_TOOL_KEY_SHOW_OUTPUT, itr->second.GetShowOutput());
		prefs->SetBoolean(section,EXTERNAL_TOOL_KEY_SHOW_ERRORS, itr->second.GetShowErrors());
		prefs->SetBoolean(section,EXTERNAL_TOOL_KEY_SHOW_ONLY_ON_ERROR, itr->second.GetShowOnlyOnError());
		prefs->SetString(section,EXTERNAL_TOOL_KEY_SHORTCUT, itr->second.GetShortcut());

		categories[itr->second.GetCategory()].push_back(itr->first);

	}
	for (cat_itr = categories.begin(); categories.end() != cat_itr; ++cat_itr)
	{
	
		ExternalTools::ExternalToolsImpl::SortBySortOrder sortOrder(m_ExternalToolsImplPtr.get());
		std::sort (cat_itr->second.begin(),cat_itr->second.end(), sortOrder);
		list<int> cat_list(cat_itr->second.begin(), cat_itr->second.end());
		prefs->SetIntegerList(EXTERNAL_TOOLS_SECTION,cat_itr->first,cat_list);
	}
}


bool ExternalTools::AddExternalTool(ExternalTool external_tool)
{
	bool rval = true;
	if (external_tool.GetCategory().empty())
	{
		external_tool.SetCategory(EXTERNAL_TOOLS_CATEGORY_DEFAULT);
	}

	if (external_tool.GetIcon().empty())
	{
		external_tool.SetIcon(QUIVER_STOCK_EXECUTE);
	}

	ExternalToolMap::iterator itr;
	// for now , dont care if a external_tool already exists..
	bool bFound = false;

	if (false == bFound)
	{
		external_tool.SetID(GetUniqueID());
		int sort_val = m_ExternalToolsImplPtr->GetNewSortOrderValue();
		m_ExternalToolsImplPtr->m_mapExternalToolOrder[external_tool.GetID()] = sort_val;
		m_mapExternalTools[external_tool.GetID()] = external_tool;
		SaveToPreferences();
		EmitExternalToolChangedEvent(ExternalToolsEvent::EXTERNAL_TOOL_ADDED);
	}
	else
	{
		rval = false;
	}

	return rval;
}

bool ExternalTools::AddExternalTool(ExternalTool external_tool, std::string category )
{
	if (category.empty())
	{
		category = EXTERNAL_TOOLS_CATEGORY_DEFAULT;
	}
	external_tool.SetCategory(category);
	return AddExternalTool(external_tool);
}


bool ExternalTools::Remove(int id)
{
	bool rval = false;
	ExternalToolMap::iterator itr;
	itr = m_mapExternalTools.find(id);
	if (m_mapExternalTools.end() != itr)
	{
		stringstream ss;
		ss << EXTERNAL_TOOL_SECTION_PREFIX << itr->first;
		string section = ss.str();

		m_ExternalToolsImplPtr->m_PrefPtr->RemoveSection(section);
		
		m_mapExternalTools.erase(itr);
		rval = true;
		SaveToPreferences();
		EmitExternalToolChangedEvent(ExternalToolsEvent::EXTERNAL_TOOL_REMOVED);
	}

	ExternalToolsImpl::ExternalToolOrderMap::iterator itr2;
	itr2 = m_ExternalToolsImplPtr->m_mapExternalToolOrder.find(id);
	if (m_ExternalToolsImplPtr->m_mapExternalToolOrder.end() != itr2)
	{
		m_ExternalToolsImplPtr->m_mapExternalToolOrder.erase(itr2);
	}

	return rval;
}

const ExternalTool* ExternalTools::GetExternalTool(int id)
{
	ExternalTool* b = NULL;
	ExternalToolMap::iterator itr;
	itr = m_mapExternalTools.find(id);
	if (m_mapExternalTools.end() != itr)
	{
		b = &itr->second;
	}
	return b;
}

std::vector<ExternalTool> ExternalTools::GetExternalTools() 
{
	return GetExternalTools(EXTERNAL_TOOLS_CATEGORY_DEFAULT);
}

std::vector<ExternalTool> ExternalTools::GetExternalTools(std::string category) 
{
	vector<ExternalTool> v;

	ExternalToolMap::iterator itr;
	for (itr = m_mapExternalTools.begin(); m_mapExternalTools.end() != itr; ++itr)
	{
		if (category == itr->second.GetCategory())
		{
			v.push_back(itr->second);
		}
	}
	
	ExternalTools::ExternalToolsImpl::SortBySortOrder sortOrder(m_ExternalToolsImplPtr.get());
	std::sort (v.begin(),v.end(), sortOrder);
	return v;
}

int ExternalTools::GetUniqueID() const
{
	int i = 0;
	while (m_mapExternalTools.end() != m_mapExternalTools.find(i))
	{
		++i;
	}
	return i;
}


bool ExternalTools::UpdateExternalTool(ExternalTool external_tool)
{
	bool bUpdated = false;
	ExternalToolMap::iterator itr;
	itr = m_mapExternalTools.find(external_tool.GetID());
	if (m_mapExternalTools.end() != itr)
	{
		itr->second = external_tool;
		bUpdated = true;
		SaveToPreferences();
		EmitExternalToolChangedEvent(ExternalToolsEvent::EXTERNAL_TOOL_CHANGED);
	}
	return bUpdated;
}

bool ExternalTools::MoveUp (int id)
{
	ExternalToolMap::iterator itr = m_mapExternalTools.find(id);
	if (m_mapExternalTools.end() != itr)
	{
		int swapID = id;
		ExternalTool b = itr->second;
		int orderid = m_ExternalToolsImplPtr->m_mapExternalToolOrder[itr->first];
		int new_orderid = orderid;
		
		for (itr = m_mapExternalTools.begin(); m_mapExternalTools.end() != itr; ++itr)
		{
			if (itr->second.GetCategory() == b.GetCategory())
			{
				int other_orderid = m_ExternalToolsImplPtr->m_mapExternalToolOrder[itr->first];
				if (other_orderid < orderid && (other_orderid > new_orderid || new_orderid == orderid))
				{
					new_orderid = other_orderid;
					swapID = itr->first;
				}
			}
		}
		if (swapID != id)
		{
			orderid = m_ExternalToolsImplPtr->m_mapExternalToolOrder[id];
			m_ExternalToolsImplPtr->m_mapExternalToolOrder[id] = new_orderid;
			m_ExternalToolsImplPtr->m_mapExternalToolOrder[swapID] = orderid;
			SaveToPreferences();
			EmitExternalToolChangedEvent(ExternalToolsEvent::EXTERNAL_TOOL_CHANGED);
			return true;
		}
	}
	return false;

}

bool ExternalTools::MoveDown (int id)
{
	ExternalToolMap::iterator itr = m_mapExternalTools.find(id);
	if (m_mapExternalTools.end() != itr)
	{
		int swapID = id;
		ExternalTool b = itr->second;
		int orderid = m_ExternalToolsImplPtr->m_mapExternalToolOrder[itr->first];
		int new_orderid = orderid;
		
		for (itr = m_mapExternalTools.begin(); m_mapExternalTools.end() != itr; ++itr)
		{
			if (itr->second.GetCategory() == b.GetCategory())
			{
				int other_orderid = m_ExternalToolsImplPtr->m_mapExternalToolOrder[itr->first];
				if (other_orderid > orderid && (other_orderid < new_orderid || new_orderid == orderid))
				{
					new_orderid = other_orderid;
					swapID = itr->first;
				}
			}
		}
		if (swapID != id)
		{
			orderid = m_ExternalToolsImplPtr->m_mapExternalToolOrder[id];
			m_ExternalToolsImplPtr->m_mapExternalToolOrder[id] = new_orderid;
			m_ExternalToolsImplPtr->m_mapExternalToolOrder[swapID] = orderid;
			SaveToPreferences();
			EmitExternalToolChangedEvent(ExternalToolsEvent::EXTERNAL_TOOL_CHANGED);
			return true;
		}
	}
	return false;
}

std::vector<std::string> ExternalTool::ParseExtensions(const std::string& exts_str)
{
	std::vector<std::string> exts;
	std::string current;
	for (size_t i = 0; i <= exts_str.length(); ++i)
	{
		char c = (i < exts_str.length()) ? exts_str[i] : ',';
		if (c == ',' || c == ';' || isspace(static_cast<unsigned char>(c)))
		{
			size_t start = 0;
			while (start < current.length() && (current[start] == '.' || isspace(static_cast<unsigned char>(current[start]))))
				start++;
			size_t end = current.length();
			while (end > start && isspace(static_cast<unsigned char>(current[end - 1])))
				end--;
			if (start < end)
			{
				std::string ext = current.substr(start, end - start);
				std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) { return std::tolower(ch); });
				if (!ext.empty() && std::find(exts.begin(), exts.end(), ext) == exts.end())
				{
					exts.push_back(ext);
				}
			}
			current.clear();
		}
		else
		{
			current += c;
		}
	}
	return exts;
}

bool ExternalTool::MatchesFile(const std::string& filepath, bool is_video) const
{
	if (is_video && !m_bTargetVideos) return false;
	if (!is_video && !m_bTargetImages) return false;

	if (!m_strExtensions.empty())
	{
		std::vector<std::string> allowed = ParseExtensions(m_strExtensions);
		if (!allowed.empty())
		{
			size_t dot = filepath.rfind('.');
			if (dot == std::string::npos) return false;
			std::string ext = filepath.substr(dot + 1);
			std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) { return std::tolower(ch); });
			if (std::find(allowed.begin(), allowed.end(), ext) == allowed.end())
			{
				return false;
			}
		}
	}
	return true;
}
