#pragma once

#include "gl/raw_image.h"
#include "av_frame_ref.h"
#include "av_buffer_ref.h"
#include "px_common/win32/d3d11_wrapper.h"
#include <d3d11.h>
#include <wrl/client.h>
extern "C" {
#include <libavutil/pixdesc.h>
}

namespace px {

struct D3D11Image final : PlatformImage {
    std::shared_ptr<D3D11DeviceWrapper> device_owner{};
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture{};
    unsigned int subresource{};
    // COM ownership alone does not reserve a decoder-pool texture slice.
    // Keep the AVFrame buffer reference until all queued consumers finish.
    std::shared_ptr<const AVFrame> source_frame{};

    std::shared_ptr<PlatformImage> Clone() const override {
        return std::make_shared<D3D11Image>(*this);
    }
};

struct VulkanImage final : PlatformImage {
    // Destruction is reversed: release the frame before the renderer/device lease.
    AvBufferPtr device_owner{};
    std::shared_ptr<const AVFrame> frame{};

    std::shared_ptr<PlatformImage> Clone() const override {
        if (!frame)
            return {};
        auto copy = std::make_shared<VulkanImage>();
        copy->device_owner = device_owner;
        copy->frame = CloneAvFrame(*frame);
        return copy->frame ? copy : nullptr;
    }
};

inline std::shared_ptr<const D3D11Image> D3D11FrameOf(const std::shared_ptr<RawImage>& image) {
    return image ? std::dynamic_pointer_cast<const D3D11Image>(image->Platform()) : nullptr;
}

inline std::shared_ptr<const VulkanImage> VulkanFrameOf(const std::shared_ptr<RawImage>& image) {
    return image ? std::dynamic_pointer_cast<const VulkanImage>(image->Platform()) : nullptr;
}

inline std::shared_ptr<RawImage> MakeD3D11Image(const AVFrame& source, std::shared_ptr<D3D11DeviceWrapper> device_owner) {
    if (source.format != AV_PIX_FMT_D3D11 || !source.data[0] || !device_owner || !device_owner->IsValid())
        return {};
    auto storage = std::make_shared<D3D11Image>();
    storage->device_owner = std::move(device_owner);
    storage->texture = Microsoft::WRL::ComPtr<ID3D11Texture2D>{reinterpret_cast<ID3D11Texture2D*>(source.data[0])};
    // FFmpeg encodes the integer array slice in data[1], not an address to dereference.
    storage->subresource = static_cast<unsigned int>(reinterpret_cast<std::uintptr_t>(source.data[1]));
    storage->source_frame = CloneAvFrame(source);
    if (!storage->source_frame)
        return {};
    return RawImage::MakePlatform(kRawImageD3D11Texture, source.width, source.height, std::move(storage));
}

inline std::shared_ptr<RawImage> MakeVulkanImage(const AVFrame& source, AvBufferPtr device_owner = {}) {
    if (!av_pix_fmt_desc_get(static_cast<AVPixelFormat>(source.format)) || source.width <= 0 || source.height <= 0)
        return {};
    auto storage = std::make_shared<VulkanImage>();
    storage->device_owner = std::move(device_owner);
    storage->frame = CloneAvFrame(source);
    if (!storage->frame)
        return {};
    return RawImage::MakePlatform(kRawImageVulkanAVFrame, source.width, source.height, std::move(storage));
}

} // namespace px
