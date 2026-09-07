//
// Created by RGAA on 28/03/2025.
//

#include "running_stream_manager.h"
#include "connection_policy.h"
#include <QApplication>
#include <QDateTime>
#include <QUuid>
#include <filesystem>
#include <algorithm>

#include "px_common/base64.h"
#include "px_common/folder_util.h"
#include "render_panel/px_settings.h"
#include "render_panel/px_context.h"
#include "px_common/log.h"
#include "render_panel/px_app_messages.h"
#include "render_panel/px_application.h"
#include "px_qt_widget/px_dialog.h"
#include "start_stream_loading.h"
#include "stream_launch_child_arguments.h"
#include "px_qt_widget/translator/px_translator.h"
#include "px_base/ct_stream_item_net_type.h"
#include "px_client_panel_message.pb.h"
#include "render_panel/network/ws_panel_server.h"
#include "render_panel/user/px_user_manager.h"

namespace px
{

    RunningStreamManager::RunningStreamManager(const std::shared_ptr<PxContext>& ctx)
        : settings_(*PxSettings::Instance()) {
        context_ = ctx;
        msg_listener_ = context_->ObtainMessageListener(MessageExecutionLane::kControl);
    }

    void RunningStreamManager::InitMessageListeners() {
        const auto weak_self = weak_from_this();
        msg_listener_->Listen<MsgClientTransportConnectedPanel>(
            [weak_self](const MsgClientTransportConnectedPanel& msg) {
            const auto self = weak_self.lock();
            if (!self) {
                return;
            }
            {
                std::scoped_lock lock(self->running_mutex_);
                if (!self->running_items_.contains(msg.stream_id_)) {
                    return;
                }
            }
            // The loading dialog reflects the remote transport, not merely the
            // local Panel websocket.
            self->context_->PostUIDelayTask([weak_self, msg]() {
                const auto self = weak_self.lock();
                if (!self) {
                    return;
                }
                if (self->loading_dialogs_.contains(msg.stream_id_)) {
                    self->loading_dialogs_[msg.stream_id_]->hide();
                    self->loading_dialogs_.erase(msg.stream_id_);
                }
            }, 200);
        });

        msg_listener_->Listen<MsgClientTransportRejectedPanel>(
            [weak_self](const MsgClientTransportRejectedPanel& msg) {
                const auto self = weak_self.lock();
                if (!self) {
                    return;
                }
                self->context_->PostUITask([weak_self, stream_id = msg.stream_id_]() {
                    const auto self = weak_self.lock();
                    if (!self) {
                        return;
                    }
                    if (const auto loading = self->loading_dialogs_.find(stream_id);
                        loading != self->loading_dialogs_.end()) {
                        loading->second->hide();
                        self->loading_dialogs_.erase(loading);
                    }
                });
            });

        msg_listener_->Listen<MsgNoAvailableConnection>([weak_self](const MsgNoAvailableConnection& msg) {
            const auto self = weak_self.lock();
            if (!self) {
                return;
            }
            self->context_->PostUITask([weak_self, msg]() {
                const auto self = weak_self.lock();
                if (!self) {
                    return;
                }
                if (self->loading_dialogs_.contains(msg.stream_id_)) {
                    self->loading_dialogs_[msg.stream_id_]->hide();
                    self->loading_dialogs_.erase(msg.stream_id_);
                }
            });
        });
    }

    RunningStreamManager::~RunningStreamManager() {
        if (msg_listener_) {
            msg_listener_->UnListenAll();
        }
    }

