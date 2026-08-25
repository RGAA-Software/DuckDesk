//
// Created by RGAA on 13/04/2025.
//

#include "rtc_connection.h"
#include "px_common_new/log.h"
#include "px_common_new/webrtc_helper.h"
#include "px_common_new/time_util.h"
#include "px_common_new/thread.h"
#include "peer_callback.h"
#include "rtc_data_channel.h"
#include "rtc_video_sink.h"
#include "rtc_encoded_frame_sink.h"
#include "rtc_audio_sink.h"
#include "rtc_null_decoder_factory.h"
#include <QApplication>
#include <px_common_new/folder_util.h>
#include <px_common_new/string_util.h>
#include <algorithm>
#include <cstdlib>
#include <nlohmann/json.hpp>
#include "api/stats/rtc_stats_collector_callback.h"

void* GetInstance() {
    static px::RtcConnection conn;
    return (void*)&conn;
}

using namespace webrtc;

namespace px
{

    class RtcStatsJsonCallback : public webrtc::RTCStatsCollectorCallback {
    public:
        explicit RtcStatsJsonCallback(OnStatsJsonCallback callback)
            : callback_(std::move(callback)) {}

        void OnStatsDelivered(
            const rtc::scoped_refptr<const webrtc::RTCStatsReport>& report) override {
            if (callback_ && report) {
                callback_(report->ToJson());
            }
        }

    private:
        OnStatsJsonCallback callback_;
    };

    // forwards libwebrtc internal logs into our logger, mainly to see decoder errors
    class WebrtcLogForwarder : public rtc::LogSink {
    public:
        void OnLogMessage(const std::string& message) override {
            std::string m = message;
            while (!m.empty() && (m.back() == '\n' || m.back() == '\r')) {
                m.pop_back();
            }
            if (!m.empty()) {
                LOGW("[libwebrtc] {}", m);
            }
        }
    };

    RtcConnection::RtcConnection() {
        work_thread_ = Thread::Make("rtc_work_thread", 1024*1024*10);
        work_thread_->Poll();
    }

