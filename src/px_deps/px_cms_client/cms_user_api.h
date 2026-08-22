//
// Created by RGAA on 31/10/2025.
//

#ifndef GAMMARAYPREMIUM_CMS_USER_API_H
#define GAMMARAYPREMIUM_CMS_USER_API_H

#include <map>
#include <string>
#include <vector>
#include <tuple>
#include <memory>

#include "px_common_new/expected.h"
#include "cms_errors.h"
#include "cms_user.h"

namespace px_cms
{

    class CmsUserApi {
    public:
        static
        px::Result<CmsUserPtr, CmsApiError>
        Register(const std::string& host,
                 int port,
                 const std::string& guest_access_token,
                 const std::string& username,
                 const std::string& password);

        // login
        static
        px::Result<CmsUserLoginResult, CmsApiError>
        Login(const std::string& host,
              int port,
              const std::string& username,
              const std::string& password);

        // logout
        static
        px::Result<bool, CmsApiError>
        Logout(const std::string& host,
               int port,
               const std::string& access_token);

        // update the authenticated user's profile
        static
        px::Result<CmsUserPtr, CmsApiError>
        UpdateProfile(const std::string& host,
                      int port,
                      const std::string& access_token,
                      const std::string& username);

        static
        px::Result<CmsUserLoginResult, CmsApiError>
        UpdatePassword(const std::string& host,
                       int port,
                       const std::string& access_token,
                       const std::string& old_password,
                       const std::string& new_password);

        // update avatar
        static
        px::Result<CmsUserPtr, CmsApiError>
        UpdateAvatar(const std::string& host,
                     int port,
                     const std::string& access_token,
                     const std::string& avatar_path);
    };

}

#endif //GAMMARAYPREMIUM_CMS_USER_API_H
