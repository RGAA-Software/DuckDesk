#pragma once

#include <atomic>
#include <memory>

namespace px {

// Bound queued datagrams independently of reliable file/control traffic. A reservation is
// released even when an I/O callback is cancelled and destroyed without being invoked.
class UdpVoiceSendBudget final {
  public:
    class Reservation final {
      public:
        explicit Reservation(std::shared_ptr<std::atomic_uint> pending) : pending_(std::move(pending)) {}
        ~Reservation() {
            --*pending_;
        }
        Reservation(const Reservation&) = delete;
        Reservation& operator=(const Reservation&) = delete;

      private:
        const std::shared_ptr<std::atomic_uint> pending_{};
    };

    static constexpr unsigned kMaximumPending = 8;

    [[nodiscard]] std::shared_ptr<const Reservation> TryAcquire() const {
        if (pending_->fetch_add(1) >= kMaximumPending) {
            --*pending_;
            return {};
        }
        try {
            return std::make_shared<const Reservation>(pending_);
        } catch (...) {
            --*pending_;
            throw;
        }
    }

    [[nodiscard]] unsigned Pending() const {
        return pending_->load();
    }

  private:
    const std::shared_ptr<std::atomic_uint> pending_{std::make_shared<std::atomic_uint>()};
};

} // namespace px
