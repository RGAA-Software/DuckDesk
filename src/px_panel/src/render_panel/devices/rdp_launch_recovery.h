#pragma once

#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <string>

#include "px_common/secret_buffer.h"

namespace px {

struct RdpLaunchRecovery final {
    std::string app_id{};
    std::string instance_id{};
    std::string nonce{};
    std::string logical_id{};
    std::string stream_id{};
    std::string host{};
    int port{};
    std::string device_id{};
    std::shared_ptr<const SecretBuffer> renewal{};
    std::shared_ptr<const SecretBuffer> configuration{};
};

// Owned by RunningStreamManager and accessed under its mutex. Never serialized.
// Taking a capability consumes it locally, including when a renewal later fails.
class RdpRecoveryStore final {
  public:
    using Clock = std::chrono::steady_clock;

    void Remember(std::shared_ptr<const RdpLaunchRecovery> recovery) {
        Prune(Clock::now());
        if (!recovery || !recovery->renewal || !recovery->configuration || recovery->logical_id.empty()) {
            return;
        }
        if (entries_.size() >= 64 && !entries_.contains(recovery->app_id)) {
            return;
        }
        const auto app_id = recovery->app_id;
        entries_.insert_or_assign(app_id, Entry{std::move(recovery), std::nullopt});
    }

    void Closed(const std::string& app_id, const std::string& stream_id, Clock::time_point now) {
        const auto found = entries_.find(app_id);
        if (found != entries_.end() && found->second.recovery->stream_id == stream_id && !found->second.deadline) {
            found->second.deadline = now + std::chrono::seconds(5);
        }
    }

    std::shared_ptr<const RdpLaunchRecovery> Take(const std::string& app_id, Clock::time_point now) {
        Prune(now);
        const auto found = entries_.find(app_id);
        if (found == entries_.end() || !found->second.deadline) {
            return {};
        }
        auto recovery = std::move(found->second.recovery);
        entries_.erase(found);
        return recovery;
    }

    void Prune(Clock::time_point now) {
        std::erase_if(entries_, [now](const auto& entry) { return entry.second.deadline && now >= *entry.second.deadline; });
    }

    void Clear() {
        entries_.clear();
    }

  private:
    struct Entry final {
        std::shared_ptr<const RdpLaunchRecovery> recovery{};
        std::optional<Clock::time_point> deadline{};
    };
    std::map<std::string, Entry> entries_{};
};

} // namespace px
