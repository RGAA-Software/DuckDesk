//
// Created by RGAA on 10/08/2026.
//

#include "rtc_video_sink.h"
#include "px_common_new/log.h"
#include "px_common_new/data.h"
#include <atomic>

namespace px
{

    std::shared_ptr<RtcVideoSink> RtcVideoSink::Make() {
        return std::make_shared<RtcVideoSink>();
    }

    void RtcVideoSink::OnFrame(const webrtc::VideoFrame& frame) {
        if (!frame_cbk_) {
            return;
        }
        auto buffer = frame.video_frame_buffer();
        if (!buffer) {
            return;
        }
        auto i420 = buffer->ToI420();
        if (!i420) {
            LOGE("RtcVideoSink, convert to I420 failed.");
            return;
        }

        const int w = i420->width();
        const int h = i420->height();
        if (w <= 0 || h <= 0) {
            return;
        }
        static std::atomic<int64_t> frame_count{0};
        const auto cnt = ++frame_count;
        if (cnt <= 3 || cnt % 300 == 0) {
            LOGI("RtcVideoSink OnFrame #{}, {}x{}", cnt, w, h);
        }
        const int uv_w = (w + 1) / 2;
        const int uv_h = (h + 1) / 2;
        const int64_t y_size = (int64_t)w * h;
        const int64_t uv_size = (int64_t)uv_w * uv_h;

        // pack Y/U/V plane by plane, dropping the strides
        auto data = Data::Make(nullptr, y_size + uv_size * 2);
        char* dst_y = data->DataAddr();
        for (int row = 0; row < h; ++row) {
            memcpy(dst_y + (int64_t)row * w, i420->DataY() + (int64_t)row * i420->StrideY(), w);
        }
        char* dst_u = dst_y + y_size;
        for (int row = 0; row < uv_h; ++row) {
            memcpy(dst_u + (int64_t)row * uv_w, i420->DataU() + (int64_t)row * i420->StrideU(), uv_w);
        }
        char* dst_v = dst_u + uv_size;
        for (int row = 0; row < uv_h; ++row) {
            memcpy(dst_v + (int64_t)row * uv_w, i420->DataV() + (int64_t)row * i420->StrideV(), uv_w);
        }

        frame_cbk_(w, h, data);
    }

    void RtcVideoSink::SetOnFrameCallback(std::function<void(int w, int h, std::shared_ptr<Data> i420)>&& cbk) {
        frame_cbk_ = std::move(cbk);
    }

}
