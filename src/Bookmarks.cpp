#include <Bookmarks.h>
#include "Preferences.h"
#include <gtk/gtk.h>
#include <sstream>

#include "QuiverStockIcons.h"

#define BOOKMARKS_SECTION          "Bookmarks"
#define BOOKMARKS_CATEGORY_DEFAULT "default"
#define BOOKMARK_SECTION_PREFIX    "Bookmark_"
#define BOOKMARK_KEY_NAME          "name"
#define BOOKMARK_KEY_DESC          "description"
#define BOOKMARK_KEY_ICON          "icon"
#define BOOKMARK_KEY_RECURSE       "recursive"
#define BOOKMARK_KEY_URIS          "uris"

using namespace std;

BookmarksPtr Bookmarks::c_BookmarksPtr;

class Bookmarks::BookmarksImpl


{
public:
	BookmarksImpl(Bookmarks* bm) : m_pParent(bm) 
	{ 
		m_PrefPtr = Preferences::GetInstance();
	};

	Bookmarks* m_pParent;
	PreferencesPtr m_PrefPtr;

	typedef std::map<int, int> BookmarkOrderMap;
	BookmarkOrderMap m_mapBookmarkOrder;

	void SimplifySortOrder()
	{
		vector<int> v;

		BookmarkMap::iterator itr;
		for (itr = m_pParent->m_mapBookmarks.begin(); m_pParent->m_mapBookmarks.end() != itr; ++itr)
		{
			v.push_back(itr->second.GetID());
		}
		
		Bookmarks::BookmarksImpl::SortBySortOrder sortOrder(this);
		std::sort (v.begin(),v.end(), sortOrder);
		
		m_mapBookmarkOrder.clear();

		for (unsigned int i = 0; i < v.size(); ++i)
		{
			m_mapBookmarkOrder[v[i]] = i;
		}
	}

	int GetNewSortOrderValue()
	{
		int i = 0;
		SimplifySortOrder();
		BookmarkOrderMap::iterator itr;
		for (itr = m_mapBookmarkOrder.begin(); m_mapBookmarkOrder.end() != itr; ++itr)
		{
			if (i <= itr->second)
			{
				i = itr->second + 1;
			}
		}
		return i;
	}

	class SortBySortOrder : public std::binary_function<Bookmark,Bookmark,bool>,
		public std::binary_function<int,int,bool>
	{
	public:

		SortBySortOrder(BookmarksImpl *parent):m_pParent(parent){};
		bool operator()(const int &a, const int &b) const
		{
			return (m_pParent->m_mapBookmarkOrder[a] < m_pParent->m_mapBookmarkOrder[b]);
		}

		bool operator()(const Bookmark &a, const Bookmark &b) const
		{
			return (m_pParent->m_mapBookmarkOrder[a.GetID()] < m_pParent->m_mapBookmarkOrder[b.GetID()]);
		}
	private: 
		BookmarksImpl* m_pParent;
	};


};

BookmarksPtr Bookmarks::GetInstance()
{
	if (NULL == c_BookmarksPtr.get())
	{
		BookmarksPtr bookmarksPtr(new Bookmarks());
		c_BookmarksPtr = bookmarksPtr;
	}
	return c_BookmarksPtr;
}

Bookmarks::Bookmarks() : m_BookmarksImplPtr (new BookmarksImpl(this))
{
	LoadFromPreferences(); 
}

void Bookmarks::LoadFromPreferences()
{
	PreferencesPtr prefs = m_BookmarksImplPtr->m_PrefPtr;
	list<string> categories = prefs->GetKeys(BOOKMARKS_SECTION);
	list<string>::iterator itr;
	for (itr = categories.begin(); categories.end() != itr; ++itr)
	{
		list<int> bookmark_ids = prefs->GetIntegerList(BOOKMARKS_SECTION,*itr);
		list<int>::iterator itr2;
		int sort_order = 0;
		
		for ( itr2 = bookmark_ids.begin() ; bookmark_ids.end() != itr2; ++itr2)
		{
			m_BookmarksImplPtr->m_mapBookmarkOrder[*itr2] = sort_order;
			++sort_order;
			
			stringstream ss;
 			ss << BOOKMARK_SECTION_PREFIX << *itr2;
			string section = ss.str();
			if (prefs->HasSection(section))
			{
				string name, desc, icon;
				list<string> uris;
				bool recursive;

				name = prefs->GetString(section,BOOKMARK_KEY_NAME);
				desc = prefs->GetString(section,BOOKMARK_KEY_DESC);
				icon = prefs->GetString(section,BOOKMARK_KEY_ICON);
				uris = prefs->GetStringList(section,BOOKMARK_KEY_URIS);
				recursive = prefs->GetBoolean(section,BOOKMARK_KEY_RECURSE);
				
				if (icon.empty())
				{
					icon = QUIVER_STOCK_DIRECTORY;
				}
				
				Bookmark b(name, desc, icon, uris,recursive);
				b.SetCategory(*itr);
				b.SetID(*itr2);
				m_mapBookmarks[*itr2] = b;
			}
		}
	}
}

