#include "config.h"

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include <iostream>
#include <algorithm>
#include <vector>

#include "Quiver.h"
#include "Statusbar.h"
#include "Browser.h"
#include "Viewer.h"
#include "ExifView.h"
#include "Preferences.h"
#include "QuiverUtils.h"
#include "QuiverDefs.h"
#include "AdjustDateDlg.h"
#include "RenameDlg.h"
#include "OrganizeDlg.h"
#include "DonateDlg.h"
#include "QuiverFileOps.h"
#include "BrowserHistory.h"

#ifdef HAVE_GSTREAMER
#include <gst/gst.h>
#endif

#include "libquiver/quiver-icon-view.h"
#include "libquiver/quiver-image-view.h"
#include "libquiver/quiver-navigation-control.h"

gchar g_szConfigFilePath[1024];

// No class definition here

static void app_activate(GtkApplication* app, gpointer user_data);
static void app_open(GApplication* app, GFile** files, gint n_files, const gchar* hint, gpointer user_data);

// Action handlers
static void on_about(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    ((Quiver*)user_data)->m_QuiverImplPtr->OnAbout();
}
static void on_donate(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    ((Quiver*)user_data)->m_QuiverImplPtr->OnDonate();
}
static void on_quit(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    ((Quiver*)user_data)->m_QuiverImplPtr->Close();
}

static void on_open_file_action(GSimpleAction* action, GVariant* param, gpointer user_data) {
    ((QuiverImpl*)user_data)->OnOpenFile();
}
static void on_open_folder_action(GSimpleAction* action, GVariant* param, gpointer user_data) {
    ((QuiverImpl*)user_data)->OnOpenFolder();
}
static void on_view_browser_action(GSimpleAction* action, GVariant* param, gpointer user_data) {
    ((QuiverImpl*)user_data)->ShowBrowser();
}
static void on_view_viewer_action(GSimpleAction* action, GVariant* param, gpointer user_data) {
    ((QuiverImpl*)user_data)->ShowViewer();
}
static void on_fullscreen_action(GSimpleAction* action, GVariant* param, gpointer user_data) {
    ((QuiverImpl*)user_data)->OnFullScreen();
}
static void on_history_back_action(GSimpleAction* action, GVariant* param, gpointer user_data) {
    ((QuiverImpl*)user_data)->OnHistoryBack();
}
static void on_history_forward_action(GSimpleAction* action, GVariant* param, gpointer user_data) {
    ((QuiverImpl*)user_data)->OnHistoryForward();
}
static void on_history_up_action(GSimpleAction* action, GVariant* param, gpointer user_data) {
    ((QuiverImpl*)user_data)->OnHistoryUp();
}

// Stateful actions
static void on_show_menubar_change_state(GSimpleAction* action, GVariant* state, gpointer user_data) {
    QuiverImpl* self = (QuiverImpl*)user_data;
    gboolean bShow = g_variant_get_boolean(state);
    self->OnShowMenubar(bShow);
    g_simple_action_set_state(action, g_variant_new_boolean(bShow));
}
static void on_show_toolbar_change_state(GSimpleAction* action, GVariant* state, gpointer user_data) {
    QuiverImpl* self = (QuiverImpl*)user_data;
    gboolean bShow = g_variant_get_boolean(state);
    self->OnShowToolbar(bShow);
    g_simple_action_set_state(action, g_variant_new_boolean(bShow));
}
static void on_show_statusbar_change_state(GSimpleAction* action, GVariant* state, gpointer user_data) {
    QuiverImpl* self = (QuiverImpl*)user_data;
    gboolean bShow = g_variant_get_boolean(state);
    self->OnShowStatusbar(bShow);
    g_simple_action_set_state(action, g_variant_new_boolean(bShow));
}


