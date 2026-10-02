#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "IconViewThumbLoader.h"
#include "ThreadUtil.h"
#include <sched.h>
#include <glib.h>

/* Ceiling for the decoded-thumbnail cache.  At 128x128 a thumbnail is 64 KB, so
 * this holds a few thousand of them; at 256x256 it is 256 KB and this holds a
 * thousand.  Anything larger than this is capped rather than allowed to grow
 * with the folder, so opening a directory of 50k images stays bounded. */
static const guint THUMB_CACHE_BUDGET_BYTES = 256u * 1024u * 1024u;

guint IconViewThumbLoader::ComputeCacheSize(guint n_items,
	guint bytes_per_item, guint budget_bytes, guint prefetch_items)
{
	if (0 == n_items || 0 == bytes_per_item)
		return 1;

	guint n = n_items;

	if (n < prefetch_items)
		n = prefetch_items;

	if (budget_bytes / bytes_per_item > 0 &&
		n > budget_bytes / bytes_per_item)
		n = budget_bytes / bytes_per_item;

	return n;
}

IconViewThumbLoader::IconViewThumbLoader(gint iThreads, bool bAutoStart)
{
	m_uiNumCachePages = 12;
	m_bStopThreads    = false;
	m_bThreadsStarted = false;
	m_ulRangeStart    = 0;
	m_ulRangeEnd      = 0;
	m_iScrollDir      = 0;
	
	m_iThreads = iThreads;
	
	m_pConditions = new pthread_cond_t[m_iThreads];
	m_pThreadIDs  = new pthread_t[m_iThreads];
	
	m_pThreadData = new ThreadData[m_iThreads];
	
	int i;
	for (i = 0 ; i < m_iThreads; ++i)
	{
		pthread_cond_init(&m_pConditions[i],NULL);
		m_pThreadData[i].parent = this;
		m_pThreadData[i].id = i;
		m_pThreadIDs[i] = 0;
	}
	pthread_mutex_init(&m_ListMutex, NULL);
	
	if (bAutoStart)
	{
		Start();
	}
}

void IconViewThumbLoader::Start()
{
	pthread_mutex_lock (&m_ListMutex);
	if (m_bThreadsStarted || m_bStopThreads)
	{
		pthread_mutex_unlock (&m_ListMutex);
		return;
	}
	m_bThreadsStarted = true;
	for (int i = 0 ; i < m_iThreads; ++i)
	{
		pthread_create(&m_pThreadIDs[i], NULL, run, &m_pThreadData[i]);
	}
	pthread_mutex_unlock (&m_ListMutex);
}

void IconViewThumbLoader::Stop()
{
	pthread_mutex_lock (&m_ListMutex);
	if (m_bStopThreads)
	{
		pthread_mutex_unlock (&m_ListMutex);
		return;
	}
	m_bStopThreads = true;
	m_listThumbItems.clear();

	int i;
	for (i = 0 ; i < m_iThreads; ++i)
	{
		pthread_cond_signal(&m_pConditions[i]);
	}
	pthread_mutex_unlock (&m_ListMutex);

	for (i = 0 ; i < m_iThreads; ++i)
	{
		if (m_pThreadIDs && m_pThreadIDs[i])
		{
			pthread_join(m_pThreadIDs[i], NULL);
			m_pThreadIDs[i] = 0;
		}
	}
}

IconViewThumbLoader::~IconViewThumbLoader()
{
	Stop();

	for (int i = 0 ; i < m_iThreads; ++i)
	{
		pthread_cond_destroy(&m_pConditions[i]);
	}
	delete [] m_pConditions;
	delete [] m_pThreadIDs;
	delete [] m_pThreadData;

	pthread_mutex_destroy(&m_ListMutex);
}


void IconViewThumbLoader::SetNumCachePages(guint uiNumCachePages)
{
	m_uiNumCachePages = uiNumCachePages;
}

