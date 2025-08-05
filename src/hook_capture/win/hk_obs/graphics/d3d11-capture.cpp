#include <d3d11.h>
#include <dxgi.h>
#include <atlbase.h>

#include "dxgi-helpers.hpp"
#include "graphics-hook.h"
#include "tc_common_new/log.h"
#include "tc_common_new/data.h"
#include "hook_manager.h"
#include "capture_message.h"
#include "../hk_video/d3d_utils.h"
#include "capture_message_maker.h"
#include <libyuv.h>
#include <fstream>

#include <d3d11.h>
#include <wrl/client.h>
#include "tc_common_new/win32/d3d_render.h"
#include "tc_common_new/win32/d3d_debug_helper.h"

using namespace tc;

static std::shared_ptr<D3DRender> g_render;

struct d3d11_data {
    ID3D11Device *device;         /* do not release */
    ID3D11DeviceContext *context; /* do not release */
    uint32_t cx;
    uint32_t cy;
    DXGI_FORMAT format;
    bool using_shtex;
    bool multisampled;

    union {
        /* shared texture */
        struct {
            struct shtex_data *shtex_info;
            ID3D11Texture2D *texture;
            HANDLE handle;
        };
        /* shared memory */
        struct {
            ID3D11Texture2D *copy_surfaces[NUM_BUFFERS];
            bool texture_ready[NUM_BUFFERS];
            bool texture_mapped[NUM_BUFFERS];
            uint32_t pitch;
            struct shmem_data *shmem_info;
            int cur_tex;
            int copy_wait;
        };
    };
};

static struct d3d11_data data = {};

void d3d11_free(void) {
    capture_free();

    if (data.using_shtex) {
        if (data.texture)
            data.texture->Release();
    } else {
        for (size_t i = 0; i < NUM_BUFFERS; i++) {
            if (data.copy_surfaces[i]) {
                if (data.texture_mapped[i])
                    data.context->Unmap(
                            data.copy_surfaces[i], 0);
                data.copy_surfaces[i]->Release();
            }
        }
    }

    memset(&data, 0, sizeof(data));

    hlog("----------------- d3d11 capture freed ----------------");
}

static bool create_d3d11_tex(uint32_t cx, uint32_t cy, ID3D11Texture2D **tex, HANDLE *handle) {
    HRESULT hr;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = cx;
    desc.Height = cy;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = apply_dxgi_format_typeless(
            data.format, global_hook_info->allow_srgb_alias);
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

    hr = data.device->CreateTexture2D(&desc, nullptr, tex);
    if (FAILED(hr)) {
        hlog_hr("create_d3d11_tex: failed to create texture", hr);
        return false;
    }

    if (!!handle) {
        IDXGIResource *dxgi_res;
        hr = (*tex)->QueryInterface(__uuidof(IDXGIResource),
                                    (void **) &dxgi_res);
        if (FAILED(hr)) {
            hlog_hr("create_d3d11_tex: failed to query "
                    "IDXGIResource interface from texture",
                    hr);
            return false;
        }

        hr = dxgi_res->GetSharedHandle(handle);
        dxgi_res->Release();
        if (FAILED(hr)) {
            hlog_hr("create_d3d11_tex: failed to get shared handle",
                    hr);
            return false;
        }
    }

    return true;
}

static inline bool d3d11_init_format(IDXGISwapChain *swap, HWND &window) {
    DXGI_SWAP_CHAIN_DESC desc;
    HRESULT hr;

    hr = swap->GetDesc(&desc);
    if (FAILED(hr)) {
        hlog_hr("d3d11_init_format: swap->GetDesc failed", hr);
        return false;
    }

    print_swap_desc(&desc);

    data.format = strip_dxgi_format_srgb(desc.BufferDesc.Format);
    data.multisampled = desc.SampleDesc.Count > 1;
    window = desc.OutputWindow;
    data.cx = desc.BufferDesc.Width;
    data.cy = desc.BufferDesc.Height;

    LOGI("d3d11 init format: {}, multisampled: {}, w: {}, h: {}", data.format, data.multisampled, data.cx, data.cy);
    return true;
}

static bool d3d11_shtex_init(HWND window) {
    bool success;
    data.using_shtex = true;
    success = create_d3d11_tex(data.cx, data.cy, &data.texture, &data.handle);

    if (!success) {
        hlog("d3d11_shtex_init: failed to create texture");
        return false;
    }
    if (!capture_init_shtex(&data.shtex_info, window, data.cx, data.cy,
                            data.format, false, (uintptr_t) data.handle)) {
        return false;
    }

    hlog("d3d11 shared texture capture successful");
    return true;
}

static void d3d11_init(IDXGISwapChain *swap) {
    HWND window;
    HRESULT hr;

    hr = swap->GetDevice(__uuidof(ID3D11Device), (void **) &data.device);
    if (FAILED(hr)) {
        hlog_hr("d3d11_init: failed to get device from swap", hr);
        return;
    }

    data.device->Release();

    data.device->GetImmediateContext(&data.context);
    data.context->Release();

    if (!d3d11_init_format(swap, window)) {
        return;
    }

    const bool success = d3d11_shtex_init(window);
    if (!success) {
        d3d11_free();
    }
}

