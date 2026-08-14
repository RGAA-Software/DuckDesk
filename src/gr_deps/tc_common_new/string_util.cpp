//
// Created by RGAA on 2023-12-17.
//

#include "string_util.h"

namespace tc {

    std::string StringUtil::Trim(const std::string& str) {
        auto view = std::string_view(str);
        auto start = view.find_first_not_of(" \t\r\n");
        if (start == std::string_view::npos) {
            return "";
        }
        auto end = view.find_last_not_of(" \t\r\n");
        return std::string(view.substr(start, end - start + 1));
    }

    std::string ToHexString(const std::vector<uint8_t>& data) {
        std::ostringstream oss;
        for (const auto& byte : data) {
            oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
        }
        return oss.str();
    }
}