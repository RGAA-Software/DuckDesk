//
// Created by RGAA on 16/04/2025.
//

#include "webrtc_connection.h"
#include "px_message.pb.h"
#include "px_common_new/log.h"
#include "px_common_new/data.h"
#include "px_common_new/md5.h"
#include "px_common_new/uuid.h"
#include "px_common_new/thread.h"
#include "px_webrtc_client/rtc_client_interface.h"
#include "px_common_new/message_notifier.h"
#include "px_client_sdk_new/connection/rtc_ice_restart_workflow.h"
#include "px_client_sdk_new/sdk_messages.h"
#include "px_client_sdk_new/thunder_sdk.h"
#include "px_client_sdk_new/connection/relay_connection.h"
#include "px_client_sdk_new/sdk_statistics.h"
#include <nlohmann/json.hpp>
#include <format>
#include <map>
#ifdef WIN32
#include <QApplication>
#endif

typedef void *(*FnGetInstance)();

namespace px
{

    std::shared_ptr<WebRtcConnection> WebRtcConnection::Make(
        const std::shared_ptr<RelayConnection>& relay_conn,
        const std::shared_ptr<ThunderSdkParams>& params,
        const std::shared_ptr<MessageNotifier>& notifier) {
        auto connection = std::make_shared<WebRtcConnection>(relay_conn, params, notifier);
        connection->Prepare();
        return connection;
    }

    WebRtcConnection::WebRtcConnection(const std::shared_ptr<RelayConnection>& relay_conn,
                                       const std::shared_ptr<ThunderSdkParams>& params,
                                       const std::shared_ptr<MessageNotifier>& notifier)
                                       : Connection(params, notifier) {
        this->relay_conn_ = relay_conn;

        relay_conn_ = relay_conn;
        sdk_params_ = params;
        msg_notifier_ = notifier;
        if (msg_notifier_) {
            async_scope_ = PxAsyncScope::Create(
                msg_notifier_->GetAsyncRuntime(), PxAsyncLane::kState);
            if (async_scope_) {
                ice_restart_workflow_ =
                    RtcIceRestartWorkflow::Create(async_scope_->Executor());
            }
        }
        thread_ = Thread::Make("rtc_client_thread", 1024 * 8);
        thread_->Poll();
    }

    void WebRtcConnection::Prepare() {
        auto weak_self = weak_from_this();
        msg_listener_ = msg_notifier_->CreateListener(MessageExecutionLane::kControl);
        msg_listener_->Listen<SdkMsgNetworkConnected>([](const SdkMsgNetworkConnected&) {
            LOGI("Sdk msg, network connected.");
        });

        msg_listener_->Listen<SdkMsgRoomPrepared>([weak_self](const SdkMsgRoomPrepared& msg) {
            auto self = weak_self.lock();
            if (self && self->sdk_params_->enable_p2p_ && msg.room_type_ == kRoomTypeMedia) {
                LOGI("Sdk msg, room prepared, will init webrtc!");
                self->Init();
            }
        });

        msg_listener_->Listen<SdkMsgRemoteAnswerSdp>([weak_self](const SdkMsgRemoteAnswerSdp& msg) {
            if (auto self = weak_self.lock()) {
                self->OnRemoteSdp(msg);
            }
        });

        msg_listener_->Listen<SdkMsgRemoteIce>([weak_self](const SdkMsgRemoteIce& msg) {
            if (auto self = weak_self.lock()) {
                self->OnRemoteIce(msg);
            }
        });

        LoadRtcLibrary();
    }

    WebRtcConnection::~WebRtcConnection() {

    }

    void WebRtcConnection::Start() {

    }

