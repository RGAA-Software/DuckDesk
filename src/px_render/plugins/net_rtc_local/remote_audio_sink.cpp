//
// RemoteAudioSink: 远端音频(浏览器麦克风)PCM 接收统计
//

#include "remote_audio_sink.h"
#include "px_common_new/log.h"
#include "px_common_new/time_util.h"

namespace tc
{

    std::shared_ptr<RemoteAudioSink> RemoteAudioSink::Make() {
        return std::make_shared<RemoteAudioSink>();
    }

    void RemoteAudioSink::OnData(const void* audio_data,
                                 int bits_per_sample,
                                 int sample_rate,
                                 size_t number_of_channels,
                                 size_t number_of_frames) {
        if (rx_frames_ == 0) {
            LOGI("RemoteAudioSink first PCM (decode ok): {} Hz, {} ch, {} bits, {} frames",
                 sample_rate, (int)number_of_channels, bits_per_sample, (int)number_of_frames);
        }
        rx_frames_ += number_of_frames;

        uint64_t now_ms = TimeUtil::GetCurrentTimestamp();
        if (last_log_ms_ == 0) last_log_ms_ = now_ms;
        if (now_ms - last_log_ms_ >= 5000) {
            last_log_ms_ = now_ms;
            LOGI("RemoteAudioSink stats: rx frames total={}", (uint64_t)rx_frames_);
        }
    }

}
