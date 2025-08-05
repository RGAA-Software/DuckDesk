#include "framework.h"

#include "hook_dxgi.h"

#include "capture_dxgi.h"
#include "capture_texture.h"

#include "hk_utils/memory.h"
#include "hk_utils/time_measure.hpp"

#include "tc_common_new/log.h"

using namespace tc;
using namespace tc;

namespace
{
    CaptureDxgi capture;
}  // namespace

IDXGISWAPCHAIN_PRESENT HookD3D11::IDXGISwapChain_Present_ = nullptr;
IDXGISWAPCHAIN_RESIZEBUFFERS HookD3D11::IDXGISwapChain_ResizeBuffers_ = nullptr;
IDXGISWAPCHAIN1_PRESENT1 HookD3D11::IDXGISwapChain1_Present1_ = nullptr;
bool HookD3D11::resize_buffers_called_ = false;

bool HookD3D11::Hook() noexcept {
    HMODULE dxgi_module = GetModuleHandle(_T("dxgi.dll"));
    if (nullptr == dxgi_module) {
        LOGE("HookD3D11 GetModuleHandle dxgi.dll failed.");
        return false;
    }

    HMODULE d3d11_module = GetModuleHandle(_T("d3d11.dll"));
    if (nullptr == d3d11_module) {
        LOGE("HookD3D11 GetModuleHandle d3d11.dll failed.");
        return false;
    }

    bool hooked_11 = HookD3D(d3d11_module);

    bool hooked_12 = false;
//    if (nullptr != GetModuleHandle(_T("d3d12.dll"))) {
//    	loaded_d3d11_module_ = LoadLibrary(_T("d3d11.dll"));
//    	if (nullptr != loaded_d3d11_module_) {
//    		hooked_12 = HookD3D(loaded_d3d11_module_);
//    	}
//    	LOGI("Hook d3d12_module " + std::to_string(hooked_12));
//    }

    return hooked_12 || hooked_11;
}

void HookD3D11::Unhook() noexcept {
    if (nullptr != loaded_d3d11_module_) {
        FreeLibrary(loaded_d3d11_module_);
        loaded_d3d11_module_ = nullptr;
    }
}

bool HookD3D11::HookD3D(HMODULE d3d11_module) noexcept {
    assert(nullptr != d3d11_module);

    const auto d3d11_create_device_and_swap_chain =
            reinterpret_cast<PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN>(GetProcAddress(d3d11_module, "D3D11CreateDeviceAndSwapChain"));
    if (nullptr == d3d11_create_device_and_swap_chain) {
        LOGE("No D3D11CreateDeviceAndSwapChain when hook.");
        return false;
    }

    HWND hwnd = CreateWindowExW(0, L"Static", L"d3d11 temporary window", WS_POPUP,
                                0, 0, 2, 2, nullptr, nullptr, nullptr, nullptr);
    if (nullptr == hwnd) {
        return false;
    }
    //BOOST_SCOPE_EXIT_ALL(&hwnd){DestroyWindow(hwnd);};

    DXGI_SWAP_CHAIN_DESC desc = {};
    desc.BufferCount = 2;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferDesc.Width = 2;
    desc.BufferDesc.Height = 2;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = hwnd;
    desc.SampleDesc.Count = 1;
    desc.Windowed = TRUE;

    CComPtr<IDXGISwapChain> swap_chain;
    CComPtr<ID3D11Device> device;
    D3D_FEATURE_LEVEL feature_levels[] = {
            D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL feature_level;
    HRESULT hr = d3d11_create_device_and_swap_chain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, feature_levels,
            _countof(feature_levels), D3D11_SDK_VERSION, &desc, &swap_chain, &device,
            &feature_level, nullptr);
    if (FAILED(hr)) {
        LOGI("d3d11_create_device_and_swap_chain failed !");
        return false;
    }

    // 8: swap_chain->Present
    IDXGISwapChain_Present_ = reinterpret_cast<IDXGISWAPCHAIN_PRESENT>(tc::GetVTableFunctionAddress(swap_chain, 8));

    // 13: swap_chain->ResizeBuffers
    IDXGISwapChain_ResizeBuffers_ = reinterpret_cast<IDXGISWAPCHAIN_RESIZEBUFFERS>(tc::GetVTableFunctionAddress(
            swap_chain, 13));

    IDXGISwapChain1 *swap_chain1;
    hr = swap_chain->QueryInterface(IID_PPV_ARGS(&swap_chain1));
    if (SUCCEEDED(hr)) {
        // 22: swap_chain1->Present1
        IDXGISwapChain1_Present1_ = reinterpret_cast<IDXGISWAPCHAIN1_PRESENT1>(tc::GetVTableFunctionAddress(swap_chain1,
                                                                                                            22));
        swap_chain1->Release();
    }

    NTSTATUS status = tc::HookAllThread(hook_IDXGISwapChain_Present_, IDXGISwapChain_Present_, MyPresent);
    if (!NT_SUCCESS(status)) {
        LOGI("Hook IDXGISwapChain_Present error");
        return false;
    }

    status = tc::HookAllThread(hook_IDXGISwapChain_ResizeBuffers_, IDXGISwapChain_ResizeBuffers_, MyResizeBuffers);
    if (!NT_SUCCESS(status)) {
        LOGI("Hook IDXGISwapChain_ResizeBuffers error");
        return false;
    }

    if (nullptr != IDXGISwapChain1_Present1_) {
        status = tc::HookAllThread(hook_IDXGISwapChain1_Present1_, IDXGISwapChain1_Present1_, MyPresent1);
        if (!NT_SUCCESS(status)) {
            LOGI("Hook IDXGISwapChain1_Present1_ error");
            return false;
        }
    }

    return true;
}

