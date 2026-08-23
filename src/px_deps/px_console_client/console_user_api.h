//
// Created by RGAA on 31/10/2025.
//

#ifndef GAMMARAYPREMIUM_CONSOLE_USER_API_H
#define GAMMARAYPREMIUM_CONSOLE_USER_API_H

#include <map>
#include <string>
#include <vector>
#include <tuple>
#include <memory>

#include "px_common_new/expected.h"
#include "console_errors.h"
#include "console_user.h"

namespace px_console
{

    class ConsoleUserApi {
    public:
        static
        px::Result<ConsoleUserPtr, ConsoleApiError>
        Register(const std::string& host,
                 int port,
                 const std::string& guest_access_token,
                 const std::string& username,
                 const std::string& password);

        // login
        static
        px::Result<ConsoleUserLoginResult, ConsoleApiError>
        Login(const std::string& host,
              int port,
              const std::string& username,
              const std::string& password);

        // logout
        static
        px::Result<bool, ConsoleApiError>
        Logout(const std::string& host,
               int port,
               const std::string& access_token);

        // update the authenticated user's profile
        static
        px::Result<ConsoleUserPtr, ConsoleApiError>
        UpdateProfile(const std::string& host,
                      int port,
                      const std::string& access_token,
                      const std::string& username);

        static
        px::Result<ConsoleUserLoginResult, ConsoleApiError>
        UpdatePassword(const std::string& host,
                       int port,
                       const std::string& access_token,
                       const std::string& old_password,
                       const std::string& new_password);

        // update avatar
        static
        px::Result<ConsoleUserPtr, ConsoleApiError>
        UpdateAvatar(const std::string& host,
                     int port,
                     const std::string& access_token,
                     const std::string& avatar_path);
    };

}

#endif //GAMMARAYPREMIUM_CONSOLE_USER_API_H