    void RunningStreamManager::StartStream(const std::shared_ptr<px_console::ConsoleStream>& item) {
        if (!item || !HasNativeLaunchBinding(item->active_session_stream_id_, item->connection_ticket_, item->connection_nonce_, false)) {
            context_->NotifyAppErrMessage(tcTr("id_error"), tcTr("id_connection_ticket_required"));
            return;
        }
        // loading dialog
        auto loading = std::make_shared<StartStreamLoading>(context_, item, kStreamItemNtTypeUdpDirect);
        loading->setWindowFlag(Qt::WindowStaysOnTopHint, true);
        loading->show();
        const auto saved_stream_id = item->stream_id_;
        const auto stream_id = item->active_session_stream_id_.empty()
            ? saved_stream_id : item->active_session_stream_id_;
        {
            std::scoped_lock lock(running_mutex_);
            running_items_[stream_id] = item;
            running_session_stream_ids_[saved_stream_id] = stream_id;
        }
        loading_dialogs_.insert({stream_id, loading});
        const auto weak_self = weak_from_this();
        QTimer::singleShot(10000, context_.get(), [weak_self, stream_id]() {
            if (const auto self = weak_self.lock()) {
                if (const auto loading = self->loading_dialogs_.find(stream_id); loading != self->loading_dialogs_.end()) {
                    loading->second->hide();
                    self->loading_dialogs_.erase(loading);
                }
            }
        });

        auto func_hide_loading_dialog = [weak_self, stream_id]() {
            const auto self = weak_self.lock();
            if (!self) {
                return;
            }
            if (self->loading_dialogs_.contains(stream_id)) {
                self->loading_dialogs_[stream_id]->hide();
                self->loading_dialogs_.erase(stream_id);
            }
        };

        const auto launch_policy = connection_policy::Classify(
            item->connect_type_, item->remote_device_id_, item->stream_host_, item->stream_port_);
        if (launch_policy == connection_policy::LaunchPolicy::kReject) {
            func_hide_loading_dialog();
            LOGE("Reject unsupported stream launch policy: type={}, remote_device_id={}, endpoint={}:{}",
                 item->connect_type_, item->remote_device_id_, item->stream_host_, item->stream_port_);
            TcDialog dialog(tcTr("id_connect_failed"), tcTr("id_connection_ticket_required"), nullptr);
            dialog.exec();
            return;
        }
        const bool uses_console_ticket = launch_policy == connection_policy::LaunchPolicy::kConsoleTicket;
        if (uses_console_ticket
            && (item->connection_ticket_.empty() || item->connection_nonce_.empty())) {
            func_hide_loading_dialog();
            LOGE("Reject Console stream without ticket or nonce: {}", item->stream_id_);
            TcDialog dialog(tcTr("id_connect_failed"), tcTr("id_connection_ticket_required"), nullptr);
            dialog.exec();
            return;
        }

        std::string screen_recording_path = settings_.GetScreenRecordingPath();
        if (screen_recording_path.empty()) {
            // 默认: C:\Users\Public\Pixels\px_client_records (与数据根同约定)
            screen_recording_path =
                (std::filesystem::path(FolderUtil::GetProgramDataPath()) / "px_client_records").string();
        }

        bool show_watermark = true;
        if (grApp->GetSkinName() == "OpenSource" || !item->remote_device_id_.empty()) {
            show_watermark = false;
        }

        // start it
        auto process = std::make_shared<QProcess>();
        // Password validation and stream preparation have already completed
        // in Panel. The child receives only the normal connection parameters.
        const auto child_remote_random_password = !item->ip_direct_prevalidated_
            ? item->remote_device_random_pwd_ : std::string{};
        const auto child_remote_safety_password = !item->ip_direct_prevalidated_
            ? item->remote_device_safety_pwd_ : std::string{};
        const std::string display_name{"My Computer"};
        const auto display_remote_name = item->stream_name_.empty() ? item->stream_host_ : item->stream_name_;
        const auto ips = context_->GetIps();
        const auto my_host = ips.empty() ? std::string{} : ips.front().ip_addr_;
        QStringList arguments;
        arguments
            << std::format("--host={}", item->stream_host_).c_str()
            << std::format("--port={}", item->stream_port_).c_str()
            << std::format("--appkey={}", grApp->GetAppkey()).c_str()
            << std::format("--console_host={}", settings_.GetConsoleServerHost()).c_str()
            << std::format("--console_port={}", settings_.GetConsoleServerPort()).c_str()
            << std::format("--console_ssl={}", settings_.IsConsoleSslEnabled()).c_str()
            << std::format("--audio={}", item->audio_enabled_).c_str()
            << std::format("--clipboard={}", item->clipboard_enabled_).c_str()
            << std::format("--stream_id={}", stream_id).c_str()
            << std::format("--conn_type={}", item->connect_type_).c_str()
            << std::format("--stream_name={}", Base64::Base64Encode(item->stream_name_)).c_str()
            << std::format("--device_id={}", settings_.GetDeviceId()).c_str()
            << std::format("--device_rp={}", Base64::Base64Encode(settings_.GetDeviceRandomPwd())).c_str()
            << std::format("--device_sp={}", Base64::Base64Encode(settings_.GetDeviceSecurityPwd())).c_str()
            << std::format("--remote_device_id={}", item->remote_device_id_).c_str()
            << std::format("--remote_device_rp={}", Base64::Base64Encode(child_remote_random_password)).c_str()
            << std::format("--remote_device_sp={}", Base64::Base64Encode(child_remote_safety_password)).c_str()
            << std::format("--auto_layout_screens={}", settings_.IsMaxWindowEnabled() ? 1 : 0).c_str()
            << std::format("--display_name={}", display_name).c_str()
            << std::format("--display_remote_name={}", display_remote_name).c_str()
            << std::format("--panel_server_port={}", settings_.GetPanelServerPort()).c_str()
            << std::format("--screen_recording_path={}", screen_recording_path).c_str()
            << std::format("--my_host={}", my_host).c_str()
            << std::format("--language={}", (int)tcTrMgr()->GetSelectedLanguage()).c_str()
            << std::format("--only_viewing={}", item->only_viewing_).c_str()
            << std::format("--split_windows={}", item->split_windows_).c_str()
            << std::format("--max_num_of_screen={}", settings_.GetMaxNumOfScreen()).c_str()
            << std::format("--display_logo={}", settings_.IsClientLogoDisplaying() ? 1 : 0).c_str()
            << std::format("--develop_mode={}", settings_.IsDevelopMode() ? 1 : 0).c_str()
            << std::format("--titlebar_color={}", settings_.IsColorfulTitleBarEnabled() ? item->bg_color_ : -1).c_str()
            << std::format("--decoder={}", settings_.GetPreferDecoder()).c_str()
            << std::format("--force_software={}", item->force_software_ ? 1 : 0).c_str()
            << std::format("--wait_debug={}", item->wait_debug_ ? 1 : 0).c_str()
            << std::format("--force_gdi_capture={}", item->force_gdi_capture_ ? 1 : 0).c_str()
            << std::format("--disable_vulkan_render={}", item->disable_vulkan_render_ ? 1 : 0).c_str()
            << std::format("--show_watermark={}", show_watermark ? 1 : 0).c_str()
            << std::format("--gl_backend={}", settings_.gl_backend_).c_str()
            ;
        const auto credential_arguments = BuildStreamLaunchCredentialArguments({
            .connection_ticket = item->connection_ticket_,
            .connection_nonce = item->connection_nonce_,
            .connection_instance_id = item->console_instance_id_,
        });
        for (const auto& argument : credential_arguments) {
            arguments << QString::fromStdString(argument);
        }
        LOGI("Start client inner args:");
        for (auto& arg : arguments) {
            const auto value = arg.toStdString();
            if (value.starts_with("--connection_ticket=")
                || value.starts_with("--remote_device_rp=")
                || value.starts_with("--remote_device_sp=")
                || value.starts_with("--device_rp=")
                || value.starts_with("--device_sp=")) {
                LOGI("{}=<redacted>", value.substr(0, value.find('=')));
            }
            else {
                LOGI("{}", value);
            }
        }

        auto client_inner_path = qApp->applicationDirPath() + "/" + kPxClientName.c_str();
        process->start(client_inner_path, arguments);
        {
            std::scoped_lock lock(running_mutex_);
            running_processes_[stream_id] = process;
        }
        LOGI("After start client: {}", client_inner_path.toStdString());
    }

