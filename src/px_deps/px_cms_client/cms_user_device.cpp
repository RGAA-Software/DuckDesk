//
// Created by RGAA on 28/11/2025.
//

#include "cms_user_device.h"
#include "px_common_new/log.h"
#include <nlohmann/json.hpp>
#include "cms_user.h"
#include "cms_device.h"

using namespace nlohmann;

namespace px_cms
{

    std::shared_ptr<CmsUserDevice> CmsUserDevice::FromJson(const std::string& body) {
        try {
            json obj = json::parse(body);
            return FromObj(obj);
        }
        catch (const std::exception& e) {
            LOGE("CmsUserDevice parse failed: {}", e.what());
            return nullptr;
        }
    }

    std::shared_ptr<CmsUserDevice> CmsUserDevice::FromObj(const json& obj) {
        try {
            auto ud = std::make_shared<CmsUserDevice>();
            ud->uid_ = obj[kUserId].get<std::string>();
            ud->device_id_ = obj[kDeviceId].get<std::string>();
            ud->created_ts_ = obj["created_ts"].get<int64_t>();
            ud->created_ts_readable_ = obj["created_ts_readable"].get<std::string>();
            ud->user_ = CmsUser::FromObj(obj["user"]);
            ud->device_ = CmsDevice::FromObj(obj["device"]);
            return ud;
        }
        catch (const std::exception& e) {
            LOGE("CmsUserDevice parse failed: {}", e.what());
            return nullptr;
        }
    }

    std::string CmsUserDevice::Dump() {
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