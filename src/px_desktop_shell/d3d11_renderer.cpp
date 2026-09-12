#include "d3d11_renderer.h"

#include "window_host.h"

#include <SDL3/SDL.h>
#include <backends/imgui_impl_dx11.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <cstring>
#include <memory>
#include <utility>

namespace px::desktop {

struct D3d11Renderer::Impl final {
    Microsoft::WRL::ComPtr<ID3D11Device> device{};
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> deviceContext{};
    Microsoft::WRL::ComPtr<IDXGISwapChain> swapChain{};
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> renderTarget{};
    Microsoft::WRL::ComPtr<ID3D11Texture2D> videoTexture{};
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> videoTextureView{};
    int videoWidth{};
    int videoHeight{};
    bool imguiBackendInitialized{false};

    bool CreateRenderTarget() {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer{};
        if (FAILED(swapChain->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf())))) {
            return false;
        }
        return SUCCEEDED(device->CreateRenderTargetView(backBuffer.Get(), nullptr, renderTarget.ReleaseAndGetAddressOf()));
    }
};

std::expected<D3d11Renderer, std::string> D3d11Renderer::Create(const WindowHost& window) {
    const HWND nativeWindow{
        reinterpret_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(&window.Native()), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr))};
    if (!nativeWindow) {
        return std::unexpected{"SDL did not expose a Win32 window handle"};
    }

    DXGI_SWAP_CHAIN_DESC description{};
    description.BufferCount = 2;
    description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.OutputWindow = nativeWindow;
    description.SampleDesc.Count = 1;
    description.Windowed = TRUE;
    description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    auto impl = std::make_unique<Impl>();
    constexpr std::array featureLevels{D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL selectedFeatureLevel{};
    const HRESULT result{D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, featureLevels.data(), static_cast<UINT>(featureLevels.size()), D3D11_SDK_VERSION, &description,
        impl->swapChain.GetAddressOf(), impl->device.GetAddressOf(), &selectedFeatureLevel, impl->deviceContext.GetAddressOf())};
    if (FAILED(result) || !impl->CreateRenderTarget()) {
        return std::unexpected{"D3D11 device or render target creation failed"};
    }
    return D3d11Renderer{std::move(impl)};
}

D3d11Renderer::D3d11Renderer(std::unique_ptr<Impl> impl) noexcept : impl_{std::move(impl)} {}
D3d11Renderer::D3d11Renderer(D3d11Renderer&&) noexcept = default;
D3d11Renderer& D3d11Renderer::operator=(D3d11Renderer&&) noexcept = default;

D3d11Renderer::~D3d11Renderer() {
    ShutdownImGuiBackend();
}

bool D3d11Renderer::InitializeImGuiBackend() {
    if (impl_->imguiBackendInitialized) {
        return true;
    }
    impl_->imguiBackendInitialized = ImGui_ImplDX11_Init(impl_->device.Get(), impl_->deviceContext.Get());
    return impl_->imguiBackendInitialized;
}

void D3d11Renderer::ShutdownImGuiBackend() {
    if (impl_ && impl_->imguiBackendInitialized) {
        ImGui_ImplDX11_Shutdown();
        impl_->imguiBackendInitialized = false;
    }
}

void D3d11Renderer::BeginImGuiFrame() const {
    ImGui_ImplDX11_NewFrame();
}

bool D3d11Renderer::Resize(const int width, const int height) {
    if (width <= 0 || height <= 0) {
        return true;
    }
    impl_->renderTarget.Reset();
    if (FAILED(impl_->swapChain->ResizeBuffers(0, static_cast<UINT>(width), static_cast<UINT>(height), DXGI_FORMAT_UNKNOWN, 0))) {
        return false;
    }
    return impl_->CreateRenderTarget();
}

bool D3d11Renderer::UpdateVideoTexture(const int width, const int height, const std::span<const std::uint8_t> bgra) {
    if (width <= 0 || height <= 0 || bgra.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U) {
        return false;
    }
    if (!impl_->videoTexture || impl_->videoWidth != width || impl_->videoHeight != height) {
        impl_->videoTextureView.Reset();
        impl_->videoTexture.Reset();
        D3D11_TEXTURE2D_DESC description{};
        description.Width = static_cast<UINT>(width);
        description.Height = static_cast<UINT>(height);
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_DYNAMIC;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(impl_->device->CreateTexture2D(&description, nullptr, impl_->videoTexture.ReleaseAndGetAddressOf())) ||
            FAILED(impl_->device->CreateShaderResourceView(impl_->videoTexture.Get(), nullptr, impl_->videoTextureView.ReleaseAndGetAddressOf()))) {
            return false;
        }
        impl_->videoWidth = width;
        impl_->videoHeight = height;
    }
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(impl_->deviceContext->Map(impl_->videoTexture.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        return false;
    }
    const std::size_t sourceStride{static_cast<std::size_t>(width) * 4U};
    const auto destination = static_cast<std::uint8_t*>(mapped.pData); // NOLINT(gammaray-raw-pointer-boundary): transient mapped D3D memory
    for (int row{}; row < height; ++row) {
        std::memcpy(destination + static_cast<std::size_t>(row) * mapped.RowPitch, bgra.data() + static_cast<std::size_t>(row) * sourceStride,
                    sourceStride);
    }
    impl_->deviceContext->Unmap(impl_->videoTexture.Get(), 0);
    return true;
}

std::uint64_t D3d11Renderer::VideoTextureId() const noexcept {
    return reinterpret_cast<std::uint64_t>(impl_->videoTextureView.Get());
}

void D3d11Renderer::Render() const {
    constexpr std::array clearColor{0.055F, 0.067F, 0.094F, 1.0F};
    impl_->deviceContext->OMSetRenderTargets(1, impl_->renderTarget.GetAddressOf(), nullptr);
    impl_->deviceContext->ClearRenderTargetView(impl_->renderTarget.Get(), clearColor.data());
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    impl_->swapChain->Present(1, 0);
}

} // namespace px::desktop
