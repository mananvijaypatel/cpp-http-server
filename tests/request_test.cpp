#include <gtest/gtest.h>

#include <string>
#include <string_view>

#include "http/request.hpp"

using namespace std::literals;
using http::ParseStatus;
using http::parse_request_head;

// --------------- Valid requests -----------------
TEST(RequestParser, ParseStatus) {
    std::string raw = "GET /index.html HTTP/1.1\r\nHost: localhost\r\n\r\n";
    auto r = parse_request_head(raw);

    ASSERT_EQ(r.status, ParseStatus::Complete);
    EXPECT_EQ(r.request.method, "GET");
    EXPECT_EQ(r.request.target, "/index.html");
    EXPECT_EQ(r.request.version, "HTTP/1.1");
    EXPECT_EQ(r.request.header("Host").value_or(""), "localhost"sv);
    EXPECT_EQ(r.consumed, raw.size());
}

TEST(RequestParser, HeaderLookupIsCaseInsensitive) {
    auto r = parse_request_head("GET / HTTP/1.1\r\nHOST: example.com\r\n\r\n");
    ASSERT_EQ(r.status, ParseStatus::Complete);
    EXPECT_EQ(r.request.header("host").value_or(""), "example.com"sv);
    EXPECT_EQ(r.request.header("Host").value_or(""), "example.com"sv);
}

TEST(RequestParser, MissingHeaderReturnsNullopt) {
    auto r = parse_request_head("GET / HTTP/1.1\r\nHost: x\r\n\r\n");
    ASSERT_EQ(r.status, ParseStatus::Complete);
    EXPECT_FALSE(r.request.header("User-Agent").has_value());
}

TEST(RequestParser, TrimsHeaderValueWhitespace) {
    auto r = parse_request_head("GET / HTTP/1.1\r\nHost:   localhost \t\r\n\r\n");
    ASSERT_EQ(r.status, ParseStatus::Complete);
    EXPECT_EQ(r.request.header("Host").value_or(""), "localhost"sv);
}

TEST(RequestParser, CombinesRepeatedHeaders) {
    auto r = parse_request_head(
        "GET / HTTP/1.1\r\nHost: x\r\nAccept: text/html\r\nAccept: text/plain\r\n\r\n");
    ASSERT_EQ(r.status, ParseStatus::Complete);
    EXPECT_EQ(r.request.header("Accept").value_or(""), "text/html, text/plain"sv);
}

TEST(RequestParser, AllowsMissingHostInHttp10) {
    auto r = parse_request_head("GET / HTTP/1.0\r\n\r\n");
    EXPECT_EQ(r.status, ParseStatus::Complete);
}


// ---------- TCP stream behavior ----------

TEST(RequestParser, IncompleteUntilBlankLine) {
    auto r = parse_request_head("GET / HTTP/1.1\r\nHost: x\r\n");
    EXPECT_EQ(r.status, ParseStatus::Incomplete);
}

TEST(RequestParser, CompletesWhenDataArrivesInPieces) {
    // Simulates two recv() calls splitting one request.
    std::string buffer = "GET /data HTTP/1.1\r\nHo";
    EXPECT_EQ(parse_request_head(buffer).status, ParseStatus::Incomplete);

    buffer += "st: x\r\n\r\n";
    auto r = parse_request_head(buffer);
    ASSERT_EQ(r.status, ParseStatus::Complete);
    EXPECT_EQ(r.request.target, "/data");
}

TEST(RequestParser, ConsumedStopsAtEndOfFirstRequest) {
    // Two pipelined requests in one buffer.
    std::string first  = "GET /a HTTP/1.1\r\nHost: x\r\n\r\n";
    std::string second = "GET /b HTTP/1.1\r\nHost: x\r\n\r\n";
    std::string buffer = first + second;

    auto r1 = parse_request_head(buffer);
    ASSERT_EQ(r1.status, ParseStatus::Complete);
    EXPECT_EQ(r1.request.target, "/a");
    EXPECT_EQ(r1.consumed, first.size());

    auto r2 = parse_request_head(std::string_view(buffer).substr(r1.consumed));
    ASSERT_EQ(r2.status, ParseStatus::Complete);
    EXPECT_EQ(r2.request.target, "/b");
}


// ---------- Malformed and malicious requests ----------

TEST(RequestParser, RejectsMissingHostInHttp11) {
    auto r = parse_request_head("GET / HTTP/1.1\r\n\r\n");
    EXPECT_EQ(r.status, ParseStatus::Error);
    EXPECT_EQ(r.error_status, 400);
}

TEST(RequestParser, RejectsMalformedRequestLine) {
    EXPECT_EQ(parse_request_head("GET  / HTTP/1.1\r\nHost: x\r\n\r\n").error_status, 400);
    EXPECT_EQ(parse_request_head("GET /\r\nHost: x\r\n\r\n").error_status, 400);
    EXPECT_EQ(parse_request_head("GET /a b HTTP/1.1\r\nHost: x\r\n\r\n").error_status, 400);
}

TEST(RequestParser, RejectsUnsupportedVersion) {
    auto r = parse_request_head("GET / HTTP/2.0\r\nHost: x\r\n\r\n");
    EXPECT_EQ(r.status, ParseStatus::Error);
    EXPECT_EQ(r.error_status, 505);
}

TEST(RequestParser, RejectsHeaderWithoutColon) {
    auto r = parse_request_head("GET / HTTP/1.1\r\nHost x\r\n\r\n");
    EXPECT_EQ(r.error_status, 400);
}

TEST(RequestParser, RejectsSpaceBeforeColon) {
    auto r = parse_request_head("GET / HTTP/1.1\r\nHost : x\r\n\r\n");
    EXPECT_EQ(r.error_status, 400);
}

TEST(RequestParser, RejectsDuplicateContentLength) {
    auto r = parse_request_head(
        "POST / HTTP/1.1\r\nHost: x\r\nContent-Length: 5\r\nContent-Length: 50\r\n\r\n");
    EXPECT_EQ(r.status, ParseStatus::Error);
    EXPECT_EQ(r.error_status, 400);
}

TEST(RequestParser, RejectsControlCharacterInValue) {
    std::string raw = "GET / HTTP/1.1\r\nHost: x\r\nX-Test: bad";
    raw += '\x01';
    raw += "value\r\n\r\n";
    EXPECT_EQ(parse_request_head(raw).error_status, 400);
}

TEST(RequestParser, RejectsOversizedHeadEvenWithoutTerminator) {
    std::string raw = "GET / HTTP/1.1\r\nHost: x\r\nX-Big: ";
    raw += std::string(9000, 'a');   // no "\r\n\r\n" ever arrives
    auto r = parse_request_head(raw);
    EXPECT_EQ(r.status, ParseStatus::Error);
    EXPECT_EQ(r.error_status, 431);
}