//
// Created by RGAA on 23/10/2025.
//

#include "console_scanner.h"
#include "console_datagram_receiver.h"
#include "px_common/log.h"
#include "px_common/message_notifier.h"
#include "px_common/time_util.h"
#include "render_panel/px_context.h"
#include "render_panel/px_application.h"
#include "render_panel/px_app_messages.h"
#include "render_panel/companion/panel_companion.h"

namespace px
{

    ConsoleScanner::ConsoleScanner(const std::shared_ptr<PxApplication>& app) {
        app_ = app;
    }

    ConsoleScanner::~ConsoleScanner() {
        Exit();
    }

    void ConsoleScanner::StartUdpReceiver(int port) {
        if (exit_udp_receiver_ || datagram_receiver_ || port < 0 || port > 65535) {
            return;
        }
        const auto app = app_.lock();
        if (!app) {
            return;
        }
        msg_listener_ = app->GetContext()->ObtainMessageListener(MessageExecutionLane::kState);
        const auto weak_self = weak_from_this();
        msg_listener_->Listen<MsgGrTimer2S>([weak_self](const MsgGrTimer2S&) {
            if (const auto self = weak_self.lock(); self && !self->exit_udp_receiver_) {
                self->ClearInactiveServer();
            }
        });
        const auto notifier = app->GetContext()->GetMessageNotifier();
        datagram_receiver_ = ConsoleDatagramReceiver::Create(
            notifier ? notifier->GetAsyncRuntime() : nullptr);
        if (!datagram_receiver_ || !datagram_receiver_->Start(
                static_cast<std::uint16_t>(port),
                [weak_self](std::string message) {
                    if (const auto self = weak_self.lock(); self && !self->exit_udp_receiver_) {
                        self->HandleDatagram(std::move(message));
                    }
                })) {
            LOGE("Cannot start Console discovery UDP receiver on port: {}", port);
            datagram_receiver_.reset();
        }
    }

    void ConsoleScanner::Exit() {
        if (exit_udp_receiver_.exchange(true)) {
            return;
        }
        if (msg_listener_) {
            msg_listener_->UnListenAll();
            msg_listener_.reset();
        }
        if (datagram_receiver_) {
            datagram_receiver_->Stop();
            datagram_receiver_.reset();
        }
        app_.reset();
    }

    void ConsoleScanner::HandleDatagram(std::string message) {
        if (!message.starts_with("console://access") && !message.starts_with("cms://access")) {
            return;
        }
        const auto app = app_.lock();
        const auto companion = app ? app->GetCompanion() : nullptr;
        if (!companion) {
            return;
        }
        const auto access = companion->ParseConsoleAccessInfo(message);
        if (!access) {
            LOGE("Parse Console access failed");
            return;
        }
        const auto info = std::make_shared<StNetworkConsoleAccessInfo>(
            StNetworkConsoleAccessInfo {
                .console_ip_ = access->console_config_.srv_w3c_ip_,
                .console_port_ = access->console_config_.srv_console_port_,
                .relay_ip_ = access->console_config_.srv_w3c_ip_,
                .relay_port_ = access->console_config_.srv_relay_port_,
                .origin_info_ = std::move(message),
                .update_timestamp_ = static_cast<int64_t>(TimeUtil::GetCurrentTimestamp()),
                .console_ssl_enable_ = access->console_config_.srv_ssl_enable_,
            });
        {
            std::lock_guard lock(ac_mtx_);
            access_info_.insert_or_assign(info->console_ip_, info);
        }
        app->GetContext()->SendAppMessage(MsgConsoleAccessInfo {
            .access_info_ = GetConsoleAccessInfo(),
        });
    }

    std::map<std::string, std::shared_ptr<StNetworkConsoleAccessInfo>> ConsoleScanner::GetConsoleAccessInfo() {
        std::lock_guard<std::mutex> guard(ac_mtx_);
        return access_info_;
    }

    void ConsoleScanner::ClearInactiveServer() {
        std::lock_guard<std::mutex> guard(ac_mtx_);
        auto it = access_info_.begin();
        while (it != access_info_.end()) {
            const auto& [ip, info] = *it;
            auto current_ts = TimeUtil::GetCurrentTimestamp();
            auto diff = current_ts - info->update_timestamp_;
            if (diff > 10 * 1000) {
                // remove
                it = access_info_.erase(it);
            }
            else {
                ++it;
            }
        }
    }

}
