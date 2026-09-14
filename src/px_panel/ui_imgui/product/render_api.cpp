//
// Created by RGAA on 11/06/2025.
//

#include "render_api.h"
#include "px_common/md5.h"
#include "px_common/http_client.h"
#include "px_common/log.h"
#include <nlohmann/json.hpp>

using namespace nlohmann;

namespace px {

const std::string kApiVerifySecurityPassword = "/verify/security/password";
const std::string kApiGetRenderConfiguration = "/get/render/configuration";

Result<RenderConfiguration, int> RenderApi::GetRenderConfiguration(const std::string& host, int port) {
    const auto client = HttpClient::Make(host, port, kApiGetRenderConfiguration);
    if (!client)
        return ErrInt<RenderConfiguration>(-1);
    const auto r = client->Request({});

    LOGI("Render configuration returned HTTP {}", r.status);
    if (r.status != 200 || r.body.empty()) {
        return ErrInt<RenderConfiguration>(r.status);
    }

    try {
        const auto obj = json::parse(r.body);
        if (obj["code"].get<int>() == 200) {
            const auto& data = obj["data"];
            RenderConfiguration rc;
            rc.device_id_ = data["device_id"].get<std::string>();
            rc.relay_host_ = data["relay_host"].get<std::string>();
            rc.relay_port_ = data["relay_port"].get<int>();
            rc.access_policy_known_ = data.contains("incoming_remote_access_enabled") && data["incoming_remote_access_enabled"].is_boolean() &&
                                      data.contains("file_transfer_enabled") && data["file_transfer_enabled"].is_boolean();
            rc.incoming_remote_access_enabled_ = data.value("incoming_remote_access_enabled", false);
            rc.file_transfer_enabled_ = data.value("file_transfer_enabled", false);
            rc.controller_availability_known_ = data.contains("controller_availability_known") &&
                                                data["controller_availability_known"].is_boolean() &&
                                                data["controller_availability_known"].get<bool>() && data.contains("controller_available") &&
                                                data["controller_available"].is_boolean() && data.contains("controller_reconnect_grace") &&
                                                data["controller_reconnect_grace"].is_boolean() && data.contains("controller_retry_after_ms") &&
                                                data["controller_retry_after_ms"].is_number_integer();
            rc.controller_available_ = data.value("controller_available", false);
            rc.controller_reconnect_grace_ = data.value("controller_reconnect_grace", false);
            rc.controller_retry_after_ms_ = std::max<std::int64_t>(0, data.value("controller_retry_after_ms", std::int64_t{}));
            return rc;
        }
    } catch (const std::exception& e) {
        LOGE("Parse json failed: {}, body: {}", e.what(), r.body);
    }

    return ErrInt<RenderConfiguration>(-1);
}

Result<bool, int> RenderApi::VerifySecurityPassword(const std::string& host, int port, const std::string& safety_pwd_md5) {
    const auto client = HttpClient::Make(host, port, kApiVerifySecurityPassword);
    if (!client)
        return ErrInt<bool>(-1);
    const auto r = client->Request({{"safety_pwd_md5", safety_pwd_md5}});

    LOGI("code: {}, msg: {}", r.status, r.body);
    if (r.status != 200 || r.body.empty()) {
        return ErrInt<bool>(r.status);
    }

    try {
        const auto obj = json::parse(r.body);
        return obj.value("code", -1) == 200;
    } catch (const std::exception& e) {
        LOGE("Parse Render password verification response failed: {}", e.what());
    }

    return ErrInt<bool>(-1);
}

} // namespace px
