//
// Created by hy on 2024/4/25.
//

#include "rtc_server.h"
#include "peer_callback.h"
#include "rtc_local_plugin.h"
#include "gr_render/plugin_interface/gr_plugin_events.h"
#include "rtc_data_channel.h"
#include "tc_common_new/log.h"
#include "rtc_video_encoder_factory.h"
#include "video_source_impl.h"
#include "audio_source_impl.h"

using namespace webrtc;

namespace tc
{

    std::shared_ptr<RtcServer> RtcServer::Make(RtcLocalPlugin* plugin) {
        return std::make_shared<RtcServer>(plugin);
    }

    RtcServer::RtcServer(RtcLocalPlugin* plugin) {
        plugin_ = plugin;
    }

    RtcLocalPlugin* RtcServer::GetPlugin() {
        return plugin_;
    }

    bool RtcServer::Start(const std::string& stream_id, const std::string& offer_sdp) {
        this->stream_id_ = stream_id;
        this->offer_sdp_ = offer_sdp;
        webrtc::field_trial::InitFieldTrialsFromString("");
        rtc::LogMessage::LogToDebug(rtc::LS_ERROR);
        rtc::InitializeSSL();

        set_remote_offer_sdp_callback_ = SetSessCallback::Make(shared_from_this());
        set_local_answer_sdp_callback_ = SetSessCallback::Make(shared_from_this());
        create_answer_callback_ = CreateSessCallback::Make(shared_from_this());
        peer_callback_ = PeerCallback::Make(shared_from_this());

        // set remote offer sdp
        set_remote_offer_sdp_callback_->SetSdpSuccessCallback([=]() {
            LOGI("Set remote sdp success");
        });

        set_remote_offer_sdp_callback_->SetSdpFailedCallback([=](const std::string& m) {
            LOGE("Set remote sdp failed: {}", m);
        });

        // set local answer sdp
        set_local_answer_sdp_callback_->SetSdpSuccessCallback([=]() {
            LOGI("Set local answer sdp success.");
        });

        set_local_answer_sdp_callback_->SetSdpFailedCallback([=, this](const std::string& m) {
            LOGI("Set local answer sdp failed:{}", m);
        });

        // create answer sdp callback
        create_answer_callback_->SetOnCreateSdpSuccessCallback([=, this](webrtc::SessionDescriptionInterface* desc) {
            peer_conn_->SetLocalDescription(this->set_local_answer_sdp_callback_.get(), desc);
        });

        create_answer_callback_->SetOnCreateSdpFailedCallback([=, this](const std::string& m) {
            LOGE("Create answer sdp failed: {}", m);
            if (answer_sdp_callback_) {
                answer_sdp_callback_("");
            }
        });

        // peer connection
        peer_callback_->SetOnIceCallback([=, this](const std::string& ice, const std::string& mid, int sdp_mline_index) {
            LOGI("ICE: {}", ice);
            this->SendIceToRemote(ice, mid, sdp_mline_index);
        });

        peer_callback_->SetOnDataChannelCallback([=, this](const std::string& name, rtc::scoped_refptr<webrtc::DataChannelInterface> ch) {
            if (name == "media_data_channel") {
                media_data_channel_ = std::make_shared<RtcDataChannel>(name, shared_from_this(), ch);

                // data callback
                media_data_channel_->SetOnDataCallback([=, this](const std::string& data) {
                    auto payload_msg = Data::Make(data.data(), data.size());
                    plugin_->OnClientEventCame(true, 0, NetPluginType::kWebRtc, NetChannelType::kMedia, payload_msg);
                });
            }
            else if (name == "ft_data_channel") {
                ft_data_channel_ = std::make_shared<RtcDataChannel>(name, shared_from_this(), ch);

                // data callback
                ft_data_channel_->SetOnDataCallback([=, this](const std::string& data) {
                    auto payload_msg = Data::Make(data.data(), data.size());
                    plugin_->OnClientEventCame(true, 0, NetPluginType::kWebRtc, NetChannelType::kFileTransfer, payload_msg);
                });
            }
        });

        // network state
        peer_callback_->SetOnIceConnectedCallback([=, this]() {

        });

        peer_callback_->SetOnIceDisConnectedCallback([=, this]() {
            if (!media_data_channel_) {return;}
            auto event = std::make_shared<GrPluginClientDisConnectedEvent>();
            event->stream_id_ = media_data_channel_->the_conn_id_;
            event->end_timestamp_ = (int64_t) TimeUtil::GetCurrentTimestamp();
            event->duration_ =   event->end_timestamp_ - media_data_channel_->created_timestamp_;
            this->plugin_->CallbackEvent(event);
        });

        peer_callback_->SetOnIceGatherCompletedCallback([=, this]() {
            LOGI("Ice Gather completed.");
            std::string answer_sdp;
            if (!this->peer_conn_->local_description()->ToString(&answer_sdp)) {
                LOGE("Get local answer failed");
                if (answer_sdp_callback_) {
                    answer_sdp_callback_("");
                }
            }
            else {
                this->answer_sdp_ = answer_sdp;
                LOGI("Get answer sdp success");
                if (answer_sdp_callback_) {
                    answer_sdp_callback_(answer_sdp);
                }
            }
        });

        CreatePeerConnectionFactory();
        CreatePeerConnection();
        return true;
    }

