//
// Created by RGAA on 2023-12-27.
//

#include "sdk_net_client.h"

#include <utility>
#include "px_common_new/log.h"
#include "px_common_new/data.h"
#include "px_common_new/thread.h"
#include "px_common_new/file.h"
#include "px_common_new/message_notifier.h"
#include "sdk_messages.h"
#include "connection/udp_connection.h"
#include "connection/ws_connection.h"
#include "connection/wss_connection.h"
#include "connection/relay_connection.h"
#include "connection/webrtc_connection.h"
#include "connection/webrtc_local_connection.h"
#include "connection/udp_direct_connection.h"
#include "px_common_new/time_util.h"
#include "sdk_statistics.h"
#include "px_message_new/proto_converter.h"
#include "px_message_new/proto_message_maker.h"
#include <asio2/websocket/ws_client.hpp>
#include <asio2/asio2.hpp>

namespace px
{

    NetClient::NetClient(const std::shared_ptr<ThunderSdkParams>& params,
                         const std::shared_ptr<MessageNotifier>& notifier,
                         const std::string& ip,
                         int port,
                         const std::string& media_path,
                         const std::string& ft_path,
                         const ClientNetworkType& nt_type,
                         const std::string& device_id,
                         const std::string& remote_device_id,
                         const std::string& ft_device_id,
                         const std::string& ft_remote_device_id,
                         const std::string& stream_id) {

        this->stat_ = SdkStatistics::Instance();

        this->sdk_params_ = params;
        this->msg_notifier_ = notifier;
        this->media_path_ = media_path;
        this->ft_path_ = ft_path;
        this->network_type_ = nt_type;
        this->device_id_ = device_id;
        this->remote_device_id_ = remote_device_id;
        this->ft_device_id_ = ft_device_id;
        this->ft_remote_device_id_ = ft_remote_device_id;
        this->stream_id_ = stream_id;

    }

    NetClient::~NetClient() {
        Exit();
    }

