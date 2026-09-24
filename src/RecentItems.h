#ifndef FILE_RECENT_ITEMS_H
#define FILE_RECENT_ITEMS_H

#include <string>
#include <deque>
#include <list>
#include "ImageListAttributes.h"

/* Most-recently-viewed items list.  Each entry pairs the item's URI with the
 * ImageListAttributes of the list it was viewed in, so clicking a recent item
 * can re-open it inside that same list context.  Entries from the same list
 * share one ImageListAttributesPtr, so the folder set is never duplicated. */

class RecentItems
{
public:
	struct Entry
	{
		std::string uri;
		ImageListAttributesPtr pAttributes;
	};

	RecentItems(size_t iMaxEntries = 15);

	/* Record a viewed item: dedupes (move-to-front, updating the stored list
	 * attributes to the latest context) and trims beyond the cap. */
	void Record(const std::string& uri, ImageListAttributesPtr attributes);

	/* Deletion pruning: remove every entry whose URI appears in uris.
	 * Returns the number of entries removed. */
	size_t RemoveAllByURIs(const std::list<std::string>& uris);
	bool RemoveByURI(const std::string& uri);

	/* Newest-first scan for uri, or NULL (does not reorder). */
	const Entry* Find(const std::string& uri) const;

	const std::deque<Entry>& GetEntries() const { return m_Entries; }
	size_t GetSize() const { return m_Entries.size(); }
	size_t GetMaxEntries() const { return m_iMaxEntries; }
	void SetMaxEntries(size_t iMaxEntries);
	void Clear() { m_Entries.clear(); }

private:
	std::deque<Entry> m_Entries;
	size_t m_iMaxEntries;
};

#endif