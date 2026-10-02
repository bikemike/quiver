#ifndef FILE_BROWSER_H
#define FILE_BROWSER_H

#include <gtk/gtk.h>
#include <boost/shared_ptr.hpp>

#include "ImageList.h"
#include "QuiverFile.h"
#include "BrowserEventSource.h"

class Statusbar;
typedef boost::shared_ptr<Statusbar> StatusbarPtr;

class FolderTree;
typedef boost::shared_ptr<FolderTree> FolderTreePtr;

class Browser : public virtual BrowserEventSource
{
public:
	Browser();
	~Browser();
	
	GtkWidget* GetWidget();
	
	ImageListPtr GetImageList();
	
	void SetImageList(ImageListPtr list);
	
	void RegisterActions();
	void SetToolbar(GtkWidget* pToolbar);
	void SetStatusbar(StatusbarPtr statusbar);

	/* Rename what the browser has selected.  The single global rename action
	 * calls this while the browser is the visible pane. */
	void Rename();
	
	void GrabFocus();
	
	void Show();
	void Hide();

	std::list<unsigned int> GetSelection();

	std::string GetCurrentFolderChild();
	GdkModifierType GetLastActivateModifiers() const;

	FolderTreePtr GetFolderTree();

	/* Overlay wrapping the icon view; floating chrome (e.g. the undo-delete
	 * toast) can be parented on top of the image grid. */
	GtkWidget *GetIconViewOverlay();

	void ShowLoadingProgress(const std::string& text, double fraction);
	void HideLoadingProgress();

	/* Applies (or lifts) the "hide the sidebar in fullscreen" preference.  The
	 * window drives this itself, so it is called from the window-state handler
	 * rather than waiting for the browser's next UpdateUI(). */
	void UpdateFullscreenSidebar();

	class BrowserImpl;
private:
	boost::shared_ptr<BrowserImpl> m_BrowserImplPtr;
};

typedef boost::shared_ptr<Browser> BrowserPtr;

#endif
