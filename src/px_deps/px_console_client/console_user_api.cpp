//
// Created by RGAA on 31/10/2025.
//

#include "console_user_api.h"
#include "console_http_client.h"
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
#include "console_device.h"

// login
const std::string kLogin = "/api/v1/session/user/login";

const std::string kRegister = "/api/v1/user/register";

// logout
const std::string kLogout = "/api/v1/session/user/logout";

const std::string kUpdateSelfProfile = "/api/v1/user/me";

// update avatar
const std::string kUpdateSelfAvatar = "/api/v1/user/me/avatar";

// update password
const std::string kUpdateSelfPassword = "/api/v1/user/me/password";


using namespace nlohmann;

namespace px_console
{
    namespace
    {
        ConsoleApiError ToUserApiError(const px::HttpResponse& response) {
            SetConsoleApiLastErrorMessage("");
            if (!response.body.empty()) {
                try {
                    const auto error = json::parse(response.body);
                    const auto code = error.value("code", 0);
                    SetConsoleApiLastErrorMessage(error.value("message", ""));
                    if (code >= static_cast<int>(ConsoleApiError::kInvalidParams)) {
                        return static_cast<ConsoleApiError>(code);
                    }
                }
                catch (const std::exception& error) {
                    LOGE("Parse Console user error response failed: {}", error.what());
                }
            }
            switch (response.status) {
                case 401: return ConsoleApiError::kAuthenticationRequired;
                case 403: return ConsoleApiError::kForbidden;
                case 404: return ConsoleApiError::kNotFound;
                case 409: return ConsoleApiError::kConflict;
                case 410: return ConsoleApiError::kGone;
                case 429: return ConsoleApiError::kRateLimited;
                case 503: return ConsoleApiError::kServiceUnavailable;
                default: return ConsoleApiError::kInternalError;
            }
        }
    }

    px::Result<ConsoleUserPtr, ConsoleApiError> ConsoleUserApi::Register(
        const std::string& host,
        int port,
        const std::string& guest_access_token,
        const std::string& username,
        const std::string& password) {
        auto client = MakeConsoleHttpClient(host, port, kRegister);
        client->SetHeader("Authorization", "Bearer " + guest_access_token);
        const auto response = client->Post({}, json{
            {kUserName, username},
            {kUserPassword, password},
        }.dump(), "application/json");
        LOGI("Register, status:{}, address-> {}:{}, user-> {}",
             response.status, host, port, username);
        if (response.status != 200 || response.body.empty()) {
            LOGE("Register failed: {}", response.status);
            return TcErr(ToUserApiError(response));
        }
        try {
            auto user = ConsoleUser::FromObj(json::parse(response.body).at("data"));
            if (!user) return TcErr(ConsoleApiError::kParseJsonFailed);
            return user;
        }
        catch (const std::exception& error) {
            LOGE("Register parse failed: {}", error.what());
            return TcErr(ConsoleApiError::kParseJsonFailed);
        }
    }

    // login
    px::Result<ConsoleUserLoginResult, ConsoleApiError> ConsoleUserApi::Login(const std::string& host,
                                                             int port,
                                                             const std::string& username,
                                                             const std::string& password) {
        auto client = MakeConsoleHttpClient(host, port, kLogin);

        json obj;
        obj[kUserName] = username;
        obj[kUserPassword] = password;
        obj["client_type"] = "panel";

        auto resp = client->Post({}, obj.dump(), "application/json");

        LOGI("Login, status:{}, address-> {}:{}, user-> {}",
             resp.status, host, port, username);
        if (resp.status != 200 || resp.body.empty()) {
            LOGE("Login failed: {}", resp.status);
            return TcErr(ToUserApiError(resp));
        }

        try {
            auto data = json::parse(resp.body)["data"];
            ConsoleUserLoginResult result;
            result.user = ConsoleUser::FromObj(data["profile"]);
            result.access_token = data["access_token"].get<std::string>();
            result.expires_at = data["expires_at"].get<int64_t>();
            result.absolute_expires_at = data["absolute_expires_at"].get<int64_t>();
            if (!result.user || result.access_token.empty()) {
                return TcErr(ConsoleApiError::kParseJsonFailed);
            }
            return result;
        }
        catch(std::exception& e) {
            LOGE("Parse json failed: {}", e.what());
            return TcErr(ConsoleApiError::kParseJsonFailed);
        }
    }

