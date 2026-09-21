#include "client_session.h"

#include <SDL3/SDL.h>
#include <Windows.h>
#include <freerdp/input.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <format>
#include <utility>

#include "client_audio_output.h"
#include "ct_virtual_display_protocol.h"
#include "px_client_sdk/platform/voice_audio_endpoint_port.h"
#include "px_client_sdk/platform/windows/windows_decoder_factory.h"
#include "px_client_sdk/platform/windows/windows_video_resources.h"
#include "px_client_sdk/sdk_connection_params.h"
#include "px_client_sdk/sdk_messages.h"
#include "px_client_sdk/sdk_net_client.h"
#include "px_client_sdk/sdk_params.h"
#include "px_client_sdk/sdk_recording_session.h"
#include "px_client_sdk/sdk_statistics.h"
#include "px_client_sdk/sdk_voice_call.h"
#include "px_client_sdk/thunder_sdk.h"
#include "px_common/console_frontend_relay_credential.h"
#include "px_common/data.h"
#include "px_common/md5.h"
#include "px_common/message_notifier.h"
#include "px_common/time_util.h"
#include "px_common/url_helper.h"
#include "px_ft_engine/ft_async_session.h"
#include "px_ft_engine/ft_engine.h"
#include "px_message.pb.h"
#include "px_message/proto_converter.h"
#include "px_message/proto_message_maker.h"
#include "px_rdp/rdp_client_endpoint.h"
#include "px_rdp/rdp_stream_packet.h"
#include "px_ui/product_brand.h"
#include "rdp/rdp_session.h"

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

}  // namespace

std::shared_ptr<ClientSession> ClientSession::Create(ClientLaunchConfig config, std::shared_ptr<px::WindowsVideoResources> videoResources) {
    auto result = std::make_shared<ClientSession>(std::move(config), std::move(videoResources));
    return result->Initialize() ? result : std::shared_ptr<ClientSession>{};
}

ClientSession::ClientSession(ClientLaunchConfig config, std::shared_ptr<px::WindowsVideoResources> videoResources)
    : config_{std::move(config)}, videoResources_{std::move(videoResources)}, audio_{std::make_unique<ClientAudioOutput>()} {}
ClientSession::~ClientSession() { Stop(); }

