#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

#include "http/response.hpp"

TEST(Response, SerializesStatusLineHeadersAndBody) {
    auto resp = http::make_response(200, "text/plain", "hello");
    std::string out = http::serialize(resp, /*keep_alive=*/true, /*head_request=*/false);

    EXPECT_EQ(out.rfind("HTTP/1.1 200 OK\r\n", 0), 0u);  // starts with status line
    // out.rfind(prefix, 0) == 0 is a common idiom for "starts with" in C++17. It searches backward
    // from position 0, so it can only match at the very start. (C++20 adds out.starts_with(...),
    // which you could use instead since we're on C++20.)
    EXPECT_NE(out.find("Content-Type: text/plain\r\n"), std::string::npos);
    EXPECT_NE(out.find("Content-Length: 5\r\n"), std::string::npos);
    EXPECT_NE(out.find("Connection: keep-alive\r\n"), std::string::npos);
    EXPECT_EQ(out.substr(out.size() - 9), "\r\n\r\nhello");
}

TEST(Response, HeadOmitsBodyButKeepsContentLength) {
    auto resp = http::make_response(200, "text/plain", "hello");
    std::string out = http::serialize(resp, true, /*head_request=*/true);

    EXPECT_NE(out.find("Content-Length: 5\r\n"), std::string::npos);
    EXPECT_EQ(out.substr(out.size() - 4), "\r\n\r\n");  // ends right after headers
}

TEST(Response, CloseConnectionHeader) {
    auto resp = http::make_response(404, "text/plain", "");
    std::string out = http::serialize(resp, /*keep_alive=*/false, false);

    EXPECT_EQ(out.rfind("HTTP/1.1 404 Not Found\r\n", 0), 0u);
    EXPECT_NE(out.find("Connection: close\r\n"), std::string::npos);
}

TEST(Response, AddHeaderRejectsNewlines) {
    http::Response resp;
    EXPECT_THROW(resp.add_header("X-Test", "a\r\nInjected: yes"), std::invalid_argument);
    EXPECT_THROW(resp.add_header("Bad\nName", "v"), std::invalid_argument);
}