    void NetClient::Start() {
        const auto weak_self = weak_from_this();
        if (!msg_listener_) {
            msg_listener_ = msg_notifier_->CreateListener(MessageExecutionLane::kControl);
            msg_listener_->Listen<SdkMsgTimer1000>([weak_self](const auto&) {
                if (const auto self = weak_self.lock()) {
                    self->HeartBeat();
                }
            });
        }
        if (network_type_ == ClientNetworkType::kWebsocket) {
            LOGI("Will connect by Websocket, ssl : {}", sdk_params_->ssl_);
            if (!sdk_params_->file_transfer_only_) {
                LOGI("media: {}", media_path_);
            }
            else {
                LOGI("file-transfer-only: media websocket disabled");
            }
            LOGI("file transfer: {}", ft_path_);
            if (sdk_params_->ssl_) {
                if (!sdk_params_->file_transfer_only_) {
                    media_conn_ = std::make_shared<WssConnection>(sdk_params_, msg_notifier_, sdk_params_->ip_, sdk_params_->port_, media_path_);
                }
                auto ft_path = ft_path_;
                if (sdk_params_->file_transfer_only_ && !sdk_params_->connection_ticket_.empty()) {
                    ft_path += "&file_only=1&ticket=" + sdk_params_->connection_ticket_ + "&client_nonce=" + sdk_params_->connection_nonce_;
                }
                ft_conn_ = std::make_shared<WssConnection>(sdk_params_, msg_notifier_, sdk_params_->ip_, sdk_params_->port_, ft_path);
            }
            else {
                if (!sdk_params_->file_transfer_only_) {
                    media_conn_ = std::make_shared<WsConnection>(sdk_params_, msg_notifier_, sdk_params_->ip_, sdk_params_->port_, media_path_);
                }
                auto ft_path = ft_path_;
                if (sdk_params_->file_transfer_only_ && !sdk_params_->connection_ticket_.empty()) {
                    ft_path += "&file_only=1&ticket=" + sdk_params_->connection_ticket_ + "&client_nonce=" + sdk_params_->connection_nonce_;
                }
                ft_conn_ = std::make_shared<WsConnection>(sdk_params_, msg_notifier_, sdk_params_->ip_, sdk_params_->port_, ft_path);
            }
        }
        else if (network_type_ == ClientNetworkType::kUdpKcp) {
            LOGI("Will connect by UDP");
            media_conn_ = std::make_shared<UdpConnection>(sdk_params_, msg_notifier_, sdk_params_->ip_, sdk_params_->port_);
        }
        else if (network_type_ == ClientNetworkType::kRelay) {
            auto auto_relay = !sdk_params_->enable_p2p_;
            if (!sdk_params_->file_transfer_only_) {
                media_conn_ = std::make_shared<RelayConnection>(sdk_params_, msg_notifier_, sdk_params_->relay_host_, sdk_params_->relay_port_, device_id_,remote_device_id_, auto_relay, kRoomTypeMedia);
            }
            ft_conn_ = std::make_shared<RelayConnection>(sdk_params_, msg_notifier_, sdk_params_->relay_host_, sdk_params_->relay_port_, ft_device_id_, ft_remote_device_id_, auto_relay, kRoomTypeFileTransfer);

            if (sdk_params_->enable_p2p_ && !sdk_params_->file_transfer_only_) {
                auto relay_conn = std::dynamic_pointer_cast<RelayConnection>(media_conn_);
                rtc_conn_ = WebRtcConnection::Make(relay_conn, sdk_params_, msg_notifier_);
            }
        }
        else if (network_type_ == ClientNetworkType::kWebRtc) {
            // Full WebRTC: Relay is signaling/control bootstrap only. Once ICE
            // succeeds, media/input/file data channels use host/srflx/relay
            // candidates selected by libwebrtc.
            LOGI("Will connect by full WebRTC, signaling relay: {}:{}",
                 sdk_params_->relay_host_, sdk_params_->relay_port_);
            sdk_params_->enable_p2p_ = true;
            auto relay_conn = std::make_shared<RelayConnection>(
                sdk_params_, msg_notifier_, sdk_params_->relay_host_, sdk_params_->relay_port_,
                device_id_, remote_device_id_, false, kRoomTypeMedia);
            media_conn_ = relay_conn;
            // File traffic uses the RTC FT data channel as well. Creating the
            // legacy file Relay here would consume a standalone file ticket
            // before the Render can redeem it for the RTC offer.
            ft_conn_ = nullptr;
            rtc_conn_ = WebRtcConnection::Make(relay_conn, sdk_params_, msg_notifier_);
        }
        else if (network_type_ == ClientNetworkType::kWebRtcDirect) {
            // Specialized direct-only path backed by net_rtc_local.
            LOGI("Will connect by WebRTC direct, ip: {}, port: {}", sdk_params_->ip_, sdk_params_->port_);
            rtc_local_conn_ = std::make_shared<WebRtcLocalConnection>(sdk_params_, msg_notifier_);
            media_conn_ = rtc_local_conn_;
        }
        else if (network_type_ == ClientNetworkType::kUdpDirect) {
            // GameStream 风格双通道:ws 控制面(可靠消息/状态机全复用) + 裸 UDP 媒体面,
            // 见 docs/udp_gamestream_channel_plan.md
            LOGI("Will connect by UDP direct, ws ctrl: {}:{}, udp media: {}:{}", sdk_params_->ip_,
                 sdk_params_->port_, sdk_params_->ip_, sdk_params_->udp_port_);
            // 文件传输复用 ws 控制面同一条 ws 服务,走独立 /file/transfer 路由;
            // 之前 kUdpDirect 未建 ft_conn_,导致文件传输(含剪贴板文件)被静默丢弃。
            auto ft_path = ft_path_;
            if (sdk_params_->file_transfer_only_ &&
                !sdk_params_->connection_ticket_.empty()) {
                ft_path += "&file_only=1&ticket=" + sdk_params_->connection_ticket_ +
                    "&client_nonce=" + sdk_params_->connection_nonce_;
            }
            if (sdk_params_->ssl_) {
                if (!sdk_params_->file_transfer_only_) {
                    media_conn_ = std::make_shared<WssConnection>(
                        sdk_params_, msg_notifier_, sdk_params_->ip_,
                        sdk_params_->port_, media_path_);
                }
                ft_conn_ = std::make_shared<WssConnection>(
                    sdk_params_, msg_notifier_, sdk_params_->ip_,
                    sdk_params_->port_, ft_path);
            }
            else {
                if (!sdk_params_->file_transfer_only_) {
                    media_conn_ = std::make_shared<WsConnection>(
                        sdk_params_, msg_notifier_, sdk_params_->ip_,
                        sdk_params_->port_, media_path_);
                }
                ft_conn_ = std::make_shared<WsConnection>(
                    sdk_params_, msg_notifier_, sdk_params_->ip_,
                    sdk_params_->port_, ft_path);
            }
            if (!sdk_params_->file_transfer_only_) {
                udp_direct_conn_ = std::make_shared<UdpDirectConnection>(
                    sdk_params_, msg_notifier_);
            }
        }
        else {
            LOGE("Start failed! Don't know the connection type: {}", (int)network_type_);
            return;
        }

        // Install the decoded-frame handoff before starting signaling. A very
        // fast Relay room preparation must not be able to create tracks before
        // the SDK callback chain exists.
        if (rtc_conn_) {
            rtc_conn_->SetOnVideoFrameCallback(
                [weak_self](int w, int h, std::shared_ptr<Data> i420) {
                    if (const auto self = weak_self.lock(); self && self->rtc_local_video_frame_cbk_) {
                        self->rtc_local_video_frame_cbk_(w, h, std::move(i420));
                    }
                });
            rtc_conn_->SetOnAudioDataCallback(
                [weak_self](std::shared_ptr<Data> pcm, int sample_rate, int channels) {
                    if (const auto self = weak_self.lock(); self && self->rtc_local_audio_cbk_) {
                        self->rtc_local_audio_cbk_(std::move(pcm), sample_rate, channels);
                    }
                });
        }

        // In full WebRTC mode Relay is only the signaling/bootstrap path. The
        // user-visible connection becomes ready after ICE plus the required
        // RTC data channel, not when the Relay room is merely established.
        std::shared_ptr<Connection> primary_conn = network_type_ == ClientNetworkType::kWebRtc
            ? std::static_pointer_cast<Connection>(rtc_conn_)
            : (media_conn_ ? media_conn_ : ft_conn_);
        if (!primary_conn) {
            LOGE("Start failed: no transport connection was created");
            return;
        }
        primary_conn->RegisterOnConnectedCallback([weak_self]() {
            if (const auto self = weak_self.lock(); self && self->conn_cbk_) {
                self->conn_cbk_();
            }
        });

        primary_conn->RegisterOnDisConnectedCallback([weak_self]() {
            if (const auto self = weak_self.lock(); self && self->dis_conn_cbk_) {
                self->dis_conn_cbk_();
            }
        });

        if (media_conn_) {
            media_conn_->RegisterOnMessageCallback([weak_self](std::shared_ptr<Data> data) {
                const auto self = weak_self.lock();
                if (!self) return;
                // statistics
                self->stat_->AppendRecvDataSize(data->Size());
                // parse
                if (auto m = self->ParseMessage(data); m) {
                    // ack
                    auto ack = ProtoMessageMaker::MakeAck(m->device_id(), m->stream_id(), m->send_time(), m->type());
                    if (self->media_conn_) self->media_conn_->PostBinaryMessage(ack);
                }
            });
            media_conn_->Start();
        }
        if (ft_conn_) {
            ft_conn_->RegisterOnMessageCallback([weak_self](std::shared_ptr<Data> data) {
                const auto self = weak_self.lock();
                if (!self) return;
                // statistics
                self->stat_->AppendRecvDataSize(data->Size());
                // parse
                if (auto m = self->ParseMessage(data); m) {
                    // ack
                    auto ack = ProtoMessageMaker::MakeAck(m->device_id(), m->stream_id(), m->send_time(), m->type());
                    if (self->ft_conn_) self->ft_conn_->PostBinaryMessage(ack);
                }
            });
            ft_conn_->Start();
        }

        if (sdk_params_->enable_p2p_ && rtc_conn_) {
            rtc_conn_->SetOnMediaMessageCallback([weak_self](std::shared_ptr<Data> msg) {
                const auto self = weak_self.lock();
                if (!self) return;
                //LOGI("OnMediaMessageCallback, : {}", msg.size());
                if (auto m = self->ParseMessage(msg); m) {
                    auto ack = ProtoMessageMaker::MakeAck(m->device_id(), m->stream_id(), m->send_time(), m->type());
                    if (self->rtc_conn_) self->rtc_conn_->PostMediaMessage(ack);
                }

                // statistics
                self->stat_->AppendRecvDataSize(msg->Size());
            });
            rtc_conn_->SetOnFtMessageCallback([weak_self](std::shared_ptr<Data> msg) {
                const auto self = weak_self.lock();
                if (!self) return;
                if (auto m = self->ParseMessage(msg); m) {
                    auto ack = ProtoMessageMaker::MakeAck(m->device_id(), m->stream_id(), m->send_time(), m->type());
                    if (self->rtc_conn_) self->rtc_conn_->PostFtMessage(ack);
                }

                self->stat_->AppendRecvDataSize(msg->Size());
            });
            rtc_conn_->Start();
        }

        if (rtc_local_conn_) {
            // media messages are handled by the generic media_conn_ callback above
            rtc_local_conn_->SetOnFtMessageCallback([weak_self](std::shared_ptr<Data> msg) {
                const auto self = weak_self.lock();
                if (!self) return;
                if (auto m = self->ParseMessage(msg); m) {
                    auto ack = ProtoMessageMaker::MakeAck(m->device_id(), m->stream_id(), m->send_time(), m->type());
                    if (self->rtc_local_conn_) self->rtc_local_conn_->PostFtMessage(ack);
                }

                self->stat_->AppendRecvDataSize(msg->Size());
            });
            rtc_local_conn_->SetOnRtcVideoFrameCallback([weak_self](int w, int h, std::shared_ptr<Data> i420) {
                if (const auto self = weak_self.lock(); self && self->rtc_local_video_frame_cbk_) {
                    self->rtc_local_video_frame_cbk_(w, h, std::move(i420));
                }
            });
            rtc_local_conn_->SetOnAudioDataCallback([weak_self](std::shared_ptr<Data> pcm, int sample_rate, int channels) {
                if (const auto self = weak_self.lock(); self && self->rtc_local_audio_cbk_) {
                    self->rtc_local_audio_cbk_(std::move(pcm), sample_rate, channels);
                }
            });
            rtc_local_conn_->SetOnVideoMessageCallback([weak_self](std::shared_ptr<px::Message> m) {
                const auto self = weak_self.lock();
                if (!self) return;
                // synthesized kVideoFrame from the encoded rtp tracks: dispatch exactly
                // like ParseMessage would, but WITHOUT an app-level ack - rtp carries
                // its own reliability(nack/pli), acking every frame would just flood
                // the media data channel
                self->stat_->AppendRecvDataSize((int64_t)m->ByteSizeLong());
                if (self->raw_msg_cbk_) {
                    self->raw_msg_cbk_(m);
                }
                if (self->video_frame_cbk_) {
                    self->video_frame_cbk_(m);
                }
            });
        }

        if (udp_direct_conn_) {
            // UDP 媒体面:组帧后合成的 kVideoFrame,与上面 rtc_local 相同的上送路径,
            // 同样不回 Ack(裸 UDP 无应用层确认,丢帧走 IDR 请求恢复)
            udp_direct_conn_->SetOnVideoMessageCallback([weak_self](std::shared_ptr<px::Message> m) {
                const auto self = weak_self.lock();
                if (!self) return;
                self->stat_->AppendRecvDataSize((int64_t)m->ByteSizeLong());
                if (self->raw_msg_cbk_) {
                    self->raw_msg_cbk_(m);
                }
                if (self->video_frame_cbk_) {
                    self->video_frame_cbk_(m);
                }
            });
            // UDP 音频:jitter buffer 按序交付/丢帧信号(空 data)都从这里上送,
            // 与 ws 路径一样直接进 audio_frame_cbk_(音频本就不走 raw_msg_cbk_)
            udp_direct_conn_->SetOnAudioMessageCallback([weak_self](std::shared_ptr<px::Message> m) {
                const auto self = weak_self.lock();
                if (!self) return;
                self->stat_->AppendRecvDataSize((int64_t)m->ByteSizeLong());
                if (self->audio_frame_cbk_) {
                    self->audio_frame_cbk_(m);
                }
            });
            // UDP 控制包踢人(kCtrlKick):复用"被接管"逻辑,与 kConnectionTakenOver 一致
            udp_direct_conn_->SetOnKickCallback([weak_self](const std::string& reason) {
                LOGW("Udp direct connection kicked, reason: {}", reason);
                if (const auto self = weak_self.lock()) {
                    self->msg_notifier_->SendAppMessage(SdkMsgConnectionTakenOver{});
                }
            });
            // UDP watchdog 断线:走与媒体连接相同的断线回调路径
            udp_direct_conn_->RegisterOnDisConnectedCallback([weak_self]() {
                LOGW("Udp direct media channel lost.");
                if (const auto self = weak_self.lock(); self && self->dis_conn_cbk_) {
                    self->dis_conn_cbk_();
                }
            });
            udp_direct_conn_->Start(sdk_params_->ip_, sdk_params_->udp_port_, device_id_, stream_id_);
        }
    }

