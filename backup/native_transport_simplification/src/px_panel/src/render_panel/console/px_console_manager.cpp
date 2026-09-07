//
// Created by RGAA on 12/12/2025.
//

#include "px_console_manager.h"

#include "px_dialog.h"
#include "render_panel/px_context.h"
#include "render_panel/px_settings.h"
#include "render_panel/px_application.h"
#include "translator/px_translator.h"
#include "console_error_presenter.h"

namespace px
{

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
                const auto endpoint = MakeConsoleEndpoint(host, port);
                context_->PostUITask([err, server_message, endpoint]() {
                    TcDialog dialog(tcTr("id_error"), MakeConsoleErrorMessage(
                        ConsoleErrorOperation::kCheckConsole, err, server_message, endpoint));
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
                const auto endpoint = MakeConsoleEndpoint(host, port);
                context_->PostUITask([err, server_message, endpoint]() {
                    TcDialog dialog(tcTr("id_error"), MakeConsoleErrorMessage(
                        ConsoleErrorOperation::kCheckConsole, err, server_message, endpoint));
                    dialog.exec();
                });
            }
            return std::nullopt;
        }
        return r.value();
    }

}
