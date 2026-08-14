
#include "d3d_debug_helper.h"
#include <DirectXTex.h>
#include <sstream>
#include "d3d_render.h"
#include "tc_common_new/string_util.h"
#include "tc_common_new/log.h"

namespace tc {

    void PrintD3DTexture2DDesc(const std::string& name, const D3D11_TEXTURE2D_DESC& desc) {
        LOGI("{}-----------------ID3D11Texture2D Desc------------------", name);
        LOGI("Width : {} , Height : {}, Format : {}", desc.Width, desc.Height, (int)desc.Format);
        LOGI("MipLevels : {}, ArraySize : {}", desc.MipLevels, desc.ArraySize);
        LOGI("Usage : {}", (int)desc.Usage);
        LOGI("BindFlags : {}", desc.BindFlags);
        LOGI("CPUAccessFlags : {}", desc.CPUAccessFlags);
        LOGI("MiscFlags : {}", desc.MiscFlags);
        LOGI("SampleDesc : {}, {}", desc.SampleDesc.Count, desc.SampleDesc.Quality);
        LOGI("-----------------ID3D11Texture2D Desc------------------ END");
    }

    void PrintD3DTexture2DDesc(const std::string &name, ID3D11Texture2D *tex) {
        D3D11_TEXTURE2D_DESC desc;
        tex->GetDesc(&desc);
        PrintD3DTexture2DDesc(name, desc);
    }

    bool DebugOutDDS(ID3D11Texture2D* pResource, const std::string &name) {
        auto rhi = tc::D3DRender::BuildD3DRenderFromTexture(pResource);
        DirectX::ScratchImage image;
        static int i = 0;
        auto hr = DirectX::CaptureTexture(rhi->GetDevice(), rhi->GetContext(), pResource, image);
        std::stringstream oss;
        oss << "debug-" << name << "-" << ++i % 5 <<".dds";

        if (SUCCEEDED(hr)) {
            hr = DirectX::SaveToDDSFile(image.GetImages(),
                                        image.GetImageCount(),
                                        image.GetMetadata(),
                                        DirectX::DDS_FLAGS_NONE,
                                        StringUtil::ToWString(oss.str()).c_str());
            if (FAILED(hr)) {
                LOGE("Save DDSFile failed");
            }
            return true;
        } else {
            LOGE("failed save image:{}", hr);
            return false;
        }
    }

    bool D3D11Texture2DLockMutex(Microsoft::WRL::ComPtr<ID3D11Texture2D> texture2d) {
        HRESULT hRes;
        Microsoft::WRL::ComPtr<IDXGIKeyedMutex> key_mutex;
        hRes = texture2d.As<IDXGIKeyedMutex>(&key_mutex);
        if (FAILED(hRes))
        {
            printf("D3D11Texture2DReleaseMutex IDXGIKeyedMutex. error\n");
            return false;
        }
        hRes = key_mutex->AcquireSync(0,INFINITE);
        if (FAILED(hRes))
        {
            printf("D3D11Texture2DReleaseMutex AcquireSync failed.\n");
            return false;
        }
        return  true;
    }

    bool D3D11Texture2DReleaseMutex(Microsoft::WRL::ComPtr<ID3D11Texture2D> texture2d) {
        HRESULT hRes;
        Microsoft::WRL::ComPtr<IDXGIKeyedMutex> key_mutex;
        hRes = texture2d.As<IDXGIKeyedMutex>(&key_mutex);
        if(FAILED(hRes))
        {
            printf("D3D11Texture2DReleaseMutex IDXGIKeyedMutex. error\n");
            return false;
        }
        hRes = key_mutex->ReleaseSync(0);
        if(FAILED(hRes))
        {
            printf("D3D11Texture2DReleaseMutex ReleaseSync failed.\n");
            return false;
        }
        return true;
    }


    bool CopyID3D11Texture2D(Microsoft::WRL::ComPtr<ID3D11Texture2D> shared_texture) {
        if(!D3D11Texture2DLockMutex(shared_texture)) {
            printf("D3D11Texture2DLockMutex error\n");
            return false;
        }
        std::shared_ptr<void> auto_realse_texture2D_mutex((void*)nullptr, [=](void* temp){
            D3D11Texture2DReleaseMutex(shared_texture);
        });

        HRESULT hRes;
        D3D11_TEXTURE2D_DESC desc;
        shared_texture->GetDesc(&desc);

        Microsoft::WRL::ComPtr<ID3D11Device> curDevice;
        shared_texture->GetDevice(&curDevice);
        static Microsoft::WRL::ComPtr<ID3D11Texture2D> texture2d;
        if (texture2d)
        {
            Microsoft::WRL::ComPtr<ID3D11Device> sharedTextureDevice;
            texture2d->GetDevice(&sharedTextureDevice);
            if (sharedTextureDevice != curDevice) {
                texture2d = nullptr;
            }
            if (texture2d)
            {
                D3D11_TEXTURE2D_DESC sharedTextureDesc;
                texture2d->GetDesc(&sharedTextureDesc);
                if (desc.Width != sharedTextureDesc.Width ||
                    desc.Height != sharedTextureDesc.Height ||
                    desc.Format != sharedTextureDesc.Format)
                {
                    texture2d = nullptr;
                }
            }
        }

        if (!texture2d)
        {
            D3D11_TEXTURE2D_DESC createDesc;
            ZeroMemory(&createDesc, sizeof(createDesc));
            createDesc.Format = desc.Format;
            createDesc.Width = desc.Width;
            createDesc.Height = desc.Height;
            createDesc.MipLevels = 1;
            createDesc.ArraySize = 1;
            createDesc.SampleDesc.Count = 1;
            createDesc.Usage = D3D11_USAGE_DEFAULT;
            createDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            hRes = curDevice->CreateTexture2D(&createDesc, NULL, texture2d.GetAddressOf());
            if (FAILED(hRes))
            {
                printf("desktop capture create texture failed with:%s",  StringUtil::GetErrorStr(hRes).c_str());
                return false;
            }
        }
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> ctx;
        curDevice->GetImmediateContext(&ctx);
        ctx->CopyResource(texture2d.Get(), shared_texture.Get());

        DebugOutDDS(texture2d.Get(), "debug");

        return true;
    }
}