    void WebRtcConnection::Stop() {
        Connection::Stop();
        if (stopped_.exchange(true)) {
            return;
        }
        if (ice_restart_workflow_) {
            static_cast<void>(ice_restart_workflow_->Cancel(MakePxAsyncError(
                PxAsyncErrorCode::kServiceStopped,
                "rtc_shutdown",
                "RTC connection is stopping")));
        }
        if (async_scope_) {
            async_scope_->BeginStop();
            if (!async_scope_->IsScopeThread()) {
                static_cast<void>(async_scope_->WaitFor(std::chrono::seconds(5)));
            }
        }
        if (rtc_client_) {
            rtc_client_->Exit();
        }
    }

    void WebRtcConnection::Init() {
        bool expected = false;
        if (!init_started_.compare_exchange_strong(expected, true)) {
            return;
        }
        auto weak_self = weak_from_this();
        RunInRtcThread([weak_self]() {
            auto self = weak_self.lock();
            if (!self) {
                return;
            }
            if (!self->rtc_client_) {
                LOGE("RTC client library is unavailable");
                self->NotifyDisconnectedOnce();
                return;
            }

            self->rtc_client_->SetOnLocalSdpSetCallback([weak_self](const std::string& sdp) {
                if (auto locked = weak_self.lock()) {
                    LOGI("Will send sdp to remote, sdp size: {}", sdp.size());
                    locked->SendSdpToRemote(sdp);
                }
            });

            self->rtc_client_->SetOnLocalIceCallback(
                [weak_self](const std::string& ice, const std::string& mid, int sdp_mline_index) {
                    if (auto locked = weak_self.lock()) {
                        LOGI("Will send ice to remote: {}", ice);
                        locked->SendIceToRemote(ice, mid, sdp_mline_index);
                    }
            });

            self->rtc_client_->SetMediaMessageCallback([weak_self](std::shared_ptr<Data> msg) {
                if (auto locked = weak_self.lock(); locked && locked->media_msg_cbk_) {
                    locked->media_msg_cbk_(std::move(msg));
                }
            });

            self->rtc_client_->SetFtMessageCallback([weak_self](std::shared_ptr<Data> msg) {
                if (auto locked = weak_self.lock(); locked && locked->ft_msg_cbk_) {
                    locked->ft_msg_cbk_(std::move(msg));
                }
            });

            // Standard RTC video is decoded inside libwebrtc. Forward its
            // packed I420 frames into the same SDK/UI path used by Direct RTC;
            // without this callback RtcVideoSink receives frames indefinitely
            // but px_client never renders one.
            self->rtc_client_->SetOnVideoFrameCallback(
                [weak_self](int w, int h, std::shared_ptr<Data> i420) {
                    auto locked = weak_self.lock();
                    if (locked && locked->video_frame_cbk_) {
                        if (!locked->first_video_frame_forwarded_.exchange(true)) {
                            LOGI("Standard RTC first decoded frame entering SDK: {}x{}", w, h);
                        }
                        locked->video_frame_cbk_(w, h, std::move(i420));
                    }
                });

            self->rtc_client_->SetOnAudioDataCallback(
                [weak_self](std::shared_ptr<Data> pcm, int sample_rate, int channels) {
                    if (auto locked = weak_self.lock(); locked && locked->audio_data_cbk_) {
                        locked->audio_data_cbk_(std::move(pcm), sample_rate, channels);
                    }
                });

            self->rtc_client_->SetOnIceStateCallback([weak_self](int state) {
                auto locked = weak_self.lock();
                if (!locked) {
                    return;
                }
                // libwebrtc IceConnectionState: connected=2, completed=3,
                // failed=4, disconnected=5, closed=6. A disconnected state
                // may recover, so only terminal states close the SDK session.
                if (state == 2 || state == 3) {
                    SdkStatistics::Instance()->rtc_ice_state_ = state == 2 ? "connected" : "completed";
                    locked->ice_connected_ = true;
                    if (locked->ice_restart_workflow_) {
                        static_cast<void>(locked->ice_restart_workflow_->CompleteConnected());
                    }
                    locked->NotifyConnectedWhenReady();
                }
                else if (state == 4) {
                    SdkStatistics::Instance()->rtc_ice_state_ = "restarting";
                    locked->ice_connected_ = false;
                    locked->BeginManagedIceRestart();
                }
                else if (state == 6) {
                    SdkStatistics::Instance()->rtc_ice_state_ = "closed";
                    locked->ice_connected_ = false;
                    locked->NotifyDisconnectedOnce();
                }
                else if (state == 5) {
                    SdkStatistics::Instance()->rtc_ice_state_ = "disconnected";
                    locked->ice_connected_ = false;
                }
                else {
                    SdkStatistics::Instance()->rtc_ice_state_ = std::to_string(state);
                }
            });

            self->rtc_client_->SetOnStatsJsonCallback([weak_self](const std::string& json) {
                if (auto locked = weak_self.lock()) {
                    locked->UpdateTransportStats(json);
                }
            });

            self->rtc_client_->SetLocalRtcMode(false);
            self->rtc_client_->SetFileTransferOnly(self->sdk_params_->file_transfer_only_);
            self->rtc_client_->SetIceServersJson(self->sdk_params_->rtc_ice_config_json_);

            if (!self->rtc_client_->Init(self->sdk_params_->bare_remote_device_id_)) {
                LOGE("RTC client init FAILED!");
                self->NotifyDisconnectedOnce();
                return;
            }

            LOGI("RTC client init success");
        });
    }

