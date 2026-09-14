#include <catch2/catch_test_macros.hpp>
#include <glib/gstdio.h>

#include <list>
#include <string>
#include <vector>

#include "ImageList.h"
#include "ImageListFilter.h"

// Regression test for the delete crash: ImageList::Remove used to drop an
// entry without emitting an ItemRemovedEvent, leaving an ImageListFilter's
// lazy view->source map stale.  The next ResolveViewIndex() then queried the
// freed source slot and tripped ImageList::Get()'s bounds assert, crashing the
// viewer when the user pressed Delete.
//
// Now Remove() emits the removal (with the pre-removal index) and the filter
// re-anchors before anyone re-reads the list.

static std::string make_temp_dir()
{
    char tpl[] = "/tmp/quiver_imlist_regev_XXXXXX";
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

static void seed_sample(const std::string& dir, const std::string& name,
                        const std::string& img)
{
    std::string dest = dir + "/" + name;
    REQUIRE(g_file_set_contents(dest.c_str(), img.c_str(), (gssize)img.size(), nullptr));
}

// tiny valid PNG so g_file_query_info sees a REGULAR file
static const char kPng[] = {
    (char)0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a, 0, 0, 0, 0
};

TEST_CASE("ImageList Remove emits ItemRemoved and the filter stays consistent",
          "[unit][imagelist][filter][regression]")
{
    std::string dirA = make_temp_dir();
    std::string dirB = make_temp_dir();

    // Mixed bag: only *.jpg survive the predicate.
    seed_sample(dirA, "a.jpg", std::string(kPng, sizeof(kPng)));
    seed_sample(dirA, "b.png", "x");
    seed_sample(dirA, "c.jpg", "x");
    seed_sample(dirA, "d.png", "x");
    seed_sample(dirA, "e.jpg", std::string(kPng, sizeof(kPng)));

    ImageListPtr list(new ImageList());

    // Add individual files so source order matches the list order exactly.
    std::list<std::string> files = {
        path_to_uri(dirA + "/a.jpg"),
        path_to_uri(dirA + "/b.png"),
        path_to_uri(dirA + "/c.jpg"),
        path_to_uri(dirA + "/d.png"),
        path_to_uri(dirA + "/e.jpg"),
    };
    list->Add(&files);
    REQUIRE(list->GetSize() == 5);

    boost::shared_ptr<ImageListFilter> filter(new ImageListFilter(list, [](const QuiverFile& qf) {
        std::string uri = qf.GetURI() ? qf.GetURI() : "";
        return uri.find(".jpg") != std::string::npos;
    }));

    SECTION("removing a mid-list item keeps indexes aligned")
    {
        REQUIRE(filter->GetSize() == 3);
        REQUIRE(std::string(filter->Get(0).GetURI()).find("a.jpg") != std::string::npos);
        REQUIRE(std::string(filter->Get(1).GetURI()).find("c.jpg") != std::string::npos);
        REQUIRE(filter->Get(2).GetURI());

        // Remove source slot 2 = "c.jpg" (view slot 1)
        list->Remove(2);

        REQUIRE(filter->GetSize() == 2);
        REQUIRE(std::string(filter->Get(0).GetURI()).find("a.jpg") != std::string::npos);
        REQUIRE(filter->Get(1).GetURI());

        // The viewer path that used to crash: resolve the current view index
        // through the (now repaired) map and fetch the item.
        (void)filter->GetCurrentIndex();
        REQUIRE(filter->GetCurrent().GetURI() != nullptr);
    }

    SECTION("removing the current item re-anchors without an assert")
    {
        REQUIRE(filter->GetSize() == 3);
        filter->SetCurrentIndex(2);          // view slot of "e.jpg" (source 4)
        REQUIRE(std::string(filter->GetCurrent().GetURI()).find("e.jpg") != std::string::npos);

        list->Remove(4);                     // delete "e.jpg" from the source

        REQUIRE(filter->GetSize() == 2);
        REQUIRE(filter->GetCurrentIndex() < filter->GetSize());
        REQUIRE(filter->GetCurrent().GetURI() != nullptr);
    }

    SECTION("removing everything leaves an empty, usable view")
    {
        filter->SetCurrentIndex(filter->GetSize() - 1);
        list->Remove(0);                     // a.jpg
        list->Remove(1);                     // c.jpg (source shifted by one)
        list->Remove(2);                     // e.jpg (source shifted by two)
        REQUIRE(filter->GetSize() == 0);
        REQUIRE(filter->GetCurrentIndex() == 0);
        REQUIRE_FALSE(filter->HasNext());
        REQUIRE_FALSE(filter->HasPrevious());
    }

    g_remove((dirA + "/a.jpg").c_str());
    g_remove((dirA + "/b.png").c_str());
    g_remove((dirA + "/c.jpg").c_str());
    g_remove((dirA + "/d.png").c_str());
    g_remove((dirA + "/e.jpg").c_str());
    g_rmdir(dirA.c_str());
    g_rmdir(dirB.c_str());
}