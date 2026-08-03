//
// RemoteAudioSink: 挂到 WebRTC 远端音频轨(浏览器麦克风上行)上的统计型 sink。
//
// 播放链路由 libwebrtc 默认 ADM(Windows CoreAudio/WASAPI)完成:
//   RTP -> NetEq -> Opus 解码 -> AudioMixer -> 默认扬声器
// 解码由 ADM 播放线程驱动(实测 dummy ADM 不驱动解码,sink 收不到数据),
// 因此本 sink 只做接收统计/日志,不再自行 WASAPI 外放,避免与 ADM 双重出声。
//

#ifndef REMOTE_AUDIO_SINK_H
#define REMOTE_AUDIO_SINK_H

#include <atomic>
#include <cstdint>
#include <memory>

#include "tc_common_new/webrtc_helper.h"

namespace tc
{

    class RemoteAudioSink : public webrtc::AudioTrackSinkInterface {
    public:
        static std::shared_ptr<RemoteAudioSink> Make();

        // webrtc::AudioTrackSinkInterface
        void OnData(const void* audio_data,
                    int bits_per_sample,
                    int sample_rate,
                    size_t number_of_channels,
                    size_t number_of_frames) override;

    private:
        std::atomic<uint64_t> rx_frames_ = 0;
        uint64_t last_log_ms_ = 0;
    };

}

#endif //REMOTE_AUDIO_SINK_H
