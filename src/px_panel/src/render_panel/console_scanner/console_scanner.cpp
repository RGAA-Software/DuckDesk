//
// Created by RGAA on 23/10/2025.
//

#include "console_scanner.h"
#include "px_common_new/log.h"
#include "px_common_new/thread.h"
#include "px_common_new/message_notifier.h"
#include "px_common_new/time_util.h"
#include "render_panel/px_context.h"
#include "render_panel/px_application.h"
#include "render_panel/px_app_messages.h"
#include "render_panel/companion/panel_companion.h"
#include "asio2/3rd/asio.hpp"
#include <array>
using asio::ip::udp;

namespace px
{

    ConsoleScanner::ConsoleScanner(const std::shared_ptr<PxApplication>& app) {
        app_ = app;
    }

    ConsoleScanner::~ConsoleScanner() {
        Exit();
    }

    void ConsoleScanner::StartUdpReceiver(int port) {
        if (exit_udp_receiver_ || udp_receiver_thread_) {
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
        udp_receiver_thread_ = Thread::MakeOnceTask([weak_self, port]() {
            for (;;) {
                {
                    const auto self = weak_self.lock();
                    if (!self || self->exit_udp_receiver_) {
                        return;
                    }
                }
                try {
                    asio::io_context io;
                    udp::socket socket(io, udp::endpoint(udp::v4(), port));
                    socket.non_blocking(true);
                    LOGI("Listening on UDP port :{}", port);
                    std::array<char, 4096> data{};
                    udp::endpoint sender_endpoint;

                    for (;;) {
                        const auto self = weak_self.lock();
                        if (!self || self->exit_udp_receiver_) {
                            return;
                        }
                        asio::error_code ec;
                        size_t len = socket.receive_from(asio::buffer(data), sender_endpoint, 0, ec);

                        if (!ec && len > 0) {
                            std::string msg(data.data(), len);
                            if (msg.starts_with("console://access") || msg.starts_with("cms://access")) {
                                //LOGI("*Received: {}", msg);
                                const auto app = self->app_.lock();
                                if (auto cp = app ? app->GetCompanion() : nullptr; cp != nullptr) {
                                    auto ac = cp->ParseConsoleAccessInfo(msg);
                                    if (!ac) {
                                        LOGE("Parse console access failed!");
                                        continue;
                                    }
                                    auto info = std::make_shared<StNetworkConsoleAccessInfo>(StNetworkConsoleAccessInfo {
                                        .console_ip_ = ac->console_config_.srv_w3c_ip_,
                                        .console_port_ = ac->console_config_.srv_console_port_,
                                        .relay_ip_ =  ac->console_config_.srv_w3c_ip_,
                                        .relay_port_ = ac->console_config_.srv_relay_port_,
                                        .origin_info_ = msg,
                                        .update_timestamp_ = (int64_t)TimeUtil::GetCurrentTimestamp(),
                                        .console_ssl_enable_ = ac->console_config_.srv_ssl_enable_,
                                    });
                                    //LOGI("*Received console: {}, {}", info->console_ip_, TimeUtil::FormatTimestamp(info->update_timestamp_));
                                    {
                                        std::lock_guard<std::mutex> guard(self->ac_mtx_);
                                        self->access_info_.insert_or_assign(info->console_ip_, info);
                                    }
                                }
                            }
                        }
                        else if (ec == asio::error::would_block || ec == asio::error::try_again) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(100));
                            continue;
                        }
                        else if (ec) {
                            LOGE("*Receive error: {}", ec.message());
                            std::this_thread::sleep_for(std::chrono::seconds(2));
                            break;
                        }

                        {
                            auto msg = MsgConsoleAccessInfo {
                                .access_info_ = self->GetConsoleAccessInfo(),
                            };
                            if (const auto app = self->app_.lock()) {
                                app->GetContext()->SendAppMessage(msg);
                            }
                        }
                    }
                }
                catch (std::exception &e) {
                    LOGE("Exception: {}", e.what());
                    std::this_thread::sleep_for(std::chrono::seconds(2));
                }
            }
        }, "udp_receiver_thread", false);
    }

    void ConsoleScanner::Exit() {
        if (exit_udp_receiver_.exchange(true)) {
            return;
        }
        if (msg_listener_) {
            msg_listener_->UnListenAll();
            msg_listener_.reset();
        }
        if (udp_receiver_thread_) {
            udp_receiver_thread_->Exit();
            udp_receiver_thread_.reset();
        }
        app_.reset();
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
