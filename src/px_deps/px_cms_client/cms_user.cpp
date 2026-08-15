//
// Created by RGAA on 31/10/2025.
//

#include "cms_user.h"
#include <nlohmann/json.hpp>
#include "px_common_new/log.h"

using namespace nlohmann;

namespace px_cms
{

    std::shared_ptr<CmsUser> CmsUser::FromJson(const std::string& json_str) {
        LOGI("Will parse json: {}", json_str);
        try {
            auto obj = json::parse(json_str);
            return FromObj(obj);
        }
        catch (const std::exception& e) {
            LOGE("Parse user failed: {}", e.what());
            return nullptr;
        }
    }

    std::shared_ptr<CmsUser> CmsUser::FromObj(const json& obj) {
        try {
            auto user = std::make_shared<CmsUser>();
            user->uid_ = obj[kUserId].get<std::string>();
            user->username_ = obj[kUserName].get<std::string>();
            user->password_ = obj[kUserPassword].get<std::string>();
            user->assigned_ = obj[kUserAssigned].get<bool>();
            user->created_timestamp_ = obj[kUserCreatedTimestamp].get<int64_t>();
            user->updated_timestamp_ = obj[kUserUpdateTimestamp].get<int64_t>();
            user->deleted_ = obj[kUserDeleted].get<bool>();
            user->avatar_path_ = obj[kUserAvatarPath].get<std::string>();
            user->administrator_ = obj[kUserAdministrator].get<bool>();
            return user;
        }
        catch (const std::exception& e) {
            LOGE("Parse user failed: {}", e.what());
            return nullptr;
        }
    }

    std::string CmsUser::AsJson() {
        json obj;
        return obj.dump();
    }

    std::string CmsUser::Dump() {
        std::ostringstream oss;
        oss << std::left;
        oss << std::setw(22) << "uid:"                << uid_ << "\n";
        oss << std::setw(22) << "username:"           << username_ << "\n";
        oss << std::setw(22) << "password:"           << password_ << "\n";
        oss << std::setw(22) << "assigned:"           << assigned_ << "\n";
        oss << std::setw(22) << "created_timestamp:"  << created_timestamp_ << "\n";
        oss << std::setw(22) << "updated_timestamp:"  << updated_timestamp_ << "\n";
        oss << std::setw(22) << "deleted:"            << (deleted_ ? "true" : "false") << "\n";
        oss << std::setw(22) << "avatar_path:"        << avatar_path_ << "\n";
        oss << std::setw(22) << "administrator:"      << (administrator_ ? "true" : "false") << "\n";
        return oss.str();
    }

}