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
#include "client_session.h"
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
#include "rdp/rdp_session.h"

namespace px::client::imgui {

void ClientSession::Start() {
    if (!started_.exchange(true)) {
        if (config_.rdp && rdpNetwork_)
            rdpNetwork_->Start();
        else if (sdk_)
            sdk_->Start();
    }
}

void ClientSession::Stop() {
    if (stopped_.exchange(true)) {
        return;
    }
    if (audio_) {
        audio_->Stop();
    }
    std::shared_ptr<px::rdp::RdpSession> rdpSession{};
    std::shared_ptr<px::rdp::RdpClientEndpoint> rdpEndpoint{};
    {
        const std::scoped_lock lock{mutex_};
        rdpSession = std::move(rdpSession_);
        rdpEndpoint = std::move(rdpEndpoint_);
    }
    if (rdpSession) rdpSession->Stop();
    if (rdpEndpoint) rdpEndpoint->Stop();
    if (rdpNetwork_) {
        rdpNetwork_->Exit();
        rdpNetwork_.reset();
    }
    std::shared_ptr<px::VoiceCallController> voice{};
    std::shared_ptr<px::ft::FtAsyncSession> fileTransfer{};
    std::shared_ptr<px::RecordingSession> recording{};
    {
        const std::scoped_lock lock{mutex_};
        voice = std::move(voiceCall_);
        fileTransfer = std::move(fileTransfer_);
        recording = std::move(recording_);
        if (recording) finishingRecordings_.push_back(recording);
    }
    if (voice) voice->Close();
    if (recording) recording->Stop();
    if (fileTransfer) static_cast<void>(fileTransfer->StopAndWait(std::chrono::seconds{2}));
    if (sdk_) {
        sdk_->Exit();
    }
    if (notifier_) {
        notifier_->Stop(px::MessageBusStopMode::kCancel);
    }
}

ClientSessionSnapshot ClientSession::Snapshot() const {
    const std::scoped_lock lock{mutex_};
    return {.state = state_,
            .failure = failure_,
            .status = status_,
            .monitorName = monitorName_,
            .frame = latestFrame_,
            .monitors = monitors_,
            .resolutions = resolutions_,
            .framesPerSecond = framesPerSecond_,
            .latencyMilliseconds = latencyMilliseconds_,
            .bitrateKbps = bitrateKbps_,
            .decodedAudioFrames = decodedAudioFrames_,
            .decodedAudioBytes = decodedAudioBytes_,
            .decoder = statistics_ ? statistics_->video_decoder_.Clone() : std::string{},
            .fileTransferAvailable = fileTransferAvailable_,
            .voiceAvailable = voiceAvailable_,
            .recording = !recordingId_.empty(),
            .virtualDisplayAvailable = virtualDisplayAvailable_,
            .virtualDisplayCount = virtualDisplayCount_,
            .virtualDisplayMaximum = virtualDisplayMaximum_,
            .virtualDisplayBusy = !virtualDisplayRequestId_.empty(),
            .voiceStatus = voiceStatus_,
            .remoteCursor = remoteCursor_};
}

bool ClientSession::SendMedia(const std::shared_ptr<px::Data>& data) const {
    if (!data || !sdk_ || !started_.load() || stopped_.load()) {
        return false;
    }
    sdk_->PostMediaMessage(data);
    return true;
}
}  // namespace px::client::imgui
