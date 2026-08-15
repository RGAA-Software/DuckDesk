//
// Created by RGAA on 23/10/2025.
//

#include "cms_scanner.h"
#include "px_common_new/log.h"
#include "px_common_new/thread.h"
#include "px_common_new/message_notifier.h"
#include "px_common_new/time_util.h"
#include "render_panel/px_context.h"
#include "render_panel/px_application.h"
#include "render_panel/px_app_messages.h"
#include "render_panel/companion/panel_companion.h"
#include "asio2/3rd/asio.hpp"
using asio::ip::udp;

namespace px
{

    CmsScanner::CmsScanner(const std::shared_ptr<GrApplication>& app) {
        app_ = app;
        msg_listener_ = app_->GetMessageNotifier()->CreateListener();
        msg_listener_->Listen<MsgGrTimer2S>([=, this](const MsgGrTimer2S& msg) {
            this->ClearInactiveServer();
        });
    }

    void CmsScanner::StartUdpReceiver(int port) {
        udp_receiver_thread_ = Thread::MakeOnceTask([=, this]() {
            while (!exit_udp_receiver_) {
                try {
                    asio::io_context io;
                    udp::socket socket(io, udp::endpoint(udp::v4(), port));
                    LOGI("Listening on UDP port :{}", port);
                    char data[4096];
                    udp::endpoint sender_endpoint;

                    while (!exit_udp_receiver_) {
                        asio::error_code ec;
                        size_t len = socket.receive_from(asio::buffer(data), sender_endpoint, 0, ec);

                        if (!ec && len > 0) {
                            std::string msg(data, len);
                            if (msg.starts_with("cms://access")) {
                                //LOGI("*Received: {}", msg);
                                if (auto cp = grApp->GetCompanion(); cp != nullptr) {
                                    auto ac = cp->ParseCmsAccessInfo(msg);
                                    if (!ac) {
                                        LOGE("Parse cms access failed!");
                                        continue;
                                    }
                                    auto info = std::make_shared<StNetworkCmsAccessInfo>(StNetworkCmsAccessInfo {
                                        .cms_ip_ = ac->cms_config_.srv_w3c_ip_,
                                        .cms_port_ = ac->cms_config_.srv_cms_port_,
                                        .relay_ip_ =  ac->cms_config_.srv_w3c_ip_,
                                        .relay_port_ = ac->cms_config_.srv_relay_port_,
                                        .origin_info_ = msg,
                                        .update_timestamp_ = (int64_t)TimeUtil::GetCurrentTimestamp(),
                                    });
                                    //LOGI("*Received cms: {}, {}", info->cms_ip_, TimeUtil::FormatTimestamp(info->update_timestamp_));
                                    {
                                        std::lock_guard<std::mutex> guard(ac_mtx_);
                                        access_info_.erase(info->cms_ip_);
                                        access_info_.insert({info->cms_ip_, info});
                                    }
                                }
                            }
                        }
                        else if (ec) {
                            LOGE("*Receive error: {}", ec.message());
                            std::this_thread::sleep_for(std::chrono::seconds(2));
                            break;
                        }

                        {
                            auto msg = MsgCmsAccessInfo {
                                .access_info_ = this->GetCmsAccessInfo(),
                            };
                            app_->GetContext()->SendAppMessage(msg);
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

    void CmsScanner::Exit() {
        exit_udp_receiver_ = true;
        if (udp_receiver_thread_) {
            udp_receiver_thread_->Join();
        }
    }

    std::map<std::string, std::shared_ptr<StNetworkCmsAccessInfo>> CmsScanner::GetCmsAccessInfo() {
        std::lock_guard<std::mutex> guard(ac_mtx_);
        return access_info_;
    }

    void CmsScanner::ClearInactiveServer() {
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
