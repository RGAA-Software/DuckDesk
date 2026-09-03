//
// Created by RGAA on 2023-12-27.
//

#include "sdk_net_client.h"
#include "px_common_new/uuid.h"

#include <string_view>
#include <utility>
#include <array>
#include "px_common_new/log.h"
#include "px_common_new/data.h"
#include "px_common_new/thread.h"
#include "px_common_new/file.h"
#include "px_common_new/message_notifier.h"
#include "px_common_new/ws_control_signal.h"
#include "sdk_messages.h"
#include "connection/udp_connection.h"
#include "connection/ws_connection.h"
#include "connection/wss_connection.h"
#include "connection/relay_connection.h"
#include "connection/webrtc_connection.h"
#include "connection/webrtc_local_connection.h"
#include "connection/udp_direct_connection.h"
#include "px_common_new/time_util.h"
#include "px_common_new/url_helper.h"
#include "sdk_statistics.h"
#include "px_message_new/proto_converter.h"
#include "px_message_new/proto_message_maker.h"
#include <asio2/websocket/ws_client.hpp>
#include <asio2/asio2.hpp>

namespace px
{

    namespace {

        std::string RedactTransportPath(std::string path) {
            constexpr std::array<std::string_view, 5> kSensitiveKeys{
                "ticket=", "client_nonce=", "direct_session_grant=",
                "safety_pwd_md5=", "udp_media_association=",
            };
            for (const auto key : kSensitiveKeys) {
                std::size_t value_begin = 0;
                while ((value_begin = path.find(key, value_begin)) != std::string::npos) {
                    value_begin += key.size();
                    const auto value_end = path.find('&', value_begin);
                    path.replace(value_begin,
                                 value_end == std::string::npos
                                     ? std::string::npos : value_end - value_begin,
                                 "<redacted>");
                    value_begin += std::string_view{"<redacted>"}.size();
                }
            }
            return path;
        }

    } // namespace

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
        if (nt_type == ClientNetworkType::kUdpDirect
            && this->sdk_params_->udp_media_association_.empty()) {
            this->sdk_params_->udp_media_association_ = GetUUID();
        }

    }

    NetClient::~NetClient() {
        Exit();
    }

    std::string NetClient::MakeAuthenticatedWebSocketPath(
        std::string path, const bool file_only) const {
        if (sdk_params_->connection_ticket_.empty()) {
            return path;
        }
        path += "&ticket=" + UrlHelper::EncodeQueryComponent(sdk_params_->connection_ticket_)
            + "&client_nonce=" + UrlHelper::EncodeQueryComponent(sdk_params_->connection_nonce_);
        if (!sdk_params_->connection_instance_id_.empty()) {
            path += "&instance_id="
                + UrlHelper::EncodeQueryComponent(sdk_params_->connection_instance_id_);
        }
        if (file_only) {
            path += "&file_only=1";
        }
        return path;
    }

    std::shared_ptr<Connection> NetClient::MakeDirectWebSocketMediaConnection(bool udp_media) const {
        std::string path = media_path_;
        constexpr std::string_view kUdpMediaQuery = "&udp_media=1";
        if (!udp_media) {
            const auto query = path.find(kUdpMediaQuery);
            if (query != std::string::npos) {
                path.erase(query, kUdpMediaQuery.size());
            }
        }
        else if (!sdk_params_->udp_media_association_.empty()) {
            path += "&udp_media_association="
                + UrlHelper::EncodeQueryComponent(sdk_params_->udp_media_association_);
        }
        path = MakeAuthenticatedWebSocketPath(std::move(path));
        if (sdk_params_->ssl_) {
            return std::make_shared<WssConnection>(sdk_params_, msg_notifier_, sdk_params_->ip_,
                                                   sdk_params_->port_, path);
        }
        return std::make_shared<WsConnection>(sdk_params_, msg_notifier_, sdk_params_->ip_,
                                              sdk_params_->port_, path);
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

    void NetClient::StartManagedUdpMediaConnection(const std::shared_ptr<Connection>& connection,
                                                    uint64_t generation) {
        const auto weak_self = weak_from_this();
        const std::weak_ptr<Connection> weak_connection = connection;
        connection->RegisterOnConnectedCallback([weak_self, generation]() {
            const auto self = weak_self.lock();
            if (!self || !self->IsCurrentManagedMediaConnection(generation)) return;
            if (self->connection_notified_.exchange(true)) return;
            if (self->conn_cbk_) self->conn_cbk_();
        });
        connection->RegisterOnDisConnectedCallback([weak_self, generation]() {
            const auto self = weak_self.lock();
            if (!self || !self->IsCurrentManagedMediaConnection(generation)) return;
            // UDP 回退复用这条已认证 WS，因此它仍是会话生命期边界。
            // generation 只过滤被后续启动或退出替换掉的旧回调。
            if (self->dis_conn_cbk_) self->dis_conn_cbk_();
        });
        connection->RegisterOnMessageCallback([weak_self, weak_connection, generation](std::shared_ptr<Data> data) {
            const auto self = weak_self.lock();
            if (!self || !self->IsCurrentManagedMediaConnection(generation)) return;
            self->stat_->AppendRecvDataSize(data->Size());
            if (auto message = self->ParseMessage(data); message) {
                self->StartFileTransferConnection();
                // A transport-level WS connected callback can run even when
                // Render subsequently rejects ticket redemption. Receiving a
                // valid routed application message proves that Render accepted
                // this media session and registered its UDP association.
                if (self->udp_media_fallback_state_.UsesUdpMedia()) {
                    self->StartUdpDirectMedia();
                }
                if (const auto active_connection = weak_connection.lock()) {
                    active_connection->PostBinaryMessage(ProtoMessageMaker::MakeAck(
                        message->device_id(), message->stream_id(), message->send_time(), message->type()));
                }
            }
        });
        connection->Start();
    }

    void NetClient::StartUdpDirectMedia() {
        bool expected = false;
        if (!udp_direct_started_.compare_exchange_strong(expected, true)) {
            return;
        }
        const auto udp_connection = CurrentUdpDirectConnection();
        if (!udp_connection || exited_) {
            udp_direct_started_ = false;
            return;
        }
        udp_media_probe_deadline_ms_ =
            TimeUtil::GetCurrentTimestamp() + kUdpMediaProbeTimeoutMs;
        LOGI("Authenticated WS control ready; start associated UDP media.");
        udp_connection->Start(sdk_params_->ip_, sdk_params_->udp_port_, stream_id_,
                              sdk_params_->udp_media_association_);
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
        if (udp_media_fallback_state_.MarkUdpMediaReady()) {
            udp_media_probe_deadline_ms_ = 0;
            LOGI("Udp direct first media received; keep UDP media transport.");
        }
    }

    void NetClient::CheckUdpMediaProbeTimeout() {
        if (network_type_ != ClientNetworkType::kUdpDirect || exited_) return;
        const auto deadline = udp_media_probe_deadline_ms_.load();
        if (deadline <= 0 || TimeUtil::GetCurrentTimestamp() < deadline) return;
        BeginUdpWebSocketFallback();
    }

    void NetClient::BeginUdpWebSocketFallback() {
        if (!udp_media_fallback_state_.BeginFallback()) return;
        udp_media_probe_deadline_ms_ = 0;
        LOGW("Udp direct media unavailable; enabling media on the authenticated WebSocket control channel.");

        const auto previous_udp = CurrentUdpDirectConnection();
        if (previous_udp) previous_udp->Stop();

        const auto control_connection = CurrentMediaConnection();
        if (!control_connection || !control_connection->IsAlive()) {
            LOGE("Cannot enable WebSocket media fallback: authenticated control channel is offline");
            return;
        }
        // This is a reliable in-session control signal. Reopening /media would
        // incorrectly try to redeem the original one-time ticket after it had
        // already authenticated this logical session.
        control_connection->PostTextMessage(std::string(kWsUseWebSocketMediaSignal));
        udp_media_fallback_state_.MarkWebSocketFallbackActive();
        LOGI("Requested WebSocket media fallback on the existing authenticated session");
    }

    void NetClient::Start() {
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
        if (network_type_ == ClientNetworkType::kWebsocket) {
            const auto media_path = MakeAuthenticatedWebSocketPath(media_path_);
            const auto ft_path = MakeAuthenticatedWebSocketPath(
                ft_path_, sdk_params_->file_transfer_only_);
            LOGI("Will connect by Websocket, ssl : {}", sdk_params_->ssl_);
            if (!sdk_params_->file_transfer_only_) {
                LOGI("media: {}", RedactTransportPath(media_path));
            }
            else {
                LOGI("file-transfer-only: media websocket disabled");
            }
            LOGI("file transfer: {}", RedactTransportPath(ft_path));
            if (sdk_params_->ssl_) {
                if (!sdk_params_->file_transfer_only_) {
                    ReplaceMediaConnection(std::make_shared<WssConnection>(sdk_params_, msg_notifier_, sdk_params_->ip_, sdk_params_->port_, media_path));
                }
                ft_conn_ = std::make_shared<WssConnection>(sdk_params_, msg_notifier_, sdk_params_->ip_, sdk_params_->port_, ft_path);
            }
            else {
                if (!sdk_params_->file_transfer_only_) {
                    ReplaceMediaConnection(std::make_shared<WsConnection>(sdk_params_, msg_notifier_, sdk_params_->ip_, sdk_params_->port_, media_path));
                }
                ft_conn_ = std::make_shared<WsConnection>(sdk_params_, msg_notifier_, sdk_params_->ip_, sdk_params_->port_, ft_path);
            }
        }
        else if (network_type_ == ClientNetworkType::kUdpKcp) {
            LOGI("Will connect by UDP");
            ReplaceMediaConnection(std::make_shared<UdpConnection>(sdk_params_, msg_notifier_, sdk_params_->ip_, sdk_params_->port_));
        }
        else if (network_type_ == ClientNetworkType::kRelay) {
            auto auto_relay = !sdk_params_->enable_p2p_;
            if (!sdk_params_->file_transfer_only_) {
                ReplaceMediaConnection(std::make_shared<RelayConnection>(sdk_params_, msg_notifier_, sdk_params_->relay_host_, sdk_params_->relay_port_, device_id_,remote_device_id_, auto_relay, kRoomTypeMedia));
            }
            ft_conn_ = std::make_shared<RelayConnection>(sdk_params_, msg_notifier_, sdk_params_->relay_host_, sdk_params_->relay_port_, ft_device_id_, ft_remote_device_id_, auto_relay, kRoomTypeFileTransfer);

            if (sdk_params_->enable_p2p_ && !sdk_params_->file_transfer_only_) {
                auto relay_conn = std::dynamic_pointer_cast<RelayConnection>(CurrentMediaConnection());
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
            ReplaceMediaConnection(relay_conn);
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
            ReplaceMediaConnection(rtc_local_conn_);
        }
        else if (network_type_ == ClientNetworkType::kUdpDirect) {
            // GameStream 风格双通道:ws 控制面(可靠消息/状态机全复用) + 裸 UDP 媒体面,
            // 见 docs/udp_gamestream_channel_plan.md
            LOGI("Will connect by UDP direct, ws ctrl: {}:{}, udp media: {}:{}", sdk_params_->ip_,
                 sdk_params_->port_, sdk_params_->ip_, sdk_params_->udp_port_);
            // Reliable control and file-transfer messages share the already
            // authenticated /media WebSocket. UDP carries audio/video only.
            // Opening another route would redeem the one-time ticket again and
            // later reconnects would be rejected after the ticket expires.
            if (!sdk_params_->file_transfer_only_) {
                ReplaceMediaConnection(MakeDirectWebSocketMediaConnection(true));
            }
            else {
                const auto ft_path = MakeAuthenticatedWebSocketPath(ft_path_, true);
                if (sdk_params_->ssl_) {
                    ft_conn_ = std::make_shared<WssConnection>(
                        sdk_params_, msg_notifier_, sdk_params_->ip_,
                        sdk_params_->port_, ft_path);
                }
                else {
                    ft_conn_ = std::make_shared<WsConnection>(
                        sdk_params_, msg_notifier_, sdk_params_->ip_,
                        sdk_params_->port_, ft_path);
                }
            }
            if (!sdk_params_->file_transfer_only_) {
                ReplaceUdpDirectConnection(std::make_shared<UdpDirectConnection>(
                    sdk_params_, msg_notifier_));
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

        const auto media_connection = CurrentMediaConnection();
        const bool managed_udp_media = network_type_ == ClientNetworkType::kUdpDirect && media_connection;
        const bool defer_ticketed_websocket_ready =
            network_type_ == ClientNetworkType::kWebsocket
            && !sdk_params_->connection_ticket_.empty()
            && media_connection;
        const bool defer_ticketed_direct_file_transfer =
            !sdk_params_->file_transfer_only_
            && !sdk_params_->connection_ticket_.empty()
            && media_connection
            && (network_type_ == ClientNetworkType::kWebsocket
                || network_type_ == ClientNetworkType::kUdpDirect);

        // Install the file callback before a fast media response can start the
        // deferred connection. Ticketed direct media and file routes are
        // deliberately serialized to avoid concurrent one-time redemption.
        if (ft_conn_) {
            ft_conn_->RegisterOnMessageCallback([weak_self](std::shared_ptr<Data> data) {
                const auto self = weak_self.lock();
                if (!self) return;
                self->stat_->AppendRecvDataSize(data->Size());
                if (auto m = self->ParseMessage(data); m) {
                    auto ack = ProtoMessageMaker::MakeAck(
                        m->device_id(), m->stream_id(), m->send_time(), m->type());
                    if (self->ft_conn_) self->ft_conn_->PostBinaryMessage(ack);
                }
            });
        }

        uint64_t managed_udp_generation = 0;
        if (managed_udp_media) {
            udp_media_fallback_state_.BeginProbe();
            managed_udp_generation = managed_media_generation_.fetch_add(1) + 1;
        }
        else {
            // In full WebRTC mode Relay is only the signaling/bootstrap path. The
            // user-visible connection becomes ready after ICE plus the required
            // RTC data channel, not when the Relay room is merely established.
            std::shared_ptr<Connection> primary_conn = network_type_ == ClientNetworkType::kWebRtc
                ? std::static_pointer_cast<Connection>(rtc_conn_)
                : (media_connection ? media_connection : ft_conn_);
            if (!primary_conn) {
                LOGE("Start failed: no transport connection was created");
                return;
            }
            primary_conn->RegisterOnConnectedCallback([weak_self, defer_ticketed_websocket_ready]() {
                if (const auto self = weak_self.lock(); self && !defer_ticketed_websocket_ready
                    && self->conn_cbk_) {
                    self->connection_notified_ = true;
                    self->conn_cbk_();
                }
            });

            primary_conn->RegisterOnDisConnectedCallback([weak_self]() {
                if (const auto self = weak_self.lock(); self && self->dis_conn_cbk_) {
                    self->dis_conn_cbk_();
                }
            });

            if (media_connection) {
                media_connection->RegisterOnMessageCallback(
                    [weak_self, defer_ticketed_websocket_ready](std::shared_ptr<Data> data) {
                    const auto self = weak_self.lock();
                    if (!self) return;
                    // statistics
                    self->stat_->AppendRecvDataSize(data->Size());
                    // parse
                    if (auto m = self->ParseMessage(data); m) {
                        if (defer_ticketed_websocket_ready
                            && !self->connection_notified_.exchange(true)
                            && self->conn_cbk_) {
                            self->conn_cbk_();
                        }
                        self->StartFileTransferConnection();
                        // ack
                        auto ack = ProtoMessageMaker::MakeAck(m->device_id(), m->stream_id(), m->send_time(), m->type());
                        if (const auto active_connection = self->CurrentMediaConnection()) {
                            active_connection->PostBinaryMessage(ack);
                        }
                    }
                    });
                media_connection->Start();
            }
        }
        if (ft_conn_ && !defer_ticketed_direct_file_transfer) {
            StartFileTransferConnection();
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

        if (const auto udp_connection = CurrentUdpDirectConnection()) {
            // UDP 媒体面:组帧后合成的 kVideoFrame,与上面 rtc_local 相同的上送路径,
            // 同样不回 Ack(裸 UDP 无应用层确认,丢帧走 IDR 请求恢复)
            udp_connection->SetOnVideoMessageCallback([weak_self](std::shared_ptr<px::Message> m) {
                const auto self = weak_self.lock();
                if (!self) return;
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
                if (!self) return;
                self->OnUdpMediaReady();
                self->stat_->AppendRecvDataSize((int64_t)m->ByteSizeLong());
                if (self->audio_frame_cbk_) {
                    self->audio_frame_cbk_(m);
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
                if (const auto self = weak_self.lock()) self->OnUdpMediaReady();
            });
            // UDP watchdog 断线:若媒体已被证明可用后又中断，仍可回退到同一 Render
            // 的直连 WS 媒体会话；非 UDP 模式沿用原有断线语义。
            udp_connection->RegisterOnDisConnectedCallback([weak_self]() {
                if (const auto self = weak_self.lock()) {
                    LOGW("Udp direct media channel lost; request WebSocket fallback.");
                    self->BeginUdpWebSocketFallback();
                }
            });
        }
        if (managed_udp_media) {
            // Configure every UDP callback before an accepted WS application
            // message can prove the association and start the media socket.
            StartManagedUdpMediaConnection(media_connection, managed_udp_generation);
        }
    }

    void NetClient::Exit() {
        if (exited_.exchange(true)) {
            return;
        }
        msg_listener_.reset();
        udp_media_fallback_state_.Stop();
        udp_media_probe_deadline_ms_ = 0;
        managed_media_generation_.fetch_add(1);
        if (const auto media_connection = CurrentMediaConnection()) {
            LOGI("Queued message count: {}", queuing_message_count_.load());
            media_connection->Stop();
        }
        if (ft_conn_) {
            ft_conn_->Stop();
        }
        if (rtc_conn_) {
            rtc_conn_->Stop();
        }
        if (const auto udp_connection = CurrentUdpDirectConnection()) {
            udp_connection->Stop();
        }
        rtc_local_conn_.reset();
        rtc_conn_.reset();
        ReplaceUdpDirectConnection(nullptr);
        ReplaceMediaConnection(nullptr);
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
            if (network_type_ == ClientNetworkType::kUdpDirect && udp_media_fallback_state_.UsesUdpMedia()) {
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
            if (network_type_ == ClientNetworkType::kUdpDirect && udp_media_fallback_state_.UsesUdpMedia()) {
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
            const auto media_connection = CurrentMediaConnection();
            auto queuing_msg_count = media_connection ? media_connection->GetQueuingMsgCount() : 0;
            int wait_count = 0;
            while (queuing_msg_count >= kMaxFileTransferQueuedMessages && wait_count < 200) {
                if (!media_connection || !media_connection->IsAlive()) {
                    LOGW("===> [Media] connection not alive, drop the message, queuing: {}", queuing_msg_count);
                    return;
                }
                //LOGI("===> queue too many msgs, count: {}, wait for 1ms", queuing_msg_count);
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
            const auto file_connection = network_type_ == ClientNetworkType::kUdpDirect
                && !sdk_params_->file_transfer_only_
                ? CurrentMediaConnection() : ft_conn_;
            if (!file_connection || !file_connection->IsAlive()) {
                return FileTransferSendResult::Disconnected("file-transfer connection is not alive");
            }
            if (file_connection->GetQueuingMsgCount() >= kMaxFileTransferQueuedMessages) {
                const auto signal = file_connection->AcquireFileTransferWritableSignal();
                if (file_connection->GetQueuingMsgCount() <= kFileTransferQueueLowWatermark) {
                    signal->NotifyWritable();
                }
                return FileTransferSendResult::Busy(
                    "file-transfer connection queue is full",
                    signal);
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
        CheckUdpMediaProbeTimeout();
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
            if (network_type_ != ClientNetworkType::kUdpDirect
                || sdk_params_->file_transfer_only_) {
                static_cast<void>(this->PostFileTransferMessage(buffer));
            }
        }
    }

    int64_t NetClient::GetQueuingMediaMsgCount() {
        if (sdk_params_->enable_p2p_ && rtc_conn_) {
            return rtc_conn_->GetQueuingMediaMsgCount();
        }
        else if (rtc_local_conn_) {
            return rtc_local_conn_->GetQueuingMediaMsgCount();
        }
        else if (const auto media_connection = CurrentMediaConnection()) {
            return media_connection->GetQueuingMsgCount();
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
        else if (network_type_ == ClientNetworkType::kUdpDirect
                 && !sdk_params_->file_transfer_only_) {
            if (const auto media_connection = CurrentMediaConnection()) {
                return media_connection->GetQueuingMsgCount();
            }
            return 0;
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
        if (const auto media_connection = CurrentMediaConnection()) {
            media_connection->On16msTimeout();
        }
    }

    void NetClient::RetryConnection() {
        if (const auto media_connection = CurrentMediaConnection()) {
            media_connection->RetryConnection();
        }
        if (ft_conn_) {
            ft_conn_->RetryConnection();
        }
        if (rtc_conn_) {
            rtc_conn_->RetryConnection();
        }
        if (const auto udp_connection = CurrentUdpDirectConnection()) {
            // 裸 UDP 无重连概念,先空实现(ws 控制面断线即整体断线)
            udp_connection->RetryConnection();
        }
    }

    bool NetClient::RestartRtcIce(const std::string& ice_config_json,
                                  const std::string& connection_ticket,
                                  const std::string& client_nonce,
                                  const std::string& instance_id,
                                  std::uint64_t revision) {
        return rtc_conn_ && rtc_conn_->RestartIce(
            ice_config_json, connection_ticket, client_nonce, instance_id, revision);
    }
}
