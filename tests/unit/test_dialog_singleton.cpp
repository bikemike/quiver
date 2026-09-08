#include <catch2/catch_test_macros.hpp>
#include <gtk/gtk.h>
#include <cstring>
#include "PreferencesDlg.h"
#include "BookmarksDlg.h"
#include "ExternalToolsDlg.h"
#include "AdjustDateDlg.h"
#include "RenameDlg.h"
#include "OrganizeDlg.h"
#include "BookmarkAddEditDlg.h"
#include "test_helpers.h"

static guint ToplevelCount()
{
    GList *tops = gtk_window_list_toplevels();
    guint n = g_list_length(tops);
    g_list_free(tops);
    return n;
}

/* Toplevels left behind by earlier test cases may still be visible, so look
 * the dialog up by its window title instead of assuming a clean slate. */
static GtkWidget* FindToplevelByTitle(const char *title)
{
    GList *tops = gtk_window_list_toplevels();
    GtkWidget *w = NULL;
    for (GList *l = tops; l != NULL; l = l->next)
    {
        const char *t = gtk_window_get_title(GTK_WINDOW(l->data));
        if (t != NULL && strcmp(t, title) == 0)
        {
            w = GTK_WIDGET(l->data);
            break;
        }
    }
    g_list_free(tops);
    return w;
}

static int s_extRun = 0;
static GtkWidget *s_extDlg[2] = {NULL, NULL};

static gboolean CloseVisibleExtCb(gpointer user_data)
{
    (void)user_data;
    GtkWidget *w = FindToplevelByTitle("External Tools");
    CHECK(w != NULL);
    if (w == NULL)
        return G_SOURCE_REMOVE;
    s_extDlg[s_extRun > 0 ? 1 : 0] = w;
    CHECK(gtk_widget_get_visible(w));
    /* Only the Adjust Date dialog may be modal. */
    CHECK(!gtk_window_get_modal(GTK_WINDOW(w)));
    gtk_window_close(GTK_WINDOW(w));
    return G_SOURCE_REMOVE;
}

/* Captures the modal state of a named dialog while it is open, then closes it
 * so the blocking Run() loop can return. */
struct ModalCapture
{
    const char *title;
    gboolean modal;
    GtkWidget *w;
};

static gboolean CaptureModalAndCloseCb(gpointer user_data)
{
    ModalCapture *cap = static_cast<ModalCapture*>(user_data);
    cap->w = FindToplevelByTitle(cap->title);
    CHECK(cap->w != NULL);
    if (cap->w == NULL)
        return G_SOURCE_REMOVE;
    cap->modal = gtk_window_get_modal(GTK_WINDOW(cap->w));
    CHECK(gtk_widget_get_visible(cap->w));
    gtk_window_close(GTK_WINDOW(cap->w));
    return G_SOURCE_REMOVE;
}

/* Regression test for the "A window is shown after it has been destroyed"
 * warning and the subsequently uncloseable Preferences dialog.  The dialog
 * must be hidden (not destroyed) when dismissed so the singleton can be
 * re-presented by a later ShowDialog(). */
TEST_CASE("Preferences dialog stays single-instance and reopens after close", "[unit][gui][dialog]")
{
    if (!QuiverTest_HasDisplay())
        SKIP("No display");

    guint base = ToplevelCount();

    PreferencesDlg::ShowDialog();
    GtkWidget *dlg = FindToplevelByTitle("Quiver Preferences");
    REQUIRE(dlg != NULL);
    REQUIRE(gtk_widget_get_visible(dlg));
    REQUIRE(ToplevelCount() == base + 1);

    /* A second ShowDialog while open must reuse the window, not stack a new one. */
    PreferencesDlg::ShowDialog();
    REQUIRE(gtk_widget_get_visible(dlg));
    REQUIRE(ToplevelCount() == base + 1);

    /* Window-manager close must HIDE the dialog (kept alive for reuse)... */
    gtk_window_close(GTK_WINDOW(dlg));
    REQUIRE(!gtk_widget_get_visible(dlg));

    /* ...and re-opening must present that same window again. */
    PreferencesDlg::ShowDialog();
    REQUIRE(gtk_widget_get_visible(dlg));
    REQUIRE(ToplevelCount() == base + 1);

    /* It must be closeable again after the round-trip. */
    gtk_window_close(GTK_WINDOW(dlg));
    REQUIRE(!gtk_widget_get_visible(dlg));

    /* Hide (all hidden singletons remain alive for the next test case). */
    g_object_ref(dlg);
    gtk_widget_set_visible(dlg, FALSE);
    g_object_unref(dlg);
}

