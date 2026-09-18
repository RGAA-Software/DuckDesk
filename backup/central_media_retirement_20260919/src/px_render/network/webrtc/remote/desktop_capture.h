//
// Created by hy on 20/07/2024.
//

#ifndef WEBRTC_CLIENT_DESKTOP_CAPTURE_H
#define WEBRTC_CLIENT_DESKTOP_CAPTURE_H

#include "px_webrtc_client/webrtc_helper.h"
#include "desktop_capture_source.h"

namespace px
{

    class DesktopCapture : public rtc::VideoSourceInterface<webrtc::VideoFrame> {
    public:
        static std::shared_ptr<DesktopCapture> Create(size_t target_fps, size_t capture_screen_index);
        ~DesktopCapture() override;
        void StartCapture();
        void StopCapture();

        void AddOrUpdateSink(rtc::VideoSinkInterface<webrtc::VideoFrame> *sink, const rtc::VideoSinkWants &wants) override;
        void RemoveSink(rtc::VideoSinkInterface<webrtc::VideoFrame> *sink) override;

    private:
        DesktopCapture();

        bool Init(size_t target_fps, size_t capture_screen_index);
        struct State;

    private:
        std::shared_ptr<State> state_;
        std::thread capture_thread_;
    };

}


#endif //WEBRTC_CLIENT_DESKTOP_CAPTURE_H
