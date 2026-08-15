//
// User-proxy WebSocket router for Render (localhost-only, single connection).
//

#include "ws_user_proxy_router.h"

#include <asio2/asio2.hpp>
#include <functional>
#include "px_common_new/data.h"
#include "px_common_new/log.h"
#include "tc_render_panel_message.pb.h"
#include "tc_message.pb.h"
#include "px_message_new/proto_converter.h"
#include "px_message_new/rp_proto_converter.h"
#include "ws_plugin.h"
#include "px_render/plugins/plugin_ids.h"
#include "px_render/plugin_interface/gr_net_plugin.h"

namespace tc
{

    std::shared_ptr<WsUserProxyRouter> WsUserProxyRouter::Make(const WsDataPtr& data) {
        return std::make_shared<WsUserProxyRouter>(data);
    }

    WsUserProxyRouter::WsUserProxyRouter(const WsDataPtr& data) : WsRouter(data) {}

    bool WsUserProxyRouter::IsLocalPeer(const std::shared_ptr<asio2::http_session>& sess_ptr) const {
        if (!sess_ptr) {
            return false;
        }
        try {
            auto endpoint = sess_ptr->socket().remote_endpoint();
            const auto addr = endpoint.address();
            return addr.is_loopback();
        } catch (const std::exception& e) {
            LOGE("user-proxy remote_endpoint failed: {}", e.what());
            return false;
        }
    }

    void WsUserProxyRouter::ReplaceSession(std::shared_ptr<asio2::http_session>& sess_ptr) {
        if (active_session_ && active_session_ != sess_ptr && active_session_->is_started()) {
            LOGW("user-proxy replacing existing session");
            active_session_->stop();
        }
        active_session_ = sess_ptr;
        session_ = sess_ptr;
    }

    void WsUserProxyRouter::OnOpen(std::shared_ptr<asio2::http_session>& sess_ptr) {
        if (!IsLocalPeer(sess_ptr)) {
            LOGE("user-proxy rejected non-local connection");
            sess_ptr->stop();
            return;
        }
        ReplaceSession(sess_ptr);
        sess_ptr->set_no_delay(true);
        LOGI("user-proxy connected, fd={}", (uint64_t)sess_ptr->socket().native_handle());
    }

    void WsUserProxyRouter::OnClose(std::shared_ptr<asio2::http_session>& sess_ptr) {
        if (active_session_ == sess_ptr) {
            active_session_.reset();
            session_.reset();
            LOGI("user-proxy disconnected");
        }
    }

    void WsUserProxyRouter::OnMessage(std::shared_ptr<asio2::http_session>& sess_ptr, int64_t socket_fd, std::string_view data) {
        asio2::ignore_unused(sess_ptr, socket_fd);
        HandleRpMessage(std::string(data));
    }

