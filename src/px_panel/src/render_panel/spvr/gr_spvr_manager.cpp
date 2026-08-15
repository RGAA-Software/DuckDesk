//
// Created by RGAA on 12/12/2025.
//

#include "gr_spvr_manager.h"

#include "tc_dialog.h"
#include "render_panel/gr_context.h"
#include "render_panel/gr_settings.h"
#include "render_panel/gr_application.h"
#include "translator/tc_translator.h"

namespace tc
{

    // Build a user-readable error message for a failed spvr api call.
    // Well-known authorization/quota errors get localized texts; otherwise prefer
    // the message returned by the CMS in the response body, then the generic text.
    static QString MakeSpvrErrorMessage(const spvr::SpvrApiError& err, const std::string& server_message) {
        QString detail;
        if (err == spvr::SpvrApiError::kInvalidAppkey || err == spvr::SpvrApiError::kInvalidAuthorization) {
            detail = tcTr("id_auth_invalid");
        }
        else if (err == spvr::SpvrApiError::kMaxStreamsReached) {
            detail = tcTr("id_no_available_connection");
        }
        else if (!server_message.empty()) {
            detail = QString::fromStdString(server_message);
        }
        else {
            detail = spvr::SpvrApiErrorAsString(err).c_str();
        }
        return tcTr("id_op_error") + ":" + QString::number(static_cast<int>(err)) + " " + detail;
    }

    GrSpvrManager::GrSpvrManager(const std::shared_ptr<GrContext>& context) {
        context_ = context;
        settings_ = GrSettings::Instance();
    }

    std::optional<spvr::AliveConnections> GrSpvrManager::QueryAliveConnections(bool show_err_dialog) const {
        const auto host = settings_->GetSpvrServerHost();
        const auto port = settings_->GetSpvrServerPort();
        const auto appkey = grApp->GetAppkey();
        const auto r = spvr::SpvrApi::QueryAliveConnections(host, port, appkey);
        if (!r.has_value()) {
            if (show_err_dialog) {
                auto err = r.error();
                auto server_message = spvr::SpvrApiLastErrorMessage();
                context_->PostUITask([=]() {
                    TcDialog dialog(tcTr("id_error"), MakeSpvrErrorMessage(err, server_message));
                    dialog.exec();
                });
            }
            return std::nullopt;
        }
        return r.value();
    }

    std::optional<spvr::AvailableNewConnection> GrSpvrManager::QueryNewConnection(bool show_err_dialog) const {
        const auto host = settings_->GetSpvrServerHost();
        const auto port = settings_->GetSpvrServerPort();
        const auto appkey = grApp->GetAppkey();
        if (host.empty() || port <= 0 || appkey.empty()) {
            return std::nullopt;
        }
        const auto r = spvr::SpvrApi::QueryAvailableNewConnection(host, port, appkey);
        if (!r.has_value()) {
            if (show_err_dialog) {
                auto err = r.error();
                auto server_message = spvr::SpvrApiLastErrorMessage();
                context_->PostUITask([=]() {
                    TcDialog dialog(tcTr("id_error"), MakeSpvrErrorMessage(err, server_message));
                    dialog.exec();
                });
            }
            return std::nullopt;
        }
        return r.value();
    }

}
