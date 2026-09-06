#include <catch2/catch_test_macros.hpp>
#include "Bookmarks.h"
#include "Preferences.h"
#include <glib.h>
#include <glib/gstdio.h>
#include <cstring>
#include <vector>
#include <list>

struct TestBookmarksFixture {
    char m_tmpPath[256];

    TestBookmarksFixture() {
        strncpy(m_tmpPath, "/tmp/quiver_test_bm_XXXXXX.ini", sizeof(m_tmpPath) - 1);
        int fd = g_mkstemp(m_tmpPath);
        if (fd >= 0) {
            close(fd);
        }
        strncpy(g_szConfigFilePath, m_tmpPath, sizeof(m_tmpPath) - 1);
        g_szConfigFilePath[sizeof(m_tmpPath) - 1] = '\0';
        Bookmarks::Reset();
        Preferences::Reset();
    }

    ~TestBookmarksFixture() {
        Bookmarks::Reset();
        Preferences::Reset();
        g_unlink(m_tmpPath);
    }
};

TEST_CASE_METHOD(TestBookmarksFixture, "Bookmarks CRUD Operations", "[unit][bookmarks][fast]")
{
    BookmarksPtr bm = Bookmarks::GetInstance();
    REQUIRE(bm != nullptr);

    SECTION("Add and retrieve bookmark")
    {
        Bookmark b("Wallpapers", "Desktop wallpapers", "folder", {"file:///home/user/wallpapers"}, false);
        REQUIRE(bm->AddBookmark(b));

        std::vector<Bookmark> list = bm->GetBookmarks();
        REQUIRE(list.size() == 1);
        REQUIRE(list[0].GetName() == "Wallpapers");
        REQUIRE(list[0].GetDescription() == "Desktop wallpapers");
        REQUIRE(list[0].GetURIs() == std::list<std::string>{"file:///home/user/wallpapers"});

        int id = list[0].GetID();
        const Bookmark* found = bm->GetBookmark(id);
        REQUIRE(found != nullptr);
        REQUIRE(found->GetName() == "Wallpapers");
    }

    SECTION("Update bookmark")
    {
        Bookmark b("Photos", "Trip photos", "camera", {"file:///home/user/photos"}, true);
        REQUIRE(bm->AddBookmark(b));

        std::vector<Bookmark> list = bm->GetBookmarks();
        REQUIRE(list.size() == 1);
        int id = list[0].GetID();

        Bookmark updated = list[0];
        updated.SetName("Summer Photos");
        updated.SetDescription("Updated description");
        REQUIRE(bm->UpdateBookmark(updated));

        const Bookmark* retrieved = bm->GetBookmark(id);
        REQUIRE(retrieved != nullptr);
        REQUIRE(retrieved->GetName() == "Summer Photos");
        REQUIRE(retrieved->GetDescription() == "Updated description");
    }

    SECTION("Remove bookmark")
    {
        Bookmark b("Temp", "To delete", "trash", {"file:///tmp"}, false);
        bm->AddBookmark(b);
        std::vector<Bookmark> list = bm->GetBookmarks();
        REQUIRE(list.size() == 1);

        int id = list[0].GetID();
        REQUIRE(bm->Remove(id));
        REQUIRE(bm->GetBookmarks().empty());
        REQUIRE(bm->GetBookmark(id) == nullptr);
    }

    SECTION("MoveUp and MoveDown ordering")
    {
        Bookmark b1("First", "", "", {"file:///1"}, false);
        Bookmark b2("Second", "", "", {"file:///2"}, false);
        Bookmark b3("Third", "", "", {"file:///3"}, false);

        bm->AddBookmark(b1);
        bm->AddBookmark(b2);
        bm->AddBookmark(b3);

        std::vector<Bookmark> list = bm->GetBookmarks();
        REQUIRE(list.size() == 3);
        int id1 = list[0].GetID();
        int id2 = list[1].GetID();
        int id3 = list[2].GetID();

        // Move second item up
        REQUIRE(bm->MoveUp(id2));
        std::vector<Bookmark> reordered = bm->GetBookmarks();
        REQUIRE(reordered[0].GetID() == id2);
        REQUIRE(reordered[1].GetID() == id1);
        REQUIRE(reordered[2].GetID() == id3);

        // Move top item down
        REQUIRE(bm->MoveDown(id2));
        reordered = bm->GetBookmarks();
        REQUIRE(reordered[0].GetID() == id1);
        REQUIRE(reordered[1].GetID() == id2);
    }
}
