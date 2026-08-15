#include "framework.h"
#include "capture_dxgi_d3d11.h"
#include "capture_texture.h"
#include "d3d_utils.h"
#include "hk_utils/time_measure.hpp"
#include "shared_texture.h"

#include <libyuv.h>

#include <d3d11.h>
#include <fstream>

#include "capture_message.h"
#include "px_common_new/data.h"
#include "px_common_new/log.h"
#include "client_manager.h"

using namespace px;

static std::shared_ptr<px::SharedTexture> shared_texture = std::make_shared<px::SharedTexture>();

namespace px_capture_d3d11
{

    namespace
    {
        bool initialized_ = false;
        ID3D11Device *device_ = nullptr;
        ID3D11DeviceContext *context_ = nullptr;

        // feature_level >= D3D_FEATURE_LEVEL_11_1
        //PFN_D3D11_CREATE_DEVICE D3D11CreateDevice_;
        //ID3D11Device* device11_ = nullptr;
        //ID3D11DeviceContext* context11_ = nullptr;

        //UINT width_ = 0;
        //UINT height_ = 0;
        //HWND window_ = nullptr;

        //HRESULT CreateD3D11DeviceOnLevel11_1() {
        //if (nullptr == D3D11CreateDevice_) {
        //	D3D11CreateDevice_ = reinterpret_cast<PFN_D3D11_CREATE_DEVICE>(GetProcAddress(GetModuleHandle(_T("d3d11.dll")), "D3D11CreateDevice"));
        //	if (nullptr == D3D11CreateDevice_) {
        //		ATLTRACE2(atlTraceException, 0,
        //			"!GetProcAddress(D3D11CreateDevice), #%d\n", GetLastError());
        //		return E_NOINTERFACE;
        //	}
        //	ATLTRACE2(atlTraceUtil, 0, "D3D11CreateDevice = 0x%p\n",
        //		D3D11CreateDevice_);
        //}

        //D3D_DRIVER_TYPE driver_types[]{
        //	D3D_DRIVER_TYPE_HARDWARE,
        //	// D3D_DRIVER_TYPE_WARP,
        //	// D3D_DRIVER_TYPE_REFERENCE,
        //};
        //
        //D3D_FEATURE_LEVEL feature_level;
        //D3D_FEATURE_LEVEL feature_levels[]{
        //	D3D_FEATURE_LEVEL_11_1,
        //	D3D_FEATURE_LEVEL_12_0,
        //	D3D_FEATURE_LEVEL_12_1,
        //};

        //HRESULT hr;
        //for (int driver_type_index = 0; driver_type_index < ARRAYSIZE(driver_types);
        //	driver_type_index++) {
        //	hr = D3D11CreateDevice_(NULL, driver_types[driver_type_index], NULL, 0,
        //		feature_levels, ARRAYSIZE(feature_levels),
        //		D3D11_SDK_VERSION, &device11_, &feature_level,
        //		&context11_);
        //	if (SUCCEEDED(hr)) {
        //		ATLTRACE2(atlTraceUtil, 0, "D3D11CreateDevice() driver type %d\n",
        //			driver_types[driver_type_index]);
        //		break;
        //	}
        //}

        //if (FAILED(hr)) {
        //	ATLTRACE2(atlTraceException, 0, "!D3D11CreateDevice(), #0x%08X\n", hr);
        //	return hr;
        //}

        //	return S_OK;
        //}

        HRESULT Initialize(IDXGISwapChain *swap) {
            HRESULT hr = swap->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void **>(&device_));
            if (FAILED(hr)) {
                ATLTRACE2(atlTraceException, 0, "%s: Failed to get device from swap, #0x%08X\n", __func__, hr);
                return hr;
            }

            device_->GetImmediateContext(&context_);
            ULONG ref = context_->Release();
            LOGI(" // ID3D11DeviceContext::Release() = {}", ref);
            ref = device_->Release();
            LOGI(" // ID3D11Device::Release() = {}", ref);

            //DXGI_SWAP_CHAIN_DESC desc;
            //hr = swap->GetDesc(&desc);
            //if (FAILED(hr)) {
            //	ATLTRACE2(atlTraceException, 0,
            //		"%s: Failed to get desc from swap, #0x%08X\n", __func__, hr);
            //	return hr;
            //}
            //window_ = desc.OutputWindow;

            //hr = CreateD3D11DeviceOnLevel11_1();
            //if (FAILED(hr)) {
            //	ATLTRACE2(atlTraceException, 0,
            //		"%s: Failed to CreateD3D11DeviceOnLevel11_1, #0x%08X\n", __func__,
            //		hr);
            //	return hr;
            //}
            return hr;
        }
    }  // namespace

    static uint64_t g_frame_index = 0;

    void Capture(void *swap, void* back_buffer) {
        bool should_update = false;
        if (!initialized_) {
            HRESULT hr = Initialize(static_cast<IDXGISwapChain *>(swap));
            if (FAILED(hr)) {
                return;
            }
            initialized_ = true;
            should_update = true;
        }

        auto dxgi_backbuffer = static_cast<IDXGIResource *>(back_buffer);
        CComQIPtr<ID3D11Texture2D> acquired_texture(dxgi_backbuffer);
        if (!acquired_texture) {
            ATLTRACE2(atlTraceException, 0, "!QueryInterface(ID3D11Texture2D)\n");
            return;
        }

        LARGE_INTEGER tick;
        QueryPerformanceCounter(&tick);

        D3D11_TEXTURE2D_DESC desc;
        CComPtr<ID3D11Texture2D> new_texture;
        HRESULT hr = CaptureTexture(device_, context_, acquired_texture, desc, new_texture, shared_texture);
        if (FAILED(hr)) {
            LOGE("CaptureTexture failed!");
            return;
        }

        if (!shared_texture->texture_) {
            LOGE("Not have Texture....");
            return;
        }

        // EasyHook legacy path: SHM frame IPC removed. Use OBS px_graphics + WS /ipc.
        static bool s_logged = false;
        if (!s_logged) {
            s_logged = true;
            LOGE("EasyHook capture_dxgi: SHM/ClientIpcManager removed; frames not sent. "
                 "Use OBS inject (px_graphics).");
        }
        g_frame_index++;
    }

    void FreeResource() {
        //if (nullptr != device11_) {
        //	device11_->Release();
        //	device11_ = nullptr;
        //}
        //if (nullptr != context11_) {
        //	context11_->Release();
        //	context11_ = nullptr;
        //}
    }

    void Free() {
        g_capture_tex.FreeSharedTexture();
        FreeResource();
        initialized_ = false;
    }

}  // namespace capture_d3d11