    bool RtcConnection::Init(const std::string& remote_device_id) {
        this->remote_device_id_ = remote_device_id;
        auto beg = TimeUtil::GetCurrentTimestamp();
        auto exe_dir = qApp->applicationDirPath();
        auto data_path = FolderUtil::GetProgramDataPath();
        const auto log_path = std::format(L"{}/px_logs/app.rtc.{}.log", data_path, StringUtil::ToWString(remote_device_id));
        Logger::InitLog(log_path, true);
        LOGI("*******************************");
        LOGI("========BEGIN RTC INIT=========");

        // WebRTC-ForcePlayoutDelay:强制 VCMTiming 目标 playout 延迟为 0,
        // 帧在 jitter buffer 里组完整即送解码,不再等"预定解码时刻"
        // (jitter 估计 + 解码估计 + 10ms render delay 的 hold)。
        // encoded-sink 模式下解码由 sdk 自己的 FFmpegVulkan 链完成,libwebrtc
        // 的延迟估计对本链路是纯开销;此 trial 存在于 webrtc.lib 的
        // modules/video_coding/timing/timing.cc(strings 确认,参数 min_ms/max_ms)。
        // RTC Local 只跑 loopback/局域网,无需抖动余量。
        webrtc::field_trial::InitFieldTrialsFromString("WebRTC-ForcePlayoutDelay/min_ms:0,max_ms:0/");
        rtc::LogMessage::LogToDebug(rtc::LS_ERROR);
        // forward libwebrtc internal logs (decoder errors etc.) into our log,
        // warnings and above only, INFO floods the file log
        static auto* webrtc_log_forwarder = new WebrtcLogForwarder();
        rtc::LogMessage::AddLogToStream(webrtc_log_forwarder, rtc::LS_WARNING);
        rtc::InitializeSSL();

        peer_callback_ = PeerCallback::Make(this);
        set_local_sdp_callback_ = SetSessCallback::Make(this);
        set_remote_sdp_callback_ = SetSessCallback::Make(this);
        create_sess_callback_ = CreateSessCallback::Make(this);

        // create sess callback
        create_sess_callback_->SetOnCreateSdpSuccessCallback([=, this](webrtc::SessionDescriptionInterface* desc) {
            LOGI("Create sdp success, will set local desc.");
            std::string sdp;
            if (!desc->ToString(&sdp)) {
                LOGE("Convert to sdp string failed.");
                return;
            }
            local_sdp_ = sdp;
            // set local description
            peer_conn_->SetLocalDescription(set_local_sdp_callback_.get(), desc);
        });

        create_sess_callback_->SetOnCreateSdpFailedCallback([=, this](const std::string& msg) {
            LOGE("Create sdp failed: {}", msg);
        });

        // set local sdp callback
        set_local_sdp_callback_->SetSdpSuccessCallback([=, this]() {
            LOGI("Set local desc success, will send to remote.");
            if (local_rtc_mode_) {
                // local mode: non-trickle, wait for ice gathering complete,
                // the final sdp(with candidates embedded) is reported in OnIceGatheringComplete()
                return;
            }
            if (local_sdp_set_cbk_) {
                local_sdp_set_cbk_(local_sdp_);
            }
        });

        set_local_sdp_callback_->SetSdpFailedCallback([=, this](const std::string& msg) {
            LOGI("Set local desc failed: {}", msg);
        });

        // set remote sdp callback
        set_remote_sdp_callback_->SetSdpSuccessCallback([=, this]() {
            LOGI("Set remote desc success.");

            already_set_answer_sdp_ = true;

            // cached ice ?
            this->SendCachedIces();
        });

        set_remote_sdp_callback_->SetSdpFailedCallback([=, this](const std::string& msg) {
            LOGI("Set remote desc failed: {}", msg);
        });

        // PeerConnection
        peer_callback_->SetOnIceCallback([=, this](const std::string& ice, const std::string& mid, int sdp_mline_idx) {
            if (local_rtc_mode_) {
                // local mode: candidates are embedded in the final offer sdp, no trickle needed
                return;
            }
            // TODO
            if (already_set_answer_sdp_) {
                // send it
                std::lock_guard<std::mutex> guard(ice_mtx_);
                if (local_ice_cbk_) {
                    local_ice_cbk_(ice, mid, sdp_mline_idx);
                }
            }
            else {
                // cache it
                cached_ices_.push_back(CachedIce {
                    .ice_ = ice,
                    .mid_ = mid,
                    .sdp_mline_index_ = sdp_mline_idx,
                });
            }
        });

        CreatePeerConnectionFactory();
        CreatePeerConnection();
        auto end = TimeUtil::GetCurrentTimestamp();
        LOGI("========AFTER RTC INIT=========, used: {}ms", (end-beg));
        return true;
    }

    bool RtcConnection::Exit() {
        if (work_thread_) {
            work_thread_->Exit();
        }
        return true;
    }

    static void CreateSomeMediaDeps(PeerConnectionFactoryDependencies& media_deps) {
        media_deps.adm = AudioDeviceModule::CreateForTest(
                AudioDeviceModule::kDummyAudio, media_deps.task_queue_factory.get());
        media_deps.audio_encoder_factory =
                webrtc::CreateAudioEncoderFactory<webrtc::AudioEncoderOpus>();
        media_deps.audio_decoder_factory =
                webrtc::CreateAudioDecoderFactory<webrtc::AudioDecoderOpus>();
        media_deps.video_encoder_factory =
                std::make_unique<VideoEncoderFactoryTemplate<
                                 LibvpxVp8EncoderTemplateAdapter, LibvpxVp9EncoderTemplateAdapter,
                        OpenH264EncoderTemplateAdapter, LibaomAv1EncoderTemplateAdapter>>();
        media_deps.video_decoder_factory =
                std::make_unique<VideoDecoderFactoryTemplate<
                                 LibvpxVp8DecoderTemplateAdapter, LibvpxVp9DecoderTemplateAdapter,
                        OpenH264DecoderTemplateAdapter, Dav1dDecoderTemplateAdapter>>();
        media_deps.audio_processing = webrtc::AudioProcessingBuilder().Create();
    }

