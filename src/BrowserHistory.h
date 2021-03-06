#ifndef BROWSERHISTORY_H_
#define BROWSERHISTORY_H_
#include <boost/shared_ptr.hpp>
#include <vector>
#include <list>
#include <string>

/*
class BrowserHistoryImpl;
typedef boost::shared_ptr<BrowserHistoryImpl> BrowserHistoryImplPtr;
*/

class BrowserHistory
{
public:
	BrowserHistory();

	void Add(const std::list<std::string>& listItems);

	bool HasIndex(unsigned int index) const;
	bool CanGoForward() const;
	bool CanGoBack() const;
	
	bool GoForward();
	bool GoBack();
	
	const std::list<std::string>& GetCurrent() const;
	const std::list<std::string>& GetAtIndex(unsigned int index) const;
	
	unsigned int GetSize() const {return m_vectHistory.size();}
	unsigned int GetCurrentIndex() const {return m_iCurrentIndex;}
	
	// back, forward
	// history : (pretty) uri name
	// current locations, 

private:
	std::vector<std::list<std::string> > m_vectHistory;
	std::list<std::string> m_listEmpty;
	unsigned int m_iCurrentIndex;
};

#endif /*BROWSERHISTORY_H_*/
