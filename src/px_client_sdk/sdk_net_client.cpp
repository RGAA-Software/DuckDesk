//
// Created by RGAA on 2023-12-27.
//

#include "sdk_net_client.h"
#include "px_common/uuid.h"

#include <string_view>
#include <chrono>
#include <utility>
#include "px_common/log.h"
#include "px_common/data.h"
#include "px_common/thread.h"
#include "px_common/file.h"
#include "px_common/message_notifier.h"
#include "px_common/ws_control_signal.h"
#include "px_common/reliable_websocket_send.h"
#include "px_rdp/rdp_stream_packet.h"
#include "sdk_messages.h"
#include "connection/ws_connection.h"
#include "connection/wss_connection.h"
#include "connection/udp_direct_connection.h"
#include "connection/relay_connection.h"
#include "px_common/time_util.h"
#include "px_common/url_helper.h"
#include "sdk_statistics.h"
#include "px_message/proto_converter.h"
#include "px_message/proto_message_maker.h"
#include <asio2/websocket/ws_client.hpp>
#include <asio2/asio2.hpp>

namespace px {

NetClient::NetClient(SdkConnectionParams params, const std::shared_ptr<MessageNotifier>& notifier)
    : params_(std::move(params)), udp_media_association_(params_.udp_media_association_.empty() ? GetUUID() : params_.udp_media_association_),
      msg_notifier_(notifier), stat_(SdkStatistics::Instance()) {
    stat_->media_transport_.store(params_.media_transport_);
}

NetClient::~NetClient() {
    Exit();
}

std::string NetClient::MakeAuthenticatedWebSocketPath(std::string path, const bool file_only) const {
    if (params_.connection_ticket_.empty()) {
        if (!params_.connection_nonce_.empty() && params_.stream_id_.starts_with("ip-direct:")) {
            path += path.find('?') == std::string::npos ? "?client_nonce=" : "&client_nonce=";
            path += UrlHelper::EncodeQueryComponent(params_.connection_nonce_);
        }
        return path;
    }
    path += path.find('?') == std::string::npos ? "?ticket=" : "&ticket=";
    path +=
        UrlHelper::EncodeQueryComponent(params_.connection_ticket_) + "&client_nonce=" + UrlHelper::EncodeQueryComponent(params_.connection_nonce_);
    if (!params_.connection_instance_id_.empty()) {
        path += "&instance_id=" + UrlHelper::EncodeQueryComponent(params_.connection_instance_id_);
    }
    if (file_only) {
        path += "&file_only=1";
    }
    return path;
}

std::shared_ptr<Connection> NetClient::MakeDirectWebSocketMediaConnection() const {
    std::string path = params_.media_path_;
    if (params_.media_transport_ == SdkMediaTransport::kWebSocket) {
        // The explicit transport selection wins over a caller's cached UDP query parameters.
        const auto query_start = path.find('?');
        if (query_start != std::string::npos) {
            std::string cleaned = path.substr(0, query_start);
            std::string_view remaining(path);
            remaining.remove_prefix(query_start + 1);
            bool first{true};
            while (!remaining.empty()) {
                const auto separator = remaining.find('&');
                const auto field = remaining.substr(0, separator);
                const auto key = field.substr(0, field.find('='));
                if (!field.empty() && key != "udp_media" && key != "udp_media_association") {
                    cleaned += first ? '?' : '&';
                    cleaned += field;
                    first = false;
                }
                if (separator == std::string_view::npos) {
                    break;
                }
                remaining.remove_prefix(separator + 1);
            }
            path = std::move(cleaned);
        }
    }
    if (params_.session_mode_ == SdkSessionMode::kRdp) {
        path += path.find('?') == std::string::npos ? "?rdp=1" : "&rdp=1";
        path = MakeAuthenticatedWebSocketPath(std::move(path));
        if (params_.ssl_) {
            return std::make_shared<WssConnection>(msg_notifier_, params_.ip_, params_.port_, path);
        }
        return std::make_shared<WsConnection>(msg_notifier_, params_.ip_, params_.port_, path);
    }
    if (params_.media_transport_ == SdkMediaTransport::kUdp) {
        constexpr std::string_view kUdpMediaQuery = "&udp_media=1";
        if (path.find("udp_media=1") == std::string::npos) {
            path += path.find('?') == std::string::npos ? "?udp_media=1" : std::string(kUdpMediaQuery);
        }
        if (!udp_media_association_.empty()) {
            path += "&udp_media_association=" + UrlHelper::EncodeQueryComponent(udp_media_association_);
        }
    }
    path = MakeAuthenticatedWebSocketPath(std::move(path));
    if (params_.ssl_) {
        return std::make_shared<WssConnection>(msg_notifier_, params_.ip_, params_.port_, path);
    }
    return std::make_shared<WsConnection>(msg_notifier_, params_.ip_, params_.port_, path);
}

bool NetClient::IsCurrentManagedMediaConnection(uint64_t generation) const {
    return !exited_.load() && managed_media_generation_.load() == generation;
}

std::shared_ptr<Connection> NetClient::CurrentMediaConnection() const {
    std::lock_guard lock(media_connection_mutex_);
    return media_conn_;
}

void NetClient::ReplaceMediaConnection(std::shared_ptr<Connection> connection) {
    std::lock_guard lock(media_connection_mutex_);
    media_conn_ = std::move(connection);
}

std::shared_ptr<UdpDirectConnection> NetClient::CurrentUdpDirectConnection() const {
    std::lock_guard lock(udp_direct_connection_mutex_);
    return udp_direct_conn_;
}

void NetClient::ReplaceUdpDirectConnection(std::shared_ptr<UdpDirectConnection> connection) {
    std::lock_guard lock(udp_direct_connection_mutex_);
    udp_direct_conn_ = std::move(connection);
}

void NetClient::StartManagedUdpMediaConnection(const std::shared_ptr<Connection>& connection, uint64_t generation) {
    const auto weak_self = weak_from_this();
    const std::weak_ptr<Connection> weak_connection = connection;
    connection->RegisterOnConnectedCallback([weak_self, generation]() {
        const auto self = weak_self.lock();
        if (!self || !self->IsCurrentManagedMediaConnection(generation))
            return;
        if (self->connection_notified_.exchange(true))
            return;
        if (self->conn_cbk_)
            self->conn_cbk_();
    });
    connection->RegisterOnDisConnectedCallback([weak_self, generation]() {
        const auto self = weak_self.lock();
        if (!self || !self->IsCurrentManagedMediaConnection(generation))
            return;
        // 已认证 WS 是控制/文件会话的生命期边界，UDP 故障不改变它。
        // generation 只过滤被后续启动或退出替换掉的旧回调。
        if (self->dis_conn_cbk_)
            self->dis_conn_cbk_();
    });
    connection->RegisterOnMessageCallback([weak_self, weak_connection, generation](std::shared_ptr<Data> data) {
        const auto self = weak_self.lock();
        if (!self || !self->IsCurrentManagedMediaConnection(generation))
            return;
        self->stat_->AppendRecvDataSize(data->Size());
        if (self->params_.session_mode_ == SdkSessionMode::kRdp) {
            px::Message envelope{};
            if (data->Size() > rdp::kMaxWireBytes || !envelope.ParseFromArray(data->Bytes().data(), static_cast<int>(data->Size())) ||
                (envelope.type() != kRdpStream && envelope.type() != kOnHeartBeat && envelope.type() != kInstanceStopped) ||
                (envelope.type() == kRdpStream && !envelope.has_rdp_stream())) {
                if (const auto active_connection = weak_connection.lock()) {
                    active_connection->Stop();
                }
                return;
            }
            if (envelope.type() == kRdpStream && envelope.has_rdp_stream()) {
                auto callback = std::function<void(std::shared_ptr<Data>)>{};
                {
                    std::lock_guard lock(self->rdp_callback_mutex_);
                    callback = self->rdp_message_callback_;
                }
                if (callback) {
                    callback(std::move(data));
                }
                return; // RDP owns its framing; do not generate one GammaRay ACK per chunk.
            }
            static_cast<void>(self->ParseMessage(std::move(data)));
            return; // No native UDP or host file/clipboard channel in RDP mode.
        }
        if (auto message = self->ParseMessage(data); message) {
            self->StartFileTransferConnection();
            // A transport-level WS connected callback can run even when
            // Render subsequently rejects ticket redemption. Receiving a
            // valid routed application message proves that Render accepted
            // this media session and registered its UDP association.
            if (self->udp_media_state_.AcceptsMedia()) {
                self->StartUdpDirectMedia();
            }
            if (const auto active_connection = weak_connection.lock()) {
                active_connection->PostBinaryMessage(
                    ProtoMessageMaker::MakeAck(message->device_id(), message->stream_id(), message->send_time(), message->type()));
            }
        }
    });
    connection->Start();
}

void NetClient::StartUdpDirectMedia() {
    if (!params_.enable_video_ && !params_.enable_audio_)
        return;
    bool expected = false;
    if (!udp_direct_started_.compare_exchange_strong(expected, true)) {
        return;
    }
    const auto udp_connection = CurrentUdpDirectConnection();
    if (!udp_connection || exited_) {
        udp_direct_started_ = false;
        return;
    }
    udp_media_probe_deadline_ms_ = TimeUtil::GetCurrentTimestamp() + kUdpMediaProbeTimeoutMs;
    LOGI("Authenticated WS control ready; start associated UDP media.");
    udp_connection->Start(params_.ip_, params_.udp_port_, params_.stream_id_, udp_media_association_);
}

void NetClient::StartFileTransferConnection() {
    bool expected = false;
    if (!file_transfer_started_.compare_exchange_strong(expected, true)) {
        return;
    }
    const auto connection = ft_conn_;
    if (!connection || exited_) {
        file_transfer_started_ = false;
        return;
    }
    connection->Start();
}

void NetClient::OnUdpMediaReady() {
    if (udp_media_state_.MarkReady()) {
        udp_media_probe_deadline_ms_ = 0;
        LOGI("Udp direct first media received; keep UDP media transport.");
    }
}

void NetClient::CheckUdpMediaProbeTimeout() {
    if (exited_)
        return;
    const auto deadline = udp_media_probe_deadline_ms_.load();
    if (deadline <= 0 || TimeUtil::GetCurrentTimestamp() < deadline)
        return;
    ReportUdpMediaUnavailable();
}

void NetClient::ReportUdpMediaUnavailable() {
    if (exited_)
        return;
    const auto failure = udp_media_state_.MarkUnavailable();
    if (!failure)
        return;
    udp_media_probe_deadline_ms_ = 0;
    LOGW("UDP media unavailable (reason={}); keep the authenticated control/file channel, without media fallback.", static_cast<int>(*failure));
    msg_notifier_->SendAppMessage(SdkMsgUdpMediaUnavailable{.reason = *failure});
}

void NetClient::Start() {
    if (params_.session_mode_ == SdkSessionMode::kRdp && params_.file_transfer_only_) {
        LOGE("RDP cannot use the host file-transfer connection.");
        return;
    }
    if (exited_ || started_.exchange(true))
        return;
    if (params_.route_ == SdkConnectionRoute::kWebSocketRelay &&
        (params_.session_mode_ != SdkSessionMode::kNative || params_.file_transfer_only_ ||
         params_.media_transport_ != SdkMediaTransport::kWebSocket ||
         params_.relay_host_.empty() || params_.relay_port_ <= 0 || params_.relay_device_id_.empty() || params_.relay_remote_device_id_.empty() ||
         params_.relay_ticket_device_id_.empty() || params_.connection_ticket_.empty() || params_.connection_nonce_.empty())) {
        LOGE("Relay connection parameters are incomplete or incompatible with this session.");
        return;
    }
    if (params_.media_transport_ == SdkMediaTransport::kUdp && params_.session_mode_ != SdkSessionMode::kRdp && !params_.file_transfer_only_ &&
        !udp_media_state_.BeginProbe())
        return;
    const auto weak_self = weak_from_this();
    connection_notified_ = false;
    if (!msg_listener_) {
        msg_listener_ = msg_notifier_->CreateListener(MessageExecutionLane::kControl);
        msg_listener_->Listen<SdkMsgTimer1000>([weak_self](const auto&) {
            if (const auto self = weak_self.lock()) {
                self->HeartBeat();
            }
        });
    }
    // GameStream 风格双通道:ws 控制面(可靠消息/状态机全复用) + 裸 UDP 媒体面,
    // 见 docs/udp_gamestream_channel_plan.md
    LOGI("Start native connection mode={}, route={}, control={}:{}", static_cast<int>(params_.session_mode_), static_cast<int>(params_.route_),
         params_.ip_, params_.port_);
    LOGI("Native media transport: {}", params_.media_transport_ == SdkMediaTransport::kWebSocket ? "WebSocket/TCP" : "UDP/FEC");
    // Reliable control and file-transfer messages share the already
    // authenticated /media WebSocket. UDP carries audio/video only.
    // Opening another route would redeem the one-time ticket again and
    // later reconnects would be rejected after the ticket expires.
    if (!params_.file_transfer_only_) {
        ReplaceMediaConnection(params_.route_ == SdkConnectionRoute::kWebSocketRelay
                                   ? std::static_pointer_cast<Connection>(std::make_shared<RelayConnection>(params_, msg_notifier_))
                                   : MakeDirectWebSocketMediaConnection());
    } else {
        const auto ft_path = MakeAuthenticatedWebSocketPath(params_.ft_path_, true);
        if (params_.ssl_) {
            ft_conn_ = std::make_shared<WssConnection>(msg_notifier_, params_.ip_, params_.port_, ft_path);
        } else {
            ft_conn_ = std::make_shared<WsConnection>(msg_notifier_, params_.ip_, params_.port_, ft_path);
        }
    }
    if (params_.route_ == SdkConnectionRoute::kDirect && params_.media_transport_ == SdkMediaTransport::kUdp && !params_.file_transfer_only_ &&
        params_.session_mode_ != SdkSessionMode::kRdp) {
        ReplaceUdpDirectConnection(std::make_shared<UdpDirectConnection>(msg_notifier_));
    }

    const auto media_connection = CurrentMediaConnection();
    // Standalone file sessions use a single authenticated WebSocket.
    if (ft_conn_) {
        ft_conn_->RegisterOnMessageCallback([weak_self](std::shared_ptr<Data> data) {
            const auto self = weak_self.lock();
            if (!self || self->exited_)
                return;
            self->stat_->AppendRecvDataSize(data->Size());
            if (auto m = self->ParseMessage(data); m) {
                auto ack = ProtoMessageMaker::MakeAck(m->device_id(), m->stream_id(), m->send_time(), m->type());
                if (self->ft_conn_)
                    self->ft_conn_->PostBinaryMessage(ack);
            }
        });
    }

    if (ft_conn_) {
        ft_conn_->RegisterOnConnectedCallback([weak_self]() {
            if (const auto self = weak_self.lock(); self && !self->exited_ && self->conn_cbk_) {
                self->connection_notified_ = true;
                self->conn_cbk_();
            }
        });
        ft_conn_->RegisterOnDisConnectedCallback([weak_self]() {
            if (const auto self = weak_self.lock(); self && !self->exited_ && self->dis_conn_cbk_) {
                self->dis_conn_cbk_();
            }
        });
        StartFileTransferConnection();
    }

    if (const auto udp_connection = CurrentUdpDirectConnection()) {
        // UDP 媒体面:组帧后合成的 kVideoFrame,交给 SDK 解码,
        // 同样不回 Ack(裸 UDP 无应用层确认,丢帧走 IDR 请求恢复)
        udp_connection->SetOnVideoMessageCallback([weak_self](std::shared_ptr<px::Message> m) {
            const auto self = weak_self.lock();
            if (!self)
                return;
            if (!self->udp_media_state_.AcceptsMedia())
                return;
            self->OnUdpMediaReady();
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
        udp_connection->SetOnAudioMessageCallback([weak_self](std::shared_ptr<px::Message> m) {
            const auto self = weak_self.lock();
            if (!self)
                return;
            if (!self->udp_media_state_.AcceptsMedia())
                return;
            self->OnUdpMediaReady();
            self->stat_->AppendRecvDataSize((int64_t)m->ByteSizeLong());
            if (self->audio_frame_cbk_) {
                self->audio_frame_cbk_(m);
            }
        });
        udp_connection->SetOnVoiceFrameCallback([weak_self](UdpVoiceFrame frame) {
            const auto self = weak_self.lock();
            if (!self || self->exited_.load() || !self->udp_media_state_.AcceptsMedia()) {
                return;
            }
            const auto message = std::make_shared<Message>();
            message->set_type(kVoiceAudioFrame);
            message->set_device_id(self->params_.device_id_);
            message->set_stream_id(self->params_.stream_id_);
            auto& voice = *message->mutable_voice_audio_frame();
            voice.set_call_id(std::move(frame.call_id));
            voice.set_sequence(frame.sequence);
            voice.set_capture_time_ms(frame.capture_time_ms);
            voice.set_opus(std::string(frame.opus.begin(), frame.opus.end()));
            self->OnUdpMediaReady();
            self->stat_->AppendRecvDataSize(static_cast<std::int64_t>(message->ByteSizeLong()));
            if (self->raw_msg_cbk_) {
                self->raw_msg_cbk_(message);
            }
        });
        // UDP 控制包踢人(kCtrlKick):复用"被接管"逻辑,与 kConnectionTakenOver 一致
        udp_connection->SetOnKickCallback([weak_self](const std::string& reason) {
            LOGW("Udp direct connection kicked, reason: {}", reason);
            if (const auto self = weak_self.lock()) {
                self->msg_notifier_->SendAppMessage(SdkMsgConnectionTakenOver{});
            }
        });
        udp_connection->SetOnMediaReadyCallback([weak_self]() {
            if (const auto self = weak_self.lock(); self && self->udp_media_state_.AcceptsMedia()) {
                self->OnUdpMediaReady();
            }
        });
        // A media watchdog failure must not disconnect the reliable session.
        udp_connection->RegisterOnDisConnectedCallback([weak_self]() {
            if (const auto self = weak_self.lock()) {
                LOGW("Udp direct media channel lost; report media unavailability.");
                self->ReportUdpMediaUnavailable();
            }
        });
    }
    if (media_connection) {
        // Configure every UDP callback before an accepted WS application
        // message can prove the association and start the media socket.
        StartManagedUdpMediaConnection(media_connection, managed_media_generation_.fetch_add(1) + 1);
    }
}

void NetClient::Exit() {
    if (exited_.exchange(true)) {
        return;
    }
    msg_listener_.reset();
    udp_media_state_.Stop();
    udp_media_probe_deadline_ms_ = 0;
    managed_media_generation_.fetch_add(1);
    if (const auto media_connection = CurrentMediaConnection()) {
        LOGI("Queued message count: {}", queuing_message_count_.load());
        media_connection->Stop();
    }
    if (ft_conn_) {
        ft_conn_->Stop();
    }
    if (const auto udp_connection = CurrentUdpDirectConnection()) {
        udp_connection->Stop();
    }
    ReplaceUdpDirectConnection(nullptr);
    ReplaceMediaConnection(nullptr);
    ft_conn_.reset();
    LOGI("WS has exited...");
}

std::shared_ptr<Message> NetClient::ParseMessage(std::shared_ptr<Data> msg) {
    auto net_msg = std::make_shared<px::Message>();
    bool ok = net_msg->ParsePartialFromArray(msg->Bytes().data(), msg->Size());
    if (!ok) {
        LOGE("Sdk ParseMessage failed.");
        return nullptr;
    }

    if (net_msg->type() == px::kVideoFrame || net_msg->type() == px::kAudioFrame || net_msg->type() == px::kVoiceAudioFrame) {
        if (params_.media_transport_ != SdkMediaTransport::kWebSocket || params_.file_transfer_only_ ||
            params_.session_mode_ != SdkSessionMode::kNative) {
            return net_msg;
        }
        if (net_msg->type() == kVideoFrame && params_.enable_video_) {
            const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            auto& window = tcp_receive_window_;
            if (window.start_ms == 0) {
                window.start_ms = now;
                LOGI("TCP media video delivered over WebSocket.");
            }
            if (window.last_frame_ms != 0) {
                const auto gap = now - window.last_frame_ms;
                window.max_gap_ms = std::max(window.max_gap_ms, gap);
                window.stalls += gap > 100 ? 1 : 0;
            }
            window.last_frame_ms = now;
            ++window.frames;
            if (now - window.start_ms >= 5000) {
                LOGI("TCP media window: frames={}, fps={:.1f}, max_gap_ms={}, gaps_gt_100ms={}", window.frames,
                     1000.0 * window.frames / (now - window.start_ms), window.max_gap_ms, window.stalls);
                window.start_ms = now;
                window.frames = 0;
                window.max_gap_ms = 0;
                window.stalls = 0;
            }
            if (raw_msg_cbk_) {
                raw_msg_cbk_(net_msg);
            }
            if (!exited_.load() && video_frame_cbk_) {
                video_frame_cbk_(net_msg);
            }
        } else if (net_msg->type() == kAudioFrame && params_.enable_audio_ && audio_frame_cbk_) {
            if (!tcp_receive_window_.audio_seen) {
                tcp_receive_window_.audio_seen = true;
                LOGI("TCP media audio delivered over WebSocket.");
            }
            audio_frame_cbk_(net_msg);
        } else if (net_msg->type() == kVoiceAudioFrame && raw_msg_cbk_) {
            raw_msg_cbk_(net_msg);
        }
        return net_msg;
    }

    if (raw_msg_cbk_) {
        raw_msg_cbk_(net_msg);
    }

    if (net_msg->type() == px::kCursorInfoSync) {
        if (cursor_info_sync_cbk_) {
            cursor_info_sync_cbk_(net_msg);
        }
    } else if (net_msg->type() == px::kRendererAudioSpectrum) {
        if (audio_spectrum_cbk_) {
            audio_spectrum_cbk_(net_msg);
        }
    } else if (net_msg->type() == px::kOnHeartBeat) {
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
    } else if (net_msg->type() == px::kClipboardInfo) {
        if (clipboard_cbk_) {
            clipboard_cbk_(net_msg);
        }
    } else if (net_msg->type() == px::kServerConfiguration) {
        if (config_cbk_) {
            config_cbk_(net_msg);
        }
    } else if (net_msg->type() == px::kMonitorSwitched) {
        if (monitor_switched_cbk_) {
            monitor_switched_cbk_(net_msg);
        }
    } else if (net_msg->type() == px::kConnectionTakenOver) {
        // render 主动断开:被其它客户端接管
        msg_notifier_->SendAppMessage(SdkMsgConnectionTakenOver{});
    } else if (net_msg->type() == px::kChangeMonitorResolutionResult) {
        auto sub = net_msg->change_monitor_resolution_result();
        msg_notifier_->SendAppMessage(SdkMsgChangeMonitorResolutionResult{
            .monitor_name_ = sub.monitor_name(),
            .result = sub.result(),
        });
    }
    return net_msg;
}

bool NetClient::PostVoiceAudioMessage(const std::shared_ptr<Message>& message) {
    if (exited_.load() || !message || message->type() != kVoiceAudioFrame || !message->has_voice_audio_frame() ||
        message->device_id() != params_.device_id_ || message->stream_id() != params_.stream_id_ || params_.file_transfer_only_ ||
        params_.session_mode_ != SdkSessionMode::kNative) {
        return false;
    }
    const auto& frame = message->voice_audio_frame();
    if (frame.opus().empty() || frame.opus().size() > UdpVoiceProtocol::kMaxOpusBytes) {
        return false;
    }
    if (params_.media_transport_ == SdkMediaTransport::kWebSocket) {
        const auto connection = CurrentMediaConnection();
        if (!connection || !connection->IsAlive() || connection->GetQueuingMsgCount() >= kMaxFileTransferQueuedMessages) {
            return false;
        }
        connection->PostBinaryMessage(Data::From(message->SerializeAsString()));
        return true;
    }
    const auto connection = CurrentUdpDirectConnection();
    if (!connection || !udp_media_state_.AcceptsMedia()) {
        return false;
    }
    const std::vector<std::uint8_t> opus(frame.opus().begin(), frame.opus().end());
    return connection->PostVoiceFrame(frame.call_id(), frame.sequence(), frame.capture_time_ms(), opus);
}

void NetClient::SetOnRdpMessageCallback(std::function<void(std::shared_ptr<Data>)> callback) {
    std::lock_guard lock(rdp_callback_mutex_);
    rdp_message_callback_ = std::move(callback);
}

void NetClient::PostRdpMessage(std::shared_ptr<Data> msg, std::function<void(bool)> completion) {
    const auto pending = std::make_shared<ReliableWriteCompletion>(std::move(completion));
    if (exited_.load() || params_.session_mode_ != SdkSessionMode::kRdp || !msg || msg->Size() == 0 || msg->Size() > rdp::kMaxWireBytes) {
        return;
    }
    Message envelope{};
    if (!envelope.ParseFromArray(msg->Bytes().data(), static_cast<int>(msg->Size())) || envelope.type() != kRdpStream || !envelope.has_rdp_stream()) {
        return;
    }
    if (const auto connection = CurrentMediaConnection()) {
        connection->PostReliableBinaryMessage(std::move(msg), [pending](bool success) { pending->Complete(success); });
    }
}

void NetClient::PostMediaMessage(std::shared_ptr<Data> msg) {
    {
        const auto media_connection = CurrentMediaConnection();
        auto queuing_msg_count = media_connection ? media_connection->GetQueuingMsgCount() : 0;
        int wait_count = 0;
        while (queuing_msg_count >= kMaxFileTransferQueuedMessages && wait_count < 200) {
            if (!media_connection || !media_connection->IsAlive()) {
                LOGW("===> [Media] connection not alive, drop the message, queuing: {}", queuing_msg_count);
                return;
            }
            // LOGI("===> queue too many msgs, count: {}, wait for 1ms", queuing_msg_count);
            TimeUtil::DelayBySleep(1);
            queuing_msg_count = media_connection->GetQueuingMsgCount();
            wait_count++;
        }
        if (wait_count >= 200) {
            LOGW("===> [Media] wait timeout after {}ms, drop the message, queuing: {}", wait_count, queuing_msg_count);
            return;
        }

        if (media_connection) {
            media_connection->PostBinaryMessage(msg);
        }
    }

    stat_->AppendSentDataSize(msg->Size());
}

bool NetClient::PostReliableControlMessage(std::shared_ptr<Data> msg) {
    if (exited_.load() || params_.session_mode_ == SdkSessionMode::kRdp || !msg || msg->Size() == 0 || msg->Size() > 32768) return false;
    Message envelope{};
    if (!envelope.ParseFromArray(msg->Bytes().data(), static_cast<int>(msg->Size())) ||
        (envelope.type() != kApplicationTextCapabilities && envelope.type() != kApplicationTextSubmit &&
         envelope.type() != kApplicationTextBarrier)) return false;
    const auto connection = CurrentMediaConnection();
    if (!connection || !connection->IsAlive() || connection->GetQueuingMsgCount() >= kMaxFileTransferQueuedMessages) return false;
    // The managed media connection is the reliable WS/WSS control connection;
    // UDP video/audio has a separate owner. A server result, not this enqueue,
    // determines success. Lost writes time out without an automatic replay.
    connection->PostReliableBinaryMessage(std::move(msg), [](bool) {});
    return true;
}

FileTransferSendResult NetClient::PostFileTransferMessage(std::shared_ptr<Data> msg) {
    if (params_.session_mode_ == SdkSessionMode::kRdp) {
        return FileTransferSendResult::Disconnected("Host file transfer is unavailable in RDP mode");
    }
    if (!msg) {
        return FileTransferSendResult::TransportError("file-transfer message is empty");
    }

    {

        const auto file_connection = params_.file_transfer_only_ ? ft_conn_ : CurrentMediaConnection();
        if (!file_connection || !file_connection->IsAlive()) {
            return FileTransferSendResult::Disconnected("file-transfer connection is not alive");
        }
        if (file_connection->GetQueuingMsgCount() >= kMaxFileTransferQueuedMessages) {
            const auto signal = file_connection->AcquireFileTransferWritableSignal();
            if (file_connection->GetQueuingMsgCount() <= kFileTransferQueueLowWatermark) {
                signal->NotifyWritable();
            }
            return FileTransferSendResult::Busy("file-transfer connection queue is full", signal);
        }
        file_connection->PostBinaryMessage(msg);
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

void NetClient::HeartBeat() {
    CheckUdpMediaProbeTimeout();
    auto msg = std::make_shared<Message>();
    msg->set_type(px::kHeartBeat);
    msg->set_device_id(params_.device_id_);
    msg->set_stream_id(params_.stream_id_);
    auto& hb = *msg->mutable_heartbeat();
    hb.set_index(hb_idx_++);
    hb.set_timestamp((int64_t)TimeUtil::GetCurrentTimestamp());
    if (auto buffer = px::ProtoAsData(msg); buffer) {
        this->PostMediaMessage(buffer);
        if (params_.file_transfer_only_) {
            static_cast<void>(this->PostFileTransferMessage(buffer));
        }
    }
}

int64_t NetClient::GetQueuingMediaMsgCount() {
    if (const auto media_connection = CurrentMediaConnection()) {
        return media_connection->GetQueuingMsgCount();
    } else {
        return 0;
    }
}

int64_t NetClient::GetQueuingFtMsgCount() {
    if (!params_.file_transfer_only_) {
        if (const auto media_connection = CurrentMediaConnection()) {
            return media_connection->GetQueuingMsgCount();
        }
        return 0;
    } else if (ft_conn_) {
        return ft_conn_->GetQueuingMsgCount();
    } else {
        return 0;
    }
}

void NetClient::On16msTimeout() {
    if (ft_conn_) {
        ft_conn_->On16msTimeout();
    }
    if (const auto media_connection = CurrentMediaConnection()) {
        media_connection->On16msTimeout();
    }
}

} // namespace px
