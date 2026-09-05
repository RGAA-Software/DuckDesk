//
// Encoded(pre-decode) video frame sink for rtc local multi-track mode.
//

#include "rtc_encoded_frame_sink.h"
#include "px_common/log.h"
#include "px_common/data.h"
#include <algorithm>
#include <format>
#include <string>

namespace px
{

    std::shared_ptr<RtcEncodedFrameSink> RtcEncodedFrameSink::Make(int track_index) {
        return std::make_shared<RtcEncodedFrameSink>(track_index);
    }

    RtcEncodedFrameSink::RtcEncodedFrameSink(int track_index) {
        track_index_ = track_index;
        LOGI("RtcEncodedFrameSink created for track #{}", track_index_);
    }

    void RtcEncodedFrameSink::SetOnFrameCallback(OnEncodedVideoFrameCallback&& cbk) {
        frame_cbk_ = cbk;
    }

    void RtcEncodedFrameSink::OnFrame(const webrtc::RecordableEncodedFrame& frame) {
        if (!frame_cbk_) {
            return;
        }
        auto buffer = frame.encoded_buffer();
        if (!buffer || buffer->size() == 0) {
            return;
        }
        if (frame.codec() != webrtc::kVideoCodecH264) {
            static bool warned = false;
            if (!warned) {
                warned = true;
                LOGW("RtcEncodedFrameSink #{}: unexpected codec {}, the sdk decode chain expects H264.",
                     track_index_, (int)frame.codec());
            }
            return;
        }

        const auto res = frame.resolution();
        auto data = Data::Copy(std::span<const char>{reinterpret_cast<const char*>(buffer->data()), buffer->size()});

        // one-time dump of the first bytes to confirm the bitstream is AnnexB(00 00 00 01)
        static bool dumped = false;
        if (!dumped) {
            dumped = true;
            const auto* p = buffer->data();
            const int n = (int)std::min<size_t>(buffer->size(), 8);
            std::string hex;
            for (int i = 0; i < n; ++i) {
                hex += std::format("{:02x} ", p[i]);
            }
            LOGI("RtcEncodedFrameSink #{}: first frame {} bytes, head: {}, key: {}, res: {}x{}",
                 track_index_, (int)buffer->size(), hex, frame.is_key_frame(), res.width, res.height);
        }

        frame_cbk_(track_index_, frame.is_key_frame(), (int)res.width, (int)res.height, data);
    }

}