TEST_CASE("Bookmarks dialog stays single-instance and reopens after close", "[unit][gui][dialog]")
{
    if (!QuiverTest_HasDisplay())
        SKIP("No display");

    guint base = ToplevelCount();

    BookmarksDlg::ShowDialog();
    GtkWidget *dlg = FindToplevelByTitle("Manage Bookmarks");
    REQUIRE(dlg != NULL);
    REQUIRE(gtk_widget_get_visible(dlg));
    REQUIRE(ToplevelCount() == base + 1);

    BookmarksDlg::ShowDialog();
    REQUIRE(gtk_widget_get_visible(dlg));
    REQUIRE(ToplevelCount() == base + 1);

    gtk_window_close(GTK_WINDOW(dlg));
    REQUIRE(!gtk_widget_get_visible(dlg));

    BookmarksDlg::ShowDialog();
    REQUIRE(gtk_widget_get_visible(dlg));
    REQUIRE(ToplevelCount() == base + 1);

    gtk_window_close(GTK_WINDOW(dlg));
    REQUIRE(!gtk_widget_get_visible(dlg));

    g_object_ref(dlg);
    gtk_widget_set_visible(dlg, FALSE);
    g_object_unref(dlg);
}

TEST_CASE("External tools dialog stays single-instance and reopens after close", "[unit][gui][dialog]")
{
    if (!QuiverTest_HasDisplay())
        SKIP("No display");

    guint base = ToplevelCount();
    s_extRun = 0;
    s_extDlg[0] = s_extDlg[1] = NULL;

    /* ShowDialog() blocks in a nested main loop until the dialog is closed,
     * so the close is scheduled from inside that loop. */
    g_timeout_add(50, CloseVisibleExtCb, NULL);
    ExternalToolsDlg::ShowDialog();
    REQUIRE(s_extDlg[0] != NULL);
    REQUIRE(!gtk_widget_get_visible(s_extDlg[0]));
    REQUIRE(ToplevelCount() == base + 1);

    s_extRun = 1;
    g_timeout_add(50, CloseVisibleExtCb, NULL);
    ExternalToolsDlg::ShowDialog();
    REQUIRE(s_extDlg[1] == s_extDlg[0]);
    REQUIRE(!gtk_widget_get_visible(s_extDlg[1]));

    g_object_ref(s_extDlg[1]);
    gtk_widget_set_visible(s_extDlg[1], FALSE);
    g_object_unref(s_extDlg[1]);
}

/* Only the Adjust Date dialog edits the current selection, so it is the only
 * one allowed to be modal (grabbing input).  The rest must stay non-modal yet
 * still appear above the Quiver window (transient-for is set at runtime from
 * the live application window, so it cannot be asserted here). */
TEST_CASE("Adjust Date is the only modal dialog", "[unit][gui][dialog]")
{
    if (!QuiverTest_HasDisplay())
        SKIP("No display");

    ModalCapture cap;

    cap = {"Adjust Date", FALSE, NULL};
    g_timeout_add(50, CaptureModalAndCloseCb, &cap);
    AdjustDateDlg adjustDate;
    adjustDate.Run();
    REQUIRE(cap.w != NULL);
    REQUIRE(cap.modal); /* the single intended modal dialog */

    cap = {"Rename", FALSE, NULL};
    g_timeout_add(50, CaptureModalAndCloseCb, &cap);
    RenameDlg rename;
    rename.Run();
    REQUIRE(cap.w != NULL);
    REQUIRE(!cap.modal);

    cap = {"Organize", FALSE, NULL};
    g_timeout_add(50, CaptureModalAndCloseCb, &cap);
    OrganizeDlg organize;
    organize.Run();
    REQUIRE(cap.w != NULL);
    REQUIRE(!cap.modal);

    cap = {"Add/Edit Bookmark", FALSE, NULL};
    g_timeout_add(50, CaptureModalAndCloseCb, &cap);
    BookmarkAddEditDlg edit;
    edit.Run();
    REQUIRE(cap.w != NULL);
    REQUIRE(!cap.modal);
}