    void NetClient::Exit() {
        if (exited_.exchange(true)) {
            return;
        }
        msg_listener_.reset();
        if (media_conn_) {
            LOGI("Queued message count: {}", queuing_message_count_.load());
            media_conn_->Stop();
        }
        if (ft_conn_) {
            ft_conn_->Stop();
        }
        if (rtc_conn_) {
            rtc_conn_->Stop();
        }
        if (udp_direct_conn_) {
            udp_direct_conn_->Stop();
        }
        rtc_local_conn_.reset();
        rtc_conn_.reset();
        udp_direct_conn_.reset();
        media_conn_.reset();
        ft_conn_.reset();
        LOGI("WS has exited...");
    }

    std::shared_ptr<Message> NetClient::ParseMessage(std::shared_ptr<Data> msg) {
        auto net_msg = std::make_shared<px::Message>();
        bool ok = net_msg->ParsePartialFromArray(msg->CStr(), msg->Size());
        if (!ok) {
            LOGE("Sdk ParseMessage failed.");
            return nullptr;
        }

        if (raw_msg_cbk_) {
            raw_msg_cbk_(net_msg);
        }

        if (net_msg->type() == px::kVideoFrame) {
            if (network_type_ == ClientNetworkType::kUdpDirect) {
                // udp_direct 模式下视频走 UDP 媒体面,ws 控制面不应携带;
                // 收到说明 render 未按 udp_media=1 过滤,直接丢弃防重复解码
                return net_msg;
            }
            {
#if 0           //save file
                px::VideoFrame frame = net_msg->video_frame();
                std::string name = frame.mon_name().substr(3);
                std::string t =  TimeUtil::FormatTimestamp2(TimeUtil::GetCurrentTimestamp());
                static auto f = File::OpenForWriteB(std::format(".\\{}_{}_recv_video.h265", name, t));
                f->Append(frame.data());
#endif
            }
            if (video_frame_cbk_) {
                video_frame_cbk_(net_msg);
            }
        }
        else if (net_msg->type() == px::kAudioFrame) {
            if (network_type_ == ClientNetworkType::kUdpDirect) {
                // udp_direct 模式下音频走 UDP 媒体面,ws 控制面不应携带;
                // 收到说明 render 未按 udp_media=1 过滤,直接丢弃防重复解码
                return net_msg;
            }
            if (audio_frame_cbk_) {
                audio_frame_cbk_(net_msg);
            }
        }
        else if (net_msg->type() == px::kCursorInfoSync) {
            if(cursor_info_sync_cbk_) {
                cursor_info_sync_cbk_(net_msg);
            }
        }
        else if (net_msg->type() == px::kRendererAudioSpectrum) {
            if (audio_spectrum_cbk_) {
                audio_spectrum_cbk_(net_msg);
            }
        }
        else if (net_msg->type() == px::kOnHeartBeat) {
            if (hb_cbk_) {
                hb_cbk_(net_msg);
            }

            // calculate network delay (心跳与鼠标事件同走 WS 控制面,此 RTT 即输入回环网络往返)
            const auto& hb = net_msg->on_heartbeat();
            auto send_timestamp = hb.timestamp();
            auto current_timestamp = TimeUtil::GetCurrentTimestamp();
            auto diff = current_timestamp - send_timestamp;
            stat_->AppendNetTimeDelay((int32_t)diff);
            // 每 5 次心跳打一条,观测输入网络往返是否异常(局域网正常应 1~3ms)
            static int s_hb_log_cnt = 0;
            if (++s_hb_log_cnt % 5 == 1) {
                LOGI("[LAT-net] heartbeat rtt={}ms", diff);
            }

            // save render statistics
            auto& monitors_info = hb.monitors_info();
            for (const auto& [monitor_name, info] : monitors_info) {
                stat_->UpdateIsolatedMonitorStatisticsInfoInRender(monitor_name, info);
            }

            stat_->video_capture_type_ = hb.video_capture_type();
            stat_->audio_capture_type_ = hb.audio_capture_type();
            stat_->audio_encode_type_ = hb.audio_encode_type();

            stat_->remote_pc_info_ = hb.pc_info();
            stat_->remote_desktop_name_ = hb.desktop_name();
            stat_->remote_hd_info_ = hb.device_info();
            stat_->remote_os_name_ = hb.os_name();
        }
        else if (net_msg->type() == px::kClipboardInfo) {
            if (clipboard_cbk_) {
                clipboard_cbk_(net_msg);
            }
        }
        else if (net_msg->type() == px::kServerConfiguration) {
            if (rtc_local_conn_ && net_msg->has_config()) {
                rtc_local_conn_->UpdateTrackMonitors(net_msg->config());
            }
            if (config_cbk_) {
                config_cbk_(net_msg);
            }
        }
        else if (net_msg->type() == px::kMonitorSwitched) {
            if (monitor_switched_cbk_) {
                monitor_switched_cbk_(net_msg);
            }
        }
        else if (net_msg->type() == px::kConnectionTakenOver) {
            // render 主动断开:被其它客户端接管
            msg_notifier_->SendAppMessage(SdkMsgConnectionTakenOver{});
        }
        else if (net_msg->type() == px::kChangeMonitorResolutionResult) {
            auto sub = net_msg->change_monitor_resolution_result();
            msg_notifier_->SendAppMessage(SdkMsgChangeMonitorResolutionResult {
                .monitor_name_ = sub.monitor_name(),
                .result = sub.result(),
            });
        }
        else if (net_msg->type() == px::kSigAnswerSdpMessage) {
            auto sub = net_msg->sig_answer_sdp();
            msg_notifier_->SendAppMessage(SdkMsgRemoteAnswerSdp {
                .answer_sdp_ = sub,
            });
        }
        else if (net_msg->type() == px::kSigIceMessage) {
            auto sub = net_msg->sig_ice();
            msg_notifier_->SendAppMessage(SdkMsgRemoteIce {
                .ice_ = sub,
            });
        }
        return net_msg;
    }

