//
// Created by RGAA on 2024/3/5.
//

#include "ws_ipc_router.h"
#include <atomic>
#include "tc_common_new/data.h"
#include "tc_common_new/log.h"
#include "tc_capture_new/capture_message.h"
#include "rd_app.h"

namespace tc
{

    void WsIpcRouter::OnOpen(std::shared_ptr<asio2::http_session> &sess_ptr) {
        WsRouter::OnOpen(sess_ptr);
    }

    void WsIpcRouter::OnClose(std::shared_ptr<asio2::http_session> &sess_ptr) {
        WsRouter::OnClose(sess_ptr);
    }

    void
    WsIpcRouter::OnMessage(std::shared_ptr<asio2::http_session> &sess_ptr, int64_t socket_fd, std::string_view data) {
        WsRouter::OnMessage(sess_ptr, socket_fd, data);
        if (data.size() < sizeof(CaptureBaseMessage)) {
            LOGE("IPC message too small: {}", data.size());
            return;
        }
        auto base_msg = (CaptureBaseMessage *) data.data();
        std::shared_ptr<RdApplication> app;
        try {
            app = Get<std::shared_ptr<RdApplication>>("app");
        } catch (...) {
            app = rdApp;
        }
        if (!app) {
            LOGE("IPC frame dropped: RdApplication not available type=0x{:x} size={}",
                 base_msg->type_, data.size());
            return;
        }
        if (base_msg->type_ == kCaptureVideoFrame) {
            auto msg = std::make_shared<CaptureVideoFrame>();
            if (data.size() != sizeof(CaptureVideoFrame)) {
                LOGE("Error size of ipc video frame, data size: {}, CaptureVideoFrame size: {}", data.size(),
                     sizeof(CaptureVideoFrame));
                return;
            }
            memcpy(msg.get(), data.data(), data.size());
            app->OnIpcVideoFrame(msg);
        } else if (base_msg->type_ == kCaptureAudioFrame) {
            if (data.size() < sizeof(IpcCaptureAudioFrame)) {
                LOGE("IPC audio frame too small: {}", data.size());
                return;
            }
            const auto* hdr = reinterpret_cast<const IpcCaptureAudioFrame*>(data.data());
            const size_t expect = sizeof(IpcCaptureAudioFrame) + hdr->data_length;
            if (data.size() != expect || hdr->data_length == 0) {
                LOGE("IPC audio size mismatch: got={}, expect={}, pcm={}", data.size(), expect,
                     hdr->data_length);
                return;
            }
            CaptureAudioFrame frame;
            frame.type_ = kCaptureAudioFrame;
            frame.data_length = hdr->data_length;
            frame.frame_index_ = hdr->frame_index_;
            frame.samples_ = hdr->samples_;
            frame.channels_ = hdr->channels_;
            frame.bits_ = hdr->bits_;
            frame.full_data_ = Data::Make(data.data() + sizeof(IpcCaptureAudioFrame),
                                          static_cast<int>(hdr->data_length));
            if (!frame.full_data_) {
                LOGE("IPC audio: Data::Make failed pcm={}", hdr->data_length);
                return;
            }
            static std::atomic<uint64_t> s_audio_rx{0};
            const auto n = ++s_audio_rx;
            if (n == 1 || (n % 200) == 0) {
                LOGI("IPC audio rx: n={} idx={} {}Hz {}ch {}bit pcm={}", n, hdr->frame_index_,
                     hdr->samples_, hdr->channels_, hdr->bits_, hdr->data_length);
            }
            app->OnIpcAudioFrame(frame);
        } else {
            LOGW("IPC unknown type=0x{:x} size={}", base_msg->type_, data.size());
        }
    }

    void WsIpcRouter::OnPing(std::shared_ptr<asio2::http_session> &sess_ptr) {
        WsRouter::OnPing(sess_ptr);
    }

    void WsIpcRouter::OnPong(std::shared_ptr<asio2::http_session> &sess_ptr) {
        WsRouter::OnPong(sess_ptr);
    }

    void WsIpcRouter::PostBinaryMessage(std::shared_ptr<Data> data) {
        session_->async_send(data->AsString());
    }

    void WsIpcRouter::PostBinaryMessage(const std::string &data) {
        if (session_ && session_->is_started()) {
            queuing_message_count_++;
            auto weak_self = std::enable_shared_from_this<WsIpcRouter>::weak_from_this();
            session_->async_send(data, [weak_self](size_t byte_sent) {
                if (auto router = weak_self.lock()) {
                    router->queuing_message_count_--;
                }
            });
        }
    }
}