    void RtcServer::CreateSomeMediaDeps(PeerConnectionFactoryDependencies& media_deps) {
        media_deps.adm = AudioDeviceModule::CreateForTest(
                AudioDeviceModule::kDummyAudio, media_deps.task_queue_factory.get());
        media_deps.audio_encoder_factory =
                webrtc::CreateAudioEncoderFactory<webrtc::AudioEncoderOpus>();
        media_deps.audio_decoder_factory =
                webrtc::CreateAudioDecoderFactory<webrtc::AudioDecoderOpus>();
        // custom encoders
        media_deps.video_encoder_factory = std::make_unique<RtcVideoEncoderFactory>(plugin_, shared_from_this()),
        // default encoders
        // media_deps.video_encoder_factory = std::make_unique<VideoEncoderFactoryTemplate<
        //         LibvpxVp8EncoderTemplateAdapter, LibvpxVp9EncoderTemplateAdapter,
        //         OpenH264EncoderTemplateAdapter, LibaomAv1EncoderTemplateAdapter>>();
        media_deps.video_decoder_factory =
                std::make_unique<VideoDecoderFactoryTemplate<
                        LibvpxVp8DecoderTemplateAdapter, LibvpxVp9DecoderTemplateAdapter,
                        OpenH264DecoderTemplateAdapter, Dav1dDecoderTemplateAdapter>>();
        media_deps.audio_processing = webrtc::AudioProcessingBuilder().Create();
    }

    void RtcServer::CreatePeerConnectionFactory() {
        configuration_.sdp_semantics = webrtc::SdpSemantics::kUnifiedPlan;
        configuration_.media_config.video.periodic_alr_bandwidth_probing = true;
        //configuration_.enable_dtls_srtp = true;

        network_thread_ = rtc::Thread::CreateWithSocketServer();
        network_thread_->Start();
        worker_thread_ = rtc::Thread::Create();
        worker_thread_->Start();
        sig_thread_ = rtc::Thread::Create();
        sig_thread_->Start();

        webrtc::PeerConnectionFactoryDependencies media_deps;
        media_deps.task_queue_factory = webrtc::CreateDefaultTaskQueueFactory();
        CreateSomeMediaDeps(media_deps);

        peer_conn_factory_ = webrtc::CreatePeerConnectionFactory(
            network_thread_.get(), worker_thread_.get(), sig_thread_.get(),
            nullptr,
            std::move(media_deps.audio_encoder_factory),
            std::move(media_deps.audio_decoder_factory),
            std::move(media_deps.video_encoder_factory),
            std::move(media_deps.video_decoder_factory),
            nullptr, nullptr);

        if (peer_conn_factory_.get() == nullptr) {
            LOGE("Error on CreateModularPeerConnectionFactory.");
            return;
        }
        LOGI("CreatePeerConnectionFactory success.");
    }

