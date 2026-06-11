#include "shared_texture.h"
#include "tc_common_new/log.h"

namespace tc
{

    bool SharedTexture::CopyCapturedTexture(ID3D11Device *device, ID3D11DeviceContext *context, ID3D11Texture2D *src) {
        D3D11_TEXTURE2D_DESC in_desc;
        src->GetDesc(&in_desc);

        if (in_desc.Width != curr_desc_.Width || in_desc.Height != curr_desc_.Height ||
            in_desc.Format != curr_desc_.Format) {

            if (texture_) {
                texture_.Release();
                texture_ = nullptr;
            }

#if 0
            D3D11_TEXTURE2D_DESC desc = {};
            memset(&desc, 0, sizeof(D3D11_TEXTURE2D_DESC));
            desc.Width = in_desc.Width;
            desc.Height = in_desc.Height;
            desc.Format = in_desc.Format;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.SampleDesc.Quality = 0;
            desc.SampleDesc.Count = 1;
            //desc.Usage = D3D11_USAGE_STAGING;// ;D3D11_USAGE_DEFAULT;//
            //desc.BindFlags = 0;// D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;;// 0;
            //desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            //desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED;

            desc.BindFlags = 0;
            desc.MiscFlags &= D3D11_RESOURCE_MISC_TEXTURECUBE;
            desc.MiscFlags |= D3D11_RESOURCE_MISC_SHARED; //D3D11_RESOURCE_MISC_SHARED_NTHANDLE |
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;


            //desc.Usage = D3D11_USAGE_DEFAULT;
            // JUST TEST...
            desc.Usage = D3D11_USAGE_STAGING;

            LOGI("Create Texture : width : {}, height : {}, the old format is :{} , new format : {}",
                      desc.Width, desc.Height, (int)in_desc.Format, (int)desc.Format);

#endif

            D3D11_TEXTURE2D_DESC desc11 = {};
            desc11.Width = in_desc.Width;
            desc11.Height = in_desc.Height;
            desc11.Format = in_desc.Format;
            desc11.MipLevels = 1;
            desc11.ArraySize = 1;
            desc11.SampleDesc.Count = 1;
            desc11.BindFlags = D3D11_BIND_SHADER_RESOURCE;
//			desc11.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
            desc11.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
            desc11.Usage = D3D11_USAGE_DEFAULT;

            // JUST TEST...
//            desc11.BindFlags = 0;
//            desc11.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
//            desc11.Usage = D3D11_USAGE_STAGING;
//            desc11.SampleDesc.Quality = 0;

            auto hr = device->CreateTexture2D(&desc11, nullptr, &texture_);
            if (FAILED(hr)) {
                LOGE("create_d3d11_stage_surface: failed to create texture_ {} , {}", hr, GetLastError());
                return false;
            }

            this->curr_desc_ = in_desc;
            LOGI("Create D3DTexture2D success...");
        }

        //
        LockMutex();
        context->CopyResource(texture_, src);
        ReleaseMutex();

        return true;
    }

    uint64_t SharedTexture::GetSharedHandle() {
        if (!texture_) {
            LOGE("texture is null");
            return 0;
        }
        HANDLE tex_handle = nullptr;
        CComPtr<IDXGIResource> resource = nullptr;
        auto hr = texture_->QueryInterface(__uuidof(IDXGIResource), reinterpret_cast<void **>(&resource));
        if (SUCCEEDED(hr)) {
            hr = resource->GetSharedHandle(&tex_handle);
            if (FAILED(hr)) {
                LOGE("GetSharedHandle Failed !!!");
                return 0;
            }
        } else {
            LOGE("Query resource error : {0:x}", hr);
        }
        if (!tex_handle) {
            LOGE("texture handle is null: {}", (void *) tex_handle);
            return 0;
        }
        return (uint64_t) tex_handle;
    }

    bool SharedTexture::LockMutex() {
        if (!texture_) {
            LOGI("SharedTexture texture_ is null ");
            return false;
        }
        HRESULT hres;
        CComPtr<IDXGIKeyedMutex> key_mutex;
        hres = texture_.QueryInterface<IDXGIKeyedMutex>(&key_mutex);
        if (FAILED(hres)) {
            printf("D3D11Texture2DReleaseMutex IDXGIKeyedMutex. error\n");
            return false;
        }
        hres = key_mutex->AcquireSync(0, INFINITE);
        if (FAILED(hres)) {
            printf("D3D11Texture2DReleaseMutex AcquireSync failed.\n");
            return false;
        }
        return true;
    }

    bool SharedTexture::ReleaseMutex() {
        if (!texture_) {
            LOGI("SharedTexture texture_ is null");
            return false;
        }
        HRESULT hres;
        CComPtr<IDXGIKeyedMutex> key_mutex;
        if (FAILED(hres = texture_.QueryInterface<IDXGIKeyedMutex>(&key_mutex))) {
            printf("D3D11Texture2DReleaseMutex IDXGIKeyedMutex. error\n");
            return false;
        }
        hres = key_mutex->ReleaseSync(0);
        if (FAILED(hres)) {
            printf("D3D11Texture2DReleaseMutex ReleaseSync failed.\n");
            return false;
        }
        return true;
    }

} // namespace tc