    // logout
    px::Result<bool, ConsoleApiError> ConsoleUserApi::Logout(const std::string& host,
                                                              int port,
                                                              const std::string& access_token) {
        auto client = MakeConsoleHttpClient(host, port, kLogout);

        client->SetHeader("Authorization", "Bearer " + access_token);
        auto resp = client->Post({}, "{}", "application/json");
        if (resp.status != 200 || resp.body.empty()) {
            LOGE("Logout failed: {}", resp.status);
            return TcErr(ToUserApiError(resp));
        }

        try {
            return json::parse(resp.body)["data"].get<bool>();
        }
        catch(std::exception& e) {
            LOGE("Logout Parse json failed: {}", e.what());
            return TcErr(ConsoleApiError::kParseJsonFailed);
        }
    }

    px::Result<ConsoleUserPtr, ConsoleApiError> ConsoleUserApi::UpdateProfile(const std::string& host,
                                                                 int port,
                                                                 const std::string& access_token,
                                                                 const std::string& username) {
        auto client = MakeConsoleHttpClient(host, port, kUpdateSelfProfile);
        client->SetHeader("Authorization", "Bearer " + access_token);
        json obj;
        obj[kUserName] = username;
        auto resp = client->Patch({}, obj.dump(), "application/json");
        if (resp.status != 200 || resp.body.empty()) {
            LOGE("Update failed: {}", resp.status);
            return TcErr(ToUserApiError(resp));
        }

        try {
            auto data = json::parse(resp.body)["data"];
            return ConsoleUser::FromObj(data);
        }
        catch(std::exception& e) {
            LOGE("Update Parse json failed: {}", e.what());
            return TcErr(ConsoleApiError::kParseJsonFailed);
        }
    }

    px::Result<ConsoleUserLoginResult, ConsoleApiError> ConsoleUserApi::UpdatePassword(const std::string& host,
                                                                      int port,
                                                                      const std::string& access_token,
                                                                      const std::string& old_password,
                                                                      const std::string& new_password) {
        auto client = MakeConsoleHttpClient(host, port, kUpdateSelfPassword);
        client->SetHeader("Authorization", "Bearer " + access_token);

        json obj;
        obj["current_password"] = old_password;
        obj["new_password"] = new_password;
        auto resp = client->Post({}, obj.dump(), "application/json");
        if (resp.status != 200 || resp.body.empty()) {
            LOGE("Update failed: {}", resp.status);
            return TcErr(ToUserApiError(resp));
        }

        try {
            auto data = json::parse(resp.body)["data"];
            ConsoleUserLoginResult result;
            result.user = ConsoleUser::FromObj(data["profile"]);
            result.access_token = data["access_token"].get<std::string>();
            result.expires_at = data["expires_at"].get<int64_t>();
            result.absolute_expires_at = data["absolute_expires_at"].get<int64_t>();
            if (!result.user || result.access_token.empty()) {
                return TcErr(ConsoleApiError::kParseJsonFailed);
            }
            return result;
        }
        catch(std::exception& e) {
            LOGE("Update Parse json failed: {}", e.what());
            return TcErr(ConsoleApiError::kParseJsonFailed);
        }
    }

    px::Result<ConsoleUserPtr, ConsoleApiError> ConsoleUserApi::UpdateAvatar(const std::string& host,
                                                                    int port,
                                                                    const std::string& access_token,
                                                                    const std::string& avatar_path) {
        auto client = MakeConsoleHttpClient(host, port, kUpdateSelfAvatar);
        client->SetHeader("Authorization", "Bearer " + access_token);
        std::map<std::string, std::string> form_parts = {};
        std::map<std::string, std::string> file_parts = {
            {"file", avatar_path}
        };
        auto resp = client->PutMultiPart({}, form_parts, file_parts);

        LOGI("Update Avatar, status:{}, address-> {}:{}", resp.status, host, port);
        if (resp.status != 200 || resp.body.empty()) {
            LOGE("Update Avatar failed: {}", resp.status);
            return TcErr(ToUserApiError(resp));
        }

        try {
            auto data = json::parse(resp.body)["data"];
            return ConsoleUser::FromObj(data);
        }
        catch(std::exception& e) {
            LOGE("Parse json failed: {}", e.what());
            return TcErr(ConsoleApiError::kParseJsonFailed);
        }
    }

}
