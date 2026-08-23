//
// Created by RGAA on 31/10/2025.
//

#ifndef GAMMARAYPREMIUM_CONSOLE_USER_H
#define GAMMARAYPREMIUM_CONSOLE_USER_H

#include <string>
#include <memory>
#include <nlohmann/json.hpp>

using namespace nlohmann;

namespace px_console
{

    const std::string kUserId = "uid";
    const std::string kUserName = "username";
    const std::string kUserHashPassword = "hash_password";
    const std::string kUserNewHashPassword = "new_hash_password";
    const std::string kUserPassword = "password";
    const std::string kUserAssigned = "assigned";
    const std::string kUserCreatedTimestamp = "created_timestamp";
    const std::string kUserUpdateTimestamp = "update_timestamp";
    const std::string kUserDeleted = "deleted";
    const std::string kUserAvatarPath = "avatar_path";
    const std::string kUserAuthVersion = "auth_version";
    const std::string kUserMustChangePassword = "must_change_password";
    const std::string kUserVersion = "version";
    const std::string kPage = "page";
    const std::string kPageSize = "page_size";

    class ConsoleUser {
    public:
        // obj["data"]["xx"]
        static std::shared_ptr<ConsoleUser> FromJson(const std::string& json_str);
        // obj["xx"]
        static std::shared_ptr<ConsoleUser> FromObj(const json& obj);
        std::string AsJson();
        std::string Dump();

    public:
        std::string uid_;
        std::string username_;
        std::string password_;
        bool assigned_;
        int64_t created_timestamp_ = 0;
        int64_t updated_timestamp_ = 0;
        bool deleted_ = false;
        std::string avatar_path_;
        int64_t auth_version_ = 0;
        bool must_change_password_ = false;
        int64_t version_ = 0;
    };

    using ConsoleUserPtr = std::shared_ptr<ConsoleUser>;

    struct ConsoleUserLoginResult {
        ConsoleUserPtr user;
        std::string access_token;
        int64_t expires_at = 0;
        int64_t absolute_expires_at = 0;
    };

}

#endif //GAMMARAYPREMIUM_CONSOLE_USER_H