    void WebRtcConnection::LoadRtcLibrary() {
        const std::weak_ptr<WebRtcConnection> weak_self = weak_from_this();
        RunInRtcThread([weak_self]() {
            const auto self = weak_self.lock();
            if (!self || self->stopped_) {
                return;
            }
#ifdef WIN32
            LOGI("Begin to load library!");
            auto lib_name = QApplication::applicationDirPath() + "/px_client_rtc.dll";
            // Existing plug-in loader ownership is an explicit compatibility
            // boundary; the module intentionally remains loaded for process life.
            self->rtc_lib_ = new QLibrary(lib_name); // NOLINT(gammaray-raw-pointer-boundary): plug-in loader ABI/lifetime
            auto r = self->rtc_lib_->load();
            if (!r) {
                LOGE("LOAD rtc conn FAILED");
                self->NotifyDisconnectedOnce();
                return;
            }

            auto fn_get_instance = (FnGetInstance)self->rtc_lib_->resolve("GetInstance");
            if (!fn_get_instance) {
                LOGE("DON'T have GetInstance");
                self->NotifyDisconnectedOnce();
                return;
            }

            self->rtc_client_ = (RtcClientInterface*)fn_get_instance();
            if (!self->rtc_client_) {
                LOGE("Can't get rtc client instance.");
                self->NotifyDisconnectedOnce();
                return;
            }
            LOGI("Load Rtc library success.");
#endif
        });
    }

    RtcClientInterface* WebRtcConnection::GetRtcClient() {
        return rtc_client_;
    }

    void WebRtcConnection::PostMediaMessage(std::shared_ptr<Data> msg) {
        if (msg && rtc_client_ && rtc_client_->IsInputChannelReady()) {
            px::Message proto_msg;
            if (proto_msg.ParseFromArray(msg->DataAddr(), static_cast<int>(msg->Size()))) {
                const auto type = proto_msg.type();
                if (type == px::kKeyEvent || type == px::kMouseEvent
                    || type == px::kGamepadState || type == px::kTextInput) {
                    rtc_client_->PostInputMessage(msg);
                    return;
                }
            }
        }
        auto weak_self = weak_from_this();
        RunInRtcThread([weak_self, msg = std::move(msg)]() {
            if (auto self = weak_self.lock(); self && self->rtc_client_) {
                self->rtc_client_->PostMediaMessage(msg);
            }
        });
    }

    void WebRtcConnection::PostFtMessage(std::shared_ptr<Data> msg) {
        if (!rtc_client_) {
            return;
        }

        rtc_client_->PostFtMessage(msg);
    }