    void RtcServer::CreatePeerConnection() {
        configuration_.port_allocator_config.min_port = 60430;
        configuration_.port_allocator_config.max_port = 60490;
        auto result = peer_conn_factory_->
                CreatePeerConnectionOrError(configuration_, webrtc::PeerConnectionDependencies(peer_callback_.get()));
        if (!result.ok()) {
            std::cerr << "create peer connection failed: " << result.error().message() << std::endl;
            return;
        }
        this->peer_conn_ = result.value();

        // video source
        video_source_ = std::make_shared<VideoSourceImpl>(plugin_);
        video_track_source_ = rtc::make_ref_counted<VideoTrackSourceImpl>(plugin_, video_source_);
        auto video_track = peer_conn_factory_->CreateVideoTrack(video_track_source_, "video_track_source_1");
        auto rtc_error_or = peer_conn_->AddTrack(video_track, { "video_track_1" });
        if (!rtc_error_or.ok()) {
            LOGE("peer connection add track failed. with {}", rtc_error_or.error().message());
            return;
        }

        // audio source
        audio_source_ = AudioSourceImpl::Create();
        auto audio_track = peer_conn_factory_->CreateAudioTrack("audio", audio_source_.get());
        peer_conn_->AddTrack(audio_track, { "audio1" });

        // set remote sdp
        LOGI("Will set remote offer sdp.");
        webrtc::SdpParseError error;
        webrtc::SessionDescriptionInterface* session_description(webrtc::CreateSessionDescription("offer", offer_sdp_, &error));
        peer_conn_->SetRemoteDescription(this->set_remote_offer_sdp_callback_.get(), session_description);
        if (!error.line.empty()) {
            LOGE("OnOfferSdpCallback, SetRemoteDescription error: {}, {}", error.line, error.description);
            return;
        }

        LOGI("Will create answer sdp.");
        webrtc::PeerConnectionInterface::RTCOfferAnswerOptions options;
        options.offer_to_receive_audio = true;
        options.offer_to_receive_video = true;
        peer_conn_->CreateAnswer(this->create_answer_callback_.get(), options);

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

    void RtcServer::SendIceToRemote(const std::string& ice, const std::string& mid, int sdp_mline_index) {
        auto event = std::make_shared<GrPluginRtcIceEvent>();
        event->stream_id_ = stream_id_;
        event->ice_ = ice;
        event->mid_ = mid;
        event->sdp_mline_index_ = sdp_mline_index;
        plugin_->CallbackEvent(event);
    }

    void RtcServer::PostProtoMessage(std::shared_ptr<Data> msg, bool run_through) {
        if (network_thread_ && media_data_channel_ && !exit_) {
            network_thread_->PostTask([=, this]() {
                media_data_channel_->SendData(msg);
            });
        }
    }

    bool RtcServer::PostTargetStreamProtoMessage(const std::string &stream_id, std::shared_ptr<Data> msg, bool run_through) {
        if (network_thread_ && media_data_channel_ && !exit_) {
            network_thread_->PostTask([=, this]() {
                media_data_channel_->SendData(msg);
            });
        }
        return true;
    }

    bool RtcServer::PostTargetFileTransferProtoMessage(const std::string &stream_id, std::shared_ptr<Data> msg, bool run_through) {
        if (ft_data_channel_ && !exit_) {
            ft_data_channel_->SendData(msg);
        }
        return true;
    }

    bool RtcServer::IsDataChannelConnected() {
        return !exit_ && media_data_channel_ && media_data_channel_->IsConnected();
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

    void RtcServer::On100msTimeout() {
        if (ft_data_channel_ && !exit_) {
            ft_data_channel_->On100msTimeout();
        }
    }

    std::string RtcServer::GetAnswerSdp() {
        return answer_sdp_;
    }

    void RtcServer::SetOnAnswerCallback(std::function<void(const std::string& answer_sdp)>&& callback) {
        answer_sdp_callback_ = callback;
    }

    void RtcServer::OnNewFrameCaptured(const std::string& mon_name, uint64_t frame_idx, int frame_width, int frame_height, uint64_t handle, int64_t adapter_id, uint64_t frame_format) {
        if (!video_source_) {
            LOGE("Don't have video source");
            return;
        }
        if (handle == 0) {
            LOGE("Invalid texture handle");
            return;
        }

        if (last_captured_frame_index_ == 0) {
            last_captured_frame_index_ = frame_idx;
        }
        auto diff = frame_idx - last_captured_frame_index_;
        last_captured_frame_index_ = frame_idx;
        if (diff > 1) {
            LOGE("OnNewFrameCaptured, but diff size is: {}", diff);
        }

        auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        auto buffer = rtc::make_ref_counted<NotifyFrameFrameBuffer>(frame_idx, frame_width, frame_height, handle, adapter_id, frame_format);
        webrtc::VideoFrame notify_frame = webrtc::VideoFrame::Builder().
                set_video_frame_buffer(buffer).
                set_timestamp_us(us).
                set_id(frame_idx).
                build();
        video_source_->OnNotifyFrame(notify_frame);
    }

    void RtcServer::Exit() {
        exit_ = true;
        if (media_data_channel_) {
            media_data_channel_->Close();
        }
        if (ft_data_channel_) {
            ft_data_channel_->Close();
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

}
