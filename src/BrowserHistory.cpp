#include "BrowserHistory.h"

using namespace std;

BrowserHistory::BrowserHistory() : m_iCurrentIndex(0)
{
}

void BrowserHistory::Add(const std::list<std::string>& listItems, std::string selectedItem)
{
	if (0 != m_vectHistory.size() && m_iCurrentIndex < m_vectHistory.size() -1)
	{
		vector<list<string> >::iterator itr;
		itr = m_vectHistory.begin();
		itr += m_iCurrentIndex + 1;
		m_vectHistory.erase(itr, m_vectHistory.end());
	}

	if (0 != m_vectHistorySelected.size() && m_iCurrentIndex < m_vectHistorySelected.size() -1)
	{
		vector<string>::iterator itr;
		itr = m_vectHistorySelected.begin();
		itr += m_iCurrentIndex + 1;
		m_vectHistorySelected.erase(itr, m_vectHistorySelected.end());
	}
	
	m_vectHistory.push_back(listItems);
	m_vectHistorySelected.push_back(selectedItem);
	m_iCurrentIndex = m_vectHistory.size() - 1;
}



bool BrowserHistory::HasIndex(unsigned int index) const
{
	return (index < m_vectHistory.size()); 
}

bool BrowserHistory::CanGoForward() const
{
	return (0 != m_vectHistory.size() && m_iCurrentIndex < m_vectHistory.size() - 1);
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
		vector<list<string> >::const_iterator itr;
		itr = m_vectHistory.begin();
		itr += m_iCurrentIndex;
		return *itr;
	}
	return m_listEmpty;
}

std::string BrowserHistory::GetCurrentSelected() const
{
	if (m_iCurrentIndex < m_vectHistorySelected.size())
	{
		vector<string>::const_iterator itr;
		itr = m_vectHistorySelected.begin();
		itr += m_iCurrentIndex;
		return *itr;
	}
	return std::string();
}

void BrowserHistory::SetCurrentSelected(std::string selected)
{
	if (m_iCurrentIndex < m_vectHistorySelected.size())
	{
		m_vectHistorySelected[m_iCurrentIndex] = selected;
	}
}

const std::list<std::string>& BrowserHistory::GetFilesAtIndex(unsigned int index) const
{
	if (index < m_vectHistory.size())
	{
		vector<list<string> >::const_iterator itr;
		itr = m_vectHistory.begin();
		itr += index;
		return *itr;
	}

	return m_listEmpty;
}

std::string BrowserHistory::GetSelectedAtIndex(unsigned int index) const
{
	if (index < m_vectHistorySelected.size())
	{
		vector<string>::const_iterator itr;
		itr = m_vectHistorySelected.begin();
		itr += index;
		return *itr;
	}

	return std::string();
}

