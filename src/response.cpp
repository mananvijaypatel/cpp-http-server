#include "http/response.hpp"

#include <stdexcept>    // std::invalid_argument
#include <string>

namespace http {

    void Response::add_header(std::string name, std::string value) {
        if (name.find_first_of("\r\n") != std::string::npos || value.find_first_of("\r\n") != std::string::npos) {
            throw std::invalid_argument("header name/value must not contain CR or LF");
        }
        headers.emplace_back(std::move(name), std::move(value));
    }

    std::string_view reason_phrase(int status) {
        switch(status) {
            case 200: return "OK";
            case 204: return "No Content";
            case 400: return "Bad Request";
            case 404: return "Not Found";
            case 405: return "Method Not Allowed";
            case 431: return "Request Header Fields Too Large";
            case 500: return "Internal Server Error";
            case 505: return "HTTP Version Not Supported";
            default: return "Unknown";
        }
    }

    Response make_response(int status, std::string content_type, std::string body) {
        Response resp;
        resp.status = status;
        resp.add_header("Content-Type", std::move(content_type));
        resp.body = std::move(body);
        return resp;
    }

    std::string serialize(const Response& resp, bool keep_alive, bool head_request) {
        std::string out;
        out.reserve(256 + resp.body.size());    // one allocation for the whole response

        // Status line: "HTTP/1.1 200 OK\r\n"
        out += "HTTP/1.1 ";
        out += std::to_string(resp.status);
        out += ' ';
        out += reason_phrase(resp.status);
        out += "\r\n";

        // Handler-provided headers, in the order they were added.
        for (const auto& [name, value] : resp.headers) {
            out += name;
            out += ": ";
            out += value;
            out += "\r\n"; 
        }

        // Headers the server always controls.
        out += "Content-Length: ";
        out += std::to_string(resp.body.size());
        out += "\r\n";
        out += keep_alive ? "Connection: keep-alive\r\n" : "Connection: close\r\n";
        out += "Server: cpp-http-server\r\n";

        out += "\r\n";      // blank line: end of headers

        if(!head_request) {
            out += resp.body;
        }

        return out;
    }

}       // namespace http