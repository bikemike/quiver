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
            m_currentIndex++;
            return true;
        }
        return false;
    }

    bool Previous() override {
        if (HasPrevious()) {
            m_currentIndex--;
            return true;
        }
        return false;
    }

    bool First() override {
        if (!m_files.empty()) {
            m_currentIndex = 0;
            return true;
        }
        return false;
    }

    bool Last() override {
        if (!m_files.empty()) {
            m_currentIndex = m_files.size() - 1;
            return true;
        }
        return false;
    }

    bool SetCurrentIndex(unsigned int iIndex) override {
        if (iIndex < m_files.size()) {
            m_currentIndex = iIndex;
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
    ImageListFilter filter(mock, [](const QuiverFile& qf) {
        std::string uri = qf.GetURI() ? qf.GetURI() : "";
        return uri.find(".jpg") != std::string::npos;
    });

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
        ImageListFilter noMatch(mock, [](const QuiverFile&) {
            return false;
        });

        REQUIRE(noMatch.GetSize() == 0);
        REQUIRE_FALSE(noMatch.HasNext());
        REQUIRE_FALSE(noMatch.HasPrevious());
    }

    SECTION("Filter that matches everything")
    {
        ImageListFilter matchAll(mock, [](const QuiverFile&) {
            return true;
        });

        REQUIRE(matchAll.GetSize() == 5);
    }
}
