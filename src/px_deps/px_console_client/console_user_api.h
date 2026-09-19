//
// Created by RGAA on 31/10/2025.
//

#ifndef PIXELSPREMIUM_CONSOLE_USER_API_H
#define PIXELSPREMIUM_CONSOLE_USER_API_H

#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "console_errors.h"
#include "console_user.h"
#include "px_common/expected.h"

namespace px_console {

class ConsoleUserApi {
public:
    static px::Result<ConsoleUserPtr, ConsoleApiError> Register(const std::string& host, int port, const std::string& username,
                                                                const std::string& password);

    // login
    static px::Result<ConsoleUserLoginResult, ConsoleApiError> Login(const std::string& host, int port, const std::string& username,
                                                                     const std::string& password);

    // logout
    static px::Result<bool, ConsoleApiError> Logout(const std::string& host, int port, const std::string& access_token);

    // update the authenticated user's profile
    static px::Result<ConsoleUserPtr, ConsoleApiError> UpdateProfile(const std::string& host, int port, const std::string& access_token,
                                                                     const std::string& username);

    static px::Result<bool, ConsoleApiError> UpdatePassword(const std::string& host, int port, const std::string& access_token,
                                                            const std::string& old_password, const std::string& new_password);

    // update avatar
    static px::Result<ConsoleUserPtr, ConsoleApiError> UpdateAvatar(const std::string& host, int port, const std::string& access_token,
                                                                    const std::string& avatar_path);
};

}  // namespace px_console

#endif  // PIXELSPREMIUM_CONSOLE_USER_API_H
