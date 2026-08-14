//
// Created by RGAA on 10/08/2026.
//

#ifndef GAMMARAY_RTC_VIDEO_SINK_H
#define GAMMARAY_RTC_VIDEO_SINK_H

#include <memory>
#include <functional>
#include "tc_common_new/webrtc_helper.h"

namespace tc
{

    class Data;

    // pulls decoded frames off a webrtc video track,
    // converts them to packed I420 and forwards them to the upper layer
    class RtcVideoSink : public rtc::VideoSinkInterface<webrtc::VideoFrame> {
    public:
        static std::shared_ptr<RtcVideoSink> Make();

        void OnFrame(const webrtc::VideoFrame& frame) override;

        void SetOnFrameCallback(std::function<void(int w, int h, std::shared_ptr<Data> i420)>&& cbk);

    private:
        std::function<void(int w, int h, std::shared_ptr<Data> i420)> frame_cbk_;
    };

}

#endif //GAMMARAY_RTC_VIDEO_SINK_H
