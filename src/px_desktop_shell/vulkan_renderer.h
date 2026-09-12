#pragma once

#include "px_client_sdk/av_buffer_ref.h"

#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>

namespace px {
class RawImage;
}

namespace px::desktop {

class WindowHost;

class VulkanRenderer final : public std::enable_shared_from_this<VulkanRenderer> {
  public:
    static std::expected<std::shared_ptr<VulkanRenderer>, std::string> Create(const WindowHost& window);

    explicit VulkanRenderer(const WindowHost& window);
    ~VulkanRenderer();

    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;

    bool Initialize();
    bool InitializeImGuiBackend();
    void ShutdownImGuiBackend();
    void BeginImGuiFrame() const;
    bool Resize(int width, int height);
    bool UpdateVideoTexture(int width, int height, std::span<const std::uint8_t> bgra);
    bool UpdateVideoFrame(const std::shared_ptr<RawImage>& image);
    [[nodiscard]] std::uint64_t VideoTextureId() const noexcept;
    [[nodiscard]] AvBufferPtr ShareDecoderDevice();
    void Render();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_{};
};

} // namespace px::desktop
