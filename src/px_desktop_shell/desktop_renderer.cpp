#include "desktop_renderer.h"

#include "vulkan_renderer.h"
#include "window_host.h"

#include "px_client_sdk/platform/windows/windows_video_resources.h"
#include "px_common/log.h"

#include <utility>

namespace px::desktop {

std::expected<DesktopRenderer, std::string> DesktopRenderer::Create(const WindowHost& window, const bool preferVulkan) {
    if (preferVulkan && window.VulkanSurfaceAvailable()) {
        auto vulkan = VulkanRenderer::Create(window);
        if (vulkan) {
            LOGI("Desktop renderer selected Vulkan video and ImGui presentation");
            return DesktopRenderer{std::move(vulkan.value())};
        }
        LOGW("Vulkan video presentation is unavailable: {}. Falling back to D3D11", vulkan.error());
    }
    auto d3d = D3d11Renderer::Create(window);
    if (!d3d)
        return std::unexpected{d3d.error()};
    LOGI("Desktop renderer selected D3D11 video and ImGui presentation");
    return DesktopRenderer{std::move(d3d.value())};
}

DesktopRenderer::DesktopRenderer(D3d11Renderer renderer) : renderer_{std::move(renderer)} {}
DesktopRenderer::DesktopRenderer(std::shared_ptr<VulkanRenderer> renderer) : renderer_{std::move(renderer)} {}

bool DesktopRenderer::InitializeImGuiBackend() {
    return std::visit(
        [](auto& renderer) {
            if constexpr (std::is_same_v<std::decay_t<decltype(renderer)>, D3d11Renderer>)
                return renderer.InitializeImGuiBackend();
            else
                return renderer->InitializeImGuiBackend();
        },
        renderer_);
}

void DesktopRenderer::ShutdownImGuiBackend() {
    std::visit(
        [](auto& renderer) {
            if constexpr (std::is_same_v<std::decay_t<decltype(renderer)>, D3d11Renderer>)
                renderer.ShutdownImGuiBackend();
            else
                renderer->ShutdownImGuiBackend();
        },
        renderer_);
}

void DesktopRenderer::BeginImGuiFrame() const {
    std::visit(
        [](const auto& renderer) {
            if constexpr (std::is_same_v<std::decay_t<decltype(renderer)>, D3d11Renderer>)
                renderer.BeginImGuiFrame();
            else
                renderer->BeginImGuiFrame();
        },
        renderer_);
}

bool DesktopRenderer::Resize(const int width, const int height) {
    return std::visit(
        [width, height](auto& renderer) {
            if constexpr (std::is_same_v<std::decay_t<decltype(renderer)>, D3d11Renderer>)
                return renderer.Resize(width, height);
            else
                return renderer->Resize(width, height);
        },
        renderer_);
}

bool DesktopRenderer::UpdateVideoTexture(const int width, const int height, const std::span<const std::uint8_t> bgra) {
    return std::visit(
        [width, height, bgra](auto& renderer) {
            if constexpr (std::is_same_v<std::decay_t<decltype(renderer)>, D3d11Renderer>)
                return renderer.UpdateVideoTexture(width, height, bgra);
            else
                return renderer->UpdateVideoTexture(width, height, bgra);
        },
        renderer_);
}

bool DesktopRenderer::UpdateVideoFrame(const std::shared_ptr<RawImage>& image) {
    return std::visit(
        [&image](auto& renderer) {
            if constexpr (std::is_same_v<std::decay_t<decltype(renderer)>, D3d11Renderer>)
                return renderer.UpdateVideoFrame(image);
            else
                return renderer->UpdateVideoFrame(image);
        },
        renderer_);
}

std::uint64_t DesktopRenderer::VideoTextureId() const noexcept {
    return std::visit(
        [](const auto& renderer) {
            if constexpr (std::is_same_v<std::decay_t<decltype(renderer)>, D3d11Renderer>)
                return renderer.VideoTextureId();
            else
                return renderer->VideoTextureId();
        },
        renderer_);
}

std::shared_ptr<WindowsVideoResources> DesktopRenderer::VideoResources(const std::string& decoderPreference) {
    auto resources = std::make_shared<WindowsVideoResources>();
    resources->decoder_preference = decoderPreference;
    if (std::holds_alternative<D3d11Renderer>(renderer_)) {
        resources->d3d11 = std::get<D3d11Renderer>(renderer_).DeviceResources();
    } else {
        resources->use_vulkan = true;
        resources->vulkan_device = std::get<std::shared_ptr<VulkanRenderer>>(renderer_)->ShareDecoderDevice();
        resources->d3d11 = D3d11Renderer::CreateVideoDeviceResources();
    }
    return resources;
}

bool DesktopRenderer::UsesVulkan() const noexcept {
    return std::holds_alternative<std::shared_ptr<VulkanRenderer>>(renderer_);
}

void DesktopRenderer::Render() {
    std::visit(
        [](auto& renderer) {
            if constexpr (std::is_same_v<std::decay_t<decltype(renderer)>, D3d11Renderer>)
                renderer.Render();
            else
                renderer->Render();
        },
        renderer_);
}

} // namespace px::desktop
