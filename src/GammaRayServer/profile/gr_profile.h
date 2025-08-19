//
// Created by RGAA on 19/08/2025.
//

#ifndef GAMMARAYPREMIUM_GR_PROFILE_H
#define GAMMARAYPREMIUM_GR_PROFILE_H

#include <string>
#include <cstdint>
#include <json/json.h>

namespace tc
{

    class GrProfile {
    public:
        // mongodb id
        std::string db_id_;

        //
        std::string pr_id_;

        //
        std::string pr_name_;

        //
        std::string pr_nick_name_;

    public:
        [[nodiscard]] Json::Value AsJson();

    };

}

#endif //GAMMARAYPREMIUM_GR_PROFILE_H
