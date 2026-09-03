#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "px_common_new/file_transfer_send_result.h"
#include "transport_route.h"

namespace px {

class Data;

namespace render {

struct NetworkTransportHubSnapshot final {
    std::uint64_t control_attempts{0};
    std::uint64_t control_failures{0};
    std::uint64_t file_transfer_attempts{0};
    std::uint64_t file_transfer_failures{0};
    std::uint64_t voice_attempts{0};
    std::uint64_t voice_failures{0};
};

// Lifetime:
// - Owned by RdApplication for the complete Render runtime.
// - Callbacks capture only weak application ownership.
// - Payloads and routes are owned values; the hub retains no request state.
//
// Threading:
// - Send methods may be called concurrently by state and worker lanes.
// - Counters are atomic and callbacks must provide their own synchronization.
class NetworkTransportHub final {
public:
    using ControlSender = std::function<bool(
        const TransportRoute&, const std::shared_ptr<Data>&, bool)>;
    using FileTransferSender = std::function<FileTransferSendResult(
        const TransportRoute&, const std::shared_ptr<Data>&)>;
    using VoiceStreamSender = std::function<bool(
        const TransportRoute&, const std::shared_ptr<Data>&)>;
    using RtcAuthorizationSender = std::function<bool(
        const TransportRoute&, const std::string&, bool)>;
    using RtcPcmSender = std::function<bool(
        const TransportRoute&, const std::string&,
        const std::shared_ptr<const std::vector<std::int16_t>>&,
        int, int)>;

    [[nodiscard]] static std::shared_ptr<NetworkTransportHub> Create(
        ControlSender control_sender,
        FileTransferSender file_transfer_sender,
        VoiceStreamSender voice_stream_sender = {},
        RtcAuthorizationSender rtc_authorization_sender = {},
        RtcPcmSender rtc_pcm_sender = {});

    NetworkTransportHub(ControlSender control_sender,
                        FileTransferSender file_transfer_sender,
                        VoiceStreamSender voice_stream_sender = {},
                        RtcAuthorizationSender rtc_authorization_sender = {},
                        RtcPcmSender rtc_pcm_sender = {});

    [[nodiscard]] bool SendControl(
        const TransportRoute& route,
        const std::shared_ptr<Data>& message,
        bool run_through = false) const;
    [[nodiscard]] FileTransferSendResult SendFileTransfer(
        const TransportRoute& route,
        const std::shared_ptr<Data>& message) const;
    [[nodiscard]] bool SendVoice(
        const TransportRoute& route,
        const std::shared_ptr<Data>& message) const;
    [[nodiscard]] bool SetRtcVoiceAuthorization(
        const TransportRoute& route,
        const std::string& call_id,
        bool authorized) const;
    [[nodiscard]] bool SendRtcVoicePcm(
        const TransportRoute& route,
        const std::string& call_id,
        const std::shared_ptr<const std::vector<std::int16_t>>& samples,
        int sample_rate,
        int channels) const;
    [[nodiscard]] NetworkTransportHubSnapshot Snapshot() const;

private:
    ControlSender control_sender_;
    FileTransferSender file_transfer_sender_;
    VoiceStreamSender voice_stream_sender_;
    RtcAuthorizationSender rtc_authorization_sender_;
    RtcPcmSender rtc_pcm_sender_;
    mutable std::atomic_uint64_t control_attempts_{0};
    mutable std::atomic_uint64_t control_failures_{0};
    mutable std::atomic_uint64_t file_transfer_attempts_{0};
    mutable std::atomic_uint64_t file_transfer_failures_{0};
    mutable std::atomic_uint64_t voice_attempts_{0};
    mutable std::atomic_uint64_t voice_failures_{0};
};

}  // namespace render
}  // namespace px
