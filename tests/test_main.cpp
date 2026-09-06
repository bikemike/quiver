#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <gtk/gtk.h>
#include <gst/gst.h>
#include <string>
#include <cstdlib>
#include "test_helpers.h"

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