/* Order the loads for one viewport.
 *
 * The queue is consumed FIFO by Run(), so the position of an entry here is
 * the order in which a worker thread gets to it.  Two rules follow:
 *
 *  - What is on screen comes first.  A thumbnail the user is looking at beats
 *    one they may scroll to later.
 *  - After that, load in the direction of travel, nearest first.  Scrolling up
 *    means the cells about to arrive are the ones just above the viewport, so
 *    they should be next; likewise downwards.  Loading against the direction of
 *    travel makes the list appear to fill in from the wrong end, which is what
 *    the previous version did.
 *
 * The old code appended a reversed copy of the whole queue to its tail, which
 * had two problems: every item was queued twice, and because the copy was last,
 * the items decoded at the very end - the ones furthest from the viewport -
 * ended up with the newest cache timestamps, the opposite of what its comment
 * claimed.  Cache times need no such help: ImageCache::GetTexture refreshes an
 * entry's timestamp on every hit, so simply reading the visible cells is
 * enough to keep them at the young end of the LRU order.
 */
std::vector<gulong> IconViewThumbLoader::BuildLoadOrder(gulong range_start,
	gulong range_end, guint n_items, guint max_items, int scroll_dir)
{
	std::vector<gulong> order;
	if (0 == n_items || 0 == max_items)
		return order;

	if (range_start >= range_end)
	{
		/* A single visible cell still has to be queued; there is just no
		 * direction to order outwards in. */
		if (range_start < n_items)
			order.push_back(range_start);
		return order;
	}

	/* The viewport can legitimately extend past the end of the list while it is
	 * still settling. */
	if (range_end >= n_items)
		range_end = n_items - 1;
	if (range_start > range_end)
		range_start = range_end;

	/* Visible cells, nearest end of the viewport first.  With an unknown
	 * direction either end is as good as the other, so keep them in list order
	 * rather than inventing a preference.  The cap is honoured by starting at
	 * the end that matters: if there is only room for a few, they are the cells
	 * at the trailing edge of the direction of travel, not the ones the cap
	 * happens to cut off in list order. */
	if (scroll_dir < 0)
	{
		for (gulong i = range_end + 1; i-- > range_start && order.size() < max_items; )
			order.push_back(i);
	}
	else
	{
		for (gulong i = range_start; i <= range_end && order.size() < max_items; ++i)
			order.push_back(i);
	}

	/* Then work outwards from the viewport, nearest first.  The side being
	 * travelled towards is drained before the side left behind, so the cells
	 * about to scroll into view are not stuck behind cells that have already
	 * gone past.  The trailing side is still queued once the leading one runs
	 * out, so a long scroll does not leave a gap behind. */
	int lead = (scroll_dir == 0) ? +1 : scroll_dir;
	gulong before = range_start;      /* one past the lowest index still free */
	gulong after  = range_end + 1;    /* lowest index still free above */
	while (order.size() < max_items && (before > 0 || after < n_items))
	{
		bool took_one = false;

		if (lead < 0)
		{
			if (before > 0)
			{
				order.push_back(--before);
				took_one = true;
			}
			else if (after < n_items)
			{
				order.push_back(after++);
				took_one = true;
			}
		}
		else
		{
			if (after < n_items)
			{
				order.push_back(after++);
				took_one = true;
			}
			else if (before > 0)
			{
				order.push_back(--before);
				took_one = true;
			}
		}

		if (!took_one)
			break;
	}

	return order;
}

