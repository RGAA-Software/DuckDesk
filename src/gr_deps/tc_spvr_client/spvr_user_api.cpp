//
// Created by RGAA on 31/10/2025.
//

#include "spvr_user_api.h"
#include <nlohmann/json.hpp>
#include "tc_common_new/log.h"
#include "tc_common_new/http_client.h"
#include "tc_common_new/http_base_op.h"
#include "tc_common_new/thread.h"
#include "tc_common_new/message_notifier.h"
#include "tc_common_new/hardware.h"
#include "tc_common_new/ip_util.h"
#include "tc_common_new/base64.h"
#include "tc_common_new/uuid.h"
#include "spvr_device.h"

// /api/v1/user/control
const std::string kSpvrUserControl = "/api/v1/user/control";

// register
const std::string kRegister = kSpvrUserControl + "/register";

// login
const std::string kLogin = kSpvrUserControl + "/login";

// logout
const std::string kLogout = kSpvrUserControl + "/logout";

// delete
const std::string kDelete = kSpvrUserControl + "/delete";

// update
const std::string kUpdate = kSpvrUserControl + "/update";

// active
const std::string kActive = kSpvrUserControl + "/active";

// update avatar
const std::string kUpdateAvatar = kSpvrUserControl + "/update/avatar";

// update password
const std::string kUpdatePassword = kSpvrUserControl + "/update/password";

// query user by id
const std::string kQueryUserById = kSpvrUserControl + "/query/user/by/id";

// query user by name
const std::string kQueryUserByName = kSpvrUserControl + "/query/user/by/name";

// query users
const std::string kQueryUsers = kSpvrUserControl + "/query/users";

using namespace nlohmann;

namespace spvr
{

    // register
    tc::Result<SpvrUserPtr, SpvrApiError> SpvrUserApi::Register(const std::string& host,
                                                                int port,
                                                                const std::string& appkey,
                                                                const std::string& username,
                                                                const std::string& hash_password) {
        auto client = tc::HttpClient::MakeSSL(host, port, kRegister);

        json obj;
        obj[kUserName] = username;
        obj[kUserHashPassword] = hash_password;

        auto resp = client->Post({
            {"appkey", appkey}
        }, obj.dump());

        LOGI("Register, status:{}, {}, address-> {}:{}, user-> {}:{}, appkey: {}",
             resp.status, resp.body, host, port, username, hash_password, appkey);
        if (resp.status != 200 || resp.body.empty()) {
            LOGE("Register failed: {}", resp.status);
            return TcErr((SpvrApiError)resp.status);
        }

        try {
            auto data = json::parse(resp.body)["data"];
            return SpvrUser::FromObj(data);
        }
        catch(std::exception& e) {
            LOGE("Parse json failed: {}", e.what());
            return TcErr(SpvrApiError::kParseJsonFailed);
        }
    }

    // login
    tc::Result<SpvrUserPtr, SpvrApiError> SpvrUserApi::Login(const std::string& host,
                                                             int port,
                                                             const std::string& appkey,
                                                             const std::string& username,
                                                             const std::string& hash_password,
                                                             const std::string& device_id) {
        auto client = tc::HttpClient::MakeSSL(host, port, kLogin);

        json obj;
        obj[kUserName] = username;
        obj[kUserHashPassword] = hash_password;
        obj[kDeviceId] = device_id;

        auto resp = client->Post({
             {"appkey", appkey}
        }, obj.dump());

        LOGI("Login, status:{}, {}, address-> {}:{}, user-> {}:{}, appkey: {}",
             resp.status, resp.body, host, port, username, hash_password, appkey);
        if (resp.status != 200 || resp.body.empty()) {
            LOGE("Register failed: {}", resp.status);
            return TcErr((SpvrApiError)resp.status);
        }

        try {
            auto data = json::parse(resp.body)["data"];
            return SpvrUser::FromObj(data);
        }
        catch(std::exception& e) {
            LOGE("Parse json failed: {}", e.what());
            return TcErr(SpvrApiError::kParseJsonFailed);
        }
    }

