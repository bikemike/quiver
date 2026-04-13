#include <config.h>
#include <gst/gst.h>
#include "Quiver.h"
#include <glib.h>
#include <glib/gstdio.h>
#include "QuiverStockIcons.h"
#include "QuiverUtils.h"
#include "QuiverPrefs.h"
#include "PreferencesDlg.h"
#include "AdjustDateDlg.h"
#include "AdjustDateTask.h"
#include "OrganizeDlg.h"
#include "OrganizeTask.h"
#include "RenameDlg.h"
#include "RenameTask.h"
#include "Bookmarks.h"
#include "BookmarksDlg.h"
#include "BookmarkAddEditDlg.h"
#include "DonateDlg.h"
#include "TaskManager.h"
#include "TaskManagerDlg.h"
#include "ThreadUtil.h"
#include "ExternalTools.h"
#include "ExternalToolsDlg.h"
#include "ImageSaveManager.h"
#include <boost/algorithm/string.hpp>
#include "quiver-i18n.h"

using namespace std;

class QuiverImpl {
public:
	QuiverImpl(Quiver *parent) : m_pQuiver(parent), m_bInitialized(false) {}
	Quiver *m_pQuiver;
	GtkWidget *m_pQuiverWindow;
	bool m_bInitialized;
    BrowserPtr m_BrowserPtr;
    ViewerPtr m_ViewerPtr;
    StatusbarPtr m_StatusbarPtr;
    ImageListPtr m_ImageListPtr;
    ExifView m_ExifView;
    GtkApplication *m_pApp;
};

static void quiver_activate (GtkApplication *app, gpointer user_data) {
    Quiver *quiver = (Quiver*)user_data;
    quiver->Init(app);
}

int main (int argc, char **argv) {
	bindtextdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

    GtkApplication *app = gtk_application_new ("org.yi.mike.quiver", G_APPLICATION_FLAGS_NONE);
    Quiver *pQuiver = new Quiver(list<string>(), false);
    g_signal_connect (app, "activate", G_CALLBACK (quiver_activate), pQuiver);
    int status = g_application_run (G_APPLICATION (app), argc, argv);
    g_object_unref (app);
    return status;
}

Quiver::Quiver(std::list<std::string> &images, bool bRecursive) : m_QuiverImplPtr(new QuiverImpl(this)) {}
Quiver::~Quiver() {}
void Quiver::Init(GtkApplication *app) {
    m_QuiverImplPtr->m_pApp = app;
    m_QuiverImplPtr->m_pQuiverWindow = gtk_application_window_new(app);
    gtk_window_present(GTK_WINDOW(m_QuiverImplPtr->m_pQuiverWindow));
}
bool Quiver::LoadSettings() { return true; }
void Quiver::SaveSettings() {}
void Quiver::SetImageList(list<string> &files, bool bRecursive) {}
void Quiver::ShowViewer() {}
void Quiver::ShowBrowser() {}
void Quiver::OnAbout() {}
void Quiver::OnFullScreen() {}
void Quiver::OnShowProperties(bool b) {}
void Quiver::OnQuit() {}
void Quiver::OnOpenFile() {}
void Quiver::OnOpenFolder() {}
void Quiver::OnSlideShow(bool b) {}
void Quiver::OnShowToolbar(bool b) {}
void Quiver::OnShowStatusbar(bool b) {}
void Quiver::OnShowMenubar(bool b) {}
void Quiver::ImageChanged() {}
void Quiver::SetWindowTitle(string s) {}
gboolean Quiver::window_close_request(GtkWindow *w, gpointer d) { return TRUE; }
gboolean Quiver::WindowCloseRequest(GtkWindow *w) { return TRUE; }
gboolean Quiver::IdleQuiverInit(gpointer d) { return FALSE; }
gboolean Quiver::idle_quiver_init(gpointer d) { return FALSE; }
void Quiver::Close() {}
GtkApplication* Quiver::GetApplication() { return m_QuiverImplPtr->m_pApp; }
