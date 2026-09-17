//
// Created by RGAA on 12/08/2026.
//

#include "udp_direct_connection.h"
#include "udp_datagram_policy.h"
#include "px_common/log.h"
#include "px_common/data.h"
#include "px_common/time_util.h"
#include "px_message.pb.h"
#include <asio2/asio2.hpp>
#include <asio2/udp/udp_client.hpp>

namespace px {

UdpDirectConnection::UdpDirectConnection(const std::shared_ptr<MessageNotifier>& notifier) : Connection(notifier) {}

UdpDirectConnection::~UdpDirectConnection() {
    Stop();
}

void UdpDirectConnection::OnAudioFrame(const media::AudioDelivery& frame) {
    if (stopped_ || !audio_msg_cbk_) {
        return;
    }
    const auto payload = media::UnwrapAudioPayload(frame.payload);
    const bool lost = !payload;
    if (!lost && frame.sequence % 250 == 0)
        LOGI("UDP media v2 audio delivered: sequence={}, opus_bytes={}", frame.sequence, payload->size());
    if (lost) {
        ++media_window_.audio_plc;
        if (++audio_lost_log_count_ == 1 || audio_lost_log_count_ % 50 == 0) {
            LOGW("UDP media v2 audio PLC, sequence={}, burst={}", frame.sequence, audio_lost_log_count_);
        }
    } else {
        audio_lost_log_count_ = 0;
        if (!media_ready_reported_) {
            media_ready_reported_ = true;
            if (media_ready_cbk_)
                media_ready_cbk_();
        }
    }
    if (stopped_)
        return;
    auto audio_message = std::make_shared<px::Message>();
    audio_message->set_type(px::kAudioFrame);
    auto& audio = *audio_message->mutable_audio_frame();
    audio.set_samples(48000);
    audio.set_channels(2);
    audio.set_bits(16);
    audio.set_frame_size(960);
    if (payload)
        audio.set_data(payload->data(), payload->size());
    audio.set_extra(lost ? "udp_lost" : "udp_synth");
    audio_msg_cbk_(audio_message);
}

void UdpDirectConnection::Start(const std::string& host, int udp_port, const std::string& stream_id, const std::string& association_code) {
    const auto weak_self = weak_from_this();
    host_ = host;
    udp_port_ = udp_port;
    stream_id_ = stream_id;
    association_code_ = association_code;

    // 支持同实例重连:先停旧 socket,再清空连接态与组帧/jitter 状态。
    // 否则 render 重启/接管后 frame_index 回退,旧 finished_ 水位会把新流全丢。
    if (udp_client_) {
        udp_client_->stop_all_timers();
        udp_client_->stop();
    }
    stopped_ = false;
    connected_ = false;
    disconn_reported_ = false;
    last_recv_ms_ = 0;
    last_video_frame_ms_ = 0;
    last_idr_request_ms_ = 0;
    last_rfi_request_ms_ = 0;
    last_idr_time_.clear();
    last_rfi_time_.clear();
    audio_lost_log_count_ = 0;
    media_ready_reported_ = false;
    received_media_packet_ = false;
    video_receiver_.Reset();
    audio_receiver_.Reset();
    media_window_ = {};
    last_delivered_us_ = 0;
    receive_timing_.Reset();
    receive_statistics_.clear();
    recv_pkt_count_ = 0;
    recv_video_pkt_count_ = 0;
    malformed_video_pkt_count_ = 0;
    selected_datagram_size_ = kUdpUnknownDatagramSize;

    // This is the Pixels protocol's maximum accepted datagram capacity, not the selected path MTU. The negotiated sender size is
    // 1040, 1200, or 1400 bytes; retaining the larger receive capacity also keeps custom server caps valid.
    constexpr std::size_t maximumUdpDatagramSize{1500};
    udp_client_ = std::make_shared<asio2::udp_client>(maximumUdpDatagramSize, maximumUdpDatagramSize);
    // 注意:裸 UDP,不传 asio2::use_kcp(可靠重传对视频是负优化,见 native_udp_media_v2_wire.md)

    udp_client_
        ->bind_connect([weak_self]() {
            const auto self = weak_self.lock();
            if (!self || !self->udp_client_)
                return;
            if (asio2::get_last_error()) {
                LOGE("udp direct connect failure : {} {}", asio2::last_error_val(), asio2::last_error_msg().c_str());
            } else {
                LOGI("udp direct connect success : {} {}, remote: {}:{}", self->udp_client_->local_address().c_str(), self->udp_client_->local_port(),
                     self->host_, self->udp_port_);
                self->connected_ = true;
                self->last_recv_ms_ = TimeUtil::GetCurrentTimestamp();
                asio::error_code endpoint_error{};
                const auto remote_endpoint = self->udp_client_->socket().remote_endpoint(endpoint_error);
                const auto profile = endpoint_error ? UdpDatagramProfile{} : SelectUdpDatagramProfile(remote_endpoint.address());
                self->selected_datagram_size_ = profile.datagram_size;
                LOGI("UDP datagram profile: path={}, remote={}, interface_mtu={}, complete_payload={}", UdpPathKindName(profile.kind),
                     endpoint_error ? self->host_ : remote_endpoint.address().to_string(), profile.interface_mtu, profile.datagram_size);
                // 高动态画面一帧 ~89 个 UDP 包(~125KB)毫秒内突发,默认接收缓冲(~64KB)必然溢出丢包,
                // 接收缓冲调 8MB、发送 1MB;Windows 上读回值可能与设置值不同,打出来即可
                {
                    asio::error_code ec;
                    auto& sock = self->udp_client_->socket();
                    sock.set_option(asio::socket_base::receive_buffer_size(8 * 1024 * 1024), ec);
                    if (ec)
                        LOGW("udp set rcvbuf 8MB failed: {}", ec.message());
                    sock.set_option(asio::socket_base::send_buffer_size(1 * 1024 * 1024), ec);
                    if (ec)
                        LOGW("udp set sndbuf 1MB failed: {}", ec.message());
                    asio::socket_base::receive_buffer_size rcv;
                    asio::socket_base::send_buffer_size snd;
                    sock.get_option(rcv, ec);
                    sock.get_option(snd, ec);
                    LOGI("udp direct socket buffer: rcv = {}, snd = {}", rcv.value(), snd.value());
                }
                // The endpoint can only be associated after the matching WS
                // control binding created this short-lived media association.
                self->PostBinaryMessage(PxUdpProtocol::BuildHello(self->association_code_, self->stream_id_, self->selected_datagram_size_));
                // 显式补一发 IDR 请求。正常路径 render 收到连接事件会自己插 IDR,
                // 但 render 重启/断线重建时容易出现“hello 已发、关键帧没来”,客户端会
                // 一直停在“已收到配置信息,等待视频帧”。这里不依赖事件链路,再要一次。
                self->last_idr_request_ms_ = TimeUtil::GetCurrentTimestamp();
                self->RequestIdrKeepalive("");
                // 1s 心跳:保持 NAT 映射,让 render 感知会话在线
                self->udp_client_->start_timer(kTimerHeartbeat, 1000, [weak_self]() {
                    if (const auto locked = weak_self.lock()) {
                        if (locked->received_media_packet_) {
                            locked->PostBinaryMessage(PxUdpProtocol::BuildHeartbeat(locked->association_code_));
                        } else {
                            locked->PostBinaryMessage(
                                PxUdpProtocol::BuildHello(locked->association_code_, locked->stream_id_, locked->selected_datagram_size_));
                        }
                    }
                });
                // 无完整视频帧兜底:2s 内没组出帧再请 IDR(节流 1s)。
                // Lost frames request throttled IDR; this timer also handles total silence.
                self->udp_client_->start_timer(kTimerIdrRetry, 1000, [weak_self]() {
                    if (const auto locked = weak_self.lock())
                        locked->CheckNeedIdr();
                });
                // watchdog:长时间收不到任何 UDP 包视为媒体面断开
                self->udp_client_->start_timer(kTimerWatchdog, 1000, [weak_self]() {
                    if (const auto locked = weak_self.lock())
                        locked->CheckWatchdog();
                });
                self->udp_client_->post_queued_event([weak_self]() {
                    if (const auto locked = weak_self.lock(); locked && locked->conn_cbk_) {
                        locked->conn_cbk_();
                    }
                });
            }
        })
        .bind_disconnect([weak_self]() {
            const auto self = weak_self.lock();
            if (!self)
                return;
            if (self->stopped_) {
                self->connected_ = false;
                return;
            }
            LOGI("udp direct disconnect : {} {}", asio2::last_error_val(), asio2::last_error_msg().c_str());
            self->connected_ = false;
            if (!self->disconn_reported_.exchange(true) && self->dis_conn_cbk_) {
                self->dis_conn_cbk_();
            }
        })
        .bind_recv([weak_self](std::string_view packet) {
            if (const auto self = weak_self.lock())
                self->OnUdpPacket(std::span<const char>{packet});
        });

    udp_client_->async_start(host_, udp_port_);
}

void UdpDirectConnection::Stop() {
    if (stopped_.exchange(true)) {
        return;
    }
    // asio2 may be between a failed connect and its automatic reconnect,
    // where is_started() is false but a reconnect timer is still armed.
    // stop() is explicitly safe from callbacks and cancels that timer.
    if (udp_client_) {
        udp_client_->stop_all_timers();
        udp_client_->stop();
    }
}

void UdpDirectConnection::PostBinaryMessage(std::shared_ptr<Data> payload) {
    if (!stopped_ && udp_client_ && udp_client_->is_started()) {
        queuing_message_count_++;
        const auto weak_self = weak_from_this();
        udp_client_->async_send(payload->Bytes().data(), payload->Size(),
                                [weak_self, payload]() {
                                    if (const auto self = weak_self.lock())
                                        self->queuing_message_count_--;
                                });
    }
}

void UdpDirectConnection::SetOnVoiceFrameCallback(std::function<void(UdpVoiceFrame)> callback) {
    voice_frame_cbk_ = std::move(callback);
}

bool UdpDirectConnection::PostVoiceFrame(const std::string& call_id, std::uint32_t sequence, std::uint64_t capture_time_ms,
                                         std::span<const std::uint8_t> opus) {
    if (stopped_.load() || !connected_.load()) {
        return false;
    }
    const auto socket = udp_client_;
    if (!socket || !socket->is_started()) {
        return false;
    }
    const auto reservation = voice_send_budget_.TryAcquire();
    if (!reservation) {
        return false;
    }
    const auto packet = UdpVoiceProtocol::Build(association_code_, call_id, sequence, capture_time_ms, opus);
    if (!packet) {
        return false;
    }
    // asio2 borrows the buffer during submission; the callback owns its storage and queue reservation.
    socket->async_send(packet->Bytes().data(), packet->Size(), [packet, reservation](std::size_t) {});
    return true;
}

void UdpDirectConnection::OnUdpPacket(std::span<const char> packet) {
    if (stopped_) {
        return;
    }
    last_recv_ms_ = TimeUtil::GetCurrentTimestamp();
    const auto now_us = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    if (!media_window_.start_us)
        media_window_.start_us = now_us;
    const auto total_packet_count = ++recv_pkt_count_;
    const auto packet_type = PxUdpProtocol::ParseCommon(packet);
    const auto datagram = media::ParseMedia(std::span<const std::uint8_t>{
        reinterpret_cast<const std::uint8_t*>(packet.data()), packet.size()});
    if (datagram && datagram->kind == media::MediaKind::kVideo) {
        RestoreReachability();
        const auto identity = media::InspectVideoPacket(*datagram);
        if (identity) {
            if (const auto gap = receive_timing_.Observe(*identity, now_us)) {
                LOGW("UDP timing receive_gap: steady_us={}, gap_us={}, stream={}, previous={}/{}/{}, current={}/{}/{}, sequence={}", now_us,
                     gap->current_us - gap->previous_us, identity->stream, gap->previous.frame, gap->previous.block, gap->previous.shard,
                     identity->frame, identity->block, identity->shard, identity->sequence);
            }
        }
        received_media_packet_ = true;
        recv_video_pkt_count_++;
        auto receive_result = video_receiver_.Feed(*datagram, now_us);
        if (receive_result.statistics)
            receive_statistics_[datagram->stream] = *receive_result.statistics;
        media_window_.recovered_shards += receive_result.recovered;
        media_window_.losses += receive_result.losses.size();
        if (receive_result.rejected) {
            malformed_video_pkt_count_++;
        }
        if (receive_result.needs_idr) {
            const auto now_ms = TimeUtil::GetCurrentTimestamp();
            if (now_ms - last_idr_request_ms_.load() >= kIdrThrottleMs) {
                last_idr_request_ms_ = now_ms;
                ++media_window_.idr_requests;
                RequestIdr("");
            }
        } else if (receive_result.invalid_reference_frame) {
            // Invalidate after the last delivered encoder timestamp, never using the independent RTP frame number.
            RequestRfi(*receive_result.invalid_reference_frame, "");
            ++media_window_.rfi_requests;
        }
        if (receive_result.frame) {
            ++media_window_.frames;
            if (last_delivered_us_) {
                const auto gap = now_us - last_delivered_us_;
                media_window_.max_gap_us = std::max(media_window_.max_gap_us, gap);
                if (gap > 100000) {
                    ++media_window_.gaps_over_100ms;
                    if (identity) {
                        LOGW(
                            "UDP timing frame_gap: steady_us={}, gap_us={}, "
                            "stream={}, trigger={}/{}/{}, encoder_frame={}, "
                            "recovered={}",
                            now_us, gap, identity->stream, identity->frame,
                            identity->block, identity->shard,
                            receive_result.frame->frame_index,
                            receive_result.recovered);
                    }
                }
            }
            last_delivered_us_ = now_us;
            PostBinaryMessage(PxUdpProtocol::BuildFrameStatus(
                static_cast<std::uint32_t>(receive_result.frame->frame_index),
                1, static_cast<std::uint16_t>(receive_result.recovered)));
            OnCompleteFrame(*receive_result.frame);
        }
        const auto elapsed_us = media::MediaSteadyMicros() - now_us;
        if (elapsed_us > 5000 && identity) {
            LOGW(
                "UDP timing receive_work: steady_us={}, elapsed_us={}, "
                "stream={}, trigger={}/{}/{}, delivered={}, rejected={}",
                now_us, elapsed_us, identity->stream, identity->frame,
                identity->block, identity->shard,
                receive_result.frame.has_value(), receive_result.rejected);
        }
    } else if (datagram && datagram->kind == media::MediaKind::kAudio) {
        RestoreReachability();
        received_media_packet_ = true;
        auto receive_result = audio_receiver_.Feed(datagram->payload, now_us);
        for (const auto& frame : receive_result.packets) {
            OnAudioFrame(frame);
        }
    } else if (packet_type == PxUdpProtocol::kPktVoice) {
        auto voice_frame = UdpVoiceProtocol::Parse(packet);
        if (voice_frame && voice_frame->association_code == association_code_ &&
            voice_frame_cbk_) {
            RestoreReachability();
            received_media_packet_ = true;
            voice_frame_cbk_(std::move(*voice_frame));
        }
    } else if (packet_type == PxUdpProtocol::kPktCtrl) {
        std::string control_primary_argument;
        std::string control_secondary_argument;
        const auto control_subtype = PxUdpProtocol::ParseCtrl(
            packet, control_primary_argument, control_secondary_argument);
        static_cast<void>(control_secondary_argument);
        if (control_subtype == PxUdpProtocol::kCtrlKick) {
            LOGW("Udp direct kicked by render, reason: {}",
                 control_primary_argument);
            if (on_kick_cbk_) {
                on_kick_cbk_(control_primary_argument);
            }
        } else if (control_subtype == PxUdpProtocol::kCtrlHeartbeat &&
                   control_primary_argument == association_code_) {
            RestoreReachability();
        }
    }
    if (now_us - media_window_.start_us >= 5000000) {
        const auto& window = media_window_;
        LOGI("UDP media v2 window: frames={}, fps={:.1f}, max_gap_ms={:.1f}, gaps_gt_100ms={}, recovered_shards={}, loss_events={}, "
             "idr={}, rfi={}, audio_plc={}",
             window.frames, 1000000.0 * window.frames / (now_us - window.start_us), window.max_gap_us / 1000.0, window.gaps_over_100ms,
             window.recovered_shards, window.losses, window.idr_requests, window.rfi_requests, window.audio_plc);
        media_window_ = {};
        media_window_.start_us = now_us;
        for (const auto& [stream, statistics] : receive_statistics_) {
            LOGI(
                "UDP video totals: stream={}, data={}, parity={}, "
                "duplicates={}, late={}, reordered={}, recovered={}, "
                "complete={}, "
                "predicted={}, corrected={}, final_loss_events={}, "
                "unrecoverable={}, malformed={}",
                stream, statistics.data_packets, statistics.parity_packets,
                statistics.duplicates, statistics.late_packets,
                statistics.reordered_packets, statistics.recovered_data,
                statistics.completed_frames, statistics.predicted_losses,
                statistics.prediction_corrections, statistics.final_loss_events,
                statistics.unrecoverable_frames, statistics.malformed_packets);
        }
    }
    if (total_packet_count == 1 || total_packet_count % 500 == 0) {
        LOGI("udp recv pkt total={}, video={}, malformed_video={}",
             total_packet_count, recv_video_pkt_count_.load(),
             malformed_video_pkt_count_.load());
    }
}

void UdpDirectConnection::OnCompleteFrame(const media::VideoFrame& frame) {
    last_video_frame_ms_ = TimeUtil::GetCurrentTimestamp();
    if (stopped_ || !video_msg_cbk_ || frame.encoded.empty()) {
        return;
    }
    if (frame.frame_index % 300 == 0 || frame.kind != media::VideoFrameKind::kPredicted)
        LOGI("UDP media v2 video delivered: frame={}, kind={}, bytes={}, size={}x{}", frame.frame_index, static_cast<int>(frame.kind),
             frame.encoded.size(), frame.width, frame.height);
    if (!media_ready_reported_) {
        media_ready_reported_ = true;
        if (media_ready_cbk_)
            media_ready_cbk_();
    }
    if (stopped_)
        return;

    // 合成与 relay/ws 路径完全一致的标准 kVideoFrame proto,
    // 让 sdk 的按屏解码链原样接上(reassembler 保证首帧必为 IDR)
    auto video_message = std::make_shared<px::Message>();
    video_message->set_type(px::kVideoFrame);
    auto& video = *video_message->mutable_video_frame();
    video.set_type(frame.codec == media::VideoCodec::kH265 ? px::kNetHevc : px::kNetH264);
    video.set_data(frame.encoded.data(), frame.encoded.size());
    video.set_frame_index(frame.frame_index);
    video.set_key(frame.kind == media::VideoFrameKind::kIdr);
    video.set_frame_width(frame.width);
    video.set_frame_height(frame.height);
    video.set_mon_name(frame.monitor);
    video.set_mon_index(frame.stream);
    // debug 标记:区分 UDP 合成帧与其它 kVideoFrame 来源(参照 webrtc_local 的 rtc_synth)
    video.set_extra("udp_synth");

    video_msg_cbk_(video_message);
}

void UdpDirectConnection::RequestIdr(const std::string& monitor_name) {
    this->PostBinaryMessage(PxUdpProtocol::BuildIdrRequest(monitor_name));
}

void UdpDirectConnection::RequestIdrKeepalive(const std::string& monitor_name) {
    this->PostBinaryMessage(PxUdpProtocol::BuildIdrKeepalive(monitor_name));
}

void UdpDirectConnection::RequestRfi(uint64_t invalid_frame_index,
                                     const std::string& monitor_name) {
    this->PostBinaryMessage(
        PxUdpProtocol::BuildRfi(invalid_frame_index, monitor_name));
}

void UdpDirectConnection::CheckNeedIdr() {
    if (stopped_ || !connected_) {
        return;
    }
    const auto current_time_ms = TimeUtil::GetCurrentTimestamp();
    const auto last_frame_time_ms = last_video_frame_ms_.load();

    // Retry the initial key frame quickly; after playback starts, tolerate a longer transient gap.
    const auto timeout_ms =
        last_frame_time_ms == 0 ? kInitialFrameTimeoutMs : kNoFrameTimeoutMs;
    if (last_frame_time_ms != 0 &&
        current_time_ms - last_frame_time_ms < timeout_ms) {
        return;
    }
    // 1s 节流,防止关键帧风暴
    const auto last_idr_request_time_ms = last_idr_request_ms_.load();
    if (current_time_ms - last_idr_request_time_ms < kIdrThrottleMs) {
        return;
    }
    last_idr_request_ms_ = current_time_ms;
    if (last_frame_time_ms == 0) {
        LOGW(
            "Udp direct has no initial complete video frame for >{}ms; request "
            "IDR. now={}",
            timeout_ms, current_time_ms);
    } else {
        LOGI(
            "Udp direct static/recovery refresh after {}ms without a new "
            "complete frame.",
            timeout_ms);
    }
    this->RequestIdrKeepalive("");
}

void UdpDirectConnection::CheckWatchdog() {
    if (stopped_ || !connected_) {
        return;
    }
    const auto idle_time_ms =
        TimeUtil::GetCurrentTimestamp() - last_recv_ms_.load();
    if (idle_time_ms > kWatchdogTimeoutMs) {
        LOGW(
            "Udp direct watchdog timeout, no udp packet for {}ms, report "
            "disconnected.",
            idle_time_ms);
        connected_ = false;
        media_ready_reported_ = false;
        if (!disconn_reported_.exchange(true) && dis_conn_cbk_) {
            dis_conn_cbk_();
        }
    }
}

void UdpDirectConnection::RestoreReachability() {
    if (connected_.exchange(true)) {
        return;
    }
    disconn_reported_ = false;
    LOGI("Udp direct media channel is reachable again.");
    if (received_media_packet_ && !media_ready_reported_) {
        media_ready_reported_ = true;
        if (media_ready_cbk_) {
            media_ready_cbk_();
        }
    }
}

void UdpDirectConnection::SetOnVideoMessageCallback(
    const std::function<void(std::shared_ptr<px::Message>)>& callback) {
    video_msg_cbk_ = callback;
}

void UdpDirectConnection::SetOnAudioMessageCallback(
    const std::function<void(std::shared_ptr<px::Message>)>& callback) {
    audio_msg_cbk_ = callback;
}

void UdpDirectConnection::SetOnKickCallback(
    std::function<void(const std::string& reason)> callback) {
    on_kick_cbk_ = std::move(callback);
}

void UdpDirectConnection::SetOnMediaReadyCallback(
    std::function<void()> callback) {
    media_ready_cbk_ = std::move(callback);
}

bool UdpDirectConnection::IsAlive() {
    return connected_.load();
}

} // namespace px
