//
// Created by RGAA on 31/10/2025.
//

#ifndef GAMMARAYPREMIUM_SPVR_USER_API_H
#define GAMMARAYPREMIUM_SPVR_USER_API_H

#include <map>
#include <string>
#include <vector>
#include <tuple>
#include <memory>

#include "tc_common_new/expected.h"
#include "spvr_errors.h"
#include "spvr_user.h"

namespace spvr
{

    class SpvrUserApi {
    public:
        // register
        static
        tc::Result<SpvrUserPtr, SpvrApiError>
        Register(const std::string& host,
                 int port,
                 const std::string& appkey,
                 const std::string& username,
                 const std::string& hash_password);

        // login
        static
        tc::Result<SpvrUserPtr, SpvrApiError>
        Login(const std::string& host,
              int port,
              const std::string& appkey,
              const std::string& username,
              const std::string& hash_password,
              const std::string& device_id);

        // logout
        static
        tc::Result<SpvrUserPtr, SpvrApiError>
        Logout(const std::string& host,
               int port,
               const std::string& appkey,
               const std::string& uid,
               const std::string& hash_password);

        // update
        static
        tc::Result<SpvrUserPtr, SpvrApiError>
        Update(const std::string& host,
               int port,
               const std::string& appkey,
               const std::string& uid,
               const std::string& hash_password,
               const std::map<std::string, std::string>& values);

        static
        tc::Result<SpvrUserPtr, SpvrApiError>
        UpdatePassword(const std::string& host,
                       int port,
                       const std::string& appkey,
                       const std::string& uid,
                       const std::string& old_hash_password,
                       const std::string& new_hash_password);

        // update avatar
        static
        tc::Result<SpvrUserPtr, SpvrApiError>
        UpdateAvatar(const std::string& host,
                     int port,
                     const std::string& appkey,
                     const std::string& uid,
                     const std::string& hash_password,
                     const std::string& avatar_path);
    };

}

#endif //GAMMARAYPREMIUM_SPVR_USER_API_H
