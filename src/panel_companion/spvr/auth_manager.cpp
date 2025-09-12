//
// Created by RGAA on 29/08/2025.
//

#include "auth_manager.h"
#include "spvr_setting.h"
#include "json/json.hpp"
#include "tc_common_new/log.h"
#include "tc_common_new/http_client.h"
#include "tc_common_new/shared_preference.h"
#include "panel_companion/panel_companion_impl.h"

static const std::string kAuthId = "key_auth_id";
static const std::string kAuthAppkey = "key_auth_appkey";

namespace tc
{

    using namespace nlohmann;

    AuthManager::AuthManager(PanelCompanionImpl* pc) {
        pc_ = pc;
    }

    void AuthManager::OnTimer5S() {
        pc_->PostNetTask([=, this]() {
            if (auto auth = this->RequestAuth(); auth && !auth->appkey_.empty()) {
                this->auth_.Update(auth);
                this->FlushToStorage();
            }
        });
    }

    void AuthManager::LoadFromStorage() {
        auto auth_id = pc_->GetSP()->Get(kAuthId);
        auto appkey = pc_->GetSP()->Get(kAuthAppkey);
        if (auth_id.empty() || appkey.empty()) {
            return;
        }
        auto auth = std::make_shared<Authorization>();
        auth->auth_id_ = auth_id;
        auth->appkey_ = appkey;
        this->auth_.Update(auth);
        LOGI("Load auth from storage: auth id: {}, appkey: {} ", auth_id, appkey);
    }

    void AuthManager::FlushToStorage() {
        this->auth_.WithLock([=, this](std::shared_ptr<Authorization>& auth) {
            if (auth->auth_id_.empty() || auth->appkey_.empty()) {
                return;
            }
            auto sp = pc_->GetSP();
            sp->Put(kAuthId, auth->auth_id_);
            sp->Put(kAuthAppkey, auth->appkey_);
        });
    }

    std::shared_ptr<Authorization> AuthManager::RequestAuth() {
        auto settings = SpvrSettings::Instance();
        if (settings->host_.empty() || settings->port_ <= 0) {
            return nullptr;
        }

        auto client = HttpClient::MakeSSL(settings->host_, settings->port_, "/api/v1/auth/control/get/authorization", 2000);
        auto resp = client->Request();
        if (resp.status != 200) {
            return nullptr;
        }

        try {
            LOGI("auth: {}", resp.body);
            auto auth = std::make_shared<Authorization>();
            auto value = json::parse(resp.body);
            auth->auth_id_ = value["data"]["auth_id"].get<std::string>();
            auth->appkey_ = value["data"]["appkey"].get<std::string>();
            this->auth_.Update(auth);
            this->FlushToStorage();
            return auth;
        } catch (std::exception& e) {
            LOGE("Parse auth failed: {}", e.what());
        }
        return nullptr;
    }

    std::shared_ptr<Authorization> AuthManager::GetAuth() {
        return auth_.Load();
    }

}