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

	/* True when the full-resolution texture for uri is already decoded and in
	 * memory.  The viewer needs this to know whether putting a cached thumbnail
	 * on screen is a genuine preview or a step backwards from an image it
	 * already holds - which is a different question from whether one can be
	 * produced, and cannot be answered from the thumbnail cache. */
	bool InCache(std::string uri) { return m_ImageCache.InCache(uri); }
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
	void SetThumbnailCache(ImageCache* pCache) { m_pThumbnailCache = pCache; }

	/* The size to declare alongside a texture, given the stored width/height of
	 * the file and the orientation its pixels are actually sitting at.
	 *
	 * Exposed because it is the one rule that was quietly wrong: the swap used to
	 * be keyed on the turn applied rather than on where the pixels ended up, so
	 * an image whose file orientation is 5-8 - already transposed by
	 * EnsureExifOrientation, needing no further turn - was announced at the
	 * untransposed stored size.  The view then believed it had been handed the
	 * opposite aspect ratio to the texture it was holding.
	 *
	 * `pixels_at` is the orientation the pixels rest at, not the one requested:
	 * on a cache hit the texture has been turned *to* the request, so there the
	 * two coincide, but on a fresh decode the request may need no turn at all
	 * while the pixels still sit transposed at the file's own orientation. */
	static void DeclaredSize(int stored_width, int stored_height, int pixels_at,
	                         int *width, int *height);

	/* Stop the worker thread and wait for it to exit. Idempotent; ensures
	 * thread is joined before any caller-owned bridged caches are destroyed. */
	void StopThread();
	
private:	
	void Load();
#if HAVE_GDK_PIXBUF
	bool LoadPixbuf(GdkPixbufLoader *loader, bool* bAborted = NULL);
#endif
	void NotifyObservers(GdkTexture *texture, gint width, gint height, bool bResetViewMode);
	bool CommandsPending();

	/* True when a newer user-visible load is already queued, which means the
	 * view has moved on and anything this command is about to hand it is stale.
	 *
	 * Unlike CommandsPending() this only looks; it does not prune the queue or
	 * promote a pending CACHE to CACHE_LOAD, so it is safe to call at the
	 * moment a texture is about to be delivered. */
	bool NewerLoadPending();
	static gboolean abort_video_load(gpointer data);
	
	bool LoadQuickPreview();

	pthread_t m_pthread_id;

	pthread_cond_t m_Condition;
	pthread_mutex_t m_CommandMutex;

	GMutex m_csObservers;
		
	std::list<IPixbufLoaderObserver*> m_observers;

	ImageCache m_ImageCache;
	ImageCache* m_pThumbnailCache;
	
	typedef struct _Command
	{
		QuiverFile quiverFile;
		LoadParams params;
	} Command;


	std::list<Command> m_Commands;
	Command m_Command;
	
	bool m_bStopThread;
	std::atomic<bool> m_bWorking;
	std::atomic<bool> m_bThreadJoined;
	/* The orientation the user has currently asked for.  Written on the GUI
	 * thread by SetLoadOrientation() and read on the loader thread by
	 * GetNextCommand() and SignalSizePrepared(), so it is atomic.  A load in
	 * progress must not consult it: it snapshots m_Command.params.orientation
	 * into a local instead, because a rotation made while a decode is running
	 * would otherwise be applied halfway through and the texture would be
	 * stamped with an orientation it was never turned to. */
	std::atomic<int> m_iLoadOrientation;
	bool m_bQuickPreview;
};

#endif
