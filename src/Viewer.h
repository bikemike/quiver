#ifndef FILE_VIEWER_H
#define FILE_VIEWER_H

#include <gtk/gtk.h>

#include <boost/shared_ptr.hpp>

#include <libquiver/quiver-image-view.h>

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

	/* Rename the item the viewer is showing.  The single global rename action
	 * calls this while the viewer is the visible pane. */
	void Rename();

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
	GtkWidget *GetPlayProgress() const;
	GtkWidget *GetPlayProgressPopover() const;
	GtkWidget *GetCenterPlayButton() const;
	GtkWidget *GetViewModeMenuPopover() const;
	GtkWidget *GetImageView() const;
	bool IsVideoZoomAnchorCenter() const;
	/* Whether handing a new image to @pImageView should reset it, given what
	 * the caller would otherwise do (@bResetViewMode).  Every delivery route
	 * asks: the image loader's, and the one for a thumbnail that is already
	 * in the cache, which skips the loader entirely.  A view left in "keep
	 * zoom and pan" is never reset, so its magnification and centre survive
	 * the image changing. */
	static gboolean ShouldResetViewForNewImage(GtkWidget *pImageView, bool bResetViewMode);

	void ToggleMute();
	bool IsMuted() const;
	void SetMuted(bool bMute);

	void RotateVideo(bool clockwise);
	int GetVideoUserRotation() const;

	void SetVideoZoom(double zoom);
	double GetVideoZoom() const;
	bool IsVideoPanSlowdownActive() const;
	double GetVideoPanVelocityX() const;
	double GetVideoPanVelocityY() const;
	double GetVideoPanX() const;
	double GetVideoPanY() const;
	/* How far the visible part of the frame can be moved along each axis, in the
	 * same pixels GetVideoPanX/Y report, and 0 when there is nothing to pan: the
	 * frame has no size yet, or the picture fits the viewport.  CanVideoPan() says
	 * the user may drag the picture, which a zoom alone is enough to answer; this
	 * says whether dragging it would move anything. */
	double GetVideoPanRangeX() const;
	double GetVideoPanRangeY() const;
	/* Whether the quick preview's scroll position is, right now, where this video
	 * is framed from.  It is only while that preview is the picture on screen:
	 * once the video page has taken the screen, while the image view is being
	 * handed back to the stills, or after the first frame, whatever the scroll
	 * position says belongs to the image view and not to this video.  Exposed so
	 * that a live framing can be told from one that was left over. */
	bool IsVideoPreviewPanSource() const;
	/* The video's own view state: the mode the viewer is in, and the *centre* of
	 * the visible part of the frame, as a fraction (0..1) of the frame on each
	 * axis.  A fraction of the centre, rather than of the offset, is what means
	 * the same thing for a frame of any size or aspect ratio: the same centre
	 * frames the same relative part of any frame, which is what switching from a
	 * 16:9 to a 4:3 file has to preserve.  It is also what the nav control's
	 * zoom box shows, as the box's centre. */
	QuiverImageViewMode GetVideoViewMode() const;
	double GetVideoPanFractionX() const;
	double GetVideoPanFractionY() const;
	/* Place the visible part of the frame at the given centre, as a fraction of
	 * the frame on each axis - the same units GetVideoPanFractionX/Y report.  A
	 * centre near an edge is clamped to where the visible part can still fit. */
	void SetVideoPanFraction(double x, double y);
	bool CanVideoPan() const;
	void StartVideoPanSlowdown();
	void StopVideoPanSlowdown();
	void RecordVideoPanSample(double dx, double dy, double dt);
	bool IsVideoPlaying() const;
	bool IsPointOverControlsOrFilmstrip(double x, double y) const;

	// returns true if the view mode was reset, false if it did not need to be reset
	bool ResetViewMode();

	void GrabFocus();
	void Show();
	void Hide();
	
	void RegisterActions();
	void UnregisterActions();
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
	void UpdateUI();

	GtkWidget *GetNavControlPill() const;
	GtkWidget *GetNavigationControl() const;
	void UpdateNavigationControl();

	class ViewerImpl;
	typedef boost::shared_ptr<ViewerImpl> ViewerImplPtr;

private:

	ViewerImplPtr m_ViewerImplPtr;
	
};

typedef boost::shared_ptr<Viewer> ViewerPtr;

#endif
