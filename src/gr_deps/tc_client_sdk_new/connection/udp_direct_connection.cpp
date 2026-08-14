//
// Created by RGAA on 12/08/2026.
//

#include "udp_direct_connection.h"
#include "tc_common_new/log.h"
#include "tc_common_new/data.h"
#include "tc_common_new/time_util.h"
#include "tc_message.pb.h"
#include <asio2/asio2.hpp>
#include <asio2/udp/udp_client.hpp>

namespace tc
{

    UdpDirectConnection::UdpDirectConnection(const std::shared_ptr<ThunderSdkParams>& params,
                                             const std::shared_ptr<MessageNotifier>& notifier)
                                             : Connection(params, notifier) {
        // 组帧完成:合成标准 kVideoFrame proto 上送(走与 webrtc_local 相同的回调通道,不回 Ack)
        reassembler_.on_frame_ = [this](const GrUdpFrameReassembler::CompleteFrame& frame) {
            this->OnCompleteFrame(frame);
        };
        // 判丢:请 render 重发 IDR(空 mon_name = 所有屏)。
        // IDR 节流(moonlight 同款):网络差时狂要 IDR 只会加重拥塞——巨型 IDR 帧
        // (可能 150+ shard)本身最易丢,丢了又要,无限 GOP 下永远花屏;按 mon_slot 1s 去重
        reassembler_.on_frame_lost_ = [this](uint8_t mon_slot, uint32_t lost_frame_index) {
            auto now = std::chrono::steady_clock::now();
            auto& last = last_rfi_time_[mon_slot];
            if (last.time_since_epoch().count() != 0 &&
                now - last < std::chrono::milliseconds(kRfiThrottleMs)) {
                return;
            }
            last = now;
            LOGW("Udp direct frame lost, mon slot: {}, frame: {}, request RFI.", mon_slot, lost_frame_index);
            this->RequestRfi(lost_frame_index, "");
            last_rfi_request_ms_ = TimeUtil::GetCurrentTimestamp();
        };
        // 帧状态反馈:每帧一条(完成/判丢都报),驱动 render 端动态调 FEC 百分比;
        // PostBinaryMessage 走 asio2 async_send 非阻塞,不会拖慢接收线程
        reassembler_.on_frame_status_ = [this](uint8_t mon_slot, uint32_t frame_index,
                                               uint16_t received, uint16_t lost) {
            (void)mon_slot;
            this->PostBinaryMessage(GrUdpProtocol::BuildFrameStatus(frame_index, received, lost));
        };
        // 音频按序交付:合成标准 kAudioFrame proto 上送(参数与 render opus_encoder 一致:
        // 48k/2ch/16bit,20ms 一帧 960 samples)
        audio_jitter_.on_frame_ = [this](uint32_t seq, uint32_t timestamp_ms, const char* payload, size_t len) {
            (void)seq;
            (void)timestamp_ms;
            if (stopped_ || !audio_msg_cbk_) {
                return;
            }
            audio_lost_log_count_ = 0; // 恢复正常交付,下一次判丢重新计数
            auto msg = std::make_shared<tc::Message>();
            msg->set_type(tc::kAudioFrame);
            auto* audio = msg->mutable_audio_frame();
            audio->set_samples(48000);
            audio->set_channels(2);
            audio->set_bits(16);
            audio->set_frame_size(960);
            audio->set_data(payload, len);
            // debug 标记:区分 UDP 合成帧与其它 kAudioFrame 来源(参照视频的 udp_synth)
            audio->set_extra("udp_synth");
            audio_msg_cbk_(msg);
        };
        // 丢帧信号:空 data 的 kAudioFrame proto,thunder_sdk 收到后调 DecodeDummy 走 PLC 补 20ms
        audio_jitter_.on_lost_ = [this](uint32_t seq) {
            if (stopped_ || !audio_msg_cbk_) {
                return;
            }
            // 丢包风暴时逐条打日志会刷爆磁盘并拖垮 UDP 接收线程(真机踩过),每 50 条汇总一次
            if (++audio_lost_log_count_ == 1 || audio_lost_log_count_ % 50 == 0) {
                LOGW("Udp direct audio frame lost, seq: {}, PLC conceal. (burst: {})", seq, audio_lost_log_count_);
            }
            auto msg = std::make_shared<tc::Message>();
            msg->set_type(tc::kAudioFrame);
            auto* audio = msg->mutable_audio_frame();
            audio->set_samples(48000);
            audio->set_channels(2);
            audio->set_bits(16);
            audio->set_frame_size(960);
            audio->set_extra("udp_lost");
            audio_msg_cbk_(msg);
        };
    }

