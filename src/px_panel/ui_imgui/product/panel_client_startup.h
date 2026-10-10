#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

#include "client_launch_result.h"

namespace px::panel::product {

// One immutable stream/launch identity, registered before its process is created.
class PanelClientStartup final {
public:
    explicit PanelClientStartup(std::string streamId) : streamId_{std::move(streamId)} {}
    [[nodiscard]] const std::string& StreamId() const { return streamId_; }

    bool Resolve(ClientLaunchResult result) {
        {
            const std::scoped_lock lock{mutex_};
            if (result_) return false;
            result_ = result;
        }
        changed_.notify_all();
        return true;
    }

    [[nodiscard]] std::optional<ClientLaunchResult> WaitFor(std::chrono::milliseconds duration) {
        std::unique_lock lock{mutex_};
        changed_.wait_for(lock, duration, [&result = result_] { return result.has_value(); });
        return result_;
    }

private:
    const std::string streamId_{};
    std::mutex mutex_{};
    std::condition_variable changed_{};
    std::optional<ClientLaunchResult> result_{};
};

}  // namespace px::panel::product
