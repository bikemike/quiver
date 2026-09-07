#include <catch2/catch_test_macros.hpp>
#include "ShortcutManager.h"
#include "Preferences.h"
#include "test_helpers.h"
#include <algorithm>
#include <cstring>
#include <gdk/gdkkeysyms.h>

TEST_CASE("ShortcutManager Default Action Registry", "[unit][shortcuts][fast]")
{
    ShortcutManager &sm = ShortcutManager::GetInstance();
    sm.Init();

    const auto &actions = sm.GetActions();
    REQUIRE(actions.size() >= 20);

    SECTION("Viewer Navigation standard defaults")
    {
        const ShortcutActionDef *next_def = sm.GetAction("ImageNext");
        REQUIRE(next_def != nullptr);
        REQUIRE(next_def->category == "Viewer Navigation");
        REQUIRE(std::find(next_def->default_accels.begin(), next_def->default_accels.end(), "Right") != next_def->default_accels.end());
        REQUIRE(std::find(next_def->default_accels.begin(), next_def->default_accels.end(), "Page_Down") != next_def->default_accels.end());
        // Spacebar and Backspace must NOT be in navigation
        REQUIRE(std::find(next_def->default_accels.begin(), next_def->default_accels.end(), "space") == next_def->default_accels.end());

        const ShortcutActionDef *prev_def = sm.GetAction("ImagePrevious");
        REQUIRE(prev_def != nullptr);
        REQUIRE(std::find(prev_def->default_accels.begin(), prev_def->default_accels.end(), "Left") != prev_def->default_accels.end());
        REQUIRE(std::find(prev_def->default_accels.begin(), prev_def->default_accels.end(), "Page_Up") != prev_def->default_accels.end());
        REQUIRE(std::find(prev_def->default_accels.begin(), prev_def->default_accels.end(), "BackSpace") == prev_def->default_accels.end());
    }

    SECTION("Video Playback and Scrubbing standard defaults")
    {
        const ShortcutActionDef *play_def = sm.GetAction("VideoPlay");
        REQUIRE(play_def != nullptr);
        REQUIRE(std::find(play_def->default_accels.begin(), play_def->default_accels.end(), "space") != play_def->default_accels.end());
        REQUIRE(std::find(play_def->default_accels.begin(), play_def->default_accels.end(), "k") != play_def->default_accels.end());

        const ShortcutActionDef *seek5_back = sm.GetAction("VideoSeekBack5");
        REQUIRE(seek5_back != nullptr);
        REQUIRE(std::find(seek5_back->default_accels.begin(), seek5_back->default_accels.end(), "comma") != seek5_back->default_accels.end());

        const ShortcutActionDef *seek5_fwd = sm.GetAction("VideoSeekFwd5");
        REQUIRE(seek5_fwd != nullptr);
        REQUIRE(std::find(seek5_fwd->default_accels.begin(), seek5_fwd->default_accels.end(), "period") != seek5_fwd->default_accels.end());

        const ShortcutActionDef *skip_back = sm.GetAction("VideoSkipBack");
        REQUIRE(skip_back != nullptr);
        REQUIRE(std::find(skip_back->default_accels.begin(), skip_back->default_accels.end(), "j") != skip_back->default_accels.end());

        const ShortcutActionDef *skip_fwd = sm.GetAction("VideoSkipForward");
        REQUIRE(skip_fwd != nullptr);
        REQUIRE(std::find(skip_fwd->default_accels.begin(), skip_fwd->default_accels.end(), "l") != skip_fwd->default_accels.end());

        const ShortcutActionDef *frame_back = sm.GetAction("VideoFrameBack");
        REQUIRE(frame_back != nullptr);
        REQUIRE(std::find(frame_back->default_accels.begin(), frame_back->default_accels.end(), "<Shift>less") != frame_back->default_accels.end());
        REQUIRE(std::find(frame_back->default_accels.begin(), frame_back->default_accels.end(), "<Shift>j") != frame_back->default_accels.end());

        const ShortcutActionDef *frame_fwd = sm.GetAction("VideoFrameFwd");
        REQUIRE(frame_fwd != nullptr);
        REQUIRE(std::find(frame_fwd->default_accels.begin(), frame_fwd->default_accels.end(), "<Shift>greater") != frame_fwd->default_accels.end());
        REQUIRE(std::find(frame_fwd->default_accels.begin(), frame_fwd->default_accels.end(), "<Shift>l") != frame_fwd->default_accels.end());
    }

    SECTION("Image Rotation defaults free l/L")
    {
        const ShortcutActionDef *cw_def = sm.GetAction("RotateCW");
        REQUIRE(cw_def != nullptr);
        REQUIRE(std::find(cw_def->default_accels.begin(), cw_def->default_accels.end(), "r") != cw_def->default_accels.end());
        REQUIRE(std::find(cw_def->default_accels.begin(), cw_def->default_accels.end(), "bracketright") != cw_def->default_accels.end());
        // Must NOT contain l or <Shift>l
        REQUIRE(std::find(cw_def->default_accels.begin(), cw_def->default_accels.end(), "l") == cw_def->default_accels.end());
        REQUIRE(std::find(cw_def->default_accels.begin(), cw_def->default_accels.end(), "<Shift>l") == cw_def->default_accels.end());

        const ShortcutActionDef *ccw_def = sm.GetAction("RotateCCW");
        REQUIRE(ccw_def != nullptr);
        REQUIRE(std::find(ccw_def->default_accels.begin(), ccw_def->default_accels.end(), "<Shift>r") != ccw_def->default_accels.end());
        REQUIRE(std::find(ccw_def->default_accels.begin(), ccw_def->default_accels.end(), "bracketleft") != ccw_def->default_accels.end());
        REQUIRE(std::find(ccw_def->default_accels.begin(), ccw_def->default_accels.end(), "l") == ccw_def->default_accels.end());
    }
}

