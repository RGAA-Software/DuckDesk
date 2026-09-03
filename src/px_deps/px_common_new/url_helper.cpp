//
// Created by RGAA on 23/11/2024.
//

#include "url_helper.h"

#include <string_view>

namespace px
{

    std::string UrlHelper::EncodeQueryComponent(const std::string& value) {
        constexpr std::string_view kHex = "0123456789ABCDEF";
        std::string encoded;
        encoded.reserve(value.size());
        for (const auto ch : value) {
            const auto byte = static_cast<unsigned char>(ch);
            const bool unreserved =
                (byte >= 'A' && byte <= 'Z')
                || (byte >= 'a' && byte <= 'z')
                || (byte >= '0' && byte <= '9')
                || byte == '-' || byte == '.' || byte == '_' || byte == '~';
            if (unreserved) {
                encoded.push_back(static_cast<char>(byte));
                continue;
            }
            encoded.push_back('%');
            encoded.push_back(kHex[(byte >> 4) & 0x0F]);
            encoded.push_back(kHex[byte & 0x0F]);
        }
        return encoded;
    }

    // 解码 URL 编码的特殊字符
    static std::string UrlDecode(const std::string& str) {
        std::string result;
        result.reserve(str.length());
        for (size_t i = 0; i < str.length(); ++i) {
            if (str[i] == '%' && i + 2 < str.length()) {
                unsigned int hex = 0;
                if (sscanf(str.substr(i + 1, 2).c_str(), "%2x", &hex) == 1) {
                    result += static_cast<char>(hex);
                    i += 2;
                } else {
                    result += str[i];
                }
            } else if (str[i] == '+') {
                result += ' ';
            } else {
                result += str[i];
            }
        }
        return result;
    }

    // 解析查询参数字符串并存储在 std::map 中
    std::unordered_map<std::string, std::string> UrlHelper::ParseQueryString(const std::string& queryString) {
        std::unordered_map<std::string, std::string> params;

        // 分割参数对
        std::istringstream iss(queryString);
        std::string param;
        while (std::getline(iss, param, '&')) {
            size_t pos = param.find('=');
            if (pos != std::string::npos) {
                std::string key = param.substr(0, pos);
                std::string value = param.substr(pos + 1);

                // 解码参数值
                key = UrlDecode(key);
                value = UrlDecode(value);

                // 存储参数
                params[key] = value;
            }
        }

        return params;
    }
}
