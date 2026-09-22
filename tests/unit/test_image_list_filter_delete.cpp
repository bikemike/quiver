#include <catch2/catch_test_macros.hpp>
#include "ImageListFilter.h"
#include "IImageListView.h"
#include "ImageListEvent.h"
#include "QuiverFile.h"
#include <atomic>
#include <thread>
#include <vector>
#include <string>

// The viewer rows on an ImageListFilter sitting on top of a real source
// ImageList.  Pressing Delete executes:
//
//     m_ImageListPtr->Remove(m_ImageListPtr->GetCurrentIndex());
//     m_ImageListPtr->SetCurrentIndex(m_ImageListPtr->GetCurrentIndex(), true-ish);
//
// where m_ImageListPtr is the FILTER.  ImageListFilter::Remove forwards the
// removal to the source, and the source emits ItemRemovedEvent synchronously.
// The filter is itself a registered handler on that source, so its own
// HandleItemRemoved runs re-entrantly inside ImageListFilter::Remove.  It
// re-emits (view-index space) and the viewer event handler then immediately
// calls GetCurrentIndex() - which resolves through the (now dirty) map.
//
// These tests model that exact nesting so regressions in the re-entrant
// resolve/rebuild path are caught.

namespace
{

// Event-aware list stub: mirrors ImageList::Remove's emit ordering.
class EmittingMockList : public virtual IImageListView
{
public:
    std::vector<QuiverFile> m_files;
    unsigned int m_currentIndex = 0;

    EmittingMockList(const std::vector<std::string>& uris)
    {
        for (const auto& u : uris)
            m_files.push_back(QuiverFile(u.c_str()));
    }

    unsigned int GetSize() const override { return (unsigned int)m_files.size(); }
    unsigned int GetCurrentIndex() const override { return m_currentIndex; }
    bool HasNext() const override { return !m_files.empty() && m_currentIndex + 1 < m_files.size(); }
    bool HasPrevious() const override { return !m_files.empty() && m_currentIndex > 0; }
    bool Next() override { if (HasNext()) { ++m_currentIndex; return true; } return false; }
    bool Previous() override { if (HasPrevious()) { --m_currentIndex; return true; } return false; }
    bool First() override { if (!m_files.empty()) { m_currentIndex = 0; return true; } return false; }
    bool Last() override { if (!m_files.empty()) { m_currentIndex = (unsigned int)m_files.size() - 1; return true; } return false; }
    bool SetCurrentIndex(unsigned int iIndex) override
    {
        if (iIndex < m_files.size()) { m_currentIndex = iIndex; return true; }
        return false;
    }
    QuiverFile GetCurrent() const override
    { return m_currentIndex < m_files.size() ? m_files[m_currentIndex] : QuiverFile(); }
    QuiverFile GetNext() const override
    { return (m_currentIndex + 1 < m_files.size()) ? m_files[m_currentIndex + 1] : QuiverFile(); }
    QuiverFile GetPrevious() const override
    { return (m_currentIndex > 0) ? m_files[m_currentIndex - 1] : QuiverFile(); }
    QuiverFile GetFirst() const override { return !m_files.empty() ? m_files.front() : QuiverFile(); }
    QuiverFile GetLast() const override { return !m_files.empty() ? m_files.back() : QuiverFile(); }
    QuiverFile Get(unsigned int nIndex) const override
    { return nIndex < m_files.size() ? m_files[nIndex] : QuiverFile(); }

    void Remove(unsigned int nIndex) override
    {
        unsigned int iOldIndex = m_currentIndex;
        if (nIndex < m_files.size())
        {
            m_files.erase(m_files.begin() + nIndex);
            if (m_currentIndex > nIndex)
                --m_currentIndex;
            else if (m_currentIndex == nIndex)
                m_currentIndex = (m_files.empty()) ? 0
                    : (m_currentIndex < m_files.size() ? m_currentIndex : (unsigned int)m_files.size() - 1);
            EmitItemRemovedEvent(nIndex);
        }
        if (iOldIndex != m_currentIndex)
            EmitCurrentIndexChangedEvent(m_currentIndex, iOldIndex);
    }
};

// Stands in for Viewer::ImageListEventHandler: any removal that reaches the
// consumer triggers an immediate re-read of the view (GetCurrentIndex /
// GetCurrent), exactly like Viewer.cpp:8544.
class ViewerLikeHandler : public IImageListEventHandler
{
public:
    unsigned int m_removedCount = 0;

    void HandleContentsChanged(ImageListEventPtr event) override { (void)event; }
    void HandleCurrentIndexChanged(ImageListEventPtr event) override
    {
        (void)event;
        IImageListViewPtr v = m_view.lock();
        if (v)
        {
            (void)v->GetCurrentIndex();
            (void)v->GetCurrent();
        }
    }
    void HandleItemAdded(ImageListEventPtr event) override { (void)event; }
    void HandleItemRemoved(ImageListEventPtr event) override
    {
        (void)event;
        ++m_removedCount;
        IImageListViewPtr v = m_view.lock();
        if (v)
        {
            // Delete handler: Remove + immediately resolve current index.
            (void)v->GetCurrentIndex();
            (void)v->GetCurrent();
        }
    }
    void HandleItemChanged(ImageListEventPtr event) override { (void)event; }

