#include <SDL3/SDL.h>
#include <Windows.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>

#include "client_screenshot.h"
#include "px_client_sdk/platform/windows/windows_video_frame.h"

extern "C" {
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d11va.h>
}

namespace px::client::imgui {
namespace {

struct SurfaceDeleter final {
    void operator()(SDL_Surface* surface) const noexcept {  // NOLINT(pixels-raw-pointer-boundary): SDL destruction ABI.
        SDL_DestroySurface(surface);
    }
};

class ClientScreenshotTest : public testing::Test {
protected:
    std::filesystem::path directory_{std::filesystem::temp_directory_path() /
                                     ("pixels-screenshot-test-" + std::to_string(GetCurrentProcessId()) + "-" +
                                      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())) /
                                     L"截图 测试"};
    std::filesystem::path target_{directory_ / L"画面.bmp"};

    void TearDown() override {
        // Only the unique test-owned directory is removed, never a product screenshot directory.
        std::error_code cleanupError{};
        std::filesystem::remove_all(directory_.parent_path(), cleanupError);
    }

    void CheckSavedImage(const int width, const int height, const bool checkRed = true) {
        const auto encodedPath = target_.u8string();
        const std::unique_ptr<SDL_Surface, SurfaceDeleter> surface{SDL_LoadBMP(reinterpret_cast<const char*>(encodedPath.c_str()))};
        ASSERT_TRUE(surface) << SDL_GetError();
        EXPECT_EQ(surface->w, width);
        EXPECT_EQ(surface->h, height);
        Uint8 red{}, green{}, blue{}, alpha{};
        ASSERT_TRUE(SDL_ReadSurfacePixel(surface.get(), width - 1, height - 1, &red, &green, &blue, &alpha));
        if (checkRed) {
            EXPECT_EQ(red, 255);
            EXPECT_EQ(green, 0);
            EXPECT_EQ(blue, 0);
        } else {
            EXPECT_NEAR(red, 255, 2);
            EXPECT_NEAR(green, 255, 2);
            EXPECT_NEAR(blue, 255, 2);
        }
    }
};

TEST_F(ClientScreenshotTest, SavesRdpBgraToUnicodePath) {
    auto frame = std::make_shared<ClientVideoFrame>(ClientVideoFrame{.width = 2, .height = 2});
    frame->bgra = {0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255};
    EXPECT_EQ(SaveScreenshotFile(frame, target_).status, ScreenshotStatus::Saved);
    CheckSavedImage(2, 2);
}

TEST_F(ClientScreenshotTest, SavesNativeSoftwareRgbaFrame) {
    auto image = px::RawImage::Make(px::kRawImageRGBA, 3, 2);
    ASSERT_TRUE(image);
    for (std::size_t pixelOffset{}; pixelOffset < image->MutableBytes().size(); pixelOffset += 4U) {
        image->MutableBytes()[pixelOffset] = static_cast<char>(255);
        image->MutableBytes()[pixelOffset + 3U] = static_cast<char>(255);
    }
    EXPECT_EQ(SaveScreenshotFile(RetainVideoFrame(image), target_).status, ScreenshotStatus::Saved);
    CheckSavedImage(3, 2);
}

TEST_F(ClientScreenshotTest, SavesRetainedAvFrameWithPaddedRowsAfterSourceIsReleased) {
    auto source = px::AllocateAvFrame();
    ASSERT_TRUE(source);
    source->format = AV_PIX_FMT_BGRA;
    source->width = 3;
    source->height = 2;
    ASSERT_EQ(av_frame_get_buffer(source.get(), 32), 0);
    for (int rowIndex{}; rowIndex < source->height; ++rowIndex) {
        for (int columnIndex{}; columnIndex < source->width; ++columnIndex) {
            source->data[0][rowIndex * source->linesize[0] + columnIndex * 4] = 0;
            source->data[0][rowIndex * source->linesize[0] + columnIndex * 4 + 1] = 0;
            source->data[0][rowIndex * source->linesize[0] + columnIndex * 4 + 2] = 255;
            source->data[0][rowIndex * source->linesize[0] + columnIndex * 4 + 3] = 255;
        }
    }
    const auto frame = RetainVideoFrame(px::MakeVulkanImage(*source));
    source.reset();
    EXPECT_EQ(SaveScreenshotFile(frame, target_).status, ScreenshotStatus::Saved);
    CheckSavedImage(3, 2);
}

TEST_F(ClientScreenshotTest, SavesSoftwareYuvFormatsAtPresenterColorRange) {
    for (const auto format : {px::kRawImageNV12, px::kRawImageI420, px::kRawImageI444}) {
        const auto image = px::RawImage::Make(format, 3, 3);
        ASSERT_TRUE(image);
        std::ranges::fill(image->MutablePlane(0), static_cast<char>(235));
        std::ranges::fill(image->MutablePlane(1), static_cast<char>(128));
        std::ranges::fill(image->MutablePlane(2), static_cast<char>(128));
        EXPECT_EQ(SaveScreenshotFile(RetainVideoFrame(image), target_).status, ScreenshotStatus::Saved);
        CheckSavedImage(3, 3, false);
    }
}

TEST_F(ClientScreenshotTest, SavesD3d11HardwareNv12Frame) {
    Microsoft::WRL::ComPtr<ID3D11Device> nativeDevice{};
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> nativeContext{};
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_VIDEO_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
                                 nativeDevice.GetAddressOf(), nullptr, nativeContext.GetAddressOf())))
        GTEST_SKIP() << "D3D11 hardware device is unavailable";
    auto device = px::AvBufferPtr{av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA), px::AvBufferDeleter{}};
    ASSERT_TRUE(device);
    auto& deviceContext = *reinterpret_cast<AVHWDeviceContext*>(device->data);
    auto& hardwareContext = *reinterpret_cast<AVD3D11VADeviceContext*>(deviceContext.hwctx);
    // FFmpeg assumes COM ownership at this device initialization ABI boundary.
    hardwareContext.device = nativeDevice.Detach();
    hardwareContext.device_context = nativeContext.Detach();
    if (av_hwdevice_ctx_init(device.get()) < 0) GTEST_SKIP() << "D3D11 FFmpeg device initialization is unavailable";
    auto frames = px::AvBufferPtr{av_hwframe_ctx_alloc(device.get()), px::AvBufferDeleter{}};
    ASSERT_TRUE(frames);
    // FFmpeg owns this context; use its fields only at the initialization boundary.
    auto& framesContext = *reinterpret_cast<AVHWFramesContext*>(frames->data);
    framesContext.format = AV_PIX_FMT_D3D11;
    framesContext.sw_format = AV_PIX_FMT_NV12;
    framesContext.width = 16;
    framesContext.height = 16;
    if (av_hwframe_ctx_init(frames.get()) < 0) GTEST_SKIP() << "D3D11 NV12 staging is unavailable";
    auto hardware = px::AllocateAvFrame();
    auto software = px::AllocateAvFrame();
    ASSERT_TRUE(hardware);
    ASSERT_TRUE(software);
    ASSERT_EQ(av_hwframe_get_buffer(frames.get(), hardware.get(), 0), 0);
    software->format = AV_PIX_FMT_NV12;
    software->width = 16;
    software->height = 16;
    ASSERT_EQ(av_frame_get_buffer(software.get(), 32), 0);
    for (int rowIndex{}; rowIndex < 16; ++rowIndex) std::memset(software->data[0] + rowIndex * software->linesize[0], 235, 16);
    for (int rowIndex{}; rowIndex < 8; ++rowIndex) std::memset(software->data[1] + rowIndex * software->linesize[1], 128, 16);
    ASSERT_EQ(av_hwframe_transfer_data(hardware.get(), software.get(), 0), 0);
    auto storage = std::make_shared<px::D3D11Image>();
    storage->source_frame = px::CloneAvFrame(*hardware);
    const auto frame = RetainVideoFrame(px::RawImage::MakePlatform(px::kRawImageD3D11Texture, 16, 16, storage));
    hardware.reset();
    software.reset();
    frames.reset();
    device.reset();
    EXPECT_EQ(SaveScreenshotFile(frame, target_).status, ScreenshotStatus::Saved);
    CheckSavedImage(16, 16, false);
}