TEST_CASE("ShortcutManager Multi-Key Customization", "[unit][shortcuts][fast]")
{
    ShortcutManager &sm = ShortcutManager::GetInstance();
    sm.Init();
    sm.ResetToDefault("ImageNext");

    SECTION("Adding and removing accelerators")
    {
        const ShortcutActionDef *def = sm.GetAction("ImageNext");
        size_t initial_count = def->current_accels.size();

        // Add a new accelerator
        bool added = sm.AddAccelerator("ImageNext", "<Control><Alt>n");
        REQUIRE(added);
        def = sm.GetAction("ImageNext");
        REQUIRE(def->current_accels.size() == initial_count + 1);
        REQUIRE(std::find(def->current_accels.begin(), def->current_accels.end(), "<Control><Alt>n") != def->current_accels.end());

        // Duplicate add returns false
        REQUIRE(!sm.AddAccelerator("ImageNext", "<Control><Alt>n"));

        // Remove accelerator
        bool removed = sm.RemoveAccelerator("ImageNext", "<Control><Alt>n");
        REQUIRE(removed);
        def = sm.GetAction("ImageNext");
        REQUIRE(def->current_accels.size() == initial_count);

        // Reset to default
        sm.ResetToDefault("ImageNext");
        def = sm.GetAction("ImageNext");
        REQUIRE(def->current_accels == def->default_accels);
    }

    SECTION("Setting entire accelerator list")
    {
        std::vector<std::string> custom = {"n", "<Alt>Right"};
        sm.SetAccelerators("ImageNext", custom);
        const ShortcutActionDef *def = sm.GetAction("ImageNext");
        REQUIRE(def->current_accels == custom);

        // Reset to default restores original
        sm.ResetToDefault("ImageNext");
        def = sm.GetAction("ImageNext");
        REQUIRE(def->current_accels == def->default_accels);
    }
}

TEST_CASE("ShortcutManager Conflict Detection", "[unit][shortcuts][fast]")
{
    ShortcutManager &sm = ShortcutManager::GetInstance();
    sm.Init();
    sm.ResetAllToDefaults();

    // <Control>o is assigned to FileOpen ("Open File")
    std::string conflict = sm.FindConflictingAction("<Control>o", "Save");
    REQUIRE(conflict == "Open File");

    // Conflict check excluding FileOpen itself returns empty
    conflict = sm.FindConflictingAction("<Control>o", "FileOpen");
    REQUIRE(conflict.empty());

    // Layout-neutral aliases conflict detection
    conflict = sm.FindConflictingAction("<Shift>less", "Save");
    REQUIRE(conflict == "Frame Step Back");
    conflict = sm.FindConflictingAction("less", "Save");
    REQUIRE(conflict == "Frame Step Back");
    conflict = sm.FindConflictingAction("<Shift>comma", "Save");
    REQUIRE(conflict == "Frame Step Back");

    conflict = sm.FindConflictingAction("<Shift>greater", "Save");
    REQUIRE(conflict == "Frame Step Forward");
    conflict = sm.FindConflictingAction("greater", "Save");
    REQUIRE(conflict == "Frame Step Forward");

    // Unassigned key combination returns empty
    conflict = sm.FindConflictingAction("<Control><Shift><Alt>z");
    REQUIRE(conflict.empty());
}

