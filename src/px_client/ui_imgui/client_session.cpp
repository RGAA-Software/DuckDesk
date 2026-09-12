#include "client_session.h"

#include "client_audio_output.h"
#include "ct_virtual_display_protocol.h"
#include "px_client_sdk/platform/windows/windows_decoder_factory.h"
#include "px_client_sdk/platform/windows/windows_video_resources.h"
#include "px_client_sdk/sdk_messages.h"
#include "px_client_sdk/sdk_params.h"
#include "px_client_sdk/sdk_connection_params.h"
#include "px_client_sdk/sdk_net_client.h"
#include "px_client_sdk/sdk_recording_session.h"
#include "px_client_sdk/sdk_statistics.h"
#include "px_client_sdk/sdk_voice_call.h"
#include "px_client_sdk/platform/voice_audio_endpoint_port.h"
#include "px_client_sdk/thunder_sdk.h"
#include "px_common/data.h"
#include "px_common/md5.h"
#include "px_common/message_notifier.h"
#include "px_common/time_util.h"
#include "px_common/url_helper.h"
#include "px_message/proto_converter.h"
#include "px_message/proto_message_maker.h"
#include "px_message.pb.h"
#include "px_ft_engine/ft_async_session.h"
#include "px_ft_engine/ft_engine.h"
#include "px_rdp/rdp_client_endpoint.h"
#include "px_rdp/rdp_stream_packet.h"
#include "rdp/rdp_session.h"

#include <SDL3/SDL.h>
#include <freerdp/input.h>
#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <format>
#include <utility>

