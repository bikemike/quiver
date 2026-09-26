#include <catch2/catch_test_macros.hpp>
#include <gtk/gtk.h>

#include <string>

#include "Browser.h"
#include "Preferences.h"
#include "QuiverPrefs.h"
#include "Statusbar.h"
#include "test_helpers.h"

// Mirrors MIN_SIDEBAR_WIDTH / MIN_PREVIEW_HEIGHT in Browser.cpp: the floor
// each pane keeps so it can never be allocated away to nothing.
static const int MIN_SIDEBAR_WIDTH = 120;
static const int MIN_PREVIEW_HEIGHT = 96;

// GTK4 lays out on a frame-clock tick, so pump for a fixed slice of wall time
// rather than draining whatever is already pending; a single non-blocking
// drain returns before the resize is ever allocated.
static void settle(int ms = 250)
{
    gint64 deadline = g_get_monotonic_time() + ms * G_TIME_SPAN_MILLISECOND;
    while (g_get_monotonic_time() < deadline)
    {
        g_main_context_iteration(nullptr, FALSE);
        g_usleep(1000);
    }
}

namespace
{

// The window and the Browser are deliberately never destroyed.  ~BrowserImpl
// unparents the browser widget subtree while `this` is still alive, and
// pumping the main loop afterwards re-enters the freed object through the icon
// view's unmap controller; destroying the window first instead leaves
// ~BrowserImpl holding freed widgets.  QuiverUtils' action group is
// process-global and every Browser registers actions into it that nothing
// unregisters, so freeing a Browser here would leave those actions pointing
// at a destroyed object for the rest of the run.  Leaving both to process
// teardown keeps the whole suite off that path.
struct WindowHandle
{
    GtkWidget* m_pWidget = nullptr;
};

struct BrowserPaneFixture
{
    WindowHandle m_window;
    Browser* m_pBrowser = nullptr;   // leaked on purpose, see above
    GtkWidget* m_pHpaned = nullptr;   // sidebar column | icon view
    GtkWidget* m_pVpaned = nullptr;   // folder tree | preview
    GtkWidget* m_pIconViewBox = nullptr;
    GtkWidget* m_pNotebook = nullptr; // folder tree column
    GtkWidget* m_pPreview = nullptr;

    BrowserPaneFixture()
    {
        // Pin the toggles on so the sidebar column is laid out regardless of
        // whatever a previous test left in the (shared) preferences file.
        Preferences::GetInstance()->SetBoolean(QUIVER_PREFS_BROWSER,
                                               QUIVER_PREFS_BROWSER_FOLDERTREE_SHOW, true);
        Preferences::GetInstance()->SetBoolean(QUIVER_PREFS_BROWSER,
                                               QUIVER_PREFS_BROWSER_PREVIEW_SHOW, true);

        m_pBrowser = new Browser();
        StatusbarPtr statusbar(new Statusbar());
        m_pBrowser->SetStatusbar(statusbar);

        m_pHpaned = m_pBrowser->GetWidget();
        REQUIRE(GTK_IS_PANED(m_pHpaned));
        m_pVpaned = gtk_paned_get_start_child(GTK_PANED(m_pHpaned));
        REQUIRE(GTK_IS_PANED(m_pVpaned));
        m_pIconViewBox = gtk_paned_get_end_child(GTK_PANED(m_pHpaned));
        REQUIRE(m_pIconViewBox != nullptr);
        m_pNotebook = gtk_paned_get_start_child(GTK_PANED(m_pVpaned));
        REQUIRE(GTK_IS_NOTEBOOK(m_pNotebook));
        m_pPreview = gtk_paned_get_end_child(GTK_PANED(m_pVpaned));
        REQUIRE(GTK_IS_WIDGET(m_pPreview));

        // The constructor leaves the browser widget hidden; start from a
        // known divider position instead of a persisted one.
        gtk_paned_set_position(GTK_PANED(m_pHpaned), 200);
        gtk_paned_set_position(GTK_PANED(m_pVpaned), 300);

        m_window.m_pWidget = gtk_window_new();
        gtk_window_set_child(GTK_WINDOW(m_window.m_pWidget), m_pHpaned);
        gtk_window_set_default_size(GTK_WINDOW(m_window.m_pWidget), 1000, 700);
        gtk_window_present(GTK_WINDOW(m_window.m_pWidget));
        gtk_widget_set_visible(m_pHpaned, TRUE);
        settle();
    }

    void resize(int width, int height)
    {
        gtk_window_set_default_size(GTK_WINDOW(m_window.m_pWidget), width, height);
        gtk_window_present(GTK_WINDOW(m_window.m_pWidget));
        settle();
    }
};

} // namespace

