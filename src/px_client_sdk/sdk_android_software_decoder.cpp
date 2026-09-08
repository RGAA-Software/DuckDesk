#include "sdk_android_software_decoder.h"
#include "ffmpeg_frame_adapter.h"

#ifdef ANDROID

#include <android/native_window.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <thread>
#include <utility>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}

#include "gl/raw_image.h"
#include "px_common/log.h"
#include "px_common/time_util.h"
#include "px_message.pb.h"
#include "sdk_statistics.h"

namespace px {
namespace {

struct CodecContextRelease final {
    void operator()(AVCodecContext* context) const noexcept { // NOLINT(gammaray-raw-pointer-boundary)
        if (context != nullptr) avcodec_free_context(&context);
    }
};

struct PacketRelease final {
    void operator()(AVPacket* packet) const noexcept { // NOLINT(gammaray-raw-pointer-boundary)
        if (packet != nullptr) av_packet_free(&packet);
    }
};

struct FrameRelease final {
    void operator()(AVFrame* frame) const noexcept { // NOLINT(gammaray-raw-pointer-boundary)
        if (frame != nullptr) av_frame_free(&frame);
    }
};

struct ScaleContextRelease final {
    void operator()(SwsContext* context) const noexcept { // NOLINT(gammaray-raw-pointer-boundary)
        if (context != nullptr) sws_freeContext(context);
    }
};

class LockedNativeWindow final {
public:
    explicit LockedNativeWindow(std::shared_ptr<ANativeWindow> window) : window_(std::move(window)) {}

    ~LockedNativeWindow() {
        if (locked_) static_cast<void>(ANativeWindow_unlockAndPost(window_.get()));
    }

    LockedNativeWindow(const LockedNativeWindow&) = delete;
    LockedNativeWindow& operator=(const LockedNativeWindow&) = delete;

    [[nodiscard]] bool Lock() {
        locked_ = ANativeWindow_lock(window_.get(), &buffer_, nullptr) == 0; // NOLINT(gammaray-raw-pointer-boundary)
        return locked_;
    }

    [[nodiscard]] std::span<std::byte> Pixels() const {
        if (!locked_ || buffer_.bits == nullptr || buffer_.stride <= 0 || buffer_.height <= 0) return {};
        const auto byte_count = static_cast<std::size_t>(buffer_.stride) * static_cast<std::size_t>(buffer_.height) * 4U;
        return {reinterpret_cast<std::byte*>(buffer_.bits), byte_count}; // NOLINT(gammaray-raw-pointer-boundary)
    }

    [[nodiscard]] int StrideBytes() const { return buffer_.stride * 4; }

private:
    std::shared_ptr<ANativeWindow> window_{};
    ANativeWindow_Buffer buffer_{};
    bool locked_{};
};

} // namespace

class AndroidSoftwareVideoDecoder::State final {
public:
    [[nodiscard]] bool Initialize(const AVCodecID codec_id, std::shared_ptr<ANativeWindow> window) {
        const auto codec_handle = reinterpret_cast<std::uintptr_t>(avcodec_find_decoder(codec_id));
        if (codec_handle == 0U) return false;
        codec_context_.reset(avcodec_alloc_context3(reinterpret_cast<const AVCodec*>(codec_handle))); // NOLINT(gammaray-raw-pointer-boundary)
        if (!codec_context_) return false;
        codec_context_->thread_count = std::clamp(static_cast<int>(std::thread::hardware_concurrency()), 1, 8);
        codec_context_->thread_type = FF_THREAD_SLICE;
        codec_context_->flags |= AV_CODEC_FLAG_LOW_DELAY;
        if (avcodec_open2(codec_context_.get(),
                          reinterpret_cast<const AVCodec*>(codec_handle), // NOLINT(gammaray-raw-pointer-boundary)
                          nullptr) < 0) {
            return false;
        }
        packet_.reset(av_packet_alloc());
        frame_.reset(av_frame_alloc());
        window_ = std::move(window);
        return packet_ && frame_ && window_;
    }

    [[nodiscard]] bool UpdateSurface(std::shared_ptr<ANativeWindow> replacement) {
        if (!replacement) return false;
        window_ = std::move(replacement);
        window_width_ = 0;
        window_height_ = 0;
        return true;
    }

