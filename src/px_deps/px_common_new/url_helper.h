//
// Created by RGAA on 23/11/2024.
//

#ifndef PX_URL_HELPER_H
#define PX_URL_HELPER_H

#include <iostream>
#include <string>
#include <unordered_map>
#include <sstream>

namespace px
{

    class UrlHelper {
    public:

        static std::unordered_map<std::string, std::string> ParseQueryString(const std::string& queryString);
        static std::string EncodeQueryComponent(const std::string& value);

    };

}

#endif //PX_URL_HELPER_H
