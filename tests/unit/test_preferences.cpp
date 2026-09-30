#include <catch2/catch_test_macros.hpp>
#include "test_helpers.h"
#include "Preferences.h"
#include "QuiverPrefs.h"
#include "Bookmarks.h"
#include "ExternalTools.h"
#include "quiver-image-view.h"
#include <glib.h>
#include <glib/gstdio.h>
#include <cstring>
#include <string>
#include <list>

struct TestPreferencesFixture {
    char m_tmpPath[256];

    TestPreferencesFixture() {
        strncpy(m_tmpPath, "/tmp/quiver_test_prefs_XXXXXX.ini", sizeof(m_tmpPath) - 1);
        int fd = g_mkstemp(m_tmpPath);
        if (fd >= 0) {
            close(fd);
        }
        strncpy(g_szConfigFilePath, m_tmpPath, sizeof(m_tmpPath) - 1);
        g_szConfigFilePath[sizeof(m_tmpPath) - 1] = '\0';
        Preferences::Reset();
    }

    ~TestPreferencesFixture() {
        Preferences::Reset();
        g_unlink(m_tmpPath);
    }
};

TEST_CASE_METHOD(TestPreferencesFixture, "Preferences Get/Set and Default Values", "[unit][prefs][fast]")
{
    PreferencesPtr prefs = Preferences::GetInstance();
    REQUIRE(prefs != nullptr);

    SECTION("Default values when key does not exist")
    {
        REQUIRE(prefs->GetString("nonexistent_sec", "no_str", "default_str") == "default_str");
        REQUIRE(prefs->GetBoolean("nonexistent_sec", "no_bool_t", true) == true);
        REQUIRE(prefs->GetBoolean("nonexistent_sec", "no_bool_f", false) == false);
        REQUIRE(prefs->GetInteger("nonexistent_sec", "no_int", 42) == 42);
        // Note: Preferences writes default values to keyfile on initial query
        REQUIRE(prefs->HasSection("nonexistent_sec"));
        REQUIRE(prefs->HasKey("nonexistent_sec", "no_str"));
    }

    SECTION("String values")
    {
        prefs->SetString("General", "Theme", "Dark");
        REQUIRE(prefs->HasSection("General"));
        REQUIRE(prefs->HasKey("General", "Theme"));
        REQUIRE(prefs->GetString("General", "Theme") == "Dark");
    }

    SECTION("Boolean and Integer values")
    {
        prefs->SetBoolean("Viewer", "Fullscreen", true);
        prefs->SetInteger("Viewer", "ZoomStep", 15);

        REQUIRE(prefs->GetBoolean("Viewer", "Fullscreen") == true);
        REQUIRE(prefs->GetInteger("Viewer", "ZoomStep") == 15);

        // Separate image and video navigation control preferences
        prefs->SetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_NAV_CONTROL, true);
        prefs->SetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_NAV_CONTROL_VIDEO, false);
        REQUIRE(prefs->GetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_NAV_CONTROL, false) == true);
        REQUIRE(prefs->GetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_NAV_CONTROL_VIDEO, true) == false);

        prefs->SetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_NAV_CONTROL, false);
        prefs->SetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_NAV_CONTROL_VIDEO, true);
        REQUIRE(prefs->GetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_NAV_CONTROL, true) == false);
        REQUIRE(prefs->GetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_NAV_CONTROL_VIDEO, false) == true);
    }

    SECTION("Lists of strings, ints, and bools")
    {
        std::list<std::string> strList = {"alpha", "beta", "gamma"};
        prefs->SetStringList("Plugins", "Enabled", strList);
        REQUIRE(prefs->GetStringList("Plugins", "Enabled") == strList);

        std::list<int> intList = {10, 20, 30};
        prefs->SetIntegerList("Metrics", "Steps", intList);
        REQUIRE(prefs->GetIntegerList("Metrics", "Steps") == intList);

        std::list<bool> boolList = {true, false, true};
        prefs->SetBooleanList("Toggles", "States", boolList);
        REQUIRE(prefs->GetBooleanList("Toggles", "States") == boolList);
    }

    SECTION("Key and Section removal")
    {
        prefs->SetString("TempSec", "Key1", "Val1");
        prefs->SetString("TempSec", "Key2", "Val2");

        REQUIRE(prefs->HasKey("TempSec", "Key1"));
        prefs->RemoveKey("TempSec", "Key1");
        REQUIRE_FALSE(prefs->HasKey("TempSec", "Key1"));
        REQUIRE(prefs->HasKey("TempSec", "Key2"));

        prefs->RemoveSection("TempSec");
        REQUIRE_FALSE(prefs->HasSection("TempSec"));
    }
}

