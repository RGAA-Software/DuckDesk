//
// Created by RGAA on 18/11/2025.
//

#include "px_user_manager.h"
#include <format>
#include "px_common_new/log.h"
#include "px_cms_client/cms_user.h"
#include "px_cms_client/cms_user_api.h"
#include "px_cms_client/cms_user_device_api.h"
#include "render_panel/px_context.h"
#include "render_panel/px_settings.h"
#include "render_panel/px_application.h"
#include "render_panel/px_app_messages.h"
#include "px_label.h"
#include "px_dialog.h"
#include <Windows.h>
#include <wincred.h>
#include <QString>

const std::string kUserPrefix = "cms_user:";

namespace px
{

    PxUserManager::PxUserManager(const std::shared_ptr<PxContext>& ctx) {
        context_ = ctx;
        settings_ = PxSettings::Instance();
    }

    bool PxUserManager::Register(const std::string& username, const std::string& password) {
        auto host = settings_->GetCmsServerHost();
        auto port = settings_->GetCmsServerPort();
        auto appkey = grApp->GetAppkey();
        auto r = px_cms::CmsUserApi::Register(host, port, appkey, username, password);
        if (r.has_value()) {
            context_->NotifyAppMessage(tcTr("id_tips"), tcTr("id_register_success"));
            return Login(username, password, false);
        }
        else {
            auto err = r.error();
            LOGE("Register failed, err: {}, msg: {}", (int)err, px_cms::CmsApiErrorAsString(err));
            context_->PostUITask([=, this]() {
                QString msg = tcTr("id_op_error") + ":" + QString::number((int)err) + " " + px_cms::CmsApiErrorAsString(err).c_str();
                TcDialog dialog(tcTr("id_error"), msg);
                dialog.exec();
            });
        }
        return false;
    }

    bool PxUserManager::Login(const std::string& username, const std::string& password, bool show_dialog) {
        auto host = settings_->GetCmsServerHost();
        auto port = settings_->GetCmsServerPort();
        auto r = px_cms::CmsUserApi::Login(host, port, username, password);
        if (r.has_value()) {
            auto login = r.value();
            auto user = login.user;
            if (!this->SaveUserInfo(user->uid_, user->username_, login.access_token, user->avatar_path_)) {
                // Do not leave a live server-side session behind when the
                // Windows credential vault cannot persist its token.
                auto logout_result = px_cms::CmsUserApi::Logout(host, port, login.access_token);
                if (!logout_result.has_value()) {
                    LOGW("Failed to revoke CMS session after credential vault error");
                }
                if (show_dialog) {
                    context_->PostUITask([this]() {
                        TcDialog dialog(tcTr("id_error"), tcTr("id_op_error"));
                        dialog.exec();
                    });
                }
                return false;
            }
            context_->NotifyAppMessage(tcTr("id_tips"), tcTr("id_login_success"));

            // send a logged in message
            context_->SendAppMessage(MsgUserLoggedIn {});

            return true;
        }
        else {
            auto err = r.error();
            LOGE("Register failed, err: {}, msg: {}", (int)err, px_cms::CmsApiErrorAsString(err));
            if (show_dialog) {
                context_->PostUITask([=, this]() {
                    QString msg = tcTr("id_op_error") + ":" + QString::number((int) err) + " " + px_cms::CmsApiErrorAsString(err).c_str();
                    TcDialog dialog(tcTr("id_error"), msg);
                    dialog.exec();
                });
            }
            return false;
        }
    }

    bool PxUserManager::Logout() {
        auto host = settings_->GetCmsServerHost();
        auto port = settings_->GetCmsServerPort();
        auto r = px_cms::CmsUserApi::Logout(host, port, GetAccessToken());
        if (r.has_value()) {
            LOGI("Logout: {} {}", GetUsername(), GetUserId());
            Clear();
        }
        else {
            auto err = r.error();
            LOGE("Logout failed, err: {}, msg: {}", (int)err, px_cms::CmsApiErrorAsString(err));
            QString msg = tcTr("id_op_error") + ":" + QString::number((int)err) + " " + px_cms::CmsApiErrorAsString(err).c_str();
            TcDialog dialog(tcTr("id_error"), msg);
            dialog.exec();
        }
        return true;
    }

