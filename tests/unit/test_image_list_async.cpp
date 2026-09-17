#include <catch2/catch_test_macros.hpp>
#include <glib/gstdio.h>

#include <algorithm>
#include <cstring>
#include <functional>
#include <list>
#include <string>

#include "ImageList.h"
#include "test_helpers.h"

// Fills a directory with a small set of sample image files so the async
// loader has something (and only something) to enumerate.
static void seed_dir(const std::string& dir, int count)
{
    std::string src = QuiverTest_GetImagesDir() + "/sample_4k.jpg";
    gchar* data = nullptr;
    gsize len = 0;
    REQUIRE(g_file_get_contents(src.c_str(), &data, &len, nullptr));
    for (int i = 0; i < count; i++)
    {
        std::string dest = dir + "/img" + std::to_string(i) + ".jpg";
        REQUIRE(g_file_set_contents(dest.c_str(), data, (gssize)len, nullptr));
    }
    g_free(data);
}

static std::string make_temp_dir()
{
    char tpl[] = "/tmp/quiver_imlist_asynctest_XXXXXX";
    char* dir = g_mkdtemp(tpl);
    REQUIRE(dir != nullptr);
    return std::string(dir);
}

static std::string path_to_uri(const std::string& path)
{
    gchar* uri = g_filename_to_uri(path.c_str(), nullptr, nullptr);
    REQUIRE(uri != nullptr);
    std::string str(uri);
    g_free(uri);
    return str;
}

// Runs the main loop until the predicate becomes true (or a timeout passes),
// which lets the async load's worker thread finish and its g_idle commit run.
static void pump_until(std::function<bool()> done, guint timeout_ms = 10000)
{
    gint64 deadline = g_get_monotonic_time() + timeout_ms * G_TIME_SPAN_MILLISECOND;
    while (g_get_monotonic_time() < deadline && !done())
    {
        g_main_context_iteration(nullptr, TRUE);
    }
}

TEST_CASE("ImageList async folder load: selectFirstItem lands on the first item",
          "[unit][imagelist][gui]")
{
    REQUIRE_DISPLAY();

    std::string dirA = make_temp_dir();
    std::string dirB = make_temp_dir();
    std::string dirC = make_temp_dir();
    seed_dir(dirA, 4);
    seed_dir(dirB, 4);
    seed_dir(dirC, 4);

    std::string uriA = path_to_uri(dirA);
    std::string uriB = path_to_uri(dirB);
    std::string uriC = path_to_uri(dirC);

    ImageListPtr list(new ImageList());

    std::list<std::string> foldersA = { uriA };
    std::list<std::string> foldersB = { uriB };
    std::list<std::string> foldersC = { uriC };

    auto loaded_folder = [&](const std::string& uri) {
        std::list<std::string> folders = list->GetFolderList();
        return std::find(folders.begin(), folders.end(), uri) != folders.end() &&
               list->GetSize() >= 4;
    };

    // Initial load; index lands somewhere in the middle.
    list->UpdateImageListAsync(&foldersA);
    pump_until([&] { return loaded_folder(uriA); });
    REQUIRE(list->GetSize() == 4);
    REQUIRE(list->SetCurrentIndex(2));
    REQUIRE(list->GetCurrentIndex() == 2);

    // Opening a different folder set without select-first keeps the stale
    // index (the reported bug scenario: a bookmark's contents replacing an
    // unrelated view keeps the old position as long as it is still in bounds).
    list->UpdateImageListAsync(&foldersB);
    pump_until([&] { return loaded_folder(uriB); });
    REQUIRE(list->GetSize() == 4);
    REQUIRE(list->GetCurrentIndex() == 2);

    // Opening yet another folder set with select-first (the bookmark-open
    // path) must land on the first item, matching the bookmark menu.
    list->UpdateImageListAsync(&foldersC, false, true);
    pump_until([&] { return loaded_folder(uriC); });
    REQUIRE(list->GetSize() == 4);
    REQUIRE(list->GetCurrentIndex() == 0);
}