// Main
int main (int argc, char *argv[])
{
    const gchar* config_dir = g_get_user_config_dir();
    g_snprintf(g_szConfigFilePath, sizeof(g_szConfigFilePath), "%s/quiver/quiver.conf", config_dir);

    GOptionContext* context = g_option_context_new("- image browser and viewer");

    // Quiver options
    gboolean bSlideShow = false;
    GOptionEntry entries[] =
    {
        { "slideshow", 's', 0, G_OPTION_ARG_NONE, &bSlideShow, "Start in slideshow mode", NULL },
        { NULL }
    };
    g_option_context_add_main_entries(context, entries, GETTEXT_PACKAGE);

    // GStreamer options
#ifdef HAVE_GSTREAMER
    g_option_context_add_group(context, gst_init_get_option_group());
#endif

    GError* error = NULL;
    if (!g_option_context_parse(context, &argc, &argv, &error))
    {
        g_print("option parsing failed: %s\n", error->message);
        return 1;
    }

#if !GLIB_CHECK_VERSION(2,35,0)
    g_type_init();
#endif
    quiver_icon_view_get_type();
    quiver_image_view_get_type();
    quiver_navigation_control_get_type();

    /*
     * Make sure the custom widget types are registered with the GObject
     * type system before we load any UI files that might use them.
     *
     * We store the result in a volatile variable to prevent the compiler
     * from optimizing away these calls, which have the important
     * side-effect of registering the types.
     */
    volatile GType type;
    type = quiver_icon_view_get_type();
    type = quiver_image_view_get_type();
    type = quiver_navigation_control_get_type();

    GtkApplication* app = gtk_application_new("org.quiver.quiver", G_APPLICATION_HANDLES_OPEN);

    Quiver quiver(app);

    const GActionEntry app_actions[] = {
        { "about", on_about },
        { "donate", on_donate },
        { "quit", on_quit }
    };
    g_action_map_add_action_entries(G_ACTION_MAP(app), app_actions, G_N_ELEMENTS(app_actions), &quiver);

    g_signal_connect(app, "activate", G_CALLBACK(app_activate), &quiver);
    g_signal_connect(app, "open", G_CALLBACK(app_open), &quiver);

#ifdef HAVE_GSTREAMER
    gst_init (&argc, &argv);
#endif

    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    return status;
}

static void app_activate(GtkApplication* app, gpointer user_data)
{
    Quiver* quiver = (Quiver*)user_data;
    std::list<std::string> files;
    quiver->Init(files, false);
    quiver->m_QuiverImplPtr->Init();
    quiver->m_QuiverImplPtr->CreateUI(app);
}

static void app_open(GApplication* app, GFile** files, gint n_files, const gchar* hint, gpointer user_data)
{
    Quiver* quiver = (Quiver*)user_data;
    std::list<std::string> file_list;
    for (int i = 0; i < n_files; i++) {
        char* path = g_file_get_path(files[i]);
        file_list.push_back(path);
        g_free(path);
    }
    quiver->Init(file_list, false);
    quiver->m_QuiverImplPtr->Init();
    quiver->m_QuiverImplPtr->CreateUI(GTK_APPLICATION(app));
}


QuiverImpl::QuiverImpl(Quiver* pQuiver, std::list<std::string>& files, bool bStartSlideShow) :
    m_pWindow(NULL),
    m_pMainVBox(NULL),
    m_pMainHPaned(NULL),
    m_pMainStack(NULL),
    m_pMenubar(NULL),
    m_pToolbar(NULL),
    m_pStatusbar(NULL),
    m_BrowserPtr(NULL),
    m_ViewerPtr(NULL),
    m_pExifView(NULL),
    m_iUIMode(QUIVER_UI_MODE_NONE),
    m_pQuiver(pQuiver),
    m_files(files),
    m_bStartSlideShow(bStartSlideShow)
{
}

QuiverImpl::~QuiverImpl()
{
    SaveSettings();

    delete m_pStatusbar;
    delete m_BrowserPtr;
    delete m_ViewerPtr;
    delete m_pExifView;
}

