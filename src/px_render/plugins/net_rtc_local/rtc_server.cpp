//
// Created by hy on 2024/4/25.
//

#include "rtc_server.h"
#include "peer_callback.h"
#include "rtc_local_plugin.h"
#include "px_render/plugin_interface/px_plugin_events.h"
#include "rtc_data_channel.h"
#include "px_common_new/log.h"
#include "rtc_video_encoder_factory.h"
#include "video_source_impl.h"
#include "audio_source_impl.h"
#include "remote_audio_sink.h"
#include "px_common_new/data.h"
#include "px_common_new/time_util.h"
#include "px_common_new/privacy_log.h"
#include "rtc_base/ref_counted_object.h"
#include "api/stats/rtc_stats_collector_callback.h"
#include <array>
#include <atomic>
#include <chrono>
#include <functional>
#include <optional>
#include <format>
#include <mutex>
#include <nlohmann/json.hpp>
#include <thread>

using namespace webrtc;

namespace px
{
    namespace {
        // A clocked, discard-only ADM. WebRTC's kDummyAudio ADM never asks the
        // receive mixer for playout data, so decoded browser microphone frames
        // cannot reach AudioTrackSinkInterface. This module supplies that 10ms
        // clock without opening a Windows audio device; RemoteAudioSink owns
        // the only authorization-gated physical playout path.
        class PullAudioDeviceModule : public webrtc::AudioDeviceModule {
        public:
            PullAudioDeviceModule() : playout_(std::make_shared<PlayoutState>()) {}
            ~PullAudioDeviceModule() override { StopPlayout(); }

            int32_t ActiveAudioLayer(AudioLayer* layer) const override {
                if (layer) *layer = kDummyAudio;
                return 0;
            }
            int32_t RegisterAudioCallback(webrtc::AudioTransport* callback) override {
                std::scoped_lock lock(playout_->callback_mutex);
                if (callback) {
                    playout_->callback = std::ref(*callback);
                }
                else {
                    playout_->callback.reset();
                }
                LOGI("PullAudioDeviceModule audio callback {}",
                     callback ? "registered" : "cleared");
                return 0;
            }
            int32_t Init() override { initialized_ = true; return 0; }
            int32_t Terminate() override { StopPlayout(); initialized_ = false; return 0; }
            bool Initialized() const override { return initialized_; }
            int16_t PlayoutDevices() override { return 1; }
            int16_t RecordingDevices() override { return 0; }
            int32_t PlayoutDeviceName(uint16_t, char name[kAdmMaxDeviceNameSize],
                                      char guid[kAdmMaxGuidSize]) override {
                if (name) name[0] = '\0';
                if (guid) guid[0] = '\0';
                return 0;
            }
            int32_t RecordingDeviceName(uint16_t, char name[kAdmMaxDeviceNameSize],
                                        char guid[kAdmMaxGuidSize]) override {
                if (name) name[0] = '\0';
                if (guid) guid[0] = '\0';
                return -1;
            }
            int32_t SetPlayoutDevice(uint16_t) override { return 0; }
            int32_t SetPlayoutDevice(WindowsDeviceType) override { return 0; }
            int32_t SetRecordingDevice(uint16_t) override { return -1; }
            int32_t SetRecordingDevice(WindowsDeviceType) override { return -1; }
            int32_t PlayoutIsAvailable(bool* available) override {
                if (available) *available = true;
                return 0;
            }
            int32_t InitPlayout() override { playout_initialized_ = true; return 0; }
            bool PlayoutIsInitialized() const override { return playout_initialized_; }
            int32_t RecordingIsAvailable(bool* available) override {
                if (available) *available = false;
                return 0;
            }
            int32_t InitRecording() override { return -1; }
            bool RecordingIsInitialized() const override { return false; }
            int32_t StartPlayout() override {
                const auto playout = playout_;
                if (playout->playing.exchange(true)) return 0;
                LOGI("PullAudioDeviceModule playout clock started");
                playout_thread_ = std::thread([playout]() {
                    auto next = std::chrono::steady_clock::now();
                    std::array<int16_t, 480> samples{};
                    while (playout->playing) {
                        next += std::chrono::milliseconds(10);
                        {
                            std::scoped_lock lock(playout->callback_mutex);
                            if (playout->callback) {
                                size_t samples_out = 0;
                                int64_t elapsed_ms = 0;
                                int64_t ntp_ms = 0;
                                playout->callback->get().NeedMorePlayData(
                                    480, sizeof(int16_t), 1, 48'000,
                                    samples.data(), samples_out, &elapsed_ms, &ntp_ms);
                                const auto count = ++playout->pull_count;
                                if (count == 1 || count % 3000 == 0) {
                                    LOGI("PullAudioDeviceModule pulled 10ms #{} samples_out={}",
                                         count, samples_out);
                                }
                            }
                        }
                        std::this_thread::sleep_until(next);
                    }
                });
                return 0;
            }
            int32_t StopPlayout() override {
                if (!playout_->playing.exchange(false)) return 0;
                if (playout_thread_.joinable()) playout_thread_.join();
                LOGI("PullAudioDeviceModule playout clock stopped after {} pulls",
                     playout_->pull_count.load());
                return 0;
            }
            bool Playing() const override { return playout_->playing; }
            int32_t StartRecording() override { return -1; }
            int32_t StopRecording() override { return 0; }
            bool Recording() const override { return false; }
            int32_t InitSpeaker() override { return 0; }
            bool SpeakerIsInitialized() const override { return true; }
            int32_t InitMicrophone() override { return -1; }
            bool MicrophoneIsInitialized() const override { return false; }
            int32_t SpeakerVolumeIsAvailable(bool* available) override { return Unavailable(available); }
            int32_t SetSpeakerVolume(uint32_t) override { return -1; }
            int32_t SpeakerVolume(uint32_t* volume) const override { return Zero(volume); }
            int32_t MaxSpeakerVolume(uint32_t* volume) const override { return Zero(volume); }
            int32_t MinSpeakerVolume(uint32_t* volume) const override { return Zero(volume); }
            int32_t MicrophoneVolumeIsAvailable(bool* available) override { return Unavailable(available); }
            int32_t SetMicrophoneVolume(uint32_t) override { return -1; }
            int32_t MicrophoneVolume(uint32_t* volume) const override { return Zero(volume); }
            int32_t MaxMicrophoneVolume(uint32_t* volume) const override { return Zero(volume); }
            int32_t MinMicrophoneVolume(uint32_t* volume) const override { return Zero(volume); }
            int32_t SpeakerMuteIsAvailable(bool* available) override { return Unavailable(available); }
            int32_t SetSpeakerMute(bool) override { return -1; }
            int32_t SpeakerMute(bool* enabled) const override { return False(enabled); }
            int32_t MicrophoneMuteIsAvailable(bool* available) override { return Unavailable(available); }
            int32_t SetMicrophoneMute(bool) override { return -1; }
            int32_t MicrophoneMute(bool* enabled) const override { return False(enabled); }
            int32_t StereoPlayoutIsAvailable(bool* available) const override { return Unavailable(available); }
            int32_t SetStereoPlayout(bool) override { return -1; }
            int32_t StereoPlayout(bool* enabled) const override { return False(enabled); }
            int32_t StereoRecordingIsAvailable(bool* available) const override { return Unavailable(available); }
            int32_t SetStereoRecording(bool) override { return -1; }
            int32_t StereoRecording(bool* enabled) const override { return False(enabled); }
            int32_t PlayoutDelay(uint16_t* delay_ms) const override {
                if (delay_ms) *delay_ms = 10;
                return 0;
            }
            bool BuiltInAECIsAvailable() const override { return false; }
            bool BuiltInAGCIsAvailable() const override { return false; }
            bool BuiltInNSIsAvailable() const override { return false; }
            int32_t EnableBuiltInAEC(bool) override { return -1; }
            int32_t EnableBuiltInAGC(bool) override { return -1; }
            int32_t EnableBuiltInNS(bool) override { return -1; }

