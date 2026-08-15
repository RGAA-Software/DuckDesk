#pragma once

#include <dxgi1_4.h>

#include "easyhook/easyhook.h"

#include "hk_utils/hook_api.hpp"

typedef HRESULT(STDMETHODCALLTYPE *IDXGISWAPCHAIN_PRESENT)(
        IDXGISwapChain *This,
        /* [in] */ UINT SyncInterval,
        /* [in] */ UINT PresentFlags);

typedef HRESULT(STDMETHODCALLTYPE *IDXGISWAPCHAIN_RESIZEBUFFERS)(
        IDXGISwapChain *This,
        /* [in] */ UINT BufferCount,
        /* [in] */ UINT Width,
        /* [in] */ UINT Height,
        /* [in] */ DXGI_FORMAT NewFormat,
        /* [in] */ UINT SwapChainFlags);

typedef HRESULT(STDMETHODCALLTYPE *IDXGISWAPCHAIN1_PRESENT1)(
        IDXGISwapChain1 *This,
        /* [in] */ UINT SyncInterval,
        /* [in] */ UINT PresentFlags,
        /* [annotation][in] */
        _In_ const DXGI_PRESENT_PARAMETERS *pPresentParameters);

class HookD3D11 {
public:
    bool Hook() noexcept;

    void Unhook() noexcept;

private:
    bool HookD3D(HMODULE d3d11_module) noexcept;

private:
    static HRESULT STDMETHODCALLTYPE MyPresent(IDXGISwapChain *This,
                                               UINT SyncInterval,
                                               UINT Flags);

    static HRESULT STDMETHODCALLTYPE MyResizeBuffers(IDXGISwapChain *This,
                                                     UINT BufferCount,
                                                     UINT Width,
                                                     UINT Height,
                                                     DXGI_FORMAT NewFormat,
                                                     UINT SwapChainFlags);

    static HRESULT STDMETHODCALLTYPE
    MyPresent1(IDXGISwapChain1 *This,
               UINT SyncInterval,
               UINT Flags,
               _In_ const DXGI_PRESENT_PARAMETERS *pPresentParameters);

    static IDXGISWAPCHAIN_PRESENT IDXGISwapChain_Present_;
    static IDXGISWAPCHAIN_RESIZEBUFFERS IDXGISwapChain_ResizeBuffers_;
    static IDXGISWAPCHAIN1_PRESENT1 IDXGISwapChain1_Present1_;

    tc::HookApi hook_IDXGISwapChain_Present_;
    tc::HookApi hook_IDXGISwapChain_ResizeBuffers_;
    tc::HookApi hook_IDXGISwapChain1_Present1_;

    static bool resize_buffers_called_;
    HMODULE loaded_d3d11_module_ = nullptr;
};