    [[nodiscard]] Result<std::shared_ptr<RawImage>, int> Decode(const std::span<const std::uint8_t> encoded) {
        if (!codec_context_ || !packet_ || !frame_ || !window_ || encoded.empty()) return TRError(-1);
        if (!PrepareDecoderPacket(*packet_, encoded)) return TRError(-1);
        const auto send_result = avcodec_send_packet(codec_context_.get(), packet_.get());
        if (send_result < 0 && send_result != AVERROR(EAGAIN)) return TRError(send_result);

        const auto receive_result = avcodec_receive_frame(codec_context_.get(), frame_.get());
        if (receive_result == AVERROR(EAGAIN) || receive_result == AVERROR_EOF) return TRError(0);
        if (receive_result < 0) return TRError(receive_result);
        if (!Render()) return TRError(-1);
        return RawImage::MakePresented(frame_->width, frame_->height);
    }

private:
    [[nodiscard]] bool Render() {
        if (frame_->width <= 0 || frame_->height <= 0 || frame_->format < 0) return false;
        const auto source_format = static_cast<AVPixelFormat>(frame_->format);
        if (!EnsureScaler(frame_->width, frame_->height, source_format)) return false;
        if ((window_width_ != frame_->width || window_height_ != frame_->height) &&
            ANativeWindow_setBuffersGeometry(window_.get(), frame_->width, frame_->height, WINDOW_FORMAT_RGBA_8888) != 0) {
            return false;
        }
        window_width_ = frame_->width;
        window_height_ = frame_->height;
        auto locked = std::make_unique<LockedNativeWindow>(window_);
        if (!locked->Lock()) return false;
        auto pixels = locked->Pixels();
        if (pixels.empty()) return false;
        std::array<std::uint8_t*, 4> destinations{ // NOLINT(gammaray-raw-pointer-boundary)
            reinterpret_cast<std::uint8_t*>(pixels.data()), nullptr, nullptr, nullptr};
        const std::array<int, 4> strides{locked->StrideBytes(), 0, 0, 0};
        return sws_scale(scaler_.get(), frame_->data, frame_->linesize, 0, frame_->height, destinations.data(), strides.data()) == frame_->height;
    }

    [[nodiscard]] bool EnsureScaler(const int width, const int height, const AVPixelFormat format) {
        if (scaler_ && scaler_width_ == width && scaler_height_ == height && scaler_format_ == format) return true;
        scaler_.reset(sws_getContext(width, height, format, width, height, AV_PIX_FMT_RGBA, SWS_FAST_BILINEAR, nullptr, nullptr, nullptr));
        scaler_width_ = width;
        scaler_height_ = height;
        scaler_format_ = format;
        return scaler_ != nullptr;
    }

    std::unique_ptr<AVCodecContext, CodecContextRelease> codec_context_{};
    std::unique_ptr<AVPacket, PacketRelease> packet_{};
    std::unique_ptr<AVFrame, FrameRelease> frame_{};
    std::unique_ptr<SwsContext, ScaleContextRelease> scaler_{};
    std::shared_ptr<ANativeWindow> window_{};
    int window_width_{};
    int window_height_{};
    int scaler_width_{};
    int scaler_height_{};
    AVPixelFormat scaler_format_{AV_PIX_FMT_NONE};
};

AndroidSoftwareVideoDecoder::AndroidSoftwareVideoDecoder(const std::shared_ptr<ThunderSdk>& sdk, std::shared_ptr<AndroidVideoOutput> output)
    : VideoDecoder(sdk), output_(std::move(output)) {}

AndroidSoftwareVideoDecoder::~AndroidSoftwareVideoDecoder() {
    Release();
}

int AndroidSoftwareVideoDecoder::Init(const std::string& monitor_name, const int codec_type, const int width, const int height,
                                      const std::string&,
                                      const int image_format, const bool ignore_hardware) {
    const auto window = output_ ? output_->Snapshot() : std::shared_ptr<ANativeWindow>{};
    if (inited_ || width <= 0 || height <= 0 || !window) return -1;
    const auto codec_id = codec_type == VideoType::kNetH264 ? AV_CODEC_ID_H264
                         : codec_type == VideoType::kNetHevc ? AV_CODEC_ID_HEVC
                                                            : AV_CODEC_ID_NONE;
    if (codec_id == AV_CODEC_ID_NONE) return -1;
    auto state = std::make_unique<State>();
    if (!state->Initialize(codec_id, window)) {
        return -1;
    }
    monitor_name_ = monitor_name;
    codec_type_ = codec_type;
    frame_width_ = width;
    frame_height_ = height;
    img_format_ = image_format;
    ignore_hw_decoder_ = ignore_hardware;
    sdk_stat_->video_format_.Update(codec_id == AV_CODEC_ID_H264 ? "H264" : "HEVC");
    sdk_stat_->video_decoder_.Update(codec_id == AV_CODEC_ID_H264 ? "FFmpeg software H.264" : "FFmpeg software HEVC");
    state_ = std::move(state);
    inited_ = true;
    stop_ = false;
    return 0;
}

Result<std::shared_ptr<RawImage>, int> AndroidSoftwareVideoDecoder::Decode(std::span<const std::uint8_t> encoded) {
    if (!state_ || stop_ || encoded.empty()) return TRError(-1);
    std::lock_guard lock(decode_mtx_);
    const auto started_at = TimeUtil::GetCurrentTimestamp();
    auto result = state_->Decode(encoded);
    if (result.has_value()) sdk_stat_->AppendDecodeDuration(monitor_name_, TimeUtil::GetCurrentTimestamp() - started_at);
    return result;
}

void AndroidSoftwareVideoDecoder::Release() {
    std::lock_guard lock(decode_mtx_);
    stop_ = true;
    inited_ = false;
    state_.reset();
}

bool AndroidSoftwareVideoDecoder::RefreshOutput() {
    std::lock_guard lock(decode_mtx_);
    return state_ && output_ && state_->UpdateSurface(output_->Snapshot());
}

bool AndroidSoftwareVideoDecoder::Ready() {
    return inited_ && state_ != nullptr;
}

} // namespace px

#endif