TEST_CASE_METHOD(TestPreferencesFixture, "Filmstrip Default Preferences", "[unit][prefs][fast]")
{
    PreferencesPtr prefs = Preferences::GetInstance();
    REQUIRE(prefs != nullptr);

    // Default filmstrip position should be RIGHT (3)
    int fpos = prefs->GetInteger(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_FILMSTRIP_POSITION, FSTRIP_POS_RIGHT);
    REQUIRE(fpos == FSTRIP_POS_RIGHT);

    // Default filmstrip overlay should be true
    bool overlay = prefs->GetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_FILMSTRIP_OVERLAY, true);
    REQUIRE(overlay == true);
}

TEST_CASE_METHOD(TestPreferencesFixture, "Reset flushes even with dangling references", "[unit][prefs][fast]")
{
    // Bookmarks and ExternalTools hold PreferencesPtr internals, and their
    // dialog singletons keep them alive past Quiver's CloseReal resets, which
    // leaks the Preferences singleton (its destructor - and the exit save -
    // never runs). Reset() must therefore flush the file regardless of how
    // many references to the Preferences object are outstanding.
    BookmarksPtr bookmarksKeeper = Bookmarks::GetInstance();
    ExternalToolsPtr toolsKeeper = ExternalTools::GetInstance();
    REQUIRE(bookmarksKeeper != nullptr);
    REQUIRE(toolsKeeper != nullptr);

    PreferencesPtr prefs = Preferences::GetInstance();
    REQUIRE(prefs != nullptr);
    prefs->SetBoolean("PrefsTest", "DetachedRef", true);

    Bookmarks::Reset();
    ExternalTools::Reset();

    // Preferences object must still be alive (leaked refs), mirroring the
    // CloseReal order where these two Resets run before Preferences::Reset().
    REQUIRE(prefs != nullptr);
    REQUIRE(bookmarksKeeper != nullptr);
    REQUIRE(toolsKeeper != nullptr);

    Preferences::Reset();

    GKeyFile *kf = g_key_file_new();
    REQUIRE(g_key_file_load_from_file(kf, g_szConfigFilePath, G_KEY_FILE_NONE, NULL));
    REQUIRE(g_key_file_get_boolean(kf, "PrefsTest", "DetachedRef", NULL) == true);
    g_key_file_free(kf);

    // Clean up the leaked references so the fixture teardown is tidy.
    bookmarksKeeper.reset();
    toolsKeeper.reset();
    Bookmarks::Reset();
    ExternalTools::Reset();
}

TEST_CASE_METHOD(TestPreferencesFixture, "Kinetic Scrolling preference support", "[unit][prefs][kinetic]")
{
    // the last part of this builds a widget to read the scroll setting off it
    REQUIRE_DISPLAY();

    PreferencesPtr prefs = Preferences::GetInstance();
    REQUIRE(prefs != nullptr);

    // Verify default value is true
    bool bDefault = prefs->GetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_KINETIC_SCROLLING, true);
    CHECK(bDefault == true);

    // Toggle off
    prefs->SetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_KINETIC_SCROLLING, false);
    CHECK_FALSE(prefs->GetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_KINETIC_SCROLLING, true));

    // Reset and check persistence
    Preferences::Reset();
    PreferencesPtr prefsReloaded = Preferences::GetInstance();
    CHECK_FALSE(prefsReloaded->GetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_KINETIC_SCROLLING, true));

    // Toggle on
    prefsReloaded->SetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_KINETIC_SCROLLING, true);
    CHECK(prefsReloaded->GetBoolean(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_KINETIC_SCROLLING, false));

    // Verify QuiverImageView smooth_scroll setter/getter
    GtkWidget *imgView = quiver_image_view_new();
    REQUIRE(imgView != nullptr);
    CHECK(quiver_image_view_get_smooth_scroll(QUIVER_IMAGE_VIEW(imgView)) == TRUE);

    quiver_image_view_set_smooth_scroll(QUIVER_IMAGE_VIEW(imgView), FALSE);
    CHECK(quiver_image_view_get_smooth_scroll(QUIVER_IMAGE_VIEW(imgView)) == FALSE);

    quiver_image_view_set_smooth_scroll(QUIVER_IMAGE_VIEW(imgView), TRUE);
    CHECK(quiver_image_view_get_smooth_scroll(QUIVER_IMAGE_VIEW(imgView)) == TRUE);

    g_object_ref_sink(imgView);
    g_object_unref(imgView);
}