void Bookmarks::SaveToPreferences()
{
	BookmarkMap::iterator itr;
	vector<Bookmark>::iterator itr2;

	PreferencesPtr prefs = m_BookmarksImplPtr->m_PrefPtr;

	map<string,vector<int> > categories;
	map<string,vector<int> >::iterator cat_itr;

	for (itr = m_mapBookmarks.begin(); m_mapBookmarks.end() != itr; ++itr)
	{
		stringstream ss;
		ss << BOOKMARK_SECTION_PREFIX << itr->first;
		string section = ss.str();

		prefs->SetString(section,BOOKMARK_KEY_NAME, itr->second.GetName());
		prefs->SetString(section,BOOKMARK_KEY_DESC, itr->second.GetDescription());

		if (itr->second.GetIcon().empty())
		{
			itr->second.SetIcon(QUIVER_STOCK_DIRECTORY);
		}
		prefs->SetString(section,BOOKMARK_KEY_ICON, itr->second.GetIcon());

		list<string> uris = itr->second.GetURIs();
		prefs->SetStringList(section,BOOKMARK_KEY_URIS, uris);

		prefs->SetBoolean(section,BOOKMARK_KEY_RECURSE, itr->second.GetRecursive());

		categories[itr->second.GetCategory()].push_back(itr->first);

	}
	for (cat_itr = categories.begin(); categories.end() != cat_itr; ++cat_itr)
	{
	
		Bookmarks::BookmarksImpl::SortBySortOrder sortOrder(m_BookmarksImplPtr.get());
		std::sort (cat_itr->second.begin(),cat_itr->second.end(), sortOrder);
		list<int> cat_list(cat_itr->second.begin(), cat_itr->second.end());
		prefs->SetIntegerList(BOOKMARKS_SECTION,cat_itr->first,cat_list);
	}
}


bool Bookmarks::AddBookmark(Bookmark bookmark)
{
	bool rval = true;
	if (bookmark.GetCategory().empty())
	{
		bookmark.SetCategory(BOOKMARKS_CATEGORY_DEFAULT);
	}

	if (bookmark.GetIcon().empty())
	{
		bookmark.SetIcon(QUIVER_STOCK_DIRECTORY);
	}

	BookmarkMap::iterator itr;
	// for now , dont care if a bookmark already exists..
	bool bFound = false;

	if (false == bFound)
	{
		bookmark.SetID(GetUniqueID());
		int sort_val = m_BookmarksImplPtr->GetNewSortOrderValue();
		m_BookmarksImplPtr->m_mapBookmarkOrder[bookmark.GetID()] = sort_val;
		m_mapBookmarks[bookmark.GetID()] = bookmark;
		EmitBookmarkChangedEvent(BookmarksEvent::BOOKMARK_ADDED);
	}
	else
	{
		rval = false;
	}

	return rval;
}

bool Bookmarks::AddBookmark(Bookmark bookmark, std::string category )
{
	if (category.empty())
	{
		category = BOOKMARKS_CATEGORY_DEFAULT;
	}
	bookmark.SetCategory(category);
	return AddBookmark(bookmark);
}


bool Bookmarks::Remove(int id)
{
	bool rval = false;
	BookmarkMap::iterator itr;
	itr = m_mapBookmarks.find(id);
	if (m_mapBookmarks.end() != itr)
	{
		stringstream ss;
		ss << BOOKMARK_SECTION_PREFIX << itr->first;
		string section = ss.str();

		m_BookmarksImplPtr->m_PrefPtr->RemoveSection(section);
		
		m_mapBookmarks.erase(itr);
		rval = true;
		EmitBookmarkChangedEvent(BookmarksEvent::BOOKMARK_REMOVED);
	}

	BookmarksImpl::BookmarkOrderMap::iterator itr2;
	itr2 = m_BookmarksImplPtr->m_mapBookmarkOrder.find(id);
	if (m_BookmarksImplPtr->m_mapBookmarkOrder.end() != itr2)
	{
		m_BookmarksImplPtr->m_mapBookmarkOrder.erase(itr2);
	}

	return rval;
}

