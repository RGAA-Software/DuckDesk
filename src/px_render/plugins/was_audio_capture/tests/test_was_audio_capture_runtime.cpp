#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "audio_capture.h"
#include "was_audio_capture_runtime.h"
#include "px_common_new/data.h"

namespace {
using namespace std::chrono_literals;

class FakeAudioCapture final : public px::IAudioCapture {
public:
    explicit FakeAudioCapture(int start_result = 0)
        : start_result_(start_result) {}

    int Start() override {
        running_ = start_result_ == 0;
        const auto callback = SnapshotCallbacks().format;
        if (running_ && callback) {
            callback(48000, 2, 16);
        }
        return start_result_;
    }

    int Pause() override {
        running_ = false;
        return 0;
    }

    int Stop() override {
        running_ = false;
        NotifyStopOnce();
        return 0;
    }

    [[nodiscard]] bool IsFatalStop() const override {
        return fatal_stop_.load();
    }

    void EmitData() {
        const auto callback = SnapshotCallbacks().data;
        if (callback) {
            callback(px::Data::From("runtime-audio-frame"));
        }
    }

    void EmitFatalStop() {
        fatal_stop_ = true;
        running_ = false;
        NotifyStopOnce();
    }

private:
    void NotifyStopOnce() {
        const auto callback = SnapshotCallbacks().stop;
        if (!stop_notified_.exchange(true) && callback) {
            callback();
        }
    }

    const int start_result_;
    std::atomic<bool> running_ = false;
    std::atomic<bool> fatal_stop_ = false;
    std::atomic<bool> stop_notified_ = false;
};

struct FactoryState final {
    px::AudioCapturePtr Create(uint32_t) {
        auto capture = std::make_shared<FakeAudioCapture>();
        std::lock_guard lock(mutex);
        captures.push_back(capture);
        return capture;
    }

    [[nodiscard]] size_t Count() const {
        std::lock_guard lock(mutex);
        return captures.size();
    }

    [[nodiscard]] std::shared_ptr<FakeAudioCapture> Latest() const {
        std::lock_guard lock(mutex);
        return captures.empty() ? std::shared_ptr<FakeAudioCapture>{} : captures.back();
    }

    mutable std::mutex mutex;
    std::vector<std::shared_ptr<FakeAudioCapture>> captures;
};

bool WaitUntil(const std::function<bool()>& condition, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (condition()) {
            return true;
        }
        std::this_thread::sleep_for(2ms);
    }
    return condition();
}

std::shared_ptr<px::WasAudioCaptureRuntime> MakeRuntime(
    const std::shared_ptr<FactoryState>& factory,
    bool process_alive = true) {
    return px::WasAudioCaptureRuntime::Make(
        [factory](uint32_t pid) { return factory->Create(pid); },
        [process_alive](uint32_t) { return process_alive; },
        30ms);
}

bool TestFatalRestartTenRounds() {
    for (int round = 1; round <= 10; ++round) {
        const auto factory = std::make_shared<FactoryState>();
        const auto runtime = MakeRuntime(factory);
        runtime->SetLoopbackProcessId(9000 + round);
        runtime->StartProviding();
        const auto first = factory->Latest();
        if (!first || !runtime->IsProviding()) {
            std::printf("FAIL fatal-restart round %d: initial start\n", round);
            return false;
        }
        first->EmitFatalStop();
        if (!WaitUntil([factory]() { return factory->Count() == 2; }, 500ms) ||
            !runtime->IsProviding()) {
            std::printf("FAIL fatal-restart round %d: retry did not start\n", round);
            return false;
        }
        runtime->Shutdown();
    }
    return true;
}

bool TestPendingRestartCancellationTenRounds() {
    for (int round = 1; round <= 10; ++round) {
        const auto factory = std::make_shared<FactoryState>();
        const auto runtime = MakeRuntime(factory);
        runtime->SetLoopbackProcessId(9100 + round);
        runtime->StartProviding();
        const auto capture = factory->Latest();
        capture->EmitFatalStop();
        runtime->StopProviding();
        std::this_thread::sleep_for(80ms);
        if (factory->Count() != 1 || runtime->IsProviding()) {
            std::printf("FAIL pending-cancel round %d: capture restarted\n", round);
            return false;
        }
        runtime->Shutdown();
    }
    return true;
}

bool TestDeadProcessDoesNotRestart() {
    const auto factory = std::make_shared<FactoryState>();
    const auto runtime = MakeRuntime(factory, false);
    runtime->SetLoopbackProcessId(9200);
    runtime->StartProviding();
    factory->Latest()->EmitFatalStop();
    std::this_thread::sleep_for(80ms);
    const bool passed = factory->Count() == 1;
    runtime->Shutdown();
    return passed;
}