void IconViewThumbLoader::UpdateList(bool bForce/* = false*/)
{
	if (m_bStopThreads)
		return;

	gulong iNewStart,iNewEnd;

	GetVisibleRange(&iNewStart, &iNewEnd);

	// FIXME: does this check cause thumbnails not to 
	// reload if the size changes but the range doesn't?
	if (bForce || m_ulRangeStart != iNewStart || m_ulRangeEnd != iNewEnd )
	{
		pthread_mutex_lock (&m_ListMutex);
		// view is different, we'll need to create a new list of items to cache
		
		/* Work out which way the view moved, so the loads past the visible
		 * range can be queued in the direction the user is heading.  Only the
		 * start index is needed: a range that changed size in place has not
		 * travelled, and treating that as a scroll would flip the order on
		 * every icon-size change. */
		if (bForce || (m_ulRangeStart == 0 && m_ulRangeEnd == 0))
		{
			m_iScrollDir = 0;
		}
		else if (iNewStart < m_ulRangeStart)
		{
			m_iScrollDir = -1;
		}
		else if (iNewStart > m_ulRangeStart)
		{
			m_iScrollDir = +1;
		}
		m_ulRangeStart = iNewStart;
		m_ulRangeEnd   = iNewEnd;
		
		m_listThumbItems.clear();
		
		guint uiNumItems = GetNumItems();

		/* Size the cache from the list size and the real decoded size of one
		 * thumbnail instead of a multiple of the viewport.  A GdkTexture is
		 * RGBA, so bytes is width * height * 4, and w/h here are the icon size,
		 * which is the size the thumbnail was requested at.  The prefetch page
		 * count still sets the floor, so a single screen of cells has room to
		 * prefetch into while a large folder is scrolling. */
		guint w = 0, h = 0;
		GetIconSize(&w, &h);
		if (0 == w || 0 == h)
			w = h = 128;

		const guint bytes_per_item = w * h * 4;
		const guint cache_size = ComputeCacheSize(uiNumItems, bytes_per_item,
			THUMB_CACHE_BUDGET_BYTES, 2 * (m_uiNumCachePages + 1));

		SetCacheSize(cache_size);

		std::vector<gulong> order = BuildLoadOrder(iNewStart, iNewEnd,
			uiNumItems, cache_size, m_iScrollDir);
		for (std::vector<gulong>::const_iterator i = order.begin();
			i != order.end(); ++i)
		{
			m_listThumbItems.push_back(
				ThumbLoaderItem(*i, GetQuiverFile(*i)) );
		}
		
		int k;
		for (k = 0; k < m_iThreads; k++)
		{
			// signal the threads to start working
			pthread_cond_signal(&m_pConditions[k]);
		}
		pthread_mutex_unlock (&m_ListMutex);
	}

}


void IconViewThumbLoader::LoadThumbnail(const ThumbLoaderItem &item, guint uiWidth, guint uiHeigh)
{ (void)uiHeigh;  (void)uiWidth;  (void)item; 
	// load the thumbnail
}

void IconViewThumbLoader::GetVisibleRange(gulong* ulStart, gulong* ulEnd)
{
	// get the visible range
	*ulStart = 0l;
	*ulEnd   = 0l;
}

void IconViewThumbLoader::GetIconSize(guint* uiWidth, guint* uiHeight) 
{
	if (uiWidth) *uiWidth  = 0;
	if (uiHeight) *uiHeight = 0;
}

gulong IconViewThumbLoader::GetNumItems() 
{
	return 0l;
}

QuiverFile IconViewThumbLoader::GetQuiverFile(gulong index) 
{ (void)index; 
	return QuiverFile();
}

void IconViewThumbLoader::SetIsRunning(bool bIsRunning)
{ (void)bIsRunning; 

}

void IconViewThumbLoader::SetCacheSize(guint uiCacheSize)
{ (void)uiCacheSize; 

}

void IconViewThumbLoader::Run(int iThreadID)
{
	while (true)
	{
		pthread_mutex_lock (&m_ListMutex);

		// wait for work.  the predicate and the wait share m_ListMutex so
		// that UpdateList() cannot lose a wakeup to a thread that is
		// between the empty check and cond_wait
		while (0 == m_listThumbItems.size() && !m_bStopThreads)
		{
			SetIsRunning(false);
			pthread_cond_wait(&m_pConditions[iThreadID], &m_ListMutex);
			if (!m_bStopThreads)
			{
				SetIsRunning(true);
			}
		}

		if (m_bStopThreads)
		{
			pthread_mutex_unlock (&m_ListMutex);
			break;
		}

		while (0 != m_listThumbItems.size() && !m_bStopThreads)
		{
			ThumbLoaderItem item = m_listThumbItems.front();
			m_listThumbItems.pop_front();
			pthread_mutex_unlock (&m_ListMutex);

			guint width, height;
			GetIconSize(&width, &height);
			LoadThumbnail(item, width, height);

			pthread_mutex_lock (&m_ListMutex);
		}

		pthread_mutex_unlock (&m_ListMutex);

		if (m_bStopThreads)
		{
			break;
		}

		sched_yield();
	}
}

void* IconViewThumbLoader::run(void * data)
{
	ThreadData *tdata = ((ThreadData*)data);
	tdata->parent->Run(tdata->id);
	return 0;
}

