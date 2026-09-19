#include "http/util.hpp"

namespace http {
    std::string_view trim(std::string_view s) {
        constexpr std::string_view ws = " \t\r\n";
        const auto start = s.find_first_not_of(ws);

        if(start == std::string_view::npos){
            return {};
        }

        const auto end = s.find_last_not_of(ws);

        return s.substr(start, end - start + 1);
    }
}