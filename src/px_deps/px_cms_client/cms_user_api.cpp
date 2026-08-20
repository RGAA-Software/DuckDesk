//
// Created by RGAA on 31/10/2025.
//

#include "cms_user_api.h"
#include "cms_http_client.h"
#include <nlohmann/json.hpp>
#include "px_common_new/log.h"
#include "px_common_new/http_client.h"
#include "px_common_new/http_base_op.h"
#include "px_common_new/thread.h"
#include "px_common_new/message_notifier.h"
#include "px_common_new/hardware.h"
#include "px_common_new/ip_util.h"
#include "px_common_new/base64.h"
#include "px_common_new/uuid.h"
#include "cms_device.h"

// login
const std::string kLogin = "/api/v1/session/user/login";

// logout
const std::string kLogout = "/api/v1/session/user/logout";

const std::string kUpdateSelfProfile = "/api/v1/user/me";

// update avatar
const std::string kUpdateSelfAvatar = "/api/v1/user/me/avatar";

// update password
const std::string kUpdateSelfPassword = "/api/v1/user/me/password";


using namespace nlohmann;

namespace px_cms
{

    // login
    px::Result<CmsUserLoginResult, CmsApiError> CmsUserApi::Login(const std::string& host,
                                                             int port,
                                                             const std::string& username,
                                                             const std::string& password) {
        auto client = MakeCmsHttpClient(host, port, kLogin);

        json obj;
        obj[kUserName] = username;
        obj[kUserPassword] = password;
        obj["client_type"] = "panel";

        auto resp = client->Post({}, obj.dump(), "application/json");

        LOGI("Login, status:{}, address-> {}:{}, user-> {}",
             resp.status, host, port, username);
        if (resp.status != 200 || resp.body.empty()) {
            LOGE("Register failed: {}", resp.status);
            return TcErr((CmsApiError)resp.status);
        }

        try {
            auto data = json::parse(resp.body)["data"];
            CmsUserLoginResult result;
            result.user = CmsUser::FromObj(data["profile"]);
            result.access_token = data["access_token"].get<std::string>();
            result.expires_at = data["expires_at"].get<int64_t>();
            result.absolute_expires_at = data["absolute_expires_at"].get<int64_t>();
            if (!result.user || result.access_token.empty()) {
                return TcErr(CmsApiError::kParseJsonFailed);
            }
            return result;
        }
        catch(std::exception& e) {
            LOGE("Parse json failed: {}", e.what());
            return TcErr(CmsApiError::kParseJsonFailed);
        }
    }

    // logout
    px::Result<bool, CmsApiError> CmsUserApi::Logout(const std::string& host,
                                                              int port,
                                                              const std::string& access_token) {
        auto client = MakeCmsHttpClient(host, port, kLogout);

        client->SetHeader("Authorization", "Bearer " + access_token);
        auto resp = client->Post({}, "{}", "application/json");
        if (resp.status != 200 || resp.body.empty()) {
            LOGE("Logout failed: {}", resp.status);
            return TcErr((CmsApiError)resp.status);
        }

        try {
            return json::parse(resp.body)["data"].get<bool>();
        }
        catch(std::exception& e) {
            LOGE("Logout Parse json failed: {}", e.what());
            return TcErr(CmsApiError::kParseJsonFailed);
        }
    }

    px::Result<CmsUserPtr, CmsApiError> CmsUserApi::UpdateProfile(const std::string& host,
                                                                 int port,
                                                                 const std::string& access_token,
                                                                 const std::string& username) {
        auto client = MakeCmsHttpClient(host, port, kUpdateSelfProfile);
        client->SetHeader("Authorization", "Bearer " + access_token);
        json obj;
        obj[kUserName] = username;
        auto resp = client->Patch({}, obj.dump(), "application/json");
        if (resp.status != 200 || resp.body.empty()) {
            LOGE("Update failed: {}", resp.status);
            return TcErr((CmsApiError)resp.status);
        }

        try {
            auto data = json::parse(resp.body)["data"];
            return CmsUser::FromObj(data);
        }
        catch(std::exception& e) {
            LOGE("Update Parse json failed: {}", e.what());
            return TcErr(CmsApiError::kParseJsonFailed);
        }
    }

    px::Result<CmsUserLoginResult, CmsApiError> CmsUserApi::UpdatePassword(const std::string& host,
                                                                      int port,
                                                                      const std::string& access_token,
                                                                      const std::string& old_password,
                                                                      const std::string& new_password) {
        auto client = MakeCmsHttpClient(host, port, kUpdateSelfPassword);
        client->SetHeader("Authorization", "Bearer " + access_token);

        json obj;
        obj["current_password"] = old_password;
        obj["new_password"] = new_password;
        auto resp = client->Post({}, obj.dump(), "application/json");
        if (resp.status != 200 || resp.body.empty()) {
            LOGE("Update failed: {}", resp.status);
            return TcErr((CmsApiError)resp.status);
        }

        try {
            auto data = json::parse(resp.body)["data"];
            CmsUserLoginResult result;
            result.user = CmsUser::FromObj(data["profile"]);
            result.access_token = data["access_token"].get<std::string>();
            result.expires_at = data["expires_at"].get<int64_t>();
            result.absolute_expires_at = data["absolute_expires_at"].get<int64_t>();
            if (!result.user || result.access_token.empty()) {
                return TcErr(CmsApiError::kParseJsonFailed);
            }
            return result;
        }
        catch(std::exception& e) {
            LOGE("Update Parse json failed: {}", e.what());
            return TcErr(CmsApiError::kParseJsonFailed);
        }
    }

    px::Result<CmsUserPtr, CmsApiError> CmsUserApi::UpdateAvatar(const std::string& host,
                                                                    int port,
                                                                    const std::string& access_token,
                                                                    const std::string& avatar_path) {
        auto client = MakeCmsHttpClient(host, port, kUpdateSelfAvatar);
        client->SetHeader("Authorization", "Bearer " + access_token);
        std::map<std::string, std::string> form_parts = {};
        std::map<std::string, std::string> file_parts = {
            {"file", avatar_path}
        };
        auto resp = client->PutMultiPart({}, form_parts, file_parts);

        LOGI("Update Avatar, status:{}, address-> {}:{}", resp.status, host, port);
        if (resp.status != 200 || resp.body.empty()) {
            LOGE("Update Avatar failed: {}", resp.status);
            return TcErr((CmsApiError)resp.status);
        }

        try {
            auto data = json::parse(resp.body)["data"];
            return CmsUser::FromObj(data);
        }
        catch(std::exception& e) {
            LOGE("Parse json failed: {}", e.what());
            return TcErr(CmsApiError::kParseJsonFailed);
        }
    }

}
