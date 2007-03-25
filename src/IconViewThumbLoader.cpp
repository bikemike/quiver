#include "IconViewThumbLoader.h"

IconViewThumbLoader::IconViewThumbLoader(gint iThreads)
{
	m_uiNumCachePages = 12;
	m_bStopThreads    = false;
	m_ulRangeStart    = 0;
	m_ulRangeEnd      = 0;
	
	m_iThreads = iThreads;
	
	m_pConditions       = new pthread_cond_t[m_iThreads];
	m_pConditionMutexes = new pthread_mutex_t[m_iThreads];
	m_pThreadIDs        = new pthread_t[m_iThreads];
	
	m_pThreadData       = new ThreadData[m_iThreads];
	
	
	int i;
	for (i = 0 ; i < m_iThreads; i++)
	{
		pthread_cond_init(&m_pConditions[i],NULL);
		pthread_mutex_init(&m_pConditionMutexes[i], NULL);
		m_pThreadData[i].parent = this;
		m_pThreadData[i].id = i;
		
	}
	pthread_mutex_init(&m_ListMutex, NULL);
	
	for (i = 0 ; i < m_iThreads; i++)
	{
		pthread_create(&m_pThreadIDs[i], NULL, run, &m_pThreadData[i]);
	}
}
IconViewThumbLoader::~IconViewThumbLoader()
{
	m_bStopThreads = true;
	
	int i;
	for (i = 0 ; i < m_iThreads; i++)
	{
		// this call to gdk_threads_leave is made to make sure we dont get into
		// a deadlock between this thread(gui) and the IconViewThumbLoader thread which calls
		// gdk_threads_enter 
		gdk_threads_leave();

		pthread_mutex_lock (&m_pConditionMutexes[i]);
		pthread_cond_signal(&m_pConditions[i]);
		pthread_mutex_unlock (&m_pConditionMutexes[i]);

		pthread_join(m_pThreadIDs[i], NULL);
	}
	
	for (i = 0 ; i < m_iThreads; i++)
	{
		pthread_cond_destroy(&m_pConditions[i]);
		pthread_mutex_destroy(&m_pConditionMutexes[i]);
	}
	delete [] m_pConditions;
	delete [] m_pConditionMutexes;
	delete [] m_pThreadIDs;
	delete [] m_pThreadData;

	pthread_mutex_destroy(&m_ListMutex);
}


void IconViewThumbLoader::SetNumCachePages(guint uiNumCachePages)
{
	m_uiNumCachePages = uiNumCachePages;
}

void IconViewThumbLoader::UpdateList(bool bForce/* = false*/)
{
	gulong iNewStart,iNewEnd;

	GetVisibleRange(&iNewStart, &iNewEnd);

	if (bForce || m_ulRangeStart != iNewStart || m_ulRangeEnd != iNewEnd )
	{
		pthread_mutex_lock (&m_ListMutex);
		// view is different, we'll need to create a new list of items to cache
		
		m_ulRangeStart = iNewStart;
		m_ulRangeEnd   = iNewEnd;
		
		m_listThumbIndexes.clear();
		
		guint uiNumItems   = GetNumItems();
		guint uiNumVisible = m_ulRangeEnd - m_ulRangeStart;
	
		guint cache_size;
		cache_size = uiNumVisible * (m_uiNumCachePages + 1); 

		SetCacheSize(cache_size);

		guint i;
		for (i = m_ulRangeStart;i <= m_ulRangeEnd;i++)
		{
			m_listThumbIndexes.push_back(i);
		}
		
		bool bBreakFromLoop = false;
		for (i = 1 ; m_listThumbIndexes.size() < cache_size && !bBreakFromLoop; i++)
		{
			if (m_ulRangeStart >= i)
			{
				m_listThumbIndexes.push_back(m_ulRangeStart - i);
				bBreakFromLoop = false;
			}
			else
			{
				bBreakFromLoop = true;
			}
			
			if (uiNumItems > m_ulRangeEnd + i)
			{
				m_listThumbIndexes.push_back(m_ulRangeEnd + i);
				bBreakFromLoop = false;
			}
			
		}

		// now add the list to itself in reverse order so the cache
		// times of the items nearest the current page are newest
		std::list<guint> listFilesReversed;
		listFilesReversed = m_listThumbIndexes;
		listFilesReversed.reverse();
		m_listThumbIndexes.insert(m_listThumbIndexes.end(), listFilesReversed.begin() , listFilesReversed.end());
		
		int k;
		for (k = 0; k < m_iThreads; k++)
		{
			pthread_mutex_lock (&m_pConditionMutexes[k]);
			// signal the threads to start working
			pthread_cond_signal(&m_pConditions[k]);
			pthread_mutex_unlock (&m_pConditionMutexes[k]);
		}
		pthread_mutex_unlock (&m_ListMutex);
	}

}


void IconViewThumbLoader::LoadThumbnail(gulong ulIndex, guint uiWidth, guint uiHeigh)
{
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
	uiWidth  = 0;
	uiHeight = 0;
}

gulong IconViewThumbLoader::GetNumItems() 
{
	return 0l;
}

void IconViewThumbLoader::SetIsRunning(bool bIsRunning)
{

}

void IconViewThumbLoader::SetCacheSize(guint uiCacheSize)
{

}

void IconViewThumbLoader::Run(int iThreadID)
{
	while (true)
	{

		pthread_mutex_lock (&m_ListMutex);

		if (0 == m_listThumbIndexes.size() && !m_bStopThreads)
		{
			// unlock mutex
			pthread_mutex_unlock (&m_ListMutex);
			
			SetIsRunning(false);
			
			pthread_mutex_lock (&m_pConditionMutexes[iThreadID]);
			pthread_cond_wait(&m_pConditions[iThreadID],&m_pConditionMutexes[iThreadID]);
			pthread_mutex_unlock (&m_pConditionMutexes[iThreadID]);
			
			SetIsRunning(true);

		}
		else
		{
			pthread_mutex_unlock (&m_ListMutex);
		}
		
		if (m_bStopThreads)
		{
			break;
		}
		
		while (true)
		{
			if (m_bStopThreads)
			{
				break;
			}

			pthread_mutex_lock (&m_ListMutex);

			if (0 == m_listThumbIndexes.size())
			{
				pthread_mutex_unlock (&m_ListMutex);
				break;
			}
			
			guint n = m_listThumbIndexes.front();
			m_listThumbIndexes.pop_front();
			pthread_mutex_unlock (&m_ListMutex);
			
			guint width, height;
			GetIconSize(&width, &height);
			LoadThumbnail(n, width, height);
			
			if (m_bStopThreads)
			{
				break;
			}

			pthread_yield();
		}

		if (m_bStopThreads)
		{
			break;
		}
		

	}
}

void* IconViewThumbLoader::run(void * data)
{
	ThreadData *tdata = ((ThreadData*)data);
	tdata->parent->Run(tdata->id);
	return 0;
}

