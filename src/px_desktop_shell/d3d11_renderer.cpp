#include "d3d11_renderer.h"

#include "window_host.h"

#include <SDL3/SDL.h>
#include <backends/imgui_impl_dx11.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <memory>
#include <utility>

namespace px::desktop {

struct D3d11Renderer::Impl final {
    Microsoft::WRL::ComPtr<ID3D11Device> device{};
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> deviceContext{};
    Microsoft::WRL::ComPtr<IDXGISwapChain> swapChain{};
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> renderTarget{};
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

void D3d11Renderer::Render() const {
    constexpr std::array clearColor{0.055F, 0.067F, 0.094F, 1.0F};
    impl_->deviceContext->OMSetRenderTargets(1, impl_->renderTarget.GetAddressOf(), nullptr);
    impl_->deviceContext->ClearRenderTargetView(impl_->renderTarget.Get(), clearColor.data());
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    impl_->swapChain->Present(1, 0);
}

} // namespace px::desktop
