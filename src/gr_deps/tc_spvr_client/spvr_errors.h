//
// Created by RGAA on 27/03/2025.
//

#ifndef GAMMARAY_SPVR_ERRORS_H
#define GAMMARAY_SPVR_ERRORS_H

#include <string>

namespace spvr
{

    // Business error codes, keep in sync with the CMS server side:
    // rust_server/gr_cms_server/src/spvr_api_error.rs
    enum class SpvrApiError {
        kInvalidHostAddress = 1,
        kParseJsonFailed,

        kInvalidParams = 600,
        kDatabaseError = 601,
        kDeviceNotFound = 602,
        kPasswordInvalid = 603,
        kInvalidAppkey = 604,
        kCreateDeviceFailed = 605,
        kInvalidAuthorization = 606,
        kInternalError = 607,
        kUserAlreadyExists = 608,
        kUserNotFound = 609,
        kUserUpdateFailed = 610,
        kFileNoExtension = 611,
        kUploadFileFailed = 612,
        kVerifyPasswordFailed = 613,
        kStreamNotFound = 614,
        kConnectionNotFound = 615,
        kUserDeviceNotFound = 616,
        kUserDeviceAlreadyExists = 617,
        kNeedDescParam = 618,
        kNeedVersionParam = 619,
        kVersionNotFound = 620,
        kFileNotFound = 621,
        kVisitNotFound = 622,
        kMachineCodeNotMatched = 623,
        kMaxStreamsReached = 624,
        kFileTransferNotFound = 625,
    };

    static std::string SpvrApiErrorAsString(const SpvrApiError& err) {
        switch (err) {
            case SpvrApiError::kInvalidHostAddress: return "Invalid host address";
            case SpvrApiError::kParseJsonFailed: return "Parse server response failed";
            case SpvrApiError::kInvalidParams: return "Invalid parameters";
            case SpvrApiError::kDatabaseError: return "Database operation failed";
            case SpvrApiError::kDeviceNotFound: return "Device not found";
            case SpvrApiError::kPasswordInvalid: return "Password invalid";
            case SpvrApiError::kInvalidAppkey: return "Invalid appkey, authorization is invalid or expired";
            case SpvrApiError::kCreateDeviceFailed: return "Create device failed";
            case SpvrApiError::kInvalidAuthorization: return "Invalid authorization";
            case SpvrApiError::kInternalError: return "Internal error";
            case SpvrApiError::kUserAlreadyExists: return "User already exists";
            case SpvrApiError::kUserNotFound: return "User not found";
            case SpvrApiError::kUserUpdateFailed: return "User update failed";
            case SpvrApiError::kFileNoExtension: return "File no extension";
            case SpvrApiError::kUploadFileFailed: return "Upload file failed";
            case SpvrApiError::kVerifyPasswordFailed: return "Verify password failed";
            case SpvrApiError::kStreamNotFound: return "Stream not found";
            case SpvrApiError::kConnectionNotFound: return "Connection not found";
            case SpvrApiError::kUserDeviceNotFound: return "User-device not found";
            case SpvrApiError::kUserDeviceAlreadyExists: return "User-device already exists";
            case SpvrApiError::kNeedDescParam: return "Need description";
            case SpvrApiError::kNeedVersionParam: return "Need version";
            case SpvrApiError::kVersionNotFound: return "Version not found";
            case SpvrApiError::kFileNotFound: return "File not found";
            case SpvrApiError::kVisitNotFound: return "Visit not found";
            case SpvrApiError::kMachineCodeNotMatched: return "Machine code not matched";
            case SpvrApiError::kMaxStreamsReached: return "Max streams reached, no available connection";
            case SpvrApiError::kFileTransferNotFound: return "File transfer not found";
            default: return "Unknown error";
        }
    }

    // The CMS returns error responses as a json body: {code, message, data}.
    // The api layer stores the latest server-side message here, so the UI can
    // display the server's own words instead of a bare error code.
    inline std::string& SpvrApiLastErrorMessageStorage() {
        static thread_local std::string last_error_message;
        return last_error_message;
    }

    inline void SetSpvrApiLastErrorMessage(const std::string& message) {
        SpvrApiLastErrorMessageStorage() = message;
    }

    inline std::string SpvrApiLastErrorMessage() {
        return SpvrApiLastErrorMessageStorage();
    }

}

#endif //GAMMARAY_SPVR_ERRORS_H
