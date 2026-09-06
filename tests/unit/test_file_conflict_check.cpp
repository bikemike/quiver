#include <catch2/catch_test_macros.hpp>
#include "FileConflictCheck.h"
#include <vector>
#include <string>

TEST_CASE("FileConflictCheck Collision Detection", "[unit][conflict][fast]")
{
    std::vector<FileConflictCheck::Mapping> mappings;
    FileConflictCheck::ResultList results;

    SECTION("Empty mappings list returns false")
    {
        bool hasConflict = FileConflictCheck::Check(mappings, results);
        REQUIRE_FALSE(hasConflict);
        REQUIRE(results.empty());
    }

    SECTION("Self-moves (source == destination) are not conflicts")
    {
        mappings.emplace_back("file:///tmp/quiver_test/pic1.jpg", "file:///tmp/quiver_test/pic1.jpg");
        mappings.emplace_back("file:///tmp/quiver_test/pic2.jpg", "file:///tmp/quiver_test/pic2.jpg");

        bool hasConflict = FileConflictCheck::Check(mappings, results);
        REQUIRE_FALSE(hasConflict);
        REQUIRE(results.size() == 2);
        REQUIRE_FALSE(results[0].HasConflict());
        REQUIRE_FALSE(results[1].HasConflict());
    }

    SECTION("Duplicate destination paths within batch are flagged as conflicts")
    {
        // Two distinct files being renamed to the same new name
        mappings.emplace_back("file:///tmp/quiver_test/a.jpg", "file:///tmp/quiver_test/target.jpg");
        mappings.emplace_back("file:///tmp/quiver_test/b.jpg", "file:///tmp/quiver_test/target.jpg");

        bool hasConflict = FileConflictCheck::Check(mappings, results);
        REQUIRE(hasConflict);
        REQUIRE(results.size() == 2);
        REQUIRE(results[0].HasConflict());
        REQUIRE(results[1].HasConflict());
        REQUIRE(results[0].strDstName == "target.jpg");
        REQUIRE(results[1].strDstName == "target.jpg");
    }

    SECTION("Unique non-existing destinations have no conflicts")
    {
        mappings.emplace_back("file:///tmp/quiver_test/a.jpg", "file:///tmp/quiver_test/unique_a_9999.jpg");
        mappings.emplace_back("file:///tmp/quiver_test/b.jpg", "file:///tmp/quiver_test/unique_b_9999.jpg");

        bool hasConflict = FileConflictCheck::Check(mappings, results);
        REQUIRE_FALSE(hasConflict);
        REQUIRE(results.size() == 2);
        REQUIRE_FALSE(results[0].HasConflict());
        REQUIRE_FALSE(results[1].HasConflict());
    }

    SECTION("Cancellation token aborts check")
    {
        mappings.emplace_back("file:///tmp/quiver_test/a.jpg", "file:///tmp/quiver_test/target.jpg");
        mappings.emplace_back("file:///tmp/quiver_test/b.jpg", "file:///tmp/quiver_test/target.jpg");

        GCancellable* cancellable = g_cancellable_new();
        g_cancellable_cancel(cancellable);

        bool hasConflict = FileConflictCheck::Check(mappings, results, cancellable);
        REQUIRE_FALSE(hasConflict);

        g_object_unref(cancellable);
    }
}
