#include "processors/frame_carrier_processor.h"

#include <utility>
#include <vector>

#include "px_common_new/file.h"
#include "px_common_new/image.h"
#include "px_common_new/log.h"
#include "px_common_new/privacy_log.h"
#include "px_common_new/string_util.h"
#include "image_generator.h"
#include "video_frame_carrier.h"

namespace px::render {
namespace {

RenderError MakeCarrierError(std::string operation, std::string reason) {
    return RenderError{
        .code = RenderErrorCode::kModuleLifecycleRejected,
        .component = "frame_carrier",
        .operation = std::move(operation),
        .stage = "processor",
        .reason = std::move(reason),
        .recoverable = true,
    };
}

std::vector<std::pair<int, int>> ExtractDarkPoints(
    const std::shared_ptr<Image>& image) {
    std::vector<std::pair<int, int>> points;
    if (!image || !image->data) {
        return points;
    }
    points.reserve(static_cast<std::size_t>(image->width * image->height / 4));
    for (int row = 0; row < image->height; ++row) {
        for (int column = 0; column < image->width; ++column) {
            if (image->data->At(
                    static_cast<std::int64_t>(row) * image->width + column) == 0) {
                points.emplace_back(column, row);
            }
        }
    }
    return points;
}

}  // namespace

std::shared_ptr<FrameCarrierProcessor> FrameCarrierProcessor::Create(
    std::filesystem::path base_path) {
    return std::make_shared<FrameCarrierProcessor>(std::move(base_path));
}

FrameCarrierProcessor::FrameCarrierProcessor(std::filesystem::path base_path)
    : base_path_(std::move(base_path)) {}

FrameCarrierProcessor::~FrameCarrierProcessor() {
    static_cast<void>(Stop());
}

BuiltinModuleRegistration FrameCarrierProcessor::MakeRegistration() {
    const std::weak_ptr<FrameCarrierProcessor> weak_owner = weak_from_this();
    return BuiltinModuleRegistration{
        .descriptor = BuiltinModuleDescriptor{
            .id = std::string(kFrameCarrierModuleId),
            .name = "Frame Carrier",
            .author = "GammaRay",
            .description = "Built-in shared-texture and pixel-format processor",
            .version_name = "2.0.0",
            .version_code = 200,
            .capability = BuiltinModuleCapability::kProcessor,
            .default_enabled = true,
        },
        .start = [weak_owner]() -> PxAwaitable<ModuleLifecycleResult> {
            const auto owner = weak_owner.lock();
            co_return owner
                ? owner->Start()
                : ModuleLifecycleResult(std::unexpected(MakeCarrierError(
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
                : ModuleLifecycleResult(std::unexpected(MakeCarrierError(
                      "set_enabled", "processor owner expired")));
        },
    };
}

ModuleLifecycleResult FrameCarrierProcessor::Start() {
    std::lock_guard lock(mutex_);
    if (running_) {
        return {};
    }
    if (!LoadResources()) {
        return std::unexpected(MakeCarrierError(
            "start", "failed to initialize logo resources"));
    }
    running_ = true;
    LOGI("event=processor.start component=frame_carrier outcome=success");
    return {};
}

ModuleLifecycleResult FrameCarrierProcessor::Stop() {
    std::map<std::string, CarrierEntry> carriers;
    {
        std::lock_guard lock(mutex_);
        if (!running_ && carriers_.empty()) {
            return {};
        }
        running_ = false;
        carriers.swap(carriers_);
    }
    for (const auto& [monitor_id, entry] : carriers) {
        if (entry.carrier) {
            entry.carrier->Exit();
        }
    }
    LOGI("event=processor.stop component=frame_carrier outcome=success "
         "released_carriers={}", carriers.size());
    return {};
}

ModuleLifecycleResult FrameCarrierProcessor::SetEnabled(const bool enabled) {
    if (enabled) {
        std::lock_guard lock(mutex_);
        enabled_ = true;
        return {};
    }
    std::map<std::string, CarrierEntry> carriers;
    {
        std::lock_guard lock(mutex_);
        enabled_ = false;
        carriers.swap(carriers_);
    }
    for (const auto& [monitor_id, entry] : carriers) {
        if (entry.carrier) {
            entry.carrier->Exit();
        }
    }
    return {};
}

bool FrameCarrierProcessor::InitializeMonitor(const FrameCarrierParams& params) {
    if (params.monitor_id.empty() || !params.device || !params.device_context) {
        LOGE("event=processor.initialize component=frame_carrier "
             "code=PIPELINE_INVALID_FRAME operation=initialize "
             "outcome=rejected recoverable=true");
        return false;
    }
    VideoFrameCarrierResources resources;
    {
        std::lock_guard lock(mutex_);
        if (!running_ || !enabled_) {
            return false;
        }
        resources.logo_image = logo_image_;
        resources.logo_points = logo_points_;
        resources.big_logo_points = big_logo_points_;
        resources.cover_points = cover_points_;
    }
    const auto carrier = VideoFrameCarrier::Create(
        std::move(resources), params.device, params.device_context,
        params.adapter_uid, params.monitor_id, params.full_color);
    if (!carrier) {
        return false;
    }
    std::shared_ptr<VideoFrameCarrier> previous;
    {
        std::lock_guard lock(mutex_);
        if (!running_ || !enabled_) {
            carrier->Exit();
            return false;
        }
        auto found = carriers_.find(params.monitor_id);
        if (found != carriers_.end()) {
            previous = std::move(found->second.carrier);
        }
        carriers_.insert_or_assign(params.monitor_id, CarrierEntry{
            .carrier = carrier,
            .adapter_uid = params.adapter_uid,
        });
    }
    if (previous) {
        previous->Exit();
    }
    LOGI("event=processor.initialize component=frame_carrier monitor={} "
         "adapter_uid={} outcome=success",
         PrivacyLogId(params.monitor_id), params.adapter_uid);
    return true;
}

std::shared_ptr<CarriedVideoFrame> FrameCarrierProcessor::CopyTexture(
    const std::string& monitor_id,
    const std::uint64_t shared_handle,
    const std::uint64_t frame_index) {
    const auto carrier = FindCarrier(monitor_id);
    if (!carrier) {
        LOGE("event=processor.copy component=frame_carrier "
             "code=PROCESSOR_CARRIER_NOT_FOUND operation=copy_texture "
             "outcome=failed recoverable=true monitor={}",
             PrivacyLogId(monitor_id));
        return {};
    }
    auto texture = carrier->CopyTexture(monitor_id, shared_handle, frame_index);
    if (!texture) {
        return {};
    }
    return std::make_shared<CarriedVideoFrame>(CarriedVideoFrame{
        .monitor_id = monitor_id,
        .frame_index = frame_index,
        .texture = std::move(texture),
    });
}

bool FrameCarrierProcessor::MapRawTexture(
    const std::string& monitor_id,
    const Microsoft::WRL::ComPtr<ID3D11Texture2D>& texture,
    const DXGI_FORMAT format,
    const int height,
    ImageCallback rgba_callback,
    ImageCallback yuv_callback) {
    const auto carrier = FindCarrier(monitor_id);
    return carrier && carrier->MapRawTexture(
        texture, format, height, std::move(rgba_callback),
        std::move(yuv_callback));
}

bool FrameCarrierProcessor::ConvertRawImage(
    const std::string& monitor_id,
    const std::shared_ptr<Image>& image,
    ImageCallback rgba_callback,
    ImageCallback yuv_callback) {
    const auto carrier = FindCarrier(monitor_id);
    return carrier && carrier->ConvertRawImage(
        image, std::move(rgba_callback), std::move(yuv_callback));
}

void FrameCarrierProcessor::ClearAdapter(const std::uint64_t adapter_uid) {
    std::vector<std::shared_ptr<VideoFrameCarrier>> removed;
    {
        std::lock_guard lock(mutex_);
        for (auto entry = carriers_.begin(); entry != carriers_.end();) {
            if (entry->second.adapter_uid == adapter_uid) {
                removed.push_back(std::move(entry->second.carrier));
                entry = carriers_.erase(entry);
            }
            else {
                ++entry;
            }
        }
    }
    for (const auto& carrier : removed) {
        if (carrier) {
            carrier->Exit();
        }
    }
}

FrameCarrierSnapshot FrameCarrierProcessor::Snapshot() const {
    std::lock_guard lock(mutex_);
    return FrameCarrierSnapshot{
        .running = running_,
        .enabled = enabled_,
        .active_monitors = carriers_.size(),
    };
}

bool FrameCarrierProcessor::LoadResources() {
    logo_points_.clear();
    big_logo_points_.clear();
    cover_points_.clear();
    const auto logo_path = base_path_ / L"deps" / L"rd_plugins" /
        L"resources" / L"ic_logo_point.png";
    if (std::filesystem::exists(logo_path)) {
        const auto file = File::OpenForReadB(
            U8Path(StringUtil::ToUTF8(logo_path.wstring())));
        if (file) {
            logo_image_ = Image::MakeByCompressedImage(file->ReadAll());
        }
    }
    const auto logo = ImageGenerator::CreateGrayscaleWithText(
        256, 48, 0xff, 0x00, 22, true, "www.pixels.yun");
    const auto big_logo = ImageGenerator::CreateGrayscaleWithText(
        256, 48, 0xff, 0x00, 24, true, "www.pixels.yun");
    const auto cover = ImageGenerator::CreateGrayscaleWithText(
        280, 48, 0xff, 0x00, 22, true, "Unlicensed Stream");
    logo_points_ = ExtractDarkPoints(logo);
    big_logo_points_ = ExtractDarkPoints(big_logo);
    cover_points_ = ExtractDarkPoints(cover);
    LOGI("event=processor.resources component=frame_carrier outcome=success "
         "logo_loaded={} logo_points={} big_logo_points={} cover_points={}",
         static_cast<bool>(logo_image_), logo_points_.size(),
         big_logo_points_.size(), cover_points_.size());
    return logo && big_logo && cover;
}

std::shared_ptr<VideoFrameCarrier> FrameCarrierProcessor::FindCarrier(
    const std::string& monitor_id) const {
    std::lock_guard lock(mutex_);
    if (!running_ || !enabled_) {
        return {};
    }
    const auto found = carriers_.find(monitor_id);
    return found == carriers_.end() ? std::shared_ptr<VideoFrameCarrier>{}
                                    : found->second.carrier;
}

}  // namespace px::render