TEST_F(ClientScreenshotTest, ReportsNoFrameOrInvalidFrameWithoutWritingFile) {
    EXPECT_EQ(SaveScreenshotFile({}, target_).status, ScreenshotStatus::NoFrame);
    const auto frame = std::make_shared<ClientVideoFrame>(ClientVideoFrame{.width = 2, .height = 2, .bgra = {0}});
    EXPECT_EQ(SaveScreenshotFile(frame, target_).status, ScreenshotStatus::ReadbackFailed);
    const auto emptyAvFrame = px::AllocateAvFrame();
    ASSERT_TRUE(emptyAvFrame);
    emptyAvFrame->format = AV_PIX_FMT_BGRA;
    emptyAvFrame->width = 2;
    emptyAvFrame->height = 2;
    const auto emptyStorage = std::make_shared<px::VulkanImage>();
    emptyStorage->frame = emptyAvFrame;
    const auto emptyNative = px::RawImage::MakePlatform(px::kRawImageVulkanAVFrame, 2, 2, emptyStorage);
    EXPECT_EQ(SaveScreenshotFile(RetainVideoFrame(emptyNative), target_).status, ScreenshotStatus::ReadbackFailed);
    EXPECT_FALSE(std::filesystem::exists(target_));
}

TEST_F(ClientScreenshotTest, ReportsUnavailableDirectoryAndWriteFailureSeparately) {
    std::filesystem::create_directories(directory_);
    const auto blocker = directory_ / "not-a-directory";
    { std::ofstream blockingFile{blocker}; }
    const auto frame = std::make_shared<ClientVideoFrame>(ClientVideoFrame{.width = 1, .height = 1, .bgra = {0, 0, 255, 255}});
    const auto directoryFailure = SaveScreenshotFile(frame, blocker / "screenshot.bmp");
    EXPECT_EQ(directoryFailure.status, ScreenshotStatus::DirectoryUnavailable);
    EXPECT_FALSE(directoryFailure.error.empty());
    std::filesystem::create_directory(target_);
    const auto writeFailure = SaveScreenshotFile(frame, target_);
    EXPECT_EQ(writeFailure.status, ScreenshotStatus::WriteFailed);
    EXPECT_FALSE(writeFailure.error.empty());
}

}  // namespace
}  // namespace px::client::imgui
