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
            ud->uid_ = obj.value(kUserId, "");
            ud->device_id_ = obj.value(kDeviceId, "");
            ud->created_ts_ = obj.value("created_ts", 0LL);
            ud->created_ts_readable_ = obj.value("created_ts_readable", "");
            if (obj.contains("user") && obj.contains("device")) {
                ud->user_ = CmsUser::FromObj(obj["user"]);
                ud->device_ = CmsDevice::FromObj(obj["device"]);
            }
            else {
                // Secure /api/v1/user/devices summary: deliberately has no
                // desktop link, password, uid or internal endpoint.
                ud->device_ = std::make_shared<CmsDevice>();
                ud->device_->device_id_ = ud->device_id_;
                ud->device_->device_name_ = obj.value("name", "");
                ud->device_->active_ = obj.value("online", false);
                ud->device_->last_update_timestamp_ = obj.value("last_seen_at", 0LL);
            }
            if (ud->device_id_.empty() || !ud->device_) {
                return nullptr;
            }
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
