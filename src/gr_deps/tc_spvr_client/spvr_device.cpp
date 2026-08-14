//
// Created by RGAA on 28/11/2025.
//

#include "spvr_device.h"
#include "spvr_errors.h"
#include <nlohmann/json.hpp>
#include "tc_common_new/log.h"

using namespace nlohmann;

namespace spvr
{

    std::shared_ptr<SpvrDevice> SpvrDevice::FromJson(const std::string& body) {
        try {
            auto obj = json::parse(body);
            return FromObj(obj);
        }
        catch(std::exception& e) {
            LOGE("ParseJsonAsDevice failed: {}, message: {}", e.what(), body);
            return nullptr;
        }
    }

    std::shared_ptr<SpvrDevice> SpvrDevice::FromObj(const json& obj) {
        try {
            auto device = std::make_shared<SpvrDevice>();
            device->device_id_ = obj[kDeviceId].get<std::string>();
            device->device_name_ = obj[kDeviceName].get<std::string>();
            device->logged_in_user_id_ = obj[kDeviceLoggedInUser].get<std::string>();
            device->seed_ = obj[kDeviceSeed].get<std::string>();
            device->random_pwd_md5_ = obj[kDeviceRandomPwd].get<std::string>();
            device->gen_random_pwd_ = obj[kGenRandomPwd].get<std::string>();
            device->safety_pwd_md5_ = obj[kDeviceSafetyPwd].get<std::string>();
            device->used_time_ = obj[kUsedTime].get<int64_t>();
            device->created_timestamp_ = obj[kDeviceCreatedTimestamp].get<int64_t>();
            device->last_update_timestamp_ = obj[kDeviceUpdatedTimestamp].get<int64_t>();
            device->desktop_link_ = obj[kDeviceDesktopLink].get<std::string>();
            device->desktop_link_raw_ = obj[kDeviceDesktopLinkRaw].get<std::string>();
            device->active_ = obj[kDeviceActive].get<bool>();
            return device;
        }
        catch (const std::exception& e) {
            LOGE("Parse device failed: {}", e.what());
            return nullptr;
        }
    }

    std::string SpvrDevice::Dump() {
        std::ostringstream oss;
        oss << std::left;
        oss << std::setw(22) << "device_id:"           << device_id_ << "\n";
        oss << std::setw(22) << "device_name:"         << device_name_ << "\n";
        oss << std::setw(22) << "logged_in_user_id:"   << logged_in_user_id_ << "\n";
        oss << std::setw(22) << "seed:"                << seed_ << "\n";
        oss << std::setw(22) << "created_timestamp:"   << created_timestamp_ << "\n";
        oss << std::setw(22) << "last_update_timestamp:" << last_update_timestamp_ << "\n";
        oss << std::setw(22) << "random_pwd_md5:"      << random_pwd_md5_ << "\n";
        oss << std::setw(22) << "safety_pwd_md5:"      << safety_pwd_md5_ << "\n";
        oss << std::setw(22) << "used_time:"           << used_time_ << "\n";
        oss << std::setw(22) << "gen_random_pwd:"      << gen_random_pwd_ << "\n";
        oss << std::setw(22) << "desktop_link:"        << desktop_link_ << "\n";
        oss << std::setw(22) << "desktop_link_raw:"    << desktop_link_raw_ << "\n";
        oss << std::setw(22) << "active:"              << active_ << "\n";
        return oss.str();
    }

}