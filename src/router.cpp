#include "http/router.hpp"

#include <map>
#include <string>
#include <string_view>

namespace http {
namespace {

constexpr std::string_view kIndexHtml = R"(<!DOCTYPE html>
<html>
<head><meta charset="utf-8"><title>cpp-http-server</title></head>
<body>
  <h1>Hello from C++!</h1>
  <p>This page was served by a hand-written HTTP/1.1 server.</p>
  <ul>
    <li><a href="/health">/health</a></li>
    <li><a href="/headers">/headers</a></li>
  </ul>
</body>
</html>
)";

constexpr std::string_view kText = "text/plain; charset=utf-8";

// "/search?q=cpp" -> "/search"
std::string_view path_of(std::string_view target) {
    auto q = target.find('?');
    return (q == std::string_view::npos) ? target : target.substr(0, q);
}
}  // namespace

Response route(const Request& req) {
    if (req.method != "GET" && req.method != "HEAD") {
        auto resp = make_response(405, std::string(kText), "405 Method Not Allowed\n");
        resp.add_header("Allow", "GET, HEAD");  // required with 405
        return resp;
    }

    std::string_view path = path_of(req.target);

    if (path == "/") {
        return make_response(200, "text/html; charset=utf-8", std::string(kIndexHtml));
    }

    if (path == "/health") {
        return make_response(200, std::string(kText), "ok\n");
    }

    if (path == "/headers") {
        // Echo the request back. std::map sorts headers so output is stable.
        std::map<std::string, std::string> sorted(req.headers.begin(), req.headers.end());
        std::string body = req.method + " " + req.target + " " + req.version + "\n\n";
        for (const auto& [name, value] : sorted) {
            body += name;
            body += ": ";
            body += value;
            body += "\n";
        }
        auto resp = make_response(200, std::string(kText), std::move(body));
        resp.add_header("X-Content-Type-Options", "nosniff");
        return resp;
    }

    if (path == "/favicon.ico") {
        Response resp;
        resp.status = 204;  // No Content: we deliberately have no icon
        return resp;
    }

    return make_response(404, std::string(kText), "404 Not Found\n");
}
}  // namespace http