    void SetView(const IImageListViewPtr& v) { m_view = v; }

private:
    boost::weak_ptr<IImageListView> m_view;
};

} // namespace

TEST_CASE("ImageListFilter delete-from-filter keeps the map consistent",
          "[unit][filter][delete][regression]")
{
    // Enough items that the first GetCurrentIndex() must build the map and a
    // later rebuild has to reallocate it from a dirty state.
    std::vector<std::string> uris;
    for (int i = 0; i < 600; i++)
        uris.push_back(std::string("file:///test/img") + std::to_string(i) + ".jpg");

    boost::shared_ptr<EmittingMockList> src(new EmittingMockList(uris));
    boost::shared_ptr<ImageListFilter> filter(new ImageListFilter(src, [](const QuiverFile& qf) {
        std::string uri = qf.GetURI() ? qf.GetURI() : "";
        return uri.find(".jpg") != std::string::npos;
    }));

    boost::shared_ptr<ViewerLikeHandler> viewer(new ViewerLikeHandler());
    viewer->SetView(filter);
    filter->AddEventHandler(viewer);

    REQUIRE(filter->GetSize() == 600);

    // The viewer Delete action path, repeated while moving through the list.
    for (unsigned int round = 0; round < 200 && filter->GetSize() > 0; round++)
    {
        unsigned int cur = filter->GetCurrentIndex();
        filter->Remove(cur);
        filter->SetCurrentIndex(filter->GetCurrentIndex());
    }

    REQUIRE(viewer->m_removedCount > 0);
    CHECK(filter->GetSize() + 200 <= 600);
    // structurally consistent
    for (unsigned int i = 0; i < filter->GetSize(); i++)
        REQUIRE(filter->Get(i).GetURI() != nullptr);
}

TEST_CASE("ImageListFilter delete of current through the filter is safe",
          "[unit][filter][delete][fast][regression]")
{
    std::vector<std::string> uris;
    for (int i = 0; i < 20; i++)
        uris.push_back(std::string("file:///test/a") + std::to_string(i) + ".jpg");

    boost::shared_ptr<EmittingMockList> src(new EmittingMockList(uris));
    boost::shared_ptr<ImageListFilter> filter(new ImageListFilter(src, [](const QuiverFile& qf) {
        std::string uri = qf.GetURI() ? qf.GetURI() : "";
        return uri.find(".jpg") != std::string::npos;
    }));

    boost::shared_ptr<ViewerLikeHandler> viewer(new ViewerLikeHandler());
    viewer->SetView(filter);
    filter->AddEventHandler(viewer);

    // current = 10
    REQUIRE(filter->SetCurrentIndex(10));
    REQUIRE(filter->GetCurrentIndex() == 10);

    filter->Remove(filter->GetCurrentIndex());
    CHECK(filter->GetSize() == 19);
    REQUIRE(filter->GetCurrentIndex() < filter->GetSize());
    REQUIRE(filter->GetCurrent().GetURI() != nullptr);
}

TEST_CASE("ImageListFilter is safe under concurrent worker GetSize + main delete",
          "[unit][filter][delete][threaded][regression]")
{
    // Viewer.cpp:8762: thumbnail loader worker threads call
    // m_ImageListPtr->GetSize() (a rebuild on the filter) while the main
    // thread deletes through ImageListFilter::Remove -> source remove ->
    // re-entrant rebuild.  Without the map mutex this races: two rebuilds
    // clearing/pushing on the same vector => "double free or corruption".
    std::vector<std::string> uris;
    for (int i = 0; i < 2000; i++)
        uris.push_back(std::string("file:///test/t") + std::to_string(i) + ".jpg");

    boost::shared_ptr<EmittingMockList> src(new EmittingMockList(uris));
    boost::shared_ptr<ImageListFilter> filter(new ImageListFilter(src, [](const QuiverFile& qf) {
        std::string uri = qf.GetURI() ? qf.GetURI() : "";
        return uri.find(".jpg") != std::string::npos;
    }));

    boost::shared_ptr<ViewerLikeHandler> viewer(new ViewerLikeHandler());
    viewer->SetView(filter);
    filter->AddEventHandler(viewer);

    std::atomic<bool> stop{false};
    std::atomic<unsigned int> reads{0};
    std::vector<std::thread> workers;
    for (int w = 0; w < 4; w++)
    {
        workers.emplace_back([&]() {
            while (!stop.load(std::memory_order_relaxed))
            {
                // worker: bounds check + thumbnail read through the filter
                for (unsigned int i = 0; i < filter->GetSize(); i++)
                {
                    (void)filter->Get(i);
                    reads.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    // main thread: the viewer delete path while workers are reading
    for (unsigned int round = 0; round < 200 && filter->GetSize() > 0; round++)
        filter->Remove(filter->GetCurrentIndex() + 0 % std::max(1u, filter->GetSize()));

    stop.store(true, std::memory_order_relaxed);
    for (auto& t : workers)
        t.join();

    CHECK(viewer->m_removedCount > 0);
    CHECK(reads.load(std::memory_order_relaxed) > 0);
    CHECK(filter->GetSize() + 200 <= 2000);
    // still structurally consistent
    for (unsigned int i = 0; i < filter->GetSize(); i++)
        REQUIRE(filter->Get(i).GetURI() != nullptr);
}