#include <catch2/catch_test_macros.hpp>
#include "ImageListFilter.h"
#include "IImageListView.h"
#include "QuiverFile.h"
#include <vector>
#include <string>

class MockImageListView : public virtual IImageListView
{
public:
    std::vector<QuiverFile> m_files;
    unsigned int m_currentIndex = 0;

    MockImageListView(const std::vector<std::string>& uris)
    {
        for (const auto& u : uris)
        {
            m_files.push_back(QuiverFile(u.c_str()));
        }
    }

    unsigned int GetSize() const override { return m_files.size(); }
    unsigned int GetCurrentIndex() const override { return m_currentIndex; }

    bool HasNext() const override { return !m_files.empty() && m_currentIndex + 1 < m_files.size(); }
    bool HasPrevious() const override { return !m_files.empty() && m_currentIndex > 0; }

    bool Next() override {
        if (HasNext()) {
            return SetCurrentIndex(m_currentIndex + 1);
        }
        return false;
    }

    bool Previous() override {
        if (HasPrevious()) {
            return SetCurrentIndex(m_currentIndex - 1);
        }
        return false;
    }

    bool First() override {
        if (!m_files.empty()) {
            return SetCurrentIndex(0);
        }
        return false;
    }

    bool Last() override {
        if (!m_files.empty()) {
            return SetCurrentIndex(m_files.size() - 1);
        }
        return false;
    }

    bool SetCurrentIndex(unsigned int iIndex) override {
        if (iIndex < m_files.size()) {
            unsigned int old = m_currentIndex;
            m_currentIndex = iIndex;
            if (old != m_currentIndex) {
                EmitCurrentIndexChangedEvent(m_currentIndex, old);
            }
            return true;
        }
        return false;
    }

    QuiverFile GetCurrent() const override {
        return m_currentIndex < m_files.size() ? m_files[m_currentIndex] : QuiverFile();
    }
    QuiverFile GetNext() const override {
        return (m_currentIndex + 1 < m_files.size()) ? m_files[m_currentIndex + 1] : QuiverFile();
    }
    QuiverFile GetPrevious() const override {
        return (m_currentIndex > 0) ? m_files[m_currentIndex - 1] : QuiverFile();
    }
    QuiverFile GetFirst() const override {
        return !m_files.empty() ? m_files.front() : QuiverFile();
    }
    QuiverFile GetLast() const override {
        return !m_files.empty() ? m_files.back() : QuiverFile();
    }
    QuiverFile Get(unsigned int nIndex) const override {
        return nIndex < m_files.size() ? m_files[nIndex] : QuiverFile();
    }

    void Remove(unsigned int nIndex) override {
        if (nIndex < m_files.size()) {
            m_files.erase(m_files.begin() + nIndex);
            if (m_currentIndex >= m_files.size() && !m_files.empty())
                m_currentIndex = m_files.size() - 1;
        }
    }

    bool m_bIsLoading = false;
    bool IsLoading() const override { return m_bIsLoading; }
};

