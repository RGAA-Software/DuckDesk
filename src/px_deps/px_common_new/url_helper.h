//
// Created by RGAA on 23/11/2024.
//

#ifndef GAMMARAY_URL_HELPER_H
#define GAMMARAY_URL_HELPER_H

#include <iostream>
#include <string>
#include <unordered_map>
#include <sstream>

namespace tc
{

    class UrlHelper {
    public:

        static std::unordered_map<std::string, std::string> ParseQueryString(const std::string& queryString);

    };

}

#endif //GAMMARAY_URL_HELPER_H
