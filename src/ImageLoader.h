#ifndef FILE_IMAGELOADER_H
#define FILE_IMAGELOADER_H

#include <pthread.h>

#include <string>
#include <list>
#include <iostream>
#include <fstream>
#include "Timer.h"
#include "ImageCache.h"

#include "PixbufLoaderObserver.h"


class ImageLoader
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
		std::string filename;
	} Command;

	ImageLoader();
	~ImageLoader();

	void AddPixbufLoaderObserver(PixbufLoaderObserver * loader_observer);
	void LoadImage(std::string image);	
	void CacheImage(std::string image);	
	
	// thread functions
	static void* run(void *data);
	int Run();

	// starts the thread
	void Start();
	

private:	
	void Load();
	bool LoadPixbuf(GdkPixbufLoader *loader);
	bool CommandsPending();

	pthread_t m_pthread_id;

	pthread_cond_t m_pthread_cond_id;
	pthread_mutex_t m_pthread_mutex_id;
	
	std::list<PixbufLoaderObserver*> m_observers;

	ImageCache m_ImageCache;
	
	pthread_mutex_t m_CommandMutex;
	std::list<Command> m_Commands;
	Command m_Command;
	
};

#endif