    bool RunningStreamManager::StopStream(const std::shared_ptr<px_console::ConsoleStream>& item) {
        const auto saved_stream_id = item->stream_id_;
        auto stream_id = saved_stream_id;
        {
            std::scoped_lock lock(running_mutex_);
            if (const auto it = running_session_stream_ids_.find(saved_stream_id);
                it != running_session_stream_ids_.end()) {
                stream_id = it->second;
            }
        }
        if (running_processes_.contains(stream_id)) {
            auto process = running_processes_[stream_id];
            if (process) {
                TcDialog dialog(tcTr("id_warning"), tcTr("id_exit_client"), nullptr);
                if (dialog.exec() != kDoneOk) {
                    return false;
                }
                process->kill();
                running_processes_.erase(stream_id);
                std::scoped_lock lock(running_mutex_);
                running_items_.erase(stream_id);
                running_session_stream_ids_.erase(saved_stream_id);
            }
        }
        context_->SendAppMessage(ClearWorkspace {
            .item_ = item,
        });
        return true;
    }

    bool RunningStreamManager::OpenFileTransferInRunningClient(
        const std::shared_ptr<px_console::ConsoleStream>& item) {
        if (!item || !context_) {
            return false;
        }
        const auto app = context_->GetApplication();
        const auto panel_server = app ? app->GetWsPanelServer() : nullptr;
        if (!panel_server) {
            return false;
        }
        pxcp::CpMessage command;
        command.set_type(pxcp::CpMessageType::kCpOpenFileTransfer);
        auto stream_id = item->stream_id_;
        {
            std::scoped_lock lock(running_mutex_);
            if (const auto it = running_session_stream_ids_.find(item->stream_id_);
                it != running_session_stream_ids_.end()) {
                stream_id = it->second;
            }
        }
        command.set_stream_id(stream_id);
        const bool delivered = panel_server->PostPanelMessageToStream(
            stream_id, command.SerializeAsString());
        if (delivered) {
            LOGI("Open file transfer in running client: {}", stream_id);
        }
        return delivered;
    }