void QuiverImpl::Init()
{
    // Get the UI mode from preferences
    Preferences* prefs = Preferences::GetInstance().get();
    m_iUIMode = prefs->GetInteger(QUIVER_PREFS_APP, "ui_mode", QUIVER_UI_MODE_BROWSER);

    // If we have files, then force viewer mode
    if (!m_files.empty() || m_bStartSlideShow)
        if (QUIVER_UI_MODE_VIEWER == m_iUIMode)
            m_iUIMode = QUIVER_UI_MODE_BROWSER;
}

void QuiverImpl::Close()
{
    g_application_quit(G_APPLICATION(gtk_window_get_application(m_pWindow)));
}

void QuiverImpl::CreateUI(GtkApplication* app)
{
    // Create the widgets
    m_pWindow = GTK_WINDOW(gtk_application_window_new(app));
    g_signal_connect (G_OBJECT (m_pWindow), "close-request",
              G_CALLBACK (Quiver::EventDelete), m_pQuiver);

    m_pMainVBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(m_pWindow, m_pMainVBox);

    GtkBuilder* builder = gtk_builder_new();
    gchar* ui_file = g_build_filename(QUIVER_DATADIR, "quiver.ui", NULL);
    if (g_file_test(ui_file, G_FILE_TEST_EXISTS)) {
        gtk_builder_add_from_file(builder, ui_file, NULL);
    }
    g_free(ui_file);

    m_pMenubar = GTK_WIDGET(gtk_builder_get_object(builder, "menubar"));
    gtk_application_set_menubar(app, G_MENU_MODEL(m_pMenubar));


    m_pToolbar = GTK_WIDGET(gtk_builder_get_object(builder, "toolbar"));
    gtk_box_append(GTK_BOX(m_pMainVBox), m_pToolbar);

    // Create the main layout
    m_pMainHPaned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_vexpand(m_pMainHPaned, TRUE);
    gtk_box_append(GTK_BOX(m_pMainVBox), m_pMainHPaned);

    m_pMainStack = gtk_stack_new();
    gtk_paned_set_start_child(GTK_PANED(m_pMainHPaned), m_pMainStack);

    m_pExifView = new ExifView();
    gtk_paned_set_end_child(GTK_PANED(m_pMainHPaned), m_pExifView->GetWidget());
    gtk_paned_set_resize_end_child(GTK_PANED(m_pMainHPaned), FALSE);
    gtk_paned_set_shrink_end_child(GTK_PANED(m_pMainHPaned), FALSE);


    // Create the status bar
    m_pStatusbar = new Statusbar();
    gtk_box_append(GTK_BOX(m_pMainVBox), m_pStatusbar->GetWidget());

    const GActionEntry win_actions[] = {
        { "open-file", on_open_file_action },
        { "open-folder", on_open_folder_action },
        { "view-browser", on_view_browser_action },
        { "view-viewer", on_view_viewer_action },
        { "fullscreen", on_fullscreen_action },
        { "back", on_history_back_action },
        { "forward", on_history_forward_action },
        { "up", on_history_up_action }
    };
    g_action_map_add_action_entries(G_ACTION_MAP(m_pWindow), win_actions, G_N_ELEMENTS(win_actions), this);

    // show-menubar
    GAction* show_menubar_action = G_ACTION(g_simple_action_new_stateful(
        "show-menubar", NULL,
        g_variant_new_boolean(gtk_widget_get_visible(GTK_WIDGET(m_pMenubar)))
    ));
    g_signal_connect(show_menubar_action, "change-state", G_CALLBACK(on_show_menubar_change_state), this);
    g_action_map_add_action(G_ACTION_MAP(m_pWindow), show_menubar_action);
    g_object_unref(show_menubar_action);

    // show-toolbar
    GAction* show_toolbar_action = G_ACTION(g_simple_action_new_stateful(
        "show-toolbar", NULL,
        g_variant_new_boolean(gtk_widget_get_visible(GTK_WIDGET(m_pToolbar)))
    ));
    g_signal_connect(show_toolbar_action, "change-state", G_CALLBACK(on_show_toolbar_change_state), this);
    g_action_map_add_action(G_ACTION_MAP(m_pWindow), show_toolbar_action);
    g_object_unref(show_toolbar_action);

    // show-statusbar
    GAction* show_statusbar_action = G_ACTION(g_simple_action_new_stateful(
        "show-statusbar", NULL,
        g_variant_new_boolean(gtk_widget_get_visible(GTK_WIDGET(m_pStatusbar->GetWidget())))
    ));
    g_signal_connect(show_statusbar_action, "change-state", G_CALLBACK(on_show_statusbar_change_state), this);
    g_action_map_add_action(G_ACTION_MAP(m_pWindow), show_statusbar_action);
    g_object_unref(show_statusbar_action);

    m_BrowserPtr = new Browser();
    m_ViewerPtr = new Viewer();

    // Add browser and viewer to the stack
    gtk_stack_add_named(GTK_STACK(m_pMainStack), m_BrowserPtr->GetWidget(), "browser");
    gtk_stack_add_named(GTK_STACK(m_pMainStack), m_ViewerPtr->GetWidget(), "viewer");

    // Add event handlers
    m_BrowserPtr->AddEventHandler(m_pQuiver->m_QuiverImplPtr);
    m_ViewerPtr->AddEventHandler(m_pQuiver->m_QuiverImplPtr);

    // Show the correct view
    if (m_iUIMode == QUIVER_UI_MODE_BROWSER)
        ShowBrowser();
    else
        ShowViewer();

    // Show all the widgets
    gtk_window_present(GTK_WINDOW(m_pWindow));

    LoadSettings();
}

