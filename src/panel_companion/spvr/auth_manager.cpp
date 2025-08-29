//
// Created by RGAA on 29/08/2025.
//

#include "auth_manager.h"
#include "spvr_setting.h"
#include "tc_common_new/http_client.h"
#include "panel_companion/panel_companion_impl.h"
#include "tc_common_new/log.h"

namespace tc
{

    AuthManager::AuthManager(PanelCompanionImpl* pc) {
        pc_ = pc;
    }

    void AuthManager::OnTimer5S() {
        pc_->PostNetTask([=, this]() {
            this->RequestAuth();
        });
    }

    void AuthManager::RequestAuth() {
        auto settings = SpvrSettings::Instance();
        if (settings->host_.empty() || settings->port_ <= 0) {
            return;
        }

        auto client = HttpClient::MakeSSL(settings->host_, settings->port_, "/api/v1/auth/control/get/authorization");
        auto resp = client->Request();
        if (resp.status != 200) {
            return;
        }

        try {
            LOGI("auth: {}", resp.body);
        } catch (...) {

        }

    }

    std::shared_ptr<Authorization> AuthManager::GetAuth() {
        return auth_.Load();
    }

}