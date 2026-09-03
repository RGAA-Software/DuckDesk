#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include <d3d11.h>
#include <wrl/client.h>

#include "modules/builtin_module_catalog.h"

namespace px {
class FrameRender;
}

namespace px::render {

inline constexpr std::string_view kFrameResizerModuleId =
    "cd407b93-429c-44a9-9c36-3429d9b390bb";

struct FrameResizeSnapshot final {
    std::string monitor_id;
    std::uint32_t source_width{0};
    std::uint32_t source_height{0};
    std::uint32_t target_width{0};
    std::uint32_t target_height{0};
    DXGI_FORMAT format{DXGI_FORMAT_UNKNOWN};
};

// GPU processor with ComPtr-owned device resources. The encoder worker calls
// Process serially; mutex_ also makes topology/reset control safe.
class FrameResizerProcessor final
    : public std::enable_shared_from_this<FrameResizerProcessor> {
public:
    [[nodiscard]] static std::shared_ptr<FrameResizerProcessor> Create();
    FrameResizerProcessor() = default;
    ~FrameResizerProcessor();

    FrameResizerProcessor(const FrameResizerProcessor&) = delete;
    FrameResizerProcessor& operator=(const FrameResizerProcessor&) = delete;

    [[nodiscard]] BuiltinModuleRegistration MakeRegistration();
    [[nodiscard]] ModuleLifecycleResult Start();
    [[nodiscard]] ModuleLifecycleResult Stop();
    [[nodiscard]] ModuleLifecycleResult SetEnabled(bool enabled);

    [[nodiscard]] Microsoft::WRL::ComPtr<ID3D11Texture2D> Process(
        const Microsoft::WRL::ComPtr<ID3D11Texture2D>& input,
        const Microsoft::WRL::ComPtr<ID3D11Device>& device,
        const Microsoft::WRL::ComPtr<ID3D11DeviceContext>& device_context,
        std::uint64_t adapter_uid,
        const std::string& monitor_id,
        std::uint32_t target_width,
        std::uint32_t target_height);
    [[nodiscard]] std::optional<FrameResizeSnapshot> Snapshot(
        const std::string& monitor_id) const;
    void ClearAdapter(std::uint64_t adapter_uid);

private:
    struct RenderEntry final {
        std::shared_ptr<FrameRender> renderer;
        FrameResizeSnapshot snapshot;
        std::uint64_t adapter_uid{0};
    };

    mutable std::mutex mutex_;
    std::map<std::string, RenderEntry> renderers_;
    bool running_{false};
    bool enabled_{true};
};

}  // namespace px::render
