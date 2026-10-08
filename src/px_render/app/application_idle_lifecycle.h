#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_set>

namespace px {

// One policy for application runtimes. It never owns Windows sessions or applications.
class ApplicationIdleLifecycle final {
public:
    using Clock = std::chrono::steady_clock;
    struct Deadline final {
        std::uint64_t generation{};
        Clock::time_point expires_at{};
        bool startup{};
    };

    explicit ApplicationIdleLifecycle(const std::chrono::seconds disconnect_grace = std::chrono::seconds{10})
        : disconnect_grace_(std::clamp(disconnect_grace, std::chrono::seconds{1}, std::chrono::seconds{3600})) {}

    [[nodiscard]] std::optional<Deadline> ArmStartup(const Clock::time_point now) {
        const std::scoped_lock lock{mutex_};
        if (armed_ || stopped_) return std::nullopt;
        armed_ = true;
        if (!clients_.empty()) return std::nullopt;
        return MakeDeadline(now, !has_seen_client_);
    }

    void Connected(const std::string& connection_id) {
        if (connection_id.empty()) return;
        const std::scoped_lock lock{mutex_};
        if (stopped_ || !clients_.insert(connection_id).second) return;
        has_seen_client_ = true;
        ++generation_;
        deadline_.reset();
    }

    [[nodiscard]] std::optional<Deadline> Disconnected(const std::string& connection_id, const Clock::time_point now) {
        const std::scoped_lock lock{mutex_};
        if (stopped_ || clients_.erase(connection_id) == 0 || !armed_ || !clients_.empty()) return std::nullopt;
        return MakeDeadline(now, false);
    }

    [[nodiscard]] bool ClaimExpiry(const Deadline& deadline, const Clock::time_point now) {
        const std::scoped_lock lock{mutex_};
        if (stopped_ || !deadline_ || deadline.generation != deadline_->generation || now < deadline_->expires_at || !clients_.empty()) return false;
        stopped_ = true;
        deadline_.reset();
        return true;
    }

    [[nodiscard]] bool HasClients() const {
        const std::scoped_lock lock{mutex_};
        return !clients_.empty();
    }

    void Stop() {
        const std::scoped_lock lock{mutex_};
        stopped_ = true;
        ++generation_;
        deadline_.reset();
    }

private:
    Deadline MakeDeadline(const Clock::time_point now, const bool startup) {
        deadline_ = Deadline{++generation_, now + (startup ? std::chrono::seconds{45} : disconnect_grace_), startup};
        return *deadline_;
    }

    const std::chrono::seconds disconnect_grace_;
    mutable std::mutex mutex_{};
    std::unordered_set<std::string> clients_{};
    std::optional<Deadline> deadline_{};
    std::uint64_t generation_{};
    bool armed_{};
    bool stopped_{};
    bool has_seen_client_{};
};

}  // namespace px
