//
// RemoteAudioSink: 远端音频(浏览器麦克风)PCM 交给已授权语音端点播放。
//

#include "remote_audio_sink.h"
#include "px_common_new/log.h"
#include "px_common_new/time_util.h"

namespace px
{

    std::shared_ptr<RemoteAudioSink> RemoteAudioSink::Make(PcmCallback pcm_callback) {
        return std::make_shared<RemoteAudioSink>(std::move(pcm_callback));
    }

    RemoteAudioSink::RemoteAudioSink(PcmCallback pcm_callback)
        : pcm_callback_(std::move(pcm_callback)) {
    }

    void RemoteAudioSink::SetAuthorized(const std::string& call_id, bool authorized) {
        std::scoped_lock lock(state_mutex_);
        call_id_ = authorized ? call_id : std::string{};
        authorized_ = authorized && !call_id.empty();
    }

    void RemoteAudioSink::OnData(const void* audio_data,
                                 int bits_per_sample,
                                 int sample_rate,
                                 size_t number_of_channels,
                                 size_t number_of_frames) {
        if (!audio_data || bits_per_sample != 16 ||
            sample_rate != 48'000 || number_of_channels == 0) {
            return;
        }
        bool first_frame = false;
        bool log_stats = false;
        uint64_t total_frames = 0;
        const uint64_t now_ms = TimeUtil::GetCurrentTimestamp();
        {
            std::scoped_lock lock(state_mutex_);
            if (!authorized_ || !pcm_callback_ || call_id_.empty()) {
                return;
            }
            first_frame = rx_frames_ == 0;
            rx_frames_ += number_of_frames;
            total_frames = rx_frames_;
            pcm_callback_(call_id_, static_cast<const int16_t*>(audio_data),
                          number_of_frames * number_of_channels,
                          sample_rate, static_cast<int>(number_of_channels));
            if (last_log_ms_ == 0) last_log_ms_ = now_ms;
            if (now_ms - last_log_ms_ >= 5000) {
                last_log_ms_ = now_ms;
                log_stats = true;
            }
        }
        if (first_frame) {
            LOGI("RemoteAudioSink first PCM (decode ok): {} Hz, {} ch, {} bits, {} frames",
                 sample_rate, (int)number_of_channels, bits_per_sample, (int)number_of_frames);
        }
        if (log_stats) {
            LOGI("RemoteAudioSink stats: authorized rx frames total={}",
                 total_frames);
        }
    }

}