void QuiverImpl::ShowBrowser()
{
    m_iUIMode = QUIVER_UI_MODE_BROWSER;
    gtk_stack_set_visible_child_name(GTK_STACK(m_pMainStack), "browser");
    m_BrowserPtr->Show();
    m_ViewerPtr->Hide();
}

void QuiverImpl::ShowViewer()
{
    m_iUIMode = QUIVER_UI_MODE_VIEWER;
    gtk_stack_set_visible_child_name(GTK_STACK(m_pMainStack), "viewer");
    m_ViewerPtr->Show();
    m_BrowserPtr->Hide();
}

Quiver::Quiver(GtkApplication* app) :
    m_QuiverImplPtr(NULL)
{
    m_pApp = app;
}

Quiver::~Quiver()
{
    // The m_QuiverImplPtr is a boost::shared_ptr, so it will be deleted automatically.
}

void Quiver::Init(const std::list<std::string>& files, bool bStartSlideShow)
{
    if (!m_QuiverImplPtr)
        m_QuiverImplPtr.reset(new QuiverImpl(this, const_cast<std::list<std::string>&>(files), bStartSlideShow));
}

void QuiverImpl::OnQuit()
{
    Close();
}

void QuiverImpl::OnFullScreen()
{
    Preferences* prefs = Preferences::GetInstance().get();
    bool bFS = prefs->GetBoolean(QUIVER_PREFS_APP, "fullscreen", false);

    if (bFS)
    {
        gtk_window_unfullscreen(m_pWindow);
        prefs->SetBoolean(QUIVER_PREFS_APP, "fullscreen", false);
    }
    else
    {
        gtk_window_fullscreen(m_pWindow);
        prefs->SetBoolean(QUIVER_PREFS_APP, "fullscreen", true);
    }
}

void QuiverImpl::OnShowToolbar(bool bShow)
{
    Preferences* prefs = Preferences::GetInstance().get();
    prefs->SetBoolean(QUIVER_PREFS_APP, "show_toolbar", bShow);

    gtk_widget_set_visible(GTK_WIDGET(m_pToolbar), bShow);
}

void QuiverImpl::OnShowStatusbar(bool bShow)
{
    Preferences* prefs = Preferences::GetInstance().get();
    prefs->SetBoolean(QUIVER_PREFS_APP, "show_statusbar", bShow);

    gtk_widget_set_visible(GTK_WIDGET(m_pStatusbar->GetWidget()), bShow);
}

