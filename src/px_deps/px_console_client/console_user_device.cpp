//
// Created by RGAA on 28/11/2025.
//

#include "console_user_device.h"

#include <nlohmann/json.hpp>

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
        auto device = std::make_shared<ConsoleUserDevice>();
        device->device_id_ = device_profile.value("id", "");
        device->public_code_ = device_profile.value("public_code", "");
        device->device_name_ = device_profile.value("name", "");
        device->platform_ = device_profile.value("platform", "");
        device->disabled_ = device_profile.value("disabled", true);
        device->revision_ = device_profile.value("revision", 0LL);
        device->registered_at_ = device_profile.value("registered_at", "");
        if (device->device_id_.empty() || device->public_code_.empty() || device->device_name_.empty() || device->platform_.empty() ||
            device->revision_ <= 0 || device->registered_at_.empty()) {
            return nullptr;
        }
        return device;
    } catch (const std::exception& error) {
        LOGE("ConsoleUserDevice parse failed: {}", error.what());
        return nullptr;
    }
}

std::string ConsoleUserDevice::Dump() {
    std::ostringstream oss;
    oss << std::left;
    oss << std::setw(22) << "device_id:" << device_id_ << "\n";
    oss << std::setw(22) << "public_code:" << public_code_ << "\n";
    oss << std::setw(22) << "name:" << device_name_ << "\n";
    oss << std::setw(22) << "platform:" << platform_ << "\n";
    oss << std::setw(22) << "disabled:" << disabled_ << "\n";
    oss << std::setw(22) << "revision:" << revision_ << "\n";
    oss << std::setw(22) << "registered_at:" << registered_at_ << "\n";
    return oss.str();
}

}  // namespace px_console
