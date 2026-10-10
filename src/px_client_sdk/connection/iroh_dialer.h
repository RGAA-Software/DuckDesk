#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>

#include "px_transport/session_open.h"
#include "sdk_connection_params.h"

namespace px {
enum class IrohDialStage { kStarting, kConnected, kFailed };
struct IrohDialResult final {
    IrohDialStage stage{IrohDialStage::kFailed};
    std::shared_ptr<transport::Connection> connection{};
    transport::SessionChannels channels{};
    std::string error_code{};
};

// Owns the endpoint for the full session and cancels its pending dial/admission on Stop.
class IrohDialer final : public std::enable_shared_from_this<IrohDialer> {
public:
    using Completion = std::function<void(IrohDialResult)>;
    IrohDialer(IrohDialParameters parameters, Completion completion);
    ~IrohDialer();
    void Start();
    void Stop();

private:
    [[nodiscard]] static bool IsStopped(const std::weak_ptr<IrohDialer>& owner);
    [[nodiscard]] static bool RetryDelay(const std::weak_ptr<IrohDialer>& owner);
    [[nodiscard]] static bool RefreshEndpoint(const std::shared_ptr<transport::Endpoint>& endpoint, IrohDialParameters& parameters);
    static void Run(std::weak_ptr<IrohDialer> owner, IrohDialParameters parameters);
    void Complete(IrohDialResult result);
    IrohDialParameters parameters_{};
    Completion completion_{};
    std::mutex mutex_{};
    std::shared_ptr<transport::Endpoint> endpoint_{};
    std::optional<std::jthread> worker_{};
    std::atomic_bool stopped_{};
    bool started_once_{};
};
}  // namespace px