void QuiverImpl::OnShowMenubar(bool bShow)
{
    Preferences* prefs = Preferences::GetInstance().get();
    prefs->SetBoolean(QUIVER_PREFS_APP, "show_menubar", bShow);

    gtk_widget_set_visible(GTK_WIDGET(m_pMenubar), bShow);
}

static void open_file_dialog_callback(GObject* source_object, GAsyncResult* res, gpointer user_data)
{
    GtkFileDialog* dialog = GTK_FILE_DIALOG(source_object);
    GFile* file = gtk_file_dialog_open_finish(dialog, res, NULL);
    if (file) {
        QuiverImpl* self = (QuiverImpl*)user_data;
        char* uri = g_file_get_uri(file);
        self->m_files.clear();
        self->m_files.push_back(uri);
        g_free(uri);
        g_object_unref(file);
        self->ShowBrowser();
        ImageListPtr imageList(new ImageList());
        imageList->SetImageList(&self->m_files);
        self->m_BrowserPtr->SetImageList(imageList);
    }
}

void QuiverImpl::OnOpenFile()
{
    GtkFileDialog* dialog = gtk_file_dialog_new();
    gtk_file_dialog_open(dialog, m_pWindow, NULL, open_file_dialog_callback, this);
    g_object_unref(dialog);
}

static void select_folder_dialog_callback(GObject* source_object, GAsyncResult* res, gpointer user_data)
{
    GtkFileDialog* dialog = GTK_FILE_DIALOG(source_object);
    GFile* file = gtk_file_dialog_select_folder_finish(dialog, res, NULL);
    if (file) {
        QuiverImpl* self = (QuiverImpl*)user_data;
        char* uri = g_file_get_uri(file);
        std::list<std::string> files;
        files.push_back(uri);
        ImageListPtr imageList(new ImageList());
        imageList->SetImageList(&files);
        self->m_BrowserPtr->SetImageList(imageList);
        g_free(uri);
        g_object_unref(file);
        self->ShowBrowser();
    }
}

void QuiverImpl::OnOpenFolder()
{
    GtkFileDialog* dialog = gtk_file_dialog_new();
    gtk_file_dialog_select_folder(dialog, m_pWindow, NULL, select_folder_dialog_callback, this);
    g_object_unref(dialog);
}

void QuiverImpl::OnHistoryUp()
{
    if (m_iUIMode == QUIVER_UI_MODE_BROWSER)
    {
        // m_BrowserPtr->GetBrowserHistory().Up();
    }
}

void QuiverImpl::OnHistoryBack()
{
    if (m_iUIMode == QUIVER_UI_MODE_BROWSER)
    {
        m_BrowserPtr->GetBrowserHistory().GoBack();
    }
}

void QuiverImpl::OnHistoryForward()
{
    if (m_iUIMode == QUIVER_UI_MODE_BROWSER)
    {
        m_BrowserPtr->GetBrowserHistory().GoForward();
    }
}

void QuiverImpl::LoadSettings()
{
    Preferences* prefs = Preferences::GetInstance().get();

    // Restore window size and position
    bool bMaximised = prefs->GetBoolean(QUIVER_PREFS_APP, "maximised", false);
    if (bMaximised)
    {
        gtk_window_maximize(m_pWindow);
    }
    else
    {
        int w = prefs->GetInteger(QUIVER_PREFS_APP, "w", -1);
        int h = prefs->GetInteger(QUIVER_PREFS_APP, "h", -1);
        if (w > 0 && h > 0)
        {
            gtk_window_set_default_size(m_pWindow,w,h);
        }
    }
}

