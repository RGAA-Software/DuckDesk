//
// Created by RGAA on 28/11/2025.
//

#include "console_user_device.h"

#include <nlohmann/json.hpp>

#include "console_device.h"
#include "console_user.h"
#include "px_common/log.h"

using namespace nlohmann;

namespace px_console {

std::shared_ptr<ConsoleUserDevice> ConsoleUserDevice::FromJson(const std::string& body) {
    try {
        const json device_profile = json::parse(body);
        return FromObj(device_profile);
    } catch (const std::exception& error) {
        LOGE("ConsoleUserDevice parse failed: {}", error.what());
        return nullptr;
    }
}

std::shared_ptr<ConsoleUserDevice> ConsoleUserDevice::FromObj(const json& device_profile) {
    try {
        auto ud = std::make_shared<ConsoleUserDevice>();
        ud->uid_.clear();
        ud->device_id_ = device_profile.value("id", "");
        ud->created_ts_ = device_profile.value("created_ts", 0LL);
        ud->created_ts_readable_ = device_profile.value("created_ts_readable", "");
        if (device_profile.contains("user") && device_profile.contains("device")) {
            ud->user_ = ConsoleUser::FromObj(device_profile["user"]);
            ud->device_ = ConsoleDevice::FromObj(device_profile["device"]);
        } else {
            // Current Console device summaries deliberately have no endpoint or password.
            ud->device_ = std::make_shared<ConsoleDevice>();
            ud->device_->device_id_ = ud->device_id_;
            ud->device_->device_name_ = device_profile.value("name", "");
            ud->device_->platform_ = device_profile.value("platform", "");
            ud->device_->active_ = !device_profile.value("disabled", true);
            ud->device_->last_update_timestamp_ = 0;
        }
        if (ud->device_id_.empty() || !ud->device_) {
            return nullptr;
        }
        return ud;
    } catch (const std::exception& error) {
        LOGE("ConsoleUserDevice parse failed: {}", error.what());
        return nullptr;
    }
}

std::string ConsoleUserDevice::Dump() {
    std::ostringstream oss;
    oss << std::left;
    oss << std::setw(22) << "uid:" << uid_ << "\n";
    oss << std::setw(22) << "device_id:" << device_id_ << "\n";
    oss << std::setw(22) << "created_ts:" << created_ts_ << "\n";
    oss << std::setw(22) << "created_ts_readable:" << created_ts_readable_ << "\n";
    if (user_) {
        oss << "User:" << std::endl;
        oss << user_->Dump();
    } else {
        oss << "No User" << std::endl;
    }

    if (device_) {
        oss << "Device:" << std::endl;
        oss << device_->Dump();
    } else {
        oss << "No Device" << std::endl;
    }
    return oss.str();
}

}  // namespace px_console
