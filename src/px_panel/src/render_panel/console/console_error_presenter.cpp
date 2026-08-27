#include "console_error_presenter.h"

#include <QRegularExpression>

#include "translator/px_translator.h"

namespace px {
namespace {

    QString OperationText(ConsoleErrorOperation operation) {
        switch (operation) {
            case ConsoleErrorOperation::kConnectRemote: return tcTr("id_error_stage_connect_remote");
            case ConsoleErrorOperation::kSignIn: return tcTr("id_error_stage_sign_in");
            case ConsoleErrorOperation::kSignOut: return tcTr("id_error_stage_sign_out");
            case ConsoleErrorOperation::kRegister: return tcTr("id_error_stage_register");
            case ConsoleErrorOperation::kUpdateAccount: return tcTr("id_error_stage_update_account");
            case ConsoleErrorOperation::kLoadResources: return tcTr("id_error_stage_load_resources");
            case ConsoleErrorOperation::kStartApplication: return tcTr("id_error_stage_start_application");
            case ConsoleErrorOperation::kStopApplication: return tcTr("id_error_stage_stop_application");
            case ConsoleErrorOperation::kFileTransfer: return tcTr("id_error_stage_file_transfer");
            case ConsoleErrorOperation::kUpdateDevice: return tcTr("id_error_stage_update_device");
            case ConsoleErrorOperation::kCheckConsole: return tcTr("id_error_stage_check_console");
        }
        return tcTr("id_error_stage_check_console");
    }

    struct Guidance {
        QString reason;
        QString action;
    };

    Guidance ErrorGuidance(px_console::ConsoleApiError error) {
        using Error = px_console::ConsoleApiError;
        switch (error) {
            case Error::kInvalidHostAddress:
                return {tcTr("id_console_reason_invalid_address"),
                        tcTr("id_console_action_check_address")};
            case Error::kNetworkUnavailable:
                return {tcTr("id_console_reason_unreachable"),
                        tcTr("id_console_action_check_service")};
            case Error::kParseJsonFailed:
                return {tcTr("id_console_reason_invalid_response"),
                        tcTr("id_console_action_check_version")};
            case Error::kAuthenticationRequired:
                return {tcTr("id_console_reason_session_expired"),
                        tcTr("id_console_action_sign_in_again")};
            case Error::kForbidden:
                return {tcTr("id_console_reason_forbidden"),
                        tcTr("id_console_action_contact_admin")};
            case Error::kNotFound:
            case Error::kDeviceNotFound:
            case Error::kUserNotFound:
            case Error::kStreamNotFound:
            case Error::kConnectionNotFound:
            case Error::kUserDeviceNotFound:
            case Error::kFileNotFound:
            case Error::kVisitNotFound:
            case Error::kFileTransferNotFound:
                return {tcTr("id_console_reason_resource_missing"),
                        tcTr("id_console_action_refresh_resources")};
            case Error::kConflict:
            case Error::kUserDeviceAlreadyExists:
                return {tcTr("id_console_reason_conflict"),
                        tcTr("id_console_action_refresh_resources")};
            case Error::kGone:
                return {tcTr("id_console_reason_ticket_expired"),
                        tcTr("id_console_action_retry")};
            case Error::kRateLimited:
                return {tcTr("id_console_reason_rate_limited"),
                        tcTr("id_console_action_retry_later")};
            case Error::kMaxStreamsReached:
                return {tcTr("id_console_reason_stream_limit"),
                        tcTr("id_console_action_close_stream")};
            case Error::kServiceUnavailable:
                return {tcTr("id_console_reason_service_unavailable"),
                        tcTr("id_console_action_check_device")};
            case Error::kPasswordInvalid:
            case Error::kVerifyPasswordFailed:
                return {tcTr("id_console_reason_password_invalid"),
                        tcTr("id_console_action_check_password")};
            case Error::kInvalidAppkey:
            case Error::kInvalidAuthorization:
            case Error::kMachineCodeNotMatched:
                return {tcTr("id_console_reason_authorization_invalid"),
                        tcTr("id_console_action_contact_admin")};
            case Error::kUserAlreadyExists:
                return {tcTr("id_console_reason_user_exists"),
                        tcTr("id_console_action_change_username")};
            case Error::kInvalidParams:
            case Error::kNeedDescParam:
            case Error::kNeedVersionParam:
            case Error::kFileNoExtension:
                return {tcTr("id_console_reason_invalid_request"),
                        tcTr("id_console_action_check_input")};
            default:
                return {tcTr("id_console_reason_internal"),
                        tcTr("id_console_action_retry_check_logs")};
        }
    }

    QString SafeDetail(const std::string& server_message) {
        auto detail = QString::fromStdString(server_message).trimmed();
        if (detail.isEmpty()) return {};
        static const QRegularExpression secret_pattern(
            R"((ticket|token|authorization|password)\s*[:=]\s*[^\s,;]+)",
            QRegularExpression::CaseInsensitiveOption);
        detail.replace(secret_pattern, QStringLiteral("\\1=<redacted>"));
        detail.replace('\r', ' ');
        detail.replace('\n', ' ');
        constexpr auto kMaxDetailLength = 300;
        if (detail.size() > kMaxDetailLength) {
            detail = detail.left(kMaxDetailLength) + QStringLiteral("…");
        }
        return detail;
    }

}

QString MakeConsoleEndpoint(const std::string& host, int port) {
    if (host.empty() || port <= 0) return {};
    return QStringLiteral("https://%1:%2").arg(QString::fromStdString(host)).arg(port);
}

QString MakeConsoleErrorMessage(ConsoleErrorOperation operation,
                                px_console::ConsoleApiError error,
                                const std::string& server_message,
                                const QString& endpoint) {
    const auto guidance = ErrorGuidance(error);
    QString message = tcTr("id_error_stage_label") + ": " + OperationText(operation)
        + "\n" + tcTr("id_error_reason_label") + ": " + guidance.reason
        + "\n" + tcTr("id_error_action_label") + ": " + guidance.action;
    if (!endpoint.isEmpty()) {
        message += "\n" + tcTr("id_error_endpoint_label") + ": " + endpoint;
    }
    const auto detail = SafeDetail(server_message);
    if (!detail.isEmpty()) {
        message += "\n" + tcTr("id_error_details_label") + ": " + detail;
    }
    message += "\n" + tcTr("id_error_code_label") + ": CONSOLE-"
        + QString::number(static_cast<int>(error));
    return message;
}

}
