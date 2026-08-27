//
// Created by RGAA on 27/03/2025.
//

#ifndef PX_CONSOLE_ERRORS_H
#define PX_CONSOLE_ERRORS_H

#include <string>

namespace px_console
{

    // Business error codes, keep in sync with the Console server side:
    // rust_server/px_console_server/src/console_api_error.rs
    enum class ConsoleApiError {
        kInvalidHostAddress = 1,
        kParseJsonFailed,
        kNetworkUnavailable,

        kAuthenticationRequired = 401,
        kForbidden = 403,
        kNotFound = 404,
        kConflict = 409,
        kGone = 410,
        kRateLimited = 429,
        kServiceUnavailable = 503,

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

    static std::string ConsoleApiErrorAsString(const ConsoleApiError& err) {
        switch (err) {
            case ConsoleApiError::kInvalidHostAddress: return "Invalid host address";
            case ConsoleApiError::kParseJsonFailed: return "Parse server response failed";
            case ConsoleApiError::kNetworkUnavailable: return "Console network unavailable";
            case ConsoleApiError::kAuthenticationRequired: return "Authentication required";
            case ConsoleApiError::kForbidden: return "Operation forbidden";
            case ConsoleApiError::kNotFound: return "Resource not found";
            case ConsoleApiError::kConflict: return "Resource state conflict";
            case ConsoleApiError::kGone: return "Ticket expired or already used";
            case ConsoleApiError::kRateLimited: return "Request rate or quota exceeded";
            case ConsoleApiError::kServiceUnavailable: return "Device or scheduler unavailable";
            case ConsoleApiError::kInvalidParams: return "Invalid parameters";
            case ConsoleApiError::kDatabaseError: return "Database operation failed";
            case ConsoleApiError::kDeviceNotFound: return "Device not found";
            case ConsoleApiError::kPasswordInvalid: return "Password invalid";
            case ConsoleApiError::kInvalidAppkey: return "Invalid appkey, authorization is invalid or expired";
            case ConsoleApiError::kCreateDeviceFailed: return "Create device failed";
            case ConsoleApiError::kInvalidAuthorization: return "Invalid authorization";
            case ConsoleApiError::kInternalError: return "Internal error";
            case ConsoleApiError::kUserAlreadyExists: return "User already exists";
            case ConsoleApiError::kUserNotFound: return "User not found";
            case ConsoleApiError::kUserUpdateFailed: return "User update failed";
            case ConsoleApiError::kFileNoExtension: return "File no extension";
            case ConsoleApiError::kUploadFileFailed: return "Upload file failed";
            case ConsoleApiError::kVerifyPasswordFailed: return "Verify password failed";
            case ConsoleApiError::kStreamNotFound: return "Stream not found";
            case ConsoleApiError::kConnectionNotFound: return "Connection not found";
            case ConsoleApiError::kUserDeviceNotFound: return "User-device not found";
            case ConsoleApiError::kUserDeviceAlreadyExists: return "User-device already exists";
            case ConsoleApiError::kNeedDescParam: return "Need description";
            case ConsoleApiError::kNeedVersionParam: return "Need version";
            case ConsoleApiError::kVersionNotFound: return "Version not found";
            case ConsoleApiError::kFileNotFound: return "File not found";
            case ConsoleApiError::kVisitNotFound: return "Visit not found";
            case ConsoleApiError::kMachineCodeNotMatched: return "Machine code not matched";
            case ConsoleApiError::kMaxStreamsReached: return "Max streams reached, no available connection";
            case ConsoleApiError::kFileTransferNotFound: return "File transfer not found";
            default: return "Unknown error";
        }
    }

    // The Console returns error responses as a json body: {code, message, data}.
    // The api layer stores the latest server-side message here, so the UI can
    // display the server's own words instead of a bare error code.
    inline std::string& ConsoleApiLastErrorMessageStorage() {
        static thread_local std::string last_error_message;
        return last_error_message;
    }

    inline void SetConsoleApiLastErrorMessage(const std::string& message) {
        ConsoleApiLastErrorMessageStorage() = message;
    }

    inline std::string ConsoleApiLastErrorMessage() {
        return ConsoleApiLastErrorMessageStorage();
    }

}

#endif //PX_CONSOLE_ERRORS_H
