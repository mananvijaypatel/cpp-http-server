#include "http/util.hpp"
#include <cctype>

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

    // helper function! (To implement the checks)
    bool iequals(std::string_view a, std::string_view b) {
        if (a.size() != b.size()) {
            return false;
        }
        for (std::size_t i = 0;i < a.size(); ++i) {
            auto ca = std::tolower(static_cast<unsigned char>(a[i]));
            auto cb = std::tolower(static_cast<unsigned char>(b[i]));
            if (ca != cb) {
                return false;
            }
        }
        return true;
    }
}