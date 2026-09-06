#include <catch2/catch_test_macros.hpp>
#include "Timer.h"
#include <thread>
#include <chrono>

TEST_CASE("Timer Basic and Elapsed Operations", "[unit][fast]")
{
    SECTION("Timer measures non-negative elapsed time")
    {
        Timer timer;
        double elapsed = timer.GetRunningTimeInSeconds();
        REQUIRE(elapsed >= 0.0);
    }

    SECTION("Timer detects elapsed delay")
    {
        Timer timer;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        double elapsed = timer.GetRunningTimeInSeconds();
        // Should be at least 15ms (0.015s)
        REQUIRE(elapsed >= 0.015);
    }

    SECTION("Multiple timers operate independently")
    {
        Timer t1("t1", true);
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        Timer t2("t2", true);
        std::this_thread::sleep_for(std::chrono::milliseconds(15));

        double e1 = t1.GetRunningTimeInSeconds();
        double e2 = t2.GetRunningTimeInSeconds();

        REQUIRE(e1 > e2);
        REQUIRE(e2 >= 0.010);
    }
}
