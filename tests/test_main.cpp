#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>
#include <gtk/gtk.h>
#include <gst/gst.h>
extern "C" {
#include <libavutil/log.h>
}
#include <string>
#include <cstdlib>
#include "test_helpers.h"
#include "Preferences.h"
#include "QuiverPrefs.h"
#include "QuiverUtils.h"

/* The view mode, the mode stored for the next session and the filmstrip settings
 * are all process-wide, and a viewer picks them up when it is built - so a test
 * that chooses a mode has changed what the next test's viewer starts from.  That
 * is how a suite ends up passing in one order and failing in another, and it is
 * not something any individual test can be trusted to remember.
 *
 * So the state is taken round every test case: whatever is there when a test
 * begins is what it is given, and whatever the test does to it is handed back
 * when it ends, whether it passed, failed or skipped.  A test can then change the
 * mode as much as it likes, and a test that cares about the mode says which one
 * it wants rather than inheriting whichever ran last. */
class ProcessWideViewerState : public Catch::EventListenerBase
{
public:
    using EventListenerBase::EventListenerBase;

    void testCaseStarting(Catch::TestCaseInfo const &) override
    {
        m_Mode = QuiverUtils::GetRadioActionCurrent("Zoom");
        m_StoredMode = Preferences::GetInstance()->GetInteger(
            QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_DEFAULT_VIEW_MODE, -1);
        m_FilmstripOverlay = Preferences::GetInstance()->GetBoolean(
            QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_FILMSTRIP_OVERLAY, true);
        m_FilmstripShow = Preferences::GetInstance()->GetBoolean(
            QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_FILMSTRIP_SHOW, true);
    }

    void testCaseEnded(Catch::TestCaseStats const &) override
    {
        Preferences::GetInstance()->SetInteger(
            QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_DEFAULT_VIEW_MODE, m_StoredMode);
        Preferences::GetInstance()->SetBoolean(
            QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_FILMSTRIP_OVERLAY, m_FilmstripOverlay);
        Preferences::GetInstance()->SetBoolean(
            QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_FILMSTRIP_SHOW, m_FilmstripShow);
        if (m_Mode >= 0)
            QuiverUtils::SetRadioActionCurrent("Zoom", m_Mode);
    }

private:
    gint m_Mode = -1;
    gint m_StoredMode = -1;
    bool m_FilmstripOverlay = true;
    bool m_FilmstripShow = true;
};

CATCH_REGISTER_LISTENER(ProcessWideViewerState)

// Define external globals required by quiver_core
GtkApplication *g_pApp = nullptr;
gchar g_szConfigFilePath[256] = "/tmp/quiver_test_config.ini";

static bool s_hasDisplay = false;
static std::string s_displayBackend = "none";
static std::string s_imagesDir = "/workspace/tests/images";
static std::string s_dataDir = "/workspace/data";

bool QuiverTest_HasDisplay()
{
    return s_hasDisplay;
}

const char* QuiverTest_GetDisplayBackend()
{
    return s_displayBackend.c_str();
}

std::string QuiverTest_GetImagesDir()
{
    const char* env = std::getenv("QUIVER_TEST_IMAGES_DIR");
    if (env && env[0])
        return std::string(env);
    return s_imagesDir;
}

std::string QuiverTest_GetDataDir()
{
    const char* env = std::getenv("QUIVER_DATA_DIR");
    if (env && env[0])
        return std::string(env);
    return s_dataDir;
}

int main(int argc, char* argv[])
{
    // Suppress FFmpeg stderr noise
    av_log_set_level(AV_LOG_ERROR);

    // Initialize GStreamer if available
    gst_init_check(&argc, &argv, nullptr);

    // Check display capability without hard-failing if headless
    if (gtk_init_check())
    {
        s_hasDisplay = true;
        const char* wayland = std::getenv("WAYLAND_DISPLAY");
        const char* x11 = std::getenv("DISPLAY");
        if (wayland && wayland[0])
        {
            s_displayBackend = "wayland";
        }
        else if (x11 && x11[0])
        {
            s_displayBackend = "x11";
        }
        else
        {
            s_displayBackend = "unknown_display";
        }
    }
    else
    {
        s_hasDisplay = false;
        s_displayBackend = "none";
    }

#ifdef QUIVER_TEST_IMAGES_DIR_DEF
    s_imagesDir = QUIVER_TEST_IMAGES_DIR_DEF;
#endif
#ifdef QUIVER_DATA_DIR_DEF
    s_dataDir = QUIVER_DATA_DIR_DEF;
#endif

    Catch::Session session;
    int result = session.applyCommandLine(argc, argv);
    if (result != 0)
        return result;

    return session.run();
}
