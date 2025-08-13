//
// Created by hy on 2024/4/26.
//

#ifndef TEST_WEBRTC_VIDEO_SOURCE_MOCK_H
#define TEST_WEBRTC_VIDEO_SOURCE_MOCK_H

#include <chrono>
#include <fstream>
#include <iostream>
#include "tc_common_new/log.h"
#include "tc_common_new/webrtc_helper.h"

namespace tc
{

    class RtcLocalPlugin;

    class NotifyFrameFrameBuffer : public webrtc::VideoFrameBuffer {
    public:
        NotifyFrameFrameBuffer(uint64_t frame_idx, int width, int height, uint64_t handle) {
            this->frame_idx_ = frame_idx;
            this->width_ = width;
            this->height_ = height;
            this->handle_ = handle;
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

    private:
        uint64_t frame_idx_ = 0;
        int width_ = 0;
        int height_ = 0;
        uint64_t handle_ = 0;
    };

    class VideoSourceImpl : public rtc::VideoSourceInterface<webrtc::VideoFrame> {
    public:
        VideoSourceImpl(RtcLocalPlugin* plugin) {
            plugin_ = plugin;
        }

        void OnNotifyFrame(webrtc::VideoFrame& notify_frame) {
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
        RtcLocalPlugin* plugin_ = nullptr;
        rtc::VideoBroadcaster broadcaster_;
        cricket::VideoAdapter video_adapter_;

    };

    ////

    class VideoTrackSourceImpl : public webrtc::VideoTrackSource {
    public:
        VideoTrackSourceImpl(RtcLocalPlugin* plugin, const std::shared_ptr<rtc::VideoSourceInterface<webrtc::VideoFrame>>& source) : webrtc::VideoTrackSource(false) {
            this->plugin_ = plugin;
            this->source_ = source;
        }

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
        RtcLocalPlugin* plugin_;
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
