//
// Created by RGAA on 18/11/2025.
//

#include "px_user_manager.h"
#include <format>
#include <tuple>
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
#include <QUuid>

const std::string kUserPrefix = "cms_user:";

namespace px
{

    PxUserManager::PxUserManager(const std::shared_ptr<PxContext>& ctx) {
        context_ = ctx;
        settings_ = PxSettings::Instance();
    }

    bool PxUserManager::Login(const std::string& username, const std::string& password, bool show_dialog) {
        auto host = settings_->GetCmsServerHost();
        auto port = settings_->GetCmsServerPort();
        auto r = px_cms::CmsUserApi::Login(host, port, username, password);
        if (r.has_value()) {
            ClearGuestSession();
            auto login = r.value();
            auto user = login.user;
            if (!this->SaveUserInfo(user->uid_, user->username_, login.access_token, user->avatar_path_, user->must_change_password_)) {
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
            LOGE("Login failed, err: {}, msg: {}", (int)err, px_cms::CmsApiErrorAsString(err));
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
        // Logging out is a local security boundary. Clear the credential even
        // when CMS is temporarily unreachable; the remote session will expire
        // and must not keep the Panel appearing signed in.
        const auto username = GetUsername();
        const auto uid = GetUserId();
        Clear();
        ClearGuestSession();
        if (r.has_value()) {
            LOGI("Logout: {} {}", username, uid);
        }
        else {
            auto err = r.error();
            LOGE("Logout failed, err: {}, msg: {}", (int)err, px_cms::CmsApiErrorAsString(err));
            QString msg = tcTr("id_op_error") + ":" + QString::number((int)err) + " " + px_cms::CmsApiErrorAsString(err).c_str();
            TcDialog dialog(tcTr("id_error"), msg);
            dialog.exec();
        }
        return r.has_value();
    }

    bool PxUserManager::ModifyUsername(const std::string& username) {
        auto host = settings_->GetCmsServerHost();
        auto port = settings_->GetCmsServerPort();
        auto r = px_cms::CmsUserApi::UpdateProfile(host, port, GetAccessToken(), username);
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
            auto login = r.value();
            auto user = login.user;
            if (!SaveUserInfo(user->uid_, user->username_, login.access_token, user->avatar_path_, user->must_change_password_)) {
                Clear();
                return false;
            }
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

    bool PxUserManager::UpdateAvatar(const std::string& avatar_path) {
        auto host = settings_->GetCmsServerHost();
        auto port = settings_->GetCmsServerPort();
        auto r = px_cms::CmsUserApi::UpdateAvatar(host, port, GetAccessToken(), avatar_path);
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

    px::Result<std::vector<std::shared_ptr<px_cms::CmsUserDevice>>, px_cms::CmsApiError>
    PxUserManager::QueryBindDevices(int page, int page_size, bool show_dialog) {
        auto host = settings_->GetCmsServerHost();
        auto port = settings_->GetCmsServerPort();
        auto access_token = GetAccessToken();
        if (access_token.empty()) {
            return std::vector<std::shared_ptr<px_cms::CmsUserDevice>>{};
        }
        (void)page;
        (void)page_size;
        auto r = px_cms::CmsUserDeviceApi::QueryUserBindDevices(host, port, access_token);
        if (!r.has_value()) {
            auto err = r.error();
            if (err == px_cms::CmsApiError::kAuthenticationRequired) {
                HandleExpiredUserSession();
            }
            if (show_dialog) {
                grApp->GetContext()->PostUITask([=, this]() {

                });
            }
            return TcErr(err);
        }
        else {
            auto v = r.value();
            return v;
        }
    }

    px::Result<px_cms::CmsConnectionTicket, px_cms::CmsApiError> PxUserManager::IssueDeviceTicket(
        const std::string& device_id,
        const std::string& client_nonce,
        const std::vector<std::string>& requested_permissions) {
        auto host = settings_->GetCmsServerHost();
        auto port = settings_->GetCmsServerPort();
        auto result = px_cms::CmsUserDeviceApi::IssueDeviceTicket(
            host, port, GetAccessToken(), device_id, client_nonce, requested_permissions);
        if (!result.has_value()
            && result.error() == px_cms::CmsApiError::kAuthenticationRequired) {
            HandleExpiredUserSession();
        }
        return result;
    }

    px::Result<std::vector<px_cms::CmsUserApplication>, px_cms::CmsApiError>
    PxUserManager::QueryApps() {
        auto [token, guest] = ResourceSession();
        if (token.empty()) return TcErr(px_cms::CmsApiError::kInternalError);
        auto result = px_cms::CmsUserAppApi::QueryApps(settings_->GetCmsServerHost(),
            settings_->GetCmsServerPort(), token, guest);
        if (!result.has_value()
            && result.error() == px_cms::CmsApiError::kAuthenticationRequired) {
            if (guest) ClearGuestSession(); else HandleExpiredUserSession();
            std::tie(token, guest) = ResourceSession();
            if (!token.empty()) result = px_cms::CmsUserAppApi::QueryApps(
                settings_->GetCmsServerHost(), settings_->GetCmsServerPort(), token, guest);
        }
        return result;
    }

    px::Result<px_cms::CmsUserAppInstance, px_cms::CmsApiError>
    PxUserManager::StartApp(const std::string& app_id, const std::string& client_nonce) {
        auto [token, guest] = ResourceSession();
        if (token.empty()) return TcErr(px_cms::CmsApiError::kInternalError);
        auto result = px_cms::CmsUserAppApi::StartApp(settings_->GetCmsServerHost(),
            settings_->GetCmsServerPort(), token, app_id, client_nonce, guest);
        if (!result.has_value()
            && result.error() == px_cms::CmsApiError::kAuthenticationRequired) {
            if (guest) ClearGuestSession(); else HandleExpiredUserSession();
            std::tie(token, guest) = ResourceSession();
            if (!token.empty()) result = px_cms::CmsUserAppApi::StartApp(
                settings_->GetCmsServerHost(), settings_->GetCmsServerPort(), token,
                app_id, client_nonce, guest);
        }
        return result;
    }

    px::Result<px_cms::CmsConnectionTicket, px_cms::CmsApiError>
    PxUserManager::IssueInstanceTicket(const std::string& instance_id,
        const std::string& client_nonce,
        const std::vector<std::string>& requested_permissions) {
        auto [token, guest] = ResourceSession();
        if (token.empty()) return TcErr(px_cms::CmsApiError::kInternalError);
        auto result = px_cms::CmsUserAppApi::IssueInstanceTicket(settings_->GetCmsServerHost(),
            settings_->GetCmsServerPort(), token, instance_id, client_nonce,
            requested_permissions, guest);
        if (!result.has_value()
            && result.error() == px_cms::CmsApiError::kAuthenticationRequired) {
            // A new guest session does not own the old instance, so this retry
            // will intentionally fail with 404. The caller then refreshes the
            // catalog and starts a new instance under the new guest identity.
            if (guest) ClearGuestSession(); else HandleExpiredUserSession();
            std::tie(token, guest) = ResourceSession();
            if (!token.empty()) result = px_cms::CmsUserAppApi::IssueInstanceTicket(
                settings_->GetCmsServerHost(), settings_->GetCmsServerPort(), token,
                instance_id, client_nonce, requested_permissions, guest);
        }
        return result;
    }

    px::Result<px_cms::CmsUserAppInstance, px_cms::CmsApiError>
    PxUserManager::StopInstance(const std::string& instance_id) {
        auto [token, guest] = ResourceSession();
        if (token.empty()) return TcErr(px_cms::CmsApiError::kInternalError);
        auto result = px_cms::CmsUserAppApi::StopInstance(settings_->GetCmsServerHost(),
            settings_->GetCmsServerPort(), token, instance_id, guest);
        if (!result.has_value()
            && result.error() == px_cms::CmsApiError::kAuthenticationRequired) {
            if (guest) ClearGuestSession(); else HandleExpiredUserSession();
            std::tie(token, guest) = ResourceSession();
            if (!token.empty()) result = px_cms::CmsUserAppApi::StopInstance(
                settings_->GetCmsServerHost(), settings_->GetCmsServerPort(), token,
                instance_id, guest);
        }
        return result;
    }

    std::pair<std::string, bool> PxUserManager::ResourceSession() {
        if (auto token = GetAccessToken(); !token.empty()) {
            return {std::move(token), false};
        }
        std::lock_guard<std::mutex> guard(guest_session_mutex_);
        if (guest_access_token_.empty()) {
            const auto nonce = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
            const auto result = px_cms::CmsUserAppApi::CreateGuestSession(
                settings_->GetCmsServerHost(), settings_->GetCmsServerPort(), nonce);
            if (!result.has_value()) return {{}, true};
            guest_access_token_ = result.value();
        }
        return {guest_access_token_, true};
    }

    void PxUserManager::ClearGuestSession() {
        std::lock_guard<std::mutex> guard(guest_session_mutex_);
        guest_access_token_.clear();
    }

    void PxUserManager::HandleExpiredUserSession() {
        Clear();
        ClearGuestSession();
        context_->SendAppMessage(MsgUserLoggedOut {});
    }

    bool PxUserManager::SaveUserInfo(const std::string& uid, const std::string& username, const std::string& access_token, const std::string& avatar_path, bool must_change_password) {
        if (!SaveAccessToken(access_token)) {
            return false;
        }

        // uid
        context_->SpPutString(KeyUid(), uid);

        // username
        this->UpdateUsername(username);

        // avatar path
        this->UpdateAvatarPath(avatar_path);
        context_->SpPutInteger(KeyMustChangePassword(), must_change_password ? 1 : 0);
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

    bool PxUserManager::IsPasswordChangeRequired() {
        return context_->SpGetInteger(KeyMustChangePassword(), 0) != 0;
    }

    void PxUserManager::Clear() {
        DeleteAccessToken();
        context_->SpPutString(KeyUid(), "");
        UpdateUsername("");
        UpdateAvatarPath("");
        context_->SpPutInteger(KeyMustChangePassword(), 0);
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

    std::string PxUserManager::KeyMustChangePassword() {
        return std::format("{}{}", kUserPrefix, px_cms::kUserMustChangePassword);
    }

}
