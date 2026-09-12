#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>

namespace px::desktop {

class WindowHost;

} // namespace px::desktop

namespace px {
class D3D11DeviceWrapper;
class RawImage;
} // namespace px

namespace px::desktop {

class D3d11Renderer final {
  public:
    static std::expected<D3d11Renderer, std::string> Create(const WindowHost& window);
    [[nodiscard]] static std::shared_ptr<D3D11DeviceWrapper> CreateVideoDeviceResources();

    D3d11Renderer(D3d11Renderer&&) noexcept;
    D3d11Renderer& operator=(D3d11Renderer&&) noexcept;
    ~D3d11Renderer();

    D3d11Renderer(const D3d11Renderer&) = delete;
    D3d11Renderer& operator=(const D3d11Renderer&) = delete;

    bool InitializeImGuiBackend();
    void ShutdownImGuiBackend();
    void BeginImGuiFrame() const;
    bool Resize(int width, int height);
    bool UpdateVideoTexture(int width, int height, std::span<const std::uint8_t> bgra);
    bool UpdateVideoFrame(const std::shared_ptr<RawImage>& image);
    [[nodiscard]] std::uint64_t VideoTextureId() const noexcept;
    [[nodiscard]] std::shared_ptr<D3D11DeviceWrapper> DeviceResources() const noexcept;
    void Render() const;

  private:
    struct Impl;

    explicit D3d11Renderer(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> impl_{};
};

} // namespace px::desktop
