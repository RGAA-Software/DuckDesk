#include "framework.h"
#include "capture_dxgi_d3d11on12.h"
#include <d3d11on12.h>
#include "capture_texture.h"
#include "d3d_utils.h"
#include "tc_common_new/log.h"
#include "hk_utils/time_measure.hpp"
#include "shared_texture.h"

static std::shared_ptr<tc::SharedTexture> shared_texture = std::make_shared<tc::SharedTexture>();

namespace tc_capture_d3d11on12
{

    constexpr size_t kMaxBackbuffers = 8;

    bool initialized_ = false;
    bool is_dxgi_1_4_ = false;

    ID3D12Device *device_ = nullptr;
    PFN_D3D11ON12_CREATE_DEVICE D3D11On12CreateDevice_ = nullptr;

    ID3D11Device *device11_ = nullptr;
    ID3D11DeviceContext *context11_ = nullptr;
    ID3D11On12Device *device11on12_ = nullptr;

    ID3D11Resource *backbuffer11_[kMaxBackbuffers]{};
    size_t backbuffer_count_;
    size_t current_backbuffer_;

    DXGI_FORMAT format_;
    UINT width_ = 0;
    UINT height_ = 0;
    bool multisampled_ = false;
    HWND window_ = nullptr;

    HRESULT InitFormat(IDXGISwapChain *swap) {
        CComPtr<IDXGISwapChain3> swap3;
        HRESULT hr = swap->QueryInterface(IID_PPV_ARGS(&swap3));
        if (SUCCEEDED(hr)) {
            is_dxgi_1_4_ = true;
            swap3.Release();
        } else {
            is_dxgi_1_4_ = false;
        }
        ATLTRACE2(atlTraceUtil, 0, "Is DXGI 1.4? %d\n", is_dxgi_1_4_);

        DXGI_SWAP_CHAIN_DESC desc;
        hr = swap->GetDesc(&desc);
        if (FAILED(hr)) {
            LOGI("!GetDesc(), {}", hr);
            return hr;
        }

        format_ = desc.BufferDesc.Format;
        width_ = desc.BufferDesc.Width;
        height_ = desc.BufferDesc.Height;
        multisampled_ = desc.SampleDesc.Count > 1;
        window_ = desc.OutputWindow;
        LOGI("Format {}, {} * {}, Count {}, OutputWindow {}, Windowed {}",
             (int) format_, width_, height_, desc.SampleDesc.Count, (uint64_t) window_, desc.Windowed);

        backbuffer_count_ =
                desc.SwapEffect == DXGI_SWAP_EFFECT_DISCARD ? 1 : desc.BufferCount;
        if (1 == backbuffer_count_) {
            is_dxgi_1_4_ = false;
        }

        if (kMaxBackbuffers < backbuffer_count_) {
            LOGI("BackbufferCount = {}", backbuffer_count_);
            backbuffer_count_ = 1;
            is_dxgi_1_4_ = false;
        }

        ID3D12Resource *backbuffer12[kMaxBackbuffers]{};
        D3D11_RESOURCE_FLAGS resource_flags = {};
        for (UINT i = 0; i < backbuffer_count_; ++i) {
            hr = swap->GetBuffer(i, IID_PPV_ARGS(&backbuffer12[i]));
            if (FAILED(hr)) {
                LOGI("!GetBuffer(), {}", hr);
                return hr;
            }
            hr = device11on12_->CreateWrappedResource(
                    backbuffer12[i], &resource_flags, D3D12_RESOURCE_STATE_COPY_SOURCE,
                    D3D12_RESOURCE_STATE_PRESENT, IID_PPV_ARGS(&backbuffer11_[i]));
            backbuffer12[i]->Release();
            if (FAILED(hr)) {
                LOGI("!CreateWrappedResource(), {}", hr);
                return hr;
            }
            device11on12_->ReleaseWrappedResources(&backbuffer11_[i], 1);
        }
        LOGI("Init D3D12 format success.");
        return hr;
    }

    HRESULT Initialize(IDXGISwapChain *swap) {
        HRESULT hr = swap->GetDevice(__uuidof(ID3D12Device),
                                     reinterpret_cast<void **>(&device_));
        if (FAILED(hr)) {
            LOGI("%s: Failed to get device from swap, {}", __func__, hr);
            return hr;
        }
        device_->Release();

        if (nullptr == D3D11On12CreateDevice_) {
            D3D11On12CreateDevice_ =
                    reinterpret_cast<PFN_D3D11ON12_CREATE_DEVICE>(GetProcAddress(
                            GetModuleHandle(_T("d3d11.dll")), "D3D11On12CreateDevice"));
            if (nullptr == D3D11On12CreateDevice_) {
                LOGI("!GetProcAddress(D3D11On12CreateDevice), #%d\n", GetLastError());
                return E_NOINTERFACE;
            }
            LOGI("D3D11On12CreateDevice = {}", (void *) D3D11On12CreateDevice_);
        }
        int creationFlags = 0;
        creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
        hr = D3D11On12CreateDevice_(device_, creationFlags, nullptr, 0, nullptr, 0, 0, &device11_, &context11_,
                                    nullptr);
        if (FAILED(hr)) {
            LOGI("!D3D11On12CreateDevice(), #0x%08X\n", hr);
            return hr;
        }

        hr = device11_->QueryInterface(IID_PPV_ARGS(&device11on12_));
        if (FAILED(hr)) {
            LOGI("!QueryInterface(ID3D11On12Device), #0x%08X\n", hr);
            return hr;
        }

        hr = InitFormat(swap);
        return hr;
    }