    int64_t WebRtcConnection::GetQueuingMediaMsgCount() {
        return rtc_client_ ? rtc_client_->GetQueuingMediaMsgCount() : -1;
    }

    int64_t WebRtcConnection::GetQueuingFtMsgCount() {
        return rtc_client_ ? rtc_client_->GetQueuingFtMsgCount() : -1;
    }

    void WebRtcConnection::SetOnMediaMessageCallback(const std::function<void(std::shared_ptr<Data>)>& cbk) {
        media_msg_cbk_ = cbk;
    }

    void WebRtcConnection::SetOnFtMessageCallback(const std::function<void(std::shared_ptr<Data>)>& cbk) {
        ft_msg_cbk_ = cbk;
    }

    void WebRtcConnection::SetOnVideoFrameCallback(
        const std::function<void(int w, int h, std::shared_ptr<Data> i420)>& cbk) {
        video_frame_cbk_ = cbk;
    }

    void WebRtcConnection::SetOnAudioDataCallback(
        const std::function<void(std::shared_ptr<Data> pcm, int sample_rate, int channels)>& cbk) {
        audio_data_cbk_ = cbk;
    }

    void WebRtcConnection::OnRemoteSdp(const SdkMsgRemoteAnswerSdp& m) {
        auto weak_self = weak_from_this();
        auto sdp = m.answer_sdp_.sdp();
        RunInRtcThread([weak_self, sdp = std::move(sdp)]() {
            if (auto self = weak_self.lock(); self && self->rtc_client_) {
                self->rtc_client_->OnRemoteSdp(sdp);
            }
        });
    }

    void WebRtcConnection::OnRemoteIce(const SdkMsgRemoteIce& m) {
        auto weak_self = weak_from_this();
        auto sub = m.ice_;
        RunInRtcThread([weak_self, sub = std::move(sub)]() {
            if (auto self = weak_self.lock(); self && self->rtc_client_) {
                self->rtc_client_->OnRemoteIce(sub.ice(), sub.mid(), sub.sdp_mline_index());
            }
        });
    }

    void WebRtcConnection::RunInRtcThread(std::function<void()>&& task) {
        thread_->Post([task = std::move(task)]() {
            task();
        });
    }

    void WebRtcConnection::SendSdpToRemote(const std::string& sdp) {
        if (sdk_params_->connection_ticket_.empty()
            && sdk_params_->connection_nonce_.empty()) {
            // Direct signaling has no Console-issued nonce. Keep one for the
            // lifetime of this SDK session so retries are recognized as a
            // reconnect rather than a second Controller request.
            sdk_params_->connection_nonce_ = GetUUID();
        }
        // pack to proto & send it
        px::Message pt_msg;
        pt_msg.set_device_id(sdk_params_->device_id_);
        pt_msg.set_stream_id(sdk_params_->stream_id_);
        pt_msg.set_type(px::MessageType::kSigOfferSdpMessage);
        auto sub = pt_msg.mutable_sig_offer_sdp();
        sub->set_device_id(sdk_params_->device_id_);
        sub->set_sdp(sdp);
        sub->set_connection_ticket(sdk_params_->connection_ticket_);
        sub->set_client_nonce(sdk_params_->connection_nonce_);
        sub->set_instance_id(sdk_params_->connection_instance_id_);
        if (sdk_params_->connection_ticket_.empty()) {
            sub->set_takeover(sdk_params_->direct_takeover_);
        }
        if (sdk_params_->connection_ticket_.empty()) {
            if (!sdk_params_->remote_device_safety_pwd_.empty()) {
                sub->set_safety_pwd_md5(sdk_params_->remote_device_safety_pwd_);
            }
            else if (!sdk_params_->remote_device_random_pwd_.empty()) {
                sub->set_safety_pwd_md5(MD5::Hex(sdk_params_->remote_device_random_pwd_));
            }
        }
        auto buffer = Data::Make(nullptr, pt_msg.ByteSizeLong());
        pt_msg.SerializeToArray(buffer->DataAddr(), buffer->Size());
        relay_conn_->PostBinaryMessage(buffer);
    }

