#include "client_video_frame.h"

#include "px_client_sdk/gl/raw_image.h"

#include <libyuv.h>

namespace px::client::imgui {

std::shared_ptr<ClientVideoFrame> ConvertVideoFrame(const std::shared_ptr<px::RawImage>& image) {
    if (!image || image->img_width <= 0 || image->img_height <= 0) {
        return {};
    }
    auto result = std::make_shared<ClientVideoFrame>();
    result->width = image->img_width;
    result->height = image->img_height;
    result->bgra.resize(static_cast<std::size_t>(result->width) * static_cast<std::size_t>(result->height) * 4U);
    const auto y = image->Plane(0);
    const auto yLayout = image->Layout(0);
    int conversion{-1};
    if (image->Format() == px::kRawImageI420 || image->Format() == px::kRawImageI444) {
        const auto u = image->Plane(1);
        const auto v = image->Plane(2);
        const auto uLayout = image->Layout(1);
        const auto vLayout = image->Layout(2);
        if (!yLayout || !uLayout || !vLayout || y.empty() || u.empty() || v.empty()) {
            return {};
        }
        if (image->Format() == px::kRawImageI420) {
            conversion = libyuv::I420ToARGB(reinterpret_cast<const std::uint8_t*>(y.data()), yLayout->stride,
                                             reinterpret_cast<const std::uint8_t*>(u.data()), uLayout->stride,
                                             reinterpret_cast<const std::uint8_t*>(v.data()), vLayout->stride, result->bgra.data(), result->width * 4,
                                             result->width, result->height);
        } else {
            conversion = libyuv::I444ToARGB(reinterpret_cast<const std::uint8_t*>(y.data()), yLayout->stride,
                                             reinterpret_cast<const std::uint8_t*>(u.data()), uLayout->stride,
                                             reinterpret_cast<const std::uint8_t*>(v.data()), vLayout->stride, result->bgra.data(), result->width * 4,
                                             result->width, result->height);
        }
    } else if (image->Format() == px::kRawImageNV12) {
        const auto uv = image->Plane(1);
        const auto uvLayout = image->Layout(1);
        if (!yLayout || !uvLayout || y.empty() || uv.empty()) {
            return {};
        }
        conversion = libyuv::NV12ToARGB(reinterpret_cast<const std::uint8_t*>(y.data()), yLayout->stride,
                                         reinterpret_cast<const std::uint8_t*>(uv.data()), uvLayout->stride, result->bgra.data(), result->width * 4,
                                         result->width, result->height);
    }
    return conversion == 0 ? result : std::shared_ptr<ClientVideoFrame>{};
}

} // namespace px::client::imgui