    void NetClient::PostMediaMessage(std::shared_ptr<Data> msg) {
        if (sdk_params_->enable_p2p_ && rtc_conn_ && rtc_conn_->IsMediaChannelReady()) {
            auto queuing_msg_count = rtc_conn_->GetQueuingMediaMsgCount();
            auto has_enough_buffer = rtc_conn_->HasEnoughBufferForQueuingMediaMessages();
            int wait_count = 0;
            while (queuing_msg_count >= kMaxFileTransferQueuedMessages || !has_enough_buffer) {
                if (!rtc_conn_->IsMediaChannelReady()) {
                    return;
                }
                TimeUtil::DelayByCount(1);
                queuing_msg_count = rtc_conn_->GetQueuingMediaMsgCount();
                has_enough_buffer = rtc_conn_->HasEnoughBufferForQueuingMediaMessages();
                wait_count++;
            }
            if (wait_count > 0) {
                LOGI("===> [Media] wait for {}ms", wait_count);
            }

            rtc_conn_->PostMediaMessage(msg);
        }
        else if (rtc_local_conn_ && rtc_local_conn_->IsMediaChannelReady()) {
            auto queuing_msg_count = rtc_local_conn_->GetQueuingMediaMsgCount();
            auto has_enough_buffer = rtc_local_conn_->HasEnoughBufferForQueuingMediaMessages();
            int wait_count = 0;
            while (queuing_msg_count >= kMaxFileTransferQueuedMessages || !has_enough_buffer) {
                if (!rtc_local_conn_->IsMediaChannelReady()) {
                    return;
                }
                TimeUtil::DelayByCount(1);
                queuing_msg_count = rtc_local_conn_->GetQueuingMediaMsgCount();
                has_enough_buffer = rtc_local_conn_->HasEnoughBufferForQueuingMediaMessages();
                wait_count++;
            }
            if (wait_count > 0) {
                LOGI("===> [RTC Local Media] wait for {}ms", wait_count);
            }

            rtc_local_conn_->PostMediaMessage(msg);
        }
        else {
            auto queuing_msg_count = this->GetQueuingMediaMsgCount();
            int wait_count = 0;
            while (queuing_msg_count >= kMaxFileTransferQueuedMessages && wait_count < 200) {
                if (!media_conn_ || !media_conn_->IsAlive()) {
                    LOGW("===> [Media] connection not alive, drop the message, queuing: {}", queuing_msg_count);
                    return;
                }
                //LOGI("===> queue too many msgs, count: {}, wait for 1ms", queuing_msg_count);
                TimeUtil::DelayBySleep(1);
                queuing_msg_count = this->GetQueuingMediaMsgCount();
                wait_count++;
            }
            if (wait_count >= 200) {
                LOGW("===> [Media] wait timeout after {}ms, drop the message, queuing: {}", wait_count, queuing_msg_count);
                return;
            }

            if (media_conn_) {
                media_conn_->PostBinaryMessage(msg);
            }
        }

        stat_->AppendSentDataSize(msg->Size());
    }