bool ClientSession::Initialize() {
    if (config_.rdp) return InitializeRdp();
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
    params->client_type_ = px::ClientType::kWindows;
    params->bare_device_id_ = config_.localDeviceId;
    params->bare_remote_device_id_ = config_.remoteDeviceId;
    params->device_id_ = clientSignalId;
    params->remote_device_id_ = remoteSignalId;
    params->ft_device_id_ = "ft_" + clientSignalId;
    params->ft_remote_device_id_ = "ft_" + remoteSignalId;
    params->stream_id_ = config_.streamId;
    params->stream_name_ = config_.streamName;
    params->display_name_ = px::ui::WindowsProductName();
    params->display_remote_name_ = config_.remoteDeviceId;
    params->device_name_ = std::string{px::ui::ApplicationName()} + " Windows";
    params->appkey_ = config_.appKey;
    params->relay_host_ = config_.relayHost;
    params->relay_port_ = config_.relayPort;
    params->relay_remote_device_id_ = config_.relayRemoteDeviceId.empty() ? remoteSignalId : config_.relayRemoteDeviceId;
    params->remote_password_hash_ = config_.forceRelay && config_.frontendToken
                                        ? px::BuildConsoleFrontendRelayCredential(config_.frontendSessionRevision, config_.frontendToken->View())
                                        : config_.remotePasswordHash;
    params->force_gdi_ = config_.forceGdiCapture;
    params->debug_ = config_.waitForDebugger;
    params->connection_nonce_ = config_.nonce;
    params->connection_instance_id_ = config_.instanceId;
    params->media_path_ = BuildClientMediaPath(config_);
    params->ft_path_ = BuildClientFileTransferPath(config_);

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
                    const std::scoped_lock lock{self->mutex_};
                    const auto found = std::ranges::find(self->transferJobs_, status.id, &ClientTransferJob::id);
                    if (found == self->transferJobs_.end()) {
                        self->transferJobs_.push_back({.id = status.id,
                                                       .totalBytes = status.total_size,
                                                       .completedBytes = status.finished_size,
                                                       .bytesPerSecond = status.speed,
                                                       .fileNumber = status.file_num,
                                                       .fileCount = status.file_count,
                                                       .download = status.is_remote,
                                                       .done = status.done,
                                                       .error = status.error});
                    } else {
                        found->totalBytes = status.total_size;
                        found->completedBytes = status.finished_size;
                        found->bytesPerSecond = status.speed;
                        found->fileNumber = status.file_num;
                        found->fileCount = status.file_count;
                        found->download = status.is_remote;
                        found->done = status.done;
                        found->error = status.error;
                    }
                }
            });
            engine->SetJobDoneCallback([weakSelf](const std::int32_t jobId, const std::int32_t fileNumber, const std::string& error) {
                if (const auto self = weakSelf.lock()) {
                    const std::scoped_lock lock{self->mutex_};
                    const auto found = std::ranges::find(self->transferJobs_, jobId, &ClientTransferJob::id);
                    if (found != self->transferJobs_.end()) {
                        found->fileNumber = fileNumber;
                        found->done = error.empty();
                        found->error = error;
                        found->bytesPerSecond = 0.0;
                        if (found->done && found->totalBytes > 0U) found->completedBytes = found->totalBytes;
                    } else {
                        self->transferJobs_.push_back({.id = jobId, .fileNumber = fileNumber, .done = error.empty(), .error = error});
                    }
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
                if (!self) return;
                if (!response.has_dir()) {
                    const std::scoped_lock lock{self->mutex_};
                    self->remoteFileOperationResult_ = {.success = response.has_done(),
                                                        .error = response.has_error() ? response.error().error() : std::string{}};
                    return;
                }
                // Directory replies generated for transfer jobs carry that job's non-zero id.
                // They initialize the writer and must not replace the browser's current path/list.
                if (response.dir().id() != 0) return;
                std::vector<ClientRemoteEntry> entries{};
                entries.reserve(static_cast<std::size_t>(response.dir().entries_size()));
                for (const auto& entry : response.dir().entries()) {
                    std::string entryPath{entry.abs_path()};
                    if (entryPath.empty()) {
                        entryPath = response.dir().path();
                        if (!entryPath.empty() && !entryPath.ends_with('/') && !entryPath.ends_with('\\')) entryPath.push_back('/');
                        entryPath += entry.name();
                    }
                    entries.push_back({.name = entry.name(),
                                       .path = std::move(entryPath),
                                       .size = entry.size(),
                                       .modifiedTime = entry.modified_time(),
                                       .directory = entry.entry_type() == px::FileType::Dir || entry.entry_type() == px::FileType::DirLink ||
                                                    entry.entry_type() == px::FileType::DirDrive,
                                       .hidden = entry.is_hidden()});
                }
                const std::scoped_lock lock{self->mutex_};
                self->remotePath_ = response.dir().path();
                if (response.dir().path() == "/") self->remoteLocations_ = entries;
                self->remoteEntries_ = std::move(entries);
            });
        });
    fileTransferAvailable_ = fileTransfer_ && fileTransfer_->Start();
    LOGI("event=file_transfer.session component=client operation=start outcome={}", fileTransferAvailable_ ? "success" : "failed");

    if (!config_.fileTransferOnly) {
        px::VoiceCallDependencies voiceDependencies{.send_control =
                                                        [weakSelf](std::shared_ptr<px::Message> message) {
                                                            const auto self = weakSelf.lock();
                                                            return self && self->sdk_ &&
                                                                   self->sdk_->PostReliableControlMessage(px::ProtoAsData(message));
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
                                                            if (!self || !self->sdk_ || !task) return false;
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
    }

    listener_->Listen<px::SdkMsgNetworkConnected>([weakSelf](const auto&) {
        if (const auto self = weakSelf.lock()) {
            if (self->config_.fileTransferOnly) {
                self->SetState(ClientConnectionState::Connected, "File transfer connected");
                static_cast<void>(self->ListRemoteDirectory("/"));
            } else {
                self->SetState(ClientConnectionState::Connecting, "Transport connected; waiting for remote desktop");
            }
        }
    });
    listener_->Listen<px::SdkMsgNetworkDisConnected>([weakSelf](const auto&) {
        if (const auto self = weakSelf.lock(); self && !self->stopped_.load()) {
            self->SetState(ClientConnectionState::Disconnected, "Connection lost; retrying");
        }
    });
    listener_->Listen<px::SdkMsgUdpMediaUnavailable>([weakSelf](const auto&) {
        if (const auto self = weakSelf.lock()) {
            if (const auto voice = self->VoiceCall()) voice->SetTransportAvailable(false);
            self->SetState(ClientConnectionState::MediaUnavailable, "UDP audio/video is unavailable; control remains connected");
        }
    });
    listener_->Listen<px::SdkMsgUdpMediaAvailable>([weakSelf](const auto&) {
        if (const auto self = weakSelf.lock()) {
            if (const auto voice = self->VoiceCall()) voice->SetTransportAvailable(true);
            self->SetState(ClientConnectionState::Connected, "Connected");
        }
    });
    listener_->Listen<px::SdkMsgWsConnectionRejected>([weakSelf](const px::SdkMsgWsConnectionRejected& event) {
        if (const auto self = weakSelf.lock()) {
            switch (event.rejection_) {
                case px::WsControlRejection::kAuthorization:
                    self->SetState(ClientConnectionState::Rejected, "The device password was rejected", ClientConnectionFailure::Authorization);
                    break;
                case px::WsControlRejection::kRemoteAccessDisabled:
                    self->SetState(ClientConnectionState::Rejected, "Remote access is disabled on the remote device",
                                   ClientConnectionFailure::RemoteAccessDisabled);
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
            if (!monitor.name().empty()) monitors.push_back(monitor.name());
            if (monitor.name() == message->config().capturing_monitor_name()) {
                for (const auto& resolution : monitor.resolutions()) {
                    if (resolution.width() > 0 && resolution.height() > 0) resolutions.push_back({resolution.width(), resolution.height()});
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
            LOGI("event=file_transfer.capability component=client operation=configure outcome={} host_enabled={}",
                 self->fileTransferAvailable_ ? "enabled" : "disabled", message->config().file_transfer_enabled());
            self->voiceAvailable_ = voiceAvailable;
            self->virtualDisplayAvailable_ = message->config().virtual_display_enabled();
            self->virtualDisplayCount_ = message->config().virtual_display_owned_count();
            self->virtualDisplayMaximum_ = message->config().virtual_display_max_count();
        }
        if (const auto voice = self->VoiceCall()) voice->SetCapabilities(voiceAvailable, message->config().voice_call_requires_headset());
        self->SetState(ClientConnectionState::Connected, "Connected");
    });
    sdk_->SetOnMonitorSwitchedCallback([weakSelf](const std::shared_ptr<px::Message>& message) {
        const auto self = weakSelf.lock();
        if (!self || !message || !message->has_monitor_switched()) {
            return;
        }
        const std::scoped_lock lock{self->mutex_};
        self->monitorName_ = message->monitor_switched().name();
        if (std::ranges::find(self->monitors_, self->monitorName_) == self->monitors_.end()) self->monitors_.push_back(self->monitorName_);
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
    sdk_->SetOnCursorInfoCallback([weakSelf](const std::shared_ptr<px::Message>& message) {
        const auto self = weakSelf.lock();
        if (!self || !message || !message->has_cursor_info_sync() || self->stopped_.load()) return;
        const auto& cursor = message->cursor_info_sync();
        const std::scoped_lock lock{self->mutex_};
        self->remoteCursor_ = {.received = true, .visible = cursor.visible(), .type = static_cast<std::uint32_t>(cursor.type())};
    });
    sdk_->SetOnAudioFrameDecodedCallback(
        [weakSelf](const std::shared_ptr<px::Data>& audioData, const int sampleRate, const int channelCount, const int bitsPerSample) {
            if (const auto self = weakSelf.lock(); self && !self->stopped_.load()) {
                bool enabled{};
                {
                    const std::scoped_lock lock{self->mutex_};
                    enabled = self->audioEnabled_;
                    if (audioData && audioData->Size() > 0 && sampleRate > 0 && channelCount > 0 && bitsPerSample > 0) {
                        ++self->decodedAudioFrames_;
                        self->decodedAudioBytes_ += static_cast<std::uint64_t>(audioData->Size());
                    }
                }
                if (enabled) static_cast<void>(self->audio_->Write(audioData, sampleRate, channelCount, bitsPerSample));
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
        if (!self || !message || self->stopped_.load()) return;
        if (message->type() == px::kVoiceCallRequest || message->type() == px::kVoiceCallResponse || message->type() == px::kVoiceAudioConfig ||
            message->type() == px::kVoiceAudioFrame) {
            if (const auto voice = self->VoiceCall()) voice->HandleMessage(message);
            return;
        }
        if (message->type() == px::kVirtualDisplayResponse && message->has_virtual_display_response()) {
            const auto& response = message->virtual_display_response();
            const std::scoped_lock lock{self->mutex_};
            self->virtualDisplayCount_ = response.owned_display_count();
            if (response.request_id() == self->virtualDisplayRequestId_) self->virtualDisplayRequestId_.clear();
            if (!response.accepted()) {
                self->status_ = response.error_message().empty() ? "Virtual display operation failed" : response.error_message();
            }
            return;
        }
        if (message->type() == px::kFileAction || message->type() == px::kFileResponse) {
            const auto fileTransfer = self->FileTransfer();
            if (!fileTransfer) return;
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
            if (recording) static_cast<void>(recording->Submit(message));
        }
    });
    sdk_->SetOnEncodedAudioFrameCallback([weakSelf](const std::shared_ptr<px::Message>& message) {
        if (const auto self = weakSelf.lock()) {
            std::shared_ptr<px::RecordingSession> recording{};
            {
                const std::scoped_lock lock{self->mutex_};
                recording = self->recording_;
            }
            if (recording) static_cast<void>(recording->Submit(message));
        }
    });

    return true;
}
}  // namespace px::client::imgui
