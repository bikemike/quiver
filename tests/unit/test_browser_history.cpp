#include <catch2/catch_test_macros.hpp>
#include "BrowserHistory.h"
#include <list>
#include <string>

TEST_CASE("BrowserHistory Navigation and Stacks", "[unit][history][fast]")
{
    BrowserHistory history;

    SECTION("Initial state is empty")
    {
        REQUIRE(history.GetSize() == 0);
        REQUIRE_FALSE(history.CanGoBack());
        REQUIRE_FALSE(history.CanGoForward());
        REQUIRE_FALSE(history.GoBack());
        REQUIRE_FALSE(history.GoForward());
        REQUIRE(history.GetCurrentSelected().empty());
        REQUIRE(history.GetCurrentFiles().empty());
    }

    SECTION("Adding single entry")
    {
        std::list<std::string> files = {"file1.jpg", "file2.jpg"};
        history.Add(files, "file1.jpg");

        REQUIRE(history.GetSize() == 1);
        REQUIRE(history.GetCurrentIndex() == 0);
        REQUIRE_FALSE(history.CanGoBack());
        REQUIRE_FALSE(history.CanGoForward());
        REQUIRE(history.GetCurrentSelected() == "file1.jpg");
        REQUIRE(history.GetCurrentFiles() == files);
    }

    SECTION("Back and Forward linear navigation")
    {
        std::list<std::string> dirA = {"a1.jpg", "a2.jpg"};
        std::list<std::string> dirB = {"b1.jpg"};
        std::list<std::string> dirC = {"c1.jpg", "c2.jpg", "c3.jpg"};

        history.Add(dirA, "a1.jpg");
        history.Add(dirB, "b1.jpg");
        history.Add(dirC, "c2.jpg");

        REQUIRE(history.GetSize() == 3);
        REQUIRE(history.GetCurrentIndex() == 2);
        REQUIRE(history.CanGoBack());
        REQUIRE_FALSE(history.CanGoForward());
        REQUIRE(history.GetCurrentSelected() == "c2.jpg");

        // Go Back to B
        REQUIRE(history.GoBack());
        REQUIRE(history.GetCurrentIndex() == 1);
        REQUIRE(history.GetCurrentSelected() == "b1.jpg");
        REQUIRE(history.CanGoBack());
        REQUIRE(history.CanGoForward());

        // Go Back to A
        REQUIRE(history.GoBack());
        REQUIRE(history.GetCurrentIndex() == 0);
        REQUIRE(history.GetCurrentSelected() == "a1.jpg");
        REQUIRE_FALSE(history.CanGoBack());
        REQUIRE(history.CanGoForward());

        // Go Forward to B
        REQUIRE(history.GoForward());
        REQUIRE(history.GetCurrentIndex() == 1);
        REQUIRE(history.GetCurrentSelected() == "b1.jpg");

        // Go Forward to C
        REQUIRE(history.GoForward());
        REQUIRE(history.GetCurrentIndex() == 2);
        REQUIRE(history.GetCurrentSelected() == "c2.jpg");
        REQUIRE_FALSE(history.GoForward());
    }

    SECTION("Branching navigation truncates forward history")
    {
        history.Add({"1.jpg"}, "1.jpg");
        history.Add({"2.jpg"}, "2.jpg");
        history.Add({"3.jpg"}, "3.jpg");

        // Navigate back to 2
        history.GoBack();
        REQUIRE(history.GetCurrentSelected() == "2.jpg");

        // Add 4.jpg while at 2: 3.jpg should be truncated
        history.Add({"4.jpg"}, "4.jpg");
        REQUIRE(history.GetSize() == 3);
        REQUIRE(history.GetCurrentIndex() == 2);
        REQUIRE(history.GetCurrentSelected() == "4.jpg");
        REQUIRE_FALSE(history.CanGoForward());

        // Going back should lead to 2, then 1
        REQUIRE(history.GoBack());
        REQUIRE(history.GetCurrentSelected() == "2.jpg");
        REQUIRE(history.GoBack());
        REQUIRE(history.GetCurrentSelected() == "1.jpg");
    }

    SECTION("Selection update in place")
    {
        history.Add({"a.jpg", "b.jpg"}, "a.jpg");
        REQUIRE(history.GetCurrentSelected() == "a.jpg");

        history.SetCurrentSelected("b.jpg");
        REQUIRE(history.GetCurrentSelected() == "b.jpg");
    }
}
