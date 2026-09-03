#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <d3d11.h>
#include <wrl/client.h>

#include "modules/builtin_module_catalog.h"

namespace px {
class Image;
class VideoFrameCarrier;
}

namespace px::render {

inline constexpr std::string_view kFrameCarrierModuleId =
    "ebde829b-ef0f-4cbc-961a-3cca5f3d646c";

struct FrameCarrierParams final {
    std::string monitor_id;
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> device_context;
    std::uint64_t adapter_uid{0};
    bool full_color{false};
};

struct CarriedVideoFrame final {
    std::string monitor_id;
    std::uint64_t frame_index{0};
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
};

struct FrameCarrierSnapshot final {
    bool running{false};
    bool enabled{true};
    std::size_t active_monitors{0};
};

class FrameCarrierProcessor final
    : public std::enable_shared_from_this<FrameCarrierProcessor> {
public:
    using ImageCallback =
        std::function<void(const std::shared_ptr<Image>&)>;

    [[nodiscard]] static std::shared_ptr<FrameCarrierProcessor> Create(
        std::filesystem::path base_path);
    explicit FrameCarrierProcessor(std::filesystem::path base_path);
    ~FrameCarrierProcessor();

    FrameCarrierProcessor(const FrameCarrierProcessor&) = delete;
    FrameCarrierProcessor& operator=(const FrameCarrierProcessor&) = delete;

    [[nodiscard]] BuiltinModuleRegistration MakeRegistration();
    [[nodiscard]] ModuleLifecycleResult Start();
    [[nodiscard]] ModuleLifecycleResult Stop();
    [[nodiscard]] ModuleLifecycleResult SetEnabled(bool enabled);

    [[nodiscard]] bool InitializeMonitor(const FrameCarrierParams& params);
    [[nodiscard]] std::shared_ptr<CarriedVideoFrame> CopyTexture(
        const std::string& monitor_id,
        std::uint64_t shared_handle,
        std::uint64_t frame_index);
    [[nodiscard]] bool MapRawTexture(
        const std::string& monitor_id,
        const Microsoft::WRL::ComPtr<ID3D11Texture2D>& texture,
        DXGI_FORMAT format,
        int height,
        ImageCallback rgba_callback,
        ImageCallback yuv_callback);
    [[nodiscard]] bool ConvertRawImage(
        const std::string& monitor_id,
        const std::shared_ptr<Image>& image,
        ImageCallback rgba_callback,
        ImageCallback yuv_callback);
    void ClearAdapter(std::uint64_t adapter_uid);
    [[nodiscard]] FrameCarrierSnapshot Snapshot() const;

private:
    struct CarrierEntry final {
        std::shared_ptr<VideoFrameCarrier> carrier;
        std::uint64_t adapter_uid{0};
    };

    [[nodiscard]] bool LoadResources();
    [[nodiscard]] std::shared_ptr<VideoFrameCarrier> FindCarrier(
        const std::string& monitor_id) const;

    const std::filesystem::path base_path_;
    mutable std::mutex mutex_;
    std::map<std::string, CarrierEntry> carriers_;
    std::shared_ptr<Image> logo_image_;
    std::vector<std::pair<int, int>> logo_points_;
    std::vector<std::pair<int, int>> big_logo_points_;
    std::vector<std::pair<int, int>> cover_points_;
    bool running_{false};
    bool enabled_{true};
};

}  // namespace px::render