    FileTransferSendResult NetClient::PostFileTransferMessage(std::shared_ptr<Data> msg) {
        if (!msg) {
            return FileTransferSendResult::TransportError("file-transfer message is empty");
        }

        if (sdk_params_->enable_p2p_ && rtc_conn_ && rtc_conn_->IsFtChannelReady()) {
            if (rtc_conn_->GetQueuingFtMsgCount() >= kMaxFileTransferQueuedMessages ||
                !rtc_conn_->HasEnoughBufferForQueuingFtMessages()) {
                const auto signal = rtc_conn_->AcquireFileTransferWritableSignal();
                if (rtc_conn_->GetQueuingFtMsgCount() <= kFileTransferQueueLowWatermark &&
                    rtc_conn_->HasEnoughBufferForQueuingFtMessages()) {
                    signal->NotifyWritable();
                }
                return FileTransferSendResult::Busy(
                    "standard RTC file channel is congested",
                    signal);
            }
            rtc_conn_->PostFtMessage(msg);
        }
        else if (rtc_local_conn_ && rtc_local_conn_->IsFtChannelReady()) {
            if (rtc_local_conn_->GetQueuingFtMsgCount() >= kMaxFileTransferQueuedMessages ||
                !rtc_local_conn_->HasEnoughBufferForQueuingFtMessages()) {
                const auto signal = rtc_local_conn_->AcquireFileTransferWritableSignal();
                if (rtc_local_conn_->GetQueuingFtMsgCount() <= kFileTransferQueueLowWatermark &&
                    rtc_local_conn_->HasEnoughBufferForQueuingFtMessages()) {
                    signal->NotifyWritable();
                }
                return FileTransferSendResult::Busy(
                    "direct RTC file channel is congested",
                    signal);
            }
            rtc_local_conn_->PostFtMessage(msg);
        }
        else {
            if (!ft_conn_ || !ft_conn_->IsAlive()) {
                return FileTransferSendResult::Disconnected("file-transfer connection is not alive");
            }
            if (ft_conn_->GetQueuingMsgCount() >= kMaxFileTransferQueuedMessages) {
                const auto signal = ft_conn_->AcquireFileTransferWritableSignal();
                if (ft_conn_->GetQueuingMsgCount() <= kFileTransferQueueLowWatermark) {
                    signal->NotifyWritable();
                }
                return FileTransferSendResult::Busy(
                    "file-transfer connection queue is full",
                    signal);
            }
            ft_conn_->PostBinaryMessage(msg);
        }

        stat_->AppendSentDataSize(msg->Size());
        return FileTransferSendResult::Accepted();
    }

