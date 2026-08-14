//
// Created by RGAA on 31/10/2025.
//

#ifndef GAMMARAYPREMIUM_SPVR_USER_H
#define GAMMARAYPREMIUM_SPVR_USER_H

#include <string>
#include <memory>
#include <nlohmann/json.hpp>

using namespace nlohmann;

namespace spvr
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
    const std::string kUserAdministrator = "administrator";
    const std::string kPage = "page";
    const std::string kPageSize = "page_size";

    class SpvrUser {
    public:
        // obj["data"]["xx"]
        static std::shared_ptr<SpvrUser> FromJson(const std::string& json_str);
        // obj["xx"]
        static std::shared_ptr<SpvrUser> FromObj(const json& obj);
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
        bool administrator_;
    };

    using SpvrUserPtr = std::shared_ptr<SpvrUser>;

}

#endif //GAMMARAYPREMIUM_SPVR_USER_H
