//
// Created by RGAA on 18/11/2025.
//

#ifndef GAMMARAYPREMIUM_USERMANAGER_H
#define GAMMARAYPREMIUM_USERMANAGER_H

#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
#include "px_cms_client/cms_user_device_api.h"
#include "px_cms_client/cms_user_app_api.h"

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
        px::Result<std::vector<std::shared_ptr<px_cms::CmsUserDevice>>, px_cms::CmsApiError>
        QueryBindDevices(int page, int page_size, bool show_dialog);
        px::Result<px_cms::CmsConnectionTicket, px_cms::CmsApiError> IssueDeviceTicket(
            const std::string& device_id,
            const std::string& client_nonce,
            const std::vector<std::string>& requested_permissions);
        px::Result<std::vector<px_cms::CmsUserApplication>, px_cms::CmsApiError> QueryApps();
        px::Result<px_cms::CmsUserAppInstance, px_cms::CmsApiError> StartApp(
            const std::string& app_id, const std::string& client_nonce);
        px::Result<px_cms::CmsConnectionTicket, px_cms::CmsApiError> IssueInstanceTicket(
            const std::string& instance_id, const std::string& client_nonce,
            const std::vector<std::string>& requested_permissions);
        px::Result<px_cms::CmsUserAppInstance, px_cms::CmsApiError> StopInstance(
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
        std::pair<std::string, bool> ResourceSession();
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
