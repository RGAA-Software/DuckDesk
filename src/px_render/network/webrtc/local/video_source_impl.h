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
#include "px_common/log.h"
#include "px_webrtc_client/webrtc_helper.h"

namespace px
{

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

        [[nodiscard]] Type type() const override {
            return webrtc::VideoFrameBuffer::Type::kNative;
        }

        rtc::scoped_refptr<webrtc::I420BufferInterface> ToI420() override {
            return nullptr;
        }

        [[nodiscard]] int width() const override {
            return width_;
        }

        [[nodiscard]] int height() const override {
            return height_;
        }

        [[nodiscard]] uint64_t GetHandle() {
            return handle_;
        }

        [[nodiscard]] int64_t GetAdapterUid() {
            return adapter_uid_;
        }

        [[nodiscard]] uint64_t GetFrameFormat() const {
            return frame_format_;
        }

        // 采集该帧的显示器名:切屏检测/编码帧按屏名匹配用
        [[nodiscard]] const std::string& GetMonName() {
            return monitor_name_;
        }

        [[nodiscard]] uint64_t GetFrameIdx() const {
            return frame_idx_;
        }

        [[nodiscard]] bool IsStreamReset() const {
            return stream_reset_;
        }

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

        void OnNotifyFrame(webrtc::VideoFrame& notify_frame) {
            // 诊断:broadcaster 会按下游(VideoStreamEncoder)上报的 VideoSinkWants
            // 静默丢帧——若 wants.max_framerate_fps 被协商/适配压低,采集 60fps
            // 在这里就被砍半,Encode 消费节奏随之下降。定期打出当前 wants 与
            // 推帧计数,与编码器 "Encode call #N" / "sent encoded frame" 对照。
            static std::atomic_uint64_t push_count = 0;
            auto cnt = ++push_count;
            if (cnt == 1 || cnt % 300 == 0) {
                auto wants = broadcaster_.wants();
                LOGI("VideoSource push #{}, wants: active={}, max_fps={}, max_px={}, target_px={}, black={}",
                     cnt, wants.is_active,
                     wants.max_framerate_fps == std::numeric_limits<int>::max() ? -1 : wants.max_framerate_fps,
                     wants.max_pixel_count == std::numeric_limits<int>::max() ? -1 : wants.max_pixel_count,
                     wants.target_pixel_count.has_value() ? wants.target_pixel_count.value() : -1,
                     wants.black_frames);
            }
            broadcaster_.OnFrame(notify_frame);
        }

    private:
        void AddOrUpdateSink(rtc::VideoSinkInterface<webrtc::VideoFrame> *sink,
                             const rtc::VideoSinkWants &wants) override {
            broadcaster_.AddOrUpdateSink(sink, wants);
            (void)video_adapter_;
            LOGI("AddOrUpdateSink");
        }

        void RemoveSink(rtc::VideoSinkInterface<webrtc::VideoFrame> *sink) override {
            broadcaster_.RemoveSink(sink);
            (void) video_adapter_;
        }

    private:
        rtc::VideoBroadcaster broadcaster_;
        cricket::VideoAdapter video_adapter_;

    };

    ////

    class VideoTrackSourceImpl : public webrtc::VideoTrackSource {
    public:
        explicit VideoTrackSourceImpl(
            const std::shared_ptr<rtc::VideoSourceInterface<webrtc::VideoFrame>>& source)
            : webrtc::VideoTrackSource(false), source_(source) {}

        bool HasMockVideoSource() {
            return source_ != nullptr && std::dynamic_pointer_cast<VideoSourceImpl>(source_) != nullptr;
        }

        auto AsVideoSourceMock() {
            return std::dynamic_pointer_cast<VideoSourceImpl>(source_);
        }

    protected:
        rtc::VideoSourceInterface<webrtc::VideoFrame> *source() override {
            return source_.get();
        }

    public:
        std::shared_ptr<rtc::VideoSourceInterface<webrtc::VideoFrame>> source_ = nullptr;
    };

    ////
    class VideoStreamReceiver : public rtc::VideoSinkInterface<webrtc::VideoFrame> {
    public:
        void OnFrame(const webrtc::VideoFrame& frame) override {
            std::cout<<"[info] received a frame, id:"<<frame.id()<<std::endl;
        }
    };

}

#endif //TEST_WEBRTC_VIDEO_SOURCE_MOCK_H
