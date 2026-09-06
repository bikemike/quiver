#include <catch2/catch_test_macros.hpp>
extern "C" {
#include "strnatcmp.h"
}
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

TEST_CASE("strnatcmp Natural vs ASCII Ordering", "[unit][strnatcmp][fast]")
{
    SECTION("Natural numeric ordering")
    {
        // In ASCII strcmp, "pic10.jpg" < "pic2.jpg" because '1' < '2'
        REQUIRE(strcmp("pic10.jpg", "pic2.jpg") < 0);

        // In strnatcmp, "pic2.jpg" must be < "pic10.jpg"
        REQUIRE(strnatcmp("pic2.jpg", "pic10.jpg") < 0);
        REQUIRE(strnatcmp("pic10.jpg", "pic2.jpg") > 0);
        REQUIRE(strnatcmp("pic2.jpg", "pic2.jpg") == 0);
    }

    SECTION("Multi-digit numeric sequence sorting")
    {
        std::vector<std::string> filenames = {
            "img100.png",
            "img2.png",
            "img1.png",
            "img20.png",
            "img10.png"
        };

        std::sort(filenames.begin(), filenames.end(), [](const std::string& a, const std::string& b) {
            return strnatcmp(a.c_str(), b.c_str()) < 0;
        });

        std::vector<std::string> expected = {
            "img1.png",
            "img2.png",
            "img10.png",
            "img20.png",
            "img100.png"
        };

        REQUIRE(filenames == expected);
    }

    SECTION("Version string comparisons")
    {
        REQUIRE(strnatcmp("1.2.3", "1.2.10") < 0);
        REQUIRE(strnatcmp("2.0.0", "1.9.9") > 0);
        REQUIRE(strnatcmp("0.1.0", "0.1.0") == 0);
    }

    SECTION("Empty and prefix strings")
    {
        REQUIRE(strnatcmp("", "") == 0);
        REQUIRE(strnatcmp("", "a") < 0);
        REQUIRE(strnatcmp("a", "") > 0);
        REQUIRE(strnatcmp("prefix", "prefix_longer") < 0);
    }
}

TEST_CASE("strnatcasecmp Case-Insensitive Natural Ordering", "[unit][strnatcmp][fast]")
{
    SECTION("Case insensitivity")
    {
        REQUIRE(strnatcasecmp("photo.jpg", "PHOTO.JPG") == 0);
        REQUIRE(strnatcasecmp("Photo2.jpg", "photo10.jpg") < 0);
        REQUIRE(strnatcasecmp("PHOTO10.JPG", "photo2.jpg") > 0);
    }

    SECTION("Case sensitivity difference between strnatcmp and strnatcasecmp")
    {
        // 'A' < 'a' in ASCII
        REQUIRE(strnatcmp("File.jpg", "file.jpg") < 0);
        // Case-insensitive considers them equal
        REQUIRE(strnatcasecmp("File.jpg", "file.jpg") == 0);
    }

    SECTION("Leading zero preservation handling")
    {
        // Numbers with leading zeros
        REQUIRE(strnatcmp("02", "2") != 0);
        REQUIRE(strnatcasecmp("pic01.png", "PIC01.png") == 0);
    }
}
