#ifndef QUIVER_IMPL_H
#define QUIVER_IMPL_H

#include <gtk/gtk.h>
#include <string>
#include <list>

// Forward declarations
class Quiver;
class Browser;
class Viewer;
class ExifView;

class QuiverImpl
{
public:
    QuiverImpl(Quiver* pQuiver, std::list<std::string>& files, bool bStartSlideShow);
    ~QuiverImpl();

    void Init();
    void CreateUI(GtkApplication* app);
    void Close();

    void OnQuit();
    void OnFullScreen();
    void OnShowToolbar(bool bShow);
    void OnShowStatusbar(bool bShow);
    void OnShowMenubar(bool bShow);
    void OnOpenFile();
    void OnOpenFolder();
    void OnHistoryUp();
    void OnHistoryBack();
    void OnHistoryForward();
    void OnAbout();
    void OnDonate();
    void OnOpenLocation();
    void OnClose();
    void OnPrint();
    void OnPreferences();
    void OnAdjustDate();
    void OnRename();
    void OnOrganize();
    void OnDelete();
    void OnFileProperties();

    void ShowBrowser();
    void ShowViewer();

    void LoadSettings();
    void SaveSettings();

    GtkWindow* m_pWindow;
    GtkWidget* m_pMainVBox;
    GtkWidget* m_pMenubar;
    GtkWidget* m_pToolbar;
    GtkWidget* m_pStatusbar;
    GtkWidget* m_pCurrentView;
    Browser* m_BrowserPtr;
    Viewer* m_ViewerPtr;
    ExifView* m_pExifView;
    int m_iUIMode;
    Quiver* m_pQuiver;
    std::list<std::string> m_files;
    bool m_bStartSlideShow;
};

#endif // QUIVER_IMPL_H
