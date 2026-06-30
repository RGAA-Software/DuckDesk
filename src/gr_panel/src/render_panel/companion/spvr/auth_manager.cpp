//
// Created by RGAA on 29/08/2025.
//

#include "auth_manager.h"
#include "spvr_setting.h"
#include <nlohmann/json.hpp>
#include "auth_defs.h"
#include "tc_common_new/log.h"
#include "tc_common_new/http_client.h"
#include "tc_common_new/shared_preference.h"
#include "tc_common_new/const_auto.h"
#include "tc_common_new/time_util.h"
#include "../panel_companion_impl.h"

namespace tc
{

    using namespace nlohmann;

    // auth beg
    bool Authorization::IsFree() const {
        return role_ == AuthRole::kFree;
    }

    bool Authorization::IsPersonal() const {
        return role_ == AuthRole::kPersonal;
    }

    bool Authorization::IsEnterprise() const {
        return role_ == AuthRole::kEnterprise;
    }
    // auth end

    AuthManager::AuthManager(PanelCompanionImpl* pc) {
        pc_ = pc;
    }

    void AuthManager::OnTimer5S() {
        pc_->PostNetTask([=, this]() {
            if (const auto auth = this->RequestAuth(); auth && !auth->appkey_.empty()) {
                this->auth_.Update(auth);
                this->FlushToStorage();
            }
        });
    }

    void AuthManager::LoadFromStorage() {
        cat auth_id = pc_->GetSP()->Get(kAuthId);
        cat auth_name = pc_->GetSP()->Get(kAuthName);
        cat machine_code = pc_->GetSP()->Get(kAuthMachineCode);
        cat appkey = pc_->GetSP()->Get(kAuthAppkey);
        cat role = pc_->GetSP()->GetInt(kAuthRole);
        cat days = pc_->GetSP()->GetInt(kAuthDays);
        cat max_streams = pc_->GetSP()->GetInt(kAuthMaxStreams);
        cat end_timestamp_ms = pc_->GetSP()->GetInt64(kAuthEndTimestampMs);
        if (auth_id.empty() || appkey.empty()) {
            LOGW("No auth loaded from storage, id: {}, appkey: {}, role: {}", auth_id, appkey, role);
            return;
        }
        const auto auth = std::make_shared<Authorization>();
        auth->auth_id_ = auth_id;
        auth->auth_name_ = auth_name;
        auth->machine_code_ = machine_code;
        auth->appkey_ = appkey;
        auth->role_ = static_cast<AuthRole>(role);
        this->auth_.Update(auth);
        LOGI("Load auth from storage: auth id: {}, appkey: {}, role: {} ", auth_id, appkey, role);
    }

    void AuthManager::FlushToStorage() {
        this->auth_.WithLock([=, this](const std::shared_ptr<Authorization>& auth) {
            if (auth->auth_id_.empty() || auth->appkey_.empty()) {
                return;
            }
            const auto sp = pc_->GetSP();
            sp->Put(kAuthId, auth->auth_id_);
            sp->Put(kAuthName, auth->auth_name_);
            sp->Put(kAuthMachineCode, auth->machine_code_);
            sp->Put(kAuthAppkey, auth->appkey_);
            sp->PutInt(kAuthRole, static_cast<int>(auth->role_));
            sp->PutInt(kAuthDays, auth->days_);
            sp->PutInt(kAuthMaxStreams, auth->max_streams_);
            sp->PutInt64(kAuthEndTimestampMs, auth->end_timestamp_ms_);
        });
    }

    std::shared_ptr<Authorization> AuthManager::RequestAuth() {
        auto settings = SpvrSettings::Instance();
        if (settings->host_.empty() || settings->port_ <= 0) {
            return nullptr;
        }

        const auto path = std::format("/api/v1/auth/control/get/authorization?appkey={}", auth_.Clone()->appkey_);
        auto client = HttpClient::MakeSSL(settings->host_, settings->port_, path, 2000);
        auto resp = client->Request();
        if (resp.status != 200) {
            return nullptr;
        }

        try {
            //LOGI("auth: {}", resp.body);
            auto auth = std::make_shared<Authorization>();
            auto value = json::parse(resp.body);
            auth->auth_id_ = value["data"]["auth_id"].get<std::string>();
            auth->auth_name_ = value["data"]["auth_name"].get<std::string>();
            auth->machine_code_ = value["data"]["machine_code"].get<std::string>();
            auth->appkey_ = value["data"]["appkey"].get<std::string>();
            if (!value["data"]["role"].is_null()) {
                auth->role_ = (AuthRole)value["data"]["role"].get<int>();
            }
            if (!value["data"]["days"].is_null()) {
                auth->days_ = value["data"]["days"].get<int>();
            }
            if (!value["data"]["max_streams"].is_null()) {
                auth->max_streams_ = value["data"]["max_streams"].get<int>();
            }
            if (!value["data"]["end_timestamp_ms"].is_null()) {
                auth->end_timestamp_ms_ = value["data"]["end_timestamp_ms"].get<int64_t>();
            }

            // test //
            // auth->role_ = AuthRole::kPersonal;
            // test //

            this->auth_.Update(auth);
            this->FlushToStorage();
            return auth;
        } catch (std::exception& e) {
            LOGE("Parse auth failed: {}", e.what());
        }
        return nullptr;
    }

    std::shared_ptr<Authorization> AuthManager::GetAuth() const {
        return auth_.Clone();
    }

    void AuthManager::UpdateAppkey(const std::string& appkey) {
        auto auth = auth_.Clone();
        if (auth->appkey_ != appkey) {
            LOGI("UpdateAppkey: '{}' -> '{}'", auth->appkey_, appkey);
            auth->appkey_ = appkey;
            auth_.Update(auth);
            FlushToStorage();
        }
    }

    bool AuthManager::IsAuthValid() const {
        cat settings = SpvrSettings::Instance();
        if (settings->host_.empty() || settings->port_ <= 0) {
            return false;
        }

        // Local expiration check: if we know the expiration timestamp, enforce it.
        const auto auth = auth_.Clone();
        if (auth->end_timestamp_ms_ > 0 &&
            static_cast<int64_t>(TimeUtil::GetCurrentTimestamp()) > auth->end_timestamp_ms_) {
            LOGW("Authorization expired locally: end_timestamp_ms={}", auth->end_timestamp_ms_);
            return false;
        }

        cat path = std::format("/api/v1/auth/control/auth/valid?appkey={}", auth->appkey_);
        cat client = HttpClient::MakeSSL(settings->host_, settings->port_, path, 2000);
        cat resp = client->Request();
        if (resp.status != 200) {
            return false;
        }

        try {
            cat value = json::parse(resp.body);
            if (value["data"].is_boolean()) {
                return value["data"].get<bool>();
            }
            return false;
        } catch (std::exception& e) {
            LOGE("Parse auth failed: {}", e.what());
        }
        return false;
    }

}