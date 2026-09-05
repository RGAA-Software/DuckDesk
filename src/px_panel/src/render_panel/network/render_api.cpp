//
// Created by RGAA on 11/06/2025.
//

#include "render_api.h"
#include "px_common/md5.h"
#include "px_common/http_client.h"
#include "px_common/log.h"
#include <nlohmann/json.hpp>

using namespace nlohmann;

namespace px
{

    const std::string kApiVerifySecurityPassword = "/verify/security/password";
    const std::string kApiGetRenderConfiguration = "/get/render/configuration";

    Result<RenderConfiguration, int> RenderApi::GetRenderConfiguration(const std::string& host, int port) {
        auto client = HttpClient::Make(host, port, kApiGetRenderConfiguration);
        auto r = client->Request({});

        LOGI("code: {}, msg: {}", r.status, r.body);
        if (r.status != 200 || r.body.empty()) {
            return ErrInt<RenderConfiguration>(r.status);
        }

        try {
            auto obj = json::parse(r.body);
            if (obj["code"].get<int>() == 200) {
                RenderConfiguration rc;
                rc.device_id_ = obj["data"]["device_id"].get<std::string>();
                rc.relay_host_ = obj["data"]["relay_host"].get<std::string>();
                rc.relay_port_ = obj["data"]["relay_port"].get<int>();
                return rc;
            }
        } catch(std::exception& e) {
            LOGE("Parse json failed: {}, body: {}", e.what(), r.body);
        }

        return ErrInt<RenderConfiguration>(-1);
    }

    Result<bool, int> RenderApi::VerifySecurityPassword(const std::string& host, int port, const std::string& safety_pwd_md5) {
        auto client = HttpClient::Make(host, port, kApiVerifySecurityPassword);
        auto r = client->Request({{
            "safety_pwd_md5", safety_pwd_md5
        }});

        LOGI("code: {}, msg: {}", r.status, r.body);
        if (r.status != 200 || r.body.empty()) {
            return ErrInt<bool>(r.status);
        }

        try {
            auto obj = json::parse(r.body);
            if (obj["code"].get<int>() == 200) {
                return true;
            }
        } catch(std::exception& e) {
            LOGE("Parse json failed: {}, body: {}", e.what(), r.body);
        }

        return ErrInt<bool>(-1);
    }

    Result<IpDirectLaunch, int> RenderApi::PrepareIpDirectLaunch(
        const std::string& host,
        const int port,
        const std::string& safety_pwd_md5,
        const std::string& client_nonce) {
        auto client = HttpClient::Make(host, port, kApiVerifySecurityPassword);
        const auto response = client->Request({
            {"safety_pwd_md5", safety_pwd_md5},
            {"client_nonce", client_nonce},
        });
        LOGI("IP direct pre-authorization response status: {}", response.status);
        if (response.status != 200 || response.body.empty()) {
            return ErrInt<IpDirectLaunch>(response.status);
        }
        try {
            const auto object = json::parse(response.body);
            if (object.value("code", -1) != 200
                || !object.contains("data") || !object["data"].is_object()) {
                return ErrInt<IpDirectLaunch>(-1);
            }
            IpDirectLaunch launch;
            launch.stream_id_ = object["data"].value("stream_id", "");
            launch.expires_at_ms_ = object["data"].value("expires_at_ms", 0LL);
            if (launch.stream_id_.empty()) {
                return ErrInt<IpDirectLaunch>(-1);
            }
            return launch;
        }
        catch (const std::exception& error) {
            LOGE("Parse IP direct pre-authorization failed: {}", error.what());
            return ErrInt<IpDirectLaunch>(-1);
        }
    }

}