    void RtcConnection::CreatePeerConnectionFactory() {
        configuration_.sdp_semantics = webrtc::SdpSemantics::kUnifiedPlan;
        configuration_.media_config.video.periodic_alr_bandwidth_probing = true;

        ApplyIceServersJson(ice_servers_json_, false);

        network_thread_ = rtc::Thread::CreateWithSocketServer();
        network_thread_->Start();
        worker_thread_ = rtc::Thread::Create();
        worker_thread_->Start();
        sig_thread_ = rtc::Thread::Create();
        sig_thread_->Start();

        webrtc::PeerConnectionFactoryDependencies media_deps;
        media_deps.task_queue_factory = webrtc::CreateDefaultTaskQueueFactory();
        CreateSomeMediaDeps(media_deps);

        // encoded-sink mode: real decoding is done by the sdk's FFmpegVulkanDecoder
        // chain, swap in a null decoder so the built-in one doesn't software-decode
        // every frame for nothing. SDP negotiation is unchanged(same H264 formats).
        if (local_rtc_mode_ && encoded_video_frame_cbk_) {
            media_deps.video_decoder_factory = std::make_unique<RtcNullVideoDecoderFactory>();
            LOGI("Rtc local encoded-sink mode, null video decoder factory installed.");
        }

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
            //exit(EXIT_FAILURE);
            // TODO:
        }
    }

    void RtcConnection::CreatePeerConnection() {
        configuration_.port_allocator_config.min_port = 60430;
        configuration_.port_allocator_config.max_port = 60490;
        auto result = peer_conn_factory_->
                CreatePeerConnectionOrError(configuration_, webrtc::PeerConnectionDependencies(peer_callback_.get()));
        if (!result.ok()) {
            LOGE("Create peer connection failed: {}", result.error().message());
            return;
        }
        LOGI("Create peer connection success.");
        auto peer_conn = result.value();
        this->peer_conn_ = peer_conn;
        // new peer connection: drop encoded sinks bound to the previous one's tracks.
        // safe here - the old peer connection was released by the reassignment above,
        // its tracks(and their sources) are gone before the sinks are freed
        encoded_sinks_.clear();
        video_track_arrive_count_ = 0;

        if (!file_transfer_only_) {
            webrtc::DataChannelInit config;
            config.ordered = true;
            config.reliable = true;
            auto ch_name = "media_data_channel";
            RTCErrorOr<rtc::scoped_refptr<DataChannelInterface>> r_dc = peer_conn->CreateDataChannelOrError(
                    ch_name, &config);
            if (!r_dc.ok()) {
                LOGE("create datachannel error: {}", r_dc.error().message());
            } else {
                auto ch = r_dc.value();
                ch->AddRef();
                media_data_channel_ = std::make_shared<RtcDataChannel>(this, ch, ch_name);
                media_data_channel_->SetOnDataCallback([=, this](std::shared_ptr<Data> msg) {
                    //LOGI("===> OnDataCallback: {}", msg.size());
                    if (media_msg_cbk_) {
                        media_msg_cbk_(msg);
                    }
                });
            }
        }
        {
            webrtc::DataChannelInit config;
            config.ordered = true;
            config.reliable = true;
            auto ch_name = "ft_data_channel";
            RTCErrorOr<rtc::scoped_refptr<DataChannelInterface>> r_dc = peer_conn->CreateDataChannelOrError(
                    ch_name, &config);
            if (!r_dc.ok()) {
                LOGE("create datachannel error: {}", r_dc.error().message());
            } else {
                auto ch = r_dc.value();
                ft_data_channel_ = std::make_shared<RtcDataChannel>(this, ch, ch_name);
                ft_data_channel_->SetOnDataCallback([=, this](std::shared_ptr<Data> msg) {
                    if (ft_msg_cbk_) {
                        ft_msg_cbk_(msg);
                    }
                });
            }
        }
        if (!file_transfer_only_) {
            // dedicated input channel(keyboard/mouse), reliable + ordered:
            // losing a key-down/up or mouse click is unacceptable, so NOT unreliable.
            // the point of the separate channel is the render side fast path:
            // it dispatches input_data_channel via CallbackEventDirectly instead of
            // queueing on the plugin work thread (rtc_server.cpp).
            webrtc::DataChannelInit config;
            config.ordered = true;
            config.reliable = true;
            auto ch_name = "input_data_channel";
            RTCErrorOr<rtc::scoped_refptr<DataChannelInterface>> r_dc = peer_conn->CreateDataChannelOrError(
                    ch_name, &config);
            if (!r_dc.ok()) {
                LOGE("create datachannel error: {}", r_dc.error().message());
            } else {
                auto ch = r_dc.value();
                ch->AddRef();
                input_data_channel_ = std::make_shared<RtcDataChannel>(this, ch, ch_name);
                // render never replies on this channel, no data callback needed
            }
        }

        auto options = webrtc::PeerConnectionInterface::RTCOfferAnswerOptions();
        options.offer_to_receive_audio = !file_transfer_only_;
        // Unified Plan accepts only 0/1 here; >=1 means "at least one recv-capable
        // video m-line". Existing recvonly transceivers(added below) satisfy that,
        // no extra auto m-line is created. Setting it to 0 would clamp ALL video
        // m-lines to sendonly/inactive in the offer.
        options.offer_to_receive_video = file_transfer_only_ ? 0 : 1;
        if (!file_transfer_only_ && local_rtc_mode_ && video_track_count_ > 1) {
            // multi-track: one recvonly video m-line per monitor(track).
            // extra m-lines beyond offer_to_receive_video=1 must come from
            // explicit transceivers.
            for (int i = 0; i < video_track_count_; ++i) {
                webrtc::RtpTransceiverInit transceiver_init;
                transceiver_init.direction = webrtc::RtpTransceiverDirection::kRecvOnly;
                auto r = peer_conn->AddTransceiver(cricket::MediaType::MEDIA_TYPE_VIDEO, transceiver_init);
                if (!r.ok()) {
                    LOGE("AddTransceiver(video #{}) failed: {}", i, r.error().message());
                }
            }
        }
        //auto create_session_observer = this->peer_callback_.get();
        peer_conn->CreateOffer(create_sess_callback_.get(), options);

        std::string typeStr = "offer";
        absl::optional<webrtc::SdpType> type_maybe = webrtc::SdpTypeFromString(typeStr);
        if (!type_maybe) {

        }

    }

    bool RtcConnection::OnRemoteSdp(const std::string &sdp) {
        LOGI("OnRemoteSdp, Will set remote answer sdp");
        webrtc::SdpParseError error;
        webrtc::SessionDescriptionInterface* session_description(webrtc::CreateSessionDescription("answer", sdp, &error));
        peer_conn_->SetRemoteDescription(set_remote_sdp_callback_.get(), session_description);
        already_set_answer_sdp_ = error.line.empty();
        if (already_set_answer_sdp_) {
            LOGI("SetRemoteAnswerSdp success, may send ices.");
            //this->SendCachedIces();
        } else {
            LOGE("SetRemoteAnswerSdp failed, error line: {}, desc: {}", error.line, error.description);
            return false;
        }
        return true;
    }

    bool RtcConnection::OnRemoteIce(const std::string &ice, const std::string &mid, int32_t sdp_mline_index) {
        LOGI("OnRemoteIce, Will set remote ice: {}", ice);
        webrtc::SdpParseError error;
        std::unique_ptr<webrtc::IceCandidateInterface>
                candidate(webrtc::CreateIceCandidate(mid, sdp_mline_index, ice, &error));
        if (!error.line.empty()) {
            LOGE("Create IceCandidate failed: {} - {}", error.line, error.description);
            return false;
        }
        peer_conn_->AddIceCandidate(std::move(candidate), [](webrtc::RTCError error) {
            if (error.ok()) {
                LOGI("AddIceCandidate success.");
            } else {
                LOGE("AddIceCandidate failed: {}", error.message());
            }
        });
        return false;
    }

    void RtcConnection::SendCachedIces() {
        if (local_ice_cbk_) {
            std::lock_guard<std::mutex> guard(ice_mtx_);
            for (const auto& ci : cached_ices_) {
                local_ice_cbk_(ci.ice_, ci.mid_, ci.sdp_mline_index_);
            }
        }
    }

    void RtcConnection::PostMediaMessage(std::shared_ptr<Data> msg) {
        if (media_data_channel_) {
            media_data_channel_->SendData(msg);
        }
    }

    void RtcConnection::PostFtMessage(std::shared_ptr<Data> msg) {
        if (ft_data_channel_) {
            ft_data_channel_->SendData(msg);
        }
    }

    void RtcConnection::PostInputMessage(std::shared_ptr<Data> msg) {
        if (input_data_channel_) {
            input_data_channel_->SendData(msg);
        }
    }

    int64_t RtcConnection::GetQueuingMediaMsgCount() {
        return media_data_channel_ ? media_data_channel_->GetPendingDataCount() : 0;
    }

    int64_t RtcConnection::GetQueuingFtMsgCount() {
        return ft_data_channel_ ? ft_data_channel_->GetPendingDataCount() : 0;
    }

    bool RtcConnection::HasEnoughBufferForQueuingMediaMessages() {
        return media_data_channel_ && media_data_channel_->HasEnoughBufferForQueuingMessages();
    }

    bool RtcConnection::HasEnoughBufferForQueuingFtMessages() {
        return ft_data_channel_ && ft_data_channel_->HasEnoughBufferForQueuingMessages();
    }

    bool RtcConnection::IsMediaChannelReady() {
        return media_data_channel_ && media_data_channel_->IsConnected();
    }

    bool RtcConnection::IsFtChannelReady() {
        return ft_data_channel_ && ft_data_channel_->IsConnected();
    }

    bool RtcConnection::IsInputChannelReady() {
        return input_data_channel_ && input_data_channel_->IsConnected();
    }

    void RtcConnection::On16msTimeout() {
        if (ft_data_channel_) {
            ft_data_channel_->On16msTimeout();
        }
        if (media_data_channel_) {
            media_data_channel_->On16msTimeout();
        }
        if (!local_rtc_mode_ && ++stats_tick_ >= 60) {
            stats_tick_ = 0;
            RequestStats();
        }
    }

    void RtcConnection::PostWorkTask(std::function<void()>&& task) {
        this->work_thread_->Post(std::move(task));
    }

    void RtcConnection::SetLocalRtcMode(bool on) {
        local_rtc_mode_ = on;
    }

    void RtcConnection::SetIceServersJson(const std::string& json) {
        ice_servers_json_ = json;
    }

    bool RtcConnection::ApplyIceServersJson(const std::string& json, bool active) {
        configuration_.servers.clear();
        if (local_rtc_mode_) {
            return true;
        }
        try {
            const auto config = nlohmann::json::parse(json);
            for (const auto& entry : config.value("ice_servers", nlohmann::json::array())) {
                const auto username = entry.value("username", "");
                const auto credential = entry.value("credential", "");
                for (const auto& url : entry.value("urls", std::vector<std::string>{})) {
                    auto server = webrtc::PeerConnectionInterface::IceServer();
                    server.uri = url;
                    server.username = username;
                    server.password = credential;
                    server.tls_cert_policy =
                        webrtc::PeerConnectionInterface::TlsCertPolicy::kTlsCertPolicySecure;
                    configuration_.servers.push_back(std::move(server));
                }
            }
            LOGI("Configured {} ICE server URLs for full RTC{}", configuration_.servers.size(),
                 active ? " restart" : "");
        }
        catch (const std::exception& error) {
            LOGE("Invalid RTC ICE configuration: {}", error.what());
            return false;
        }
        if (active && peer_conn_) {
            const auto result = peer_conn_->SetConfiguration(configuration_);
            if (!result.ok()) {
                LOGE("SetConfiguration failed before ICE restart: {}", result.message());
                return false;
            }
        }
        ice_servers_json_ = json;
        return true;
    }

    bool RtcConnection::RestartIce(const std::string& json) {
        if (local_rtc_mode_ || !peer_conn_ || !ApplyIceServersJson(json, true)) {
            return false;
        }
        already_set_answer_sdp_ = false;
        {
            std::lock_guard<std::mutex> guard(ice_mtx_);
            cached_ices_.clear();
        }
        peer_conn_->RestartIce();
        auto options = webrtc::PeerConnectionInterface::RTCOfferAnswerOptions();
        options.offer_to_receive_audio = !file_transfer_only_;
        options.offer_to_receive_video = file_transfer_only_ ? 0 : 1;
        options.ice_restart = true;
        peer_conn_->CreateOffer(create_sess_callback_.get(), options);
        LOGI("SetConfiguration succeeded; ICE restart offer requested");
        return true;
    }

    void RtcConnection::RequestStats() {
        if (!peer_conn_ || !stats_json_cbk_) {
            return;
        }
        rtc::scoped_refptr<webrtc::RTCStatsCollectorCallback> callback(
            new rtc::RefCountedObject<RtcStatsJsonCallback>(stats_json_cbk_));
        peer_conn_->GetStats(callback.get());
    }

    void RtcConnection::OnIceGatheringComplete() {
        if (!local_rtc_mode_) {
            return;
        }
        if (!peer_conn_) {
            LOGE("OnIceGatheringComplete, peer connection is null.");
            return;
        }
        auto local_desc = peer_conn_->local_description();
        if (!local_desc) {
            LOGE("OnIceGatheringComplete, local description is null.");
            return;
        }
        std::string sdp;
        if (!local_desc->ToString(&sdp)) {
            LOGE("OnIceGatheringComplete, convert local desc to sdp string failed.");
            return;
        }
        local_sdp_ = sdp;
        LOGI("Ice gathering complete, report the final offer sdp, size: {}", sdp.size());
        if (local_sdp_set_cbk_) {
            local_sdp_set_cbk_(sdp);
        }
    }

    void RtcConnection::OnVideoTrack(webrtc::VideoTrackInterface* track) {
        if (!track) {
            return;
        }
        // encoded-sink mode: consume the pre-decode H264 via AddEncodedSink,
        // the sdk decodes it with its own FFmpegVulkanDecoder chain.
        if (local_rtc_mode_ && encoded_video_frame_cbk_) {
            // track id "video_track_{i}" is assigned by the render in multi-track mode;
            // anything else(old render's single track) maps to index 0 by arrival order
            int track_index = video_track_arrive_count_++;
            const auto track_id = track->id();
            const std::string prefix = "video_track_";
            if (track_id.size() > prefix.size() && track_id.starts_with(prefix)) {
                auto tail = track_id.substr(prefix.size());
                if (std::all_of(tail.begin(), tail.end(), ::isdigit)) {
                    track_index = std::atoi(tail.c_str());
                }
            }
            LOGI("OnVideoTrack(encoded sink), id: {}, mapped index: {}", track_id, track_index);
            auto sink = RtcEncodedFrameSink::Make(track_index);
            sink->SetOnFrameCallback([=, this](int idx, bool key, int w, int h, std::shared_ptr<Data> encoded) {
                if (encoded_video_frame_cbk_) {
                    encoded_video_frame_cbk_(idx, key, w, h, encoded);
                }
            });
            // AddEncodedSink also triggers a key frame request towards the encoder,
            // so the sdk decode chain starts with an IDR
            track->GetSource()->AddEncodedSink(sink.get());
            encoded_sinks_.push_back(sink);
            return;
        }
        LOGI("OnVideoTrack, will attach the video sink.");
        if (!video_sink_) {
            video_sink_ = RtcVideoSink::Make();
            video_sink_->SetOnFrameCallback([=, this](int w, int h, std::shared_ptr<Data> i420) {
                if (video_frame_cbk_) {
                    video_frame_cbk_(w, h, i420);
                }
            });
        }
        track->AddOrUpdateSink(video_sink_.get(), rtc::VideoSinkWants());
    }

    void RtcConnection::OnAudioTrack(webrtc::AudioTrackInterface* track) {
        if (!track) {
            return;
        }
        // When the existing ABI callback is installed, tap decoded PCM and hand
        // it to the SDK AudioPlayer in both standard and local RTC modes. The
        // plug-in keeps its original object and pointer lifetime model.
        if (audio_data_cbk_) {
            LOGI("OnAudioTrack(audio sink), id: {}", track->id());
            if (audio_sink_) {
                track->RemoveSink(audio_sink_.get());
            }
            audio_sink_ = RtcAudioSink::Make();
            audio_sink_->SetOnDataCallback([=, this](std::shared_ptr<Data> pcm, int sample_rate, int channels) {
                if (audio_data_cbk_) {
                    audio_data_cbk_(pcm, sample_rate, channels);
                }
            });
            track->AddSink(audio_sink_.get());
        }
    }

    void RtcConnection::OnIceStateChanged(int state) {
        if (ice_state_cbk_) {
            ice_state_cbk_(state);
        }
    }

}
