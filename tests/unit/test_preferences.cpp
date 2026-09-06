#include <catch2/catch_test_macros.hpp>
#include "Preferences.h"
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
