//
// Created by hy on 2024/4/26.
//

#ifndef TEST_WEBRTC_VIDEO_SOURCE_MOCK_H
#define TEST_WEBRTC_VIDEO_SOURCE_MOCK_H

#include <chrono>
#include <fstream>
#include <iostream>
#include "tc_common_new/webrtc_helper.h"
#include "tc_common_new/log.h"

namespace tc
{

    class NotifyFrameFrameBuffer : public webrtc::VideoFrameBuffer
    {
    public:
        NotifyFrameFrameBuffer(int width, int height) : mWidth(width), mHeight(height) {}
        virtual Type type() const { return webrtc::VideoFrameBuffer::Type::kNative; }
        virtual rtc::scoped_refptr<webrtc::I420BufferInterface> ToI420() { return nullptr; }
        virtual int width() const { return mWidth; }
        virtual int height() const { return mHeight; }
        int mWidth;
        int mHeight;
    };

    class VideoSourceImpl : public rtc::VideoSourceInterface<webrtc::VideoFrame> {
    public:
        VideoSourceImpl() {
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
        rtc::VideoBroadcaster broadcaster_;
        cricket::VideoAdapter video_adapter_;

    };

    ////

    class VideoTrackSourceImpl : public webrtc::VideoTrackSource {
    public:
        VideoTrackSourceImpl(const std::shared_ptr<rtc::VideoSourceInterface<webrtc::VideoFrame>>& source) : webrtc::VideoTrackSource(false) {
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
