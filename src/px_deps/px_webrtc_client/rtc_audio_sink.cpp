//
// Decoded audio sink for rtc local mode, see rtc_audio_sink.h
//

#include "rtc_audio_sink.h"
#include "px_common/data.h"

namespace px
{

    std::shared_ptr<RtcAudioSink> RtcAudioSink::Make() {
        return std::make_shared<RtcAudioSink>();
    }

    void RtcAudioSink::SetOnDataCallback(OnAudioDataCallback&& cbk) {
        data_cbk_ = cbk;
    }

    void RtcAudioSink::OnData(const void* audio_data,
                              int bits_per_sample,
                              int sample_rate,
                              size_t number_of_channels,
                              size_t number_of_frames) {
        if (!data_cbk_ || !audio_data || bits_per_sample != 16
            || number_of_channels == 0 || number_of_frames == 0) {
            return;
        }
        // copy out: the pcm buffer is owned by the webrtc audio pipeline,
        // the sdk plays it on its own audio thread
        const auto size = number_of_frames * number_of_channels * sizeof(int16_t);
        auto pcm = px::Data::Copy(std::span<const char>{static_cast<const char*>(audio_data), size});
        data_cbk_(pcm, sample_rate, (int)number_of_channels);
    }

}
