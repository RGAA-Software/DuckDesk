#pragma once

#include "d3d11_renderer.h"

#include <expected>
#include <memory>
#include <span>
#include <string>
#include <variant>

namespace px {
class RawImage;
struct WindowsVideoResources;
} // namespace px

namespace px::desktop {

class VulkanRenderer;
class WindowHost;

class DesktopRenderer final {
  public:
    static std::expected<DesktopRenderer, std::string> Create(const WindowHost& window, bool preferVulkan);

    explicit DesktopRenderer(D3d11Renderer renderer);
    explicit DesktopRenderer(std::shared_ptr<VulkanRenderer> renderer);

    bool InitializeImGuiBackend();
    void ShutdownImGuiBackend();
    void BeginImGuiFrame() const;
    bool Resize(int width, int height);
    bool UpdateVideoTexture(int width, int height, std::span<const std::uint8_t> bgra);
    bool UpdateVideoFrame(const std::shared_ptr<RawImage>& image);
    [[nodiscard]] std::uint64_t VideoTextureId() const noexcept;
    [[nodiscard]] std::shared_ptr<WindowsVideoResources> VideoResources(const std::string& decoderPreference);
    [[nodiscard]] bool UsesVulkan() const noexcept;
    void Render();

  private:
    std::variant<D3d11Renderer, std::shared_ptr<VulkanRenderer>> renderer_;
};

} // namespace px::desktop
