#pragma once

#include <memory>
#include <mutex>

#include "modules/builtin_module_catalog.h"

namespace px {
class CaptureMonitorInfoMessage;
class Message;
class WinEventReplayer;
}

namespace px::render {

inline constexpr std::string_view kInputReplayModuleId =
    "b6cb3d88-f397-4182-863c-2aaed752d1a9";

struct InputReplaySnapshot final {
    bool running{false};
    bool enabled{true};
    std::uint64_t accepted_messages{0};
};

class InputReplayService final
    : public std::enable_shared_from_this<InputReplayService> {
public:
    [[nodiscard]] static std::shared_ptr<InputReplayService> Create();
    InputReplayService() = default;
    ~InputReplayService();

    InputReplayService(const InputReplayService&) = delete;
    InputReplayService& operator=(const InputReplayService&) = delete;

    [[nodiscard]] BuiltinModuleRegistration MakeRegistration();
    [[nodiscard]] ModuleLifecycleResult Start();
    [[nodiscard]] ModuleLifecycleResult Stop();
    [[nodiscard]] ModuleLifecycleResult SetEnabled(bool enabled);

    void HandleMessage(const std::shared_ptr<Message>& message);
    void ReleaseInputState();
    void UpdateCaptureMonitorInfo(const CaptureMonitorInfoMessage& message);
    [[nodiscard]] InputReplaySnapshot Snapshot() const;

private:
    [[nodiscard]] std::shared_ptr<WinEventReplayer> GetReplayer() const;

    mutable std::mutex mutex_;
    std::shared_ptr<WinEventReplayer> replayer_;
    bool running_{false};
    bool enabled_{true};
    std::uint64_t accepted_messages_{0};
};

}  // namespace px::render
