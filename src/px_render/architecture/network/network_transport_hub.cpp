#include "network_transport_hub.h"

#include <utility>

#include "px_common_new/data.h"

namespace px::render {

std::shared_ptr<NetworkTransportHub> NetworkTransportHub::Create(
    ControlSender control_sender,
    FileTransferSender file_transfer_sender,
    VoiceStreamSender voice_stream_sender,
    RtcAuthorizationSender rtc_authorization_sender,
    RtcPcmSender rtc_pcm_sender) {
    return std::make_shared<NetworkTransportHub>(
        std::move(control_sender), std::move(file_transfer_sender),
        std::move(voice_stream_sender),
        std::move(rtc_authorization_sender), std::move(rtc_pcm_sender));
}

NetworkTransportHub::NetworkTransportHub(
    ControlSender control_sender,
    FileTransferSender file_transfer_sender,
    VoiceStreamSender voice_stream_sender,
    RtcAuthorizationSender rtc_authorization_sender,
    RtcPcmSender rtc_pcm_sender)
    : control_sender_(std::move(control_sender)),
      file_transfer_sender_(std::move(file_transfer_sender)),
      voice_stream_sender_(std::move(voice_stream_sender)),
      rtc_authorization_sender_(std::move(rtc_authorization_sender)),
      rtc_pcm_sender_(std::move(rtc_pcm_sender)) {}

bool NetworkTransportHub::SendControl(
    const TransportRoute& route,
    const std::shared_ptr<Data>& message,
    const bool run_through) const {
    ++control_attempts_;
    if (!message || route.stream_id.empty() || !control_sender_ ||
        !control_sender_(route, message, run_through)) {
        ++control_failures_;
        return false;
    }
    return true;
}

FileTransferSendResult NetworkTransportHub::SendFileTransfer(
    const TransportRoute& route,
    const std::shared_ptr<Data>& message) const {
    ++file_transfer_attempts_;
    if (!message || route.stream_id.empty() || !file_transfer_sender_) {
        ++file_transfer_failures_;
        return FileTransferSendResult::TransportError(
            "invalid file-transfer route or payload");
    }
    auto result = file_transfer_sender_(route, message);
    if (!result.accepted()) {
        ++file_transfer_failures_;
    }
    return result;
}

bool NetworkTransportHub::SendVoice(
    const TransportRoute& route,
    const std::shared_ptr<Data>& message) const {
    ++voice_attempts_;
    if (!message || route.stream_id.empty() || !voice_stream_sender_ ||
        !voice_stream_sender_(route, message)) {
        ++voice_failures_;
        return false;
    }
    return true;
}

bool NetworkTransportHub::SetRtcVoiceAuthorization(
    const TransportRoute& route,
    const std::string& call_id,
    const bool authorized) const {
    ++voice_attempts_;
    if (route.stream_id.empty() || call_id.empty() ||
        !rtc_authorization_sender_ ||
        !rtc_authorization_sender_(route, call_id, authorized)) {
        ++voice_failures_;
        return false;
    }
    return true;
}

bool NetworkTransportHub::SendRtcVoicePcm(
    const TransportRoute& route,
    const std::string& call_id,
    const std::shared_ptr<const std::vector<std::int16_t>>& samples,
    const int sample_rate,
    const int channels) const {
    ++voice_attempts_;
    if (route.stream_id.empty() || call_id.empty() || !samples ||
        samples->empty() || !rtc_pcm_sender_ ||
        !rtc_pcm_sender_(route, call_id, samples, sample_rate, channels)) {
        ++voice_failures_;
        return false;
    }
    return true;
}

NetworkTransportHubSnapshot NetworkTransportHub::Snapshot() const {
    return NetworkTransportHubSnapshot{
        .control_attempts = control_attempts_.load(),
        .control_failures = control_failures_.load(),
        .file_transfer_attempts = file_transfer_attempts_.load(),
        .file_transfer_failures = file_transfer_failures_.load(),
        .voice_attempts = voice_attempts_.load(),
        .voice_failures = voice_failures_.load(),
    };
}

}  // namespace px::render
