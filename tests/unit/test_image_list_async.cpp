#include <catch2/catch_test_macros.hpp>
#include <glib/gstdio.h>

#include <algorithm>
#include <cstring>
#include <functional>
#include <list>
#include <string>

#include "ImageList.h"
#include "IImageListEventHandler.h"
#include "Browser.h"
#include "Statusbar.h"
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

    // Opening a different folder set whose files do not contain the previous
    // current URI resets the index to the first item (index 0).
    list->UpdateImageListAsync(&foldersB);
    pump_until([&] { return loaded_folder(uriB); });
    REQUIRE(list->GetSize() == 4);
    REQUIRE(list->GetCurrentIndex() == 0);

    // Opening yet another folder set with select-first (the bookmark-open
    // or folder-tree-click path) also lands on the first item.
    REQUIRE(list->SetCurrentIndex(2));
    REQUIRE(list->GetCurrentIndex() == 2);
    list->UpdateImageListAsync(&foldersC, false, true);
    pump_until([&] { return loaded_folder(uriC); });
    REQUIRE(list->GetSize() == 4);
    REQUIRE(list->GetCurrentIndex() == 0);
}

class TestProgressHandler : public IImageListEventHandler
{
public:
    int m_iProgressCount = 0;
    double m_dLastFraction = -2.0;

    virtual void HandleContentsChanged(ImageListEventPtr event) { (void)event; }
    virtual void HandleCurrentIndexChanged(ImageListEventPtr event) { (void)event; }
    virtual void HandleItemAdded(ImageListEventPtr event) { (void)event; }
    virtual void HandleItemRemoved(ImageListEventPtr event) { (void)event; }
    virtual void HandleItemChanged(ImageListEventPtr event) { (void)event; }
    virtual void HandleLoadProgress(double fraction, int current, int total)
    {
        (void)current;
        (void)total;
        m_iProgressCount++;
        m_dLastFraction = fraction;
    }
};

TEST_CASE("ImageList async folder load: filename sort skips eager Exif parsing and emits progress",
          "[unit][imagelist][gui]")
{
    REQUIRE_DISPLAY();

    std::string dir = make_temp_dir();
    seed_dir(dir, 4);
    std::string uri = path_to_uri(dir);

    ImageListPtr list(new ImageList());
    boost::shared_ptr<TestProgressHandler> handler(new TestProgressHandler());
    list->AddEventHandler(handler);

    std::list<std::string> folders = { uri };
    list->UpdateImageListAsync(&folders);

    pump_until([&] { return list->GetSize() >= 4; });
    REQUIRE(list->GetSize() == 4);

    // Verify eager Exif parsing was skipped: HasCachedTimeT() is false before GetTimeT is called
    REQUIRE_FALSE((*list)[0].HasCachedTimeT());

    // Verify progress events were emitted and completed at 1.0
    REQUIRE(handler->m_iProgressCount > 0);
    REQUIRE(handler->m_dLastFraction == 1.0);

    // When sorting by date, it queries dates and caches them
    list->Sort(ImageList::SORT_BY_DATE, true, true);
    pump_until([&] { return (*list)[0].HasCachedTimeT(); });
    REQUIRE((*list)[0].HasCachedTimeT());

    list->RemoveEventHandler(handler);
}

TEST_CASE("ImageList: StopAsyncLoad and destruction safety",
          "[unit][imagelist][gui]")
{
    REQUIRE_DISPLAY();

    std::string dir = make_temp_dir();
    seed_dir(dir, 10);
    std::string uri = path_to_uri(dir);

    std::list<std::string> folders = { uri };

    // Test StopAsyncLoad cleans up running thread without issue
    {
        ImageListPtr list(new ImageList());
        list->UpdateImageListAsync(&folders);
        list->StopAsyncLoad();
        list->StopAsyncSort();
    }

    // Test destruction while load is queued or running
    {
        ImageListPtr list(new ImageList());
        list->UpdateImageListAsync(&folders);
        // List destructor runs immediately on scope exit
    }
}

TEST_CASE("Browser loading overlay and statusbar HUD integration",
          "[unit][browser][gui]")
{
    REQUIRE_DISPLAY();

    BrowserPtr browser(new Browser());
    StatusbarPtr statusbar(new Statusbar());
    browser->SetStatusbar(statusbar);

    // Show loading progress
    browser->ShowLoadingProgress("Loading test folder...", 0.5);

    // Hide loading progress
    browser->HideLoadingProgress();
}