    void WebRtcConnection::SendIceToRemote(const std::string& ice, const std::string& mid, int sdp_mline_index) {
        // pack to proto & send it
        px::Message pt_msg;
        pt_msg.set_device_id(sdk_params_->device_id_);
        pt_msg.set_stream_id(sdk_params_->stream_id_);
        pt_msg.set_type(px::MessageType::kSigIceMessage);
        auto sub = pt_msg.mutable_sig_ice();
        sub->set_device_id(sdk_params_->device_id_);
        sub->set_ice(ice);
        sub->set_mid(mid);
        sub->set_sdp_mline_index(sdp_mline_index);
        auto buffer = Data::Make(nullptr, pt_msg.ByteSizeLong());
        pt_msg.SerializeToArray(buffer->DataAddr(), buffer->Size());
        relay_conn_->PostBinaryMessage(buffer);
    }

    void WebRtcConnection::PostBinaryMessage(std::shared_ptr<Data> msg) {
        this->PostMediaMessage(msg);
    }

    int64_t WebRtcConnection::GetQueuingMsgCount() {
        return 0;
    }

    void WebRtcConnection::RequestPauseStream() {

    }

    void WebRtcConnection::RequestResumeStream() {

    }

    bool WebRtcConnection::HasEnoughBufferForQueuingMediaMessages() {
        return rtc_client_ && rtc_client_->HasEnoughBufferForQueuingMediaMessages();
    }

    bool WebRtcConnection::HasEnoughBufferForQueuingFtMessages() {
        return rtc_client_ && rtc_client_->HasEnoughBufferForQueuingFtMessages();
    }

    bool WebRtcConnection::IsMediaChannelReady() {
        return rtc_client_ && rtc_client_->IsMediaChannelReady();
    }

    bool WebRtcConnection::IsFtChannelReady() {
        return rtc_client_ && rtc_client_->IsFtChannelReady();
    }

    void WebRtcConnection::On16msTimeout() {
        if (rtc_client_) {
            rtc_client_->On16msTimeout();
        }
        NotifyConnectedWhenReady();
        if (!IsFtChannelReady()) {
            NotifyFileTransferClosed();
        } else if (HasEnoughBufferForQueuingFtMessages()) {
            NotifyFileTransferWritable();
        }
    }

    bool WebRtcConnection::RestartIce(const std::string& ice_config_json,
                                      const std::string& connection_ticket,
                                      const std::string& client_nonce,
                                      const std::string& instance_id,
                                      std::uint64_t revision) {
        if (stopped_ || !rtc_client_ || ice_config_json.empty() || connection_ticket.empty()
            || client_nonce.empty() || !ice_restart_workflow_ || !async_scope_) {
            return false;
        }
        const auto begin = ice_restart_workflow_->ApplyConfiguration(revision);
        if (!begin.ShouldApplyConfiguration()) {
            LOGI("Ignore duplicate/stale RTC ICE configuration, revision={}, disposition={}",
                 revision, static_cast<int>(begin.disposition));
            return true;
        }
        if (begin.StartedWorkflow() && !SpawnManagedIceRestartWait(begin)) {
            static_cast<void>(ice_restart_workflow_->Cancel(MakePxAsyncError(
                PxAsyncErrorCode::kServiceStopped,
                "rtc_restart_scope",
                "RTC async scope rejected the restart workflow")));
            return false;
        }
        sdk_params_->rtc_ice_config_json_ = ice_config_json;
        sdk_params_->connection_ticket_ = connection_ticket;
        sdk_params_->connection_nonce_ = client_nonce;
        sdk_params_->connection_instance_id_ = instance_id;
        auto weak_self = weak_from_this();
        const std::weak_ptr<RtcIceRestartWorkflow> weak_workflow = ice_restart_workflow_;
        const auto generation = begin.generation;
        const auto apply_sequence = begin.apply_sequence;
        RunInRtcThread([weak_self, weak_workflow, ice_config_json, generation, apply_sequence]() {
            auto self = weak_self.lock();
            const auto workflow = weak_workflow.lock();
            if (!workflow) {
                return;
            }
            if (!self || !self->rtc_client_ || self->stopped_ ||
                !self->rtc_client_->RestartIce(ice_config_json)) {
                if (workflow->MarkApplyFailed(
                        generation, apply_sequence,
                        MakePxAsyncError(PxAsyncErrorCode::kServiceRejected,
                            "set_configuration",
                            "active RTC SetConfiguration/RestartIce failed",
                            true,
                            "RTC_ICE_CONFIGURATION_REJECTED"))) {
                    LOGE("Active full RTC SetConfiguration/RestartIce failed");
                }
                return;
            }
            static_cast<void>(workflow->MarkApplyAccepted(generation, apply_sequence));
        });
        return true;
    }