HRESULT STDMETHODCALLTYPE HookD3D11::MyPresent(IDXGISwapChain *swap, UINT sync_interval, UINT flags) {
    if (resize_buffers_called_) {
        resize_buffers_called_ = false;
        capture.Reset();
        LOGI("HookD3D11::MyPresent Buffer Resized !");
    }

    if ((flags & DXGI_PRESENT_TEST) == 0) {
        if (nullptr == capture.GetSwapChain()) {
            capture.Setup(swap);
        } else if (swap != capture.GetSwapChain()) {
            capture.Free();
            capture.Setup(swap);
            ATLTRACE2(atlTraceUtil, 0, "%s: swap changed.\n", __func__);
        }

        if (capture.CanCapture()) {
            capture.Capture(swap);
        }
    }

    if (!g_capture_tex.IsPresentEnabled()) {
        flags |= DXGI_PRESENT_TEST;
    }
    return IDXGISwapChain_Present_(swap, sync_interval, flags);
}

HRESULT STDMETHODCALLTYPE
HookD3D11::MyResizeBuffers(IDXGISwapChain *This,
                           UINT BufferCount,
                           UINT Width,
                           UINT Height,
                           DXGI_FORMAT NewFormat,
                           UINT SwapChainFlags) {
    ATLTRACE2(atlTraceUtil, 0, "%s(0x%p, %u, %u, %u, %d, %u)\n", __func__, This,
              BufferCount, Width, Height, NewFormat, SwapChainFlags);

    capture.Free();

    HRESULT hr = IDXGISwapChain_ResizeBuffers_(This, BufferCount, Width, Height, NewFormat, SwapChainFlags);
    resize_buffers_called_ = true;
    return hr;
}

HRESULT STDMETHODCALLTYPE
HookD3D11::MyPresent1(IDXGISwapChain1 *swap,
                      UINT sync_interval,
                      UINT flags,
                      _In_ const DXGI_PRESENT_PARAMETERS *present_parameters) {
    if (resize_buffers_called_) {
        resize_buffers_called_ = false;
        capture.Reset();
        ATLTRACE2(atlTraceUtil, 0, "%s(0x%p, %u, %u, 0x%p)\n", __func__, swap,
                  sync_interval, flags, present_parameters);
    }

    if ((flags & DXGI_PRESENT_TEST) == 0) {
        if (nullptr == capture.GetSwapChain()) {
            capture.Setup(swap);
        } else if (swap != capture.GetSwapChain()) {
            capture.Free();
            capture.Setup(swap);
            ATLTRACE2(atlTraceUtil, 0, "%s: swap changed.\n", __func__);
        }

        if (capture.CanCapture()) {
            capture.Capture(swap);
        }
    }

    if (!g_capture_tex.IsPresentEnabled()) {
        flags |= DXGI_PRESENT_TEST;
    }
    return IDXGISwapChain1_Present1_(swap, sync_interval, flags, present_parameters);
}
