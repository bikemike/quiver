#ifndef BROWSERHISTORY_H_
#define BROWSERHISTORY_H_
#include <boost/shared_ptr.hpp>
#include <vector>
#include <list>
#include <set>
#include <string>

/*
class BrowserHistoryImpl;
typedef boost::shared_ptr<BrowserHistoryImpl> BrowserHistoryImplPtr;
*/

struct BrowserHistoryEntry
{
	std::list<std::string> listItems;
	std::string selectedItem;
	bool bRecursive = false;
	std::set<std::string> recursiveFolders;
};

class BrowserHistory
{
public:
	BrowserHistory();

	void Add(const std::list<std::string>& listItems, std::string selectedItem,
	         bool bRecursive = false,
	         const std::set<std::string>& recursiveFolders = std::set<std::string>());

	bool HasIndex(unsigned int index) const;
	bool CanGoForward() const;
	bool CanGoBack() const;
	
	bool GoForward();
	bool GoBack();
	
	const std::list<std::string>& GetCurrentFiles() const;
	std::string                   GetCurrentSelected() const;
	void                          SetCurrentSelected(std::string selected);
	bool                          GetCurrentRecursive() const;
	const std::set<std::string>&  GetCurrentRecursiveFolders() const;

	const std::list<std::string>& GetFilesAtIndex(unsigned int index) const;
	std::string                   GetSelectedAtIndex(unsigned int index) const;
	
	unsigned int GetSize() const {return m_vectHistory.size();}
	unsigned int GetCurrentIndex() const {return m_iCurrentIndex;}

	
	// back, forward
	// history : (pretty) uri name
	// current locations, 

private:
	std::vector<BrowserHistoryEntry> m_vectHistory;
	std::list<std::string> m_listEmpty;
	std::set<std::string>  m_setEmpty;
	unsigned int m_iCurrentIndex;
};

#endif /*BROWSERHISTORY_H_*/
