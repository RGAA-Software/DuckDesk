#pragma once

#include "iroh_server.h"
#include "px_rdp/rdp_tcp_bridge.h"
#include "px_transport/media_datagrams.h"
#include "px_transport/message_session.h"
#include "px_transport/video_rate_control.h"

namespace px {

// Owns business routing for one admitted frontend; mode and loopback port come from Render configuration.
class IrohSession final : public std::enable_shared_from_this<IrohSession> {
public:
    IrohSession(AcceptedIrohFrontend accepted, RenderEventCallback events);
    ~IrohSession();
    [[nodiscard]] bool Start(asio::any_io_executor executor, std::uint16_t rdp_proxy_port = 0);
    void Close(ResourceChannelCloseOutcome outcome = ResourceChannelCloseOutcome::kUserStopped);
    void Send(std::shared_ptr<Data> payload, std::function<void(bool)> completion = {});
    [[nodiscard]] FileTransferSendResult SendFile(std::shared_ptr<Data> payload);
    [[nodiscard]] bool HasVideo() const { return !accepted_.frontend->IsRdp() && accepted_.frontend->Allows("view"); }
    void UpdatePermissions(const std::vector<std::string>& permissions);
    [[nodiscard]] bool SendDatagram(std::span<const std::uint8_t> payload);
    [[nodiscard]] bool SendVideo(const media::VideoFrame& frame);
    [[nodiscard]] bool SendAudio(std::span<const std::uint8_t> opus);
    [[nodiscard]] bool SendVoice(const Message& message);
    [[nodiscard]] bool IsAlive() const;
    [[nodiscard]] std::uint64_t VideoEncodingBitrate(std::uint64_t ceiling_bps) const;
    [[nodiscard]] const std::string& StreamId() const { return accepted_.frontend->StreamId(); }
    [[nodiscard]] const std::string& BindingId() const { return accepted_.frontend->BindingId(); }

private:
    void Receive(transport::ChannelKind kind, std::shared_ptr<Data> payload, bool voice_datagram = false);
    void ReportTraffic(std::size_t sent_bytes, std::size_t received_bytes);
    void RequestKeyFrame(const std::string& monitor);
    bool AllowVideoRecovery(const std::string& monitor);
    void Publish(RenderEvent event);
    AcceptedIrohFrontend accepted_{};
    RenderEventCallback events_{};
    std::mutex mutex_{};
    std::shared_ptr<transport::MessageSession> messages_{};
    std::shared_ptr<rdp::RdpTcpBridge> rdp_bridge_{};
    std::shared_ptr<transport::MediaDatagramSender> media_sender_{};
    std::map<std::string, std::chrono::steady_clock::time_point> video_recovery_requests_{};
    std::atomic_bool closed_{};
    bool started_once_{};
    mutable std::mutex rate_mutex_{};
    mutable transport::VideoRateControl rate_control_{};
    transport::VideoDeliveryProgress delivery_progress_{};
    mutable transport::ConnectionSnapshot rate_snapshot_{};
    mutable std::chrono::steady_clock::time_point next_rate_sample_{};
    mutable std::uint64_t last_rate_bps_{};
    mutable std::atomic_uint64_t offered_video_frames_{};
    mutable std::atomic_uint64_t dropped_video_frames_{};
    mutable bool sampled_media_backpressure_{};
};
}  // namespace px
