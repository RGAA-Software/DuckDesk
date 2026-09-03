//
// Created by RGAA on 28/03/2025.
//

#include "running_stream_manager.h"
#include "connection_policy.h"
#include <QApplication>
#include <QDateTime>
#include <QProcessEnvironment>
#include <QUuid>
#include <filesystem>
#include <algorithm>

#include "px_common_new/base64.h"
#include "px_common_new/folder_util.h"
#include "render_panel/px_settings.h"
#include "render_panel/px_context.h"
#include "px_common_new/log.h"
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
        msg_listener_->Listen<MsgClientConnectedPanel>([weak_self](const MsgClientConnectedPanel& msg) {
            const auto self = weak_self.lock();
            if (!self) {
                return;
            }
            {
                std::scoped_lock lock(self->running_mutex_);
                const auto state = self->running_connection_states_.find(msg.stream_id_);
                if (state != self->running_connection_states_.end()) {
                    state->second.MarkPanelChannelConnected();
                }
            }
        });

        msg_listener_->Listen<MsgClientTransportConnectedPanel>(
            [weak_self](const MsgClientTransportConnectedPanel& msg) {
            const auto self = weak_self.lock();
            if (!self) {
                return;
            }
            {
                std::scoped_lock lock(self->running_mutex_);
                const auto state = self->running_connection_states_.find(msg.stream_id_);
                if (state == self->running_connection_states_.end()) {
                    return;
                }
                state->second.MarkTransportConnected();
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
                {
                    std::scoped_lock lock(self->running_mutex_);
                    if (const auto state = self->running_connection_states_.find(msg.stream_id_);
                        state != self->running_connection_states_.end()) {
                        state->second.MarkTerminalRejected();
                    }
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

        msg_listener_->Listen<MsgRtcIceConfigUpdated>([weak_self](const MsgRtcIceConfigUpdated& msg) {
            if (const auto self = weak_self.lock()) {
                self->context_->PostTask([weak_self, revision = msg.revision_]() {
                    if (const auto self = weak_self.lock()) {
                        self->RestartActiveRtcSessions(revision);
                    }
                });
            }
        });
        msg_listener_->Listen<MsgClientRtcIceRestartRequested>(
            [weak_self](const MsgClientRtcIceRestartRequested& msg) {
                if (const auto self = weak_self.lock()) {
                    self->context_->PostTask([weak_self, stream_id = msg.stream_id_]() {
                        if (const auto self = weak_self.lock()) {
                            self->RestartRtcSession(stream_id, 0);
                        }
                    });
                }
            });
    }

    RunningStreamManager::~RunningStreamManager() {
        if (msg_listener_) {
            msg_listener_->UnListenAll();
        }
    }

    bool RunningStreamManager::RefreshConsoleTicket(
        const std::shared_ptr<px_console::ConsoleStream>& item) {
        if (!item || !connection_policy::IsConsoleTicket(item->connect_type_)) {
            return false;
        }
        const auto user_manager = grApp->GetUserManager();
        if (!user_manager) {
            return false;
        }
        const auto nonce = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
        std::vector<std::string> permissions {"view"};
        if (user_manager->IsLoggedIn()) {
            permissions.push_back("file");
        }
        if (!item->only_viewing_) {
            permissions.push_back("input");
            if (user_manager->IsLoggedIn()) {
                permissions.insert(permissions.end(), {"clipboard", "audio"});
            }
        }
        auto result = item->connect_type_ == connection_policy::kConsoleAppTicket
            ? user_manager->IssueInstanceTicket(item->console_instance_id_, nonce, permissions)
            : user_manager->IssueDeviceTicket(item->remote_device_id_, nonce, permissions);
        if (!result.has_value()) {
            LOGE("Refresh Console ticket failed for active RTC stream: {}", item->stream_id_);
            return false;
        }
        const auto& ticket = result.value();
        if (ticket.stream_id.empty()) {
            LOGE("Refresh Console ticket returned no runtime stream ID: {}", item->stream_id_);
            return false;
        }
        item->connection_ticket_ = ticket.ticket;
        item->connection_renewal_token_ = ticket.renewal_token;
        item->connection_nonce_ = nonce;
        item->active_session_stream_id_ = ticket.stream_id;
        item->rtc_ice_config_json_ = ticket.rtc_ice_config_json;
        item->console_signal_device_id_ = ticket.signal_device_id;
        if (!ticket.relay_host.empty()) {
            item->relay_host_ = ticket.relay_host;
        }
        if (ticket.relay_port > 0) {
            item->relay_port_ = ticket.relay_port;
        }
        return true;
    }

    bool RunningStreamManager::RenewConsoleTicket(
        const std::shared_ptr<px_console::ConsoleStream>& item,
        const std::string& active_stream_id) {
        if (!item || active_stream_id.empty() || item->connection_renewal_token_.empty()
            || item->connection_nonce_.empty()) {
            return false;
        }
        const auto user_manager = grApp->GetUserManager();
        if (!user_manager) {
            return false;
        }
        const auto result = user_manager->RenewConnectionTicket(
            item->connection_renewal_token_, item->connection_nonce_);
        if (!result.has_value()) {
            LOGE("Renew Console ticket failed for active RTC stream: {}", active_stream_id);
            return false;
        }
        const auto& ticket = result.value();
        if (ticket.stream_id != active_stream_id) {
            LOGE("Renew Console ticket changed runtime stream ID; refusing RTC restart");
            return false;
        }
        item->connection_ticket_ = ticket.ticket;
        item->connection_renewal_token_ = ticket.renewal_token;
        item->rtc_ice_config_json_ = ticket.rtc_ice_config_json;
        return true;
    }

    void RunningStreamManager::RestartActiveRtcSessions(uint64_t revision) {
        std::vector<std::string> streams;
        {
            std::scoped_lock lock(running_mutex_);
            for (const auto& [stream_id, network_type] : running_network_types_) {
                if (network_type == kStreamItemNtTypeWebRTC && running_items_.contains(stream_id)) {
                    streams.push_back(stream_id);
                }
            }
        }
        for (const auto& stream_id : streams) {
            RestartRtcSession(stream_id, revision);
        }
    }

    void RunningStreamManager::RestartRtcSession(const std::string& stream_id,
                                                  uint64_t revision) {
        std::shared_ptr<px_console::ConsoleStream> item;
        {
            std::scoped_lock lock(running_mutex_);
            if (!running_items_.contains(stream_id)
                || !running_network_types_.contains(stream_id)
                || running_network_types_.at(stream_id) != kStreamItemNtTypeWebRTC) {
                return;
            }
            item = running_items_.at(stream_id);
        }
        const auto panel_server = context_->GetApplication()->GetWsPanelServer();
        if (!RenewConsoleTicket(item, stream_id) || !panel_server) {
            return;
        }
        pxcp::CpMessage command;
        command.set_type(pxcp::CpMessageType::kCpRtcIceRestart);
        command.set_stream_id(stream_id);
        auto& restart = *command.mutable_rtc_ice_restart();
        restart.set_connection_ticket(item->connection_ticket_);
        restart.set_client_nonce(item->connection_nonce_);
        restart.set_instance_id(item->console_instance_id_);
        restart.set_ice_config_json(item->rtc_ice_config_json_);
        restart.set_revision(revision);
        if (!panel_server->PostPanelMessageToStream(
                stream_id, command.SerializeAsString())) {
            LOGW("Active RTC ICE restart could not reach stream: {}", stream_id);
        }
    }

    void RunningStreamManager::FallbackDirectRtc(const std::string& stream_id,
                                                  std::string_view reason) {
        std::shared_ptr<px_console::ConsoleStream> item;
        std::shared_ptr<QProcess> process;
        {
            std::scoped_lock lock(running_mutex_);
            if (!running_network_types_.contains(stream_id)
                || running_network_types_.at(stream_id) != kStreamItemNtTypeWebRTCDirect
                || !running_connection_states_.contains(stream_id)
                || !running_connection_states_.at(stream_id).ShouldFallback()) {
                return;
            }
            running_network_types_[stream_id] = "rtc_fallback_pending";
            item = running_items_[stream_id];
            if (running_processes_.contains(stream_id)) {
                process = running_processes_[stream_id];
            }
        }
        LOGW("Direct RTC failed after probe ({}); reopening standard RTC: {}",
             reason, stream_id);
        if (!RefreshConsoleTicket(item)) {
            LOGE("Direct RTC fallback could not obtain a fresh Console ticket: {}", stream_id);
            return;
        }
        if (process && process->state() != QProcess::NotRunning) {
            process->kill();
            process->waitForFinished(2000);
        }
        {
            std::scoped_lock lock(running_mutex_);
            running_processes_.erase(stream_id);
            running_items_.erase(stream_id);
            running_network_types_.erase(stream_id);
            running_connection_states_.erase(stream_id);
            loading_dialogs_.erase(stream_id);
        }
        StartStream(item, kStreamItemNtTypeWebRTC, false);
    }

    void RunningStreamManager::StartStream(const std::shared_ptr<px_console::ConsoleStream>& item, const std::string& network_type, bool direct) {
        // loading dialog
        auto loading = std::make_shared<StartStreamLoading>(context_, item, network_type);
        loading->setWindowFlag(Qt::WindowStaysOnTopHint, true);
        loading->show();
        const auto saved_stream_id = item->stream_id_;
        const auto stream_id = item->active_session_stream_id_.empty()
            ? saved_stream_id : item->active_session_stream_id_;
        {
            std::scoped_lock lock(running_mutex_);
            running_items_[stream_id] = item;
            running_session_stream_ids_[saved_stream_id] = stream_id;
            running_network_types_[stream_id] = network_type;
            running_connection_states_[stream_id] = DirectRtcFallbackState {};
        }
        loading_dialogs_.insert({stream_id, loading});
        const auto weak_self = weak_from_this();
        const bool allow_standard_fallback = connection_policy::IsConsoleTicket(item->connect_type_);
        QTimer::singleShot(10000, context_.get(),
                          [weak_self, stream_id, network_type, allow_standard_fallback]() {
            const auto self = weak_self.lock();
            if (!self) {
                return;
            }
            const auto loading = self->loading_dialogs_.find(stream_id);
            if (loading == self->loading_dialogs_.end()) {
                return;
            }
            loading->second->hide();
            self->loading_dialogs_.erase(loading);
            if (network_type == kStreamItemNtTypeWebRTCDirect) {
                if (allow_standard_fallback) {
                    self->FallbackDirectRtc(stream_id, "connect timeout");
                }
                else {
                    LOGW("Explicit direct RTC connection timed out; Console fallback is disabled: {}",
                         stream_id);
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
        auto process_environment = QProcessEnvironment::systemEnvironment();
        if (!item->rtc_ice_config_json_.empty()) {
            process_environment.insert("PX_RTC_ICE_CONFIG",
                                       QString::fromStdString(item->rtc_ice_config_json_));
        }
        process->setProcessEnvironment(process_environment);
        // Password validation and stream preparation have already completed
        // in Panel. The child receives only the normal connection parameters.
        const auto child_remote_random_password = !item->ip_direct_prevalidated_
            ? item->remote_device_random_pwd_ : std::string{};
        const auto child_remote_safety_password = !item->ip_direct_prevalidated_
            ? item->remote_device_safety_pwd_ : std::string{};
        const auto display_name = network_type == kStreamItemNtTypeRelay
            ? settings_.GetDeviceId() : std::string("My Computer");
        const auto display_remote_name = network_type == kStreamItemNtTypeRelay
            ? item->remote_device_id_
            : (item->stream_name_.empty() ? item->stream_host_ : item->stream_name_);
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
            << std::format("--network_type={}", network_type).c_str()
            << std::format("--stream_name={}", Base64::Base64Encode(item->stream_name_)).c_str()
            << std::format("--device_id={}", settings_.GetDeviceId()).c_str()
            << std::format("--device_rp={}", Base64::Base64Encode(settings_.GetDeviceRandomPwd())).c_str()
            << std::format("--device_sp={}", Base64::Base64Encode(settings_.GetDeviceSecurityPwd())).c_str()
            << std::format("--remote_device_id={}", item->remote_device_id_).c_str()
            << std::format("--signal_remote_device_id={}", item->console_signal_device_id_).c_str()
            << std::format("--remote_device_rp={}", Base64::Base64Encode(child_remote_random_password)).c_str()
            << std::format("--remote_device_sp={}", Base64::Base64Encode(child_remote_safety_password)).c_str()
            << std::format("--enable_p2p={}", item->enable_p2p_).c_str()
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
            << std::format("--relay_host={}", item->relay_host_).c_str()
            << std::format("--relay_port={}", item->relay_port_).c_str()
            << std::format("--relay_appkey={}", grApp->GetAppkey()).c_str() //item->relay_appkey_
            << std::format("--force_software={}", item->force_software_ ? 1 : 0).c_str()
            << std::format("--wait_debug={}", item->wait_debug_ ? 1 : 0).c_str()
            << std::format("--force_gdi_capture={}", item->force_gdi_capture_ ? 1 : 0).c_str()
            << std::format("--disable_vulkan_render={}", item->disable_vulkan_render_ ? 1 : 0).c_str()
            << std::format("--show_watermark={}", show_watermark ? 1 : 0).c_str()
            << std::format("--gl_backend={}", settings_.gl_backend_).c_str()
            << std::format("--force_direct={}", item->force_direct_ ? 1 : 0).c_str()
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
        if (network_type == kStreamItemNtTypeWebRTCDirect) {
            QObject::connect(process.get(), qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
                             context_.get(), [weak_self, stream_id](int, QProcess::ExitStatus) {
                if (const auto self = weak_self.lock()) {
                    self->FallbackDirectRtc(stream_id, "client exited before connect");
                }
            });
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
                running_network_types_.erase(stream_id);
                running_connection_states_.erase(stream_id);
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

    void RunningStreamManager::StartFileTransfer(const std::shared_ptr<px_console::ConsoleStream>& item, const std::string& network_type) {
        const auto session_id = "ft_" + item->stream_id_ + "_" + std::to_string(QDateTime::currentMSecsSinceEpoch());
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
             << std::format("--network_type={}", network_type).c_str()
             << std::format("--device_id={}", settings_.GetDeviceId()).c_str()
             << std::format("--remote_device_id={}", item->remote_device_id_).c_str()
             << std::format("--signal_remote_device_id={}", item->console_signal_device_id_).c_str()
             << std::format("--stream_name={}", Base64::Base64Encode(item->stream_name_)).c_str()
             << std::format("--connection_ticket={}", Base64::Base64Encode(item->connection_ticket_)).c_str()
             << std::format("--connection_nonce={}", item->connection_nonce_).c_str()
             << std::format("--relay_host={}", item->relay_host_).c_str()
             << std::format("--relay_port={}", item->relay_port_).c_str()
             << std::format("--relay_appkey={}", grApp->GetAppkey()).c_str()
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
