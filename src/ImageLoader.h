#ifndef FILE_IMAGELOADER_H
#define FILE_IMAGELOADER_H

#include <pthread.h>

#include <string>
#include <list>
#include <iostream>
#include <fstream>
#include "Timer.h"
#include "ImageCache.h"

#include "QuiverFile.h"
#include "PixbufLoaderObserver.h"


class ImageLoader : public PixbufLoaderObserver
{
	
public:

	typedef enum _State
	{
		LOAD,
		CACHE,
		CACHE_LOAD,
	} State;

	typedef struct _LoadParams
	{
		int orientation;
		bool reload;
		bool fullsize;
		bool no_thumb_preview;
		State state;
	} LoadParams;



	ImageLoader();
	~ImageLoader();

	void LoadImage(QuiverFile);	
	void LoadImage(QuiverFile,LoadParams load_params);	

	void CacheImage(QuiverFile);
	void ReloadImage(QuiverFile);
	
	// thread functions
	static void* run(void *data);
	int Run();
	
	void SignalSizePrepared(GdkPixbufLoader *loader,gint width, gint height);
	void AddPixbufLoaderObserver(IPixbufLoaderObserver * loader_observer);
	void RemovePixbufLoaderObserver(IPixbufLoaderObserver * loader_observer);
	
private:	
	void Load();
	bool LoadPixbuf(GdkPixbufLoader *loader);
	bool CommandsPending();
	
	void LoadQuickPreview();

	pthread_t m_pthread_id;

	pthread_cond_t m_Condition;
	pthread_mutex_t m_ConditionMutex;
	pthread_mutex_t m_CommandMutex;
		
	std::list<IPixbufLoaderObserver*> m_observers;

	ImageCache m_ImageCache;
	
	typedef struct _Command
	{
		QuiverFile quiverFile;
		LoadParams params;
	} Command;


	std::list<Command> m_Commands;
	Command m_Command;
	
	bool m_bStopThread;
	
};

#endif
