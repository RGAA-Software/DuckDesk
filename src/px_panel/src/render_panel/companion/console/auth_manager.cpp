//
// Created by RGAA on 29/08/2025.
//

#include "auth_manager.h"
#include "console_setting.h"
#include "render_panel/companion/panel_companion.h"
#include <nlohmann/json.hpp>
#include <utility>
#include "auth_defs.h"
#include "px_common/log.h"
#include "px_common/http_client.h"
#include "px_common/shared_preference.h"
#include "px_common/time_util.h"

namespace px
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

    AuthManager::AuthManager(std::shared_ptr<SharedPreference> storage)
        : storage_(std::move(storage)) {}

    std::function<void()> MakeAuthRefreshTask(
        const std::shared_ptr<AuthManager>& manager) {
        const std::weak_ptr<AuthManager> weak_manager = manager;
        return [weak_manager]() {
            if (const auto locked = weak_manager.lock()) {
                locked->RequestAuth();
            }
        };
    }

    void AuthManager::LoadFromStorage() {
        if (!storage_) {
            return;
        }
        const auto auth_id = storage_->Get(kAuthId);
        const auto auth_name = storage_->Get(kAuthName);
        const auto machine_code = storage_->Get(kAuthMachineCode);
        const auto appkey = storage_->Get(kAuthAppkey);
        const auto role = storage_->GetInt(kAuthRole);
        const auto days = storage_->GetInt(kAuthDays);
        const auto max_streams = storage_->GetInt(kAuthMaxStreams);
        const auto end_timestamp_ms = storage_->GetInt64(kAuthEndTimestampMs);
        if (auth_id.empty() || appkey.empty()) {
            LOGW("No auth loaded from storage, id: {}, role: {}", auth_id, role);
            return;
        }
        const auto auth = std::make_shared<Authorization>();
        auth->auth_id_ = auth_id;
        auth->auth_name_ = auth_name;
        auth->machine_code_ = machine_code;
        auth->appkey_ = appkey;
        auth->role_ = static_cast<AuthRole>(role);
        this->auth_.Update(auth);
        LOGI("Load auth from storage: auth id: {}, role: {} ", auth_id, role);
    }

    void AuthManager::FlushToStorage() {
        const auto storage = storage_;
        if (!storage) {
            return;
        }
        auth_.WithLock([storage](const std::shared_ptr<Authorization>& auth) {
            if (!auth || auth->auth_id_.empty() || auth->appkey_.empty()) {
                return;
            }
            storage->Put(kAuthId, auth->auth_id_);
            storage->Put(kAuthName, auth->auth_name_);
            storage->Put(kAuthMachineCode, auth->machine_code_);
            storage->Put(kAuthAppkey, auth->appkey_);
            storage->PutInt(kAuthRole, static_cast<int>(auth->role_));
            storage->PutInt(kAuthDays, auth->days_);
            storage->PutInt(kAuthMaxStreams, auth->max_streams_);
            storage->PutInt64(kAuthEndTimestampMs, auth->end_timestamp_ms_);
        });
    }

    std::shared_ptr<Authorization> AuthManager::RequestAuth() {
        auto settings = ConsoleSettings::Instance();
        if (settings->host_.empty() || settings->port_ <= 0) {
            return nullptr;
        }

        const auto cached_auth = auth_.Clone();
        if (!cached_auth || cached_auth->appkey_.empty()) {
            return nullptr;
        }

        const auto path = std::format("/api/v1/auth/control/get/authorization?appkey={}", cached_auth->appkey_);
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
        if (!auth) {
            auth = std::make_shared<Authorization>();
        }
        if (auth->appkey_ != appkey) {
            LOGI("Console application credential updated");
            auth->appkey_ = appkey;
            auth_.Update(auth);
            FlushToStorage();
        }
    }

    bool AuthManager::IsAuthValid() const {
        const auto settings = ConsoleSettings::Instance();
        if (settings->host_.empty() || settings->port_ <= 0) {
            return false;
        }

        // Local expiration check: if we know the expiration timestamp, enforce it.
        const auto auth = auth_.Clone();
        if (!auth || auth->appkey_.empty()) {
            return false;
        }
        if (auth->end_timestamp_ms_ > 0 &&
            static_cast<int64_t>(TimeUtil::GetCurrentTimestamp()) > auth->end_timestamp_ms_) {
            LOGW("Authorization expired locally: end_timestamp_ms={}", auth->end_timestamp_ms_);
            return false;
        }

        const auto path = std::format("/api/v1/auth/control/auth/valid?appkey={}", auth->appkey_);
        const auto client = HttpClient::MakeSSL(settings->host_, settings->port_, path, 2000);
        const auto resp = client->Request();
        if (resp.status != 200) {
            return false;
        }

        try {
            const auto value = json::parse(resp.body);
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
