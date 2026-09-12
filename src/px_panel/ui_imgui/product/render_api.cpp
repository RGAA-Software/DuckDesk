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

        LOGI("Render password verification returned HTTP {}", r.status);
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
            return obj.value("code", -1) == 200;
        } catch(std::exception& e) {
            LOGE("Parse Render password verification response failed: {}", e.what());
        }

        return ErrInt<bool>(-1);
    }

}
