#pragma once
#include <string_view>

namespace http {
    // remove leading / trailing spaces, tabs, /r and n
    std::string_view trim(std::string_view s);

    // Case-insensitive ASCII comparison: iequals("Close", "close") == true
    bool iequals(std::string_view a, std::string_view b);
}


