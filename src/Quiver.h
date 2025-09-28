#ifndef __QUIVER_H__
#define __QUIVER_H__

#include <list>
#include <string>
#include <boost/shared_ptr.hpp>
#include <gtk/gtk.h>
#include "IBrowserEventHandler.h"
#include "IViewerEventHandler.h"

class QuiverImpl;

class Quiver
{
public:
    Quiver(GtkApplication* app);
    ~Quiver();

    void Init(const std::list<std::string>& files, bool bStartSlideShow);

    static gboolean EventDelete(GtkWidget *widget, gpointer data);

    boost::shared_ptr<QuiverImpl> m_QuiverImplPtr;
    GtkApplication* m_pApp;
};

class Browser;
class Viewer;
class ExifView;
class Statusbar;

class QuiverImpl : public IBrowserEventHandler, public IViewerEventHandler
{
public:
    QuiverImpl(Quiver* pQuiver, std::list<std::string>& files, bool bStartSlideShow);
    ~QuiverImpl();

    void Init();
    void CreateUI(GtkApplication* app);
    void Close();

    void ShowBrowser();
    void ShowViewer();

    // IBrowserEventHandler
	virtual void HandleItemActivated(BrowserEventPtr event);
	virtual void HandleSelectionChanged(BrowserEventPtr event);
	virtual void HandleCursorChanged(BrowserEventPtr event);

	// IViewerEventHandler
	virtual void HandleItemClicked(ViewerEventPtr event);
	virtual void HandleItemActivated(ViewerEventPtr event);
	virtual void HandleCursorChanged(ViewerEventPtr event);
	virtual void HandleSlideShowStarted(ViewerEventPtr event);
	virtual void HandleSlideShowStopped(ViewerEventPtr event);

    void OnQuit();
    void OnFullScreen();
    void OnShowToolbar(bool bShow);
    void OnShowStatusbar(bool bShow);
    void OnShowMenubar(bool bShow);
    void OnOpenFile();
    void OnOpenFolder();
    void OnOpenLocation();
    void OnClose();
    void OnPrint();
    void OnPreferences();
    void OnAbout();
    void OnDonate();
    void OnAdjustDate();
    void OnRename();
    void OnOrganize();
    void OnDelete();
    void OnFileProperties();
    void OnHistoryUp();
    void OnHistoryBack();
    void OnHistoryForward();

    void LoadSettings();
    void SaveSettings();

    GtkWindow* m_pWindow;
    GtkWidget* m_pMainVBox;
    GtkWidget* m_pMainHPaned;
    GtkWidget* m_pMainStack;
    GtkWidget* m_pMenubar;
    GtkWidget* m_pToolbar;
    Statusbar* m_pStatusbar;
    Browser* m_BrowserPtr;
    Viewer* m_ViewerPtr;
    ExifView* m_pExifView;
    int m_iUIMode;

    Quiver* m_pQuiver;
    std::list<std::string> m_files;
    bool m_bStartSlideShow;
};

#endif // __QUIVER_H__