    void NetClient::SetOnVideoFrameMsgCallback(OnVideoFrameMsgCallback&& cbk) {
        video_frame_cbk_ = std::move(cbk);
    }

    void NetClient::SetOnAudioFrameMsgCallback(OnAudioFrameMsgCallback&& cbk) {
        audio_frame_cbk_ = std::move(cbk);
    }

    void NetClient::SetOnCursorInfoSyncMsgCallback(OnCursorInfoSyncMsgCallback&& cbk) {
        cursor_info_sync_cbk_ = std::move(cbk);
    }

    void NetClient::SetOnConnectCallback(OnConnectedCallback&& cbk) {
        conn_cbk_ = std::move(cbk);
    }

    void NetClient::SetOnDisconnectedCallback(OnDisconnectedCallback&& cbk) {
        dis_conn_cbk_ = std::move(cbk);
    }

    void NetClient::SetOnAudioSpectrumCallback(OnAudioSpectrumCallback&& cbk) {
        audio_spectrum_cbk_ = std::move(cbk);
    }

    void NetClient::SetOnHeartBeatCallback(px::OnHeartBeatInfoCallback&& cbk) {
        hb_cbk_ = std::move(cbk);
    }

    void NetClient::SetOnClipboardCallback(OnClipboardInfoCallback&& cbk) {
        clipboard_cbk_ = std::move(cbk);
    }