TEST_CASE("ImageListFilter Predicate and Navigation", "[unit][filter][fast]")
{
    std::vector<std::string> uris = {
        "file:///test/a.jpg",
        "file:///test/b.png",
        "file:///test/c.jpg",
        "file:///test/d.gif",
        "file:///test/e.jpg"
    };

    boost::shared_ptr<MockImageListView> mock(new MockImageListView(uris));

    // Predicate filters only .jpg files
    boost::shared_ptr<ImageListFilter> filterPtr(new ImageListFilter(mock, [](const QuiverFile& qf) {
        std::string uri = qf.GetURI() ? qf.GetURI() : "";
        return uri.find(".jpg") != std::string::npos;
    }));
    ImageListFilter& filter = *filterPtr;

    SECTION("Filtered size and elements")
    {
        // Out of 5 items, exactly 3 are .jpg (a, c, e)
        REQUIRE(filter.GetSize() == 3);
        REQUIRE(std::string(filter.Get(0).GetURI()) == "file:///test/a.jpg");
        REQUIRE(std::string(filter.Get(1).GetURI()) == "file:///test/c.jpg");
        REQUIRE(std::string(filter.Get(2).GetURI()) == "file:///test/e.jpg");
    }

    SECTION("Filtered navigation forward and backward")
    {
        REQUIRE(filter.First());
        REQUIRE(std::string(filter.GetCurrent().GetURI()) == "file:///test/a.jpg");
        REQUIRE(filter.HasNext());
        REQUIRE_FALSE(filter.HasPrevious());

        REQUIRE(filter.Next());
        REQUIRE(std::string(filter.GetCurrent().GetURI()) == "file:///test/c.jpg");
        REQUIRE(filter.HasNext());
        REQUIRE(filter.HasPrevious());

        REQUIRE(filter.Next());
        REQUIRE(std::string(filter.GetCurrent().GetURI()) == "file:///test/e.jpg");
        REQUIRE_FALSE(filter.HasNext());
        REQUIRE(filter.HasPrevious());

        // Can go back to c.jpg
        REQUIRE(filter.Previous());
        REQUIRE(std::string(filter.GetCurrent().GetURI()) == "file:///test/c.jpg");
    }

    SECTION("Filter that matches nothing")
    {
        boost::shared_ptr<ImageListFilter> noMatch(new ImageListFilter(mock, [](const QuiverFile&) {
            return false;
        }));

        REQUIRE(noMatch->GetSize() == 0);
        REQUIRE_FALSE(noMatch->HasNext());
        REQUIRE_FALSE(noMatch->HasPrevious());
    }

    SECTION("Filter that matches everything")
    {
        boost::shared_ptr<ImageListFilter> matchAll(new ImageListFilter(mock, [](const QuiverFile&) {
            return true;
        }));

        REQUIRE(matchAll->GetSize() == 5);
    }

    SECTION("Contents changed resets view index to source current index")
    {
        REQUIRE(filter.SetCurrentIndex(2));
        REQUIRE(filter.GetCurrentIndex() == 2);
        REQUIRE(std::string(filter.GetCurrent().GetURI()) == "file:///test/e.jpg");

        // Simulate folder switch: replace mock's files with an entirely new set
        mock->m_files.clear();
        mock->m_files.push_back(QuiverFile("file:///other/x.jpg"));
        mock->m_files.push_back(QuiverFile("file:///other/y.jpg"));
        mock->m_files.push_back(QuiverFile("file:///other/z.jpg"));
        mock->m_currentIndex = 0;
        mock->EmitContentsChangedEvent();

        REQUIRE(filter.GetSize() == 3);
        REQUIRE(filter.GetCurrentIndex() == 0);
        REQUIRE(std::string(filter.GetCurrent().GetURI()) == "file:///other/x.jpg");
    }

    SECTION("ImageListFilter forwards IsLoading from source")
    {
        mock->m_bIsLoading = false;
        REQUIRE_FALSE(filter.IsLoading());

        mock->m_bIsLoading = true;
        REQUIRE(filter.IsLoading());

        mock->m_bIsLoading = false;
        REQUIRE_FALSE(filter.IsLoading());
    }

    SECTION("ImageListFilter forwards LoadProgress to listeners")
    {
        class TestProgressHandler : public IImageListEventHandler
        {
        public:
            std::vector<std::tuple<double, int, int>> events;
            void HandleContentsChanged(ImageListEventPtr) override {}
            void HandleCurrentIndexChanged(ImageListEventPtr) override {}
            void HandleItemAdded(ImageListEventPtr) override {}
            void HandleItemRemoved(ImageListEventPtr) override {}
            void HandleItemChanged(ImageListEventPtr) override {}
            void HandleLoadProgress(double fraction, int current, int total) override
            {
                events.push_back(std::make_tuple(fraction, current, total));
            }
        };

        boost::shared_ptr<TestProgressHandler> handler(new TestProgressHandler());
        filter.AddEventHandler(handler);

        // Source emits indeterminate progress
        mock->EmitLoadProgress(-1.0, 50, 0);
        // Source emits determinate progress
        mock->EmitLoadProgress(0.5, 50, 100);
        // Source emits load complete
        mock->EmitLoadProgress(1.0, 100, 100);

        REQUIRE(handler->events.size() == 3);
        REQUIRE(std::get<0>(handler->events[0]) == -1.0);
        REQUIRE(std::get<1>(handler->events[0]) == 50);

        REQUIRE(std::get<0>(handler->events[1]) == 0.5);
        REQUIRE(std::get<1>(handler->events[1]) == 50);
        REQUIRE(std::get<2>(handler->events[1]) == 100);

        REQUIRE(std::get<0>(handler->events[2]) == 1.0);

        filter.RemoveEventHandler(handler);
    }

    SECTION("All items filtered out produces empty view")
    {
        boost::shared_ptr<MockImageListView> all_dirs_mock(
            new MockImageListView({"file:///tmp/dir1", "file:///tmp/dir2"}));
        ImageListFilter filter_dirs(all_dirs_mock, [](const QuiverFile&) { return false; });
        REQUIRE(filter_dirs.GetSize() == 0);
        QuiverFile cur = filter_dirs.GetCurrent();
        REQUIRE(cur.GetURI() == nullptr);
    }

    SECTION("Stale map when source shrinks without event does not crash in ResolveViewIndex")
    {
        boost::shared_ptr<MockImageListView> shrink_mock(
            new MockImageListView({"file:///test/1.jpg", "file:///test/2.jpg"}));
        ImageListFilter filter_shrink(shrink_mock, [](const QuiverFile&) { return true; });

        REQUIRE(filter_shrink.GetSize() == 2);
        REQUIRE(std::string(filter_shrink.GetCurrent().GetURI()) == "file:///test/1.jpg");

        // Clear mock files directly without emitting event to simulate out-of-order event dispatch
        shrink_mock->m_files.clear();

        // GetCurrent() should not crash (previously segfaulted on strlen(NULL))
        QuiverFile cur = filter_shrink.GetCurrent();
        REQUIRE(cur.GetURI() == nullptr);
        REQUIRE(filter_shrink.GetSize() == 0);
    }

    SECTION("Navigation emits CurrentIndexChangedEvent and maintains GetCurrent consistency")
    {
        class TestNavHandler : public IImageListEventHandler
        {
        public:
            ImageListFilter* m_pFilter;
            std::vector<std::pair<unsigned int, unsigned int>> index_changes;
            std::vector<std::string> current_uris_at_event;

            TestNavHandler(ImageListFilter* f) : m_pFilter(f) {}

            void HandleContentsChanged(ImageListEventPtr) override {}
            void HandleCurrentIndexChanged(ImageListEventPtr event) override
            {
                index_changes.push_back({event->GetIndex(), event->GetOldIndex()});
                QuiverFile cur = m_pFilter->GetCurrent();
                current_uris_at_event.push_back(cur.GetURI() ? cur.GetURI() : "");
            }
            void HandleItemAdded(ImageListEventPtr) override {}
            void HandleItemRemoved(ImageListEventPtr) override {}
            void HandleItemChanged(ImageListEventPtr) override {}
            void HandleLoadProgress(double, int, int) override {}
        };

        boost::shared_ptr<TestNavHandler> navHandler(new TestNavHandler(&filter));
        filter.AddEventHandler(navHandler);

        // Reset to first
        REQUIRE(filter.First());
        navHandler->index_changes.clear();
        navHandler->current_uris_at_event.clear();

        // 1. Next() moves from index 0 (a.jpg) to 1 (c.jpg)
        REQUIRE(filter.Next());
        REQUIRE(navHandler->index_changes.size() == 1);
        REQUIRE(navHandler->index_changes[0].first == 1);
        REQUIRE(navHandler->index_changes[0].second == 0);
        // CRUCIAL: GetCurrent() inside handler must report the new item c.jpg
        REQUIRE(navHandler->current_uris_at_event[0] == "file:///test/c.jpg");
        REQUIRE(std::string(filter.GetCurrent().GetURI()) == "file:///test/c.jpg");

        // 2. Next() moves from index 1 (c.jpg) to 2 (e.jpg)
        REQUIRE(filter.Next());
        REQUIRE(navHandler->index_changes.size() == 2);
        REQUIRE(navHandler->index_changes[1].first == 2);
        REQUIRE(navHandler->index_changes[1].second == 1);
        REQUIRE(navHandler->current_uris_at_event[1] == "file:///test/e.jpg");

        // 3. Next() at end fails and does not emit spurious event
        REQUIRE_FALSE(filter.Next());
        REQUIRE(navHandler->index_changes.size() == 2);

        // 4. Previous() moves from index 2 (e.jpg) back to 1 (c.jpg)
        REQUIRE(filter.Previous());
        REQUIRE(navHandler->index_changes.size() == 3);
        REQUIRE(navHandler->index_changes[2].first == 1);
        REQUIRE(navHandler->index_changes[2].second == 2);
        REQUIRE(navHandler->current_uris_at_event[2] == "file:///test/c.jpg");

        // 5. First() moves from index 1 back to 0 (a.jpg)
        REQUIRE(filter.First());
        REQUIRE(navHandler->index_changes.size() == 4);
        REQUIRE(navHandler->index_changes[3].first == 0);
        REQUIRE(navHandler->index_changes[3].second == 1);
        REQUIRE(navHandler->current_uris_at_event[3] == "file:///test/a.jpg");

        // 6. Previous() at start fails and does not emit spurious event
        REQUIRE_FALSE(filter.Previous());
        REQUIRE(navHandler->index_changes.size() == 4);

        // 7. Last() moves from index 0 directly to 2 (e.jpg)
        REQUIRE(filter.Last());
        REQUIRE(navHandler->index_changes.size() == 5);
        REQUIRE(navHandler->index_changes[4].first == 2);
        REQUIRE(navHandler->index_changes[4].second == 0);
        REQUIRE(navHandler->current_uris_at_event[4] == "file:///test/e.jpg");

        filter.RemoveEventHandler(navHandler);
    }

    SECTION("Source moving to filtered-out item does not emit filter index change")
    {
        class TestNavHandler : public IImageListEventHandler
        {
        public:
            int change_count = 0;
            void HandleContentsChanged(ImageListEventPtr) override {}
            void HandleCurrentIndexChanged(ImageListEventPtr) override { change_count++; }
            void HandleItemAdded(ImageListEventPtr) override {}
            void HandleItemRemoved(ImageListEventPtr) override {}
            void HandleItemChanged(ImageListEventPtr) override {}
            void HandleLoadProgress(double, int, int) override {}
        };

        boost::shared_ptr<TestNavHandler> navHandler(new TestNavHandler());
        filter.AddEventHandler(navHandler);

        REQUIRE(filter.First());
        navHandler->change_count = 0;

        // Source item 1 is "file:///test/b.png", which does not match .jpg predicate
        mock->SetCurrentIndex(1);
        REQUIRE(navHandler->change_count == 0);
        REQUIRE(std::string(filter.GetCurrent().GetURI()) == "file:///test/a.jpg");

        // Source item 2 is "file:///test/c.jpg", which matches .jpg predicate
        mock->SetCurrentIndex(2);
        REQUIRE(navHandler->change_count == 1);
        REQUIRE(std::string(filter.GetCurrent().GetURI()) == "file:///test/c.jpg");

        filter.RemoveEventHandler(navHandler);
    }

    SECTION("Empty filter First and Last return false safely")
    {
        boost::shared_ptr<MockImageListView> empty_mock(new MockImageListView({}));
        ImageListFilter empty_filter(empty_mock, [](const QuiverFile&) { return true; });
        REQUIRE(empty_filter.GetSize() == 0);
        REQUIRE_FALSE(empty_filter.First());
        REQUIRE_FALSE(empty_filter.Last());
        REQUIRE_FALSE(empty_filter.Next());
        REQUIRE_FALSE(empty_filter.Previous());
    }
}

