#include <catch2/catch_test_macros.hpp>
#include "MD5.h"
#include <string>
#include <sstream>
#include <memory>

static std::string ComputeMD5(const std::string& input)
{
    MD5 md5;
    md5.update((unsigned char*)input.c_str(), input.length());
    md5.finalize();
    char* hex = md5.hex_digest();
    std::string result(hex);
    delete[] hex;
    return result;
}

TEST_CASE("MD5 RFC 1321 Standard Test Vectors", "[unit][crypto][fast][md5]")
{
    SECTION("Empty string")
    {
        REQUIRE(ComputeMD5("") == "d41d8cd98f00b204e9800998ecf8427e");
    }

    SECTION("Single character 'a'")
    {
        REQUIRE(ComputeMD5("a") == "0cc175b9c0f1b6a831c399e269772661");
    }

    SECTION("String 'abc'")
    {
        REQUIRE(ComputeMD5("abc") == "900150983cd24fb0d6963f7d28e17f72");
    }

    SECTION("String 'message digest'")
    {
        REQUIRE(ComputeMD5("message digest") == "f96b697d7cb7938d525a2f31aaf161d0");
    }

    SECTION("Alphabet lowercase")
    {
        REQUIRE(ComputeMD5("abcdefghijklmnopqrstuvwxyz") == "c3fcd3d76192e4007dfb496cca67e13b");
    }

    SECTION("Alphabet alphanumeric uppercase and lowercase")
    {
        REQUIRE(ComputeMD5("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789") ==
                "d174ab98d277d9f5a5611c2c9f419d9f");
    }

    SECTION("Digits 1 to 0 repeated eight times")
    {
        REQUIRE(ComputeMD5("12345678901234567890123456789012345678901234567890123456789012345678901234567890") ==
                "57edf4a22be3c955ac49da2e2107b67a");
    }
}

TEST_CASE("MD5 Incremental and Stream Hashing", "[unit][crypto][fast][md5]")
{
    SECTION("Streaming in chunks matches single pass")
    {
        std::string full = "The quick brown fox jumps over the lazy dog";
        std::string expected = ComputeMD5(full);

        MD5 chunked;
        chunked.update((unsigned char*)full.substr(0, 10).c_str(), 10);
        chunked.update((unsigned char*)full.substr(10, 15).c_str(), 15);
        chunked.update((unsigned char*)full.substr(25).c_str(), full.length() - 25);
        chunked.finalize();

        char* hex = chunked.hex_digest();
        std::string actual(hex);
        delete[] hex;

        REQUIRE(actual == expected);
    }

    SECTION("Hashing std::istream")
    {
        std::string message = "Quiver fast image viewer stream test";
        std::stringstream ss(message);

        MD5 md5(ss);
        char* hex = md5.hex_digest();
        std::string actual(hex);
        delete[] hex;

        REQUIRE(actual == ComputeMD5(message));
    }

    SECTION("Raw 16-byte binary digest")
    {
        MD5 md5((unsigned char*)"test");
        unsigned char* raw = md5.raw_digest();
        REQUIRE(raw != nullptr);

        char* hex = md5.hex_digest();
        // Convert first raw byte to hex and compare
        char first_byte_hex[3];
        snprintf(first_byte_hex, sizeof(first_byte_hex), "%02x", raw[0]);
        std::string hex_str(hex);
        REQUIRE(hex_str.substr(0, 2) == std::string(first_byte_hex));

        delete[] raw;
        delete[] hex;
    }
}
