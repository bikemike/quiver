#ifndef FILE_VIEWER_H
#define FILE_VIEWER_H

#include <gtk/gtk.h>

#include <boost/shared_ptr.hpp>

#include "ViewerEventSource.h"
#include "ImageList.h"
#include "IImageListView.h"

class Statusbar;
typedef boost::shared_ptr<Statusbar> StatusbarPtr;

class Viewer : public virtual ViewerEventSource
{
public:
	//constructor
	Viewer();
	~Viewer();
	
	//member functions
	GtkWidget *GetWidget();

	void SetImageList(IImageListViewPtr imgList);
	int GetCurrentOrientation();

	void StopVideo(bool reloadImage = true);

	void SlideShowStart();
	void SlideShowStop();
	void SlideShowPause();
	void SlideShowResume();
	void SlideShowTogglePause();
	bool IsSlideShowRunning() const;
	bool IsSlideShowPaused() const;

	GtkWidget *GetViewerOverlayBar() const;
	GtkWidget *GetTimelineRow() const;
	GtkWidget *GetCenterPlayButton() const;
	GtkWidget *GetImageView() const;
	bool IsVideoZoomAnchorCenter() const;

	void ToggleMute();
	bool IsMuted() const;
	void SetMuted(bool bMute);

	void RotateVideo(bool clockwise);
	int GetVideoUserRotation() const;

	// returns true if the view mode was reset, false if it did not need to be reset
	bool ResetViewMode();

	void GrabFocus();
	void Show();
	void Hide();
	
	void RegisterActions();
	void SetStatusbar(StatusbarPtr statusbarPtr);

	double GetMagnification() const;
	bool IsFilmstripOverlay() const;
	bool IsHideFilmstripFS() const;
	GtkWidget *GetFilmstripWidget() const;
	/* Overlay wrapping the image/video area; floating chrome (e.g. the
	 * undo-delete toast) can be parented on top of the current image. */
	GtkWidget *GetOverlay();
	void ShowFilmstripOverlay();
	void HideFilmstripOverlay();
	void CancelFilmstripHide();
	void SetFilmstripHiddenByFS(bool bHidden);
	bool IsFilmstripHiddenByFS() const;
	void UpdateHUDPosition();
	void ResetIdleCursor();
	void RefreshAutoHideTimer();
	/* Leaving fullscreen produces no motion event, so a pointer that was
	 * auto-hidden while idle in fullscreen would stay invisible. Restore
	 * it and bring the controls back exactly as a motion event would. */
	void OnExitFullscreen();

	class ViewerImpl;
	typedef boost::shared_ptr<ViewerImpl> ViewerImplPtr;

private:

	ViewerImplPtr m_ViewerImplPtr;
	
};

typedef boost::shared_ptr<Viewer> ViewerPtr;

#endif
