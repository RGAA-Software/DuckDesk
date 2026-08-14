//
// Created by RGAA on 2023-12-16.
//

#include <fstream>
#include "video_encoder.h"
#include "tc_common_new/string_util.h"
#include "tc_common_new/message_notifier.h"
#include "tc_encoder_new/encoder_messages.h"
#include "tc_common_new/log.h"
#include "frame_render/FrameRender.h"
#include "tc_common_new/data.h"
#include "tc_common_new/image.h"
#include "tc_common_new/thread.h"
#include "tc_common_new/time_util.h"
#include "libyuv/convert.h"

namespace tc
{

    VideoEncoder::VideoEncoder(const std::shared_ptr<MessageNotifier> &msg_notifier, const EncoderFeature &encoder_feature) {
        msg_notifier_ = msg_notifier;
        encoder_feature_ = encoder_feature;
        LOGI("adapter_uid_ = {}", encoder_feature_.adapter_uid_);
        ComPtr<IDXGIFactory1> factory1;
        ComPtr<IDXGIAdapter1> adapter;
        DXGI_ADAPTER_DESC desc;
        HRESULT res = NULL;
        bool found_adapter = false;
        int adapter_index = 0;
        res = CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void **>(factory1.GetAddressOf()));
        if (res != S_OK) {
            LOGE("CreateDXGIFactory1 failed");
            return;
        }
        while (true) {
            res = factory1->EnumAdapters1(adapter_index, adapter.GetAddressOf());
            if (res != S_OK) {
                LOGE("EnumAdapters1 index:{} failed\n", adapter_index);
                return;
            }
            D3D_FEATURE_LEVEL featureLevel;

            adapter->GetDesc(&desc);
            if (encoder_feature_.adapter_uid_ == desc.AdapterLuid.LowPart) {
                found_adapter = true;
                LOGI("Adapter Index:{} Name: {}", adapter_index, StringUtil::ToUTF8(desc.Description).c_str());
                LOGI("find adapter");
                break;
            }
            ++adapter_index;
        }

        if (!found_adapter) {
            LOGE("can not found adapter\n");
            return;
        }

        D3D_FEATURE_LEVEL featureLevel;
        res = D3D11CreateDevice(adapter.Get(),
                                D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                                D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                nullptr, 0, D3D11_SDK_VERSION,
                                &d3d11_device_, &featureLevel, &d3d11_device_context_);

        if (res != S_OK || !d3d11_device_) {
            LOGE("D3D11CreateDevice failed: {}", res);
        } else {
            LOGI("D3D11CreateDevice mDevice = {}", (void *) d3d11_device_.Get());
        }

        ListenMessages();
    }

    VideoEncoder::~VideoEncoder() = default;

    bool VideoEncoder::Initialize(const tc::EncoderConfig &config) {
        encoder_config_ = config;
        input_frame_width_ = config.width;
        input_frame_height_ = config.height;
        out_width_ = config.encode_width;
        out_height_ = config.encode_height;
        refresh_rate_ = config.fps;
        return true;
    }

    void VideoEncoder::InsertIDR() {
        insert_idr_ = true;
    }

    void VideoEncoder::RegisterEncodeCallback(EncoderCallback &&cbk) {
        this->encoder_callback_ = std::move(cbk);
    }

    void VideoEncoder::Encode(ID3D11Texture2D *tex2d, uint64_t frame_index) {
    }

    void VideoEncoder::Encode(const std::shared_ptr<Image> &i420_data, uint64_t frame_index) {
    }

    void VideoEncoder::Exit() {
    }

