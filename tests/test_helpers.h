#ifndef QUIVER_TEST_HELPERS_H
#define QUIVER_TEST_HELPERS_H

#include <catch2/catch_test_macros.hpp>
#include <string>

bool QuiverTest_HasDisplay();
const char* QuiverTest_GetDisplayBackend();
std::string QuiverTest_GetImagesDir();
std::string QuiverTest_GetDataDir();

#define REQUIRE_DISPLAY() \
    do { \
        if (!QuiverTest_HasDisplay()) { \
            SKIP("Display required for this test; run with wlheadless-run or xvfb-run"); \
        } \
    } while (0)

#endif // QUIVER_TEST_HELPERS_H
