//
// Created by RGAA on 19/11/2024.
//

#include "video_frame_carrier.h"
#include <atlcomcli.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <limits>
#include <libyuv/convert.h>
#include <libyuv/convert_from_argb.h>
#include "tc_common_new/log.h"
#include "tc_common_new/string_util.h"
#include "tc_common_new/time_util.h"
#include "tc_common_new/image.h"
#include "tc_common_new/thread.h"
#include "tc_common_new/defer.h"
#include "tc_common_new/file.h"
#include "frame_carrier_plugin.h"
#include "gr_render/plugins/plugin_manager.h"
#include "tc_common_new/win32/d3d_debug_helper.h"
#include "gr_render/plugin_interface/gr_frame_processor_plugin.h"

namespace tc
{

    VideoFrameCarrier::VideoFrameCarrier(FrameCarrierPlugin* plugin,
                                         const ComPtr<ID3D11Device>& d3d11_device,
                                         const ComPtr<ID3D11DeviceContext>& d3d11_device_context,
                                         uint64_t adapter_uid,
                                         const std::string& monitor_name,
                                         bool enable_full_color_mode)
    {
        plugin_ = plugin;
        d3d11_device_ = d3d11_device;
        d3d11_device_context_ = d3d11_device_context;
        adapter_uid_ = adapter_uid;
        monitor_name_ = monitor_name;
        enable_full_color_mode_ = enable_full_color_mode;
        yuv_converter_thread_ = Thread::Make("video frame carrier", 1024);
        yuv_converter_thread_->Poll();
        // logo points
        logo_points_ = plugin_->GetLogoPoints();
        big_logo_points_ = plugin_->GetBigLogoPoints();
        cover_points_ = plugin_->GetCoverPoints();

#ifdef OPENSOURCE_BUILD
        enable_logo_ = true;
#elif defined(OFFICIAL_BUILD)
        enable_logo_ = false;
#else
        enable_logo_ = false;
#endif

    }

    bool VideoFrameCarrier::D3D11Texture2DLockMutex(const ComPtr<ID3D11Texture2D>& texture2d) {
        HRESULT res;
        ComPtr<IDXGIKeyedMutex> key_mutex;
        res = texture2d.As(&key_mutex);
        if (FAILED(res) || !key_mutex) {
            // Plain SHARED textures (e.g. current DDA path) have no keyed mutex.
            return true;
        }
        // 仅当纹理带 SHARED_KEYEDMUTEX 时才需要 acquire;plain SHARED 直接返回。
        res = key_mutex->AcquireSync(0x0, INFINITE);
        if (FAILED(res)) {
            LOGE("D3D11Texture2DLockMutex AcquireSync failed: {:x}", (uint32_t)res);
            return false;
        }
        return true;
    }

    bool VideoFrameCarrier::D3D11Texture2DReleaseMutex(const ComPtr<ID3D11Texture2D>& texture2d) {
        HRESULT res;
        ComPtr<IDXGIKeyedMutex> key_mutex;
        res = texture2d.As(&key_mutex);
        if (FAILED(res) || !key_mutex) {
            return true;
        }
        res = key_mutex->ReleaseSync(0x0);
        if (FAILED(res)) {
            LOGE("D3D11Texture2DReleaseMutex ReleaseSync failed: {:x}", (uint32_t)res);
            return false;
        }
        return true;
    }

    bool VideoFrameCarrier::IsEncoderFriendlyFormat(DXGI_FORMAT format) {
        return format == DXGI_FORMAT_B8G8R8A8_UNORM
               || format == DXGI_FORMAT_B8G8R8X8_UNORM
               || format == DXGI_FORMAT_R8G8B8A8_UNORM;
    }

