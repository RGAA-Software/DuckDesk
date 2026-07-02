//
// User-proxy WebSocket router for Render (localhost-only, single connection).
//

#include "ws_user_proxy_router.h"

#include <asio2/asio2.hpp>
#include "tc_common_new/data.h"
#include "tc_common_new/log.h"
#include "tc_render_panel_message.pb.h"
#include "tc_message.pb.h"
#include "tc_message_new/proto_converter.h"
#include "tc_message_new/rp_proto_converter.h"
#include "ws_plugin.h"

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

        if (m.type() == tcrp::kRpClipboardEvent) {
            const auto& clipboard_info = m.clipboard_info();
            if (clipboard_info.type() == tcrp::kRpClipboardText) {
                LOGI("user-proxy clipboard text outbound, len={}", clipboard_info.msg().size());
                tc::Message out;
                out.set_type(tc::kClipboardInfo);
                auto sub = out.mutable_clipboard_info();
                sub->set_type(ClipboardType::kClipboardText);
                sub->set_msg(clipboard_info.msg());
                plugin->PostProtoMessage(ProtoAsData(&out), false);
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
                plugin->PostProtoMessage(ProtoAsData(&out), false);
            }
            return;
        }

        if (m.type() == tcrp::kRpRawRenderMessage) {
            const auto& sub = m.raw_render_msg();
            auto buffer = Data::From(sub.msg());
            if (sub.data_channel()) {
                plugin->PostTargetFileTransferProtoMessage(sub.stream_id(), buffer, sub.run_through());
            } else {
                plugin->PostProtoMessage(buffer, sub.run_through());
            }
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