    void NetClient::SetOnServerConfigurationCallback(px::OnConfigCallback&& cbk) {
        config_cbk_ = std::move(cbk);
    }

    void NetClient::SetOnMonitorSwitchedCallback(OnMonitorSwitchedCallback&& cbk) {
        monitor_switched_cbk_ = std::move(cbk);
    }

    void NetClient::SetOnRawMessageCallback(px::OnRawMessageCallback&& cbk) {
        raw_msg_cbk_ = std::move(cbk);
    }

    void NetClient::SetOnRtcLocalVideoFrameCallback(OnRtcLocalVideoFrameCallback&& cbk) {
        rtc_local_video_frame_cbk_ = std::move(cbk);
    }

    void NetClient::SetOnRtcLocalAudioCallback(OnRtcLocalAudioCallback&& cbk) {
        rtc_local_audio_cbk_ = std::move(cbk);
    }

    void NetClient::SetRtcLocalCapturingMonitorNameProvider(std::function<std::string()>&& provider) {
        if (rtc_local_conn_) {
            rtc_local_conn_->SetCapturingMonitorNameProvider(std::move(provider));
        }
    }

    void NetClient::HeartBeat() {
        auto msg = std::make_shared<Message>();
        msg->set_type(px::kHeartBeat);
        msg->set_device_id(device_id_);
        msg->set_stream_id(stream_id_);
        auto hb = msg->mutable_heartbeat();
        hb->set_index(hb_idx_++);
        hb->set_timestamp((int64_t)TimeUtil::GetCurrentTimestamp());
        auto proto_msg = msg->SerializeAsString();
        if (auto buffer = px::ProtoAsData(msg); buffer) {
            this->PostMediaMessage(buffer);
            static_cast<void>(this->PostFileTransferMessage(buffer));
        }
    }

