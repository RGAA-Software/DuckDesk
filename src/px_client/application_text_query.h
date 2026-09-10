#pragma once

#include <chrono>
#include <cstdint>

namespace px {

// Capability discovery is read-only: retry a lost admission-time query, never a text submission or barrier.
class ApplicationTextQuery final {
  public:
    using Clock = std::chrono::steady_clock;
    void Reset() {
        *this = ApplicationTextQuery{};
    }
    void Replied(bool supported) {
        state_ = supported ? State::Supported : State::Unavailable;
        pending_ = false;
    }
    bool Due(Clock::time_point now) const {
        return state_ != State::Unavailable && (state_ == State::Supported || attempts_ < kMaxAttempts) && now >= next_query_ &&
               (!pending_ || now >= deadline_);
    }
    void Sent(bool queued, Clock::time_point now) {
        pending_ = queued;
        if (queued && state_ == State::Probing)
            ++attempts_;
        deadline_ = now + std::chrono::seconds(3);
        next_query_ = now + std::chrono::seconds(1);
    }

  private:
    enum class State { Probing, Supported, Unavailable };
    static constexpr std::uint8_t kMaxAttempts{3};
    State state_{State::Probing};
    std::uint8_t attempts_{};
    bool pending_{};
    Clock::time_point deadline_{};
    Clock::time_point next_query_{};
};

} // namespace px
