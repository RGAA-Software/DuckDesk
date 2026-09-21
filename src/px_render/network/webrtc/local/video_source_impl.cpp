//
// Created by hy on 2024/4/26.
//

#include "video_source_impl.h"

#include "rtc_base/time_utils.h"

namespace px {

void VideoSourceImpl::OnNotifyFrame(const webrtc::VideoFrame& notify_frame) {
    {
        std::lock_guard lock(latest_frame_mutex_);
        latest_frame_ = notify_frame;
    }

    // The broadcaster applies the current downstream VideoSinkWants. Compare
    // this counter with the encoder's "Encode call" and "sent encoded frame"
    // diagnostics when investigating media stalls.
    static std::atomic_uint64_t push_count{0};
    const auto current_push_count = ++push_count;
    if (current_push_count == 1 || current_push_count % 300 == 0) {
        const auto wants = broadcaster_.wants();
        LOGI("VideoSource push #{}, wants: active={}, max_fps={}, max_px={}, target_px={}, black={}", current_push_count, wants.is_active,
             wants.max_framerate_fps == std::numeric_limits<int>::max() ? -1 : wants.max_framerate_fps,
             wants.max_pixel_count == std::numeric_limits<int>::max() ? -1 : wants.max_pixel_count,
             wants.target_pixel_count.has_value() ? wants.target_pixel_count.value() : -1, wants.black_frames);
    }
    broadcaster_.OnFrame(notify_frame);
}

void VideoSourceImpl::AddOrUpdateSink(
    rtc::VideoSinkInterface<webrtc::VideoFrame>* sink,  // NOLINT(pixels-raw-pointer-boundary): libwebrtc observer ABI
    const rtc::VideoSinkWants& wants) {
    broadcaster_.AddOrUpdateSink(sink, wants);
    static_cast<void>(video_adapter_);
    LOGI("AddOrUpdateSink");
}

bool VideoSourceImpl::ReplayLatestNotification() {
    std::optional<webrtc::VideoFrame> frame_to_replay;
    {
        std::lock_guard lock(latest_frame_mutex_);
        frame_to_replay = latest_frame_;
    }
    if (!frame_to_replay.has_value()) {
        return false;
    }
    const auto replay_frame = webrtc::VideoFrame::Builder()
                                  .set_video_frame_buffer(frame_to_replay->video_frame_buffer())
                                  .set_timestamp_us(rtc::TimeMicros())
                                  .set_id(frame_to_replay->id())
                                  .build();
    LOGI("Replay latest VideoSource notification after encoder initialization, frame_id={}", replay_frame.id());
    broadcaster_.OnFrame(replay_frame);
    return true;
}

void VideoSourceImpl::RemoveSink(rtc::VideoSinkInterface<webrtc::VideoFrame>* sink) {  // NOLINT(pixels-raw-pointer-boundary): libwebrtc observer ABI
    broadcaster_.RemoveSink(sink);
    static_cast<void>(video_adapter_);
}

}  // namespace px
