//
// Created by RGAA on 2023-12-27.
//

#include "sdk_net_client.h"

#include <utility>
#include "tc_common_new/log.h"
#include "tc_common_new/data.h"
#include "tc_common_new/thread.h"
#include "tc_common_new/file.h"
#include "tc_common_new/message_notifier.h"
#include "sdk_messages.h"
#include "connection/udp_connection.h"
#include "connection/ws_connection.h"
#include "connection/wss_connection.h"
#include "connection/relay_connection.h"
#include "connection/webrtc_connection.h"
#include "connection/webrtc_local_connection.h"
#include "connection/udp_direct_connection.h"
#include "tc_common_new/time_util.h"
#include "sdk_statistics.h"
#include "tc_message_new/proto_converter.h"
#include "tc_message_new/proto_message_maker.h"
#include <asio2/websocket/ws_client.hpp>
#include <asio2/asio2.hpp>

namespace tc
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

        msg_listener_ = msg_notifier_->CreateListener();
        msg_listener_->Listen<SdkMsgTimer1000>([=, this](const auto& msg) {
            this->HeartBeat();
        });
    }

    NetClient::~NetClient() = default;

    void NetClient::Start() {
        if (network_type_ == ClientNetworkType::kWebsocket) {
            LOGI("Will connect by Websocket, ssl : {}", sdk_params_->ssl_);
            LOGI("media: {}", media_path_);
            LOGI("file transfer: {}", ft_path_);
            if (sdk_params_->ssl_) {
                media_conn_ = std::make_shared<WssConnection>(sdk_params_, msg_notifier_, sdk_params_->ip_, sdk_params_->port_, media_path_);
                ft_conn_ = std::make_shared<WssConnection>(sdk_params_, msg_notifier_, sdk_params_->ip_, sdk_params_->port_, ft_path_);
            }
            else {
                media_conn_ = std::make_shared<WsConnection>(sdk_params_, msg_notifier_, sdk_params_->ip_, sdk_params_->port_, media_path_);
                ft_conn_ = std::make_shared<WsConnection>(sdk_params_, msg_notifier_, sdk_params_->ip_, sdk_params_->port_, ft_path_);
            }
        }
        else if (network_type_ == ClientNetworkType::kUdpKcp) {
            LOGI("Will connect by UDP");
            media_conn_ = std::make_shared<UdpConnection>(sdk_params_, msg_notifier_, sdk_params_->ip_, sdk_params_->port_);
        }
        else if (network_type_ == ClientNetworkType::kRelay) {
            auto auto_relay = !sdk_params_->enable_p2p_;
            media_conn_ = std::make_shared<RelayConnection>(sdk_params_, msg_notifier_, sdk_params_->relay_host_, sdk_params_->relay_port_, device_id_,remote_device_id_, auto_relay, kRoomTypeMedia);
            ft_conn_ = std::make_shared<RelayConnection>(sdk_params_, msg_notifier_, sdk_params_->relay_host_, sdk_params_->relay_port_, ft_device_id_, ft_remote_device_id_, auto_relay, kRoomTypeFileTransfer);

            if (sdk_params_->enable_p2p_) {
                auto relay_conn = std::dynamic_pointer_cast<RelayConnection>(media_conn_);
                rtc_conn_ = std::make_shared<WebRtcConnection>(relay_conn, sdk_params_, msg_notifier_);
            }
        }
        else if (network_type_ == ClientNetworkType::kWebRtc) {
            // webrtc local(direct): http signaling + rtp video track + data channels,
            // one connection carries both media and ft messages
            LOGI("Will connect by WebRTC local(direct), ip: {}, port: {}", sdk_params_->ip_, sdk_params_->port_);
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
            if (sdk_params_->ssl_) {
                media_conn_ = std::make_shared<WssConnection>(sdk_params_, msg_notifier_, sdk_params_->ip_, sdk_params_->port_, media_path_);
                ft_conn_ = std::make_shared<WssConnection>(sdk_params_, msg_notifier_, sdk_params_->ip_, sdk_params_->port_, ft_path_);
            }
            else {
                media_conn_ = std::make_shared<WsConnection>(sdk_params_, msg_notifier_, sdk_params_->ip_, sdk_params_->port_, media_path_);
                ft_conn_ = std::make_shared<WsConnection>(sdk_params_, msg_notifier_, sdk_params_->ip_, sdk_params_->port_, ft_path_);
            }
            udp_direct_conn_ = std::make_shared<UdpDirectConnection>(sdk_params_, msg_notifier_);
        }
        else {
            LOGE("Start failed! Don't know the connection type: {}", (int)network_type_);
            return;
        }

        media_conn_->RegisterOnConnectedCallback([=, this]() {
            if (conn_cbk_) {
                conn_cbk_();
            }
        });

        media_conn_->RegisterOnDisConnectedCallback([=, this]() {
            if (dis_conn_cbk_) {
                dis_conn_cbk_();
            }
        });

        media_conn_->RegisterOnMessageCallback([=, this](std::shared_ptr<Data> data) {
            // statistics
            this->stat_->AppendRecvDataSize(data->Size());
            // parse
            if (auto m = this->ParseMessage(data); m) {
                // ack
                auto ack = ProtoMessageMaker::MakeAck(m->device_id(), m->stream_id(), m->send_time(), m->type());
                media_conn_->PostBinaryMessage(ack);
            }
        });

        media_conn_->Start();
        if (ft_conn_) {
            ft_conn_->RegisterOnMessageCallback([=, this](std::shared_ptr<Data> data) {
                // statistics
                this->stat_->AppendRecvDataSize(data->Size());
                // parse
                if (auto m = this->ParseMessage(data); m) {
                    // ack
                    auto ack = ProtoMessageMaker::MakeAck(m->device_id(), m->stream_id(), m->send_time(), m->type());
                    ft_conn_->PostBinaryMessage(ack);
                }
            });
            ft_conn_->Start();
        }

        if (sdk_params_->enable_p2p_ && rtc_conn_) {
            rtc_conn_->SetOnMediaMessageCallback([=, this](std::shared_ptr<Data> msg) {
                //LOGI("OnMediaMessageCallback, : {}", msg.size());
                if (auto m = this->ParseMessage(msg); m) {
                    auto ack = ProtoMessageMaker::MakeAck(m->device_id(), m->stream_id(), m->send_time(), m->type());
                    rtc_conn_->PostMediaMessage(ack);
                }

                // statistics
                this->stat_->AppendRecvDataSize(msg->Size());
            });
            rtc_conn_->SetOnFtMessageCallback([=, this](std::shared_ptr<Data> msg) {
                if (auto m = this->ParseMessage(msg); m) {
                    auto ack = ProtoMessageMaker::MakeAck(m->device_id(), m->stream_id(), m->send_time(), m->type());
                    rtc_conn_->PostFtMessage(ack);
                }

                this->stat_->AppendRecvDataSize(msg->Size());
            });
            rtc_conn_->Start();
        }

        if (rtc_local_conn_) {
            // media messages are handled by the generic media_conn_ callback above
            rtc_local_conn_->SetOnFtMessageCallback([=, this](std::shared_ptr<Data> msg) {
                if (auto m = this->ParseMessage(msg); m) {
                    auto ack = ProtoMessageMaker::MakeAck(m->device_id(), m->stream_id(), m->send_time(), m->type());
                    rtc_local_conn_->PostFtMessage(ack);
                }

                this->stat_->AppendRecvDataSize(msg->Size());
            });
            rtc_local_conn_->SetOnRtcVideoFrameCallback([=, this](int w, int h, std::shared_ptr<Data> i420) {
                if (rtc_local_video_frame_cbk_) {
                    rtc_local_video_frame_cbk_(w, h, i420);
                }
            });
            rtc_local_conn_->SetOnAudioDataCallback([=, this](std::shared_ptr<Data> pcm, int sample_rate, int channels) {
                if (rtc_local_audio_cbk_) {
                    rtc_local_audio_cbk_(pcm, sample_rate, channels);
                }
            });
            rtc_local_conn_->SetOnVideoMessageCallback([=, this](std::shared_ptr<tc::Message> m) {
                // synthesized kVideoFrame from the encoded rtp tracks: dispatch exactly
                // like ParseMessage would, but WITHOUT an app-level ack - rtp carries
                // its own reliability(nack/pli), acking every frame would just flood
                // the media data channel
                this->stat_->AppendRecvDataSize((int64_t)m->ByteSizeLong());
                if (raw_msg_cbk_) {
                    raw_msg_cbk_(m);
                }
                if (video_frame_cbk_) {
                    video_frame_cbk_(m);
                }
            });
        }

        if (udp_direct_conn_) {
            // UDP 媒体面:组帧后合成的 kVideoFrame,与上面 rtc_local 相同的上送路径,
            // 同样不回 Ack(裸 UDP 无应用层确认,丢帧走 IDR 请求恢复)
            udp_direct_conn_->SetOnVideoMessageCallback([=, this](std::shared_ptr<tc::Message> m) {
                this->stat_->AppendRecvDataSize((int64_t)m->ByteSizeLong());
                if (raw_msg_cbk_) {
                    raw_msg_cbk_(m);
                }
                if (video_frame_cbk_) {
                    video_frame_cbk_(m);
                }
            });
            // UDP 音频:jitter buffer 按序交付/丢帧信号(空 data)都从这里上送,
            // 与 ws 路径一样直接进 audio_frame_cbk_(音频本就不走 raw_msg_cbk_)
            udp_direct_conn_->SetOnAudioMessageCallback([=, this](std::shared_ptr<tc::Message> m) {
                this->stat_->AppendRecvDataSize((int64_t)m->ByteSizeLong());
                if (audio_frame_cbk_) {
                    audio_frame_cbk_(m);
                }
            });
            // UDP 控制包踢人(kCtrlKick):复用"被接管"逻辑,与 kConnectionTakenOver 一致
            udp_direct_conn_->SetOnKickCallback([=, this](const std::string& reason) {
                LOGW("Udp direct connection kicked, reason: {}", reason);
                msg_notifier_->SendAppMessage(SdkMsgConnectionTakenOver{});
            });
            // UDP watchdog 断线:走与媒体连接相同的断线回调路径
            udp_direct_conn_->RegisterOnDisConnectedCallback([=, this]() {
                LOGW("Udp direct media channel lost.");
                if (dis_conn_cbk_) {
                    dis_conn_cbk_();
                }
            });
            udp_direct_conn_->Start(sdk_params_->ip_, sdk_params_->udp_port_, device_id_, stream_id_);
        }
    }

    void NetClient::Exit() {
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
        LOGI("WS has exited...");
    }

    std::shared_ptr<Message> NetClient::ParseMessage(std::shared_ptr<Data> msg) {
        auto net_msg = std::make_shared<tc::Message>();
        bool ok = net_msg->ParsePartialFromArray(msg->CStr(), msg->Size());
        if (!ok) {
            LOGE("Sdk ParseMessage failed.");
            return nullptr;
        }

        if (raw_msg_cbk_) {
            raw_msg_cbk_(net_msg);
        }

        if (net_msg->type() == tc::kVideoFrame) {
            if (network_type_ == ClientNetworkType::kUdpDirect) {
                // udp_direct 模式下视频走 UDP 媒体面,ws 控制面不应携带;
                // 收到说明 render 未按 udp_media=1 过滤,直接丢弃防重复解码
                return net_msg;
            }
            {
#if 0           //save file
                tc::VideoFrame frame = net_msg->video_frame();
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
        else if (net_msg->type() == tc::kAudioFrame) {
            if (network_type_ == ClientNetworkType::kUdpDirect) {
                // udp_direct 模式下音频走 UDP 媒体面,ws 控制面不应携带;
                // 收到说明 render 未按 udp_media=1 过滤,直接丢弃防重复解码
                return net_msg;
            }
            if (audio_frame_cbk_) {
                audio_frame_cbk_(net_msg);
            }
        }
        else if (net_msg->type() == tc::kCursorInfoSync) {
            if(cursor_info_sync_cbk_) {
                cursor_info_sync_cbk_(net_msg);
            }
        }
        else if (net_msg->type() == tc::kRendererAudioSpectrum) {
            if (audio_spectrum_cbk_) {
                audio_spectrum_cbk_(net_msg);
            }
        }
        else if (net_msg->type() == tc::kOnHeartBeat) {
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
        else if (net_msg->type() == tc::kClipboardInfo) {
            if (clipboard_cbk_) {
                clipboard_cbk_(net_msg);
            }
        }
        else if (net_msg->type() == tc::kServerConfiguration) {
            if (config_cbk_) {
                config_cbk_(net_msg);
            }
        }
        else if (net_msg->type() == tc::kMonitorSwitched) {
            if (monitor_switched_cbk_) {
                monitor_switched_cbk_(net_msg);
            }
        }
        else if (net_msg->type() == tc::kConnectionTakenOver) {
            // render 主动断开:被其它客户端接管
            msg_notifier_->SendAppMessage(SdkMsgConnectionTakenOver{});
        }
        else if (net_msg->type() == tc::kChangeMonitorResolutionResult) {
            auto sub = net_msg->change_monitor_resolution_result();
            msg_notifier_->SendAppMessage(SdkMsgChangeMonitorResolutionResult {
                .monitor_name_ = sub.monitor_name(),
                .result = sub.result(),
            });
        }
        else if (net_msg->type() == tc::kSigAnswerSdpMessage) {
            auto sub = net_msg->sig_answer_sdp();
            msg_notifier_->SendAppMessage(SdkMsgRemoteAnswerSdp {
                .answer_sdp_ = sub,
            });
        }
        else if (net_msg->type() == tc::kSigIceMessage) {
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
            while (queuing_msg_count >= kMaxQueuingFtMessages || !has_enough_buffer) {
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
            while (queuing_msg_count >= kMaxQueuingFtMessages || !has_enough_buffer) {
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
            while (queuing_msg_count >= kMaxQueuingFtMessages && wait_count < 200) {
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

    void NetClient::PostFileTransferMessage(std::shared_ptr<Data> msg) {
        if (sdk_params_->enable_p2p_ && rtc_conn_ && rtc_conn_->IsFtChannelReady()) {
            auto queuing_msg_count = rtc_conn_->GetQueuingFtMsgCount();
            auto has_enough_buffer = rtc_conn_->HasEnoughBufferForQueuingFtMessages();
            int wait_count = 0;
            while (queuing_msg_count >= kMaxQueuingFtMessages || !has_enough_buffer) {
                if (!rtc_conn_->IsFtChannelReady()) {
                    return;
                }
                TimeUtil::DelayByCount(1);
                queuing_msg_count = rtc_conn_->GetQueuingFtMsgCount();
                has_enough_buffer = rtc_conn_->HasEnoughBufferForQueuingFtMessages();
                wait_count++;
            }
            if (wait_count > 0) {
                LOGI("===> [RTC File] wait for {}ms", wait_count);
            }

            rtc_conn_->PostFtMessage(msg);
        }
        else if (rtc_local_conn_ && rtc_local_conn_->IsFtChannelReady()) {
            auto queuing_msg_count = rtc_local_conn_->GetQueuingFtMsgCount();
            auto has_enough_buffer = rtc_local_conn_->HasEnoughBufferForQueuingFtMessages();
            int wait_count = 0;
            while (queuing_msg_count >= kMaxQueuingFtMessages || !has_enough_buffer) {
                if (!rtc_local_conn_->IsFtChannelReady()) {
                    return;
                }
                TimeUtil::DelayByCount(1);
                queuing_msg_count = rtc_local_conn_->GetQueuingFtMsgCount();
                has_enough_buffer = rtc_local_conn_->HasEnoughBufferForQueuingFtMessages();
                wait_count++;
            }
            if (wait_count > 0) {
                LOGI("===> [RTC Local File] wait for {}ms", wait_count);
            }

            rtc_local_conn_->PostFtMessage(msg);
        }
        else {
            // TODO:
            auto queuing_msg_count = this->GetQueuingFtMsgCount();
            int wait_count = 0;
            while (queuing_msg_count >= kMaxQueuingFtMessages && wait_count < 2000) {
                if (!ft_conn_ || !ft_conn_->IsAlive()) {
                    LOGW("===> [WS File] connection not alive, drop the message, queuing: {}", queuing_msg_count);
                    return;
                }
                //LOGI("===> queue too many msgs, count: {}, wait for 1ms", queuing_msg_count);
                TimeUtil::DelayBySleep(1);
                queuing_msg_count = this->GetQueuingFtMsgCount();
                wait_count++;
            }
            if (wait_count >= 2000) {
                LOGW("===> [WS File] wait timeout after {}ms, drop the message, queuing: {}", wait_count, queuing_msg_count);
                return;
            }
            if (wait_count > 0) {
                LOGI("===> [WS File] wait for {}ms", wait_count);
            }
            if (ft_conn_) {
                ft_conn_->PostBinaryMessage(msg);
            }
        }

        stat_->AppendSentDataSize(msg->Size());
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

    void NetClient::SetOnHeartBeatCallback(tc::OnHeartBeatInfoCallback&& cbk) {
        hb_cbk_ = std::move(cbk);
    }

    void NetClient::SetOnClipboardCallback(OnClipboardInfoCallback&& cbk) {
        clipboard_cbk_ = std::move(cbk);
    }

    void NetClient::SetOnServerConfigurationCallback(tc::OnConfigCallback&& cbk) {
        config_cbk_ = std::move(cbk);
    }

    void NetClient::SetOnMonitorSwitchedCallback(OnMonitorSwitchedCallback&& cbk) {
        monitor_switched_cbk_ = std::move(cbk);
    }

    void NetClient::SetOnRawMessageCallback(tc::OnRawMessageCallback&& cbk) {
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
        msg->set_type(tc::kHeartBeat);
        msg->set_device_id(device_id_);
        msg->set_stream_id(stream_id_);
        auto hb = msg->mutable_heartbeat();
        hb->set_index(hb_idx_++);
        hb->set_timestamp((int64_t)TimeUtil::GetCurrentTimestamp());
        auto proto_msg = msg->SerializeAsString();
        if (auto buffer = tc::ProtoAsData(msg); buffer) {
            this->PostMediaMessage(buffer);
            this->PostFileTransferMessage(buffer);
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
}