TEST_CASE("Browser panes: growth is absorbed by the icon view, not the sidebar",
          "[unit][browser][gui][layout]")
{
    REQUIRE_DISPLAY();
    BrowserPaneFixture f;

    // The policy itself, which is what a window resize acts on.
    CHECK(gtk_paned_get_resize_start_child(GTK_PANED(f.m_pHpaned)) == FALSE);
    CHECK(gtk_paned_get_resize_end_child(GTK_PANED(f.m_pHpaned)) == TRUE);
    CHECK(gtk_paned_get_resize_start_child(GTK_PANED(f.m_pVpaned)) == TRUE);
    CHECK(gtk_paned_get_resize_end_child(GTK_PANED(f.m_pVpaned)) == FALSE);
    CHECK(gtk_paned_get_shrink_end_child(GTK_PANED(f.m_pVpaned)) == FALSE);

    int sidebarWidth = gtk_widget_get_width(f.m_pVpaned);
    int previewHeight = gtk_widget_get_height(f.m_pPreview);
    int treeHeight = gtk_widget_get_height(f.m_pNotebook);
    int iconBoxHeight = gtk_widget_get_height(f.m_pIconViewBox);
    REQUIRE(sidebarWidth >= MIN_SIDEBAR_WIDTH);
    REQUIRE(previewHeight > 0);
    REQUIRE(iconBoxHeight > treeHeight);

    // A wider, taller window: the sidebar column keeps its width, the icon
    // view takes the horizontal slack, and the tree takes the vertical slack.
    f.resize(1400, 900);

    CHECK(gtk_widget_get_width(f.m_pVpaned) == sidebarWidth);
    CHECK(gtk_widget_get_height(f.m_pIconViewBox) > iconBoxHeight);
    CHECK(gtk_widget_get_height(f.m_pNotebook) > treeHeight);
    CHECK(gtk_widget_get_height(f.m_pPreview) == previewHeight);
}

TEST_CASE("Browser panes: a stale divider position cannot strand the preview",
          "[unit][browser][gui][layout]")
{
    REQUIRE_DISPLAY();
    BrowserPaneFixture f;

    // A position persisted from a much taller window (quiver used to store
    // 1101 here) is applied unclamped; allocation then has to keep the
    // preview usable and the grabber on screen.
    gtk_paned_set_position(GTK_PANED(f.m_pVpaned), 1101);
    settle();

    CHECK(gtk_paned_get_position(GTK_PANED(f.m_pVpaned)) < gtk_widget_get_height(f.m_pVpaned));
    CHECK(gtk_widget_get_height(f.m_pPreview) >= MIN_PREVIEW_HEIGHT);

    // The same must hold when the window is short enough that the persisted
    // position is larger than the whole sidebar column.
    f.resize(1400, 500);
    CHECK(gtk_widget_get_height(f.m_pPreview) >= MIN_PREVIEW_HEIGHT);
    CHECK(gtk_widget_get_visible(f.m_pPreview));
}

TEST_CASE("Browser panes: sidebar and preview toggles act independently",
          "[unit][browser][gui][layout]")
{
    REQUIRE_DISPLAY();
    BrowserPaneFixture f;

    // Preview off: the folder tree keeps the full sidebar column.
    gtk_widget_set_visible(f.m_pPreview, FALSE);
    settle();
    CHECK(gtk_widget_get_visible(f.m_pVpaned));
    CHECK(gtk_widget_get_visible(f.m_pNotebook));
    CHECK(gtk_widget_get_height(f.m_pNotebook) > gtk_widget_get_height(f.m_pPreview));

    // Preview on again: it comes back with a usable share of the column.
    gtk_widget_set_visible(f.m_pPreview, TRUE);
    settle();
    CHECK(gtk_widget_get_visible(f.m_pVpaned));
    CHECK(gtk_widget_get_height(f.m_pPreview) >= MIN_PREVIEW_HEIGHT);

    // Folder tree off: the column and the preview stay, tree gone.
    gtk_widget_set_visible(f.m_pNotebook, FALSE);
    settle();
    CHECK(gtk_widget_get_visible(f.m_pVpaned));
    CHECK(gtk_widget_get_visible(f.m_pPreview));
    CHECK(gtk_widget_get_height(f.m_pPreview) == gtk_widget_get_height(f.m_pVpaned));

    // Both off: only then is the whole column hidden.
    gtk_widget_set_visible(f.m_pPreview, FALSE);
    settle();
    CHECK(gtk_widget_get_visible(f.m_pVpaned));
}
