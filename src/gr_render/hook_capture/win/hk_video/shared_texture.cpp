#include "shared_texture.h"
#include <dxgi1_2.h>
#include <d3dcompiler.h>
#include <cstring>
#include "tc_common_new/log.h"

namespace tc
{

    bool SharedTexture::IsDirectShareableFormat(DXGI_FORMAT format) {
        return format == DXGI_FORMAT_B8G8R8A8_UNORM
               || format == DXGI_FORMAT_B8G8R8X8_UNORM
               || format == DXGI_FORMAT_R8G8B8A8_UNORM;
    }

    bool SharedTexture::EnsureConvertShaders(ID3D11Device *device) {
        if (conv_vs_ && conv_ps_) {
            return true;
        }
        if (conv_shader_failed_) {
            return false;
        }
        // d3dcompiler_47.dll 是 Win10+ 系统组件,运行时加载避免新增链接依赖。
        HMODULE d3dcompiler = LoadLibraryA("d3dcompiler_47.dll");
        if (!d3dcompiler) {
            LOGE("SharedTexture: d3dcompiler_47.dll not found");
            conv_shader_failed_ = true;
            return false;
        }
        auto d3d_compile = (pD3DCompile) GetProcAddress(d3dcompiler, "D3DCompile");
        if (!d3d_compile) {
            LOGE("SharedTexture: D3DCompile not found");
            conv_shader_failed_ = true;
            return false;
        }
        // 全屏三角形:SV_VertexID 生成,无需 input layout;PS 按像素 Load 做格式转换。
        const char* hlsl = R"(
float4 VSMain(uint vid : SV_VertexID) : SV_Position {
    float2 p = float2((vid << 1) & 2, vid & 2);
    return float4(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, 0.0, 1.0);
}
Texture2D tex0 : register(t0);
float4 PSMain(float4 pos : SV_Position) : SV_Target {
    return tex0.Load(int3((int2)pos.xy, 0));
}
)";
        CComPtr<ID3DBlob> vs_blob, ps_blob, err_blob;
        HRESULT hr = d3d_compile(hlsl, strlen(hlsl), nullptr, nullptr, nullptr,
                                 "VSMain", "vs_4_0", 0, 0, &vs_blob, &err_blob);
        if (FAILED(hr)) {
            LOGE("SharedTexture: VS compile failed: {:x}", (uint32_t)hr);
            conv_shader_failed_ = true;
            return false;
        }
        hr = d3d_compile(hlsl, strlen(hlsl), nullptr, nullptr, nullptr,
                         "PSMain", "ps_4_0", 0, 0, &ps_blob, &err_blob);
        if (FAILED(hr)) {
            LOGE("SharedTexture: PS compile failed: {:x}", (uint32_t)hr);
            conv_shader_failed_ = true;
            return false;
        }
        hr = device->CreateVertexShader(vs_blob->GetBufferPointer(),
                                        vs_blob->GetBufferSize(), nullptr, &conv_vs_);
        if (FAILED(hr)) {
            LOGE("SharedTexture: CreateVertexShader failed: {:x}", (uint32_t)hr);
            conv_shader_failed_ = true;
            return false;
        }
        hr = device->CreatePixelShader(ps_blob->GetBufferPointer(),
                                       ps_blob->GetBufferSize(), nullptr, &conv_ps_);
        if (FAILED(hr)) {
            LOGE("SharedTexture: CreatePixelShader failed: {:x}", (uint32_t)hr);
            conv_shader_failed_ = true;
            return false;
        }
        LOGI("SharedTexture: convert shaders ready");
        return true;
    }

    bool SharedTexture::BlitConvertToBgra(ID3D11Device *device, ID3D11DeviceContext *context, ID3D11Texture2D *src) {
        CComPtr<ID3D11ShaderResourceView> srv;
        HRESULT hr = device->CreateShaderResourceView(src, nullptr, &srv);
        if (FAILED(hr)) {
            LOGE("SharedTexture: CreateShaderResourceView failed: {:x}", (uint32_t)hr);
            return false;
        }
        CComPtr<ID3D11RenderTargetView> rtv;
        hr = device->CreateRenderTargetView(texture_, nullptr, &rtv);
        if (FAILED(hr)) {
            LOGE("SharedTexture: CreateRenderTargetView failed: {:x}", (uint32_t)hr);
            return false;
        }
        D3D11_VIEWPORT vp = {0.0f, 0.0f, (FLOAT)curr_desc_.Width, (FLOAT)curr_desc_.Height, 0.0f, 1.0f};
        // 借用游戏的 immediate context 画画,必须显式重置继承来的状态:
        // 游戏(UE Slate/UMG 裁剪)常留着小的 scissor rect 或自定义 blend/depth 状态,
        // 不重置的话 Draw 会被裁剪成一个小块、或混入错误的混合结果。
        D3D11_RECT full_scissor = {0, 0, (LONG)curr_desc_.Width, (LONG)curr_desc_.Height};

        context->IASetInputLayout(nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(conv_vs_, nullptr, 0);
        context->PSSetShader(conv_ps_, nullptr, 0);
        context->HSSetShader(nullptr, nullptr, 0);
        context->DSSetShader(nullptr, nullptr, 0);
        context->GSSetShader(nullptr, nullptr, 0);
        ID3D11ShaderResourceView* srvs[] = {srv};
        context->PSSetShaderResources(0, 1, srvs);
        ID3D11RenderTargetView* rtvs[] = {rtv};
        context->OMSetRenderTargets(1, rtvs, nullptr);
        context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
        context->OMSetDepthStencilState(nullptr, 0);
        context->RSSetViewports(1, &vp);
        context->RSSetScissorRects(1, &full_scissor);
        context->RSSetState(nullptr);
        context->Draw(3, 0);
        // 解除绑定,避免 SRV/RTV 残留影响后续拷贝。
        ID3D11ShaderResourceView* null_srvs[] = {nullptr};
        context->PSSetShaderResources(0, 1, null_srvs);
        ID3D11RenderTargetView* null_rtvs[] = {nullptr};
        context->OMSetRenderTargets(1, null_rtvs, nullptr);
        return true;
    }

    bool SharedTexture::CopyCapturedTexture(ID3D11Device *device, ID3D11DeviceContext *context, ID3D11Texture2D *src) {
        D3D11_TEXTURE2D_DESC in_desc;
        src->GetDesc(&in_desc);

        if (in_desc.Width != curr_desc_.Width || in_desc.Height != curr_desc_.Height ||
            in_desc.Format != curr_desc_.Format) {

            if (texture_) {
                texture_.Release();
                texture_ = nullptr;
            }

            // 非 8bit 格式(如 UE5 的 R10G10B10A2)在生产端转成 B8G8R8A8 再共享,
            // 消费端与编码器只处理 8bit 纹理。
            const bool need_convert = !IsDirectShareableFormat(in_desc.Format);
            const DXGI_FORMAT dst_format = need_convert ? DXGI_FORMAT_B8G8R8A8_UNORM : in_desc.Format;

            D3D11_TEXTURE2D_DESC desc11 = {};
            desc11.Width = in_desc.Width;
            desc11.Height = in_desc.Height;
            desc11.Format = dst_format;
            desc11.MipLevels = 1;
            desc11.ArraySize = 1;
            desc11.SampleDesc.Count = 1;
            desc11.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            if (need_convert) {
                desc11.BindFlags |= D3D11_BIND_RENDER_TARGET;
            }
            // 注意:11on12(D3D12 游戏)设备上 KEYEDMUTEX/NTHANDLE 纹理创建会 E_INVALIDARG,
            // 只能用 plain SHARED;跨进程并发访问的稳定性由消费端长生命周期打开(不反复
            // open/close)+ 生产端 Flush 保证。
            desc11.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
            desc11.Usage = D3D11_USAGE_DEFAULT;

            auto hr = device->CreateTexture2D(&desc11, nullptr, &texture_);
            if (FAILED(hr)) {
                LOGE("create_d3d11_stage_surface: failed to create texture_ {} , {} src_format={} dst_format={}",
                     hr, GetLastError(), (int)in_desc.Format, (int)dst_format);
                return false;
            }

            this->curr_desc_ = in_desc;
            LOGI("Create D3DTexture2D success: {}x{} src_format={} dst_format={}",
                 desc11.Width, desc11.Height, (int)in_desc.Format, (int)dst_format);
        }

        const bool need_convert = texture_ && (curr_desc_.Format != [&] {
            D3D11_TEXTURE2D_DESC d; texture_->GetDesc(&d); return d.Format;
        }());

        if (!LockMutex()) {
            return false;
        }
        bool ok;
        if (need_convert) {
            ok = EnsureConvertShaders(device) && BlitConvertToBgra(device, context, src);
        } else {
            context->CopyResource(texture_, src);
            ok = true;
        }
        // Cross-process shared textures stay black until the producer GPU queue is flushed.
        context->Flush();
        ReleaseMutex();

        return ok;
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
            LOGE("SharedTexture texture_ is null");
            return false;
        }
        HRESULT hres;
        CComPtr<IDXGIKeyedMutex> key_mutex;
        hres = texture_.QueryInterface(&key_mutex);
        if (FAILED(hres) || !key_mutex) {
            // Plain SHARED — nothing to acquire.
            return true;
        }
        hres = key_mutex->AcquireSync(0, INFINITE);
        if (FAILED(hres)) {
            LOGE("SharedTexture AcquireSync failed: {:x}", (uint32_t)hres);
            return false;
        }
        return true;
    }

    bool SharedTexture::ReleaseMutex() {
        if (!texture_) {
            LOGE("SharedTexture texture_ is null");
            return false;
        }
        HRESULT hres;
        CComPtr<IDXGIKeyedMutex> key_mutex;
        hres = texture_.QueryInterface(&key_mutex);
        if (FAILED(hres) || !key_mutex) {
            return true;
        }
        hres = key_mutex->ReleaseSync(0);
        if (FAILED(hres)) {
            LOGE("SharedTexture ReleaseSync failed: {:x}", (uint32_t)hres);
            return false;
        }
        return true;
    }

} // namespace tc
