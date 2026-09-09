#ifndef FILE_IMAGELOADER_H
#define FILE_IMAGELOADER_H

#include <pthread.h>
#include <atomic>
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
		bool loaded_quick_preview;
		int max_width;
		int max_height;
		State state;
	} LoadParams;



	ImageLoader();
	~ImageLoader();

	void LoadImage(QuiverFile);	
	void LoadImageAtSize(QuiverFile, int width, int height);
	void LoadImage(QuiverFile,LoadParams load_params);	
	void ReloadImage(QuiverFile);

	void CacheImage(QuiverFile);
	void CacheImageAtSize(QuiverFile, int width, int height);
	
	void ReCacheImage(QuiverFile);

#if HAVE_GDK_PIXBUF
	GdkPixbuf* GetCachedPixbuf(QuiverFile f);
#endif
	GdkTexture* GetCachedTexture(QuiverFile f);
	
	// thread functions
	static void* run(void *data);
	int Run();
	
#if HAVE_GDK_PIXBUF
	void SignalSizePrepared(GdkPixbufLoader *loader,gint width, gint height);
#endif
	void AddPixbufLoaderObserver(IPixbufLoaderObserver * loader_observer);
	void RemovePixbufLoaderObserver(IPixbufLoaderObserver * loader_observer);

	bool IsWorking();
	void EnableQuickPreview(bool bQuickPreview){m_bQuickPreview = bQuickPreview;};
	void SetLoadOrientation(int iLoadOrientation){m_iLoadOrientation=iLoadOrientation;};
	
private:	
	void Load();
#if HAVE_GDK_PIXBUF
	bool LoadPixbuf(GdkPixbufLoader *loader, bool* bAborted = NULL);
#endif
	bool CommandsPending();
	static gboolean abort_video_load(gpointer data);
	
	bool LoadQuickPreview();

	pthread_t m_pthread_id;

	pthread_cond_t m_Condition;
	pthread_mutex_t m_CommandMutex;

	GMutex m_csObservers;
		
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
	std::atomic<bool> m_bWorking;
	int m_iLoadOrientation;
	bool m_bQuickPreview;
};

#endif
