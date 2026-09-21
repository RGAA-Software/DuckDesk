//
// Created by hy on 2024/4/26.
//

#ifndef TEST_WEBRTC_VIDEO_SOURCE_MOCK_H
#define TEST_WEBRTC_VIDEO_SOURCE_MOCK_H

#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#include <mutex>
#include <optional>

#include "px_common/log.h"
#include "px_webrtc_client/webrtc_helper.h"

namespace px {

class NotifyFrameFrameBuffer : public webrtc::VideoFrameBuffer {
public:
    NotifyFrameFrameBuffer(const std::string& mon_name, uint64_t frame_idx, int width, int height, uint64_t handle, int64_t adapter_uid,
                           uint64_t frame_format, bool stream_reset) {
        this->monitor_name_ = mon_name;
        this->frame_idx_ = frame_idx;
        this->width_ = width;
        this->height_ = height;
        this->handle_ = handle;
        this->adapter_uid_ = adapter_uid;
        this->frame_format_ = frame_format;
        this->stream_reset_ = stream_reset;
    }

    [[nodiscard]] Type type() const override { return webrtc::VideoFrameBuffer::Type::kNative; }

    rtc::scoped_refptr<webrtc::I420BufferInterface> ToI420() override { return nullptr; }

    [[nodiscard]] int width() const override { return width_; }

    [[nodiscard]] int height() const override { return height_; }

    [[nodiscard]] uint64_t GetHandle() { return handle_; }

    [[nodiscard]] int64_t GetAdapterUid() { return adapter_uid_; }

    [[nodiscard]] uint64_t GetFrameFormat() const { return frame_format_; }

    // 采集该帧的显示器名:切屏检测/编码帧按屏名匹配用
    [[nodiscard]] const std::string& GetMonName() { return monitor_name_; }

    [[nodiscard]] uint64_t GetFrameIdx() const { return frame_idx_; }

    [[nodiscard]] bool IsStreamReset() const { return stream_reset_; }

private:
    std::string monitor_name_;
    uint64_t frame_idx_ = 0;
    int width_ = 0;
    int height_ = 0;
    uint64_t handle_ = 0;
    int64_t adapter_uid_ = 0;
    uint64_t frame_format_ = 0;
    bool stream_reset_ = false;
};

class VideoSourceImpl : public rtc::VideoSourceInterface<webrtc::VideoFrame> {
public:
    VideoSourceImpl() = default;

    void OnNotifyFrame(const webrtc::VideoFrame& notify_frame);
    bool ReplayLatestNotification();

private:
    void AddOrUpdateSink(rtc::VideoSinkInterface<webrtc::VideoFrame>* sink,  // NOLINT(pixels-raw-pointer-boundary): libwebrtc observer ABI
                         const rtc::VideoSinkWants& wants) override;

    void RemoveSink(rtc::VideoSinkInterface<webrtc::VideoFrame>* sink) override;  // NOLINT(pixels-raw-pointer-boundary): libwebrtc observer ABI

private:
    rtc::VideoBroadcaster broadcaster_;
    cricket::VideoAdapter video_adapter_;
    std::mutex latest_frame_mutex_;
    std::optional<webrtc::VideoFrame> latest_frame_;
};

////

class VideoTrackSourceImpl : public webrtc::VideoTrackSource {
public:
    explicit VideoTrackSourceImpl(const std::shared_ptr<rtc::VideoSourceInterface<webrtc::VideoFrame>>& source)
        : webrtc::VideoTrackSource(false), source_(source) {}

    bool HasMockVideoSource() { return source_ != nullptr && std::dynamic_pointer_cast<VideoSourceImpl>(source_) != nullptr; }

    auto AsVideoSourceMock() { return std::dynamic_pointer_cast<VideoSourceImpl>(source_); }

protected:
    rtc::VideoSourceInterface<webrtc::VideoFrame>* source() override {  // NOLINT(pixels-raw-pointer-boundary): libwebrtc source ABI
        return source_.get();
    }

public:
    std::shared_ptr<rtc::VideoSourceInterface<webrtc::VideoFrame>> source_ = nullptr;
};

////
class VideoStreamReceiver : public rtc::VideoSinkInterface<webrtc::VideoFrame> {
public:
    void OnFrame(const webrtc::VideoFrame& frame) override { std::cout << "[info] received a frame, id:" << frame.id() << std::endl; }
};

}  // namespace px

#endif  // TEST_WEBRTC_VIDEO_SOURCE_MOCK_H
