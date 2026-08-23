//
// Created by RGAA on 11/04/2025.
//

#ifndef PX_PROFILE_API_H
#define PX_PROFILE_API_H

#include <string>

namespace px
{

    enum class ProfileVerifyResult {
        kVfParamInvalid,
        kVfServerInternalError,
        kVfDeviceNotFound,
        kVfEmptyDeviceId,
        kVfEmptyServerHost,
        kVfNetworkFailed,
        kVfResponseFailed,
        kVfParseJsonFailed,
        kVfSuccessRandomPwd, // 8
        kVfSuccessSafetyPwd,
        kVfSuccessAllPwd,
        kVfPasswordFailed,
    };

    // HTTP CODE
    // see pr_error.rs
    constexpr int kERR_PARAM_INVALID = 600;
    constexpr int kERR_OPERATE_DB_FAILED = 601;
    constexpr int kERR_DEVICE_NOT_FOUND = 602;
    constexpr int kERR_PASSWORD_FAILED = 603;

    class ProfileApi {
    public:
        // whether the profile(console) server requires ssl(https), default true for old deployments.
        // the panel process syncs this switch from PxSettings(console_ssl_enable).
        static void SetSslEnabled(bool enabled);
        static bool IsSslEnabled();

        // verify device_id/random_pwd pair
        // pr_srv_host: profile server host
        // pr_srv_port: profile server port
        // device id
        // md5 random pwd
        // md5 safety pwd
        static ProfileVerifyResult VerifyDeviceInfo(const std::string& pr_srv_host,
                                                    int pr_srv_port,
                                                    const std::string& device_id,
                                                    const std::string& random_pwd_md5,
                                                    const std::string& safety_pwd_md5,
                                                    const std::string& appkey);
    };

}

#endif //PX_PROFILE_API_H
