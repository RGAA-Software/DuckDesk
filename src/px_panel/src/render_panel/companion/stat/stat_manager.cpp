//
// Created by RGAA on 8/02/2026.
//

#include "stat_manager.h"
#include <format>
#include <nlohmann/json.hpp>
#include "px_common_new/log.h"
#include "px_common_new/http_client.h"
#include "px_common_new/shared_preference.h"
#include "px_common_new/const_auto.h"
#include "../panel_companion_impl.h"

using namespace nlohmann;

#define LOCAL_HOST 0
#if LOCAL_HOST
cat sHost = "127.0.0.1";
#else
cat sHost = "pixels.yun";
#endif

namespace px
{

    static const std::string kUpdateAuthStat = "/api/v1/insert/update/auth/stat";
    static const std::string kOpenUp = "/api/v1/open/up";

    static const std::string kToken = "G7pK3mR9tY5xW2vS8qL6nZ4bC1dF0jH5uA3oP8iM7cX9wE2rT4yU6iJ1hN5gB8vF3dS6kL9pM2aQ4zX7wC0vB5nK8jH3mG6fD9sA";

    StatManager::StatManager(PanelCompanionImpl *impl) {
        impl_ = impl;
    }

    bool StatManager::ReportWorkingAuth(const std::shared_ptr<SysInfo>& info) {
        cat host = sHost;//"pixels.yun";
        cat port = 30300;
        cat client = HttpClient::MakeSSL(host, port, kUpdateAuthStat, 5000);
        json obj;
        obj["auth_id"] = impl_->GetAuthId();
        obj["auth_name"] = impl_->GetAuthName();
        obj["auth_machine_code"] = impl_->GetMachineCode();
        obj["sys_info"] = info->AsSimpleInfo();

        cat response = client->Post({
            {"token", kToken}
        }, obj.dump());
        if (response.status != 200) {
            LOGE("ReportWorkingAuth failed, status: {}, error: {}", response.status, response.error_message);
            return false;
        }

        try {
            LOGI("report auth: {}", response.body);
            cat value = json::parse(response.body);
            if (!value["data"].is_null()) {
                return true;
            }
            return false;
        } catch (std::exception& e) {
            LOGE("Parse auth failed: {}", e.what());
            return false;
        }
    }

    bool StatManager::ReportOpenUp(const std::shared_ptr<SysInfo>& info) {
        cat host = sHost;
        cat port = 30300;
        cat client = HttpClient::MakeSSL(host, port, kOpenUp, 5000);
        json obj;
        obj["device_id"] = impl_->GetDeviceId();
        obj["sys_info"] = info->AsSimpleInfo();

        cat response = client->Post({
            {"token", kToken}
        }, obj.dump());
        if (response.status != 200) {
            LOGE("ReportOpenUp failed, status: {}, error: {}", response.status, response.error_message);
            return false;
        }

        try {
            LOGI("report auth: {}", response.body);
            cat value = json::parse(response.body);
            if (!value["data"].is_null()) {
                return true;
            }
            return false;
        } catch (std::exception& e) {
            LOGE("Parse auth failed: {}", e.what());
            return false;
        }
    }

}
