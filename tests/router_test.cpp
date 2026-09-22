#include <gtest/gtest.h>

#include <string>
#include <string_view>

#include "http/request.hpp"
#include "http/router.hpp"

namespace {

http::Request make_request(std::string method, std::string target) {
    http::Request req;
    req.method = std::move(method);
    req.target = std::move(target);
    req.version = "HTTP/1.1";
    req.headers["host"] = "localhost";
    return req;
}

bool has_header(const http::Response& resp, std::string_view name, std::string_view value) {
    for (const auto& [n, v] : resp.headers) {
        if (n == name && v == value) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST(Router, IndexReturnsHtml) {
    auto resp = http::route(make_request("GET", "/"));
    EXPECT_EQ(resp.status, 200);
    EXPECT_TRUE(has_header(resp, "Content-Type", "text/html; charset=utf-8"));
    EXPECT_NE(resp.body.find("<h1>"), std::string::npos);
}

TEST(Router, HealthIgnoresQueryString) {
    auto resp = http::route(make_request("GET", "/health?check=1"));
    EXPECT_EQ(resp.status, 200);
    EXPECT_EQ(resp.body, "ok\n");
}

TEST(Router, HeadIsAllowed) {
    EXPECT_EQ(http::route(make_request("HEAD", "/health")).status, 200);
}

TEST(Router, UnknownPathIs404) {
    EXPECT_EQ(http::route(make_request("GET", "/nope")).status, 404);
}

TEST(Router, PostIs405WithAllowHeader) {
    auto resp = http::route(make_request("POST", "/"));
    EXPECT_EQ(resp.status, 405);
    EXPECT_TRUE(has_header(resp, "Allow", "GET, HEAD"));
}

TEST(Router, HeadersEndpointIsPlainTextWithNosniff) {
    auto resp = http::route(make_request("GET", "/headers"));
    EXPECT_EQ(resp.status, 200);
    EXPECT_TRUE(has_header(resp, "Content-Type", "text/plain; charset=utf-8"));
    EXPECT_TRUE(has_header(resp, "X-Content-Type-Options", "nosniff"));
    EXPECT_NE(resp.body.find("host: localhost"), std::string::npos);
}