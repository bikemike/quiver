#include <catch2/catch_test_macros.hpp>
#include "ExternalTools.h"
#include "Preferences.h"
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
}
