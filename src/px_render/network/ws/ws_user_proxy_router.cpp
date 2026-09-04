//
// User-proxy WebSocket router for Render (localhost-only, single connection).
//

#include "ws_user_proxy_router.h"

#include <asio2/asio2.hpp>
#include <functional>
#include "px_common_new/data.h"
#include "px_common_new/log.h"
#include "px_common_new/privacy_log.h"
#include "px_render_panel_message.pb.h"
#include "px_message.pb.h"
#include "px_message_new/proto_converter.h"
#include "px_message_new/rp_proto_converter.h"
#include "ws_plugin.h"
#include "px_render/modules/module_ids.h"
#include "px_render/plugin_interface/px_net_plugin.h"

namespace px
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
            static_cast<void>(e);
            LOGE("event=transport.connect component=net_ws "
                 "code=WS_REMOTE_ENDPOINT_FAILED operation=validate_user_proxy_peer "
                 "outcome=rejected recoverable=false reason=endpoint_exception");
            return false;
        }
    }

    void WsUserProxyRouter::ReplaceSession(std::shared_ptr<asio2::http_session>& sess_ptr) {
        if (active_session_ && active_session_ != sess_ptr && active_session_->is_started()) {
            LOGW("event=session.admit component=net_ws "
                 "code=SESSION_REPLACED operation=replace_user_proxy "
                 "outcome=replaced recoverable=true");
            active_session_->stop();
        }
        active_session_ = sess_ptr;
        session_ = sess_ptr;
    }

    void WsUserProxyRouter::OnOpen(std::shared_ptr<asio2::http_session>& sess_ptr) {
        if (!IsLocalPeer(sess_ptr)) {
            LOGW("event=session.admit component=net_ws "
                 "code=SESSION_PEER_NOT_LOCAL operation=validate_user_proxy_peer "
                 "outcome=rejected recoverable=false");
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
        pxrp::RpMessage m;
        if (!m.ParseFromArray(data.data(), (int)data.size())) {
            LOGW("event=transport.receive component=net_ws "
                 "code=WS_MESSAGE_PARSE_FAILED operation=parse_user_proxy "
                 "outcome=dropped recoverable=true bytes={}", data.size());
            return;
        }

        const auto plugin = ws_data_ ? ws_data_->plugin_.lock() : nullptr;
        if (!plugin) {
            LOGE("event=transport.receive component=net_ws "
                 "code=MODULE_DEPENDENCY_UNAVAILABLE operation=route_user_proxy "
                 "outcome=failed recoverable=false");
            return;
        }

        if (m.type() == pxrp::kRpHello) {
            pxrp::RpMessage resp;
            resp.set_type(pxrp::kRpHelloResp);
            PostBinaryMessage(RpProtoAsData(&resp));
            return;
        }

        if (m.type() == pxrp::kRpClipboardEvent) {
            const auto& clipboard_info = m.clipboard_info();
            auto broadcast = [&](const std::shared_ptr<Data>& buffer) {
                plugin->BroadcastNetworkMessage(buffer, false);
            };
            if (clipboard_info.type() == pxrp::kRpClipboardText) {
                LOGI("user-proxy clipboard text outbound, len={}", clipboard_info.msg().size());
                px::Message out;
                out.set_type(px::kClipboardInfo);
                auto sub = out.mutable_clipboard_info();
                sub->set_type(ClipboardType::kClipboardText);
                sub->set_msg(clipboard_info.msg());
                broadcast(ProtoAsData(&out));
            }
            else if (clipboard_info.type() == pxrp::kRpClipboardFiles && clipboard_info.files_size() > 0) {
                LOGI("user-proxy clipboard files outbound, count={}", clipboard_info.files_size());
                px::Message out;
                out.set_type(px::kClipboardInfo);
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

        if (m.type() == pxrp::kRpRawRenderMessage) {
            const auto& sub = m.raw_render_msg();
            auto buffer = Data::From(sub.msg());
            // 与 broadcast 同理:按 stream 投递也要覆盖 relay/udp,否则原生客户端
            // (relay) 收不到 userproxy 的定向消息(剪切板文件取数应答等)
            px::Message inner;
            bool inner_parsed = inner.ParseFromArray(sub.msg().data(), (int)sub.msg().size());
            LOGI("event=transport.send component=net_ws route=user_proxy "
                 "data_channel={} stream={} message_type={} bytes={}",
                 sub.data_channel(), PrivacyLogId(sub.stream_id()),
                 inner_parsed ? (int)inner.type() : -1, sub.msg().size());
            if (sub.data_channel()) {
                plugin->BroadcastFileTransferMessage(
                    sub.stream_id(), buffer, sub.run_through());
            }
            else {
                plugin->BroadcastNetworkMessage(buffer, sub.run_through());
            }
            return;
        }

        if (m.type() == pxrp::kRpHeartBeat) {
            pxrp::RpMessage resp;
            resp.set_type(pxrp::kRpHeartBeatResp);
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
            if (const auto plugin = self->ws_data_
                    ? self->ws_data_->plugin_.lock() : nullptr) {
                plugin->ReportSentDataSize((int)byte_sent);
            }
        });
    }

    bool WsUserProxyRouter::IsConnected() const {
        return active_session_ && active_session_->is_started();
    }

}