    void UdpDirectConnection::Start(const std::string& host, int udp_port,
                                    const std::string& device_id, const std::string& stream_id) {
        host_ = host;
        udp_port_ = udp_port;
        device_id_ = device_id;
        stream_id_ = stream_id;

        // 支持同实例重连:先停旧 socket,再清空连接态与组帧/jitter 状态。
        // 否则 render 重启/接管后 frame_index 回退,旧 finished_ 水位会把新流全丢。
        if (udp_client_ && udp_client_->is_started()) {
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
        reassembler_.Reset();
        audio_jitter_.Reset();

        udp_client_ = std::make_shared<asio2::udp_client>();
        // 注意:裸 UDP,不传 asio2::use_kcp(可靠重传对视频是负优化,见 udp_gamestream_channel_plan.md)

        udp_client_->bind_connect([this]() {
            if (asio2::get_last_error()) {
                LOGE("udp direct connect failure : {} {}", asio2::last_error_val(), asio2::last_error_msg().c_str());
            }
            else {
                LOGI("udp direct connect success : {} {}, remote: {}:{}", udp_client_->local_address().c_str(),
                     udp_client_->local_port(), host_, udp_port_);
                connected_ = true;
                last_recv_ms_ = TimeUtil::GetCurrentTimestamp();
                // 高动态画面一帧 ~89 个 UDP 包(~125KB)毫秒内突发,默认接收缓冲(~64KB)必然溢出丢包,
                // 接收缓冲调 8MB、发送 1MB;Windows 上读回值可能与设置值不同,打出来即可
                {
                    asio::error_code ec;
                    auto& sock = udp_client_->socket();
                    sock.set_option(asio::socket_base::receive_buffer_size(8 * 1024 * 1024), ec);
                    if (ec) LOGW("udp set rcvbuf 8MB failed: {}", ec.message());
                    sock.set_option(asio::socket_base::send_buffer_size(1 * 1024 * 1024), ec);
                    if (ec) LOGW("udp set sndbuf 1MB failed: {}", ec.message());
                    asio::socket_base::receive_buffer_size rcv;
                    asio::socket_base::send_buffer_size snd;
                    sock.get_option(rcv, ec);
                    sock.get_option(snd, ec);
                    LOGI("udp direct socket buffer: rcv = {}, snd = {}", rcv.value(), snd.value());
                }
                // 立即发 hello,render 按源地址绑定媒体会话并触发该屏 IDR
                this->PostBinaryMessage(GrUdpProtocol::BuildHello(device_id_, stream_id_));
                // 显式补一发 IDR 请求。正常路径 render 收到连接事件会自己插 IDR,
                // 但 render 重启/断线重建时容易出现“hello 已发、关键帧没来”,客户端会
                // 一直停在“已收到配置信息,等待视频帧”。这里不依赖事件链路,再要一次。
                last_idr_request_ms_ = TimeUtil::GetCurrentTimestamp();
                this->RequestIdrKeepalive("");
                // 1s 心跳:保持 NAT 映射,让 render 感知会话在线
                udp_client_->start_timer(kTimerHeartbeat, 1000, [this]() {
                    this->PostBinaryMessage(GrUdpProtocol::BuildHeartbeat(stream_id_));
                });
                // 无完整视频帧兜底:2s 内没组出帧再请 IDR(节流 1s)。
                // 丢帧恢复走 RFI 重试,不走这里。
                udp_client_->start_timer(kTimerIdrRetry, 1000, [this]() {
                    this->CheckNeedIdr();
                });
                // watchdog:长时间收不到任何 UDP 包视为媒体面断开
                udp_client_->start_timer(kTimerWatchdog, 1000, [this]() {
                    this->CheckWatchdog();
                });
                udp_client_->post_queued_event([this]() {
                    if (conn_cbk_) {
                        conn_cbk_();
                    }
                });
            }

        }).bind_disconnect([this]() {
            if (stopped_) {
                connected_ = false;
                return;
            }
            LOGI("udp direct disconnect : {} {}", asio2::last_error_val(), asio2::last_error_msg().c_str());
            connected_ = false;
            if (!disconn_reported_.exchange(true) && dis_conn_cbk_) {
                dis_conn_cbk_();
            }

        }).bind_recv([this](std::string_view data) {
            this->OnUdpPacket(data.data(), data.size());
        });

        udp_client_->async_start(host_, udp_port_);
    }

    void UdpDirectConnection::Stop() {
        if (stopped_.exchange(true)) {
            return;
        }
        if (udp_client_ && udp_client_->is_started()) {
            udp_client_->stop_all_timers();
            udp_client_->stop();
        }
    }

    void UdpDirectConnection::PostBinaryMessage(std::shared_ptr<Data> msg) {
        if (!stopped_ && udp_client_ && udp_client_->is_started()) {
            queuing_message_count_++;
            udp_client_->async_send(msg->CStr(), msg->Size(), [this]() {
                queuing_message_count_--;
            });
        }
    }

    void UdpDirectConnection::OnUdpPacket(const char* data, size_t size) {
        if (stopped_) {
            return;
        }
        last_recv_ms_ = TimeUtil::GetCurrentTimestamp();
        auto total = ++recv_pkt_count_;
        auto pkt_type = GrUdpProtocol::ParseCommon(data, size);
        if (pkt_type == GrUdpProtocol::kPktVideo) {
            recv_video_pkt_count_++;
            GrUdpProtocol::VideoShardInfo shard;
            if (!GrUdpProtocol::ParseVideoShard(data, size, shard)) {
                malformed_video_pkt_count_++;
            }
            reassembler_.AddPacket(data, size);
        }
        else if (pkt_type == GrUdpProtocol::kPktAudio) {
            GrUdpProtocol::AudioPacketInfo audio;
            if (GrUdpProtocol::ParseAudioPacket(data, size, audio)) {
                audio_jitter_.AddPacket(audio.seq_, audio.timestamp_ms_, audio.payload_, audio.payload_len_);
            }
        }
        else if (pkt_type == GrUdpProtocol::kPktCtrl) {
            std::string s1, s2;
            auto subtype = GrUdpProtocol::ParseCtrl(data, size, s1, s2);
            if (subtype == GrUdpProtocol::kCtrlKick) {
                LOGW("Udp direct kicked by render, reason: {}", s1);
                if (on_kick_cbk_) {
                    on_kick_cbk_(s1);
                }
            }
        }
        if (total == 1 || total % 500 == 0) {
            LOGI("udp recv pkt total={}, video={}, malformed_video={}",
                 total, recv_video_pkt_count_.load(), malformed_video_pkt_count_.load());
        }
    }

    void UdpDirectConnection::OnCompleteFrame(const GrUdpFrameReassembler::CompleteFrame& frame) {
        last_video_frame_ms_ = TimeUtil::GetCurrentTimestamp();
        if (stopped_ || !video_msg_cbk_ || !frame.data_ || frame.data_->Size() == 0) {
            return;
        }

        // 合成与 relay/ws 路径完全一致的标准 kVideoFrame proto,
        // 让 sdk 的按屏解码链原样接上(reassembler 保证首帧必为 IDR)
        auto msg = std::make_shared<tc::Message>();
        msg->set_type(tc::kVideoFrame);
        auto* video = msg->mutable_video_frame();
        video->set_type(frame.codec_ == GrUdpProtocol::kCodecH265 ? tc::kNetHevc : tc::kNetH264);
        video->set_data(frame.data_->CStr(), frame.data_->Size());
        video->set_frame_index(frame.frame_index_);
        video->set_key(frame.key_);
        video->set_frame_width(frame.frame_width_);
        video->set_frame_height(frame.frame_height_);
        video->set_mon_name(frame.mon_name_);
        video->set_mon_index(frame.mon_slot_);
        // debug 标记:区分 UDP 合成帧与其它 kVideoFrame 来源(参照 webrtc_local 的 rtc_synth)
        video->set_extra("udp_synth");

        video_msg_cbk_(msg);
    }

    void UdpDirectConnection::RequestIdr(const std::string& mon_name) {
        this->PostBinaryMessage(GrUdpProtocol::BuildIdrRequest(mon_name));
    }

    void UdpDirectConnection::RequestIdrKeepalive(const std::string& mon_name) {
        this->PostBinaryMessage(GrUdpProtocol::BuildIdrKeepalive(mon_name));
    }

    void UdpDirectConnection::RequestRfi(uint64_t invalid_frame_index, const std::string& mon_name) {
        this->PostBinaryMessage(GrUdpProtocol::BuildRfi(invalid_frame_index, mon_name));
    }

    void UdpDirectConnection::CheckNeedIdr() {
        if (stopped_ || !connected_) {
            return;
        }
        auto now = TimeUtil::GetCurrentTimestamp();
        auto last_frame = last_video_frame_ms_.load();

        // 单帧/多帧丢包靠 RFI 重试恢复(Moonlight 同款),这里只做"长时间完全没帧"的兜底。
        if (last_frame != 0 && now - last_frame < kNoFrameTimeoutMs) {
            return;
        }
        // 1s 节流,防止关键帧风暴
        auto last_idr = last_idr_request_ms_.load();
        if (now - last_idr < kIdrThrottleMs) {
            return;
        }
        last_idr_request_ms_ = now;
        LOGW("Udp direct no complete video frame for >{}ms, request IDR. last_frame={}, now={}",
             kNoFrameTimeoutMs, last_frame, now);
        this->RequestIdrKeepalive("");
    }

    void UdpDirectConnection::CheckWatchdog() {
        if (stopped_ || !connected_) {
            return;
        }
        auto idle = TimeUtil::GetCurrentTimestamp() - last_recv_ms_.load();
        if (idle > kWatchdogTimeoutMs) {
            LOGW("Udp direct watchdog timeout, no udp packet for {}ms, report disconnected.", idle);
            connected_ = false;
            if (!disconn_reported_.exchange(true) && dis_conn_cbk_) {
                dis_conn_cbk_();
            }
        }
    }

    void UdpDirectConnection::SetOnVideoMessageCallback(const std::function<void(std::shared_ptr<tc::Message>)>& cbk) {
        video_msg_cbk_ = cbk;
    }

    void UdpDirectConnection::SetOnAudioMessageCallback(const std::function<void(std::shared_ptr<tc::Message>)>& cbk) {
        audio_msg_cbk_ = cbk;
    }

    void UdpDirectConnection::SetOnKickCallback(std::function<void(const std::string& reason)> cbk) {
        on_kick_cbk_ = std::move(cbk);
    }

    bool UdpDirectConnection::IsAlive() {
        return connected_.load();
    }

}
