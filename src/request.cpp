#include "http/request.hpp"

#include <cctype>  // std::isalnum, std::tolower
#include <string>
#include <utility>  // std::move

#include "http/util.hpp"  // http::trim (from Milestone 0, finally used!)

namespace http {
namespace {

constexpr auto npos = std::string_view::npos;
constexpr std::string_view kCrlf = "\r\n";
constexpr std::string_view kHeadEnd = "\r\n\r\n";

// Lowercase a string. The unsigned char cast matters: passing a negative
// char to std::tolower is undefined behavior.
std::string to_lower(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

// "tchar" from RFC 9110: characters allowed in methods and header names.
bool is_tchar(char c) {
    if (std::isalnum(static_cast<unsigned char>(c)) != 0) {
        return true;
    }
    switch (c) {
        case '!':
        case '#':
        case '$':
        case '%':
        case '&':
        case '\'':
        case '*':
        case '+':
        case '-':
        case '.':
        case '^':
        case '_':
        case '`':
        case '|':
        case '~':
            return true;
        default:
            return false;
    }
}

// A "token" is one or more tchars (no spaces, no colons, no control chars).
bool is_token(std::string_view s) {
    if (s.empty()) {
        return false;
    }
    for (char c : s) {
        if (!is_tchar(c)) {
            return false;
        }
    }
    return true;
}

// Control characters (except tab) are never allowed in header values
// or the request target.
bool has_forbidden_ctl(std::string_view s, bool allow_tab) {
    for (char c : s) {
        auto uc = static_cast<unsigned char>(c);
        if ((uc < 0x20 && !(allow_tab && c == '\t')) || uc == 0x7F) {
            return true;
        }
    }
    return false;
}

// Build an Error result.
ParseResult fail(int status, std::string reason) {
    ParseResult r;
    r.status = ParseStatus::Error;
    r.error_status = status;
    r.error = std::move(reason);
    return r;
}

}  // namespace

std::optional<std::string_view> Request::header(std::string_view name) const {
    auto it = headers.find(to_lower(name));
    if (it == headers.end()) {
        return std::nullopt;
    }
    return it->second;
}

ParseResult parse_request_head(std::string_view data, std::size_t max_head_bytes) {
    // ---- 1. Find the end of the head (the blank line) -------------------
    std::size_t end = data.find(kHeadEnd);

    if (end == npos) {
        if (data.size() > max_head_bytes) {
            return fail(431, "request header section too large");
        }
        return ParseResult{};  // default status is Incomplete: wait for more data
    }

    std::size_t head_size = end + kHeadEnd.size();
    if (head_size > max_head_bytes) {
        return fail(431, "request header section too large");
    }

    std::string_view head = data.substr(0, end);  // excludes the final "\r\n\r\n"

    // ---- 2. Split off the request line ---------------------------------
    std::size_t line_end = head.find(kCrlf);
    std::string_view request_line = head.substr(0, line_end);  // npos = whole thing
    std::string_view rest =
        (line_end == npos) ? std::string_view{} : head.substr(line_end + kCrlf.size());

    // ---- 3. Parse "METHOD SP TARGET SP VERSION" --------------------------
    std::size_t sp1 = request_line.find(' ');
    std::size_t sp2 = (sp1 == npos) ? npos : request_line.find(' ', sp1 + 1);

    // Exactly two single spaces: no more, no less.
    if (sp1 == npos || sp2 == npos || request_line.find(' ', sp2 + 1) != npos) {
        return fail(400, "malformed request line");
    }

    std::string_view method = request_line.substr(0, sp1);
    std::string_view target = request_line.substr(sp1 + 1, sp2 - sp1 - 1);
    std::string_view version = request_line.substr(sp2 + 1);

    if (!is_token(method)) {
        return fail(400, "invalid method");
    }
    if (target.empty() || (target.front() != '/' && target != "*") ||
        has_forbidden_ctl(target, /*allow_tab=*/false)) {
        return fail(400, "invalid request target");
    }
    if (!version.starts_with("HTTP/")) {
        return fail(400, "invalid HTTP version");
    }
    if (version != "HTTP/1.1" && version != "HTTP/1.0") {
        return fail(505, "unsupported HTTP version");
    }

    Request req;
    req.method = std::string(method);
    req.target = std::string(target);
    req.version = std::string(version);

    // ---- 4. Parse header lines: "Name: value" ----------------------------
    while (!rest.empty()) {
        std::size_t eol = rest.find(kCrlf);
        std::string_view line = rest.substr(0, eol);
        rest = (eol == npos) ? std::string_view{} : rest.substr(eol + kCrlf.size());

        std::size_t colon = line.find(':');
        if (colon == npos) {
            return fail(400, "header line without ':'");
        }

        // The name must be a token. This also rejects "Host : x" (space before
        // the colon) and folded lines starting with whitespace.
        std::string_view name = line.substr(0, colon);
        if (!is_token(name)) {
            return fail(400, "invalid header name");
        }

        std::string_view value = trim(line.substr(colon + 1));
        if (has_forbidden_ctl(value, /*allow_tab=*/true)) {
            return fail(400, "invalid character in header value");
        }

        std::string key = to_lower(name);
        auto it = req.headers.find(key);

        if (it == req.headers.end()) {
            req.headers.emplace(std::move(key), std::string(value));
        } else if (key == "content-length" || key == "host") {
            // Duplicates of these are a classic request-smuggling trick.
            return fail(400, "duplicate " + key + " header");
        } else {
            // RFC 9110: repeated headers combine into a comma-separated list.
            it->second += ", ";
            it->second += value;
        }
    }

    // ---- 5. HTTP/1.1 requires a Host header ------------------------------
    if (req.version == "HTTP/1.1" && !req.headers.contains("host")) {
        return fail(400, "missing Host header");
    }

    ParseResult result;
    result.status = ParseStatus::Complete;
    result.request = std::move(req);
    result.consumed = head_size;
    return result;
}

bool wants_keep_alive(const Request& req) {
    auto conn = req.header("Connection");
    if (req.version == "HTTP/1.1") {
        return !(conn && iequals(*conn, "close"));
    }
    return conn && iequals(*conn, "keep-alive");
}

bool has_body(const Request& req) {
    if (req.header("Transfer-Encoding")) {
        return true;
    }
    auto length = req.header("Content-Length");
    return length && *length != "0";
}

}  // namespace http