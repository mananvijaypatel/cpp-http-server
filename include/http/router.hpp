#pragma once

#include "http/request.hpp"
#include "http/response.hpp"

namespace http {

// Maps a parsed request to a response. Pure function: no I/O, easy to test.
Response route(const Request& req);

}  // namespace http