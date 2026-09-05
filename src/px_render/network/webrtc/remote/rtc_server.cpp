//
// Created by hy on 2024/4/25.
//

#include "rtc_server.h"
#include "peer_callback.h"
#include "desktop_capture.h"
#include "desktop_capture_source.h"
#include "webrtc_remote_transport.h"
#include "rtc_messages.h"
#include "rtc_data_channel.h"
#include "px_common_new/data.h"
#include "px_common_new/md5.h"
#include "px_common_new/time_util.h"
#include "px_common_new/uuid.h"
#include <nlohmann/json.hpp>
#include <algorithm>

using namespace webrtc;

namespace px {

std::shared_ptr<RtcServer> RtcServer::Make(const std::shared_ptr<WebRtcRemoteRuntime>& runtime) {
    return std::make_shared<RtcServer>(runtime);
}

RtcServer::RtcServer(const std::shared_ptr<WebRtcRemoteRuntime>& runtime) : runtime_(runtime) {
    connection_instance_id_ = MD5::Hex(px::GetUUID());
}

std::shared_ptr<WebRtcExecutionContext> RtcServer::GetExecutionContext() const {
    return runtime_ ? runtime_->GetContext() : nullptr;
}

void RtcServer::DispatchEvent(WebRtcEvent event) const {
    if (runtime_) {
        runtime_->QueueEvent(std::move(event));
    }
}

bool RtcServer::Start(const std::string& stream_id, const std::string& offer_sdp, const std::string& ice_config_json,
                      const std::vector<std::string>& permissions) {
    this->stream_id_ = stream_id;
    this->offer_sdp_ = offer_sdp;
    this->ice_config_json_ = ice_config_json;
    this->permissions_ = permissions;
    webrtc::field_trial::InitFieldTrialsFromString("");
    rtc::LogMessage::LogToDebug(rtc::LS_ERROR);
    rtc::InitializeSSL();

    set_remote_offer_sdp_callback_ = SetSessCallback::Make(shared_from_this());
    set_local_answer_sdp_callback_ = SetSessCallback::Make(shared_from_this());
    create_answer_callback_ = CreateSessCallback::Make(shared_from_this());
    peer_callback_ = PeerCallback::Make(shared_from_this());
    const auto weak_server = weak_from_this();

    // set remote offer sdp
    set_remote_offer_sdp_callback_->SetSdpSuccessCallback([weak_server]() {
        const auto server = weak_server.lock();
        if (!server) {
            return;
        }
        LOGI("Set remote sdp success");
        if (!server->peer_conn_) {
            return;
        }
        LOGI("Will create answer sdp.");
        webrtc::PeerConnectionInterface::RTCOfferAnswerOptions options;
        options.offer_to_receive_audio = true;
        options.offer_to_receive_video = true;
        server->peer_conn_->CreateAnswer(server->create_answer_callback_.get(), options);
    });

    set_remote_offer_sdp_callback_->SetSdpFailedCallback([](const std::string& m) { LOGE("Set remote sdp failed: {}", m); });

    // set local answer sdp
    set_local_answer_sdp_callback_->SetSdpSuccessCallback([]() { LOGI("Set local answer sdp success."); });

    set_local_answer_sdp_callback_->SetSdpFailedCallback([](const std::string& m) { LOGI("Set local answer sdp failed:{}", m); });

    // create answer sdp callback
    create_answer_callback_->SetOnCreateSdpSuccessCallback(
        [weak_server](webrtc::SessionDescriptionInterface* desc) { // NOLINT(gammaray-raw-pointer-boundary): libwebrtc SDP callback ABI
            const auto server = weak_server.lock();
            if (!server) {
                return;
            }
            LOGI("Create answer sdp success, will set local sdp.");
            std::string sdp;
            desc->ToString(&sdp);
            server->sdp_ = sdp;
            server->peer_conn_->SetLocalDescription(server->set_local_answer_sdp_callback_.get(), desc);
            // send to remote
            server->SendSdpToRemote(sdp);
        });

    create_answer_callback_->SetOnCreateSdpFailedCallback([](const std::string& m) { LOGE("Create answer sdp failed: {}", m); });

    // peer connection
    peer_callback_->SetOnIceCallback([weak_server](const std::string& ice, const std::string& mid, int sdp_mline_index) {
        LOGI("ICE: {}", ice);
        if (const auto server = weak_server.lock()) {
            server->SendIceToRemote(ice, mid, sdp_mline_index);
        }
    });

    peer_callback_->SetOnDataChannelCallback([weak_server](const std::string& name, rtc::scoped_refptr<webrtc::DataChannelInterface> ch) {
        const auto server = weak_server.lock();
        if (!server) {
            ch->Close();
            return;
        }
        const bool may_view = std::find(server->permissions_.begin(), server->permissions_.end(), "view") != server->permissions_.end();
        const bool may_file = std::find(server->permissions_.begin(), server->permissions_.end(), "file") != server->permissions_.end();
        if (name == "media_data_channel" && may_view) {
            server->media_data_channel_ = std::make_shared<RtcDataChannel>(name, server, ch);

            // data callback
            server->media_data_channel_->SetOnDataCallback([weak_server](const std::string& data) {
                const auto locked = weak_server.lock();
                if (!locked) {
                    return;
                }
                if (!IsRtcPayloadAuthorized(data, locked->permissions_)) {
                    LOGW("Drop unauthorized or malformed full RTC media payload");
                    return;
                }
                auto payload_msg = Data::Make(data.data(), data.size());
                locked->runtime_->DispatchClientEvent(false, TransportChannel::kMedia, std::move(payload_msg),
                                                      std::string("rtc:") + locked->stream_id_);
            });
        } else if (name == "ft_data_channel" && may_file) {
            server->ft_data_channel_ = std::make_shared<RtcDataChannel>(name, server, ch);

            // data callback
            server->ft_data_channel_->SetOnDataCallback([weak_server](const std::string& data) {
                const auto locked = weak_server.lock();
                if (!locked) {
                    return;
                }
                if (!IsRtcPayloadAuthorized(data, locked->permissions_)) {
                    LOGW("Drop unauthorized or malformed full RTC file payload");
                    return;
                }
                auto payload_msg = Data::Make(data.data(), data.size());
                locked->runtime_->DispatchClientEvent(false, TransportChannel::kFileTransfer, std::move(payload_msg),
                                                      locked->connection_instance_id_);
            });
        } else if (name == "input_data_channel") {
            const bool may_input = std::find(server->permissions_.begin(), server->permissions_.end(), "input") != server->permissions_.end();
            if (!may_input) {
                LOGW("Close full RTC input channel: permission not granted");
                ch->Close();
                return;
            }
            server->input_data_channel_ = std::make_shared<RtcDataChannel>(name, server, ch);
            server->input_data_channel_->SetOnDataCallback([weak_server](const std::string& data) {
                const auto locked = weak_server.lock();
                if (!locked) {
                    return;
                }
                auto payload_msg = Data::Make(data.data(), data.size());
                locked->runtime_->DispatchClientEvent(true, TransportChannel::kMedia, std::move(payload_msg),
                                                      std::string("rtc:") + locked->stream_id_);
            });
        }
    });

    // network state
    peer_callback_->SetOnIceConnectedCallback([]() {

    });

    peer_callback_->SetOnIceDisConnectedCallback([weak_server]() {
        if (const auto server = weak_server.lock()) {
            server->EmitClientDisconnectedEvent();
        }
    });

    CreatePeerConnectionFactory();
    CreatePeerConnection();
    return peer_conn_ != nullptr;
}

bool RtcServer::RestartWithOffer(const std::string& offer_sdp, const std::string& ice_config_json, const std::vector<std::string>& permissions) {
    if (exit_ || !peer_conn_) {
        return false;
    }
    if (!ApplyIceConfiguration(ice_config_json, true)) {
        return false;
    }
    offer_sdp_ = offer_sdp;
    ice_config_json_ = ice_config_json;
    permissions_ = permissions;
    disconnect_event_sent_ = false;
    LOGI("Apply in-place full RTC ICE restart, stream={}", stream_id_);
    return SetRemoteOffer(offer_sdp_);
}

static void CreateSomeMediaDeps(PeerConnectionFactoryDependencies& media_deps) {
    media_deps.adm = AudioDeviceModule::CreateForTest(AudioDeviceModule::kDummyAudio, media_deps.task_queue_factory.get());
    media_deps.audio_encoder_factory = webrtc::CreateAudioEncoderFactory<webrtc::AudioEncoderOpus>();
    media_deps.audio_decoder_factory = webrtc::CreateAudioDecoderFactory<webrtc::AudioDecoderOpus>();
    media_deps.video_encoder_factory =
        std::make_unique<VideoEncoderFactoryTemplate<LibvpxVp8EncoderTemplateAdapter, LibvpxVp9EncoderTemplateAdapter, OpenH264EncoderTemplateAdapter,
                                                     LibaomAv1EncoderTemplateAdapter>>();
    media_deps.video_decoder_factory = std::make_unique<VideoDecoderFactoryTemplate<LibvpxVp8DecoderTemplateAdapter, LibvpxVp9DecoderTemplateAdapter,
                                                                                    OpenH264DecoderTemplateAdapter, Dav1dDecoderTemplateAdapter>>();
    media_deps.audio_processing = webrtc::AudioProcessingBuilder().Create();
}

void RtcServer::CreatePeerConnectionFactory() {
    configuration_.sdp_semantics = webrtc::SdpSemantics::kUnifiedPlan;
    configuration_.media_config.video.periodic_alr_bandwidth_probing = true;
    // configuration_.enable_dtls_srtp = true;

    ApplyIceConfiguration(ice_config_json_, false);
    network_thread_ = rtc::Thread::CreateWithSocketServer();
    network_thread_->Start();
    worker_thread_ = rtc::Thread::Create();
    worker_thread_->Start();
    sig_thread_ = rtc::Thread::Create();
    sig_thread_->Start();

    webrtc::PeerConnectionFactoryDependencies media_deps;
    media_deps.task_queue_factory = webrtc::CreateDefaultTaskQueueFactory();
    CreateSomeMediaDeps(media_deps);

    peer_conn_factory_ = webrtc::CreatePeerConnectionFactory(network_thread_.get(), worker_thread_.get(), sig_thread_.get(), nullptr,
                                                             std::move(media_deps.audio_encoder_factory), std::move(media_deps.audio_decoder_factory),
                                                             std::move(media_deps.video_encoder_factory), std::move(media_deps.video_decoder_factory),
                                                             nullptr, nullptr);

    if (peer_conn_factory_.get() == nullptr) {
        LOGE("Error on CreateModularPeerConnectionFactory.");
        return;
    }
    LOGI("CreatePeerConnectionFactory success.");
}

bool RtcServer::ApplyIceConfiguration(const std::string& ice_config_json, bool update_peer_connection) {
    auto next_configuration = configuration_;
    next_configuration.servers.clear();
    try {
        const auto config = nlohmann::json::parse(ice_config_json);
        for (const auto& entry : config.value("ice_servers", nlohmann::json::array())) {
            const auto username = entry.value("username", "");
            const auto credential = entry.value("credential", "");
            for (const auto& url : entry.value("urls", std::vector<std::string>{})) {
                auto server = webrtc::PeerConnectionInterface::IceServer();
                server.uri = url;
                server.username = username;
                server.password = credential;
                server.tls_cert_policy = webrtc::PeerConnectionInterface::TlsCertPolicy::kTlsCertPolicySecure;
                next_configuration.servers.push_back(std::move(server));
            }
        }
        LOGI("Configured {} ICE server URLs for full RTC render", next_configuration.servers.size());
    } catch (const std::exception& error) {
        LOGE("Invalid full RTC ICE configuration: {}", error.what());
        return false;
    }
    if (update_peer_connection && peer_conn_) {
        const auto error = peer_conn_->SetConfiguration(next_configuration);
        if (!error.ok()) {
            LOGE("SetConfiguration for full RTC restart failed: {}", error.message());
            return false;
        }
    }
    configuration_ = std::move(next_configuration);
    return true;
}

void RtcServer::CreatePeerConnection() {
    if (!peer_conn_factory_) {
        LOGE("Cannot create full RTC PeerConnection without a factory");
        return;
    }
    configuration_.port_allocator_config.min_port = 60430;
    configuration_.port_allocator_config.max_port = 60490;
    auto result = peer_conn_factory_->CreatePeerConnectionOrError(configuration_, webrtc::PeerConnectionDependencies(peer_callback_.get()));
    if (!result.ok()) {
        std::cerr << "create peer connection failed: " << result.error().message() << std::endl;
        return;
    }
    auto peer_conn = result.value();

    if (peer_conn.get() == nullptr) {
        peer_conn_factory_ = nullptr;
        std::cout << ":" << std::this_thread::get_id() << ":"
                  << "Error on CreatePeerConnection." << std::endl;
        exit(EXIT_FAILURE);
    }
    this->peer_conn_ = peer_conn;

    SetRemoteOffer(offer_sdp_);
}

bool RtcServer::SetRemoteOffer(const std::string& offer_sdp) {
    if (!peer_conn_) {
        return false;
    }
    LOGI("Will set remote offer sdp.");
    webrtc::SdpParseError error;
    std::unique_ptr<webrtc::SessionDescriptionInterface> session_description{webrtc::CreateSessionDescription("offer", offer_sdp, &error)};
    if (!session_description || !error.line.empty()) {
        LOGE("OnOfferSdpCallback, SetRemoteDescription error: {}, {}", error.line, error.description);
        return false;
    }
    peer_conn_->SetRemoteDescription(set_remote_offer_sdp_callback_.get(),
                                     session_description.release()); // NOLINT(gammaray-raw-pointer-boundary): ownership passes to libwebrtc
    return true;
}

void RtcServer::OnRemoteIce(const std::string& ice, const std::string& mid, int sdp_mline_index) {
    LOGI("OnRemoteIce: {}", ice);
    webrtc::SdpParseError error;
    std::unique_ptr<webrtc::IceCandidateInterface> candidate(webrtc::CreateIceCandidate(mid, sdp_mline_index, ice, &error));
    if (!error.line.empty()) {
        LOGE("Create IceCandidate failed: {} - {}", error.line, error.description);
        return;
    }
    peer_conn_->AddIceCandidate(std::move(candidate), [](webrtc::RTCError error) {
        if (error.ok()) {
            LOGI("AddIceCandidate success.");
        } else {
            LOGE("AddIceCandidate failed: {}", error.message());
        }
    });
}

void RtcServer::SendSdpToRemote(const std::string& sdp) {
    runtime_->QueueEvent(WebRtcAnswerSdpEvent{.stream_id = stream_id_, .sdp = sdp});
}

void RtcServer::SendIceToRemote(const std::string& ice, const std::string& mid, int sdp_mline_index) {
    runtime_->QueueEvent(WebRtcIceEvent{
        .stream_id = stream_id_,
        .ice = ice,
        .mid = mid,
        .sdp_mline_index = sdp_mline_index,
    });
}

void RtcServer::PostProtoMessage(std::shared_ptr<Data> msg, bool run_through) {
    if (!msg || !IsRtcPayloadAuthorized(msg->AsString(), permissions_)) {
        LOGW("Drop outbound RTC protocol message: session lacks its required permission");
        return;
    }
    if (network_thread_ && media_data_channel_ && !exit_) {
        const auto weak_server = weak_from_this();
        network_thread_->PostTask([weak_server, msg]() {
            if (const auto server = weak_server.lock(); server && server->media_data_channel_ && !server->exit_) {
                server->media_data_channel_->SendData(msg);
            }
        });
    }
}

bool RtcServer::PostTargetStreamProtoMessage(const std::string& stream_id, std::shared_ptr<Data> msg, bool run_through) {
    if (stream_id.empty() || stream_id != stream_id_ || !msg || !IsRtcPayloadAuthorized(msg->AsString(), permissions_)) {
        return false;
    }
    if (network_thread_ && media_data_channel_ && !exit_) {
        const auto weak_server = weak_from_this();
        network_thread_->PostTask([weak_server, msg]() {
            if (const auto server = weak_server.lock(); server && server->media_data_channel_ && !server->exit_) {
                server->media_data_channel_->SendData(msg);
            }
        });
    }
    return true;
}

bool RtcServer::PostTargetFileTransferProtoMessage(const std::string& stream_id, std::shared_ptr<Data> msg, bool run_through) {
    if (stream_id.empty() || stream_id != stream_id_ || !msg || !IsRtcPayloadAuthorized(msg->AsString(), permissions_)) {
        return false;
    }
    // 与 net_rtc_local 同理:必须投递到 WebRTC 网络线程再 Send,避免跨线程
    // 调用 data_channel_->Send() 被 libwebrtc 静默丢弃。
    if (!network_thread_ || !ft_data_channel_ || exit_ || !ft_data_channel_->IsConnected()) {
        return false;
    }
    const auto weak_self = weak_from_this();
    network_thread_->PostTask([weak_self, msg]() {
        if (const auto self = weak_self.lock(); self && self->ft_data_channel_ && !self->exit_) {
            self->ft_data_channel_->SendData(msg);
        }
    });
    return true;
}

bool RtcServer::IsDataChannelConnected() {
    return !exit_ && media_data_channel_ && media_data_channel_->IsConnected();
}

bool RtcServer::IsFtDataChannelConnected() {
    return !exit_ && ft_data_channel_ && ft_data_channel_->IsConnected();
}

uint32_t RtcServer::GetMediaPendingMessages() {
    return !exit_ && media_data_channel_ ? media_data_channel_->GetPendingDataCount() : 0;
}

uint32_t RtcServer::GetFtPendingMessages() {
    return !exit_ && ft_data_channel_ ? ft_data_channel_->GetPendingDataCount() : 0;
}

bool RtcServer::HasEnoughBufferForQueuingMediaMessages() {
    return !exit_ && media_data_channel_ && media_data_channel_->HasEnoughBufferForQueuingMessages();
}

bool RtcServer::HasEnoughBufferForQueuingFtMessages() {
    return !exit_ && ft_data_channel_ && ft_data_channel_->HasEnoughBufferForQueuingMessages();
}

std::shared_ptr<FileTransferWritableSignal> RtcServer::AcquireFtWritableSignal() {
    return !exit_ && ft_data_channel_ ? ft_data_channel_->AcquireFileTransferWritableSignal() : std::shared_ptr<FileTransferWritableSignal>{};
}

void RtcServer::On100msTimeout() {
    if (ft_data_channel_ && !exit_) {
        ft_data_channel_->On100msTimeout();
    }
}

void RtcServer::EmitClientDisconnectedEvent() {
    // 全连接生命周期只发一次
    if (disconnect_event_sent_.exchange(true)) {
        return;
    }
    if (!runtime_) {
        return;
    }
    WebRtcClientDisconnectedEvent event{};
    // 真实访客 stream id(Start 时信令传入,与 px::Message.stream_id 一致);
    // 空时回退 datachannel 内部 id(历史行为)
    event.stream_id = !stream_id_.empty() ? stream_id_ : (media_data_channel_ ? media_data_channel_->the_connection_id_ : "");
    event.connection_id = connection_instance_id_;
    event.end_timestamp = static_cast<std::int64_t>(TimeUtil::GetCurrentTimestamp());
    event.duration = media_data_channel_ ? event.end_timestamp - media_data_channel_->created_timestamp_ : 0;
    const auto stream_id = event.stream_id;
    runtime_->QueueEvent(std::move(event));
    LOGW("Client disconnected event emitted, stream: {}", stream_id);
}

void RtcServer::EmitFileTransferDisconnectedEvent() {
    if (!runtime_ || stream_id_.empty()) {
        return;
    }
    runtime_->QueueEvent(WebRtcFileTransferDisconnectedEvent{
        .stream_id = stream_id_,
        .connection_instance_id = connection_instance_id_,
    });
    LOGI("File-transfer route disconnected, connection: {}, stream: {}", connection_instance_id_, stream_id_);
}

void RtcServer::Exit() {
    exit_ = true;
    if (media_data_channel_) {
        media_data_channel_->Close();
    }
    if (ft_data_channel_) {
        ft_data_channel_->Close();
    }
    if (input_data_channel_) {
        input_data_channel_->Close();
    }
    if (peer_conn_) {
        peer_conn_->Close();
        peer_conn_ = nullptr;
    }
    peer_conn_factory_ = nullptr;

    if (network_thread_) {
        network_thread_->Stop();
    }
    if (worker_thread_) {
        worker_thread_->Stop();
    }
    if (sig_thread_) {
        sig_thread_->Stop();
    }

    rtc::CleanupSSL();
}

} // namespace px
