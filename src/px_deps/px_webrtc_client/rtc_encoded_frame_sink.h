//
// Encoded(pre-decode) video frame sink for rtc local multi-track mode.
//
// Attached via VideoTrackSourceInterface::AddEncodedSink, it receives the
// SAME H264 AnnexB bitstream the render's encoder produced, before the
// built-in webrtc decoder touches it. The frames are handed to the sdk and
// decoded by our own FFmpegVulkanDecoder chain(zero-copy d3d11/pl_vulkan
// rendering), instead of paying webrtc's software decode + I420 copy.
//

#ifndef PX_RTC_ENCODED_FRAME_SINK_H
#define PX_RTC_ENCODED_FRAME_SINK_H

#include <memory>
#include "rtc_client.h"
#include "api/media_stream_interface.h"

namespace px
{

    class Data;

    class RtcEncodedFrameSink : public rtc::VideoSinkInterface<webrtc::RecordableEncodedFrame> {
    public:
        static std::shared_ptr<RtcEncodedFrameSink> Make(int track_index);

        explicit RtcEncodedFrameSink(int track_index);

        void SetOnFrameCallback(OnEncodedVideoFrameCallback&& cbk);

        void OnFrame(const webrtc::RecordableEncodedFrame& frame) override;

    private:
        int track_index_ = 0;
        OnEncodedVideoFrameCallback frame_cbk_;
    };

}

#endif //PX_RTC_ENCODED_FRAME_SINK_H