TEST_CASE("ShortcutManager Unmodified Key Detection", "[unit][shortcuts][fast]")
{
    // Unmodified keys (letters, symbols, arrows without Ctrl/Alt/Meta)
    REQUIRE(ShortcutManager::IsUnmodifiedAccel("r"));
    REQUIRE(ShortcutManager::IsUnmodifiedAccel("j"));
    REQUIRE(ShortcutManager::IsUnmodifiedAccel("k"));
    REQUIRE(ShortcutManager::IsUnmodifiedAccel("l"));
    REQUIRE(ShortcutManager::IsUnmodifiedAccel("space"));
    REQUIRE(ShortcutManager::IsUnmodifiedAccel("comma"));
    REQUIRE(ShortcutManager::IsUnmodifiedAccel("period"));
    REQUIRE(ShortcutManager::IsUnmodifiedAccel("less"));
    REQUIRE(ShortcutManager::IsUnmodifiedAccel("greater"));
    REQUIRE(ShortcutManager::IsUnmodifiedAccel("<Shift>less"));
    REQUIRE(ShortcutManager::IsUnmodifiedAccel("<Shift>greater"));
    REQUIRE(ShortcutManager::IsUnmodifiedAccel("<Shift>j"));
    REQUIRE(ShortcutManager::IsUnmodifiedAccel("<Shift>l"));
    REQUIRE(ShortcutManager::IsUnmodifiedAccel("Right"));
    REQUIRE(ShortcutManager::IsUnmodifiedAccel("Left"));

    // Modified keys
    REQUIRE_FALSE(ShortcutManager::IsUnmodifiedAccel("<Control>o"));
    REQUIRE_FALSE(ShortcutManager::IsUnmodifiedAccel("<Alt>Left"));
    REQUIRE_FALSE(ShortcutManager::IsUnmodifiedAccel("<Control><Shift>m"));
}

TEST_CASE("ShortcutManager Key Translation Helpers", "[unit][shortcuts][fast]")
{
    std::string accel = ShortcutManager::KeyvalAndModsToAccelString(GDK_KEY_Right, (GdkModifierType)0);
    REQUIRE(accel == "Right");

    std::string ctrl_o = ShortcutManager::KeyvalAndModsToAccelString(GDK_KEY_o, GDK_CONTROL_MASK);
    REQUIRE(ctrl_o == "<Control>o");

    std::string label = ShortcutManager::AccelStringToHumanLabel("<Control>o");
    REQUIRE(label.find("Ctrl") != std::string::npos);
    REQUIRE(label.find("O") != std::string::npos);

    SECTION("Shift with comma and period produces <Shift>less and <Shift>greater")
    {
        // On US keyboards, Shift + ',' produces GDK_KEY_less and GDK_SHIFT_MASK
        std::string accel_less = ShortcutManager::KeyvalAndModsToAccelString(GDK_KEY_less, GDK_SHIFT_MASK);
        REQUIRE(accel_less == "<Shift>less");
        std::string human_less = ShortcutManager::AccelStringToHumanLabel(accel_less);
        REQUIRE(human_less == "Shift+<");

        // Shift + '.' produces GDK_KEY_greater and GDK_SHIFT_MASK
        std::string accel_greater = ShortcutManager::KeyvalAndModsToAccelString(GDK_KEY_greater, GDK_SHIFT_MASK);
        REQUIRE(accel_greater == "<Shift>greater");
        std::string human_greater = ShortcutManager::AccelStringToHumanLabel(accel_greater);
        REQUIRE(human_greater == "Shift+>");
    }

    SECTION("Pango markup escaping for < and > key labels")
    {
        std::string human_less = "Shift+<";
        std::string human_greater = "Shift+>";

        // Unescaped g_strdup_printf fails to parse in Pango because '<' breaks XML tag syntax
        gchar *unescaped = g_strdup_printf("<span size='xx-large'><b>%s</b></span>", human_less.c_str());
        GError *err = nullptr;
        gboolean ok = pango_parse_markup(unescaped, -1, 0, nullptr, nullptr, nullptr, &err);
        REQUIRE_FALSE(ok); // Must fail without escaping!
        REQUIRE(err != nullptr);
        g_error_free(err);
        g_free(unescaped);

        // Escaped markup via g_markup_printf_escaped must parse cleanly
        gchar *escaped_less = g_markup_printf_escaped("<span size='xx-large'><b>%s</b></span>", human_less.c_str());
        err = nullptr;
        ok = pango_parse_markup(escaped_less, -1, 0, nullptr, nullptr, nullptr, &err);
        REQUIRE(ok == TRUE);
        REQUIRE(err == nullptr);
        g_free(escaped_less);

        gchar *escaped_greater = g_markup_printf_escaped("<span size='xx-large'><b>%s</b></span>", human_greater.c_str());
        err = nullptr;
        ok = pango_parse_markup(escaped_greater, -1, 0, nullptr, nullptr, nullptr, &err);
        REQUIRE(ok == TRUE);
        REQUIRE(err == nullptr);
        g_free(escaped_greater);
    }
}

