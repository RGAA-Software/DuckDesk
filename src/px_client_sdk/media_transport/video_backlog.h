#pragma once

#include <chrono>
#include <cstdint>
#include <cstddef>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <string>

namespace px::media {

// A bounded pre-packetization queue. Dropping here must invalidate the reference chain:
// transport sequence numbers have not been assigned yet, so the receiver cannot detect this loss.
template <class Payload> class VideoBacklog final {
  public:
    using Clock = std::chrono::steady_clock;
    struct Frame final {
        std::string stream{};
        Payload payload{};
        std::size_t bytes{};
        bool idr{};
        Clock::time_point queued{};
        std::uint64_t generation{};
    };
    struct Delivery final {
        Frame frame{};
        bool discard{};
    };
    static constexpr std::size_t kMaxFrames = 4;
    static constexpr std::size_t kMaxBytes = 4 * 1024 * 1024;
    static constexpr auto kMaxAge = std::chrono::milliseconds(100);

    bool Push(Frame frame) {
        std::lock_guard lock(mutex_);
        if (closed_)
            return false;
        if (!streams_.contains(frame.stream) && streams_.size() >= 256) {
            ++dropped_;
            return false;
        }
        auto& state = streams_[frame.stream];
        if (frames_.size() >= kMaxFrames || frame.bytes > kMaxBytes - bytes_) {
            ++state.generation;
            state.needs_idr = true;
            ++dropped_;
            return false;
        }
        frame.generation = state.generation;
        bytes_ += frame.bytes;
        frames_.push_back(std::move(frame));
        return true;
    }
    std::optional<Delivery> Pop(Clock::time_point now) {
        std::lock_guard lock(mutex_);
        if (closed_ || frames_.empty())
            return std::nullopt;
        auto frame = std::move(frames_.front());
        frames_.pop_front();
        bytes_ -= frame.bytes;
        auto& state = streams_[frame.stream];
        const bool expired = now - frame.queued > kMaxAge;
        // A queued old IDR cannot acknowledge a loss that occurred after it was encoded.
        if (!expired && frame.idr && frame.generation == state.generation)
            state.needs_idr = false;
        const bool discard = expired || state.needs_idr;
        if (discard) {
            state.needs_idr = true;
            ++dropped_;
        }
        return Delivery{std::move(frame), discard};
    }
    void Close() {
        std::lock_guard lock(mutex_);
        closed_ = true;
        frames_.clear();
        streams_.clear();
        bytes_ = 0;
    }
    [[nodiscard]] std::size_t Bytes() const {
        std::lock_guard lock(mutex_);
        return bytes_;
    }
    [[nodiscard]] std::uint64_t Dropped() const {
        std::lock_guard lock(mutex_);
        return dropped_;
    }

  private:
    struct Stream final {
        std::uint64_t generation{};
        bool needs_idr{};
    };
    mutable std::mutex mutex_{};
    std::deque<Frame> frames_{};
    std::map<std::string, Stream> streams_{};
    std::size_t bytes_{};
    std::uint64_t dropped_{};
    bool closed_{};
};

} // namespace px::media