    // logout
    tc::Result<SpvrUserPtr, SpvrApiError> SpvrUserApi::Logout(const std::string& host,
                                                              int port,
                                                              const std::string& appkey,
                                                              const std::string& uid,
                                                              const std::string& hash_password) {
        auto client = tc::HttpClient::MakeSSL(host, port, kLogout);

        std::map<std::string, std::string> query = {
            {"appkey", appkey}
        };

        json obj;
        obj[kUserId] = uid;
        obj[kUserHashPassword] = hash_password;
        auto resp = client->Post(query, obj.dump());
        if (resp.status != 200 || resp.body.empty()) {
            LOGE("Logout failed: {}", resp.status);
            return TcErr((SpvrApiError)resp.status);
        }

        try {
            auto data = json::parse(resp.body)["data"];
            return SpvrUser::FromObj(data);
        }
        catch(std::exception& e) {
            LOGE("Logout Parse json failed: {}", e.what());
            return TcErr(SpvrApiError::kParseJsonFailed);
        }
    }

    tc::Result<SpvrUserPtr, SpvrApiError> SpvrUserApi::Update(const std::string& host,
                                                              int port,
                                                              const std::string& appkey,
                                                              const std::string& uid,
                                                              const std::string& hash_password,
                                                              const std::map<std::string, std::string>& values) {

        auto client = tc::HttpClient::MakeSSL(host, port, kUpdate);

        std::map<std::string, std::string> query = {
            {"appkey", appkey}
        };

        json obj;
        obj[kUserId] = uid;
        obj[kUserHashPassword] = hash_password;
        for (const auto& [k, v] : values) {
            obj[k] = v;
        }
        auto resp = client->Post(query, obj.dump());
        if (resp.status != 200 || resp.body.empty()) {
            LOGE("Update failed: {}", resp.status);
            return TcErr((SpvrApiError)resp.status);
        }

        try {
            auto data = json::parse(resp.body)["data"];
            return SpvrUser::FromObj(data);
        }
        catch(std::exception& e) {
            LOGE("Update Parse json failed: {}", e.what());
            return TcErr(SpvrApiError::kParseJsonFailed);
        }
    }

    tc::Result<SpvrUserPtr, SpvrApiError> SpvrUserApi::UpdatePassword(const std::string& host,
                                                                      int port,
                                                                      const std::string& appkey,
                                                                      const std::string& uid,
                                                                      const std::string& old_hash_password,
                                                                      const std::string& new_hash_password) {
        auto client = tc::HttpClient::MakeSSL(host, port, kUpdatePassword);

        std::map<std::string, std::string> query = {
            {"appkey", appkey}
        };

        json obj;
        obj[kUserId] = uid;
        obj[kUserHashPassword] = old_hash_password;
        obj[kUserNewHashPassword] = new_hash_password;
        auto resp = client->Post(query, obj.dump());
        if (resp.status != 200 || resp.body.empty()) {
            LOGE("Update failed: {}", resp.status);
            return TcErr((SpvrApiError)resp.status);
        }

        try {
            auto data = json::parse(resp.body)["data"];
            return SpvrUser::FromObj(data);
        }
        catch(std::exception& e) {
            LOGE("Update Parse json failed: {}", e.what());
            return TcErr(SpvrApiError::kParseJsonFailed);
        }
    }

    tc::Result<SpvrUserPtr, SpvrApiError> SpvrUserApi::UpdateAvatar(const std::string& host,
                                                                    int port,
                                                                    const std::string& appkey,
                                                                    const std::string& uid,
                                                                    const std::string& hash_password,
                                                                    const std::string& avatar_path) {
        auto client = tc::HttpClient::MakeSSL(host, port, kUpdateAvatar);

        std::map<std::string, std::string> query = {
            {"uid", uid},
            {"appkey", appkey}
        };
        std::map<std::string, std::string> form_parts = {};
        std::map<std::string, std::string> file_parts = {
            {"file", avatar_path}
        };
        auto resp = client->PostMultiPart(query, form_parts, file_parts);

        LOGI("Update Avatar, status:{}, {}, address-> {}:{}, user-> {}:{}, appkey: {}, avatar path: {}",
             resp.status, resp.body, host, port, uid, hash_password, appkey, avatar_path);
        if (resp.status != 200 || resp.body.empty()) {
            LOGE("Update Avatar failed: {}", resp.status);
            return TcErr((SpvrApiError)resp.status);
        }

        try {
            auto data = json::parse(resp.body)["data"];
            return SpvrUser::FromObj(data);
        }
        catch(std::exception& e) {
            LOGE("Parse json failed: {}", e.what());
            return TcErr(SpvrApiError::kParseJsonFailed);
        }
    }

}