#include <Windows.h>
#include <atlbase.h>
#include <d3d11.h>
#include <array>
#include <chrono>
#include <cmath>
#include <memory>
#include <type_traits>

namespace {
struct WindowCloser final {
    void operator()(HWND window) const noexcept { // NOLINT(gammaray-raw-pointer-boundary) Owned Win32 window ABI.
        if (window) {
            DestroyWindow(window);
        }
    }
};
using Window = std::unique_ptr<std::remove_pointer_t<HWND>, WindowCloser>;
} // namespace

// Godot 4 does not provide a D3D11 renderer. This bounded fixture exercises a real hardware D3D11 swapchain instead.
int main() {
    const Window window{CreateWindowExW(0, L"STATIC", L"Pixels D3D11 capture fixture", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
                                        1280, 720, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr)};
    if (!window) {
        return 1;
    }
    DXGI_SWAP_CHAIN_DESC description{};
    description.BufferDesc.Width = 1280;
    description.BufferDesc.Height = 720;
    description.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.BufferCount = 2;
    description.OutputWindow = window.get();
    description.Windowed = TRUE;
    description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    CComPtr<ID3D11Device> device{};
    CComPtr<ID3D11DeviceContext> context{};
    CComPtr<IDXGISwapChain> swapchain{};
    D3D_FEATURE_LEVEL level{};
    if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &description, &swapchain,
                                             &device, &level, &context))) {
        return 2;
    }
    CComPtr<ID3D11Texture2D> backbuffer{};
    CComPtr<ID3D11RenderTargetView> target{};
    if (FAILED(swapchain->GetBuffer(0, IID_PPV_ARGS(&backbuffer))) || FAILED(device->CreateRenderTargetView(backbuffer, nullptr, &target))) {
        return 3;
    }
    const auto started = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - started < std::chrono::seconds(120)) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (!IsWindow(window.get())) {
            return 0;
        }
        const auto seconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - started).count();
        const std::array<float, 4> color{0.5f + 0.4f * std::sin(seconds), 0.25f, 0.5f + 0.4f * std::cos(seconds), 1.0f};
        context->ClearRenderTargetView(target, color.data());
        if (FAILED(swapchain->Present(1, 0))) {
            return 4;
        }
    }
    return 0;
}