    void FreeResource() {
        for (size_t i = 0; i < backbuffer_count_; ++i) {
            if (nullptr != backbuffer11_[i]) {
                backbuffer11_[i]->Release();
            }
        }
        backbuffer_count_ = 0;

        if (nullptr != device11_) {
            device11_->Release();
            device11_ = nullptr;
        }
        if (nullptr != context11_) {
            context11_->Release();
            context11_ = nullptr;
        }
        if (nullptr != device11on12_) {
            device11on12_->Release();
            device11on12_ = nullptr;
        }
    }

    static uint64_t g_frame_index = 0;

    void Capture(void *swap, void*) {
        bool should_update = false;
        if (!initialized_) {
            HRESULT hr = Initialize(static_cast<IDXGISwapChain *>(swap));
            if (FAILED(hr)) {
                return;
            }
            initialized_ = true;
            should_update = true;
        }

        LARGE_INTEGER tick;
        QueryPerformanceCounter(&tick);

        size_t index = current_backbuffer_;
        if (is_dxgi_1_4_) {
            auto swap3 = reinterpret_cast<IDXGISwapChain3*>(swap);
            index = swap3->GetCurrentBackBufferIndex();
            if (++current_backbuffer_ >= backbuffer_count_) {
                index = 0;
            }
        }

        ID3D11Resource* back_buffer = backbuffer11_[index];
        device11on12_->AcquireWrappedResources(&back_buffer, 1);

        D3D11_TEXTURE2D_DESC desc;
        CComPtr<ID3D11Texture2D> new_texture;
        HRESULT hr = CaptureTexture(device11_, context11_, back_buffer, desc, new_texture, shared_texture);
        //LOGI("After capture Texture in d3d12.");
        device11on12_->ReleaseWrappedResources(&back_buffer, 1);
        context11_->Flush();
        if (FAILED(hr)) {
            LOGE("!CaptureTexture(), {}", (uint64_t )hr);
            return;
        }

        uint64_t handle = shared_texture->GetSharedHandle();
        LOGI("FROM D3D12 : shared handle : {}, format : {}, frame_index: {}", (uint64_t) handle, (int) desc.Format, g_frame_index);
//		auto ipc_message = IPCFrameMessage::MakeEmptyMessage();
//		ipc_message->type = IPCMessageType::kSharedTextureHandle;
//		ipc_message->sender = IPCMessageSender::kSenderClient;
//		ipc_message->handle = handle;
//		ipc_message->format = desc.Format;
//		ipc_message->width = desc.Width;
//		ipc_message->height = desc.Height;
//		ipc_message->frame_index = g_frame_index++;
//		InterCommClient::Instance()->SendBack(IPCFrameMessage::ConvertToData(ipc_message));
        //if (should_update) {
        //	SharedVideoFrameInfo* svfi = g_capture_tex.GetSharedVideoFrameInfo();
        //	svfi->timestamp = tick.QuadPart;
        //	svfi->type = VideoFrameType::kTexture;
        //	svfi->width = desc.Width;
        //	svfi->height = desc.Height;
        //	svfi->format = desc.Format;
        //	svfi->window = reinterpret_cast<std::uint64_t>(window_);
        //}

        //VideoFrameStats stats = {};
        //stats.timestamp = tick.QuadPart;
        //stats.elapsed.preprocess = tc::TimeMeasure::Delta(tick.QuadPart);

        //if (!g_capture_tex.CreateSharedVideoTextureFrames()) {
        //  return;
        //}

        //if (!g_capture_tex.ShareTexture(new_texture, device11_, context11_)) {
        //	return;
        //}
        //stats.elapsed.total = tc::TimeMeasure::Delta(stats.timestamp);
        //
        //auto frame = g_capture_tex.GetAvailablePackedVideoTextureFrame();
        //frame->stats.timestamp = stats.timestamp;
        //frame->stats.elapsed.preprocess = stats.elapsed.preprocess;
        //frame->stats.elapsed.total = stats.elapsed.total;
        //g_capture_tex.SetSharedFrameReadyEvent();
    }

    void Free() {
        FreeResource();
        initialized_ = false;
    }

}
