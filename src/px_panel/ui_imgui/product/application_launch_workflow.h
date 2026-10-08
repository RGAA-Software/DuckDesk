#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "cloud_application_progress.h"
#include "px_console_client/console_user_app_api.h"

namespace px::panel::product {

struct ApplicationLaunchRequest final {
    std::string applicationId{};
    std::string applicationName{};
    std::string existingInstanceId{};
    std::string requestId{};
    bool viewOnly{};
    bool forceTcp{};
    bool forceRelay{};
};

// Capabilities supplied by the product composition root; tests exercise the same workflow.
struct ApplicationLaunchOperations final {
    using InstanceResult = px::Result<px_console::ConsoleUserAppInstance, px_console::ConsoleApiError>;
    using ConnectionResult = px::Result<px_console::ConsoleNativeApplicationConnection, px_console::ConsoleApiError>;
    std::function<InstanceResult(const std::string&, const std::string&)> start{};
    std::function<InstanceResult(const std::string&)> query{};
    std::function<ConnectionResult(const std::string&, bool, const std::string&)> authorize{};
    std::function<bool()> clientAvailable{};
    std::function<bool(const ApplicationLaunchRequest&, const px_console::ConsoleNativeApplicationConnection&)> launch{};
    std::function<void(const std::string&, std::int64_t)> close{};
    std::function<void()> wait{};
    std::chrono::milliseconds readinessTimeout{45'000};
    std::chrono::milliseconds retirementTimeout{45'000};
};

class ApplicationLaunchWorkflow final {
public:
    explicit ApplicationLaunchWorkflow(ApplicationLaunchOperations operations);
    ~ApplicationLaunchWorkflow();
    [[nodiscard]] std::optional<std::uint64_t> Begin(ApplicationLaunchRequest request);
    void Prepare(std::uint64_t generation);
    void LaunchPrepared(std::uint64_t generation);
    void WorkerUnavailable(std::uint64_t generation);
    [[nodiscard]] std::optional<ui::ApplicationLaunchProgress> Snapshot() const;

private:
    [[nodiscard]] bool SetStage(std::uint64_t generation, ui::ApplicationLaunchStage stage);
    void Fail(std::uint64_t generation, px::ui::TextId error, std::string diagnostic = {});
    void ApiFailure(std::uint64_t generation, px_console::ConsoleApiError error);
    void Close(const px_console::ConsoleNativeApplicationConnection& connection) noexcept;
    bool AwaitReadiness(std::uint64_t generation, ApplicationLaunchOperations::InstanceResult& instance);

    ApplicationLaunchOperations operations_{};
    mutable std::mutex mutex_{};
    std::uint64_t nextGeneration_{1};
    bool preparationStarted_{};
    ApplicationLaunchRequest request_{};
    std::optional<ui::ApplicationLaunchProgress> progress_{};
    std::optional<px_console::ConsoleNativeApplicationConnection> prepared_{};
};

}  // namespace px::panel::product
