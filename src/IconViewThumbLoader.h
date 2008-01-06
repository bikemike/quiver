#ifndef FILE_THUMBNAIL_LOADER_H
#define FILE_THUMBNAIL_LOADER_H

#include <pthread.h>
#include <list>
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
	bool                m_bStopThreads;
	ThreadData*         m_pThreadData;

	pthread_t*          m_pThreadIDs;
	pthread_cond_t*     m_pConditions;
	pthread_mutex_t*    m_pConditionMutexes;
	pthread_mutex_t     m_ListMutex;
	
	std::list<ThumbLoaderItem>    m_listThumbItems;


	gulong              m_ulRangeStart, 
                        m_ulRangeEnd;
	guint               m_bLargeThumbs;

	guint               m_uiNumCachePages;
};

#endif

