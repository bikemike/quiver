#include "RecentItems.h"

RecentItems::RecentItems(size_t iMaxEntries)
	: m_iMaxEntries(iMaxEntries)
{
}

void RecentItems::Record(const std::string& uri, ImageListAttributesPtr attributes)
{
	if (uri.empty() || !attributes)
	{
		return;
	}

	for (std::deque<Entry>::iterator itr = m_Entries.begin();
			m_Entries.end() != itr; ++itr)
	{
		if (itr->uri == uri)
		{
			m_Entries.erase(itr);
			break;
		}
	}

	Entry entry;
	entry.uri = uri;
	entry.pAttributes = attributes;
	m_Entries.push_front(entry);

	while (m_Entries.size() > m_iMaxEntries)
	{
		m_Entries.pop_back();
	}
}

size_t RecentItems::RemoveAllByURIs(const std::list<std::string>& uris)
{
	size_t removed = 0;
	for (std::list<std::string>::const_iterator itr = uris.begin();
			uris.end() != itr; ++itr)
	{
		if (RemoveByURI(*itr))
		{
			++removed;
		}
	}
	return removed;
}

bool RecentItems::RemoveByURI(const std::string& uri)
{
	for (std::deque<Entry>::iterator itr = m_Entries.begin();
			m_Entries.end() != itr; ++itr)
	{
		if (itr->uri == uri)
		{
			m_Entries.erase(itr);
			return true;
		}
	}
	return false;
}

const RecentItems::Entry* RecentItems::Find(const std::string& uri) const
{
	for (std::deque<Entry>::const_iterator itr = m_Entries.begin();
			m_Entries.end() != itr; ++itr)
	{
		if (itr->uri == uri)
		{
			return &(*itr);
		}
	}
	return NULL;
}

void RecentItems::SetMaxEntries(size_t iMaxEntries)
{
	m_iMaxEntries = iMaxEntries;
	while (m_Entries.size() > m_iMaxEntries)
	{
		m_Entries.pop_back();
	}
}