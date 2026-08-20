//
// Created by RGAA on 18/11/2025.
//

#ifndef GAMMARAYPREMIUM_USERMANAGER_H
#define GAMMARAYPREMIUM_USERMANAGER_H

#include <memory>
#include <string>
#include <vector>

namespace px_cms
{
    class CmsUserDevice;
}

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
        std::vector<std::shared_ptr<px_cms::CmsUserDevice>> QueryBindDevices(int page, int page_size, bool show_dialog);
        std::shared_ptr<px_cms::CmsUserDevice> AddDeviceForUser(const std::string& device_id);
        std::shared_ptr<px_cms::CmsUserDevice> RemoveDeviceFromUser(const std::string& device_id);

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

        static std::string KeyUid();
        static std::string KeyUsername();
        std::wstring CredentialTarget() const;
        static std::string KeyAvatarPath();

    private:
        PxSettings* settings_ = nullptr;
        std::shared_ptr<PxContext> context_ = nullptr;

    };

}

#endif //GAMMARAYPREMIUM_USERMANAGER_H
