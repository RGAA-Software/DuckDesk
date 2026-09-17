//
// Created by RGAA on 11/06/2025.
//

#include "render_api.h"

#include <nlohmann/json.hpp>

#include "px_common/http_client.h"
#include "px_common/log.h"
#include "px_common/md5.h"

using namespace nlohmann;

namespace px {

const std::string kApiVerifySecurityPassword = "/verify/security/password";
const std::string kApiGetRenderConfiguration = "/get/render/configuration";

Result<RenderConfiguration, int> RenderApi::GetRenderConfiguration(
    const std::string& host, int port) {
    const auto client =
        HttpClient::Make(host, port, kApiGetRenderConfiguration);
    if (!client) return ErrInt<RenderConfiguration>(-1);
    const auto response = client->Request({});

    LOGI("Render configuration returned HTTP {}", response.status);
    if (response.status != 200 || response.body.empty()) {
        return ErrInt<RenderConfiguration>(response.status);
    }

    try {
        const auto response_json = json::parse(response.body);
        if (response_json["code"].get<int>() == 200) {
            const auto& configuration_json = response_json["data"];
            RenderConfiguration configuration;
            configuration.device_id_ =
                configuration_json["device_id"].get<std::string>();
            configuration.relay_host_ =
                configuration_json["relay_host"].get<std::string>();
            configuration.relay_port_ =
                configuration_json["relay_port"].get<int>();
            configuration.access_policy_known_ =
                configuration_json.contains("incoming_remote_access_enabled") &&
                configuration_json["incoming_remote_access_enabled"]
                    .is_boolean() &&
                configuration_json.contains("file_transfer_enabled") &&
                configuration_json["file_transfer_enabled"].is_boolean();
            configuration.incoming_remote_access_enabled_ =
                configuration_json.value("incoming_remote_access_enabled",
                                         false);
            configuration.file_transfer_enabled_ =
                configuration_json.value("file_transfer_enabled", false);
            configuration.controller_availability_known_ =
                configuration_json.contains("controller_availability_known") &&
                configuration_json["controller_availability_known"]
                    .is_boolean() &&
                configuration_json["controller_availability_known"]
                    .get<bool>() &&
                configuration_json.contains("controller_available") &&
                configuration_json["controller_available"].is_boolean() &&
                configuration_json.contains("controller_reconnect_grace") &&
                configuration_json["controller_reconnect_grace"].is_boolean() &&
                configuration_json.contains("controller_retry_after_ms") &&
                configuration_json["controller_retry_after_ms"]
                    .is_number_integer();
            configuration.controller_available_ =
                configuration_json.value("controller_available", false);
            configuration.controller_reconnect_grace_ =
                configuration_json.value("controller_reconnect_grace", false);
            configuration.controller_retry_after_ms_ = std::max<std::int64_t>(
                0, configuration_json.value("controller_retry_after_ms",
                                            std::int64_t{}));
            return configuration;
        }
    } catch (const std::exception& exception) {
        LOGE("Parse json failed: {}, body: {}", exception.what(),
             response.body);
    }

    return ErrInt<RenderConfiguration>(-1);
}

Result<bool, int> RenderApi::VerifySecurityPassword(
    const std::string& host, int port, const std::string& safety_pwd_md5) {
    const auto client =
        HttpClient::Make(host, port, kApiVerifySecurityPassword);
    if (!client) return ErrInt<bool>(-1);
    const auto response = client->Request({{"safety_pwd_md5", safety_pwd_md5}});

    LOGI("code: {}, msg: {}", response.status, response.body);
    if (response.status != 200 || response.body.empty()) {
        return ErrInt<bool>(response.status);
    }

    try {
        const auto response_json = json::parse(response.body);
        return response_json.value("code", -1) == 200;
    } catch (const std::exception& exception) {
        LOGE("Parse Render password verification response failed: {}",
             exception.what());
    }

    return ErrInt<bool>(-1);
}

}  // namespace px