const Bookmark* Bookmarks::GetBookmark(int id)
{
	Bookmark* b = NULL;
	BookmarkMap::iterator itr;
	itr = m_mapBookmarks.find(id);
	if (m_mapBookmarks.end() != itr)
	{
		b = &itr->second;
	}
	return b;
}

std::vector<Bookmark> Bookmarks::GetBookmarks() 
{
	return GetBookmarks(BOOKMARKS_CATEGORY_DEFAULT);
}

std::vector<Bookmark> Bookmarks::GetBookmarks(std::string category) 
{
	vector<Bookmark> v;

	BookmarkMap::iterator itr;
	for (itr = m_mapBookmarks.begin(); m_mapBookmarks.end() != itr; ++itr)
	{
		if (category == itr->second.GetCategory())
		{
			v.push_back(itr->second);
		}
	}
	
	Bookmarks::BookmarksImpl::SortBySortOrder sortOrder(m_BookmarksImplPtr.get());
	std::sort (v.begin(),v.end(), sortOrder);
	return v;
}

int Bookmarks::GetUniqueID() const
{
	int i = 0;
	while (m_mapBookmarks.end() != m_mapBookmarks.find(i))
	{
		++i;
	}
	return i;
}


bool Bookmarks::UpdateBookmark(Bookmark bookmark)
{
	bool bUpdated = false;
	BookmarkMap::iterator itr;
	itr = m_mapBookmarks.find(bookmark.GetID());
	if (m_mapBookmarks.end() != itr)
	{
		itr->second = bookmark;
		bUpdated = true;
		EmitBookmarkChangedEvent(BookmarksEvent::BOOKMARK_CHANGED);
	}
	return bUpdated;
}

bool Bookmarks::MoveUp (int id)
{
	BookmarkMap::iterator itr = m_mapBookmarks.find(id);
	if (m_mapBookmarks.end() != itr)
	{
		int swapID = id;
		Bookmark b = itr->second;
		int orderid = m_BookmarksImplPtr->m_mapBookmarkOrder[itr->first];
		int new_orderid = orderid;
		
		for (itr = m_mapBookmarks.begin(); m_mapBookmarks.end() != itr; ++itr)
		{
			if (itr->second.GetCategory() == b.GetCategory())
			{
				int other_orderid = m_BookmarksImplPtr->m_mapBookmarkOrder[itr->first];
				if (other_orderid < orderid && (other_orderid > new_orderid || new_orderid == orderid))
				{
					new_orderid = other_orderid;
					swapID = itr->first;
				}
			}
		}
		if (swapID != id)
		{
			orderid = m_BookmarksImplPtr->m_mapBookmarkOrder[id];
			m_BookmarksImplPtr->m_mapBookmarkOrder[id] = new_orderid;
			m_BookmarksImplPtr->m_mapBookmarkOrder[swapID] = orderid;
			EmitBookmarkChangedEvent(BookmarksEvent::BOOKMARK_CHANGED);
			return true;
		}
	}
	return false;

}

bool Bookmarks::MoveDown (int id)
{
	BookmarkMap::iterator itr = m_mapBookmarks.find(id);
	if (m_mapBookmarks.end() != itr)
	{
		int swapID = id;
		Bookmark b = itr->second;
		int orderid = m_BookmarksImplPtr->m_mapBookmarkOrder[itr->first];
		int new_orderid = orderid;
		
		for (itr = m_mapBookmarks.begin(); m_mapBookmarks.end() != itr; ++itr)
		{
			if (itr->second.GetCategory() == b.GetCategory())
			{
				int other_orderid = m_BookmarksImplPtr->m_mapBookmarkOrder[itr->first];
				if (other_orderid > orderid && (other_orderid < new_orderid || new_orderid == orderid))
				{
					new_orderid = other_orderid;
					swapID = itr->first;
				}
			}
		}
		if (swapID != id)
		{
			orderid = m_BookmarksImplPtr->m_mapBookmarkOrder[id];
			m_BookmarksImplPtr->m_mapBookmarkOrder[id] = new_orderid;
			m_BookmarksImplPtr->m_mapBookmarkOrder[swapID] = orderid;
			EmitBookmarkChangedEvent(BookmarksEvent::BOOKMARK_CHANGED);
			return true;
		}
	}
	return false;
}