static inline void d3d11_copy_texture(ID3D11Resource *dst, ID3D11Resource *src) {
    if (data.multisampled) {
        data.context->ResolveSubresource(dst, 0, src, 0, data.format);
    } else {
        data.context->CopyResource(dst, src);
    }
}

static inline void d3d11_shtex_capture(ID3D11Resource *back_buffer) {
    d3d11_copy_texture(data.texture, back_buffer);
}

void d3d11_capture(void *swap_ptr, void *back_buffer_ptr) {
    auto dxgi_back_buffer = (IDXGIResource *) back_buffer_ptr;
    auto swap = (IDXGISwapChain *) swap_ptr;

    HRESULT hr;
    if (!capture_active()) {
        LOGI("d3d11 init in d3d11_capture");
        d3d11_init(swap);
    }
    if (!capture_ready()) {
        LOGE("capture not ready in d3d11 capture");
        return;
    }

    ID3D11Resource *back_buffer;
    hr = dxgi_back_buffer->QueryInterface(__uuidof(ID3D11Resource), (void **) &back_buffer);
    if (FAILED(hr)) {
        LOGE("d3d11_shtex_capture: failed to get back_buffer: {}", hr);
        return;
    }

    d3d11_shtex_capture(back_buffer);

    // debug beg
    if (false) {
        D3D11_TEXTURE2D_DESC desc;
        data.texture->GetDesc(&desc);
        auto width = desc.Width;
        auto height = desc.Height;
        std::string msg = " --> width : " + std::to_string(width) + " height : " + std::to_string(height);
        LOGI(msg);

        std::vector<uint8_t> yuv_frame_data_;
        yuv_frame_data_.resize(1.5 * width * height);
        size_t pixel_size = width * height;

        const int uv_stride = width >> 1;
        uint8_t *y = yuv_frame_data_.data();
        uint8_t *u = y + pixel_size;
        uint8_t *v = u + (pixel_size >> 2);

        CComPtr<IDXGISurface> staging_surface = nullptr;
        hr =  data.texture->QueryInterface(IID_PPV_ARGS(&staging_surface));
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
        if (DXGI_FORMAT_B8G8R8A8_UNORM == desc.Format) {
            libyuv::ARGBToI420(mapped_rect.pBits, mapped_rect.Pitch, y, width, u, uv_stride, v, uv_stride, width,
                               height);
        } else if (DXGI_FORMAT_R8G8B8A8_UNORM == desc.Format) {
            libyuv::ABGRToI420(mapped_rect.pBits, mapped_rect.Pitch, y, width, u, uv_stride, v, uv_stride, width,
                               height);
        } else {
            libyuv::ARGBToI420(mapped_rect.pBits, mapped_rect.Pitch, y, width, u, uv_stride, v, uv_stride, width,
                               height);
        }
        {
            std::ofstream yuv_file("capture_yuv.yuv", std::ios::binary);
            yuv_file.write((char*)yuv_frame_data_.data(), yuv_frame_data_.size());
            yuv_file.close();
        }
    }
    // debug end

    // debug texture begin
    if (false) {
        //CopyID3D11Texture2D(data.texture, "captured_image");
        DebugOutDDS(data.texture, "xxcaptured_image");
    }
    // debug texture end

    auto hook_mgr = HookManager::Instance();
    auto shared_texture = hook_mgr->shared_texture_;
    shared_texture->CopyCapturedTexture(data.device, data.context, data.texture);

    D3D11_TEXTURE2D_DESC desc;
    data.texture->GetDesc(&desc);
    auto adapter_uid = tc::GetAdapterUid(data.device);
    CaptureVideoFrame capture_video_frame_msg{};
    capture_video_frame_msg.capture_type_ = kCaptureVideoByHandle;
    capture_video_frame_msg.data_length = 0;
    capture_video_frame_msg.frame_width_ = desc.Width;
    capture_video_frame_msg.frame_height_ = desc.Height;
    capture_video_frame_msg.frame_index_ = hook_mgr->AppendFrameIndex();
    capture_video_frame_msg.handle_ = shared_texture->GetSharedHandle();
    capture_video_frame_msg.frame_format_ = desc.Format;
    if (adapter_uid.has_value()) {
        capture_video_frame_msg.adapter_uid_ = adapter_uid.value();
    }
#if ENABLE_SHM
    auto msg_data = CaptureMessageMaker::ConvertMessageToData(capture_video_frame_msg);
    hook_mgr->Send(std::move(msg_data));
#else
    auto msg_data = CaptureMessageMaker::ConvertMessageToString(capture_video_frame_msg);
    hook_mgr->Send(msg_data);
#endif
    back_buffer->Release();
}
