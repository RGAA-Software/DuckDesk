#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>

#include "modules/builtin_module_catalog.h"
#include "px_capture/capture_message.h"

namespace px {
class WasAudioCaptureRuntime;
}

namespace px::render {

inline constexpr std::string_view kWasAudioCaptureModuleId =
    "75f2c0ba-d76b-4955-944c-220838e7b0fe";

struct WasAudioCaptureSnapshot final {
    bool running{false};
    bool enabled{true};
    bool providing{false};
    std::uint32_t loopback_process_id{0};
    int last_start_error{0};
    std::uint64_t delivered_frames{0};
};

class WasAudioCaptureSource final
    : public std::enable_shared_from_this<WasAudioCaptureSource> {
public:
    using FrameCallback = std::function<void(const CaptureAudioFrame&)>;
    using RuntimeFactory =
        std::function<std::shared_ptr<WasAudioCaptureRuntime>()>;

    [[nodiscard]] static std::shared_ptr<WasAudioCaptureSource> Create(
        FrameCallback callback,
        RuntimeFactory runtime_factory = {});

    WasAudioCaptureSource(FrameCallback callback, RuntimeFactory runtime_factory);
    ~WasAudioCaptureSource();

    WasAudioCaptureSource(const WasAudioCaptureSource&) = delete;
    WasAudioCaptureSource& operator=(const WasAudioCaptureSource&) = delete;

    [[nodiscard]] BuiltinModuleRegistration MakeRegistration();
    [[nodiscard]] ModuleLifecycleResult Start();
    [[nodiscard]] ModuleLifecycleResult Stop();
    [[nodiscard]] ModuleLifecycleResult SetEnabled(bool enabled);

    void SetLoopbackProcessId(std::uint32_t pid);
    [[nodiscard]] std::uint32_t GetLoopbackProcessId() const;
    void StartProviding();
    void StopProviding();
    [[nodiscard]] bool IsProviding() const;
    [[nodiscard]] int GetLastStartError() const;
    [[nodiscard]] WasAudioCaptureSnapshot Snapshot() const;

private:
    void Publish(const CaptureAudioFrame& frame);
    [[nodiscard]] std::shared_ptr<WasAudioCaptureRuntime> GetRuntime() const;

    mutable std::mutex mutex_;
    FrameCallback callback_;
    RuntimeFactory runtime_factory_;
    std::shared_ptr<WasAudioCaptureRuntime> runtime_;
    bool running_{false};
    bool enabled_{true};
    std::uint64_t delivered_frames_{0};
};

}  // namespace px::render