        private:
            struct PlayoutState {
                std::mutex callback_mutex;
                std::optional<std::reference_wrapper<webrtc::AudioTransport>> callback;
                std::atomic_bool playing{false};
                std::atomic_uint64_t pull_count{0};
            };

            static int32_t Unavailable(bool* value) { if (value) *value = false; return 0; }
            static int32_t False(bool* value) { if (value) *value = false; return 0; }
            static int32_t Zero(uint32_t* value) { if (value) *value = 0; return 0; }

            std::shared_ptr<PlayoutState> playout_;
            std::thread playout_thread_;
            std::atomic_bool initialized_ = false;
            std::atomic_bool playout_initialized_ = false;
        };

        class VoiceInboundStatsCallback
            : public webrtc::RTCStatsCollectorCallback {
        public:
            void OnStatsDelivered(
                const rtc::scoped_refptr<const webrtc::RTCStatsReport>& report) override {
                if (!report) return;
                try {
                    const auto root = nlohmann::json::parse(report->ToJson());
                    const auto inspect = [](const nlohmann::json& stat) {
                        if (!stat.is_object() || stat.value("type", "") != "inbound-rtp") return;
                        const auto kind = stat.value("kind", stat.value("mediaType", ""));
                        if (kind == "audio") {
                            LOGI("Voice inbound RTC stats packets={} bytes={} lost={} jitter={} samples={} emitted={} concealed={} audio_level={} codec={}",
                                 stat.value("packetsReceived", 0ULL),
                                 stat.value("bytesReceived", 0ULL),
                                 stat.value("packetsLost", 0LL),
                                 stat.value("jitter", 0.0),
                                 stat.value("totalSamplesReceived", 0ULL),
                                 stat.value("jitterBufferEmittedCount", 0ULL),
                                 stat.value("concealedSamples", 0ULL),
                                 stat.value("audioLevel", 0.0),
                                 stat.value("codecId", ""));
                        }
                    };
                    if (root.is_array()) {
                        for (const auto& stat : root) inspect(stat);
                    } else if (root.is_object()) {
                        for (const auto& [_, stat] : root.items()) inspect(stat);
                    }
                } catch (const std::exception& error) {
                    LOGW("Voice inbound RTC stats parse failed: {}", error.what());
                }
            }
        };

        std::optional<uint64_t> ReadVarint(const std::string& data, size_t& offset) {
            uint64_t value = 0;
            for (int shift = 0; shift < 64 && offset < data.size(); shift += 7) {
                const auto byte = static_cast<uint8_t>(data[offset++]);
                value |= static_cast<uint64_t>(byte & 0x7f) << shift;
                if ((byte & 0x80) == 0) {
                    return value;
                }
            }
            return std::nullopt;
        }

        // Read only field 10(Message.type) from the protobuf envelope. This
        // intentionally avoids pulling a second Abseil/Protobuf ABI into the
        // WebRTC plugin, which uses WebRTC's bundled Abseil.
        std::optional<int> ExtractMessageType(const std::string& data) {
            size_t offset = 0;
            bool has_hello_payload = false;
            while (offset < data.size()) {
                const auto tag = ReadVarint(data, offset);
                if (!tag || *tag == 0) {
                    return std::nullopt;
                }
                const auto field = static_cast<uint32_t>(*tag >> 3);
                const auto wire = static_cast<uint32_t>(*tag & 7);
                if (field == 10 && wire == 0) {
                    const auto value = ReadVarint(data, offset);
                    return value ? std::optional<int>(static_cast<int>(*value)) : std::nullopt;
                }
                // kHello is enum value 0. Protobuf omits a scalar field whose
                // value is the default, so a valid Hello envelope has no field
                // 10 and is identified by its field-40 payload instead.
                if (field == 40 && wire == 2) {
                    has_hello_payload = true;
                }
                switch (wire) {
                    case 0:
                        if (!ReadVarint(data, offset)) return std::nullopt;
                        break;
                    case 1:
                        if (offset + 8 > data.size()) return std::nullopt;
                        offset += 8;
                        break;
                    case 2: {
                        const auto length = ReadVarint(data, offset);
                        if (!length || *length > data.size() - offset) return std::nullopt;
                        offset += static_cast<size_t>(*length);
                        break;
                    }
                    case 5:
                        if (offset + 4 > data.size()) return std::nullopt;
                        offset += 4;
                        break;
                    default:
                        return std::nullopt;
                }
            }
            return has_hello_payload ? std::optional<int>(0) : std::nullopt;
        }

        bool IsClipboardMessage(const int type) {
            return type == 160 || type == 161 || type == 349
                || type == 350 || type == 351 || type == 360;
        }

        bool IsInteractiveControlMessage(const int type) {
            switch (type) {
                case 50:  // key
                case 60:  // mouse
                case 80:  // gamepad
                case 170: // switch monitor
                case 190: // switch work mode
                case 200: // change resolution
                case 230: // insert key frame
                case 328: // lock device
                case 329: // stop render
                case 330: // ctrl-alt-delete
                case 340: // update desktop
                case 341: // hard update desktop
                case 460: // full color
                case 470: // start recording
                case 471: // stop recording
                case 480: // modify fps
                case 570: // virtual display management
                    return true;
                default:
                    return false;
            }
        }
    }

    std::shared_ptr<RtcServer> RtcServer::Make(
        const std::shared_ptr<RtcLocalPluginRuntime>& runtime) {
        return std::make_shared<RtcServer>(runtime);
    }

    RtcServer::RtcServer(
        const std::shared_ptr<RtcLocalPluginRuntime>& runtime)
        : runtime_(runtime) {}

    std::shared_ptr<PxPluginContext> RtcServer::GetPluginContext() const {
        return runtime_ ? runtime_->GetContext() : nullptr;
    }

