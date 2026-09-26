#ifndef FILE_FOLDER_TREE_EVENT_H
#define FILE_FOLDER_TREE_EVENT_H

#include <list>
#include <string>

#include "EventBase.h"

class FolderTreeEvent : public EventBase
{
public:
	FolderTreeEvent(IEventSourcePtr src) : EventBase(src), m_bRecursive(false), m_bPreserveCurrentIndex(true) {};

	void SetURIs(const std::list<std::string>& uris){m_URIs = uris;};
	const std::list<std::string>& GetURIs() const {return m_URIs;};

	void SetRecursive(bool bRecursive){m_bRecursive = bRecursive;};
	bool GetRecursive() const {return m_bRecursive;};

	bool HasBookmarkData() const {return !m_URIs.empty();};

	/* True when the new checked set merely extends the list already loaded
	 * (a checkbox was ticked), false when it replaces it (a plain click on a
	 * row).  See ImageList::UpdateImageListAsync(..., bPreserveCurrentIndex). */
	void SetPreserveCurrentIndex(bool bPreserve){m_bPreserveCurrentIndex = bPreserve;};
	bool PreserveCurrentIndex() const {return m_bPreserveCurrentIndex;};

private:
	std::list<std::string> m_URIs;
	bool m_bRecursive;
	bool m_bPreserveCurrentIndex;
};

typedef boost::shared_ptr<FolderTreeEvent> FolderTreeEventPtr;


#endif

