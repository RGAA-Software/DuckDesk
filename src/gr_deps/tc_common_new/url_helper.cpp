//
// Created by RGAA on 23/11/2024.
//

#include "url_helper.h"

namespace tc
{

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