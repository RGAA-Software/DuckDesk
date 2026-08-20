//
// Created by RGAA on 31/10/2025.
//

#ifndef GAMMARAYPREMIUM_CMS_USER_H
#define GAMMARAYPREMIUM_CMS_USER_H

#include <string>
#include <memory>
#include <nlohmann/json.hpp>

using namespace nlohmann;

namespace px_cms
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
    const std::string kPage = "page";
    const std::string kPageSize = "page_size";

    class CmsUser {
    public:
        // obj["data"]["xx"]
        static std::shared_ptr<CmsUser> FromJson(const std::string& json_str);
        // obj["xx"]
        static std::shared_ptr<CmsUser> FromObj(const json& obj);
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
    };

    using CmsUserPtr = std::shared_ptr<CmsUser>;

    struct CmsUserLoginResult {
        CmsUserPtr user;
        std::string access_token;
        int64_t expires_at = 0;
        int64_t absolute_expires_at = 0;
    };

}

#endif //GAMMARAYPREMIUM_CMS_USER_H
