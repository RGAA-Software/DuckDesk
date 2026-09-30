#include "client_screenshot.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include "px_client_sdk/platform/windows/windows_video_frame.h"
#include "px_ui/product_brand.h"

extern "C" {
#include <libavutil/hwcontext.h>
#include <libswscale/swscale.h>
}

namespace px::client::imgui {
namespace {

struct ScaleContextDeleter final {
    void operator()(SwsContext* context) const noexcept {  // NOLINT(pixels-raw-pointer-boundary): FFmpeg destruction ABI.
        sws_freeContext(context);
    }
};

struct SurfaceDeleter final {
    void operator()(SDL_Surface* surface) const noexcept {  // NOLINT(pixels-raw-pointer-boundary): SDL destruction ABI.
        SDL_DestroySurface(surface);
    }
};

px::AvFramePtr SoftwareFrame(const std::shared_ptr<px::RawImage>& image) {
    if (!image) return {};
    std::shared_ptr<const AVFrame> sourceFrame{};
    if (const auto vulkanImage = px::VulkanFrameOf(image)) sourceFrame = vulkanImage->frame;
    if (const auto d3dImage = px::D3D11FrameOf(image)) sourceFrame = d3dImage->source_frame;
    if (sourceFrame) {
        if (sourceFrame->format != AV_PIX_FMT_VULKAN && sourceFrame->format != AV_PIX_FMT_D3D11) return px::CloneAvFrame(*sourceFrame);
        auto downloaded = px::AllocateAvFrame();
        if (!sourceFrame->hw_frames_ctx || !downloaded || av_hwframe_transfer_data(downloaded.get(), sourceFrame.get(), 0) < 0) return {};
        if (av_frame_copy_props(downloaded.get(), sourceFrame.get()) < 0) return {};
        return downloaded;
    }
    AVPixelFormat pixelFormat{AV_PIX_FMT_NONE};
    switch (image->Format()) {
        case px::kRawImageNV12:
            pixelFormat = AV_PIX_FMT_NV12;
            break;
        case px::kRawImageI420:
            pixelFormat = AV_PIX_FMT_YUV420P;
            break;
        case px::kRawImageI444:
            pixelFormat = AV_PIX_FMT_YUV444P;
            break;
        case px::kRawImageRGB:
            pixelFormat = AV_PIX_FMT_RGB24;
            break;
        case px::kRawImageRGBA:
            pixelFormat = AV_PIX_FMT_RGBA;
            break;
        default:
            return {};
    }
    auto software = px::AllocateAvFrame();
    if (!software) return {};
    software->format = pixelFormat;
    software->width = image->img_width;
    software->height = image->img_height;
    const bool rgb{pixelFormat == AV_PIX_FMT_RGB24 || pixelFormat == AV_PIX_FMT_RGBA};
    // RawImage's full_color_ describes 4:4:4 chroma, not full-range luminance. Match the YUV presenter.
    software->color_range = rgb ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;
    software->colorspace = rgb ? AVCOL_SPC_RGB : AVCOL_SPC_BT709;
    if (av_frame_get_buffer(software.get(), 32) < 0) return {};
    for (std::size_t planeIndex{}; planeIndex < 3U; ++planeIndex) {
        const auto layout = image->Layout(planeIndex);
        if (!layout) break;
        const auto planeBytes = image->Plane(planeIndex);
        if (!software->data[planeIndex] || software->linesize[planeIndex] < layout->stride ||
            planeBytes.size() < static_cast<std::size_t>(layout->stride) * layout->rows)
            return {};
        for (int rowIndex{}; rowIndex < layout->rows; ++rowIndex) {
            // FFmpeg-owned plane pointers are used only at this synchronous copy boundary.
            std::memcpy(software->data[planeIndex] + static_cast<std::ptrdiff_t>(rowIndex) * software->linesize[planeIndex],
                        planeBytes.data() + static_cast<std::size_t>(rowIndex) * layout->stride, layout->stride);
        }
    }
    return software;
}

std::vector<std::uint8_t> ReadBgra(const ClientVideoFrame& frame) {
    const std::size_t byteCount{static_cast<std::size_t>(frame.width) * frame.height * 4U};
    if (!frame.bgra.empty()) return frame.bgra.size() >= byteCount ? frame.bgra : std::vector<std::uint8_t>{};
    const auto source = SoftwareFrame(frame.native);
    if (!source || source->width != frame.width || source->height != frame.height || !source->data[0]) return {};
    auto converted = px::AllocateAvFrame();
    if (!converted) return {};
    converted->format = AV_PIX_FMT_BGRA;
    converted->width = source->width;
    converted->height = source->height;
    const std::unique_ptr<SwsContext, ScaleContextDeleter> converter{sws_alloc_context()};
    if (!converter || sws_scale_frame(converter.get(), converted.get(), source.get()) < 0) return {};
    if (!converted->data[0] || converted->linesize[0] < frame.width * 4) return {};
    std::vector<std::uint8_t> pixels(byteCount);
    for (int rowIndex{}; rowIndex < frame.height; ++rowIndex) {
        // The converted frame owns its padded FFmpeg row storage until the copy completes.
        std::memcpy(pixels.data() + static_cast<std::size_t>(rowIndex) * frame.width * 4U,
                    converted->data[0] + static_cast<std::ptrdiff_t>(rowIndex) * converted->linesize[0], frame.width * 4U);
    }
    return pixels;
}

}  // namespace

std::optional<std::filesystem::path> DefaultScreenshotDirectory() {
    // SDL borrows the redirected Windows Documents known-folder path; never retain its ABI pointer.
    if (!SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS)) return std::nullopt;
    return std::filesystem::u8path(SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS)) / px::ui::StorageDirectoryName() / "Screenshots";
}

ScreenshotResult SaveScreenshotFile(const std::shared_ptr<ClientVideoFrame>& frame, const std::filesystem::path& path) {
    if (!frame || frame->width <= 0 || frame->height <= 0) return {.status = ScreenshotStatus::NoFrame, .path = path};
    constexpr int maximumDimension{16384};
    if (frame->width > maximumDimension || frame->height > maximumDimension) return {.status = ScreenshotStatus::ReadbackFailed, .path = path};
    std::error_code directoryError{};
    std::filesystem::create_directories(path.parent_path(), directoryError);
    if (directoryError) return {.status = ScreenshotStatus::DirectoryUnavailable, .path = path, .error = directoryError.message()};
    auto pixels = ReadBgra(*frame);
    if (pixels.empty()) return {.status = ScreenshotStatus::ReadbackFailed, .path = path};
    const std::unique_ptr<SDL_Surface, SurfaceDeleter> surface{
        SDL_CreateSurfaceFrom(frame->width, frame->height, SDL_PIXELFORMAT_BGRA32, pixels.data(), frame->width * 4)};
    const auto encodedPath = path.u8string();
    // SDL requires UTF-8 even on Windows; std::filesystem::path::string() would corrupt non-ASCII user directories.
    if (!surface || !SDL_SaveBMP(surface.get(), reinterpret_cast<const char*>(encodedPath.c_str())))
        return {.status = ScreenshotStatus::WriteFailed, .path = path, .error = SDL_GetError()};
    return {.status = ScreenshotStatus::Saved, .path = path};
}

}  // namespace px::client::imgui