    void RtcServer::DispatchEvent(
        const std::shared_ptr<PxPluginBaseEvent>& event) const {
        if (runtime_) {
            runtime_->QueueEvent(event);
        }
    }

    void RtcServer::QueueEvent(
        const std::shared_ptr<PxPluginBaseEvent>& event) const {
        DispatchEvent(event);
    }

    void RtcServer::RequestEncodedIdr(const std::string& mon_name) {
        runtime_->InsertIdr(mon_name);
    }

    uint64_t RtcServer::GetLatestEncodedSeq(const std::string& mon_name) {
        return runtime_->GetLatestEncodedSeq(mon_name);
    }

    size_t RtcServer::GetCachedFrameCount(
        const std::string& mon_name, uint64_t after_seq) {
        return runtime_->GetCachedFrameCount(mon_name, after_seq);
    }

    std::shared_ptr<RtcLocalEncodedVideoFrame>
    RtcServer::ReadNextEncodedVideoFrame(
        const std::string& mon_name, uint64_t after_seq, bool& out_gap) {
        return runtime_->ReadNextEncodedVideoFrame(mon_name, after_seq, out_gap);
    }

    bool RtcServer::WaitForEncodedFrame(
        const std::string& mon_name, uint64_t after_seq, int timeout_ms) {
        return runtime_->WaitForEncodedFrame(mon_name, after_seq, timeout_ms);
    }

    void RtcServer::NotifyTerminal() {
        if (runtime_) {
            runtime_->NotifyTerminal(conn_id_, shared_from_this());
        }
    }

