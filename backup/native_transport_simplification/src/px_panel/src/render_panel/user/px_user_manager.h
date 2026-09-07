//
// Created by RGAA on 18/11/2025.
//

#ifndef GAMMARAYPREMIUM_USERMANAGER_H
#define GAMMARAYPREMIUM_USERMANAGER_H

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
#include "px_console_client/console_user_device_api.h"
#include "px_console_client/console_user_app_api.h"

namespace px
{

    class PxContext;
    class PxSettings;

    class PxUserManager {
    public:
        explicit PxUserManager(const std::shared_ptr<PxContext>& ctx);
        bool Register(const std::string& username, const std::string& password);
        bool Login(const std::string& username, const std::string& password, bool show_dialog = true);
        bool Logout();
        bool ModifyUsername(const std::string& username);
        bool ModifyPassword(const std::string& current_password, const std::string& new_password);
        bool UpdateAvatar(const std::string& avatar_path);
        // user - device
        px::Result<std::vector<std::shared_ptr<px_console::ConsoleUserDevice>>, px_console::ConsoleApiError>
        QueryBindDevices(int page, int page_size, bool show_dialog);
        px::Result<px_console::ConsoleConnectionTicket, px_console::ConsoleApiError> IssueDeviceTicket(
            const std::string& device_id,
            const std::string& client_nonce,
            const std::vector<std::string>& requested_permissions);
        px::Result<px_console::ConsoleConnectionTicket, px_console::ConsoleApiError> RenewConnectionTicket(
            const std::string& renewal_token, const std::string& client_nonce);
        px::Result<std::vector<px_console::ConsoleUserApplication>, px_console::ConsoleApiError> QueryApps();
        px::Result<px_console::ConsoleUserAppInstance, px_console::ConsoleApiError> StartApp(
            const std::string& app_id, const std::string& client_nonce);
        px::Result<px_console::ConsoleConnectionTicket, px_console::ConsoleApiError> IssueInstanceTicket(
            const std::string& instance_id, const std::string& client_nonce,
            const std::vector<std::string>& requested_permissions);
        px::Result<px_console::ConsoleUserAppInstance, px_console::ConsoleApiError> StopInstance(
            const std::string& instance_id);

        bool IsLoggedIn();
        std::string GetUserId();
        std::string GetUsername();
        std::string GetAccessToken();
        std::string GetAvatarPath();
        void Clear();

    private:
        bool SaveUserInfo(const std::string& uid, const std::string& username, const std::string& access_token, const std::string& avatar_path);
        void UpdateUsername(const std::string& username);
        bool SaveAccessToken(const std::string& access_token);
        void DeleteAccessToken();
        void UpdateAvatarPath(const std::string& avatar_path);
        std::tuple<std::string, bool, std::optional<px_console::ConsoleApiError>> ResourceSession();
        void ClearGuestSession();
        void HandleExpiredUserSession();

        static std::string KeyUid();
        static std::string KeyUsername();
        std::wstring CredentialTarget() const;
        static std::string KeyAvatarPath();

    private:
        PxSettings* settings_ = nullptr;
        std::shared_ptr<PxContext> context_ = nullptr;
        std::mutex guest_session_mutex_;
        std::string guest_access_token_;

    };

}

#endif //GAMMARAYPREMIUM_USERMANAGER_H
