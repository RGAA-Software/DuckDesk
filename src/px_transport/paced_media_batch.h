#pragma once

#include <asio2/external/asio.hpp>
#include <functional>
#include <memory>
#include <utility>

#include "media_transport/media_wire.h"
#include "media_transport/send_policy.h"
#include "video_send_pacing.h"

namespace px {

// One bounded frame/audio batch. All methods and callbacks run on the socket executor.
// Timers yield between bursts so receiving feedback/audio never waits behind video pacing.
class PacedMediaBatch final : public std::enable_shared_from_this<PacedMediaBatch> {
public:
    using SendBurst = std::function<bool(std::span<const media::Packet>)>;
    PacedMediaBatch(asio::any_io_executor executor, std::vector<media::Packet> packets, bool unpaced, SendBurst send_burst,
                    std::function<bool()> can_continue, std::function<void(bool)> completion,
                    std::shared_ptr<transport::VideoSendPacing> video_pacing = {}, SendBurst can_submit = {})
        : timer_(std::move(executor)),
          packets_(std::move(packets)),
          unpaced_(unpaced),
          send_burst_(std::move(send_burst)),
          can_continue_(std::move(can_continue)),
          completion_(std::move(completion)),
          video_pacing_(std::move(video_pacing)),
          can_submit_(std::move(can_submit)) {}
    ~PacedMediaBatch() { Complete(false); }

    void Start() {
        pacing_start_ = std::chrono::steady_clock::now();
        Pump();
    }

private:
    void Complete(bool succeeded) {
        if (const auto completion = std::exchange(completion_, {})) completion(succeeded);
    }

    void Pump() {
        if (!completion_) return;
        if (!can_continue_ || !can_continue_()) {
            Complete(false);
            return;
        }
        if (packet_offset_ == packets_.size()) {
            Complete(true);
            return;
        }
        const auto send_started = std::chrono::steady_clock::now();
        if (video_pacing_ && send_started < video_pacing_->NextSendTime()) {
            WaitUntil(video_pacing_->NextSendTime());
            return;
        }
        const auto packet_bytes = packets_[packet_offset_].size();
        if (packet_bytes == 0 || !send_burst_) {
            Complete(false);
            return;
        }
        const auto socket_limit = std::max<std::size_t>(1, 65536 / packet_bytes);
        const auto batch_limit = video_pacing_ ? std::min(socket_limit, transport::VideoSendBudget::kPacketsPerBurst)
                                 : unpaced_    ? socket_limit
                                               : std::min(socket_limit, media::UdpVideoBurstPacing::kPacketsPerBatch);
        const auto packet_count = std::min(batch_limit, packets_.size() - packet_offset_);
        const auto burst = std::span<const media::Packet>{packets_}.subspan(packet_offset_, packet_count);
        bool sent{};
        try {
            // Leave already encoded fragments intact while QUIC is congested.
            // Yield so control, audio and cancellation can run on this executor.
            // The frame's existing deadline still bounds the wait.
            if (can_submit_ && !can_submit_(burst)) {
                WaitUntil(send_started + std::chrono::milliseconds(1));
                return;
            }
            sent = send_burst_(burst);
        } catch (const std::exception&) {
            Complete(false);
            return;
        }
        if (!sent) {
            Complete(false);
            return;
        }
        packet_offset_ += packet_count;
        if (video_pacing_) {
            std::uint64_t wire_bytes{};
            for (const auto& packet : burst) wire_bytes += packet.size() + transport::VideoSendBudget::kPacketWireOverhead;
            video_pacing_->Sent(wire_bytes, send_started);
        }
        for (const auto& packet : burst) paced_wire_bytes_ += packet.size() + 48;
        if (packet_offset_ == packets_.size()) {
            Complete(true);
            return;
        }
        const auto owner = shared_from_this();
        if (unpaced_) {
            asio::post(timer_.get_executor(), [owner] { owner->Pump(); });
            return;
        }
        // An overdue burst still yields through the executor; never spin or catch up an entire frame in one callback.
        WaitUntil(pacing_start_ + media::UdpVideoBurstPacing::Duration(paced_wire_bytes_));
    }

    void WaitUntil(std::chrono::steady_clock::time_point deadline) {
        timer_.expires_at(deadline);
        const auto owner = shared_from_this();
        timer_.async_wait([owner](const asio::error_code& error) {
            if (error)
                owner->Complete(false);
            else
                owner->Pump();
        });
    }

    asio::steady_timer timer_;
    std::vector<media::Packet> packets_{};
    bool unpaced_{};
    SendBurst send_burst_{};
    std::function<bool()> can_continue_{};
    std::function<void(bool)> completion_{};
    std::shared_ptr<transport::VideoSendPacing> video_pacing_{};
    SendBurst can_submit_{};
    std::size_t packet_offset_{};
    std::uint64_t paced_wire_bytes_{};
    std::chrono::steady_clock::time_point pacing_start_{};
};
}  // namespace px
