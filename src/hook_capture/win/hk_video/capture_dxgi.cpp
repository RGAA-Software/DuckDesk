#include "framework.h"

#include "capture_dxgi.h"
#include "tc_common_new/log.h"

#include <d3d12.h>
#include <d3d11on12.h>

#include "capture_dxgi_d3d11.h"
#include "capture_dxgi_d3d11on12.h"

using namespace tc;

bool CaptureDxgi::Setup(IDXGISwapChain *swap) noexcept {
    CComPtr<ID3D11Device> d3d11;
    HRESULT hr11 = swap->GetDevice(IID_PPV_ARGS(&d3d11));
    if (SUCCEEDED(hr11)) {
        D3D_FEATURE_LEVEL level = d3d11->GetFeatureLevel();
        if (level >= D3D_FEATURE_LEVEL_11_0) {
            swap_ = swap;
            capture_ = tc_capture_d3d11::Capture;
            free_ = tc_capture_d3d11::Free;
            LOGI("Use D3D11 hooked.");
            return true;
        }
    }

    CComPtr<IUnknown> device;
    HRESULT hr = swap->GetDevice(__uuidof(ID3D10Device), reinterpret_cast<void **>(&device));
    if (SUCCEEDED(hr)) {
        swap_ = swap;
        ATLTRACE2(atlTraceUtil, 0, "%s: ID3D10Device\n", __func__);
        return true;
    }

    if (SUCCEEDED(hr11)) {
        swap_ = swap;
        capture_ = tc_capture_d3d11::Capture;
        free_ = tc_capture_d3d11::Free;
        LOGI("Use D3D11 hooked //");
        return true;
    }

    hr = swap->GetDevice(__uuidof(ID3D12Device), reinterpret_cast<void **>(&device));
    if (SUCCEEDED(hr)) {
        swap_ = swap;
        capture_ = tc_capture_d3d11on12::Capture;
        free_ = tc_capture_d3d11on12::Free;
        LOGI("{}: USE ID3D12Device", __func__);
        return true;
    }

    return false;
}
