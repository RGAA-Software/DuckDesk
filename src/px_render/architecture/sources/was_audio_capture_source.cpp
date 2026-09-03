#include "sources/was_audio_capture_source.h"

#include <utility>

#include "px_common_new/log.h"
#include "was_audio_capture_runtime.h"

namespace px::render {
namespace {

RenderError MakeSourceError(std::string operation, std::string reason) {
    return RenderError{
        .code = RenderErrorCode::kModuleLifecycleRejected,
        .component = "was_audio_capture",
        .operation = std::move(operation),
        .stage = "source",
        .reason = std::move(reason),
        .recoverable = true,
    };
}

}  // namespace

std::shared_ptr<WasAudioCaptureSource> WasAudioCaptureSource::Create(
    FrameCallback callback,
    RuntimeFactory runtime_factory) {
    if (!runtime_factory) {
        runtime_factory = [] { return WasAudioCaptureRuntime::Make(); };
    }
    return std::make_shared<WasAudioCaptureSource>(
        std::move(callback), std::move(runtime_factory));
}

WasAudioCaptureSource::WasAudioCaptureSource(
    FrameCallback callback,
    RuntimeFactory runtime_factory)
    : callback_(std::move(callback)),
      runtime_factory_(std::move(runtime_factory)) {}

WasAudioCaptureSource::~WasAudioCaptureSource() {
    static_cast<void>(Stop());
}

BuiltinModuleRegistration WasAudioCaptureSource::MakeRegistration() {
    const std::weak_ptr<WasAudioCaptureSource> weak_owner = weak_from_this();
    return BuiltinModuleRegistration{
        .descriptor = BuiltinModuleDescriptor{
            .id = std::string(kWasAudioCaptureModuleId),
            .name = "WAS Audio Capture",
            .author = "GammaRay",
            .description = "Built-in WASAPI default-device and process-loopback source",
            .version_name = "2.0.0",
            .version_code = 200,
            .capability = BuiltinModuleCapability::kSource,
            .default_enabled = true,
        },
        .start = [weak_owner]() -> PxAwaitable<ModuleLifecycleResult> {
            const auto owner = weak_owner.lock();
            co_return owner
                ? owner->Start()
                : ModuleLifecycleResult(std::unexpected(MakeSourceError(
                      "start", "source owner expired")));
        },
        .stop = [weak_owner]() -> PxAwaitable<ModuleLifecycleResult> {
            const auto owner = weak_owner.lock();
            co_return owner ? owner->Stop() : ModuleLifecycleResult{};
        },
        .set_enabled = [weak_owner](const bool enabled) {
            const auto owner = weak_owner.lock();
            return owner
                ? owner->SetEnabled(enabled)
                : ModuleLifecycleResult(std::unexpected(MakeSourceError(
                      "set_enabled", "source owner expired")));
        },
    };
}

ModuleLifecycleResult WasAudioCaptureSource::Start() {
    RuntimeFactory runtime_factory;
    bool enabled = true;
    {
        std::lock_guard lock(mutex_);
        if (running_) {
            return {};
        }
        runtime_factory = runtime_factory_;
        enabled = enabled_;
    }
    if (!runtime_factory) {
        return std::unexpected(MakeSourceError(
            "start", "audio runtime factory is unavailable"));
    }
    auto runtime = runtime_factory();
    if (!runtime) {
        return std::unexpected(MakeSourceError(
            "start", "audio runtime creation failed"));
    }
    const std::weak_ptr<WasAudioCaptureSource> weak_owner = weak_from_this();
    runtime->ConfigureDelivery(
        [weak_owner](const CaptureAudioFrame& frame) {
            if (const auto owner = weak_owner.lock()) {
                owner->Publish(frame);
            }
        },
        enabled);
    {
        std::lock_guard lock(mutex_);
        if (running_) {
            runtime->Shutdown();
            return {};
        }
        runtime_ = std::move(runtime);
        running_ = true;
    }
    LOGI("event=source.start component=was_audio_capture outcome=success");
    return {};
}

ModuleLifecycleResult WasAudioCaptureSource::Stop() {
    std::shared_ptr<WasAudioCaptureRuntime> runtime;
    {
        std::lock_guard lock(mutex_);
        if (!running_ && !runtime_) {
            return {};
        }
        running_ = false;
        runtime = std::move(runtime_);
    }
    if (runtime) {
        runtime->Shutdown();
    }
    LOGI("event=source.stop component=was_audio_capture outcome=success");
    return {};
}

ModuleLifecycleResult WasAudioCaptureSource::SetEnabled(const bool enabled) {
    const auto runtime = GetRuntime();
    {
        std::lock_guard lock(mutex_);
        enabled_ = enabled;
    }
    if (runtime) {
        runtime->SetAudioEnabled(enabled);
        if (!enabled) {
            runtime->StopProviding();
        }
    }
    LOGI("event=source.enable component=was_audio_capture enabled={} outcome=success",
         enabled);
    return {};
}

void WasAudioCaptureSource::SetLoopbackProcessId(const std::uint32_t pid) {
    if (const auto runtime = GetRuntime()) {
        runtime->SetLoopbackProcessId(pid);
    }
}

std::uint32_t WasAudioCaptureSource::GetLoopbackProcessId() const {
    if (const auto runtime = GetRuntime()) {
        return runtime->GetLoopbackProcessId();
    }
    return 0;
}

void WasAudioCaptureSource::StartProviding() {
    if (!GetRuntime()) {
        const auto started = Start();
        if (!started) {
            LOGE("event=source.provide_start component=was_audio_capture "
                 "outcome=failed reason={}", started.error().reason);
            return;
        }
    }
    bool enabled = false;
    std::shared_ptr<WasAudioCaptureRuntime> runtime;
    {
        std::lock_guard lock(mutex_);
        enabled = enabled_;
        runtime = runtime_;
    }
    if (!enabled || !runtime) {
        LOGW("event=source.provide_start component=was_audio_capture "
             "outcome=rejected reason=not_running_or_disabled");
        return;
    }
    runtime->StartProviding();
}

void WasAudioCaptureSource::StopProviding() {
    if (const auto runtime = GetRuntime()) {
        runtime->StopProviding();
    }
}

bool WasAudioCaptureSource::IsProviding() const {
    if (const auto runtime = GetRuntime()) {
        return runtime->IsProviding();
    }
    return false;
}

int WasAudioCaptureSource::GetLastStartError() const {
    if (const auto runtime = GetRuntime()) {
        return runtime->GetLastStartError();
    }
    return -1;
}

WasAudioCaptureSnapshot WasAudioCaptureSource::Snapshot() const {
    const auto runtime = GetRuntime();
    std::lock_guard lock(mutex_);
    return WasAudioCaptureSnapshot{
        .running = running_,
        .enabled = enabled_,
        .providing = runtime && runtime->IsProviding(),
        .loopback_process_id = runtime ? runtime->GetLoopbackProcessId() : 0,
        .last_start_error = runtime ? runtime->GetLastStartError() : -1,
        .delivered_frames = delivered_frames_,
    };
}

void WasAudioCaptureSource::Publish(const CaptureAudioFrame& frame) {
    FrameCallback callback;
    {
        std::lock_guard lock(mutex_);
        if (!running_ || !enabled_) {
            return;
        }
        ++delivered_frames_;
        callback = callback_;
    }
    if (callback) {
        callback(frame);
    }
}

std::shared_ptr<WasAudioCaptureRuntime>
WasAudioCaptureSource::GetRuntime() const {
    std::lock_guard lock(mutex_);
    return runtime_;
}

}  // namespace px::render
