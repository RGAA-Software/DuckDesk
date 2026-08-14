//
// Created by RGAA on 28/11/2025.
//

#include "spvr_user_device.h"
#include "tc_common_new/log.h"
#include <nlohmann/json.hpp>
#include "spvr_user.h"
#include "spvr_device.h"

using namespace nlohmann;

namespace spvr
{

    std::shared_ptr<SpvrUserDevice> SpvrUserDevice::FromJson(const std::string& body) {
        try {
            json obj = json::parse(body);
            return FromObj(obj);
        }
        catch (const std::exception& e) {
            LOGE("SpvrUserDevice parse failed: {}", e.what());
            return nullptr;
        }
    }

    std::shared_ptr<SpvrUserDevice> SpvrUserDevice::FromObj(const json& obj) {
        try {
            auto ud = std::make_shared<SpvrUserDevice>();
            ud->uid_ = obj[kUserId].get<std::string>();
            ud->device_id_ = obj[kDeviceId].get<std::string>();
            ud->created_ts_ = obj["created_ts"].get<int64_t>();
            ud->created_ts_readable_ = obj["created_ts_readable"].get<std::string>();
            ud->user_ = SpvrUser::FromObj(obj["user"]);
            ud->device_ = SpvrDevice::FromObj(obj["device"]);
            return ud;
        }
        catch (const std::exception& e) {
            LOGE("SpvrUserDevice parse failed: {}", e.what());
            return nullptr;
        }
    }

    std::string SpvrUserDevice::Dump() {
        std::ostringstream oss;
        oss << std::left;
        oss << std::setw(22) << "uid:"                 << uid_ << "\n";
        oss << std::setw(22) << "device_id:"           << device_id_ << "\n";
        oss << std::setw(22) << "created_ts:"          << created_ts_ << "\n";
        oss << std::setw(22) << "created_ts_readable:" << created_ts_readable_ << "\n";
        if (user_) {
            oss << "User:" << std::endl;
            oss << user_->Dump();
        }
        else {
            oss << "No User" << std::endl;
        }

        if (device_) {
            oss << "Device:" << std::endl;
            oss << device_->Dump();
        }
        else {
            oss << "No Device" << std::endl;
        }
        return oss.str();
    }

}