#include "BrowserHistory.h"

using namespace std;

BrowserHistory::BrowserHistory() : m_iCurrentIndex(0)
{
}

void BrowserHistory::Add(const std::list<std::string>& listItems, std::string selectedItem,
                        bool bRecursive,
                        const std::set<std::string>& recursiveFolders)
{
	if (!m_vectHistory.empty() && m_iCurrentIndex < m_vectHistory.size() - 1)
	{
		m_vectHistory.erase(m_vectHistory.begin() + m_iCurrentIndex + 1, m_vectHistory.end());
	}
	
	BrowserHistoryEntry entry;
	entry.listItems = listItems;
	entry.selectedItem = selectedItem;
	entry.bRecursive = bRecursive;
	entry.recursiveFolders = recursiveFolders;
	m_vectHistory.push_back(entry);
	m_iCurrentIndex = m_vectHistory.size() - 1;
}

bool BrowserHistory::HasIndex(unsigned int index) const
{
	return (index < m_vectHistory.size()); 
}

bool BrowserHistory::CanGoForward() const
{
	return (!m_vectHistory.empty() && m_iCurrentIndex < m_vectHistory.size() - 1);
}

bool BrowserHistory::CanGoBack() const
{
	return (0 < m_iCurrentIndex);
}

bool BrowserHistory::GoForward()
{
	bool rval = CanGoForward();
	if (rval)
	{
		++m_iCurrentIndex;
	}
	return rval;
}

bool BrowserHistory::GoBack()
{
	bool rval = CanGoBack();
	if (rval)
	{
		--m_iCurrentIndex;
	}
	return rval;
}

const std::list<std::string>& BrowserHistory::GetCurrentFiles() const
{
	if (m_iCurrentIndex < m_vectHistory.size())
	{
		return m_vectHistory[m_iCurrentIndex].listItems;
	}
	return m_listEmpty;
}

std::string BrowserHistory::GetCurrentSelected() const
{
	if (m_iCurrentIndex < m_vectHistory.size())
	{
		return m_vectHistory[m_iCurrentIndex].selectedItem;
	}
	return std::string();
}

void BrowserHistory::SetCurrentSelected(std::string selected)
{
	if (m_iCurrentIndex < m_vectHistory.size())
	{
		m_vectHistory[m_iCurrentIndex].selectedItem = selected;
	}
}

bool BrowserHistory::GetCurrentRecursive() const
{
	if (m_iCurrentIndex < m_vectHistory.size())
	{
		return m_vectHistory[m_iCurrentIndex].bRecursive;
	}
	return false;
}

const std::set<std::string>& BrowserHistory::GetCurrentRecursiveFolders() const
{
	if (m_iCurrentIndex < m_vectHistory.size())
	{
		return m_vectHistory[m_iCurrentIndex].recursiveFolders;
	}
	return m_setEmpty;
}

const std::list<std::string>& BrowserHistory::GetFilesAtIndex(unsigned int index) const
{
	if (index < m_vectHistory.size())
	{
		return m_vectHistory[index].listItems;
	}

	return m_listEmpty;
}

std::string BrowserHistory::GetSelectedAtIndex(unsigned int index) const
{
	if (index < m_vectHistory.size())
	{
		return m_vectHistory[index].selectedItem;
	}

	return std::string();
}