    int64_t NetClient::GetQueuingMediaMsgCount() {
        if (sdk_params_->enable_p2p_ && rtc_conn_) {
            return rtc_conn_->GetQueuingMediaMsgCount();
        }
        else if (rtc_local_conn_) {
            return rtc_local_conn_->GetQueuingMediaMsgCount();
        }
        else if (media_conn_) {
            return media_conn_->GetQueuingMsgCount();
        }
        else {
            return 0;
        }
    }

    int64_t NetClient::GetQueuingFtMsgCount() {
        if (sdk_params_->enable_p2p_ && rtc_conn_) {
            return rtc_conn_->GetQueuingFtMsgCount();
        }
        else if (rtc_local_conn_) {
            return rtc_local_conn_->GetQueuingFtMsgCount();
        }
        else if (ft_conn_) {
            return ft_conn_->GetQueuingMsgCount();
        }
        else {
            return 0;
        }
    }

    void NetClient::On16msTimeout() {
        if (sdk_params_->enable_p2p_ && rtc_conn_) {
            rtc_conn_->On16msTimeout();
        }
        if (rtc_local_conn_) {
            rtc_local_conn_->On16msTimeout();
        }
        if (ft_conn_) {
            ft_conn_->On16msTimeout();
        }
        if (media_conn_) {
            media_conn_->On16msTimeout();
        }
    }

    void NetClient::RetryConnection() {
        if (media_conn_) {
            media_conn_->RetryConnection();
        }
        if (ft_conn_) {
            ft_conn_->RetryConnection();
        }
        if (rtc_conn_) {
            rtc_conn_->RetryConnection();
        }
        if (udp_direct_conn_) {
            // 裸 UDP 无重连概念,先空实现(ws 控制面断线即整体断线)
            udp_direct_conn_->RetryConnection();
        }
    }

    bool NetClient::RestartRtcIce(const std::string& ice_config_json,
                                  const std::string& connection_ticket,
                                  const std::string& client_nonce,
                                  const std::string& instance_id) {
        return rtc_conn_ && rtc_conn_->RestartIce(
            ice_config_json, connection_ticket, client_nonce, instance_id);
    }
}