    bool PxUserManager::ModifyUsername(const std::string& username) {
        auto host = settings_->GetCmsServerHost();
        auto port = settings_->GetCmsServerPort();
        auto appkey = grApp->GetAppkey();
        auto uid = GetUserId();
        std::map<std::string, std::string> values = {
            {px_cms::kUserName, username}
        };
        auto r = px_cms::CmsUserApi::Update(host, port, appkey, uid, "", values);
        if (r.has_value()) {
            auto user = r.value();
            this->UpdateUsername(user->username_);
            context_->NotifyAppMessage(tcTr("id_tips"), tcTr("id_update_success"));
            return true;
        }
        else {
            auto err = r.error();
            QString msg = tcTr("id_op_error") + ":" + QString::number((int)err) + " " + px_cms::CmsApiErrorAsString(err).c_str();
            TcDialog dialog(tcTr("id_error"), msg);
            dialog.exec();
            return false;
        }
    }

    bool PxUserManager::ModifyPassword(const std::string& current_password, const std::string& new_password) {
        auto host = settings_->GetCmsServerHost();
        auto port = settings_->GetCmsServerPort();
        auto r = px_cms::CmsUserApi::UpdatePassword(host, port, GetAccessToken(), current_password, new_password);
        if (r.has_value()) {
            context_->NotifyAppMessage(tcTr("id_tips"), tcTr("id_update_success"));
            // Password changes increment auth_version and invalidate every
            // existing session, including this one.
            Clear();
            return true;
        }
        else {
            auto err = r.error();
            QString msg = tcTr("id_op_error") + ":" + QString::number((int)err) + " " + px_cms::CmsApiErrorAsString(err).c_str();
            TcDialog dialog(tcTr("id_error"), msg);
            dialog.exec();
            return false;
        }
    }

    bool PxUserManager::UpdateAvatar(const std::string& avatar_path) {
        auto host = settings_->GetCmsServerHost();
        auto port = settings_->GetCmsServerPort();
        auto appkey = grApp->GetAppkey();
        auto uid = GetUserId();
        auto r = px_cms::CmsUserApi::UpdateAvatar(host, port, appkey, uid, avatar_path);
        if (r.has_value()) {
            auto user = r.value();
            UpdateAvatarPath(user->avatar_path_);
            context_->NotifyAppMessage(tcTr("id_tips"), tcTr("id_update_success"));
            return true;
        }
        else {
            auto err = r.error();
            QString msg = tcTr("id_op_error") + ":" + QString::number((int)err) + " " + px_cms::CmsApiErrorAsString(err).c_str();
            TcDialog dialog(tcTr("id_error"), msg);
            dialog.exec();
            return false;
        }
    }

    std::vector<std::shared_ptr<px_cms::CmsUserDevice>> PxUserManager::QueryBindDevices(int page, int page_size, bool show_dialog) {
        auto host = settings_->GetCmsServerHost();
        auto port = settings_->GetCmsServerPort();
        auto appkey = grApp->GetAppkey();
        auto uid = GetUserId();
        if (uid.empty()) {
            return {};
        }
        auto r = px_cms::CmsUserDeviceApi::QueryUserBindDevices(host, port, appkey, uid, page, page_size);
        if (!r.has_value()) {
            auto err = r.error();
            if (show_dialog) {
                grApp->GetContext()->PostUITask([=, this]() {

                });
            }
            return {};
        }
        else {
            auto v = r.value();
            return v;
        }
    }

    std::shared_ptr<px_cms::CmsUserDevice> PxUserManager::AddDeviceForUser(const std::string& device_id) {
        auto host = settings_->GetCmsServerHost();
        auto port = settings_->GetCmsServerPort();
        auto appkey = grApp->GetAppkey();
        auto uid = GetUserId();
        if (uid.empty()) {
            return nullptr;
        }
        auto r = px_cms::CmsUserDeviceApi::AddDeviceForUser(host, port, appkey, uid, device_id);
        if (!r.has_value()) {
            auto ctx = grApp->GetContext();
            ctx->PostUITask([=, this]() {

            });
            return nullptr;
        }
        auto device = r.value();
        return device;
    }

