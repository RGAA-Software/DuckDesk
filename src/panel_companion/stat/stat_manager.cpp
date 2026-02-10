//
// Created by RGAA on 8/02/2026.
//

#include "stat_manager.h"
#include <format>
#include "json/json.hpp"
#include "tc_common_new/log.h"
#include "tc_common_new/http_client.h"
#include "tc_common_new/shared_preference.h"
#include "tc_common_new/const_auto.h"
#include "panel_companion/panel_companion_impl.h"

using namespace nlohmann;

namespace tc
{

    static const std::string kUpdateAuthStat = "/api/v1/insert/update/auth/stat";

    static const std::string kToken = "G7pK3mR9tY5xW2vS8qL6nZ4bC1dF0jH5uA3oP8iM7cX9wE2rT4yU6iJ1hN5gB8vF3dS6kL9pM2aQ4zX7wC0vB5nK8jH3mG6fD9sA";

    StatManager::StatManager(PanelCompanionImpl *impl) {
        impl_ = impl;
    }

    bool StatManager::ReportWorkingAuth(const std::shared_ptr<SysInfo>& info) {
        cat host = "godesk.uk";
        cat port = 443;
        cat client = HttpClient::MakeSSL(host, port, kUpdateAuthStat, 5000);
        json obj;
        obj["auth_id"] = impl_->GetAuthId();
        obj["auth_name"] = impl_->GetAuthName();
        obj["auth_machine_code"] = impl_->GetMachineCode();
        obj["sys_info"] = std::format("os:{}-cpu:{}-cpu core size:{}-cpu frequency:{}-memory:{}GB",
            info->os_.sys_os_long_version_, info->cpu_.brand_, info->cpu_.cpus_.size(), info->cpu_.base_frequency_, info->mem_.total_gb_);

        cat resp = client->Post({
            {"token", kToken}
        }, obj.dump());
        if (resp.status != 200) {
            LOGE("ReportWorkingAuth: {}", resp.status);
            return false;
        }

        try {
            LOGI("report auth: {}", resp.body);
            cat value = json::parse(resp.body);
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
