#include <catch2/catch_test_macros.hpp>
#include "ExternalTools.h"
#include "Preferences.h"
#include "ShortcutManager.h"
#include <glib.h>
#include <glib/gstdio.h>
#include <cstring>
#include <vector>

struct TestExternalToolsFixture {
    char m_tmpPath[256];

    TestExternalToolsFixture() {
        strncpy(m_tmpPath, "/tmp/quiver_test_tools_XXXXXX.ini", sizeof(m_tmpPath) - 1);
        int fd = g_mkstemp(m_tmpPath);
        if (fd >= 0) {
            close(fd);
        }
        strncpy(g_szConfigFilePath, m_tmpPath, sizeof(m_tmpPath) - 1);
        g_szConfigFilePath[sizeof(m_tmpPath) - 1] = '\0';
        ExternalTools::Reset();
        Preferences::Reset();
    }

    ~TestExternalToolsFixture() {
        ExternalTools::Reset();
        Preferences::Reset();
        g_unlink(m_tmpPath);
    }
};

TEST_CASE_METHOD(TestExternalToolsFixture, "ExternalTools CRUD and Ordering", "[unit][tools][fast]")
{
    ExternalToolsPtr tools = ExternalTools::GetInstance();
    REQUIRE(tools != nullptr);

    SECTION("Add and query external tool")
    {
        ExternalTool tool("GIMP", "Open with GIMP", "gimp", "gimp %F", true, false, true);
        REQUIRE(tools->AddExternalTool(tool));

        std::vector<ExternalTool> list = tools->GetExternalTools();
        REQUIRE(list.size() == 1);
        REQUIRE(list[0].GetName() == "GIMP");
        REQUIRE(list[0].GetCmd() == "gimp %F");
        REQUIRE(list[0].GetSupportsMultiple() == true);
        REQUIRE(list[0].GetShowOutput() == false);
        REQUIRE(list[0].GetShowErrors() == true);

        int id = list[0].GetID();
        const ExternalTool* found = tools->GetExternalTool(id);
        REQUIRE(found != nullptr);
        REQUIRE(found->GetName() == "GIMP");
    }

    SECTION("Update external tool")
    {
        ExternalTool tool("Editor", "Text Editor", "gedit", "gedit %f", false, true, true);
        REQUIRE(tools->AddExternalTool(tool));

        std::vector<ExternalTool> list = tools->GetExternalTools();
        REQUIRE(list.size() == 1);
        int id = list[0].GetID();

        ExternalTool updated = list[0];
        updated.SetCmd("code %f");
        updated.SetName("VSCode");
        REQUIRE(tools->UpdateExternalTool(updated));

        const ExternalTool* retrieved = tools->GetExternalTool(id);
        REQUIRE(retrieved != nullptr);
        REQUIRE(retrieved->GetName() == "VSCode");
        REQUIRE(retrieved->GetCmd() == "code %f");
    }

    SECTION("Remove tool")
    {
        ExternalTool tool("Temporary", "", "", "echo %f", false, false, false);
        tools->AddExternalTool(tool);

        std::vector<ExternalTool> list = tools->GetExternalTools();
        REQUIRE(list.size() == 1);
        int id = list[0].GetID();

        REQUIRE(tools->Remove(id));
        REQUIRE(tools->GetExternalTools().empty());
        REQUIRE(tools->GetExternalTool(id) == nullptr);
    }

    SECTION("Shortcut and ShowOnlyOnError serialization")
    {
        ExternalTool tool("Nautilus", "Open folder", "system-file-manager", "nautilus %d", true, false, false, true, "<Control><Alt>f");
        REQUIRE(tools->AddExternalTool(tool));

        std::vector<ExternalTool> list = tools->GetExternalTools();
        REQUIRE(list.size() == 1);
        REQUIRE(list[0].GetShowOnlyOnError() == true);
        REQUIRE(list[0].GetShortcut() == "<Control><Alt>f");

        // Reload from preferences
        ExternalTools::Reset();
        ExternalToolsPtr reloaded = ExternalTools::GetInstance();
        std::vector<ExternalTool> reloadedList = reloaded->GetExternalTools();
        REQUIRE(reloadedList.size() == 1);
        REQUIRE(reloadedList[0].GetName() == "Nautilus");
        REQUIRE(reloadedList[0].GetShowOnlyOnError() == true);
        REQUIRE(reloadedList[0].GetShortcut() == "<Control><Alt>f");
    }

    SECTION("Multi-selection and Media Type options serialization")
    {
        ExternalTool tool("CustomTool", "Custom", "exec", "tool %f", true, false, false);
        tool.SetAllowMultiple(true);
        tool.SetSeparateProcess(true);
        tool.SetTargetImages(true);
        tool.SetTargetVideos(false);
        tool.SetExtensions("jpg, png, webp");
        REQUIRE(tools->AddExternalTool(tool));

        std::vector<ExternalTool> list = tools->GetExternalTools();
        REQUIRE(list.size() == 1);
        REQUIRE(list[0].GetAllowMultiple() == true);
        REQUIRE(list[0].GetSeparateProcess() == true);
        REQUIRE(list[0].GetSupportsMultiple() == false);
        REQUIRE(list[0].GetTargetImages() == true);
        REQUIRE(list[0].GetTargetVideos() == false);
        REQUIRE(list[0].GetExtensions() == "jpg, png, webp");

        // Reload from preferences
        ExternalTools::Reset();
        ExternalToolsPtr reloaded = ExternalTools::GetInstance();
        std::vector<ExternalTool> reloadedList = reloaded->GetExternalTools();
        REQUIRE(reloadedList.size() == 1);
        REQUIRE(reloadedList[0].GetAllowMultiple() == true);
        REQUIRE(reloadedList[0].GetSeparateProcess() == true);
        REQUIRE(reloadedList[0].GetSupportsMultiple() == false);
        REQUIRE(reloadedList[0].GetTargetImages() == true);
        REQUIRE(reloadedList[0].GetTargetVideos() == false);
        REQUIRE(reloadedList[0].GetExtensions() == "jpg, png, webp");
    }

    SECTION("MatchesFile and ParseExtensions")
    {
        // ParseExtensions
        auto exts = ExternalTool::ParseExtensions(" .JPG, png; WEBP, .mp4 ");
        REQUIRE(exts.size() == 4);
        REQUIRE(exts[0] == "jpg");
        REQUIRE(exts[1] == "png");
        REQUIRE(exts[2] == "webp");
        REQUIRE(exts[3] == "mp4");

        // Tool: Images only, no extension filter
        ExternalTool imgOnly("ImagesOnly", "", "", "img %f", false, false, false);
        imgOnly.SetTargetImages(true);
        imgOnly.SetTargetVideos(false);
        CHECK(imgOnly.MatchesFile("/home/user/pic.jpg", false) == true);
        CHECK(imgOnly.MatchesFile("/home/user/movie.mp4", true) == false);

        // Tool: Videos only, extensions mp4, mkv
        ExternalTool vidOnly("VideosOnly", "", "", "vid %f", false, false, false);
        vidOnly.SetTargetImages(false);
        vidOnly.SetTargetVideos(true);
        vidOnly.SetExtensions("mp4, mkv");
        CHECK(vidOnly.MatchesFile("/home/user/movie.mp4", true) == true);
        CHECK(vidOnly.MatchesFile("/home/user/movie.MKV", true) == true);
        CHECK(vidOnly.MatchesFile("/home/user/movie.avi", true) == false);
        CHECK(vidOnly.MatchesFile("/home/user/pic.mp4", false) == false); // not video

        // Tool: Images and Videos, extension png
        ExternalTool pngOnly("PngOnly", "", "", "png %f", false, false, false);
        pngOnly.SetTargetImages(true);
        pngOnly.SetTargetVideos(true);
        pngOnly.SetExtensions(".png");
        CHECK(pngOnly.MatchesFile("/home/user/photo.PNG", false) == true);
        CHECK(pngOnly.MatchesFile("/home/user/photo.jpg", false) == false);
    }

    SECTION("ShortcutManager bidirectional synchronization")
    {
        ExternalTool tool("SyncedTool", "", "", "echo %f", false, false, false);
        REQUIRE(tools->AddExternalTool(tool));

        std::vector<ExternalTool> list = tools->GetExternalTools();
        REQUIRE(list.size() == 1);
        int id = list[0].GetID();
        std::string action_name = "ExternalTool_" + std::to_string(id);

        ShortcutManager &sm = ShortcutManager::GetInstance();
        sm.Init();
        sm.UpdateExternalToolActions(list);

        const ShortcutActionDef *def = sm.GetAction(action_name);
        REQUIRE(def != nullptr);
        REQUIRE(def->current_accels.empty());

        // Assign accelerator via ShortcutManager
        REQUIRE(sm.SetAccelerators(action_name, {"<Control><Alt>t"}));

        // Verify ExternalTools in-memory was updated
        const ExternalTool *updated_tool = tools->GetExternalTool(id);
        REQUIRE(updated_tool != nullptr);
        REQUIRE(updated_tool->GetShortcut() == "<Control><Alt>t");

        // Verify persistence to preferences
        ExternalTools::Reset();
        ExternalToolsPtr reloaded = ExternalTools::GetInstance();
        const ExternalTool *reloaded_tool = reloaded->GetExternalTool(id);
        REQUIRE(reloaded_tool != nullptr);
        REQUIRE(reloaded_tool->GetShortcut() == "<Control><Alt>t");

        // Remove accelerator via ShortcutManager
        REQUIRE(sm.RemoveAccelerator(action_name, "<Control><Alt>t"));
        const ExternalTool *cleared_tool = reloaded->GetExternalTool(id);
        REQUIRE(cleared_tool != nullptr);
        REQUIRE(cleared_tool->GetShortcut() == "");
    }
}

