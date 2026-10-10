#pragma once

#include <asio2/external/asio.hpp>
#include <functional>
#include <memory>
#include <utility>

#include "media_transport/media_wire.h"
#include "media_transport/send_policy.h"

namespace px {

// One bounded frame/audio batch. All methods and callbacks run on the socket executor.
// Timers yield between bursts so receiving feedback/audio never waits behind video pacing.
class PacedMediaBatch final : public std::enable_shared_from_this<PacedMediaBatch> {
public:
    using SendBurst = std::function<bool(std::span<const media::Packet>)>;
    PacedMediaBatch(asio::any_io_executor executor, std::vector<media::Packet> packets, bool unpaced, SendBurst send_burst,
                    std::function<bool()> can_continue, std::function<void(bool)> completion)
        : timer_(std::move(executor)),
          packets_(std::move(packets)),
          unpaced_(unpaced),
          send_burst_(std::move(send_burst)),
          can_continue_(std::move(can_continue)),
          completion_(std::move(completion)) {}
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
        const auto packet_bytes = packets_[packet_offset_].size();
        if (packet_bytes == 0 || !send_burst_) {
            Complete(false);
            return;
        }
        const auto socket_limit = std::max<std::size_t>(1, 65536 / packet_bytes);
        const auto batch_limit = unpaced_ ? socket_limit : std::min(socket_limit, media::UdpVideoBurstPacing::kPacketsPerBatch);
        const auto packet_count = std::min(batch_limit, packets_.size() - packet_offset_);
        const auto burst = std::span<const media::Packet>{packets_}.subspan(packet_offset_, packet_count);
        bool sent{};
        try {
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
        timer_.expires_at(pacing_start_ + media::UdpVideoBurstPacing::Duration(paced_wire_bytes_));
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
    std::size_t packet_offset_{};
    std::uint64_t paced_wire_bytes_{};
    std::chrono::steady_clock::time_point pacing_start_{};
};
}  // namespace px
