#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace http {

struct Response {
    int status = 200;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;

    // Adds a header. Throws std::invalid_argument if the name or value
    // contains CR or LF (which would allow response splitting).
    void add_header(std::string name, std::string value);
};

// "OK" for 200 , "not Found" for 404, ...
std::string_view reason_phrase(int status);

// Convenience: a response with a status, Content-Type, and body.
Response make_response(int status, std::string content_type, std::string body);

// Turns a Response into bytes to send. Adds Content-Length and Connection.
// for HEAD requests, the body is omitted but Content-Length is still sent.
std::string serialize(const Response& resp, bool keep_alie, bool head_request);

}  // namespace http