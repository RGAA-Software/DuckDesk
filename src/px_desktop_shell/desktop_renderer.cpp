#include "desktop_renderer.h"

#if PX_DESKTOP_SHELL_MEDIA
#include "vulkan_renderer.h"
#endif
#include "window_host.h"

#if PX_DESKTOP_SHELL_MEDIA
#include "px_client_sdk/platform/windows/windows_video_resources.h"
#endif
#include "px_common/log.h"

#include <utility>

namespace px::desktop {

std::expected<DesktopRenderer, std::string> DesktopRenderer::Create(const WindowHost& window, const bool preferVulkan) {
#if PX_DESKTOP_SHELL_MEDIA
    if (preferVulkan && window.VulkanSurfaceAvailable()) {
        auto vulkan = VulkanRenderer::Create(window);
        if (vulkan) {
            LOGI("Desktop renderer selected Vulkan video and ImGui presentation");
            return DesktopRenderer{std::move(vulkan.value())};
        }
        LOGW("Vulkan video presentation is unavailable: {}. Falling back to D3D11", vulkan.error());
    }
#else
    static_cast<void>(preferVulkan);
#endif
    auto d3d = D3d11Renderer::Create(window);
    if (!d3d)
        return std::unexpected{d3d.error()};
    LOGI("Desktop renderer selected D3D11 video and ImGui presentation");
    return DesktopRenderer{std::move(d3d.value())};
}

DesktopRenderer::DesktopRenderer(D3d11Renderer renderer) : renderer_{std::move(renderer)} {}
DesktopRenderer::DesktopRenderer(std::shared_ptr<VulkanRenderer> renderer) : renderer_{std::move(renderer)} {}

bool DesktopRenderer::InitializeImGuiBackend() {
#if PX_DESKTOP_SHELL_MEDIA
    return std::visit(
        [](auto& renderer) {
            if constexpr (std::is_same_v<std::decay_t<decltype(renderer)>, D3d11Renderer>)
                return renderer.InitializeImGuiBackend();
            else
                return renderer->InitializeImGuiBackend();
        },
        renderer_);
#else
    return std::get<D3d11Renderer>(renderer_).InitializeImGuiBackend();
#endif
}

void DesktopRenderer::ShutdownImGuiBackend() {
#if PX_DESKTOP_SHELL_MEDIA
    std::visit(
        [](auto& renderer) {
            if constexpr (std::is_same_v<std::decay_t<decltype(renderer)>, D3d11Renderer>)
                renderer.ShutdownImGuiBackend();
            else
                renderer->ShutdownImGuiBackend();
        },
        renderer_);
#else
    std::get<D3d11Renderer>(renderer_).ShutdownImGuiBackend();
#endif
}

void DesktopRenderer::BeginImGuiFrame() const {
#if PX_DESKTOP_SHELL_MEDIA
    std::visit(
        [](const auto& renderer) {
            if constexpr (std::is_same_v<std::decay_t<decltype(renderer)>, D3d11Renderer>)
                renderer.BeginImGuiFrame();
            else
                renderer->BeginImGuiFrame();
        },
        renderer_);
#else
    std::get<D3d11Renderer>(renderer_).BeginImGuiFrame();
#endif
}

bool DesktopRenderer::Resize(const int width, const int height) {
#if PX_DESKTOP_SHELL_MEDIA
    return std::visit(
        [width, height](auto& renderer) {
            if constexpr (std::is_same_v<std::decay_t<decltype(renderer)>, D3d11Renderer>)
                return renderer.Resize(width, height);
            else
                return renderer->Resize(width, height);
        },
        renderer_);
#else
    return std::get<D3d11Renderer>(renderer_).Resize(width, height);
#endif
}

bool DesktopRenderer::UpdateVideoTexture(const int width, const int height, const std::span<const std::uint8_t> bgra) {
#if PX_DESKTOP_SHELL_MEDIA
    return std::visit(
        [width, height, bgra](auto& renderer) {
            if constexpr (std::is_same_v<std::decay_t<decltype(renderer)>, D3d11Renderer>)
                return renderer.UpdateVideoTexture(width, height, bgra);
            else
                return renderer->UpdateVideoTexture(width, height, bgra);
        },
        renderer_);
#else
    return std::get<D3d11Renderer>(renderer_).UpdateVideoTexture(width, height, bgra);
#endif
}

bool DesktopRenderer::UpdateVideoFrame(const std::shared_ptr<RawImage>& image) {
#if PX_DESKTOP_SHELL_MEDIA
    return std::visit(
        [&image](auto& renderer) {
            if constexpr (std::is_same_v<std::decay_t<decltype(renderer)>, D3d11Renderer>)
                return renderer.UpdateVideoFrame(image);
            else
                return renderer->UpdateVideoFrame(image);
        },
        renderer_);
#else
    return std::get<D3d11Renderer>(renderer_).UpdateVideoFrame(image);
#endif
}

std::uint64_t DesktopRenderer::VideoTextureId() const noexcept {
#if PX_DESKTOP_SHELL_MEDIA
    return std::visit(
        [](const auto& renderer) {
            if constexpr (std::is_same_v<std::decay_t<decltype(renderer)>, D3d11Renderer>)
                return renderer.VideoTextureId();
            else
                return renderer->VideoTextureId();
        },
        renderer_);
#else
    return std::get<D3d11Renderer>(renderer_).VideoTextureId();
#endif
}

std::shared_ptr<WindowsVideoResources> DesktopRenderer::VideoResources(const std::string& decoderPreference) {
#if PX_DESKTOP_SHELL_MEDIA
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
#else
    static_cast<void>(decoderPreference);
    return {};
#endif
}

bool DesktopRenderer::UsesVulkan() const noexcept {
#if PX_DESKTOP_SHELL_MEDIA
    return std::holds_alternative<std::shared_ptr<VulkanRenderer>>(renderer_);
#else
    return false;
#endif
}

void DesktopRenderer::Render() {
#if PX_DESKTOP_SHELL_MEDIA
    std::visit(
        [](auto& renderer) {
            if constexpr (std::is_same_v<std::decay_t<decltype(renderer)>, D3d11Renderer>)
                renderer.Render();
            else
                renderer->Render();
        },
        renderer_);
#else
    std::get<D3d11Renderer>(renderer_).Render();
#endif
}

} // namespace px::desktop
