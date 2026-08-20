//
// Created by RGAA on 27/03/2025.
//

#ifndef PX_CMS_ERRORS_H
#define PX_CMS_ERRORS_H

#include <string>

namespace px_cms
{

    // Business error codes, keep in sync with the CMS server side:
    // rust_server/px_cms_server/src/cms_api_error.rs
    enum class CmsApiError {
        kInvalidHostAddress = 1,
        kParseJsonFailed,

        kAuthenticationRequired = 401,
        kForbidden = 403,
        kNotFound = 404,
        kConflict = 409,

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

    static std::string CmsApiErrorAsString(const CmsApiError& err) {
        switch (err) {
            case CmsApiError::kInvalidHostAddress: return "Invalid host address";
            case CmsApiError::kParseJsonFailed: return "Parse server response failed";
            case CmsApiError::kAuthenticationRequired: return "Authentication required";
            case CmsApiError::kForbidden: return "Operation forbidden";
            case CmsApiError::kNotFound: return "Resource not found";
            case CmsApiError::kConflict: return "Resource state conflict";
            case CmsApiError::kInvalidParams: return "Invalid parameters";
            case CmsApiError::kDatabaseError: return "Database operation failed";
            case CmsApiError::kDeviceNotFound: return "Device not found";
            case CmsApiError::kPasswordInvalid: return "Password invalid";
            case CmsApiError::kInvalidAppkey: return "Invalid appkey, authorization is invalid or expired";
            case CmsApiError::kCreateDeviceFailed: return "Create device failed";
            case CmsApiError::kInvalidAuthorization: return "Invalid authorization";
            case CmsApiError::kInternalError: return "Internal error";
            case CmsApiError::kUserAlreadyExists: return "User already exists";
            case CmsApiError::kUserNotFound: return "User not found";
            case CmsApiError::kUserUpdateFailed: return "User update failed";
            case CmsApiError::kFileNoExtension: return "File no extension";
            case CmsApiError::kUploadFileFailed: return "Upload file failed";
            case CmsApiError::kVerifyPasswordFailed: return "Verify password failed";
            case CmsApiError::kStreamNotFound: return "Stream not found";
            case CmsApiError::kConnectionNotFound: return "Connection not found";
            case CmsApiError::kUserDeviceNotFound: return "User-device not found";
            case CmsApiError::kUserDeviceAlreadyExists: return "User-device already exists";
            case CmsApiError::kNeedDescParam: return "Need description";
            case CmsApiError::kNeedVersionParam: return "Need version";
            case CmsApiError::kVersionNotFound: return "Version not found";
            case CmsApiError::kFileNotFound: return "File not found";
            case CmsApiError::kVisitNotFound: return "Visit not found";
            case CmsApiError::kMachineCodeNotMatched: return "Machine code not matched";
            case CmsApiError::kMaxStreamsReached: return "Max streams reached, no available connection";
            case CmsApiError::kFileTransferNotFound: return "File transfer not found";
            default: return "Unknown error";
        }
    }

    // The CMS returns error responses as a json body: {code, message, data}.
    // The api layer stores the latest server-side message here, so the UI can
    // display the server's own words instead of a bare error code.
    inline std::string& CmsApiLastErrorMessageStorage() {
        static thread_local std::string last_error_message;
        return last_error_message;
    }

    inline void SetCmsApiLastErrorMessage(const std::string& message) {
        CmsApiLastErrorMessageStorage() = message;
    }

    inline std::string CmsApiLastErrorMessage() {
        return CmsApiLastErrorMessageStorage();
    }

}

#endif //PX_CMS_ERRORS_H
