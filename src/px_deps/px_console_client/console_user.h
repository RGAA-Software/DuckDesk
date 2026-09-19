//
// Created by RGAA on 31/10/2025.
//

#ifndef PIXELSPREMIUM_CONSOLE_USER_H
#define PIXELSPREMIUM_CONSOLE_USER_H

#include <memory>
#include <nlohmann/json.hpp>
#include <string>

using namespace nlohmann;

namespace px_console {

const std::string kUserId = "id";
const std::string kUserName = "username";
const std::string kUserHashPassword = "hash_password";
const std::string kUserNewHashPassword = "new_hash_password";
const std::string kUserPassword = "password";
const std::string kUserAssigned = "assigned";
const std::string kUserCreatedTimestamp = "created_timestamp";
const std::string kUserUpdateTimestamp = "update_timestamp";
const std::string kUserDeleted = "deleted";
const std::string kUserAvatarPath = "avatar_url";
const std::string kUserAuthVersion = "authorization_revision";
const std::string kUserMustChangePassword = "must_change_password";
const std::string kUserVersion = "revision";
const std::string kPage = "page";
const std::string kPageSize = "page_size";

class ConsoleUser {
public:
    // Parse one profile object.
    static std::shared_ptr<ConsoleUser> FromJson(const std::string& json_str);
    static std::shared_ptr<ConsoleUser> FromObj(const json& profile);
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

}  // namespace px_console

#endif  // PIXELSPREMIUM_CONSOLE_USER_H
