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

	typedef struct _Command
	{
		State state;
		QuiverFile quiverFile;
	} Command;

	ImageLoader();
	~ImageLoader();

	void AddPixbufLoaderObserver(IPixbufLoaderObserver * loader_observer);
	void LoadImage(QuiverFile);	
	void CacheImage(QuiverFile);
	void ReloadImage(QuiverFile);
	
	// thread functions
	static void* run(void *data);
	int Run();

	// starts the thread
	void Start();
	
	void SignalSizePrepared(GdkPixbufLoader *loader,gint width, gint height);

private:	
	void Load();
	bool LoadPixbuf(GdkPixbufLoader *loader);
	bool CommandsPending();

	pthread_t m_pthread_id;

	pthread_cond_t m_Condition;
	pthread_mutex_t m_ConditionMutex;
	pthread_mutex_t m_CommandMutex;
		
	std::list<IPixbufLoaderObserver*> m_observers;

	ImageCache m_ImageCache;
	

	std::list<Command> m_Commands;
	Command m_Command;
	
};

#endif
