//
// Created by RGAA on 31/10/2025.
//

#include "console_user.h"
#include <nlohmann/json.hpp>
#include "px_common_new/log.h"

using namespace nlohmann;

namespace px_console
{

    std::shared_ptr<ConsoleUser> ConsoleUser::FromJson(const std::string& json_str) {
        try {
            auto obj = json::parse(json_str);
            return FromObj(obj);
        }
        catch (const std::exception& e) {
            LOGE("Parse user failed: {}", e.what());
            return nullptr;
        }
    }

    std::shared_ptr<ConsoleUser> ConsoleUser::FromObj(const json& obj) {
        try {
            auto user = std::make_shared<ConsoleUser>();
            user->uid_ = obj[kUserId].get<std::string>();
            user->username_ = obj[kUserName].get<std::string>();
            // Password verifiers are deliberately absent from Console responses.
            user->password_.clear();
            user->assigned_ = obj[kUserAssigned].get<bool>();
            user->created_timestamp_ = obj[kUserCreatedTimestamp].get<int64_t>();
            user->updated_timestamp_ = obj[kUserUpdateTimestamp].get<int64_t>();
            user->deleted_ = obj[kUserDeleted].get<bool>();
            user->avatar_path_ = obj[kUserAvatarPath].get<std::string>();
            user->auth_version_ = obj.value(kUserAuthVersion, 0LL);
            user->must_change_password_ = obj.value(kUserMustChangePassword, false);
            user->version_ = obj.value(kUserVersion, 0LL);
            return user;
        }
        catch (const std::exception& e) {
            LOGE("Parse user failed: {}", e.what());
            return nullptr;
        }
    }

    std::string ConsoleUser::AsJson() {
        json obj;
        return obj.dump();
    }

    std::string ConsoleUser::Dump() {
        std::ostringstream oss;
        oss << std::left;
        oss << std::setw(22) << "uid:"                << uid_ << "\n";
        oss << std::setw(22) << "username:"           << username_ << "\n";
        oss << std::setw(22) << "assigned:"           << assigned_ << "\n";
        oss << std::setw(22) << "created_timestamp:"  << created_timestamp_ << "\n";
        oss << std::setw(22) << "updated_timestamp:"  << updated_timestamp_ << "\n";
        oss << std::setw(22) << "deleted:"            << (deleted_ ? "true" : "false") << "\n";
        oss << std::setw(22) << "avatar_path:"        << avatar_path_ << "\n";
        oss << std::setw(22) << "auth_version:"       << auth_version_ << "\n";
        oss << std::setw(22) << "must_change_password:" << (must_change_password_ ? "true" : "false") << "\n";
        oss << std::setw(22) << "version:"            << version_ << "\n";
        return oss.str();
    }

}