    void RunningStreamManager::StartFileTransfer(const std::shared_ptr<px_console::ConsoleStream>& item) {
        if (!item || !HasNativeLaunchBinding(item->active_session_stream_id_, item->connection_ticket_, item->connection_nonce_, true)) {
            context_->NotifyAppErrMessage(tcTr("id_error"), tcTr("id_connection_ticket_required"));
            return;
        }
        const auto session_id = item->active_session_stream_id_;
        auto process = std::make_shared<QProcess>();
        QStringList args;
        args << "--mode=file-transfer"
             << std::format("--host={}", item->stream_host_).c_str()
             << std::format("--port={}", item->stream_port_).c_str()
             << std::format("--appkey={}", grApp->GetAppkey()).c_str()
             << std::format("--console_host={}", settings_.GetConsoleServerHost()).c_str()
             << std::format("--console_port={}", settings_.GetConsoleServerPort()).c_str()
             << std::format("--console_ssl={}", settings_.IsConsoleSslEnabled()).c_str()
             << std::format("--stream_id={}", session_id).c_str()
             << std::format("--device_id={}", settings_.GetDeviceId()).c_str()
             << std::format("--remote_device_id={}", item->remote_device_id_).c_str()
             << std::format("--stream_name={}", Base64::Base64Encode(item->stream_name_)).c_str()
             << std::format("--connection_ticket={}", Base64::Base64Encode(item->connection_ticket_)).c_str()
             << std::format("--connection_nonce={}", item->connection_nonce_).c_str()
             << std::format("--language={}", (int)tcTrMgr()->GetSelectedLanguage()).c_str();
        const auto path = qApp->applicationDirPath() + "/" + kPxClientName.c_str();
        process->start(path, args);
        running_processes_.insert({session_id, process});
        QObject::connect(process.get(), qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
                         context_.get(), [weak_self = weak_from_this(), session_id](int, QProcess::ExitStatus) {
            if (const auto self = weak_self.lock()) {
                self->running_processes_.erase(session_id);
            }
        });
    }
}
