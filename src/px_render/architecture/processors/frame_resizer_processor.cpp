#include "processors/frame_resizer_processor.h"

#include <utility>

#include "frame_render.h"
#include "px_common/log.h"
#include "px_common/privacy_log.h"

namespace px::render {
namespace {

RenderError MakeResizeError(std::string operation, std::string reason) {
    return RenderError{
        .code = RenderErrorCode::kModuleLifecycleRejected,
        .component = "frame_resizer",
        .operation = std::move(operation),
        .stage = "processor",
        .reason = std::move(reason),
        .recoverable = true,
    };
}

}  // namespace

std::shared_ptr<FrameResizerProcessor> FrameResizerProcessor::Create() {
    return std::make_shared<FrameResizerProcessor>();
}

FrameResizerProcessor::~FrameResizerProcessor() {
    static_cast<void>(Stop());
}

BuiltinModuleRegistration FrameResizerProcessor::MakeRegistration() {
    const std::weak_ptr<FrameResizerProcessor> weak_owner = weak_from_this();
    return BuiltinModuleRegistration{
        .descriptor = BuiltinModuleDescriptor{
            .id = std::string(kFrameResizerModuleId),
            .name = "Frame Resizer",
            .author = "GammaRay",
            .description = "Built-in D3D11 pre-encode resize processor",
            .version_name = "2.0.0",
            .version_code = 200,
            .capability = BuiltinModuleCapability::kProcessor,
            .default_enabled = true,
        },
        .start = [weak_owner]() -> PxAwaitable<ModuleLifecycleResult> {
            const auto owner = weak_owner.lock();
            co_return owner
                ? owner->Start()
                : ModuleLifecycleResult(std::unexpected(MakeResizeError(
                      "start", "processor owner expired")));
        },
        .stop = [weak_owner]() -> PxAwaitable<ModuleLifecycleResult> {
            const auto owner = weak_owner.lock();
            co_return owner ? owner->Stop() : ModuleLifecycleResult{};
        },
        .set_enabled = [weak_owner](const bool enabled) {
            const auto owner = weak_owner.lock();
            return owner
                ? owner->SetEnabled(enabled)
                : ModuleLifecycleResult(std::unexpected(MakeResizeError(
                      "set_enabled", "processor owner expired")));
        },
    };
}

ModuleLifecycleResult FrameResizerProcessor::Start() {
    std::lock_guard lock(mutex_);
    running_ = true;
    LOGI("event=processor.start component=frame_resizer outcome=success");
    return {};
}

ModuleLifecycleResult FrameResizerProcessor::Stop() {
    std::lock_guard lock(mutex_);
    if (!running_ && renderers_.empty()) {
        return {};
    }
    running_ = false;
    const auto released = renderers_.size();
    renderers_.clear();
    LOGI("event=processor.stop component=frame_resizer outcome=success "
         "released_renderers={}", released);
    return {};
}

ModuleLifecycleResult FrameResizerProcessor::SetEnabled(const bool enabled) {
    std::lock_guard lock(mutex_);
    enabled_ = enabled;
    if (!enabled_) {
        renderers_.clear();
    }
    return {};
}

Microsoft::WRL::ComPtr<ID3D11Texture2D> FrameResizerProcessor::Process(
    const Microsoft::WRL::ComPtr<ID3D11Texture2D>& input,
    const Microsoft::WRL::ComPtr<ID3D11Device>& device,
    const Microsoft::WRL::ComPtr<ID3D11DeviceContext>& device_context,
    const std::uint64_t adapter_uid,
    const std::string& monitor_id,
    const std::uint32_t target_width,
    const std::uint32_t target_height) {
    if (!input || !device || !device_context || monitor_id.empty() ||
        target_width == 0 || target_height == 0) {
        LOGE("event=processor.process component=frame_resizer "
             "code=PIPELINE_INVALID_FRAME operation=resize "
             "outcome=rejected recoverable=true");
        return {};
    }

    D3D11_TEXTURE2D_DESC source{};
    input->GetDesc(&source);
    std::lock_guard lock(mutex_);
    if (!running_ || !enabled_) {
        return {};
    }
    const FrameResizeSnapshot desired{
        .monitor_id = monitor_id,
        .source_width = source.Width,
        .source_height = source.Height,
        .target_width = target_width,
        .target_height = target_height,
        .format = source.Format,
    };
    auto found = renderers_.find(monitor_id);
    const auto needs_rebuild = found == renderers_.end() ||
        found->second.adapter_uid != adapter_uid ||
        found->second.snapshot.source_width != desired.source_width ||
        found->second.snapshot.source_height != desired.source_height ||
        found->second.snapshot.target_width != desired.target_width ||
        found->second.snapshot.target_height != desired.target_height ||
        found->second.snapshot.format != desired.format;
    if (needs_rebuild) {
        auto renderer = std::make_shared<FrameRender>(device, device_context);
        const auto prepared = renderer->Prepare(
            SIZE{static_cast<LONG>(target_width),
                 static_cast<LONG>(target_height)},
            SIZE{static_cast<LONG>(source.Width),
                 static_cast<LONG>(source.Height)},
            static_cast<int>(source.Format));
        if (FAILED(prepared)) {
            LOGE("event=processor.prepare component=frame_resizer "
                 "code=PROCESSOR_RESIZE_PREPARE_FAILED operation=prepare "
                 "outcome=failed recoverable=true monitor={} adapter_uid={} "
                 "native_code={}",
                 PrivacyLogId(monitor_id), adapter_uid,
                 static_cast<std::int64_t>(prepared));
            return {};
        }
        found = renderers_.insert_or_assign(
            monitor_id,
            RenderEntry{
                .renderer = std::move(renderer),
                .snapshot = desired,
                .adapter_uid = adapter_uid,
            }).first;
        LOGI("event=processor.prepare component=frame_resizer monitor={} "
             "source={}x{} target={}x{} adapter_uid={} outcome=success",
             PrivacyLogId(monitor_id), source.Width, source.Height,
             target_width, target_height, adapter_uid);
    }

    const auto renderer = found->second.renderer;
    const auto context = renderer->GetD3D11DeviceContext();
    const auto source_texture = renderer->GetSrcTexture();
    context->CopyResource(source_texture.Get(), input.Get());
    if (FAILED(renderer->Draw())) {
        LOGE("event=processor.process component=frame_resizer "
             "code=PROCESSOR_RESIZE_DRAW_FAILED operation=draw "
             "outcome=failed recoverable=true monitor={}",
             PrivacyLogId(monitor_id));
        return {};
    }
    return renderer->GetFinalTexture();
}

std::optional<FrameResizeSnapshot> FrameResizerProcessor::Snapshot(
    const std::string& monitor_id) const {
    std::lock_guard lock(mutex_);
    const auto found = renderers_.find(monitor_id);
    return found == renderers_.end()
        ? std::nullopt
        : std::optional<FrameResizeSnapshot>(found->second.snapshot);
}

void FrameResizerProcessor::ClearAdapter(const std::uint64_t adapter_uid) {
    std::lock_guard lock(mutex_);
    for (auto entry = renderers_.begin(); entry != renderers_.end();) {
        if (entry->second.adapter_uid == adapter_uid) {
            entry = renderers_.erase(entry);
        }
        else {
            ++entry;
        }
    }
}

}  // namespace px::render