bool TestCallbackStopAndPostShutdownInvalidationTenRounds() {
    for (int round = 1; round <= 10; ++round) {
        const auto factory = std::make_shared<FactoryState>();
        auto runtime = MakeRuntime(factory);
        const auto deliveries = std::make_shared<std::atomic<int>>(0);
        const std::weak_ptr<px::WasAudioCaptureRuntime> weak_runtime = runtime;
        runtime->ConfigureDelivery(
            [weak_runtime, deliveries](const px::CaptureAudioFrame& frame) {
                if (!frame.full_data_ || frame.samples_ != 48000 ||
                    frame.channels_ != 2 || frame.bits_ != 16) {
                    return;
                }
                ++(*deliveries);
                const auto active_runtime = weak_runtime.lock();
                if (active_runtime) {
                    active_runtime->StopProviding();
                }
            },
            true);
        runtime->StartProviding();
        const auto capture = factory->Latest();
        capture->EmitData();
        if (!WaitUntil([deliveries]() { return deliveries->load() == 1; }, 500ms) ||
            runtime->IsProviding()) {
            std::printf("FAIL callback-stop round %d\n", round);
            return false;
        }

        runtime->Shutdown();
        runtime.reset();
        capture->EmitData();
        capture->EmitFatalStop();
        std::this_thread::sleep_for(20ms);
        if (deliveries->load() != 1) {
            std::printf("FAIL post-shutdown callback round %d\n", round);
            return false;
        }
    }
    return true;
}

bool TestRepeatedStartStopTenRounds() {
    const auto factory = std::make_shared<FactoryState>();
    const auto runtime = MakeRuntime(factory);
    for (int round = 1; round <= 10; ++round) {
        runtime->StartProviding();
        if (!runtime->IsProviding()) {
            std::printf("FAIL repeated-start-stop round %d: not providing\n", round);
            return false;
        }
        runtime->StopProviding();
        if (runtime->IsProviding()) {
            std::printf("FAIL repeated-start-stop round %d: still providing\n", round);
            return false;
        }
    }
    runtime->Shutdown();
    return factory->Count() == 10;
}

bool TestCallbackReplacementDuringDispatchTenRounds() {
    for (int round = 1; round <= 10; ++round) {
        const auto capture = std::make_shared<FakeAudioCapture>();
        const auto entered = std::make_shared<std::atomic_bool>(false);
        const auto release = std::make_shared<std::atomic_bool>(false);
        const auto first_deliveries = std::make_shared<std::atomic_int>(0);
        const auto replacement_deliveries = std::make_shared<std::atomic_int>(0);
        capture->RegisterDataCallback(
            [entered, release, first_deliveries](const px::DataPtr&) {
                ++(*first_deliveries);
                *entered = true;
                while (!release->load()) {
                    std::this_thread::yield();
                }
            });

        std::jthread dispatch([capture]() { capture->EmitData(); });
        if (!WaitUntil([entered]() { return entered->load(); }, 500ms)) {
            std::printf("FAIL callback replacement round %d: no entry\n", round);
            return false;
        }
        capture->RegisterDataCallback(
            [replacement_deliveries](const px::DataPtr&) {
                ++(*replacement_deliveries);
            });
        *release = true;
        dispatch.join();

        capture->EmitData();
        if (first_deliveries->load() != 1 ||
            replacement_deliveries->load() != 1) {
            std::printf(
                "FAIL callback replacement round %d: first=%d replacement=%d\n",
                round, first_deliveries->load(),
                replacement_deliveries->load());
            return false;
        }
    }
    return true;
}

}  // namespace

int main() {
    if (!TestFatalRestartTenRounds()) {
        return 1;
    }
    if (!TestPendingRestartCancellationTenRounds()) {
        return 2;
    }
    if (!TestDeadProcessDoesNotRestart()) {
        std::printf("FAIL dead-process restart gate\n");
        return 3;
    }
    if (!TestCallbackStopAndPostShutdownInvalidationTenRounds()) {
        return 4;
    }
    if (!TestRepeatedStartStopTenRounds()) {
        return 5;
    }
    if (!TestCallbackReplacementDuringDispatchTenRounds()) {
        return 6;
    }
    std::printf(
        "PASS: audio runtime fatal restart, cancellation, callback shutdown, "
        "post-destroy invalidation, callback replacement during dispatch and "
        "repeated start/stop\n");
    return 0;
}
