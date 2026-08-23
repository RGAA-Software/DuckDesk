//
// RemoteAudioSink: 挂到 WebRTC 远端音频轨(浏览器麦克风上行)上的授权播放 sink。
//
// libwebrtc 完成 RTP/NetEq/Opus 解码，本 sink 只在通话授权匹配时
// 把 PCM 转交 VoiceAudioEndpoint。后者负责独立 WASAPI 播放，并把实际
// 播放缓冲送入 APM 作为 reverse reference；不会与桌面系统声音混流。
//

#ifndef REMOTE_AUDIO_SINK_H
#define REMOTE_AUDIO_SINK_H

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include "px_common_new/webrtc_helper.h"

namespace px
{

    class RemoteAudioSink : public webrtc::AudioTrackSinkInterface {
    public:
        using PcmCallback = std::function<void(
            const std::string& call_id, const int16_t* samples,
            size_t sample_count, int sample_rate, int channels)>;
        static std::shared_ptr<RemoteAudioSink> Make(PcmCallback pcm_callback);
        explicit RemoteAudioSink(PcmCallback pcm_callback);

        void SetAuthorized(const std::string& call_id, bool authorized);
        [[nodiscard]] bool IsAuthorized() const { return authorized_; }

        // webrtc::AudioTrackSinkInterface
        void OnData(const void* audio_data,
                    int bits_per_sample,
                    int sample_rate,
                    size_t number_of_channels,
                    size_t number_of_frames) override;

    private:
        mutable std::mutex state_mutex_;
        PcmCallback pcm_callback_;
        std::atomic_bool authorized_ = false;
        std::string call_id_;
        std::atomic<uint64_t> rx_frames_ = 0;
        uint64_t last_log_ms_ = 0;
    };

}

#endif //REMOTE_AUDIO_SINK_H