namespace px::client::imgui {
namespace {

std::string VoiceStatusText(const px::VoiceCallStatus& status) {
    switch (status.phase) {
    case px::VoiceCallPhase::kIdle:
        return status.supported ? "Ready" : "Unavailable";
    case px::VoiceCallPhase::kOutgoingPending:
        return "Calling";
    case px::VoiceCallPhase::kIncomingPending:
        return "Incoming call";
    case px::VoiceCallPhase::kConnected:
        return "Connected";
    }
    return "Unavailable";
}

} // namespace

std::shared_ptr<ClientSession> ClientSession::Create(ClientLaunchConfig config, std::shared_ptr<px::WindowsVideoResources> videoResources) {
    auto result = std::make_shared<ClientSession>(std::move(config), std::move(videoResources));
    return result->Initialize() ? result : std::shared_ptr<ClientSession>{};
}

ClientSession::ClientSession(ClientLaunchConfig config, std::shared_ptr<px::WindowsVideoResources> videoResources)
    : config_{std::move(config)}, videoResources_{std::move(videoResources)}, audio_{std::make_unique<ClientAudioOutput>()} {}
ClientSession::~ClientSession() {
    Stop();
}

bool ClientSession::Initialize() {
    if (config_.rdp)
        return InitializeRdp();
    notifier_ = std::make_shared<px::MessageNotifier>();
    listener_ = notifier_->CreateListener(px::MessageExecutionLane::kControl);
    sdk_ = px::ThunderSdk::Make(notifier_);
    if (!listener_ || !sdk_) {
        return false;
    }
    const std::string clientSignalId{"client_" + config_.localDeviceId + "_" + px::MD5::Hex(config_.remoteDeviceId)};
    const std::string remoteSignalId{"server_" + config_.remoteDeviceId};
    auto params = std::make_shared<px::ThunderSdkParams>();
    params->media_transport_ = config_.forceTcp || config_.forceRelay ? px::SdkMediaTransport::kWebSocket : px::SdkMediaTransport::kUdp;
    params->connection_route_ = config_.forceRelay ? px::SdkConnectionRoute::kWebSocketRelay : px::SdkConnectionRoute::kDirect;
    params->enable_audio_ = config_.audio;
    params->enable_video_ = !config_.fileTransferOnly;
    params->enable_controller_ = !config_.viewOnly;
    params->file_transfer_only_ = config_.fileTransferOnly;
    params->ip_ = config_.host;
    params->port_ = config_.port;
    params->udp_port_ = config_.port;
    params->client_type_ = px::ClientType::kWindows;
    params->bare_device_id_ = config_.localDeviceId;
    params->bare_remote_device_id_ = config_.remoteDeviceId;
    params->device_id_ = clientSignalId;
    params->remote_device_id_ = remoteSignalId;
    params->ft_device_id_ = "ft_" + clientSignalId;
    params->ft_remote_device_id_ = "ft_" + remoteSignalId;
    params->stream_id_ = config_.streamId;
    params->stream_name_ = config_.streamName;
    params->display_name_ = "Pixels Client";
    params->display_remote_name_ = config_.remoteDeviceId;
    params->device_name_ = "Pixels Windows";
    params->appkey_ = config_.appKey;
    params->relay_host_ = config_.relayHost;
    params->relay_port_ = config_.relayPort;
    params->relay_remote_device_id_ = config_.relayRemoteDeviceId.empty() ? remoteSignalId : config_.relayRemoteDeviceId;
    params->remote_password_hash_ = config_.remotePasswordHash;
    params->force_gdi_ = config_.forceGdiCapture;
    params->debug_ = config_.waitForDebugger;
    params->connection_nonce_ = config_.nonce;
    params->connection_instance_id_ = config_.instanceId;
    params->media_path_ =
        std::format("/media?only_audio=0&remote_device_id={}&stream_id={}&visitor_device_id={}&safety_pwd_md5={}",
                    UrlHelper::EncodeQueryComponent(config_.remoteDeviceId), UrlHelper::EncodeQueryComponent(config_.streamId),
                    UrlHelper::EncodeQueryComponent(config_.localDeviceId), UrlHelper::EncodeQueryComponent(config_.remotePasswordHash));
    params->ft_path_ =
        std::format("/file/transfer?remote_device_id={}&stream_id={}&visitor_device_id={}&safety_pwd_md5={}",
                    UrlHelper::EncodeQueryComponent(config_.remoteDeviceId), UrlHelper::EncodeQueryComponent(config_.streamId),
                    UrlHelper::EncodeQueryComponent(config_.localDeviceId), UrlHelper::EncodeQueryComponent(config_.remotePasswordHash));

    if (!videoResources_ || !sdk_->Init(params, px::MakeWindowsVideoDecoderFactory(videoResources_))) {
        return false;
    }

    statistics_ = px::SdkStatistics::Instance();
    const std::weak_ptr<ClientSession> weakSelf{shared_from_this()};
    fileTransfer_ = px::ft::FtAsyncSession::Create(
        [weakSelf](const std::shared_ptr<const px::Message>& message) {
            const auto self = weakSelf.lock();
            if (!self || !message || self->stopped_.load() || !self->sdk_) {
                return px::FileTransferSendResult::Disconnected("Client session is unavailable");
            }
            const auto outgoing = std::make_shared<px::Message>(*message);
            outgoing->set_type(outgoing->has_file_response() ? px::kFileResponse : px::kFileAction);
            outgoing->set_device_id(self->config_.localDeviceId);
            outgoing->set_stream_id(self->config_.streamId);
            return self->sdk_->PostFileTransferMessage(px::ProtoAsData(outgoing));
        },
        [weakSelf](const std::shared_ptr<px::ft::FtEngine>& engine) {
            engine->SetProgressCallback([weakSelf](const px::ft::TransferJobStatus& status) {
                if (const auto self = weakSelf.lock()) {
                    const ClientTransferJob converted{.id = status.id,
                                                      .totalBytes = status.total_size,
                                                      .completedBytes = status.finished_size,
                                                      .bytesPerSecond = status.speed,
                                                      .download = status.is_remote,
                                                      .done = status.done,
                                                      .error = status.error};
                    const std::scoped_lock lock{self->mutex_};
                    const auto found = std::ranges::find(self->transferJobs_, status.id, &ClientTransferJob::id);
                    if (found == self->transferJobs_.end())
                        self->transferJobs_.push_back(converted);
                    else
                        found[0] = converted;
                }
            });
            engine->SetOverwriteConfirmCallback([weakSelf](const std::int32_t jobId, const std::int32_t fileNumber, const std::string& path,
                                                           const bool upload, const bool identical) {
                if (const auto self = weakSelf.lock()) {
                    const std::scoped_lock lock{self->mutex_};
                    self->overwrite_ = ClientOverwriteRequest{jobId, fileNumber, path, upload, identical};
                }
            });
            engine->SetResponseCallback([weakSelf](const px::FileResponse& response) {
                const auto self = weakSelf.lock();
                if (!self || !response.has_dir())
                    return;
                std::vector<ClientRemoteEntry> entries{};
                entries.reserve(static_cast<std::size_t>(response.dir().entries_size()));
                for (const auto& entry : response.dir().entries()) {
                    entries.push_back({.name = entry.name(),
                                       .path = entry.abs_path().empty() ? (response.dir().path() + "/" + entry.name()) : entry.abs_path(),
                                       .size = entry.size(),
                                       .directory = entry.entry_type() == px::FileType::Dir || entry.entry_type() == px::FileType::DirLink ||
                                                    entry.entry_type() == px::FileType::DirDrive});
                }
                const std::scoped_lock lock{self->mutex_};
                self->remotePath_ = response.dir().path();
                self->remoteEntries_ = std::move(entries);
            });
        });
    fileTransferAvailable_ = fileTransfer_ && fileTransfer_->Start();

    px::VoiceCallDependencies voiceDependencies{.send_control =
                                                    [weakSelf](std::shared_ptr<px::Message> message) {
                                                        const auto self = weakSelf.lock();
                                                        return self && self->sdk_ && self->sdk_->PostReliableControlMessage(px::ProtoAsData(message));
                                                    },
                                                .send_audio =
                                                    [weakSelf](std::shared_ptr<px::Message> message) {
                                                        const auto self = weakSelf.lock();
                                                        return self && self->sdk_ && self->sdk_->PostVoiceAudioMessage(message);
                                                    },
                                                .create_audio = [] { return std::make_shared<px::VoiceAudioEndpointPort>(); },
                                                .post_task =
                                                    [weakSelf](std::function<void()> task) {
                                                        const auto self = weakSelf.lock();
                                                        if (!self || !self->sdk_ || !task)
                                                            return false;
                                                        self->sdk_->PostMiscTask(std::move(task));
                                                        return true;
                                                    },
                                                .status_changed =
                                                    [weakSelf](const px::VoiceCallStatus& status) {
                                                        if (const auto self = weakSelf.lock()) {
                                                            const std::scoped_lock lock{self->mutex_};
                                                            self->voiceStatus_ = VoiceStatusText(status);
                                                        }
                                                    }};
    voiceCall_ = px::VoiceCallController::Create({clientSignalId, config_.streamId}, std::move(voiceDependencies));

    listener_->Listen<px::SdkMsgNetworkConnected>([weakSelf](const auto&) {
        if (const auto self = weakSelf.lock()) {
            self->SetState(ClientConnectionState::Connecting, "Transport connected; waiting for remote desktop");
        }
    });
    listener_->Listen<px::SdkMsgNetworkDisConnected>([weakSelf](const auto&) {
        if (const auto self = weakSelf.lock(); self && !self->stopped_.load()) {
            self->SetState(ClientConnectionState::Disconnected, "Connection lost; retrying");
        }
    });
    listener_->Listen<px::SdkMsgUdpMediaUnavailable>([weakSelf](const auto&) {
        if (const auto self = weakSelf.lock()) {
            self->SetState(ClientConnectionState::MediaUnavailable, "UDP audio/video is unavailable; control remains connected");
        }
    });
    listener_->Listen<px::SdkMsgWsConnectionRejected>([weakSelf](const px::SdkMsgWsConnectionRejected& event) {
        if (const auto self = weakSelf.lock()) {
            switch (event.rejection_) {
            case px::WsControlRejection::kAuthorization:
                self->SetState(ClientConnectionState::Rejected, "The device password was rejected", ClientConnectionFailure::Authorization);
                break;
            case px::WsControlRejection::kOccupied:
                self->SetState(ClientConnectionState::Rejected, "The device is in use. Please try again in a few seconds",
                               ClientConnectionFailure::Occupied);
                break;
            case px::WsControlRejection::kSessionPolicy:
                self->SetState(ClientConnectionState::Rejected, "The device policy does not allow this connection",
                               ClientConnectionFailure::SessionPolicy);
                break;
            case px::WsControlRejection::kNone:
            default:
                self->SetState(ClientConnectionState::Rejected, "The remote control channel rejected the connection",
                               ClientConnectionFailure::Transport);
                break;
            }
        }
    });
    listener_->Listen<px::SdkMsgConnectionTakenOver>([weakSelf](const auto&) {
        if (const auto self = weakSelf.lock()) {
            self->SetState(ClientConnectionState::Rejected, "The session was taken over by another controller", ClientConnectionFailure::TakenOver);
        }
    });
    sdk_->SetOnServerConfigurationCallback([weakSelf](const std::shared_ptr<px::Message>& message) {
        const auto self = weakSelf.lock();
        if (!self || !message || !message->has_config()) {
            return;
        }
        std::vector<std::string> monitors{};
        std::vector<ClientResolution> resolutions{};
        monitors.reserve(static_cast<std::size_t>(message->config().monitors_info_size()));
        for (const auto& monitor : message->config().monitors_info()) {
            if (!monitor.name().empty())
                monitors.push_back(monitor.name());
            if (monitor.name() == message->config().capturing_monitor_name()) {
                for (const auto& resolution : monitor.resolutions()) {
                    if (resolution.width() > 0 && resolution.height() > 0)
                        resolutions.push_back({resolution.width(), resolution.height()});
                }
            }
        }
        const bool voiceAvailable = message->config().voice_call_enabled() && message->config().voice_call_protocol_version() == 1U;
        {
            const std::scoped_lock lock{self->mutex_};
            self->monitorName_ = message->config().capturing_monitor_name();
            self->monitors_ = std::move(monitors);
            self->resolutions_ = std::move(resolutions);
            self->fileTransferAvailable_ = self->fileTransferAvailable_ && message->config().file_transfer_enabled();
            self->voiceAvailable_ = voiceAvailable;
            self->virtualDisplayAvailable_ = message->config().virtual_display_enabled();
            self->virtualDisplayCount_ = message->config().virtual_display_owned_count();
            self->virtualDisplayMaximum_ = message->config().virtual_display_max_count();
        }
        if (const auto voice = self->VoiceCall())
            voice->SetCapabilities(voiceAvailable, message->config().voice_call_requires_headset());
        self->SetState(ClientConnectionState::Connected, "Connected");
    });
    sdk_->SetOnMonitorSwitchedCallback([weakSelf](const std::shared_ptr<px::Message>& message) {
        const auto self = weakSelf.lock();
        if (!self || !message || !message->has_monitor_switched()) {
            return;
        }
        const std::scoped_lock lock{self->mutex_};
        self->monitorName_ = message->monitor_switched().name();
        if (std::ranges::find(self->monitors_, self->monitorName_) == self->monitors_.end())
            self->monitors_.push_back(self->monitorName_);
    });
    sdk_->SetOnVideoFrameDecodedCallback([weakSelf](const std::shared_ptr<px::RawImage>& image, const px::SdkCaptureMonitorInfo& info) {
        const auto self = weakSelf.lock();
        if (!self || self->stopped_.load()) {
            return;
        }
        auto converted = RetainVideoFrame(image);
        if (!converted) {
            return;
        }
        const std::scoped_lock lock{self->mutex_};
        self->latestFrame_ = std::move(converted);
        ++self->decodedFrames_;
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - self->statisticsStarted_).count();
        if (elapsed >= 1'000) {
            self->framesPerSecond_ = static_cast<int>((self->decodedFrames_ * 1'000) / elapsed);
            const auto received = self->statistics_ ? self->statistics_->recv_data_size_.load() : 0;
            const auto delta = std::max<std::int64_t>(0, received - self->lastReceivedBytes_);
            self->bitrateKbps_ = static_cast<int>((delta * 8) / elapsed);
            self->lastReceivedBytes_ = received;
            self->decodedFrames_ = 0;
            self->statisticsStarted_ = now;
        }
        if (!info.mon_name_.empty()) {
            self->monitorName_ = info.mon_name_;
        }
    });
    sdk_->SetOnAudioFrameDecodedCallback([weakSelf](const std::shared_ptr<px::Data>& data, const int samples, const int channels, const int bits) {
        if (const auto self = weakSelf.lock(); self && !self->stopped_.load()) {
            bool enabled{};
            {
                const std::scoped_lock lock{self->mutex_};
                enabled = self->audioEnabled_;
            }
            if (enabled)
                static_cast<void>(self->audio_->Write(data, samples, channels, bits));
        }
    });
    sdk_->SetOnClipboardCallback([weakSelf](const std::shared_ptr<px::Message>& message) {
        if (const auto self = weakSelf.lock(); self && message && message->has_clipboard_info() &&
                                               message->clipboard_info().type() == px::kClipboardText && !message->clipboard_info().msg().empty()) {
            const std::scoped_lock lock{self->mutex_};
            self->remoteClipboardText_ = message->clipboard_info().msg();
        }
    });
    sdk_->SetOnHeartBeatCallback([weakSelf](const std::shared_ptr<px::Message>& message) {
        if (const auto self = weakSelf.lock(); self && message && message->has_on_heartbeat()) {
            const auto now = px::TimeUtil::GetCurrentTimestamp();
            const auto sent = message->on_heartbeat().timestamp();
            const std::scoped_lock lock{self->mutex_};
            self->latencyMilliseconds_ = static_cast<int>(now >= sent ? now - sent : 0);
        }
    });
    sdk_->SetOnRawMessageCallback([weakSelf](const std::shared_ptr<px::Message>& message) {
        const auto self = weakSelf.lock();
        if (!self || !message || self->stopped_.load())
            return;
        if (message->type() == px::kVoiceCallRequest || message->type() == px::kVoiceCallResponse || message->type() == px::kVoiceAudioConfig ||
            message->type() == px::kVoiceAudioFrame) {
            if (const auto voice = self->VoiceCall())
                voice->HandleMessage(message);
            return;
        }
        if (message->type() == px::kVirtualDisplayResponse && message->has_virtual_display_response()) {
            const auto& response = message->virtual_display_response();
            const std::scoped_lock lock{self->mutex_};
            self->virtualDisplayCount_ = response.owned_display_count();
            if (response.request_id() == self->virtualDisplayRequestId_)
                self->virtualDisplayRequestId_.clear();
            if (!response.accepted()) {
                self->status_ = response.error_message().empty() ? "Virtual display operation failed" : response.error_message();
            }
            return;
        }
        if (message->type() == px::kFileAction || message->type() == px::kFileResponse) {
            const auto fileTransfer = self->FileTransfer();
            if (!fileTransfer)
                return;
            static_cast<void>(fileTransfer->Post("pixels-client-ft-inbound", [message](const auto& engine) {
                if (message->type() == px::kFileAction && message->has_file_action()) {
                    engine->HandleFileAction(message->file_action(), message->stream_id());
                } else if (message->type() == px::kFileResponse && message->has_file_response()) {
                    engine->HandleFileResponse(message->file_response());
                }
            }));
        }
    });
    sdk_->SetOnEncodedVideoFrameCallback([weakSelf](const std::shared_ptr<px::Message>& message) {
        if (const auto self = weakSelf.lock()) {
            std::shared_ptr<px::RecordingSession> recording{};
            {
                const std::scoped_lock lock{self->mutex_};
                recording = self->recording_;
            }
            if (recording)
                static_cast<void>(recording->Submit(message));
        }
    });
    sdk_->SetOnEncodedAudioFrameCallback([weakSelf](const std::shared_ptr<px::Message>& message) {
        if (const auto self = weakSelf.lock()) {
            std::shared_ptr<px::RecordingSession> recording{};
            {
                const std::scoped_lock lock{self->mutex_};
                recording = self->recording_;
            }
            if (recording)
                static_cast<void>(recording->Submit(message));
        }
    });

    return true;
}
} // namespace px::client::imgui
