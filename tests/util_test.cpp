#include <gtest/gtest.h>
#include "http/util.hpp"

// GoogleTest printed the expected value as memory address instead of test, That happens because "Host: example.com" is a raw C string (const char*), and GoogleTest prints pointers as addresses. That's why the message is ugly!

// adding
using namespace std::literals;

TEST(Trim, RemovesSurroundingWhitespace) {
    // adding sv suffix to the expected values.
    EXPECT_EQ(http::trim("   Host: example.com \r\n"), "Host: example.com"sv);
}

TEST(Trim, AllWhitespaceBecomesEmpty) {
    EXPECT_EQ(http::trim(" \t\r\n"), "");
}

TEST(Trim, EmptyStaysEmpty) {
    EXPECT_EQ(http::trim(""), "");
}