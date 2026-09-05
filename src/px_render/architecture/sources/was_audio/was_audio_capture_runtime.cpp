#include "was_audio_capture_runtime.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <utility>

#include "miniaudio_audio_capture.h"
#include "process_loopback_audio_capture.h"
#include "px_common/log.h"

namespace px {
namespace {

struct ProcessHandleCloser final {
    void operator()(void* handle) const noexcept {
        if (handle != nullptr) {
            CloseHandle(handle);
        }
    }
};

using ProcessHandle = std::unique_ptr<void, ProcessHandleCloser>;

}  // namespace

std::shared_ptr<WasAudioCaptureRuntime> WasAudioCaptureRuntime::Make(
    CaptureFactory capture_factory,
    ProcessAlivePredicate process_alive,
    std::chrono::milliseconds restart_delay) {
    if (!capture_factory) {
        capture_factory = DefaultCaptureFactory();
    }
    if (!process_alive) {
        process_alive = DefaultProcessAlivePredicate();
    }
    auto runtime = std::make_shared<WasAudioCaptureRuntime>(
        ConstructionToken{}, std::move(capture_factory),
        std::move(process_alive), restart_delay);
    runtime->StartWorker();
    return runtime;
}

WasAudioCaptureRuntime::WasAudioCaptureRuntime(
    ConstructionToken,
    CaptureFactory capture_factory,
    ProcessAlivePredicate process_alive,
    std::chrono::milliseconds restart_delay)
    : capture_factory_(std::move(capture_factory)),
      process_alive_(std::move(process_alive)),
      restart_delay_(restart_delay) {}

WasAudioCaptureRuntime::~WasAudioCaptureRuntime() {
    Shutdown();
}

void WasAudioCaptureRuntime::StartWorker() {
    const auto state = restart_state_;
    const auto weak_runtime = weak_from_this();
    restart_thread_ = std::jthread(
        [state, weak_runtime](std::stop_token stop_token) {
            RestartWorkerMain(state, weak_runtime, stop_token);
        });
}

void WasAudioCaptureRuntime::ConfigureDelivery(
    FrameCallback callback,
    bool audio_enabled) {
    event_channel_->Configure(std::move(callback), audio_enabled);
}

void WasAudioCaptureRuntime::SetAudioEnabled(bool enabled) {
    event_channel_->SetAudioEnabled(enabled);
}

void WasAudioCaptureRuntime::SetLoopbackProcessId(uint32_t pid) {
    std::lock_guard lock(operation_mutex_);
    loopback_process_id_ = pid;
    ResetRestartState();
    LOGI("[WasAudioCaptureRuntime] loopback pid={}", pid);
}

uint32_t WasAudioCaptureRuntime::GetLoopbackProcessId() const {
    return loopback_process_id_.load();
}

bool WasAudioCaptureRuntime::IsProviding() const {
    std::lock_guard lock(operation_mutex_);
    return audio_capture_ != nullptr;
}

int WasAudioCaptureRuntime::GetLastStartError() const {
    return last_start_error_.load();
}

void WasAudioCaptureRuntime::StartProviding() {
    static_cast<void>(StartCapture(true));
}

int WasAudioCaptureRuntime::StartCapture(
    bool external_start,
    uint64_t expected_restart_generation) {
    std::lock_guard lock(operation_mutex_);
    if (shutting_down_) {
        last_start_error_ = -1;
        return last_start_error_;
    }

    if (external_start) {
        desired_running_ = true;
        ResetRestartState();
    } else if (!desired_running_ ||
               !IsRestartGenerationCurrent(expected_restart_generation)) {
        return 0;
    }

    last_start_error_ = 0;
    const uint32_t capture_pid = loopback_process_id_.load();
    LOGI("[WasAudioCaptureRuntime] start, loopback_pid={}", capture_pid);

    // Invalidate callbacks from the previous capture before Stop(), whose stop
    // callback may run synchronously on this thread.
    const uint64_t capture_generation = ++capture_generation_;
    auto previous_capture = std::move(audio_capture_);
    if (previous_capture) {
        LOGW("[WasAudioCaptureRuntime] stopping previous capture before restart");
        previous_capture->Stop();
    }

    auto capture = capture_factory_(capture_pid);
    if (!capture) {
        last_start_error_ = -1;
        LOGE("[WasAudioCaptureRuntime] capture factory failed, pid={}", capture_pid);
        return last_start_error_;
    }

    const auto weak_runtime = weak_from_this();
    const std::weak_ptr<IAudioCapture> weak_capture = capture;
    capture->RegisterFormatCallback(
        [weak_runtime](int samples, int channels, int bits) {
            const auto runtime = weak_runtime.lock();
            if (!runtime || runtime->shutting_down_) {
                return;
            }
            runtime->samples_ = samples;
            runtime->channels_ = channels;
            runtime->bits_ = bits;
            LOGI("[WasAudioCaptureRuntime] format ready: {}Hz {}ch {}bit",
                 samples, channels, bits);
        });
    capture->RegisterDataCallback(
        [weak_runtime](const std::shared_ptr<Data>& data) {
            const auto runtime = weak_runtime.lock();
            if (!runtime || runtime->shutting_down_ || !data || data->Size() <= 0) {
                return;
            }
            CaptureAudioFrame frame;
            frame.frame_index_ = ++runtime->frame_index_;
            frame.full_data_ = data;
            frame.samples_ = static_cast<uint32_t>(runtime->samples_.load());
            frame.channels_ = static_cast<uint32_t>(runtime->channels_.load());
            frame.bits_ = static_cast<uint32_t>(runtime->bits_.load());
            runtime->event_channel_->Publish(frame);
        });
    capture->RegisterSplitDataCallback(
        [weak_runtime](const auto& left, const auto& right) {
            const auto runtime = weak_runtime.lock();
            if (!runtime || runtime->shutting_down_) {
                return;
            }
            CaptureAudioFrame frame;
            frame.frame_index_ = ++runtime->frame_index_;
            frame.left_ch_data_ = left;
            frame.right_ch_data_ = right;
            frame.samples_ = static_cast<uint32_t>(runtime->samples_.load());
            frame.channels_ = static_cast<uint32_t>(runtime->channels_.load());
            frame.bits_ = static_cast<uint32_t>(runtime->bits_.load());
            runtime->event_channel_->Publish(frame);
        });
    capture->RegisterStopCallback(
        [weak_runtime, weak_capture, capture_pid, capture_generation]() {
            const auto runtime = weak_runtime.lock();
            if (runtime) {
                runtime->HandleCaptureStop(
                    weak_capture, capture_pid, capture_generation);
            }
        });

    audio_capture_ = capture;
    const int start_result = capture->Start();
    last_start_error_ = start_result;
    if (start_result != 0) {
        LOGE("[WasAudioCaptureRuntime] start failed, ret={}, pid={}",
             start_result, capture_pid);
        audio_capture_.reset();
        return start_result;
    }
    LOGI("[WasAudioCaptureRuntime] start OK ({})",
         capture_pid == 0 ? "default device loopback" : "PID process-loopback");
    return 0;
}

void WasAudioCaptureRuntime::StopProviding() {
    std::lock_guard lock(operation_mutex_);
    desired_running_ = false;
    ++capture_generation_;
    ResetRestartState();
    auto capture = std::move(audio_capture_);
    if (!capture) {
        LOGW("[WasAudioCaptureRuntime] stop: capture already null");
        return;
    }
    LOGI("[WasAudioCaptureRuntime] stop");
    capture->Stop();
}

void WasAudioCaptureRuntime::Shutdown() {
    if (shutting_down_.exchange(true)) {
        return;
    }
    event_channel_->Disable();
    desired_running_ = false;
    ++capture_generation_;
    ResetRestartState();
    restart_thread_.request_stop();
    restart_state_->condition.notify_all();

    {
        std::lock_guard lock(operation_mutex_);
        auto capture = std::move(audio_capture_);
        if (capture) {
            capture->Stop();
        }
    }
    if (restart_thread_.joinable()) {
        restart_thread_.join();
    }
    LOGI("[WasAudioCaptureRuntime] shutdown complete");
}

void WasAudioCaptureRuntime::HandleCaptureStop(
    const std::weak_ptr<IAudioCapture>& capture,
    uint32_t capture_pid,
    uint64_t capture_generation) {
    const auto active_capture = capture.lock();
    if (!active_capture || shutting_down_ || !desired_running_ ||
        capture_generation != capture_generation_.load()) {
        return;
    }
    if (!active_capture->IsFatalStop()) {
        LOGW("[WasAudioCaptureRuntime] capture stopped normally, pid={}", capture_pid);
        return;
    }
    ScheduleRestart(capture_pid, capture_generation);
}

void WasAudioCaptureRuntime::ScheduleRestart(
    uint32_t capture_pid,
    uint64_t capture_generation) {
    if (capture_pid == 0 || shutting_down_ || !desired_running_ ||
        capture_generation != capture_generation_.load() ||
        capture_pid != loopback_process_id_.load()) {
        return;
    }
    std::lock_guard lock(restart_state_->mutex);
    if (shutting_down_ || !desired_running_) {
        return;
    }
    ++restart_state_->fail_count;
    restart_state_->pending = true;
    LOGW("[WasAudioCaptureRuntime] fatal stop, pid={}, retry #{} in {}ms",
         capture_pid, restart_state_->fail_count, restart_delay_.count());
    restart_state_->condition.notify_all();
}

uint64_t WasAudioCaptureRuntime::ResetRestartState() {
    std::lock_guard lock(restart_state_->mutex);
    restart_state_->pending = false;
    restart_state_->fail_count = 0;
    const uint64_t generation = ++restart_state_->generation;
    restart_state_->condition.notify_all();
    return generation;
}

bool WasAudioCaptureRuntime::IsRestartGenerationCurrent(uint64_t generation) const {
    std::lock_guard lock(restart_state_->mutex);
    return generation == restart_state_->generation;
}

void WasAudioCaptureRuntime::RestartWorkerMain(
    const std::shared_ptr<RestartState>& state,
    const std::weak_ptr<WasAudioCaptureRuntime>& weak_runtime,
    std::stop_token stop_token) {
    std::unique_lock lock(state->mutex);
    while (!stop_token.stop_requested()) {
        if (!state->condition.wait(lock, stop_token, [state]() {
                return state->pending;
            })) {
            break;
        }
        const uint64_t generation = state->generation;
        auto runtime_for_delay = weak_runtime.lock();
        if (!runtime_for_delay) {
            break;
        }
        const auto delay = runtime_for_delay->restart_delay_;
        runtime_for_delay.reset();
        const bool cancelled = state->condition.wait_for(
            lock, stop_token, delay, [state, generation]() {
                return !state->pending || state->generation != generation;
            });
        if (stop_token.stop_requested()) {
            break;
        }
        if (cancelled || state->generation != generation) {
            continue;
        }
        state->pending = false;
        lock.unlock();

        const auto runtime = weak_runtime.lock();
        if (!runtime || runtime->shutting_down_ || !runtime->desired_running_) {
            lock.lock();
            continue;
        }
        const uint32_t pid = runtime->loopback_process_id_.load();
        if (pid == 0 || !runtime->process_alive_(pid)) {
            LOGE("[WasAudioCaptureRuntime] restart cancelled: target pid={} is gone", pid);
            runtime->ResetRestartState();
            lock.lock();
            continue;
        }

        LOGI("[WasAudioCaptureRuntime] restarting capture, pid={}", pid);
        const int start_result = runtime->StartCapture(false, generation);
        if (start_result != 0 && !runtime->shutting_down_ && runtime->desired_running_) {
            runtime->ScheduleRestart(pid, runtime->capture_generation_.load());
        }
        lock.lock();
    }
    LOGI("[WasAudioCaptureRuntime] restart worker exit");
}

WasAudioCaptureRuntime::CaptureFactory
WasAudioCaptureRuntime::DefaultCaptureFactory() {
    return [](uint32_t pid) -> AudioCapturePtr {
        if (pid != 0) {
            return ProcessLoopbackAudioCapture::Make(pid);
        }
        return MiniAudioCapture::Make();
    };
}

WasAudioCaptureRuntime::ProcessAlivePredicate
WasAudioCaptureRuntime::DefaultProcessAlivePredicate() {
    return [](uint32_t pid) {
        if (pid == 0) {
            return false;
        }
        ProcessHandle process{OpenProcess(
            PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)};
        if (!process) {
            return false;
        }
        DWORD exit_code = 0;
        return GetExitCodeProcess(process.get(), &exit_code) &&
               exit_code == STILL_ACTIVE;
    };
}

void WasAudioCaptureRuntime::EventChannel::Configure(
    FrameCallback new_callback,
    bool audio_enabled) {
    std::lock_guard lock(mutex);
    callback = std::move(new_callback);
    enabled = audio_enabled;
}

void WasAudioCaptureRuntime::EventChannel::SetAudioEnabled(bool value) {
    enabled = value;
}

void WasAudioCaptureRuntime::EventChannel::Disable() {
    accepting = false;
    enabled = false;
    std::lock_guard lock(mutex);
    callback = {};
}

void WasAudioCaptureRuntime::EventChannel::Publish(
    const CaptureAudioFrame& frame) {
    if (!accepting || !enabled) {
        return;
    }
    FrameCallback delivery;
    {
        std::lock_guard lock(mutex);
        delivery = callback;
    }
    if (delivery && accepting && enabled) {
        delivery(frame);
    }
}

}  // namespace px
