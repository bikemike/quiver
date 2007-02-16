#ifndef FILE_THUMBNAIL_LOADER_H
#define FILE_THUMBNAIL_LOADER_H

#include <pthread.h>
#include <list>
#include <gtk/gtk.h>

class IconViewThumbLoader
{
public:
	IconViewThumbLoader(gint nThreads);
	virtual ~IconViewThumbLoader();

	virtual void UpdateList(bool bForce = false);
	void SetNumCachePages(guint uiNumCachePages);


	typedef struct _ThreadData
	{
		IconViewThumbLoader *parent;
		gint id;
	} ThreadData;

protected:
	virtual void LoadThumbnail(gulong ulIndex, guint uiWidth, guint uiHeight);
	virtual void GetVisibleRange(gulong* ulStart, gulong* ulEnd);
	virtual void GetIconSize(guint* uiWidth, guint* uiHeight);
	virtual gulong GetNumItems();
	virtual void SetIsRunning(bool bIsRunning);
	virtual void SetCacheSize(guint uiSize);
	
private:
	static void* run(void *data);
	void Run(int iThreadID);

	
	gint                m_iThreads;
	bool                m_bStopThreads;
	ThreadData*         m_pThreadData;

	pthread_t*          m_pThreadIDs;
	pthread_cond_t*     m_pConditions;
	pthread_mutex_t*    m_pConditionMutexes;
	pthread_mutex_t     m_ListMutex;
	
	std::list<guint>    m_listThumbIndexes;


	gulong              m_ulRangeStart, 
                        m_ulRangeEnd;
	guint               m_bLargeThumbs;

	guint               m_uiNumCachePages;
};

#endif