    void WebRtcConnection::BeginManagedIceRestart() {
        if (stopped_ || !ice_restart_workflow_ || !async_scope_ || !msg_notifier_) {
            return;
        }
        const auto begin = ice_restart_workflow_->BeginConfigurationRequest();
        if (!begin.StartedWorkflow()) {
            return;
        }
        if (!SpawnManagedIceRestartWait(begin)) {
            static_cast<void>(ice_restart_workflow_->Cancel(MakePxAsyncError(
                PxAsyncErrorCode::kServiceStopped,
                "rtc_restart_scope",
                "RTC async scope rejected the restart workflow")));
            NotifyDisconnectedOnce();
            return;
        }
        if (!msg_notifier_->PublishAppMessage(SdkMsgRtcIceRestartNeeded {})) {
            static_cast<void>(ice_restart_workflow_->Cancel(MakePxAsyncError(
                PxAsyncErrorCode::kQueueFull,
                "request_rtc_configuration",
                "could not queue the managed RTC restart request",
                true)));
            return;
        }
        LOGW("Full RTC ICE failed; requested one managed ICE restart");
    }

    bool WebRtcConnection::SpawnManagedIceRestartWait(const RtcIceRestartBegin& begin) {
        if (!async_scope_ || !begin.operation) {
            return false;
        }
        const auto weak_connection = weak_from_this();
        const auto workflow = ice_restart_workflow_;
        return async_scope_->Spawn("managed-rtc-ice-restart",
            [weak_connection, workflow, begin]() {
                return AwaitManagedIceRestart(weak_connection, workflow, begin);
            });
    }

    PxAwaitable<void> WebRtcConnection::AwaitManagedIceRestart(
        std::weak_ptr<WebRtcConnection> weak_connection,
        std::shared_ptr<RtcIceRestartWorkflow> workflow,
        RtcIceRestartBegin begin) {
        // A real TURN-backed restart can spend more than 15 seconds gathering
        // and checking replacement candidates while the old selected pair
        // continues to carry media. Do not tear down a healthy session just
        // before libwebrtc reports completed.
        constexpr auto kRestartDeadline = std::chrono::seconds(30);
        auto result = co_await RtcIceRestartWorkflow::Operation::WaitUntil(
            begin.operation, std::chrono::steady_clock::now() + kRestartDeadline);
        const auto connection = weak_connection.lock();
        if (!connection) {
            co_return;
        }
        if (!result.HasValue()) {
            static_cast<void>(workflow->DetachTimedOut(begin.generation, begin.operation));
            if (!connection->stopped_) {
                SdkStatistics::Instance()->rtc_ice_state_ = "failed";
                LOGE("Managed RTC ICE restart failed: code={}, stage={}, reason={}",
                     result.Error().StableCode(), result.Error().stage, result.Error().message);
                connection->NotifyDisconnectedOnce();
            }
            co_return;
        }
        LOGI("Managed RTC ICE restart completed, generation={}, revision={}",
             result.Value().generation, result.Value().revision);
        co_return;
    }

