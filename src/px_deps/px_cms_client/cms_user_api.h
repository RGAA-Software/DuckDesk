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
        // register
        static
        px::Result<CmsUserPtr, CmsApiError>
        Register(const std::string& host,
                 int port,
                 const std::string& appkey,
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

        // update
        static
        px::Result<CmsUserPtr, CmsApiError>
        Update(const std::string& host,
               int port,
               const std::string& appkey,
               const std::string& uid,
               const std::string& hash_password,
               const std::map<std::string, std::string>& values);

        static
        px::Result<CmsUserPtr, CmsApiError>
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
                     const std::string& appkey,
                     const std::string& uid,
                     const std::string& avatar_path);
    };

}

#endif //GAMMARAYPREMIUM_CMS_USER_API_H
