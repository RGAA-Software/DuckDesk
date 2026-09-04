#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "architecture/diagnostics/render_error.h"
#include "architecture/pipeline/captured_media_pipeline.h"
#include "architecture/pipeline/media_types.h"
#include "px_common_new/async_runtime.h"

namespace px::render {

// A plug-in is a replaceable node in the one-way media graph. It is not a
// delivery format: built-in and dynamically delivered nodes implement the
// same role contract, while any DLL ABI adapter remains outside this API.
enum class FlowNodeRole {
    kVideoSource,
    kAudioSource,
    kVideoProcessor,
    kAudioProcessor,
    kVideoEncoder,
    kAudioEncoder,
    kObserver,
    kSink,
};

struct FlowNodeDescriptor final {
    std::string id;
    std::string name;
    std::string author;
    std::string description;
    std::string version_name;
    std::uint32_t version_code{0};
    FlowNodeRole role{FlowNodeRole::kObserver};
    bool default_enabled{true};
};

struct FlowNodeStartContext final {
    // Every asynchronous child operation must be attached to this scope.
    // The composition root cancels and joins the scope before unloading a
    // dynamically delivered node.
    std::shared_ptr<PxAsyncScope> async_scope;
};

using FlowNodeLifecycleResult = std::expected<void, RenderError>;

class FlowNodePlugin {
public:
    virtual ~FlowNodePlugin() = default;

    [[nodiscard]] virtual const FlowNodeDescriptor& Descriptor() const noexcept = 0;
    [[nodiscard]] virtual bool IsEnabled() const noexcept = 0;
    [[nodiscard]] virtual FlowNodeLifecycleResult SetEnabled(bool enabled) = 0;

    // Lifecycle is awaitable because start/stop may bridge callback APIs.
    // Frame processing below deliberately remains synchronous: high-rate
    // frames must never create one coroutine per frame.
    [[nodiscard]] virtual PxAwaitable<FlowNodeLifecycleResult> Start(
        FlowNodeStartContext context) = 0;
    [[nodiscard]] virtual PxAwaitable<FlowNodeLifecycleResult> Stop() = 0;
};

class VideoSourcePlugin : public FlowNodePlugin {
public:
    virtual FlowNodeLifecycleResult AttachOutput(
        std::shared_ptr<MediaSourcePort> output) = 0;
};

class AudioSourcePlugin : public FlowNodePlugin {
public:
    virtual FlowNodeLifecycleResult AttachOutput(
        std::shared_ptr<MediaSourcePort> output) = 0;
};

class VideoProcessorPlugin : public FlowNodePlugin {
public:
    [[nodiscard]] virtual CapturedVideoProcessResult Process(
        const std::shared_ptr<const CapturedVideoFrame>& frame) = 0;
};

class AudioProcessorPlugin : public FlowNodePlugin {
public:
    [[nodiscard]] virtual CapturedAudioProcessResult Process(
        const std::shared_ptr<const CapturedAudioFrame>& frame) = 0;
};

using EncodedVideoResult = std::expected<
    std::shared_ptr<const EncodedVideoFrame>, RenderError>;
using EncodedAudioResult = std::expected<
    std::shared_ptr<const EncodedAudioFrame>, RenderError>;

class VideoEncoderPlugin : public FlowNodePlugin {
public:
    [[nodiscard]] virtual EncodedVideoResult Encode(
        const std::shared_ptr<const CapturedVideoFrame>& frame) = 0;
    virtual void RequestKeyFrame(const std::string& monitor_id) = 0;
};

class AudioEncoderPlugin : public FlowNodePlugin {
public:
    [[nodiscard]] virtual EncodedAudioResult Encode(
        const std::shared_ptr<const CapturedAudioFrame>& frame) = 0;
};

class ObserverPlugin : public FlowNodePlugin {
public:
    // Observer callbacks are read-only and cannot affect graph delivery.
    virtual void ObserveCapturedVideo(
        const std::shared_ptr<const CapturedVideoFrame>& frame) noexcept = 0;
    virtual void ObserveEncodedVideo(
        const std::shared_ptr<const EncodedVideoFrame>& frame) noexcept = 0;
    virtual void ObserveCapturedAudio(
        const std::shared_ptr<const CapturedAudioFrame>& frame) noexcept = 0;
    virtual void ObserveEncodedAudio(
        const std::shared_ptr<const EncodedAudioFrame>& frame) noexcept = 0;
};

class SinkPlugin : public FlowNodePlugin {
public:
    // Submit must only validate/enqueue. Blocking I/O belongs to a bounded
    // queue consumed by one long-lived coroutine owned by async_scope.
    [[nodiscard]] virtual MediaSubmitResult SubmitVideo(
        const std::shared_ptr<const EncodedVideoFrame>& frame) = 0;
    [[nodiscard]] virtual MediaSubmitResult SubmitAudio(
        const std::shared_ptr<const EncodedAudioFrame>& frame) = 0;
};

using FlowNodePluginFactory =
    std::function<std::shared_ptr<FlowNodePlugin>()>;

struct FlowNodePluginRegistration final {
    FlowNodeDescriptor descriptor;
    FlowNodePluginFactory create;
    std::vector<std::string> dependencies;
};

}  // namespace px::render
