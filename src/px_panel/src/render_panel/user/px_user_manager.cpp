//
// Created by RGAA on 18/11/2025.
//

#include "px_user_manager.h"
#include <format>
#include <tuple>
#include "px_common_new/log.h"
#include "px_console_client/console_user.h"
#include "px_console_client/console_user_api.h"
#include "px_console_client/console_user_device_api.h"
#include "render_panel/px_context.h"
#include "render_panel/px_settings.h"
#include "render_panel/px_application.h"
#include "render_panel/px_app_messages.h"
#include "render_panel/console/console_error_presenter.h"
#include "px_label.h"
#include "px_dialog.h"
#include <Windows.h>
#include <wincred.h>
#include <QString>
#include <QUuid>

const std::string kUserPrefix = "console_user:";

namespace px
{

    PxUserManager::PxUserManager(const std::shared_ptr<PxContext>& ctx) {
        context_ = ctx;
        settings_ = PxSettings::Instance();
    }

    bool PxUserManager::Login(const std::string& username, const std::string& password, bool show_dialog) {
        auto host = settings_->GetConsoleServerHost();
        auto port = settings_->GetConsoleServerPort();
        auto r = px_console::ConsoleUserApi::Login(host, port, username, password);
        if (r.has_value()) {
            ClearGuestSession();
            auto login = r.value();
            auto user = login.user;
            if (!this->SaveUserInfo(user->uid_, user->username_, login.access_token, user->avatar_path_)) {
                // Do not leave a live server-side session behind when the
                // Windows credential vault cannot persist its token.
                auto logout_result = px_console::ConsoleUserApi::Logout(host, port, login.access_token);
                if (!logout_result.has_value()) {
                    LOGW("Failed to revoke Console session after credential vault error");
                }
                if (show_dialog) {
                    context_->PostUITask([]() {
                        TcDialog dialog(tcTr("id_error"), tcTr("id_credential_store_failed"));
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
            LOGE("Login failed, err: {}, msg: {}", (int)err, px_console::ConsoleApiErrorAsString(err));
            if (show_dialog) {
                const auto server_message = px_console::ConsoleApiLastErrorMessage();
                const auto endpoint = MakeConsoleEndpoint(host, port);
                context_->PostUITask([err, server_message, endpoint]() {
                    const auto msg = MakeConsoleErrorMessage(
                        ConsoleErrorOperation::kSignIn, err, server_message, endpoint);
                    TcDialog dialog(tcTr("id_error"), msg);
                    dialog.exec();
                });
            }
            return false;
        }
    }

    bool PxUserManager::Logout() {
        auto host = settings_->GetConsoleServerHost();
        auto port = settings_->GetConsoleServerPort();
        auto r = px_console::ConsoleUserApi::Logout(host, port, GetAccessToken());
        // Logging out is a local security boundary. Clear the credential even
        // when Console is temporarily unreachable; the remote session will expire
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
            LOGE("Logout failed, err: {}, msg: {}", (int)err, px_console::ConsoleApiErrorAsString(err));
            const auto msg = MakeConsoleErrorMessage(ConsoleErrorOperation::kSignOut, err,
                px_console::ConsoleApiLastErrorMessage(), MakeConsoleEndpoint(host, port));
            TcDialog dialog(tcTr("id_error"), msg);
            dialog.exec();
        }
        return r.has_value();
    }

    bool PxUserManager::ModifyUsername(const std::string& username) {
        auto host = settings_->GetConsoleServerHost();
        auto port = settings_->GetConsoleServerPort();
        auto r = px_console::ConsoleUserApi::UpdateProfile(host, port, GetAccessToken(), username);
        if (r.has_value()) {
            auto user = r.value();
            this->UpdateUsername(user->username_);
            context_->NotifyAppMessage(tcTr("id_tips"), tcTr("id_update_success"));
            return true;
        }
        else {
            auto err = r.error();
            const auto msg = MakeConsoleErrorMessage(ConsoleErrorOperation::kUpdateAccount, err,
                px_console::ConsoleApiLastErrorMessage(), MakeConsoleEndpoint(host, port));
            TcDialog dialog(tcTr("id_error"), msg);
            dialog.exec();
            return false;
        }
    }

    bool PxUserManager::ModifyPassword(const std::string& current_password, const std::string& new_password) {
        auto host = settings_->GetConsoleServerHost();
        auto port = settings_->GetConsoleServerPort();
        auto r = px_console::ConsoleUserApi::UpdatePassword(host, port, GetAccessToken(), current_password, new_password);
        if (r.has_value()) {
            auto login = r.value();
            auto user = login.user;
            if (!SaveUserInfo(user->uid_, user->username_, login.access_token, user->avatar_path_)) {
                Clear();
                return false;
            }
            context_->NotifyAppMessage(tcTr("id_tips"), tcTr("id_update_success"));
            return true;
        }
        else {
            auto err = r.error();
            const auto msg = MakeConsoleErrorMessage(ConsoleErrorOperation::kUpdateAccount, err,
                px_console::ConsoleApiLastErrorMessage(), MakeConsoleEndpoint(host, port));
            TcDialog dialog(tcTr("id_error"), msg);
            dialog.exec();
            return false;
        }
    }

    bool PxUserManager::UpdateAvatar(const std::string& avatar_path) {
        auto host = settings_->GetConsoleServerHost();
        auto port = settings_->GetConsoleServerPort();
        auto r = px_console::ConsoleUserApi::UpdateAvatar(host, port, GetAccessToken(), avatar_path);
        if (r.has_value()) {
            auto user = r.value();
            UpdateAvatarPath(user->avatar_path_);
            context_->NotifyAppMessage(tcTr("id_tips"), tcTr("id_update_success"));
            return true;
        }
        else {
            auto err = r.error();
            const auto msg = MakeConsoleErrorMessage(ConsoleErrorOperation::kUpdateAccount, err,
                px_console::ConsoleApiLastErrorMessage(), MakeConsoleEndpoint(host, port));
            TcDialog dialog(tcTr("id_error"), msg);
            dialog.exec();
            return false;
        }
    }

    px::Result<std::vector<std::shared_ptr<px_console::ConsoleUserDevice>>, px_console::ConsoleApiError>
    PxUserManager::QueryBindDevices(int page, int page_size, bool show_dialog) {
        auto host = settings_->GetConsoleServerHost();
        auto port = settings_->GetConsoleServerPort();
        auto access_token = GetAccessToken();
        if (access_token.empty()) {
            return std::vector<std::shared_ptr<px_console::ConsoleUserDevice>>{};
        }
        (void)page;
        (void)page_size;
        auto r = px_console::ConsoleUserDeviceApi::QueryUserBindDevices(host, port, access_token);
        if (!r.has_value()) {
            auto err = r.error();
            if (err == px_console::ConsoleApiError::kAuthenticationRequired) {
                HandleExpiredUserSession();
            }
            if (show_dialog) {
                const auto server_message = px_console::ConsoleApiLastErrorMessage();
                const auto endpoint = MakeConsoleEndpoint(host, port);
                grApp->GetContext()->PostUITask([err, server_message, endpoint]() {
                    TcDialog dialog(tcTr("id_error"), MakeConsoleErrorMessage(
                        ConsoleErrorOperation::kLoadResources, err, server_message, endpoint));
                    dialog.exec();
                });
            }
            return TcErr(err);
        }
        else {
            auto v = r.value();
            return v;
        }
    }

    px::Result<px_console::ConsoleConnectionTicket, px_console::ConsoleApiError> PxUserManager::IssueDeviceTicket(
        const std::string& device_id,
        const std::string& client_nonce,
        const std::vector<std::string>& requested_permissions) {
        auto host = settings_->GetConsoleServerHost();
        auto port = settings_->GetConsoleServerPort();
        auto result = px_console::ConsoleUserDeviceApi::IssueDeviceTicket(
            host, port, GetAccessToken(), device_id, client_nonce, requested_permissions);
        if (!result.has_value()
            && result.error() == px_console::ConsoleApiError::kAuthenticationRequired) {
            HandleExpiredUserSession();
        }
        return result;
    }

    px::Result<px_console::ConsoleConnectionTicket, px_console::ConsoleApiError>
    PxUserManager::RenewConnectionTicket(const std::string& renewal_token,
                                         const std::string& client_nonce) {
        return px_console::ConsoleUserDeviceApi::RenewConnectionTicket(
            settings_->GetConsoleServerHost(), settings_->GetConsoleServerPort(),
            renewal_token, client_nonce);
    }

    bool PxUserManager::Register(const std::string& username, const std::string& password) {
        auto [guest_access_token, guest, session_error] = ResourceSession();
        if (guest_access_token.empty() || !guest) {
            const auto message = MakeConsoleErrorMessage(ConsoleErrorOperation::kRegister,
                session_error.value_or(px_console::ConsoleApiError::kInternalError),
                px_console::ConsoleApiLastErrorMessage(), MakeConsoleEndpoint(
                    settings_->GetConsoleServerHost(), settings_->GetConsoleServerPort()));
            TcDialog dialog(tcTr("id_error"), message);
            dialog.exec();
            return false;
        }
        const auto result = px_console::ConsoleUserApi::Register(
            settings_->GetConsoleServerHost(), settings_->GetConsoleServerPort(),
            guest_access_token, username, password);
        if (result.has_value()) {
            ClearGuestSession();
            context_->NotifyAppMessage(tcTr("id_tips"), tcTr("id_register_success"));
            return true;
        }
        const auto error = result.error();
        const auto message = MakeConsoleErrorMessage(ConsoleErrorOperation::kRegister, error,
            px_console::ConsoleApiLastErrorMessage(), MakeConsoleEndpoint(
                settings_->GetConsoleServerHost(), settings_->GetConsoleServerPort()));
        TcDialog dialog(tcTr("id_error"), message);
        dialog.exec();
        return false;
    }

    px::Result<std::vector<px_console::ConsoleUserApplication>, px_console::ConsoleApiError>
    PxUserManager::QueryApps() {
        auto [token, guest, session_error] = ResourceSession();
        if (token.empty()) return TcErr(session_error.value_or(px_console::ConsoleApiError::kInternalError));
        auto result = px_console::ConsoleUserAppApi::QueryApps(settings_->GetConsoleServerHost(),
            settings_->GetConsoleServerPort(), token, guest);
        if (!result.has_value()
            && result.error() == px_console::ConsoleApiError::kAuthenticationRequired) {
            if (guest) ClearGuestSession(); else HandleExpiredUserSession();
            std::tie(token, guest, session_error) = ResourceSession();
            if (!token.empty()) result = px_console::ConsoleUserAppApi::QueryApps(
                settings_->GetConsoleServerHost(), settings_->GetConsoleServerPort(), token, guest);
        }
        return result;
    }

    px::Result<px_console::ConsoleUserAppInstance, px_console::ConsoleApiError>
    PxUserManager::StartApp(const std::string& app_id, const std::string& client_nonce) {
        auto [token, guest, session_error] = ResourceSession();
        if (token.empty()) return TcErr(session_error.value_or(px_console::ConsoleApiError::kInternalError));
        auto result = px_console::ConsoleUserAppApi::StartApp(settings_->GetConsoleServerHost(),
            settings_->GetConsoleServerPort(), token, app_id, client_nonce, guest);
        if (!result.has_value()
            && result.error() == px_console::ConsoleApiError::kAuthenticationRequired) {
            if (guest) ClearGuestSession(); else HandleExpiredUserSession();
            std::tie(token, guest, session_error) = ResourceSession();
            if (!token.empty()) result = px_console::ConsoleUserAppApi::StartApp(
                settings_->GetConsoleServerHost(), settings_->GetConsoleServerPort(), token,
                app_id, client_nonce, guest);
        }
        return result;
    }

    px::Result<px_console::ConsoleConnectionTicket, px_console::ConsoleApiError>
    PxUserManager::IssueInstanceTicket(const std::string& instance_id,
        const std::string& client_nonce,
        const std::vector<std::string>& requested_permissions) {
        auto [token, guest, session_error] = ResourceSession();
        if (token.empty()) return TcErr(session_error.value_or(px_console::ConsoleApiError::kInternalError));
        auto result = px_console::ConsoleUserAppApi::IssueInstanceTicket(settings_->GetConsoleServerHost(),
            settings_->GetConsoleServerPort(), token, instance_id, client_nonce,
            requested_permissions, guest);
        if (!result.has_value()
            && result.error() == px_console::ConsoleApiError::kAuthenticationRequired) {
            // A new guest session does not own the old instance, so this retry
            // will intentionally fail with 404. The caller then refreshes the
            // catalog and starts a new instance under the new guest identity.
            if (guest) ClearGuestSession(); else HandleExpiredUserSession();
            std::tie(token, guest, session_error) = ResourceSession();
            if (!token.empty()) result = px_console::ConsoleUserAppApi::IssueInstanceTicket(
                settings_->GetConsoleServerHost(), settings_->GetConsoleServerPort(), token,
                instance_id, client_nonce, requested_permissions, guest);
        }
        return result;
    }

    px::Result<px_console::ConsoleUserAppInstance, px_console::ConsoleApiError>
    PxUserManager::StopInstance(const std::string& instance_id) {
        auto [token, guest, session_error] = ResourceSession();
        if (token.empty()) return TcErr(session_error.value_or(px_console::ConsoleApiError::kInternalError));
        auto result = px_console::ConsoleUserAppApi::StopInstance(settings_->GetConsoleServerHost(),
            settings_->GetConsoleServerPort(), token, instance_id, guest);
        if (!result.has_value()
            && result.error() == px_console::ConsoleApiError::kAuthenticationRequired) {
            if (guest) ClearGuestSession(); else HandleExpiredUserSession();
            std::tie(token, guest, session_error) = ResourceSession();
            if (!token.empty()) result = px_console::ConsoleUserAppApi::StopInstance(
                settings_->GetConsoleServerHost(), settings_->GetConsoleServerPort(), token,
                instance_id, guest);
        }
        return result;
    }

    std::tuple<std::string, bool, std::optional<px_console::ConsoleApiError>>
    PxUserManager::ResourceSession() {
        if (auto token = GetAccessToken(); !token.empty()) {
            return {std::move(token), false, std::nullopt};
        }
        std::lock_guard<std::mutex> guard(guest_session_mutex_);
        if (guest_access_token_.empty()) {
            const auto nonce = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
            const auto result = px_console::ConsoleUserAppApi::CreateGuestSession(
                settings_->GetConsoleServerHost(), settings_->GetConsoleServerPort(), nonce);
            if (!result.has_value()) return {{}, true, result.error()};
            guest_access_token_ = result.value();
        }
        return {guest_access_token_, true, std::nullopt};
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
        credential.UserName = const_cast<wchar_t*>(L"Pixels Console user session");
        if (!CredWriteW(&credential, 0)) {
            LOGE("Save Console user session failed, win32 error: {}", GetLastError());
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
            LOGW("Delete Console user session failed, win32 error: {}", GetLastError());
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
        return std::format("{}{}", kUserPrefix, px_console::kUserId);
    }

    std::string PxUserManager::KeyUsername() {
        return std::format("{}{}", kUserPrefix, px_console::kUserName);
    }

    std::wstring PxUserManager::CredentialTarget() const {
        return QString::fromStdString(std::format("Pixels.Console.UserSession.{}:{}", settings_->GetConsoleServerHost(), settings_->GetConsoleServerPort())).toStdWString();
    }

    std::string PxUserManager::KeyAvatarPath() {
        return std::format("{}{}", kUserPrefix, px_console::kUserAvatarPath);
    }

}
