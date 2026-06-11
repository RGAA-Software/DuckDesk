#include "framework.h"
#include "capture_dxgi_d3d11.h"
#include "capture_texture.h"
#include "d3d_utils.h"
#include "hk_utils/time_measure.hpp"
#include "shared_texture.h"

#include <libyuv.h>

#include <d3d11.h>
#include <fstream>

#include "client_ipc_manager.h"
#include "capture_message.h"
#include "tc_common_new/data.h"
#include "tc_common_new/log.h"
#include "client_manager.h"

using namespace tc;

static std::shared_ptr<tc::SharedTexture> shared_texture = std::make_shared<tc::SharedTexture>();

namespace tc_capture_d3d11
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

        auto client_manager = ClientManager::Instance();
        auto send_video_frame_by_handle = !client_manager->GetInjectParams()->send_video_frame_by_shm;
        if (send_video_frame_by_handle){
            // 暂时在这里获取 adapter_uid， 不过获取的太频繁了
            auto adapter_uid = tc::GetAdapterUid(device_);
            CaptureVideoFrame capture_video_frame_msg{};
            capture_video_frame_msg.type_ = kCaptureVideoFrame;
            capture_video_frame_msg.capture_type_ = kCaptureVideoByHandle;
            capture_video_frame_msg.data_length = 0;
            capture_video_frame_msg.frame_width_ = desc.Width;
            capture_video_frame_msg.frame_height_ = desc.Height;
            capture_video_frame_msg.frame_index_ = g_frame_index;
            capture_video_frame_msg.handle_ = shared_texture->GetSharedHandle();;
            capture_video_frame_msg.frame_format_ = desc.Format;
            if(adapter_uid.has_value()) {
                capture_video_frame_msg.adapter_uid_ = adapter_uid.value();
            }
            //capture_video_frame_msg.adapter_uid_ = 78007;
            auto data = Data::Make(nullptr, sizeof(CaptureVideoFrame));
            memcpy(data->DataAddr(), &capture_video_frame_msg, sizeof(CaptureVideoFrame));
            ClientIpcManager::Instance()->Send(data);
            LOGI("by handle, Send with handle : {} ", g_frame_index);
        }
        else {
            D3D11_TEXTURE2D_DESC desc;
            shared_texture->texture_->GetDesc(&desc);

            auto width = desc.Width;
            auto height = desc.Height;
            std::string msg = " --> width : " + std::to_string(width) + " height : " + std::to_string(height);
            LOGI(msg);

            std::vector<uint8_t> yuv_frame_data_;
            yuv_frame_data_.resize(1.5 * width * height);
            size_t pixel_size = width * height;

            const int uv_stride = width >> 1;
            uint8_t* y = yuv_frame_data_.data();
            uint8_t* u = y + pixel_size;
            uint8_t* v = u + (pixel_size >> 2);

            CComPtr<IDXGISurface> staging_surface = nullptr;
            hr = shared_texture->texture_->QueryInterface(IID_PPV_ARGS(&staging_surface));
            if (FAILED(hr)) {
                LOGE("!QueryInterface(IDXGISurface) err");
                return;
            }
            DXGI_MAPPED_RECT mapped_rect{};
            hr = staging_surface->Map(&mapped_rect, DXGI_MAP_READ);
            if (FAILED(hr)) {
                LOGE("!Map(IDXGISurface)");
                return;
            }

            //LOGI("the format is : " + std::to_string(desc.Format));

//            {
//                std::ofstream rgba_file("capture_yuv_tex.rgba", std::ios::binary);
//                rgba_file.write((char*)mapped_rect.pBits, width * height * 4);
//                rgba_file.close();
//            }

            if (DXGI_FORMAT_B8G8R8A8_UNORM == desc.Format) {
                libyuv::ARGBToI420(mapped_rect.pBits, mapped_rect.Pitch, y, width, u, uv_stride, v, uv_stride, width, height);
            }
            else if (DXGI_FORMAT_R8G8B8A8_UNORM == desc.Format) {
                libyuv::ABGRToI420(mapped_rect.pBits, mapped_rect.Pitch, y, width, u, uv_stride, v, uv_stride, width, height);
            }
            else {
                libyuv::ARGBToI420(mapped_rect.pBits, mapped_rect.Pitch, y, width, u, uv_stride, v, uv_stride, width, height);
            }

            LOGI("by shm, desc.Format: " + std::to_string(desc.Format) + " the yuv size : " + std::to_string(yuv_frame_data_.size()));
//            std::ofstream yuv_file("capture_yuv_tex.yuv", std::ios::binary);
//            yuv_file.write((char*)yuv_frame_data_.data(), width * height * 1.5);
//            yuv_file.close();

            CaptureVideoFrame capture_video_frame_msg{};
            capture_video_frame_msg.type_ = kCaptureVideoFrame;
            capture_video_frame_msg.data_length = yuv_frame_data_.size();
            capture_video_frame_msg.capture_type_ = kCaptureVideoBySharedMemory;
            capture_video_frame_msg.frame_width_ = width;
            capture_video_frame_msg.frame_height_ = height;
            capture_video_frame_msg.frame_index_ = g_frame_index;
            capture_video_frame_msg.handle_ = 0;
            auto data = Data::Make(nullptr, sizeof(CaptureVideoFrame) + yuv_frame_data_.size());
            memcpy(data->DataAddr(), &capture_video_frame_msg, sizeof(CaptureVideoFrame));
            memcpy(data->DataAddr() + sizeof(CaptureVideoFrame), yuv_frame_data_.data(), yuv_frame_data_.size());
            ClientIpcManager::Instance()->Send(data);
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
