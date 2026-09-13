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
        bool recovery{};
        bool request_idr{};
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
        if (state.needs_idr) {
            if (!frame.idr || state.idr_queued || state.recovering) {
                ++dropped_;
                return true;
            }
            EraseStreamFrames(frame.stream);
        }
        if (frames_.size() >= kMaxFrames || frame.bytes > kMaxBytes - bytes_) {
            if (!state.needs_idr) {
                ++state.generation;
                state.needs_idr = true;
                EraseStreamFrames(frame.stream);
            }
            ++dropped_;
            return false;
        }
        frame.generation = state.generation;
        if (state.needs_idr && frame.idr)
            state.idr_queued = true;
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
        const bool current = frame.generation == state.generation;
        const bool recovery = !expired && current && state.needs_idr && frame.idr;
        if (recovery) {
            state.idr_queued = false;
            state.recovering = true;
        }
        const bool requestIdr = expired && !state.needs_idr;
        const bool discard = expired || (state.needs_idr && !recovery);
        if (discard) {
            if (!state.needs_idr) {
                ++state.generation;
                state.needs_idr = true;
            }
            if (frame.idr)
                state.idr_queued = false;
            ++dropped_;
        }
        return Delivery{std::move(frame), discard, recovery, requestIdr};
    }
    [[nodiscard]] bool Complete(const Delivery& delivery, const bool delivered) {
        std::lock_guard lock(mutex_);
        const auto found = streams_.find(delivery.frame.stream);
        if (closed_ || found == streams_.end())
            return false;
        auto& state = found->second;
        if (!delivered) {
            if (!state.needs_idr) {
                ++state.generation;
                state.needs_idr = true;
            }
            state.idr_queued = false;
            state.recovering = false;
            EraseStreamFrames(delivery.frame.stream);
            return true;
        }
        if (delivery.recovery && delivery.frame.generation == state.generation) {
            state.needs_idr = false;
            state.recovering = false;
        }
        return false;
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
        bool idr_queued{};
        bool recovering{};
    };
    void EraseStreamFrames(const std::string& stream) {
        for (auto frame = frames_.begin(); frame != frames_.end();) {
            if (frame->stream == stream) {
                bytes_ -= frame->bytes;
                frame = frames_.erase(frame);
                ++dropped_;
            } else {
                ++frame;
            }
        }
    }
    mutable std::mutex mutex_{};
    std::deque<Frame> frames_{};
    std::map<std::string, Stream> streams_{};
    std::size_t bytes_{};
    std::uint64_t dropped_{};
    bool closed_{};
};

} // namespace px::media