void QuiverImpl::SaveSettings()
{
    Preferences* prefs = Preferences::GetInstance().get();

    if (gtk_window_is_fullscreen(m_pWindow))
    {
        // Don't save settings if fullscreen
    }
    else if (gtk_window_is_maximized(m_pWindow))
    {
        prefs->SetBoolean(QUIVER_PREFS_APP, "maximised", true);
    }
    else
    {
        prefs->SetBoolean(QUIVER_PREFS_APP, "maximised", false);
        int w,h;
        gtk_window_get_default_size(m_pWindow,&w,&h);
        prefs->SetInteger(QUIVER_PREFS_APP,"w",w);
        prefs->SetInteger(QUIVER_PREFS_APP,"h",h);
    }

    prefs->SetInteger(QUIVER_PREFS_APP, "ui_mode", m_iUIMode);
}

gboolean Quiver::EventDelete(GtkWidget *widget, gpointer data)
{
    Quiver* pQuiver = (Quiver*)data;
    pQuiver->m_QuiverImplPtr->Close();
    return TRUE;
}


void QuiverImpl::OnAbout()
{
    const gchar *authors[] = { "Mike Morrison <mike@yi.org>", NULL };
    gtk_show_about_dialog(m_pWindow,
                          "program-name", "Quiver",
                          "version", VERSION,
                          "copyright", "Copyright © 2008 Mike Morrison",
                          "website", "http://www.kainjow.com/quiver",
                          "comments", "An image viewer for the GNOME desktop.",
                          "authors", authors,
                          "logo-icon-name", "quiver-icon-app",
                          NULL);
}

void QuiverImpl::OnDonate()
{
    DonateDlg donate(m_pWindow);
    donate.Run();
}

void QuiverImpl::OnOpenLocation()
{
}
void QuiverImpl::OnClose()
{
}
void QuiverImpl::OnPrint()
{
}
void QuiverImpl::OnPreferences()
{
}
void QuiverImpl::OnAdjustDate()
{
    AdjustDateDlg dlg(m_pWindow);
    dlg.Run();
}
void QuiverImpl::OnRename()
{
    //RenameDlg dlg(m_pWindow);
    //dlg.Run();
}
void QuiverImpl::OnOrganize()
{
    //OrganizeDlg dlg(m_pWindow);
    //dlg.Run();
}
void QuiverImpl::OnDelete()
{
}
void QuiverImpl::OnFileProperties()
{
}

//=============================================================================
// IBrowserEventHandler implementation
//=============================================================================
void QuiverImpl::HandleItemActivated(BrowserEventPtr event)
{
    ShowViewer();
    m_ViewerPtr->SetImageList(m_BrowserPtr->GetImageList());
}

void QuiverImpl::HandleSelectionChanged(BrowserEventPtr event)
{
    // For now, we only care about the single-item cursor change for the EXIF view.
}

void QuiverImpl::HandleCursorChanged(BrowserEventPtr event)
{
    if (m_BrowserPtr && m_pExifView) {
        ImageListPtr imageList = m_BrowserPtr->GetImageList();
        if (imageList && imageList->GetSize() > 0) {
            QuiverFile qf = imageList->GetCurrent();
            m_pExifView->SetQuiverFile(qf);
        }
    }
}

//=============================================================================
// IViewerEventHandler implementation
//=============================================================================
void QuiverImpl::HandleItemClicked(ViewerEventPtr event)
{
}

void QuiverImpl::HandleItemActivated(ViewerEventPtr event)
{
    // In viewer, activating an item might mean switching back to browser
    ShowBrowser();
}

void QuiverImpl::HandleCursorChanged(ViewerEventPtr event)
{
    if (m_ViewerPtr && m_pExifView) {
        ImageListPtr imageList = m_ViewerPtr->GetImageList();
        if (imageList && imageList->GetSize() > 0) {
            QuiverFile qf = imageList->GetCurrent();
            m_pExifView->SetQuiverFile(qf);
        }
    }
}

void QuiverImpl::HandleSlideShowStarted(ViewerEventPtr event)
{
}

void QuiverImpl::HandleSlideShowStopped(ViewerEventPtr event)
{
}
