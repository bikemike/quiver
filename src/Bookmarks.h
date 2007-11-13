#ifndef FILE_BOOKMARKS_H
#define FILE_BOOKMARKS_H
#include <vector>
#include <list>
#include <map>
#include <string>
#include <boost/shared_ptr.hpp>

#include "BookmarksEventSource.h"


class Bookmarks;
typedef boost::shared_ptr<Bookmarks> BookmarksPtr;

class Bookmark
{
public:
	Bookmark(){};
	Bookmark(std::string name, std::string desc, std::string icon,
		std::list<std::string> listURIs, bool bRecursive) :
		m_strName(name), m_strDescription(desc), m_strIcon(icon),
		m_listURIs(listURIs), m_bRecursive(bRecursive)
		{};
	~Bookmark(){};

	std::string GetName() const {return m_strName;};
	void SetName(std::string name){m_strName = name;};
	
	std::string GetCategory() const {return m_strCategory;};
	void SetCategory(std::string cat){m_strCategory = cat;};

	std::string GetDescription() const{return m_strDescription;};
	void SetDescription(std::string desc){m_strDescription = desc;};

	std::string GetIcon() const{return m_strIcon;};
	void SetIcon(std::string icon) { m_strIcon = icon;};

	std::list<std::string> GetURIs() const {return m_listURIs;};
	void SetURIs(std::list<std::string> uris){m_listURIs = uris;};

	bool GetRecursive() const{return m_bRecursive;};
	void SetRecursive(bool bRecursive){m_bRecursive = bRecursive;};

	int GetID() const{return m_iID;};
private:
	void SetID(int id){m_iID = id;};
	std::string m_strName;	
	std::string m_strCategory;	
	std::string m_strDescription;	
	std::string m_strIcon;
	std::list<std::string> m_listURIs;
	bool m_bRecursive;
	int m_iID;
	// friend for SetID function
	friend class Bookmarks;
};

class Bookmarks : public BookmarksEventSource
{
public:
	static BookmarksPtr GetInstance();
	~Bookmarks(){ SaveToPreferences(); };

	bool AddBookmark(Bookmark bookmark);
	bool AddBookmark(Bookmark bookmark, std::string category );

	bool UpdateBookmark(Bookmark bookmark);
	bool MoveUp (int id);
	bool MoveDown (int id);

	int GetUniqueID() const;

	const Bookmark* GetBookmark(int id); 
	bool Remove(int id);

	std::vector<Bookmark> GetBookmarks(); 
	std::vector<Bookmark> GetBookmarks(std::string category);

	class BookmarksImpl;
	typedef boost::shared_ptr<BookmarksImpl> BookmarksImplPtr;
private:

	Bookmarks();
	void LoadFromPreferences();
	void SaveToPreferences();

	typedef std::map<int, Bookmark > BookmarkMap;
	BookmarkMap m_mapBookmarks;

	static BookmarksPtr c_BookmarksPtr;
	BookmarksImplPtr m_BookmarksImplPtr;
};


#endif // FILE_BOOKMARKS_H