    std::shared_ptr<px_cms::CmsUserDevice> PxUserManager::RemoveDeviceFromUser(const std::string& device_id) {
        auto host = settings_->GetCmsServerHost();
        auto port = settings_->GetCmsServerPort();
        auto appkey = grApp->GetAppkey();
        auto uid = GetUserId();
        if (uid.empty()) {
            return nullptr;
        }
        auto r = px_cms::CmsUserDeviceApi::RemoveDeviceFromUser(host, port, appkey, uid, device_id);
        if (!r.has_value()) {
            return nullptr;
        }
        auto device = r.value();
        return device;
    }

    bool PxUserManager::SaveUserInfo(const std::string& uid, const std::string& username, const std::string& access_token, const std::string& avatar_path) {
        if (!SaveAccessToken(access_token)) {
            return false;
        }

        // uid
        context_->SpPutString(KeyUid(), uid);

        // username
        this->UpdateUsername(username);

        // avatar path
        this->UpdateAvatarPath(avatar_path);
        return true;
    }

    bool PxUserManager::IsLoggedIn() {
        auto uid = GetUserId();
        auto username = GetUsername();
        auto access_token = GetAccessToken();
        return !uid.empty() && !username.empty() && !access_token.empty();
    }

    std::string PxUserManager::GetUserId() {
        return context_->SpGetString(KeyUid());
    }

    void PxUserManager::UpdateUsername(const std::string& username) {
        context_->SpPutString(KeyUsername(), username);
    }

    std::string PxUserManager::GetUsername() {
        return context_->SpGetString(KeyUsername());
    }

    bool PxUserManager::SaveAccessToken(const std::string& access_token) {
        if (access_token.empty()) {
            DeleteAccessToken();
            return true;
        }
        auto target = CredentialTarget();
        CREDENTIALW credential{};
        credential.Type = CRED_TYPE_GENERIC;
        credential.TargetName = const_cast<wchar_t*>(target.c_str());
        credential.CredentialBlobSize = static_cast<DWORD>(access_token.size());
        credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(access_token.data()));
        credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
        credential.UserName = const_cast<wchar_t*>(L"Pixels CMS user session");
        if (!CredWriteW(&credential, 0)) {
            LOGE("Save CMS user session failed, win32 error: {}", GetLastError());
            return false;
        }
        return true;
    }

    std::string PxUserManager::GetAccessToken() {
        PCREDENTIALW credential = nullptr;
        auto target = CredentialTarget();
        if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &credential)) {
            return {};
        }
        std::string token(reinterpret_cast<const char*>(credential->CredentialBlob), credential->CredentialBlobSize);
        CredFree(credential);
        return token;
    }

    void PxUserManager::DeleteAccessToken() {
        auto target = CredentialTarget();
        if (!CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0) && GetLastError() != ERROR_NOT_FOUND) {
            LOGW("Delete CMS user session failed, win32 error: {}", GetLastError());
        }
    }

    void PxUserManager::UpdateAvatarPath(const std::string& avatar_path) {
        context_->SpPutString(KeyAvatarPath(), avatar_path);
    }

    std::string PxUserManager::GetAvatarPath() {
        return context_->SpGetString(KeyAvatarPath());
    }

    void PxUserManager::Clear() {
        DeleteAccessToken();
        context_->SpPutString(KeyUid(), "");
        UpdateUsername("");
        UpdateAvatarPath("");
    }

    std::string PxUserManager::KeyUid() {
        return std::format("{}{}", kUserPrefix, px_cms::kUserId);
    }

    std::string PxUserManager::KeyUsername() {
        return std::format("{}{}", kUserPrefix, px_cms::kUserName);
    }

    std::wstring PxUserManager::CredentialTarget() const {
        return QString::fromStdString(std::format("Pixels.CMS.UserSession.{}:{}", settings_->GetCmsServerHost(), settings_->GetCmsServerPort())).toStdWString();
    }

    std::string PxUserManager::KeyAvatarPath() {
        return std::format("{}{}", kUserPrefix, px_cms::kUserAvatarPath);
    }

}
