//
// Created by RGAA on 24/01/2025.
//

#ifndef GAMMARAY_UID_SPACER_H
#define GAMMARAY_UID_SPACER_H

#include <string>

namespace tc
{

    static std::string SpaceId(const std::string& uid) {
        if ((uid.size() != 9 && uid.size() != 10) || uid.find(".") != std::string::npos) {
            return uid;
        }
        std::string result;
        result += uid.substr(0, 3);
        result += " ";
        result += uid.substr(3, 3);
        result += " ";
        result += uid.substr(6, 3);
        return result;
    }

}

#endif //GAMMARAY_UID_SPACER_H
