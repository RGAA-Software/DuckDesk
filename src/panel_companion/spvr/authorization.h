//
// Created by RGAA on 29/08/2025.
//

#ifndef GAMMARAYPREMIUM_AUTHORIZATION_H
#define GAMMARAYPREMIUM_AUTHORIZATION_H

#include <string>
#include <cstdint>

namespace tc
{

    class Authorization {
    public:
        std::string auth_id;
        std::string auth_name;
        std::string machine_code;
        std::string description;
        int32_t max_streams;
        std::string appkey;
        std::string app_secret;
        std::string username;
        std::string password;

        int64_t created_timestamp;
        int64_t end_timestamp;

        int32_t days;
        std::string verify_server;
    };

}

#endif //GAMMARAYPREMIUM_AUTHORIZATION_H
