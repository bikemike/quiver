#include "BrowserHistory.h"

using namespace std;

BrowserHistory::BrowserHistory() : m_iCurrentIndex(0)
{
}

void BrowserHistory::Add(const std::list<std::string>& listItems)
{
	if (0 != m_vectHistory.size() && m_iCurrentIndex < m_vectHistory.size() -1)
	{
		vector<list<string> >::iterator itr;
		itr = m_vectHistory.begin();
		itr += m_iCurrentIndex + 1;
		m_vectHistory.erase(itr, m_vectHistory.end());
	}
	
	m_vectHistory.push_back(listItems);
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


const std::list<std::string>& BrowserHistory::GetCurrent() const
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

const std::list<std::string>& BrowserHistory::GetAtIndex(unsigned int index) const
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

