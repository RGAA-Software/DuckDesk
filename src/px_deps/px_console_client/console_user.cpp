//
// Created by RGAA on 31/10/2025.
//

#include "console_user.h"

#include <nlohmann/json.hpp>

#include "px_common/log.h"

using namespace nlohmann;

namespace px_console {

std::shared_ptr<ConsoleUser> ConsoleUser::FromJson(const std::string& json_str) {
    try {
        const auto profile = json::parse(json_str);
        return FromObj(profile);
    } catch (const std::exception& error) {
        LOGE("Parse user failed: {}", error.what());
        return nullptr;
    }
}

std::shared_ptr<ConsoleUser> ConsoleUser::FromObj(const json& profile) {
    try {
        auto user = std::make_shared<ConsoleUser>();
        user->uid_ = profile[kUserId].get<std::string>();
        user->username_ = profile[kUserName].get<std::string>();
        // Password verifiers are deliberately absent from Console responses.
        user->password_.clear();
        user->assigned_ = true;
        user->created_timestamp_ = 0;
        user->updated_timestamp_ = 0;
        user->deleted_ = false;
        user->avatar_path_ = profile.contains(kUserAvatarPath) && profile.at(kUserAvatarPath).is_string()
                                 ? profile.at(kUserAvatarPath).get<std::string>()
                                 : std::string{};
        user->auth_version_ = profile.value(kUserAuthVersion, 0LL);
        user->must_change_password_ = profile.value(kUserMustChangePassword, false);
        user->version_ = profile.value(kUserVersion, 0LL);
        return user;
    } catch (const std::exception& error) {
        LOGE("Parse user failed: {}", error.what());
        return nullptr;
    }
}

std::string ConsoleUser::AsJson() {
    const json profile;
    return profile.dump();
}

std::string ConsoleUser::Dump() {
    std::ostringstream oss;
    oss << std::left;
    oss << std::setw(22) << "uid:" << uid_ << "\n";
    oss << std::setw(22) << "username:" << username_ << "\n";
    oss << std::setw(22) << "assigned:" << assigned_ << "\n";
    oss << std::setw(22) << "created_timestamp:" << created_timestamp_ << "\n";
    oss << std::setw(22) << "updated_timestamp:" << updated_timestamp_ << "\n";
    oss << std::setw(22) << "deleted:" << (deleted_ ? "true" : "false") << "\n";
    oss << std::setw(22) << "avatar_path:" << avatar_path_ << "\n";
    oss << std::setw(22) << "auth_version:" << auth_version_ << "\n";
    oss << std::setw(22) << "must_change_password:" << (must_change_password_ ? "true" : "false") << "\n";
    oss << std::setw(22) << "version:" << version_ << "\n";
    return oss.str();
}

}  // namespace px_console
