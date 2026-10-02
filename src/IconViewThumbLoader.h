#ifndef FILE_THUMBNAIL_LOADER_H
#define FILE_THUMBNAIL_LOADER_H

#include <pthread.h>
#include <atomic>
#include <list>
#include <vector>
#include <gtk/gtk.h>
#include <string>

#include "QuiverFile.h"

class ThumbLoaderItem
{
public:
	ThumbLoaderItem() : m_ulIndex(0) {};
	ThumbLoaderItem(gulong index, QuiverFile f) : m_ulIndex(index), m_QuiverFile(f) {};
	gulong m_ulIndex;
	QuiverFile m_QuiverFile;
};

class IconViewThumbLoader
{
public:
	IconViewThumbLoader(gint nThreads, bool bAutoStart = true);
	virtual ~IconViewThumbLoader();

	void Start();
	void Stop();
	bool IsStopped() const { return m_bStopThreads.load(std::memory_order_relaxed); }

	virtual void UpdateList(bool bForce = false);
	void SetNumCachePages(guint uiNumCachePages);

	/* The order in which thumbnail loads should be queued for a viewport
	 * showing [range_start, range_end] of a list of @n_items, capped at
	 * @max_items loads and ordered for travel in @scroll_dir.
	 *
	 * Exposed (and free of any thread or widget state) so the ordering can be
	 * unit tested without driving a scroll.
	 *
	 * @scroll_dir is -1 when the view moved towards index 0, +1 towards the
	 * end, and 0 when the direction is unknown or the range did not move.
	 */
	static std::vector<gulong> BuildLoadOrder(gulong range_start,
		gulong range_end, guint n_items, guint max_items, int scroll_dir);

	/* How many thumbnails to keep, given the list size and the size of one
	 * decoded thumbnail.  The old figure was visible_cells * 13, which is a
	 * multiple of a page count rather than anything to do with memory: it held
	 * 520 of a 595-item folder, so the top of the list was evicted while still
	 * on screen and re-decoded on every repaint.  Sizing from the item count
	 * instead means a folder that fits in the budget is decoded once and stays
	 * decoded, and a folder that does not fit is capped rather than growing
	 * without limit.
	 *
	 * @n_items is the list size, @bytes_per_item the memory one decoded
	 * thumbnail needs, and @budget_bytes the ceiling to fit them under.
	 * @prefetch_items is the floor, so a single screen of cells always has room
	 * to prefetch into.
	 */
	static guint ComputeCacheSize(guint n_items, guint bytes_per_item,
		guint budget_bytes, guint prefetch_items);


	typedef struct _ThreadData
	{
		IconViewThumbLoader *parent;
		gint id;
	} ThreadData;

protected:
	virtual void LoadThumbnail(const ThumbLoaderItem &item, guint uiWidth, guint uiHeight);
	virtual void GetVisibleRange(gulong* ulStart, gulong* ulEnd);
	virtual void GetIconSize(guint* uiWidth, guint* uiHeight);
	virtual gulong GetNumItems();
	virtual void SetIsRunning(bool bIsRunning);
	virtual void SetCacheSize(guint uiSize);
	virtual QuiverFile GetQuiverFile(gulong ulIndex);
	
private:
	static void* run(void *data);
	void Run(int iThreadID);

	
	gint                m_iThreads;
	std::atomic<bool>   m_bStopThreads;
	std::atomic<bool>   m_bThreadsStarted;
	ThreadData*         m_pThreadData;

	pthread_t*          m_pThreadIDs;
	pthread_cond_t*     m_pConditions;
	pthread_mutex_t     m_ListMutex;
	
	std::list<ThumbLoaderItem>    m_listThumbItems;


	gulong              m_ulRangeStart, 
                        m_ulRangeEnd;
	guint               m_bLargeThumbs;
	/* -1 travelling towards index 0, +1 towards the end. */
	int                 m_iScrollDir;

	guint               m_uiNumCachePages;
};

#endif