    bool RtcServer::Start(const std::string& stream_id, const std::string& offer_sdp,
                          PxLocalRtcSessionRole session_role,
                          const std::string& ice_config_json) {
        this->stream_id_ = stream_id;
        this->offer_sdp_ = offer_sdp;
        this->ice_config_json_ = ice_config_json;
        this->standard_rtc_ = !ice_config_json.empty();
        this->wall_observer_ = session_role == PxLocalRtcSessionRole::kWallObserver;
        this->created_timestamp_ms_ = (int64_t)TimeUtil::GetCurrentTimestamp();
        webrtc::field_trial::InitFieldTrialsFromString("");
        rtc::LogMessage::LogToDebug(rtc::LS_ERROR);

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
            webrtc::PeerConnectionInterface::RTCOfferAnswerOptions options;
            options.offer_to_receive_audio = !server->IsWallObserver() && server->HasPermission("audio");
            options.offer_to_receive_video = server->HasPermission("view");
            LOGI("Will create answer sdp.");
            server->peer_conn_->CreateAnswer(
                server->create_answer_callback_.get(), options);
        });

        set_remote_offer_sdp_callback_->SetSdpFailedCallback([](const std::string& m) {
            LOGE("Set remote sdp failed: {}", m);
        });

        // set local answer sdp
        set_local_answer_sdp_callback_->SetSdpSuccessCallback([weak_server]() {
            const auto server = weak_server.lock();
            if (!server) {
                return;
            }
            LOGI("Set local answer sdp success.");
            // Standard RTC always trickles ICE over Console Relay.  Returning
            // the answer as soon as SetLocalDescription succeeds is required
            // for TURN/TCP: an unreachable UDP TURN URL can keep libwebrtc's
            // gathering state open for tens of seconds while TCP candidates
            // are still perfectly usable.  The legacy direct HTTP flow has no
            // trickle channel, so it continues to wait for gathering complete.
            if (server->standard_rtc_ && server->peer_conn_
                && server->peer_conn_->local_description()) {
                std::string answer_sdp;
                if (!server->peer_conn_->local_description()->ToString(&answer_sdp)) {
                    LOGE("Get local standard RTC answer failed");
                    if (server->answer_sdp_callback_) {
                        server->answer_sdp_callback_("");
                    }
                    return;
                }
                server->answer_sdp_ = answer_sdp;
                LOGI("Get standard RTC answer success before ICE gathering completes");
                if (server->answer_sdp_callback_) {
                    server->answer_sdp_callback_(answer_sdp);
                }
            }
        });

        set_local_answer_sdp_callback_->SetSdpFailedCallback([](const std::string& m) {
            LOGI("Set local answer sdp failed:{}", m);
        });

        // create answer sdp callback
        create_answer_callback_->SetOnCreateSdpSuccessCallback([weak_server](webrtc::SessionDescriptionInterface* desc) { // NOLINT(gammaray-raw-pointer-boundary): libwebrtc SDP callback ABI
            if (const auto server = weak_server.lock()) {
                server->peer_conn_->SetLocalDescription(
                    server->set_local_answer_sdp_callback_.get(), desc);
            }
        });

        create_answer_callback_->SetOnCreateSdpFailedCallback([weak_server](const std::string& m) {
            LOGE("Create answer sdp failed: {}", m);
            if (const auto server = weak_server.lock();
                server && server->answer_sdp_callback_) {
                server->answer_sdp_callback_("");
            }
        });

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
            // A wall observer is receive-only. Even a crafted offer must not
            // obtain an input, file-transfer or protocol channel.
            if (server->IsWallObserver()) {
                LOGW("Ignore data channel from wall observer: {}", name);
                ch->Close();
                return;
            }
            if (name == "media_data_channel") {
                if (server->capability_enforced_ && !server->HasPermission("view")) {
                    LOGW("Close media channel: ticket grants file-only access");
                    ch->Close();
                    return;
                }
                server->media_data_channel_ = std::make_shared<RtcDataChannel>(name, server, ch);

                // data callback
                server->media_data_channel_->SetOnDataCallback([weak_server](const std::string& data) {
                    const auto locked = weak_server.lock();
                    if (!locked) {
                        return;
                    }
                    if (locked->capability_enforced_) {
                        const auto message_type = ExtractMessageType(data);
                        if (!message_type) {
                            LOGW("Drop malformed media control message from capability session");
                            return;
                        }
                        if (IsClipboardMessage(*message_type) && !locked->HasPermission("clipboard")) {
                            LOGW("Drop clipboard message: ticket does not grant clipboard permission");
                            return;
                        }
                        if (IsInteractiveControlMessage(*message_type) && !locked->HasPermission("input")) {
                            LOGW("Drop interactive control message: ticket does not grant input permission");
                            return;
                        }
                    }
                    auto payload_msg = Data::Make(data.data(), data.size());
                    locked->runtime_->DispatchClientEvent(
                        false, NetChannelType::kMedia, std::move(payload_msg));
                });
            }
            else if (name == "ft_data_channel") {
                if (!server->HasPermission("file")) {
                    LOGW("Close file-transfer channel: ticket does not grant file permission");
                    ch->Close();
                    return;
                }
                server->ft_data_channel_ = std::make_shared<RtcDataChannel>(name, server, ch);

                // data callback
                server->ft_data_channel_->SetOnDataCallback([weak_server](const std::string& data) {
                    const auto locked = weak_server.lock();
                    if (!locked) {
                        return;
                    }
                    auto payload_msg = Data::Make(data.data(), data.size());
                    locked->runtime_->DispatchClientEvent(
                        true, NetChannelType::kFileTransfer,
                        std::move(payload_msg), locked->conn_id_);
                });
            }
            else if (name == "input_data_channel") {
                if (!server->HasPermission("input")) {
                    LOGW("Close input channel: ticket does not grant input permission");
                    ch->Close();
                    return;
                }
                // web client 的低延迟输入通道(unreliable/unordered)。
                // 走 CallbackEventDirectly:跳过 OnClientEventCame→PostWorkTask
                // 排队(插件 work 线程在高负载时可能多等数 ms),在 WebRTC
                // 回调线程直接投递到 event_replayer→SendInput。
                // 鼠标消息解析+SendInput 极轻量,不阻塞网络线程。
                server->input_data_channel_ = std::make_shared<RtcDataChannel>(name, server, ch);

                server->input_data_channel_->SetOnDataCallback([weak_server](const std::string& data) {
                    const auto locked = weak_server.lock();
                    if (!locked) {
                        return;
                    }
                    auto payload_msg = Data::Make(data.data(), data.size());
                    locked->runtime_->DispatchClientEvent(
                        true, NetChannelType::kMedia, std::move(payload_msg));
                });
            }
            else if (name == "ping_data_channel") {
                // 诊断通道:RtcDataChannel::OnMessage 里收到即原样回显
                server->ping_data_channel_ = std::make_shared<RtcDataChannel>(name, server, ch);
            }
        });

        // network state
        peer_callback_->SetOnIceConnectedCallback([weak_server]() {
            if (const auto server = weak_server.lock()) {
                server->ice_connected_ = true;
                server->ice_ever_connected_ = true;
                server->ice_disconnected_since_ms_ = 0;
            }
        });

        // 远端音频轨(浏览器麦克风上行):接收解码后经 WASAPI 播放
        peer_callback_->SetOnAudioTrackCallback([weak_server](rtc::scoped_refptr<webrtc::AudioTrackInterface> track) {
            const auto server = weak_server.lock();
            if (!server || server->IsWallObserver()) {
                return;
            }
            server->OnRemoteAudioTrack(std::move(track));
        });

        peer_callback_->SetOnRemoveAudioTrackCallback([weak_server](rtc::scoped_refptr<webrtc::AudioTrackInterface> track) {
            if (const auto server = weak_server.lock()) {
                server->OnRemoteAudioTrackRemoved(std::move(track));
            }
        });

        peer_callback_->SetOnIceDisConnectedCallback([weak_server]() {
            const auto server = weak_server.lock();
            if (!server) {
                return;
            }
            server->ice_connected_ = false;
            // 记录 Disconnected 起始时刻,On100msTimeout 负责超时判死。
            // 若 ICE 在 5 秒宽限期内恢复为 Connected,回调会清零该标记。
            int64_t expect = 0;
            auto now = (int64_t)TimeUtil::GetCurrentTimestamp();
            server->ice_disconnected_since_ms_.compare_exchange_strong(expect, now);
        });

        // ICE 终态(Failed/Closed):立即置退出标记停止收发,并通知 plugin 将其
        // 从 rtc_servers_ 中清除。注意:此回调运行在 libwebrtc 线程上,不能在此
        // 直接 Exit()(会 Stop/join 当前线程),真正的资源回收由 plugin 延迟 Sweep。
        peer_callback_->SetOnIceTerminalCallback([weak_server]() {
            if (const auto server = weak_server.lock()) {
                server->ice_connected_ = false;
                LOGW("Rtc server terminal, conn_id: {}, will be swept by plugin.", server->conn_id_);
                server->exit_ = true;
                server->EmitClientDisconnectedEvent();
                server->NotifyTerminal();
            }
        });

        peer_callback_->SetOnIceGatherCompletedCallback([weak_server]() {
            const auto server = weak_server.lock();
            if (!server) {
                return;
            }
            LOGI("Ice Gather completed.");
            if (server->standard_rtc_) {
                return;
            }
            std::string answer_sdp;
            if (!server->peer_conn_->local_description()->ToString(&answer_sdp)) {
                LOGE("Get local answer failed");
                if (server->answer_sdp_callback_) {
                    server->answer_sdp_callback_("");
                }
            }
            else {
                server->answer_sdp_ = answer_sdp;
                LOGI("Get answer sdp success");
                if (server->answer_sdp_callback_) {
                    server->answer_sdp_callback_(answer_sdp);
                }
            }
        });

        if (!ice_config_json_.empty() &&
            !ApplyIceConfiguration(ice_config_json_, false)) {
            return false;
        }
        CreatePeerConnectionFactory();
        CreatePeerConnection();
        return peer_conn_ != nullptr;
    }

    bool RtcServer::RestartWithOffer(const std::string& offer_sdp,
                                     const std::string& ice_config_json) {
        if (exit_ || !peer_conn_ || ice_config_json.empty()) {
            return false;
        }
        if (!ApplyIceConfiguration(ice_config_json, true)) {
            return false;
        }
        offer_sdp_ = offer_sdp;
        ice_config_json_ = ice_config_json;
        standard_rtc_ = true;
        disconnect_event_sent_ = false;
        ice_disconnected_since_ms_ = 0;
        peer_conn_->RestartIce();
        LOGI("Apply in-place standard RTC ICE restart, stream={}", stream_id_);
        return SetRemoteOffer(offer_sdp_);
    }

    void RtcServer::CreateSomeMediaDeps(PeerConnectionFactoryDependencies& media_deps) {
        media_deps.adm = rtc::scoped_refptr<webrtc::AudioDeviceModule>(
            new rtc::RefCountedObject<PullAudioDeviceModule>());
        media_deps.audio_encoder_factory =
                webrtc::CreateAudioEncoderFactory<webrtc::AudioEncoderOpus>();
        media_deps.audio_decoder_factory =
                webrtc::CreateAudioDecoderFactory<webrtc::AudioDecoderOpus>();
        // custom encoders
        media_deps.video_encoder_factory =
            std::make_unique<RtcVideoEncoderFactory>(shared_from_this()),
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

        // Use dummy ADM from CreateSomeMediaDeps. nullptr would create Windows
        // CoreAudio ADM whose capture thread races AudioSourceImpl::SendAudio
        // into AudioSendStream::SendAudioData (FatalLog / 0x80000003).
        // Outbound game audio uses AudioSourceImpl; inbound mic playout needs
        // a dedicated WASAPI path when using dummy ADM.
        adm_ = media_deps.adm;
        peer_conn_factory_ = webrtc::CreatePeerConnectionFactory(
            network_thread_.get(), worker_thread_.get(), sig_thread_.get(),
            adm_,
            std::move(media_deps.audio_encoder_factory),
            std::move(media_deps.audio_decoder_factory),
            std::move(media_deps.video_encoder_factory),
            std::move(media_deps.video_decoder_factory),
            nullptr, nullptr);

        if (peer_conn_factory_.get() == nullptr) {
            LOGE("Error on CreateModularPeerConnectionFactory.");
            return;
        }
        if (adm_->Init() != 0 || adm_->InitPlayout() != 0 ||
            adm_->StartPlayout() != 0) {
            LOGE("Failed to start discard-only WebRTC playout clock");
        }
        LOGI("CreatePeerConnectionFactory success.");
    }

    bool RtcServer::ApplyIceConfiguration(const std::string& ice_config_json,
                                           bool update_peer_connection) {
        auto next = configuration_;
        next.servers.clear();
        try {
            const auto config = nlohmann::json::parse(ice_config_json);
            for (const auto& entry : config.value("ice_servers", nlohmann::json::array())) {
                const auto username = entry.value("username", "");
                const auto credential = entry.value("credential", "");
                for (const auto& url : entry.value("urls", std::vector<std::string>{})) {
                    webrtc::PeerConnectionInterface::IceServer server;
                    server.uri = url;
                    server.username = username;
                    server.password = credential;
                    server.tls_cert_policy =
                        webrtc::PeerConnectionInterface::TlsCertPolicy::kTlsCertPolicySecure;
                    next.servers.push_back(std::move(server));
                }
            }
        }
        catch (const std::exception& error) {
            LOGE("Invalid standard RTC ICE configuration: {}", error.what());
            return false;
        }
        if (update_peer_connection && peer_conn_) {
            const auto result = peer_conn_->SetConfiguration(next);
            if (!result.ok()) {
                LOGE("SetConfiguration for standard RTC failed: {}", result.message());
                return false;
            }
        }
        LOGI("Configured {} ICE server URLs for standard RTC", next.servers.size());
        configuration_ = std::move(next);
        return true;
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

        const bool allow_video = HasPermission("view");
        // video sources/tracks
        // offer 里的 video m-line 数决定布局:
        // - 1 条(web/旧客户端): 单动态 track,接收所有屏的帧(旧行为,
        //   编码器侧的切屏等 IDR 逻辑不变,web 端切屏继续可用);
        // - >=2 条(新 Windows 客户端): 每台显示器一条静态 track,帧按 mon_name
        //   路由,根治单 track 混流(两屏帧交替 → 反复"切屏等 IDR"的风暴)。
        int offer_video_mlines = 0;
        int offer_audio_mlines = 0;
        {
            size_t pos = 0;
            while ((pos = offer_sdp_.find("m=video", pos)) != std::string::npos) {
                ++offer_video_mlines;
                pos += 7;
            }
            pos = 0;
            while ((pos = offer_sdp_.find("m=audio", pos)) != std::string::npos) {
                ++offer_audio_mlines;
                pos += 7;
            }
        }
        auto monitors = runtime_->GetRtcTrackMonitors();
        multi_track_mode_ = allow_video && offer_video_mlines > 1;
        static constexpr const char* kMediaStreamId = "pixels_media";
        if (allow_video && multi_track_mode_) {
            const auto track_count = std::min(
                offer_video_mlines, RtcLocalPlugin::kMaxRtcVideoTracks);
            for (int track_index = 0; track_index < track_count; ++track_index) {
                MonitorVideoTrack mvt;
                if (track_index < static_cast<int>(monitors.size())) {
                    mvt.mon_name_ = monitors[track_index].name_;
                }
                mvt.source_ = std::make_shared<VideoSourceImpl>();
                mvt.track_source_ = rtc::make_ref_counted<VideoTrackSourceImpl>(mvt.source_);
                auto video_track = peer_conn_factory_->CreateVideoTrack(mvt.track_source_, std::format("video_track_{}", track_index));
                // 每条 track 独立 stream id,客户端按 receiver->stream_ids() 区分屏
                auto rtc_error_or = peer_conn_->AddTrack(video_track, { std::format("{}_{}", kMediaStreamId, track_index) });
                if (!rtc_error_or.ok()) {
                    LOGE("peer connection add video track {} failed. with {}", track_index, rtc_error_or.error().message());
                    return;
                }
                video_tracks_.push_back(mvt);
            }
            LOGI("Multi-track mode: reserved {} video track slot(s), active monitors: {}, offer video m-lines: {}",
                 video_tracks_.size(), monitors.size(), offer_video_mlines);
            // 多 track = 客户端声明要多屏:让采集端产出所有显示器的帧,
            // 否则非当前屏的 track 永远等不到帧(采集端默认只采当前屏)
            runtime_->EnableAllMonitorCapture();
        }
        else if (allow_video) {
            MonitorVideoTrack mvt;  // mon_name_ 为空 = 接收所有屏的动态 track(旧行为)
            mvt.source_ = std::make_shared<VideoSourceImpl>();
            mvt.track_source_ = rtc::make_ref_counted<VideoTrackSourceImpl>(mvt.source_);
            // video/audio 必须挂同一 MediaStream id,否则 web 端若直接用
            // ontrack.streams[0] 赋值 srcObject,后到的轨会覆盖先到的(有画面无声)。
            auto video_track = peer_conn_factory_->CreateVideoTrack(mvt.track_source_, "video_track_source_1");
            auto rtc_error_or = peer_conn_->AddTrack(video_track, { kMediaStreamId });
            if (!rtc_error_or.ok()) {
                LOGE("peer connection add track failed. with {}", rtc_error_or.error().message());
                return;
            }
            video_tracks_.push_back(mvt);
        }

        // Console wall sessions are video-only by contract. Do not create an RTP
        // audio sender at all; this saves capture/encode/network work and makes
        // the privacy boundary independent of browser mute state.
        if (!IsWallObserver() && HasPermission("audio")) {
            audio_source_ = AudioSourceImpl::Create();
            auto audio_track = peer_conn_factory_->CreateAudioTrack("audio", audio_source_.get());
            // 多 track 模式下音频用独立 stream id,避免和多路 video 混在同一 stream;
            // 单 track 模式保持与 video 同 stream(web 端 srcObject 需要)
            if (multi_track_mode_) {
                peer_conn_->AddTrack(audio_track, { std::format("{}_audio", kMediaStreamId) });
            }
            else {
                peer_conn_->AddTrack(audio_track, { kMediaStreamId });
            }

            // The second audio m-line is dedicated to the authorized voice
            // call. Desktop/system audio remains on the first track.
            if (offer_audio_mlines >= 2) {
                voice_audio_source_ = AudioSourceImpl::Create();
                auto voice_track = peer_conn_factory_->CreateAudioTrack(
                    "voice_call_audio", voice_audio_source_.get());
                const auto voice_result = peer_conn_->AddTrack(
                    voice_track, { "pixels_voice_call" });
                if (!voice_result.ok()) {
                    LOGE("peer connection add voice track failed: {}",
                         voice_result.error().message());
                    voice_audio_source_ = nullptr;
                }
            }
        }

        // BWE 初始种子:默认起始估计只有 300kbps,爬坡期 pacing 饿死视频码流,
        // 而 BWE 又依赖码流动起来才能探测上行——鸡生蛋死锁,表现为视频完全发不出来。
        // 给一个有意义的起点,后续仍由 BWE 按真实链路状况上下调整。
        //
        // 本地链路(loopback/局域网)直接把工作点钉住:实测 GCC 的延迟估计
        // 在客户端高负载(有头浏览器解码渲染)下会误判拥塞,目标码率/fps 在
        // 1M~15M / 14~44fps 之间秒级震荡——x264 每 3s 被迫重开、生产速率被压到
        // ~30fps,pacing 失配又反过来喂养延迟估计,形成延迟螺旋。
        // 本插件只服务本地链路,钉死 min=start=max 让 GCC 无震荡空间;
        // 链路侧其余自适应(IDR/pacing)不受影响。
        //
        // 钉值取 24M(原 12M):pacer 按此速率放包,钉值翻倍让 IDR 等大帧的
        // 排空时间减半(73KB: 49ms→24ms),直接削掉 pacing 段延迟;loopback/
        // 有线 LAN 容量充裕,Wi-Fi 直连也留有余量。实测双 track 总分配
        // ~11.3M 已到 12M 上限,24M 给双屏高动态场景留出头空间。
        webrtc::BitrateSettings bitrate_settings;
        static constexpr int kMaximumBitrateBps = 24 * 1000 * 1000;
        if (standard_rtc_) {
            // TURN/WAN capacity is not known in advance. Seed GCC high enough
            // for a responsive first picture but keep a real adaptation range.
            bitrate_settings.min_bitrate_bps = 500 * 1000;
            bitrate_settings.start_bitrate_bps = 6 * 1000 * 1000;
            bitrate_settings.max_bitrate_bps = kMaximumBitrateBps;
        }
        else {
            bitrate_settings.min_bitrate_bps = kMaximumBitrateBps;
            bitrate_settings.start_bitrate_bps = kMaximumBitrateBps;
            bitrate_settings.max_bitrate_bps = kMaximumBitrateBps;
        }
        auto bitrate_err = peer_conn_->SetBitrate(bitrate_settings);
        LOGI("SetBitrate: mode={} min={} start={} max={} ok={}",
             standard_rtc_ ? "standard" : "direct",
             *bitrate_settings.min_bitrate_bps, *bitrate_settings.start_bitrate_bps,
             *bitrate_settings.max_bitrate_bps, bitrate_err.ok());

        // 首帧加速:即将开始发流,此刻主动请求主管线产 IDR。
        // 建连前的旧帧无需清理:Encode 首次执行时会以当前产出序号引导
        // (consumed_seq_ = GetLatestEncodedSeq),只消费之后新产的帧,
        // 配合 mWaitIDRFrame 保证首帧必为关键帧。
        runtime_->InsertIdr();

        SetRemoteOffer(offer_sdp_);

    }

    bool RtcServer::SetRemoteOffer(const std::string& offer_sdp) {
        if (!peer_conn_) {
            return false;
        }
        LOGI("Will set remote offer sdp.");
        webrtc::SdpParseError error;
        auto* session_description = webrtc::CreateSessionDescription("offer", offer_sdp, &error);
        if (!session_description || !error.line.empty()) {
            LOGE("OnOfferSdpCallback, SetRemoteDescription error: {}, {}", error.line, error.description);
            delete session_description;
            return false;
        }
        peer_conn_->SetRemoteDescription(this->set_remote_offer_sdp_callback_.get(), session_description);
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

    void RtcServer::SendIceToRemote(const std::string& ice, const std::string& mid, int sdp_mline_index) {
        auto event = std::make_shared<PxPluginRtcIceEvent>();
        event->stream_id_ = stream_id_;
        event->ice_ = ice;
        event->mid_ = mid;
        event->sdp_mline_index_ = sdp_mline_index;
        runtime_->QueueEvent(event);
    }

    void RtcServer::OnRemoteAudioTrack(rtc::scoped_refptr<webrtc::AudioTrackInterface> track) {
        LOGI("OnRemoteAudioTrack: {}", track->id());
        // Offers create the microphone transceiver without a sender track and
        // attach it only after local consent. Explicitly keep the negotiated
        // receiver enabled so the first post-consent RTP packet starts decode
        // instead of remaining in a muted source state.
        track->set_enabled(true);
        // 已有 sink(理论上一条连接只有一条上行音频轨),先清理
        OnRemoteAudioTrackRemoved(remote_audio_track_);

        // Factory uses dummy ADM so decoded browser audio is delivered only to
        // this sink. The sink forwards PCM to the authorization-bound voice
        // endpoint, which owns WASAPI playout and the AEC reverse reference.
        const std::weak_ptr<RtcServer> weak_self = shared_from_this();
        auto sink = RemoteAudioSink::Make(
            [weak_self](const std::string& call_id, const int16_t* samples,
                        size_t sample_count, int sample_rate, int channels) {
                if (const auto self = weak_self.lock(); self && self->runtime_) {
                    self->runtime_->OnRemoteVoiceCallPcm(
                        self->stream_id_, call_id, samples, sample_count,
                        sample_rate, channels);
                }
            });
        track->AddSink(sink.get());
        std::string authorized_call;
        {
            std::scoped_lock lock(voice_mutex_);
            remote_audio_track_ = std::move(track);
            remote_audio_sink_ = sink;
            authorized_call = authorized_voice_call_id_;
        }
        if (!authorized_call.empty()) {
            sink->SetAuthorized(authorized_call, true);
        }
        LOGI("Remote audio sink attached (authorization-gated WASAPI playout).");
    }

    void RtcServer::OnRemoteAudioTrackRemoved(rtc::scoped_refptr<webrtc::AudioTrackInterface> track) {
        rtc::scoped_refptr<webrtc::AudioTrackInterface> removed_track;
        std::shared_ptr<RemoteAudioSink> removed_sink;
        {
            std::scoped_lock lock(voice_mutex_);
            if (!remote_audio_sink_) {
                return;
            }
            if (track && remote_audio_track_ && track->id() != remote_audio_track_->id()) {
                return;
            }
            removed_track = std::move(remote_audio_track_);
            removed_sink = std::move(remote_audio_sink_);
        }
        LOGI("OnRemoteAudioTrackRemoved");
        removed_sink->SetAuthorized({}, false);
        if (removed_track) {
            removed_track->RemoveSink(removed_sink.get());
        }
    }

    bool RtcServer::SetVoiceCallAuthorization(
        const std::string& call_id, bool authorized) {
        std::shared_ptr<RemoteAudioSink> sink;
        {
            std::scoped_lock lock(voice_mutex_);
            authorized_voice_call_id_ = authorized ? call_id : std::string{};
            sink = remote_audio_sink_;
        }
        if (!authorized) {
            if (peer_conn_) {
                rtc::scoped_refptr<webrtc::RTCStatsCollectorCallback> callback(
                    new rtc::RefCountedObject<VoiceInboundStatsCallback>());
                peer_conn_->GetStats(callback.get());
            }
            if (sink) sink->SetAuthorized({}, false);
            return true;
        }
        if (call_id.empty() || !voice_audio_source_ || !sink) {
            LOGW("WebRTC voice authorization unavailable: call={}, source={}, sink={}",
                 PrivacyLogId(call_id), voice_audio_source_ != nullptr, sink != nullptr);
            return false;
        }
        sink->SetAuthorized(call_id, true);
        return true;
    }

    void RtcServer::OnVoiceCallPcm(
        const std::string& call_id, const int16_t* samples,
        size_t sample_count, int sample_rate, int channels) {
        rtc::scoped_refptr<AudioSourceImpl> source;
        {
            std::scoped_lock lock(voice_mutex_);
            if (call_id.empty() || call_id != authorized_voice_call_id_) {
                return;
            }
            source = voice_audio_source_;
        }
        if (source && samples && sample_count > 0) {
            source->SendAudio(samples, sample_count * sizeof(int16_t),
                              sample_rate, channels, 16);
        }
    }

    void RtcServer::PostProtoMessage(std::shared_ptr<Data> msg, bool run_through) {
        if (network_thread_ && media_data_channel_ && !exit_) {
            const auto weak_server = weak_from_this();
            network_thread_->PostTask([weak_server, msg]() {
                if (const auto server = weak_server.lock();
                    server && server->media_data_channel_ && !server->exit_) {
                    server->media_data_channel_->SendData(msg);
                }
            });
        }
    }

    bool RtcServer::PostTargetStreamProtoMessage(const std::string &stream_id, std::shared_ptr<Data> msg, bool run_through) {
        if (stream_id.empty() || stream_id != stream_id_) {
            return false;
        }
        if (network_thread_ && media_data_channel_ && !exit_) {
            const auto weak_server = weak_from_this();
            network_thread_->PostTask([weak_server, msg]() {
                if (const auto server = weak_server.lock();
                    server && server->media_data_channel_ && !server->exit_) {
                    server->media_data_channel_->SendData(msg);
                }
            });
        }
        return true;
    }

    bool RtcServer::PostTargetFileTransferProtoMessage(const std::string &stream_id, std::shared_ptr<Data> msg, bool run_through) {
        if (stream_id.empty() || stream_id != stream_id_) {
            return false;
        }
        // 必须投递到 WebRTC 网络线程再 Send,否则 data_channel_->Send() 跨线程调用
        // 会被 libwebrtc 静默丢弃(剪切板文件取数 kClipboardReqBuffer/kClipboardRespBuffer
        // 偶发 60s 超时即源于此)。与 media 通道 PostTargetStreamProtoMessage 对齐。
        if (!network_thread_ || !ft_data_channel_ || exit_ ||
            !ft_data_channel_->IsConnected()) {
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

    bool RtcServer::IsMediaConsumerActive() const {
        if (exit_) {
            return false;
        }
        // Observer offers intentionally have no data channel. Count the
        // allocated session while ICE is being established, then its ICE
        // lifecycle owns cleanup. Interactive sessions retain legacy behavior.
        return IsWallObserver() || (media_data_channel_ && media_data_channel_->IsConnected());
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
        return !exit_ && ft_data_channel_
            ? ft_data_channel_->AcquireFileTransferWritableSignal()
            : std::shared_ptr<FileTransferWritableSignal>{};
    }

    void RtcServer::On100msTimeout() {
        if (!exit_ && wall_observer_ && !ice_ever_connected_) {
            auto now = (int64_t)TimeUtil::GetCurrentTimestamp();
            if (now - created_timestamp_ms_ >= kWallObserverConnectTimeoutMs) {
                LOGW("Wall observer connect timeout, conn_id: {}, will be swept.", conn_id_);
                exit_ = true;
                NotifyTerminal();
            }
        }
        if (!exit_ && ice_disconnected_since_ms_.load() != 0) {
            auto now = (int64_t)TimeUtil::GetCurrentTimestamp();
            if (now - ice_disconnected_since_ms_.load() >= kIceDisconnectedTimeoutMs) {
                LOGW("Rtc server ice disconnected timeout, conn_id: {}, will be swept.", conn_id_);
                exit_ = true;
                EmitClientDisconnectedEvent();
                NotifyTerminal();
            }
        }
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
        if (exit_) {
            return;
        }
        if (video_tracks_.empty()) {
            LOGE("Don't have video source");
            return;
        }
        if (handle == 0) {
            LOGE("Invalid texture handle");
            return;
        }
        DispatchCapturedFrameNotify(mon_name, frame_idx, frame_width, frame_height, handle, adapter_id, frame_format);
    }

    // CPU 采集(GDI/mock)的裸帧通知:没有共享纹理,handle 置 0。
    // NotifyFrameFrameBuffer 只是"有帧了"的载体,像素从不经 webrtc 传递
    // (RtcVideoEncoder 用预编码码流替换),所以 handle=0 无影响。
    void RtcServer::OnNewRawFrameCaptured(const std::string& mon_name, uint64_t frame_idx, int frame_width, int frame_height) {
        if (exit_) {
            return;
        }
        if (video_tracks_.empty()) {
            LOGE("Don't have video source");
            return;
        }
        DispatchCapturedFrameNotify(mon_name, frame_idx, frame_width, frame_height, 0, 0, 0);
    }

    void RtcServer::DispatchCapturedFrameNotify(const std::string& mon_name, uint64_t frame_idx, int frame_width, int frame_height, uint64_t handle, int64_t adapter_id, uint64_t frame_format) {

        // 按屏路由:多 track 模式每条 track 只发自己那块屏的帧;
        // 单 track(动态)模式接收所有屏,编码器侧处理切屏。多轨模式遇到
        // 新显示器时在已协商的空槽位内绑定,不触发连接重建或 SDP 重协商。
        if (multi_track_mode_) {
            bool monitor_is_mapped = false;
            {
                std::lock_guard<std::mutex> guard(video_tracks_mutex_);
                monitor_is_mapped = std::any_of(
                    video_tracks_.begin(), video_tracks_.end(),
                    [&mon_name](const auto& track) { return track.mon_name_ == mon_name; });
            }
            if (!monitor_is_mapped) {
                const auto monitors = runtime_->GetRtcTrackMonitors();
                std::vector<std::string> active_monitor_names;
                active_monitor_names.reserve(monitors.size());
                for (const auto& monitor : monitors) {
                    active_monitor_names.push_back(monitor.name_);
                }
                IncludeObservedRtcMonitor(active_monitor_names, mon_name);

                std::lock_guard<std::mutex> guard(video_tracks_mutex_);
                std::vector<std::string> track_slots;
                track_slots.reserve(video_tracks_.size());
                for (const auto& track : video_tracks_) {
                    track_slots.push_back(track.mon_name_);
                }
                if (ReconcileRtcMonitorTrackSlots(track_slots, active_monitor_names)) {
                    for (size_t index = 0; index < video_tracks_.size(); ++index) {
                        if (video_tracks_[index].mon_name_ != track_slots[index]) {
                            LOGI("RTC video track slot #{} rebound: '{}' -> '{}'", index,
                                 video_tracks_[index].mon_name_, track_slots[index]);
                            video_tracks_[index].mon_name_ = track_slots[index];
                            video_tracks_[index].frame_sequence_ = {};
                            video_tracks_[index].requires_stream_reset_ = true;
                        }
                    }
                }
            }
        }

        std::shared_ptr<VideoSourceImpl> target_source;
        RtcFrameSequenceResult frame_sequence_result;
        bool topology_rebound = false;
        {
            std::lock_guard<std::mutex> guard(video_tracks_mutex_);
            std::optional<size_t> target_index;
            if (!multi_track_mode_ && !video_tracks_.empty()) {
                target_index = 0;
            }
            else {
                for (size_t index = 0; index < video_tracks_.size(); ++index) {
                    if (video_tracks_[index].mon_name_ == mon_name) {
                        target_index = index;
                        break;
                    }
                }
            }
            if (!target_index.has_value()) {
                static std::atomic_uint64_t unknown_mon_drops = 0;
                if (++unknown_mon_drops % 300 == 1) {
                    LOGW("OnNewFrameCaptured, no negotiated video track slot for monitor: {}", mon_name);
                }
                return;
            }

            auto& target = video_tracks_[target_index.value()];
            frame_sequence_result = AdvanceRtcFrameSequence(target.frame_sequence_, frame_idx);
            topology_rebound = target.requires_stream_reset_;
            target.requires_stream_reset_ = false;
            target_source = target.source_;
        }
        const bool stream_reset = ShouldResetRtcCaptureStream(
            topology_rebound, frame_sequence_result);
        if (stream_reset) {
            LOGW("RTC capture stream reset for [{}] (topology rebound: {}); discard prior delta chain and wait for IDR",
                 mon_name, topology_rebound);
        }
        else if (frame_sequence_result.disposition_ == RtcFrameSequenceDisposition::kForwardGap
                 && frame_sequence_result.gap_ > 1) {
            LOGW("OnNewFrameCaptured [{}] skipped {} frame(s)", mon_name,
                 frame_sequence_result.gap_ - 1);
        }

        // timestamp_us = Unix us. Do NOT set ntp_time_ms here: WebRTC fills NTP
        // on the encode path; stuffing the wrong epoch caused DebugBreak crashes.
        // RtcSharedVideoEncoder normalizes EncodedImage.ntp_time_ms_ before send.
        const auto now_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        auto buffer = rtc::make_ref_counted<NotifyFrameFrameBuffer>(
            mon_name, frame_idx, frame_width, frame_height, handle, adapter_id,
            frame_format, stream_reset);
        webrtc::VideoFrame notify_frame = webrtc::VideoFrame::Builder()
                .set_video_frame_buffer(buffer)
                .set_timestamp_us(now_us)
                .set_id(static_cast<uint16_t>(frame_idx & 0xFFFF))
                .build();
        if (target_source) {
            target_source->OnNotifyFrame(notify_frame);
        }
    }

    std::vector<std::string> RtcServer::GetVideoTrackMonitors() const {
        std::lock_guard<std::mutex> guard(video_tracks_mutex_);
        std::vector<std::string> names;
        names.reserve(video_tracks_.size());
        for (const auto& t : video_tracks_) {
            names.push_back(t.mon_name_);
        }
        return names;
    }

    void RtcServer::OnRawAudioData(const std::shared_ptr<Data>& data, int samples, int channels, int bits) {
        if (exit_ || !audio_source_ || !data || data->Size() == 0) {
            return;
        }
        audio_source_->SendAudio(data->DataAddr(), data->Size(), samples, channels, bits);
    }

    void RtcServer::EmitClientDisconnectedEvent() {
        if (IsWallObserver()) {
            return;
        }
        // 全连接生命周期只发一次:ICE 瞬断/终态、media datachannel 独立关闭、
        // ICE 超时判死 都可能触发。
        if (disconnect_event_sent_.exchange(true)) {
            return;
        }
        if (!runtime_) {
            return;
        }
        auto event = std::make_shared<PxPluginClientDisConnectedEvent>();
        event->conn_id_ = conn_id_;
        // visitor 标识与连接事件保持一致(真实访客 stream id):
        // 连接/断开事件必须能按 id 配对, 否则按 visitor 键控的插件
        // (如 media_recorder 自动录制的启停)永远等不到配对断开。
        event->visitor_device_id_ = !stream_id_.empty() ? stream_id_ : conn_id_;
        // 真实访客 stream id(Start 时信令传入,与 px::Message.stream_id 一致);
        // 空时回退 datachannel 内部 id(历史行为)。
        event->stream_id_ = !stream_id_.empty() ? stream_id_
            : (media_data_channel_ ? media_data_channel_->the_conn_id_ : "");
        event->end_timestamp_ = (int64_t)TimeUtil::GetCurrentTimestamp();
        event->duration_ = media_data_channel_
            ? event->end_timestamp_ - media_data_channel_->created_timestamp_ : 0;
        runtime_->QueueEvent(event);
        LOGW("Client disconnected event emitted, conn: {}, stream: {}", conn_id_, event->stream_id_);
    }

    void RtcServer::Exit() {
        // 幂等:ICE 终态 Sweep、takeover 替换、插件销毁等路径可能重复调用
        if (cleaned_up_.exchange(true)) {
            return;
        }
        exit_ = true;
        OnRemoteAudioTrackRemoved(remote_audio_track_);
        if (media_data_channel_) {
            media_data_channel_->Close();
        }
        if (ft_data_channel_) {
            ft_data_channel_->Close();
        }
        if (input_data_channel_) {
            input_data_channel_->Close();
        }
        if (ping_data_channel_) {
            ping_data_channel_->Close();
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

        // 打断 RtcServer <-> RtcDataChannel 的 shared_ptr 循环引用,避免泄漏
        media_data_channel_ = nullptr;
        ft_data_channel_ = nullptr;
        input_data_channel_ = nullptr;
        ping_data_channel_ = nullptr;

    }

}
