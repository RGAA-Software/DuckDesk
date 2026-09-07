#pragma once

#include <atomic>
#include <functional>
#include <memory>

namespace px {

// Testable orchestration for the Panel exit/uninstall sequence. Scheduling is
// supplied by PxContext in production and by a deterministic queue in tests.
class PanelShutdownSequence final
    : public std::enable_shared_from_this<PanelShutdownSequence> {
public:
    using Task = std::function<void()>;
    using Scheduler = std::function<void(Task, int)>;

    struct Hooks final {
        Task prepare;
        std::function<bool()> launch_service_helper;
        Task fallback_cleanup;
    };

    static std::shared_ptr<PanelShutdownSequence> Make(
        Scheduler ui_scheduler,
        Scheduler worker_scheduler,
        Hooks hooks);

    void Start();
    [[nodiscard]] bool IsCompleted() const;

private:
    struct ConstructionToken final {};

public:
    PanelShutdownSequence(
        ConstructionToken,
        Scheduler ui_scheduler,
        Scheduler worker_scheduler,
        Hooks hooks);

private:
    void Prepare();
    void LaunchServiceHelper();
    void RunFallbackCleanup();

    Scheduler ui_scheduler_;
    Scheduler worker_scheduler_;
    Hooks hooks_;
    std::atomic<bool> started_ = false;
    std::atomic<bool> completed_ = false;
};

}  // namespace px
