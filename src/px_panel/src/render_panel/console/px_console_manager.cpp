//
// Created by RGAA on 12/12/2025.
//

#include "px_console_manager.h"

#include "px_dialog.h"
#include "render_panel/px_context.h"
#include "render_panel/px_settings.h"
#include "render_panel/px_application.h"
#include "translator/px_translator.h"

namespace px
{

    // Build a user-readable error message for a failed console api call.
    // Well-known authorization/quota errors get localized texts; otherwise prefer
    // the message returned by the Console in the response body, then the generic text.
    static QString MakeConsoleErrorMessage(const px_console::ConsoleApiError& err, const std::string& server_message) {
        QString detail;
        if (err == px_console::ConsoleApiError::kInvalidAppkey || err == px_console::ConsoleApiError::kInvalidAuthorization) {
            detail = tcTr("id_auth_invalid");
        }
        else if (err == px_console::ConsoleApiError::kMaxStreamsReached) {
            detail = tcTr("id_no_available_connection");
        }
        else if (!server_message.empty()) {
            detail = QString::fromStdString(server_message);
        }
        else {
            detail = px_console::ConsoleApiErrorAsString(err).c_str();
        }
        return tcTr("id_op_error") + ":" + QString::number(static_cast<int>(err)) + " " + detail;
    }

    PxConsoleManager::PxConsoleManager(const std::shared_ptr<PxContext>& context) {
        context_ = context;
        settings_ = PxSettings::Instance();
    }

    std::optional<px_console::AliveConnections> PxConsoleManager::QueryAliveConnections(bool show_err_dialog) const {
        const auto host = settings_->GetConsoleServerHost();
        const auto port = settings_->GetConsoleServerPort();
        const auto appkey = grApp->GetAppkey();
        const auto r = px_console::ConsoleApi::QueryAliveConnections(host, port, appkey);
        if (!r.has_value()) {
            if (show_err_dialog) {
                auto err = r.error();
                auto server_message = px_console::ConsoleApiLastErrorMessage();
                context_->PostUITask([=]() {
                    TcDialog dialog(tcTr("id_error"), MakeConsoleErrorMessage(err, server_message));
                    dialog.exec();
                });
            }
            return std::nullopt;
        }
        return r.value();
    }

    std::optional<px_console::AvailableNewConnection> PxConsoleManager::QueryNewConnection(bool show_err_dialog) const {
        const auto host = settings_->GetConsoleServerHost();
        const auto port = settings_->GetConsoleServerPort();
        const auto appkey = grApp->GetAppkey();
        if (host.empty() || port <= 0 || appkey.empty()) {
            return std::nullopt;
        }
        const auto r = px_console::ConsoleApi::QueryAvailableNewConnection(host, port, appkey);
        if (!r.has_value()) {
            if (show_err_dialog) {
                auto err = r.error();
                auto server_message = px_console::ConsoleApiLastErrorMessage();
                context_->PostUITask([=]() {
                    TcDialog dialog(tcTr("id_error"), MakeConsoleErrorMessage(err, server_message));
                    dialog.exec();
                });
            }
            return std::nullopt;
        }
        return r.value();
    }

}