    void WebRtcConnection::UpdateTransportStats(const std::string& json) {
        try {
            const auto report = nlohmann::json::parse(json);
            const auto& entries = report.is_array() ? report : report.value("stats", nlohmann::json::array());
            std::map<std::string, nlohmann::json> candidates;
            nlohmann::json selected;
            for (const auto& entry : entries) {
                const auto type = entry.value("type", "");
                if ((type == "local-candidate" || type == "remote-candidate")
                    && entry.contains("id")) {
                    candidates[entry.value("id", "")] = entry;
                }
                else if (type == "candidate-pair" && entry.value("nominated", false)
                    && entry.value("state", "") == "succeeded") {
                    selected = entry;
                }
            }
            if (selected.empty()) {
                return;
            }
            const auto format_candidate = [](const nlohmann::json& candidate) {
                if (candidate.empty()) return std::string("-");
                const auto type = candidate.value("candidateType", candidate.value("candidate_type", "?"));
                const auto address = candidate.value("address", candidate.value("ip", "?"));
                const auto port = candidate.value("port", 0);
                const auto protocol = candidate.value("protocol", "?");
                return std::format("{} {}:{}/{}", type, address, port, protocol);
            };
            const auto local_id = selected.value("localCandidateId", selected.value("local_candidate_id", ""));
            const auto remote_id = selected.value("remoteCandidateId", selected.value("remote_candidate_id", ""));
            const auto local = candidates.contains(local_id) ? candidates.at(local_id) : nlohmann::json();
            const auto remote = candidates.contains(remote_id) ? candidates.at(remote_id) : nlohmann::json();
            auto stats = SdkStatistics::Instance();
            stats->rtc_local_candidate_ = format_candidate(local);
            stats->rtc_remote_candidate_ = format_candidate(remote);
            const auto local_type = local.value("candidateType", local.value("candidate_type", ""));
            const auto remote_type = remote.value("candidateType", remote.value("candidate_type", ""));
            if (local_type == "relay" || remote_type == "relay") {
                const auto& relay = local_type == "relay" ? local : remote;
                const auto url = relay.value("url", "");
                stats->rtc_turn_node_ = url.empty() ? format_candidate(relay) : url;
            }
            else {
                stats->rtc_turn_node_ = "-";
            }
            const auto rtt = selected.value("currentRoundTripTime",
                                            selected.value("current_round_trip_time", 0.0));
            const auto bitrate = selected.value("availableOutgoingBitrate",
                                                selected.value("available_outgoing_bitrate", 0.0));
            stats->rtc_rtt_ms_ = static_cast<int>(rtt * 1000.0 + 0.5);
            stats->rtc_available_outgoing_bitrate_ = static_cast<int64_t>(bitrate);
        }
        catch (const std::exception& error) {
            LOGW("Parse WebRTC stats failed: {}", error.what());
        }
    }

    void WebRtcConnection::NotifyConnectedWhenReady() {
        if (stopped_ || !ice_connected_ || connected_notified_) {
            return;
        }
        const bool channel_ready = sdk_params_->file_transfer_only_
            ? IsFtChannelReady()
            : IsMediaChannelReady();
        if (!channel_ready) {
            return;
        }
        bool expected = false;
        if (connected_notified_.compare_exchange_strong(expected, true) && conn_cbk_) {
            LOGI("Full WebRTC transport is ready");
            conn_cbk_();
        }
    }

    void WebRtcConnection::NotifyDisconnectedOnce() {
        if (stopped_) {
            return;
        }
        bool expected = false;
        if (disconnected_notified_.compare_exchange_strong(expected, true) && dis_conn_cbk_) {
            dis_conn_cbk_();
        }
    }

}
