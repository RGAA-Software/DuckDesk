#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>

#include "audio_capture.h"

namespace px {

class PxPluginBaseEvent;
class PxPluginContext;

// Owns every asynchronous resource used by the WAS audio plug-in. The
// loader-owned plug-in ABI object only forwards synchronous lifecycle calls.
class WasAudioCaptureRuntime final
    : public std::enable_shared_from_this<WasAudioCaptureRuntime> {
private:
    struct ConstructionToken final {};

public:
    using CaptureFactory = std::function<AudioCapturePtr(uint32_t)>;
    using ProcessAlivePredicate = std::function<bool(uint32_t)>;
    using EventCallback = std::function<void(const std::shared_ptr<PxPluginBaseEvent>&)>;

    static std::shared_ptr<WasAudioCaptureRuntime> Make(
        CaptureFactory capture_factory = {},
        ProcessAlivePredicate process_alive = {},
        std::chrono::milliseconds restart_delay = std::chrono::seconds(2));

    WasAudioCaptureRuntime(
        ConstructionToken,
        CaptureFactory capture_factory,
        ProcessAlivePredicate process_alive,
        std::chrono::milliseconds restart_delay);

    ~WasAudioCaptureRuntime();

    WasAudioCaptureRuntime(const WasAudioCaptureRuntime&) = delete;
    WasAudioCaptureRuntime& operator=(const WasAudioCaptureRuntime&) = delete;

    void ConfigureDelivery(
        std::weak_ptr<PxPluginContext> context,
        EventCallback callback,
        bool audio_enabled);
    void SetAudioEnabled(bool enabled);
    void SetLoopbackProcessId(uint32_t pid);
    [[nodiscard]] uint32_t GetLoopbackProcessId() const;
    [[nodiscard]] bool IsProviding() const;
    [[nodiscard]] int GetLastStartError() const;

    void StartProviding();
    void StopProviding();
    void Shutdown();

private:
    struct RestartState final {
        std::mutex mutex;
        std::condition_variable_any condition;
        bool pending = false;
        int fail_count = 0;
        uint64_t generation = 0;
    };

    struct EventChannel final
        : public std::enable_shared_from_this<EventChannel> {
        void Configure(
            std::weak_ptr<PxPluginContext> context,
            EventCallback callback,
            bool audio_enabled);
        void SetAudioEnabled(bool enabled);
        void Disable();
        void Publish(const std::shared_ptr<PxPluginBaseEvent>& event);
        void Deliver(const std::shared_ptr<PxPluginBaseEvent>& event);

        std::mutex mutex;
        std::weak_ptr<PxPluginContext> context;
        EventCallback callback;
        std::atomic<bool> enabled = false;
        std::atomic<bool> accepting = true;
    };

    void StartWorker();
    int StartCapture(bool external_start, uint64_t expected_restart_generation = 0);
    void HandleCaptureStop(
        const std::weak_ptr<IAudioCapture>& capture,
        uint32_t capture_pid,
        uint64_t capture_generation);
    void ScheduleRestart(uint32_t capture_pid, uint64_t capture_generation);
    uint64_t ResetRestartState();
    [[nodiscard]] bool IsRestartGenerationCurrent(uint64_t generation) const;
    static void RestartWorkerMain(
        const std::shared_ptr<RestartState>& state,
        const std::weak_ptr<WasAudioCaptureRuntime>& runtime,
        std::stop_token stop_token);
    static CaptureFactory DefaultCaptureFactory();
    static ProcessAlivePredicate DefaultProcessAlivePredicate();

    CaptureFactory capture_factory_;
    ProcessAlivePredicate process_alive_;
    const std::chrono::milliseconds restart_delay_;
    std::shared_ptr<EventChannel> event_channel_ = std::make_shared<EventChannel>();
    std::shared_ptr<RestartState> restart_state_ = std::make_shared<RestartState>();

    mutable std::mutex operation_mutex_;
    AudioCapturePtr audio_capture_;
    std::atomic<uint32_t> loopback_process_id_ = 0;
    std::atomic<int> last_start_error_ = 0;
    std::atomic<int> samples_ = 0;
    std::atomic<int> channels_ = 0;
    std::atomic<int> bits_ = 0;
    std::atomic<bool> desired_running_ = false;
    std::atomic<bool> shutting_down_ = false;
    std::atomic<uint64_t> capture_generation_ = 0;
    std::jthread restart_thread_;
};

}  // namespace px
