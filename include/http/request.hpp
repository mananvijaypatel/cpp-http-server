#pragma once 

#include <cstddef>          // std::size_t
#include <optional>         // std::optional
#include <string>           // std::string
#include <string_view>      // std::string_view
#include <unordered_map>    // std::unordered_map

namespace http {

    struct Request {
        std::string method;     //"GET", "POST",  ....
        std::string target;     // "index.html?x=1"
        std::string version;    // "HTTP/1.1"

        // Header names are case-insensitive in HTTP, so keys are stored lowercase.
        std::unordered_map<std::string, std::string> headers;

        // Case-insensitive lookup: header("Host") and header("host") both works.
        // Returns std::nulllopt if the header is absent.
        std::optional<std::string_view> header(std::string_view name) const;
    };

    enum class ParseStatus {
        Complete,           // a full request head was parsed
        Incomplete,         // need more bytes: call again after the next recv()
        Error,              // malformed or too large: send error_status and close
    };

    struct ParseResult {
        ParseStatus status = ParseStatus::Incomplete;
        Request request;                    // filled in only when status == Complete
        std::size_t consumed = 0;           // bytes used by the head, including "\r\n\r\n"
        int error_status = 0;               // HTTP status to send on Error (400, 431, 505)
        std::string error;                  // human-readable reason, for logs
    };


    // Limit on request line + headers. Real servers use similar limits (~8 KB)
    // So a client can't exhaust memory by sending endless headers.
    inline constexpr std::size_t kMaxHeadBytes = 8192;

    // Parses the request line and headers from the start of 'data'.
    ParseResult parse_request_head(std::string_view data, std::size_t max_head_bytes = kMaxHeadBytes);

    // Should the connection stay open after this request?
    // HTTP/1.1 defaults to keep-alive; HTTP/1.0 defaults to close
    bool wants_keep_alive(const Request& req);

    // Does this request claim to carry a body?
    bool has_body(const Request& req);

} // namespace http