    bool VideoFrameCarrier::EnsureConvertShaders() {
        if (conv_vs_ && conv_ps_) {
            return true;
        }
        if (conv_shader_failed_) {
            return false;
        }
        // d3dcompiler_47.dll 是 Win10+ 系统组件,运行时加载避免新增链接依赖。
        HMODULE d3dcompiler = LoadLibraryA("d3dcompiler_47.dll");
        if (!d3dcompiler) {
            LOGE("EnsureConvertShaders: d3dcompiler_47.dll not found");
            conv_shader_failed_ = true;
            return false;
        }
        auto d3d_compile = (pD3DCompile) GetProcAddress(d3dcompiler, "D3DCompile");
        if (!d3d_compile) {
            LOGE("EnsureConvertShaders: D3DCompile not found");
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
        ComPtr<ID3DBlob> vs_blob, ps_blob, err_blob;
        HRESULT hr = d3d_compile(hlsl, strlen(hlsl), nullptr, nullptr, nullptr,
                                 "VSMain", "vs_4_0", 0, 0, &vs_blob, &err_blob);
        if (FAILED(hr)) {
            LOGE("EnsureConvertShaders: VS compile failed: {:x}", (uint32_t)hr);
            conv_shader_failed_ = true;
            return false;
        }
        hr = d3d_compile(hlsl, strlen(hlsl), nullptr, nullptr, nullptr,
                         "PSMain", "ps_4_0", 0, 0, &ps_blob, &err_blob);
        if (FAILED(hr)) {
            LOGE("EnsureConvertShaders: PS compile failed: {:x}", (uint32_t)hr);
            conv_shader_failed_ = true;
            return false;
        }
        hr = d3d11_device_->CreateVertexShader(vs_blob->GetBufferPointer(),
                                               vs_blob->GetBufferSize(), nullptr, &conv_vs_);
        if (FAILED(hr)) {
            LOGE("EnsureConvertShaders: CreateVertexShader failed: {:x}", (uint32_t)hr);
            conv_shader_failed_ = true;
            return false;
        }
        hr = d3d11_device_->CreatePixelShader(ps_blob->GetBufferPointer(),
                                              ps_blob->GetBufferSize(), nullptr, &conv_ps_);
        if (FAILED(hr)) {
            LOGE("EnsureConvertShaders: CreatePixelShader failed: {:x}", (uint32_t)hr);
            conv_shader_failed_ = true;
            return false;
        }
        LOGI("EnsureConvertShaders: 10bit->8bit convert shaders ready");
        return true;
    }

    bool VideoFrameCarrier::BlitConvertToBgra(const ComPtr<ID3D11Texture2D>& src) {
        ComPtr<ID3D11ShaderResourceView> srv;
        HRESULT hr = d3d11_device_->CreateShaderResourceView(src.Get(), nullptr, &srv);
        if (FAILED(hr)) {
            LOGE("BlitConvertToBgra: CreateShaderResourceView failed: {:x}", (uint32_t)hr);
            return false;
        }
        ComPtr<ID3D11RenderTargetView> rtv;
        hr = d3d11_device_->CreateRenderTargetView(texture2d_.Get(), nullptr, &rtv);
        if (FAILED(hr)) {
            LOGE("BlitConvertToBgra: CreateRenderTargetView failed: {:x}", (uint32_t)hr);
            return false;
        }
        D3D11_TEXTURE2D_DESC desc;
        texture2d_->GetDesc(&desc);
        D3D11_VIEWPORT vp = {0.0f, 0.0f, (FLOAT)desc.Width, (FLOAT)desc.Height, 0.0f, 1.0f};

        auto ctx = d3d11_device_context_;
        ctx->IASetInputLayout(nullptr);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(conv_vs_.Get(), nullptr, 0);
        ctx->PSSetShader(conv_ps_.Get(), nullptr, 0);
        ID3D11ShaderResourceView* srvs[] = {srv.Get()};
        ctx->PSSetShaderResources(0, 1, srvs);
        ID3D11RenderTargetView* rtvs[] = {rtv.Get()};
        ctx->OMSetRenderTargets(1, rtvs, nullptr);
        ctx->RSSetViewports(1, &vp);
        ctx->Draw(3, 0);
        // 解除绑定,避免 SRV/RTV 残留影响后续 CopyResource / NVENC。
        ID3D11ShaderResourceView* null_srvs[] = {nullptr};
        ctx->PSSetShaderResources(0, 1, null_srvs);
        ID3D11RenderTargetView* null_rtvs[] = {nullptr};
        ctx->OMSetRenderTargets(1, null_rtvs, nullptr);
        ctx->Flush();
        return true;
    }

    bool VideoFrameCarrier::CopyID3D11Texture2D(const ComPtr<ID3D11Texture2D>& shared_texture) {
        if (!D3D11Texture2DLockMutex(shared_texture)) {
            LOGE("D3D11Texture2DLockMutex error");
            return false;
        }
        std::shared_ptr<void> auto_release_texture2D_mutex((void *) nullptr, [=, this](void *temp) {
            D3D11Texture2DReleaseMutex(shared_texture);
        });

        HRESULT res;
        D3D11_TEXTURE2D_DESC desc;
        shared_texture->GetDesc(&desc);

        // UE5(D3D12) 默认 R10G10B10A2 swapchain,NVENC H264 编码 10bit 输入会失败
        // 甚至导致 GPU TDR(device hung)。统一转成 B8G8R8A8 再走原有编码管线。
        const bool need_convert = !IsEncoderFriendlyFormat(desc.Format);
        const DXGI_FORMAT dst_format = need_convert ? DXGI_FORMAT_B8G8R8A8_UNORM : desc.Format;

        ComPtr<ID3D11Device> curDevice;
        shared_texture->GetDevice(&curDevice);

        if (texture2d_) {
            ComPtr<ID3D11Device> sharedTextureDevice;
            texture2d_->GetDevice(&sharedTextureDevice);
            if (sharedTextureDevice != curDevice) {
                texture2d_ = nullptr;
            }
            if (texture2d_) {
                D3D11_TEXTURE2D_DESC sharedTextureDesc;
                texture2d_->GetDesc(&sharedTextureDesc);
                if (desc.Width != sharedTextureDesc.Width ||
                    desc.Height != sharedTextureDesc.Height ||
                    dst_format != sharedTextureDesc.Format) {
                    texture2d_ = nullptr;
                }
            }
        }

        if (!texture2d_) {
            D3D11_TEXTURE2D_DESC createDesc;
            ZeroMemory(&createDesc, sizeof(createDesc));
            createDesc.Format = dst_format;
            createDesc.Width = desc.Width;
            createDesc.Height = desc.Height;
            createDesc.MipLevels = 1;
            createDesc.ArraySize = 1;
            createDesc.SampleDesc.Count = 1;
            // DEFAULT for NVENC CopyResource; STAGING as encode source can yield black on some paths.
            createDesc.Usage = D3D11_USAGE_DEFAULT;
            createDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
            createDesc.CPUAccessFlags = 0;
            res = curDevice->CreateTexture2D(&createDesc, NULL, texture2d_.GetAddressOf());
            if (FAILED(res)) {
                LOGE("frame carrier create texture failed with:{} format={}",
                     StringUtil::GetErrorStr(res).c_str(), (int)desc.Format);
                return false;
            }
            LOGI("frame carrier texture: {}x{} dxgi_format={} (src_format={})",
                 desc.Width, desc.Height, (int)dst_format, (int)desc.Format);
        }
        ComPtr<ID3D11DeviceContext> ctx;
        curDevice->GetImmediateContext(&ctx);
        if (need_convert) {
            if (!EnsureConvertShaders() || !BlitConvertToBgra(shared_texture)) {
                LOGE("CopyID3D11Texture2D: convert to BGRA failed, src format={}", (int)desc.Format);
                return false;
            }
        } else {
            ctx->CopyResource(texture2d_.Get(), shared_texture.Get());
        }

        return true;
    }

    ComPtr<ID3D11Texture2D> VideoFrameCarrier::OpenSharedTexture(HANDLE handle) {
        ComPtr<ID3D11Texture2D> sharedTexture;
        HRESULT res;
        res = d3d11_device_->OpenSharedResource(handle, IID_PPV_ARGS(sharedTexture.GetAddressOf()));
        if (FAILED(res)) {
            HRESULT res1 = res;
            // D3D12(11on12)路径的共享纹理是 NT handle,必须走 OpenSharedResource1。
            ComPtr<ID3D11Device1> device1;
            if (SUCCEEDED(d3d11_device_.As(&device1)) && device1) {
                res1 = device1->OpenSharedResource1(handle, IID_PPV_ARGS(sharedTexture.GetAddressOf()));
            }
            LOGW("OpenSharedTexture: handle={:#x} km_hr={:x} nt_hr={:x}",
                 (uint64_t)handle, (uint32_t)res, (uint32_t)res1);
            res = res1;
        }
        if (FAILED(res)) {
            LOGE("OpenSharedResource failed: {:x}", (uint32_t)res);
            return nullptr;
        }
        return sharedTexture;
    }

    ComPtr<ID3D11Texture2D> VideoFrameCarrier::CopyTexture(const std::string& mon_name, uint64_t handle, uint64_t frame_index) {
        // 同一 handle 只打开一次并长期持有;反复 OpenSharedResource/Close 会让
        // 11on12 共享资源的底层 D3D12 资源状态紊乱,最终 device removed。
        if (handle != opened_shared_handle_ || !opened_shared_texture_) {
            opened_shared_texture_ = OpenSharedTexture(reinterpret_cast<HANDLE>(handle));
            opened_shared_handle_ = opened_shared_texture_ ? handle : 0;
        }
        ComPtr<ID3D11Texture2D> shared_texture = opened_shared_texture_;
        if (!shared_texture) {
            LOGE("OpenSharedTexture failed.");
            return nullptr;
        }

        D3D11_TEXTURE2D_DESC desc;
        shared_texture->GetDesc(&desc);

        // keyed mutex 的 Acquire/Release 由 CopyID3D11Texture2D 内部处理,这里不要重复加锁。
        if (!CopyID3D11Texture2D(shared_texture)) {
            LOGE("CopyID3D11Texture2D failed.");
            return nullptr;
        }
        //DebugOutDDS(texture2d_.Get(), "2.dds");
        //PrintD3DTexture2DDesc("frame carrier, texture2d", texture2d_.Get());

        // logo
        if (enable_logo_) {
            StampLogoOnTexture(texture2d_, desc.Width, desc.Height);
        }

        return texture2d_;
    }

    void VideoFrameCarrier::StampLogoOnTexture(const ComPtr<ID3D11Texture2D>& texture, int tex_width, int tex_height) {
        auto logo_image = plugin_->GetLogoImage();
        auto logo_width = static_cast<UINT>(logo_image->GetWidth());
        auto logo_height = static_cast<UINT>(logo_image->GetHeight());

        if (logo_point_texture_ == nullptr) {
            D3D11_TEXTURE2D_DESC desc;
            texture->GetDesc(&desc);
            D3D11_TEXTURE2D_DESC logo_desc = {
                logo_width, logo_height, 1, 1,
                desc.Format, // MUST SAME FORMAT
                {1, 0},
                D3D11_USAGE_DEFAULT,
                D3D11_BIND_SHADER_RESOURCE,
                0, 0
            };

            d3d11_device_->CreateTexture2D(&logo_desc, nullptr, &logo_point_texture_);
            d3d11_device_context_->UpdateSubresource(logo_point_texture_.Get(), 0, nullptr, logo_image->data->DataAddr(), logo_image->GetWidth() * 4, 0);
        }

        // logo
        {
            auto big_picture = tex_width > 1920 && tex_height > 1080;
            const auto &points = big_picture ? big_logo_points_ : logo_points_;
            auto right_offset = big_picture ? 280 : 135;
            right_offset += 130; // total logo width
            D3D11_BOX srcBox = {0, 0, 0, 1, 1, 1};
            for (const auto &point: points) {
                d3d11_device_context_->CopySubresourceRegion(
                        texture.Get(),
                        0,
                        (logo_pos_offset_ ? (tex_width - right_offset) : 0) + point.first, point.second, 0,
                        logo_point_texture_.Get(),
                        0,
                        &srcBox
                );
            }
        }

        // cover
        // if (!plugin_->IsLicenseOk()) {
        //     //D3D11_BOX srcBox = {0, 0, 0, 280, 48, 1};
        //     D3D11_BOX srcBox = {0, 0, 0, 1, 1, 1};
        //     int size = std::min(tex_width/300, 5);
        //     for (int i = 0; i < size; i++) {
        //         int offset_x = i * (tex_width/size);
        //         int offset_y = i * ((tex_height)/ size);
        //         for (const auto &point: cover_points_) {
        //             d3d11_device_context_->CopySubresourceRegion(
        //                 texture.Get(),
        //                 0,
        //                 offset_x + point.first,
        //                 offset_y + point.second,
        //                 0,
        //                 logo_point_texture_.Get(),
        //                 0,
        //                 &srcBox
        //             );
        //         }
        //     }
        // }
    }

    bool VideoFrameCarrier::ConvertRawImage(const std::shared_ptr<Image> image,
                                            std::function<void(const std::shared_ptr<Image>&)>&& rgba_cbk,
                                            std::function<void(const std::shared_ptr<Image>&)>&& yuv_cbk) {
        raw_image_rgba_ = image;
        // stamp logo
        if (enable_logo_) {
            StampLogoOnRGBABuffer(image);
        }

        rgba_cbk(raw_image_rgba_);
		// to do 明确下gdi原图的像素布局
        if (enable_full_color_mode_) {
            ConvertToYuv444(std::move(yuv_cbk));
        }
        else {
            ConvertToYuv420(std::move(yuv_cbk));
        }

        return true;
    }

    void VideoFrameCarrier::StampLogoOnRGBABuffer(const std::shared_ptr<Image>& image) {
        auto big_picture = image->width > 1920 && image->height > 1080;
        const auto& points = big_picture ? big_logo_points_ : logo_points_;
        auto right_offset = big_picture ? 280 : 135;
        right_offset += 130; // total logo width
        for (const auto& point : points) {
            auto offset = (point.second * image->width + (point.first + (image->width - (logo_pos_offset_ ? right_offset : 0)))) * 4;
            if (image->data->Size() <= offset + 3) {
                return;
            }
            uint8_t *dst_r = (uint8_t*)&image->data->DataAddr()[offset];
            uint8_t *dst_g = (uint8_t*)&image->data->DataAddr()[offset+1];
            uint8_t *dst_b = (uint8_t*)&image->data->DataAddr()[offset+2];
            uint8_t *dst_a = (uint8_t*)&image->data->DataAddr()[offset+3];
            *dst_r = 0;
            *dst_g = 0;
            *dst_b = 0;
            *dst_a = 0;
        }
    }

    bool VideoFrameCarrier::MapRawTexture(const ComPtr<ID3D11Texture2D>& texture, DXGI_FORMAT format, int height,
                                          std::function<void(const std::shared_ptr<Image>&)>&& rgba_cbk,
                                          std::function<void(const std::shared_ptr<Image>&)>&& yuv_cbk) {
        // The texture coming from CopyTexture is DEFAULT-usage (GPU encoders like NVENC consume it
        // directly), which is not CPU-mappable. CPU encoders (ffmpeg/QSV) need a STAGING readback copy.
        ComPtr<ID3D11Texture2D> map_source = texture;
        D3D11_TEXTURE2D_DESC src_desc{};
        texture->GetDesc(&src_desc);
        const bool cpu_readable = src_desc.Usage == D3D11_USAGE_STAGING
                                  && (src_desc.CPUAccessFlags & D3D11_CPU_ACCESS_READ);
        if (!cpu_readable) {
            bool need_create = true;
            if (map_staging_texture_) {
                D3D11_TEXTURE2D_DESC staging_desc{};
                map_staging_texture_->GetDesc(&staging_desc);
                need_create = staging_desc.Width != src_desc.Width
                              || staging_desc.Height != src_desc.Height
                              || staging_desc.Format != src_desc.Format;
            }
            if (need_create) {
                D3D11_TEXTURE2D_DESC staging_desc = src_desc;
                staging_desc.Usage = D3D11_USAGE_STAGING;
                staging_desc.BindFlags = 0;
                staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                staging_desc.MiscFlags = 0;
                map_staging_texture_ = nullptr;
                auto hr = d3d11_device_->CreateTexture2D(&staging_desc, nullptr, map_staging_texture_.GetAddressOf());
                if (FAILED(hr)) {
                    LOGE("MapRawTexture create staging texture failed: {:x}", (uint32_t)hr);
                    return false;
                }
                LOGI("MapRawTexture staging texture: {}x{} dxgi_format={}", staging_desc.Width, staging_desc.Height, (int)staging_desc.Format);
            }
            ComPtr<ID3D11DeviceContext> ctx;
            d3d11_device_->GetImmediateContext(&ctx);
            ctx->CopyResource(map_staging_texture_.Get(), texture.Get());
            map_source = map_staging_texture_;
        }

        CComPtr<IDXGISurface> staging_surface = nullptr;
        auto hr = map_source->QueryInterface(IID_PPV_ARGS(&staging_surface));
        if (FAILED(hr)) {
            LOGE("MapRawTexture !QueryInterface(IDXGISurface) err");
            return false;
        }
        DXGI_MAPPED_RECT mapped_rect{};
        hr = staging_surface->Map(&mapped_rect, DXGI_MAP_READ);
        if (FAILED(hr)) {
            LOGE("MapRawTexture !Map(IDXGISurface)");
            return false;
        }
        auto defer = Defer::Make([staging_surface]() {
            staging_surface->Unmap();
        });

        // copy to raw image buffer
        // 用纹理自身的实际格式——10bit 源纹理已在 CopyTexture 里转成 B8G8R8A8,
        // 而调用方传的 format 仍是捕获原始格式(如 R10G10B10A2),直接用会解释错通道。
        raw_image_rgba_format_ = src_desc.Format;
        bool ok = CopyToRawImage(mapped_rect.pBits, mapped_rect.Pitch, height);
        if (ok) {
            rgba_cbk(raw_image_rgba_);
        }
        
        if (enable_full_color_mode_) {
            ConvertToYuv444(std::move(yuv_cbk));
        }
        else {
            ConvertToYuv420(std::move(yuv_cbk));
        }

        return ok;
    }

    bool VideoFrameCarrier::CopyToRawImage(const uint8_t* data, int row_pitch_bytes, int height) {
        if (!data) {
            LOGE("CopyToRawImage failed: data is null");
            return false;
        }
        if (row_pitch_bytes <= 0 || height <= 0) {
            LOGE("CopyToRawImage failed: invalid row_pitch_bytes ({}) or height ({})", row_pitch_bytes, height);
            return false;
        }
        if ((row_pitch_bytes % 4) != 0) {
            LOGE("CopyToRawImage failed: row_pitch_bytes ({}) is not RGBA aligned", row_pitch_bytes);
            return false;
        }

        const auto row_pitch = static_cast<size_t>(row_pitch_bytes);
        const auto image_height = static_cast<size_t>(height);
        if (row_pitch > (std::numeric_limits<size_t>::max)() / image_height) {
            LOGE("CopyToRawImage failed: size overflow, row_pitch_bytes: {}, height: {}", row_pitch_bytes, height);
            return false;
        }

        const auto total_size = row_pitch * image_height;
        const auto width = row_pitch_bytes / 4;
        if (raw_image_rgba_ == nullptr ||
            !raw_image_rgba_->GetData() ||
            raw_image_rgba_->GetData()->Size() != total_size) {
            raw_image_rgba_ = Image::Make(Data::Make(nullptr, total_size), width, height);
        }
        if (!raw_image_rgba_ || !raw_image_rgba_->GetData()) {
            LOGE("CopyToRawImage failed: raw image buffer allocation failed");
            return false;
        }

        auto* dst = raw_image_rgba_->GetData()->DataAddr();
        if (!dst) {
            LOGE("CopyToRawImage failed: raw image buffer address is null");
            return false;
        }
        if (total_size > raw_image_rgba_->GetData()->Size()) {
            LOGE("CopyToRawImage failed: raw image buffer too small, need: {}, actual: {}",
                 total_size, raw_image_rgba_->GetData()->Size());
            return false;
        }

        memcpy(dst, data, total_size);
        raw_image_rgba_->raw_img_type_ = (RawImageType)GetRawImageType();
        return true;
    }

    void VideoFrameCarrier::ConvertToYuv420(std::function<void(const std::shared_ptr<Image>&)>&& yuv_cbk) {
        auto task = [=, this]() {
            auto beg = TimeUtil::GetCurrentTimestamp();
            if (!raw_image_rgba_ || !raw_image_rgba_->GetData()) {
                return;
            }
            if (!raw_image_yuv_ ||
                (raw_image_yuv_->GetWidth() != raw_image_rgba_->GetWidth() || raw_image_yuv_->GetHeight() != raw_image_yuv_->GetHeight()) ||
                raw_image_yuv_->raw_img_type_ != RawImageType::kI420) 
            {
                raw_image_yuv_ = Image::Make(Data::Make(nullptr, raw_image_rgba_->GetWidth() * raw_image_rgba_->GetHeight() * 1.5),
                                             raw_image_rgba_->GetWidth(), raw_image_rgba_->GetHeight(), RawImageType::kI420);
            }
            int width = raw_image_rgba_->GetWidth();
            int height = raw_image_rgba_->GetHeight();
            size_t pixel_size = width * height;

            const int uv_stride = width >> 1;
            uint8_t* y = (uint8_t*)raw_image_yuv_->GetData()->DataAddr();
            uint8_t* u = y + pixel_size;
            uint8_t* v = u + (pixel_size >> 2);

            auto pitch = raw_image_rgba_->GetWidth() * 4;
            auto data_buffer = (uint8_t*)raw_image_rgba_->GetData()->DataAddr();
            if (DXGI_FORMAT_B8G8R8A8_UNORM == raw_image_rgba_format_) {
                libyuv::ARGBToI420(data_buffer, pitch, y, width, u, uv_stride, v, uv_stride, width, height);
            }
            else if (DXGI_FORMAT_R8G8B8A8_UNORM == raw_image_rgba_format_) {
                libyuv::ABGRToI420(data_buffer, pitch, y, width, u, uv_stride, v, uv_stride, width, height);
            }
            else {
                libyuv::ARGBToI420(data_buffer, pitch, y, width, u, uv_stride, v, uv_stride, width, height);
            }

#if 0   // save to file
            static int index = 0;
            auto file = File::OpenForWrite("ConvertToYuv_" +  std::to_string(index % 10) + ".yuv420");
            if (file) {
                file->Write(0, raw_image_yuv_->GetData());
            }
            ++index;
#endif

            yuv_cbk(raw_image_yuv_);
        };
        yuv_converter_thread_->Post(std::move(task));

    }

    void VideoFrameCarrier::ConvertToYuv444(std::function<void(const std::shared_ptr<Image>&)>&& yuv_cbk) {
        auto task = [=, this]() {
            auto beg = TimeUtil::GetCurrentTimestamp();

            if (!raw_image_rgba_ || !raw_image_rgba_->GetData()) {
                return;
            }

            if (!raw_image_yuv_ ||
                (raw_image_yuv_->GetWidth() != raw_image_rgba_->GetWidth() || raw_image_yuv_->GetHeight() != raw_image_yuv_->GetHeight()) ||
                raw_image_yuv_->raw_img_type_ != RawImageType::kI444)
            {
                raw_image_yuv_ = Image::Make(Data::Make(nullptr, raw_image_rgba_->GetWidth() * raw_image_rgba_->GetHeight() * 3),
                    raw_image_rgba_->GetWidth(), raw_image_rgba_->GetHeight(), RawImageType::kI444);
            }
            int width = raw_image_rgba_->GetWidth();
            int height = raw_image_rgba_->GetHeight();
            size_t pixel_size = width * height;

            const int uv_stride = width;
            uint8_t* y = (uint8_t*)raw_image_yuv_->GetData()->DataAddr();
            uint8_t* u = y + pixel_size;
            uint8_t* v = u + pixel_size;

            auto pitch = raw_image_rgba_->GetWidth() * 4;
            auto data_buffer = (uint8_t*)raw_image_rgba_->GetData()->DataAddr();
            if (DXGI_FORMAT_B8G8R8A8_UNORM == raw_image_rgba_format_) {
                libyuv::ARGBToI444(data_buffer, pitch, y, width, u, uv_stride, v, uv_stride, width, height);
            }
            else if (DXGI_FORMAT_R8G8B8A8_UNORM == raw_image_rgba_format_) {
                libyuv::ARGBToI444(data_buffer, pitch, y, width, u, uv_stride, v, uv_stride, width, height);
            }
            else {
                libyuv::ARGBToI444(data_buffer, pitch, y, width, u, uv_stride, v, uv_stride, width, height);
            }

#if 0   // save yuv file
            static int index = 0;
            auto yuv444_file = File::OpenForWrite("ConvertToYuv_" + std::to_string(index % 10) + ".yuv444");
            if (yuv444_file) {
                yuv444_file->Write(0, raw_image_yuv_->GetData());
            }
            ++index;
#endif
            yuv_cbk(raw_image_yuv_);
        };
        yuv_converter_thread_->Post(std::move(task));

    }

    int VideoFrameCarrier::GetRawImageType() const {
        if (DXGI_FORMAT_B8G8R8A8_UNORM == raw_image_rgba_format_) {
            return (int)RawImageType::kBGRA;
        }
        else if (DXGI_FORMAT_R8G8B8A8_UNORM == raw_image_rgba_format_) {
            return (int)RawImageType::kRGBA;
        }
        else {
            return (int)RawImageType::kRGBA;
        }
    }

    void VideoFrameCarrier::Exit() {
        if (yuv_converter_thread_) {
            yuv_converter_thread_->Exit();
        }
        opened_shared_texture_.Reset();
        opened_shared_handle_ = 0;
        if (texture2d_) {
            texture2d_.Reset();
        }
        if (logo_point_texture_) {
            logo_point_texture_.Reset();
        }
        if (texture2d_) {
            texture2d_.Reset();
        }
        if (d3d11_device_) {
            d3d11_device_.Reset();
        }
        if (d3d11_device_context_) {
            d3d11_device_context_.Reset();
        }
        logo_points_.clear();
        big_logo_points_.clear();
    }

    void VideoFrameCarrier::SetFullColorModeEnabled(bool enabled) {
        enable_full_color_mode_ = enabled;
    }

    void VideoFrameCarrier::ChangeLogoPosition() {
        logo_pos_offset_ = !logo_pos_offset_;
    }

}