    void WsUserProxyRouter::HandleRpMessage(const std::string& data) {
        tcrp::RpMessage m;
        if (!m.ParseFromArray(data.data(), (int)data.size())) {
            LOGE("user-proxy parse RpMessage failed, len={}", data.size());
            return;
        }

        auto plugin = Get<WsPlugin*>("plugin");
        if (!plugin) {
            LOGE("user-proxy plugin missing");
            return;
        }

        if (m.type() == tcrp::kRpHello) {
            tcrp::RpMessage resp;
            resp.set_type(tcrp::kRpHelloResp);
            PostBinaryMessage(RpProtoAsData(&resp));
            return;
        }

        // 投递到所有网络插件:ws 走自身;net 插件的 net_plugins_ 为空
        // (plugin_manager 只给非 net 插件挂载),需经 total_plugins_ 按 id 找到后
        // 逐个投递。必须覆盖 rtc/rtc_local(WebRTC 网页客户端)和 relay/udp(原生
        // 客户端主路径)——此前白名单只有 rtc,relay 客户端永远收不到远端消息。
        auto for_each_net_plugin = [&](const std::function<void(GrNetPlugin*)>& fn) {
            fn(plugin);
            for (const auto& id : { kNetRtcPluginId, kNetRtcLocalPluginId,
                                    kRelayPluginId, kNetUdpPluginId }) {
                if (auto p = plugin->GetPluginById(id); p && p != plugin) {
                    if (auto np = dynamic_cast<GrNetPlugin*>(p)) {
                        fn(np);
                    }
                }
            }
        };

        if (m.type() == tcrp::kRpClipboardEvent) {
            const auto& clipboard_info = m.clipboard_info();
            auto broadcast = [&](const std::shared_ptr<Data>& buffer) {
                for_each_net_plugin([&](GrNetPlugin* np) {
                    np->PostProtoMessage(buffer, false);
                });
            };
            if (clipboard_info.type() == tcrp::kRpClipboardText) {
                LOGI("user-proxy clipboard text outbound, len={}", clipboard_info.msg().size());
                tc::Message out;
                out.set_type(tc::kClipboardInfo);
                auto sub = out.mutable_clipboard_info();
                sub->set_type(ClipboardType::kClipboardText);
                sub->set_msg(clipboard_info.msg());
                broadcast(ProtoAsData(&out));
            }
            else if (clipboard_info.type() == tcrp::kRpClipboardFiles && clipboard_info.files_size() > 0) {
                LOGI("user-proxy clipboard files outbound, count={}", clipboard_info.files_size());
                tc::Message out;
                out.set_type(tc::kClipboardInfo);
                auto sub = out.mutable_clipboard_info();
                sub->set_type(ClipboardType::kClipboardFiles);
                for (const auto& file : clipboard_info.files()) {
                    auto pf = sub->mutable_files()->Add();
                    pf->set_file_name(file.file_name());
                    pf->set_full_path(file.full_path());
                    pf->set_ref_path(file.ref_path());
                    pf->set_total_size(file.total_size());
                }
                broadcast(ProtoAsData(&out));
            }
            return;
        }

        if (m.type() == tcrp::kRpRawRenderMessage) {
            const auto& sub = m.raw_render_msg();
            auto buffer = Data::From(sub.msg());
            // 与 broadcast 同理:按 stream 投递也要覆盖 relay/udp,否则原生客户端
            // (relay) 收不到 userproxy 的定向消息(剪切板文件取数应答等)
            tc::Message inner;
            bool inner_parsed = inner.ParseFromArray(sub.msg().data(), (int)sub.msg().size());
            LOGI("[LAT-clip] user-proxy outbound, data_channel={}, stream_id={}, inner_type={}, len={}",
                 sub.data_channel(), sub.stream_id(), inner_parsed ? (int)inner.type() : -1, sub.msg().size());
            for_each_net_plugin([&](GrNetPlugin* np) {
                if (sub.data_channel()) {
                    np->PostTargetFileTransferProtoMessage(sub.stream_id(), buffer, sub.run_through());
                } else {
                    np->PostProtoMessage(buffer, sub.run_through());
                }
            });
            return;
        }

        if (m.type() == tcrp::kRpHeartBeat) {
            tcrp::RpMessage resp;
            resp.set_type(tcrp::kRpHeartBeatResp);
            PostBinaryMessage(RpProtoAsData(&resp));
            return;
        }

        LOGI("user-proxy ignored message type: {}", (int)m.type());
    }

    void WsUserProxyRouter::PostBinaryMessage(std::shared_ptr<Data> data) {
        if (!data || !active_session_ || !active_session_->is_started()) {
            return;
        }
        active_session_->ws_stream().binary(true);
        queuing_message_count_++;
        auto weak_self = weak_from_this();
        active_session_->async_send(data->CStr(), data->Size(), [weak_self](size_t byte_sent) {
            auto self = weak_self.lock();
            if (!self) {
                return;
            }
            self->queuing_message_count_--;
            if (auto plugin = self->Get<WsPlugin*>("plugin")) {
                plugin->ReportSentDataSize((int)byte_sent);
            }
        });
    }

    bool WsUserProxyRouter::IsConnected() const {
        return active_session_ && active_session_->is_started();
    }

}