//    bool VideoEncoder::D3D11Texture2DLockMutex(ComPtr<ID3D11Texture2D> texture2d) {
//        HRESULT hres;
//        ComPtr<IDXGIKeyedMutex> key_mutex;
//        hres = texture2d.As<IDXGIKeyedMutex>(&key_mutex);
//        if (FAILED(hres)) {
//            printf("D3D11Texture2DReleaseMutex IDXGIKeyedMutex. error\n");
//            return false;
//        }
//        hres = key_mutex->AcquireSync(0, INFINITE);
//        if (FAILED(hres)) {
//            printf("D3D11Texture2DReleaseMutex AcquireSync failed.\n");
//            return false;
//        }
//        return true;
//    }
//
//    bool VideoEncoder::D3D11Texture2DReleaseMutex(ComPtr<ID3D11Texture2D> texture2d) {
//        HRESULT hres;
//        ComPtr<IDXGIKeyedMutex> key_mutex;
//        hres = texture2d.As<IDXGIKeyedMutex>(&key_mutex);
//        if (FAILED(hres)) {
//            printf("D3D11Texture2DReleaseMutex IDXGIKeyedMutex. error\n");
//            return false;
//        }
//        hres = key_mutex->ReleaseSync(0);
//        if (FAILED(hres)) {
//            printf("D3D11Texture2DReleaseMutex ReleaseSync failed.\n");
//            return false;
//        }
//        return true;
//    }
//
//    bool VideoEncoder::CopyID3D11Texture2D(ComPtr<ID3D11Texture2D> shared_texture) {
//        if (!D3D11Texture2DLockMutex(shared_texture)) {
//            LOGE("D3D11Texture2DLockMutex error");
//            return false;
//        }
//        std::shared_ptr<void> auto_release_texture2D_mutex((void *) nullptr, [=, this](void *temp) {
//            D3D11Texture2DReleaseMutex(shared_texture);
//        });
//
//        HRESULT hres;
//        D3D11_TEXTURE2D_DESC desc;
//        shared_texture->GetDesc(&desc);
//
//        ComPtr<ID3D11Device> curDevice;
//        shared_texture->GetDevice(&curDevice);
//
//        if (texture2d_) {
//            ComPtr<ID3D11Device> sharedTextureDevice;
//            texture2d_->GetDevice(&sharedTextureDevice);
//            if (sharedTextureDevice != curDevice) {
//                texture2d_ = nullptr;
//            }
//            if (texture2d_) {
//                D3D11_TEXTURE2D_DESC sharedTextureDesc;
//                texture2d_->GetDesc(&sharedTextureDesc);
//                if (desc.Width != sharedTextureDesc.Width ||
//                    desc.Height != sharedTextureDesc.Height ||
//                    desc.Format != sharedTextureDesc.Format) {
//                    texture2d_ = nullptr;
//                }
//            }
//        }
//
//        if (!texture2d_) {
//            D3D11_TEXTURE2D_DESC createDesc;
//            ZeroMemory(&createDesc, sizeof(createDesc));
//            createDesc.Format = desc.Format;
//            createDesc.Width = desc.Width;
//            createDesc.Height = desc.Height;
//            createDesc.MipLevels = 1;
//            createDesc.ArraySize = 1;
//            createDesc.SampleDesc.Count = 1;
//            //createDesc.Usage = D3D11_USAGE_DEFAULT;
//            //createDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
//            createDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
//            createDesc.Usage = D3D11_USAGE_STAGING;
//            hres = curDevice->CreateTexture2D(&createDesc, NULL, texture2d_.GetAddressOf());
//            if (FAILED(hres)) {
//                LOGE("desktop capture create texture failed with:{}", StringUtil::GetErrorStr(hres).c_str());
//                return false;
//            }
//        }
//        ComPtr<ID3D11DeviceContext> ctx;
//        curDevice->GetImmediateContext(&ctx);
//        ctx->CopyResource(texture2d_.Get(), shared_texture.Get());
//
//        return true;
//    }
//
//    ComPtr<ID3D11Texture2D> VideoEncoder::OpenSharedTexture(HANDLE handle) {
//        ComPtr<ID3D11Texture2D> sharedTexture;
//        HRESULT hres;
//        hres = d3d11_device_->OpenSharedResource(handle, IID_PPV_ARGS(sharedTexture.GetAddressOf()));
//        if (FAILED(hres)) {
//            LOGE("OpenSharedResource failed: {}", hres);
//            return nullptr;
//        }
//        return sharedTexture;
//    }

    void VideoEncoder::ListenMessages() {
        if (msg_notifier_) {
            msg_listener_ = msg_notifier_->CreateListener();
            msg_listener_->Listen<MsgInsertIDR>([=](const auto &msg) {
                this->InsertIDR();
            });
        }
    }

}