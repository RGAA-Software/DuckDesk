//
// Decoded audio sink for rtc local mode.
//
// The dll runs webrtc with a DUMMY AudioDeviceModule, so the built-in playout
// discards everything. Attaching this sink to the remote audio track taps the
// decoded 16-bit PCM(opus -> PCM, 48kHz) and hands it to the sdk, which feeds
// its own AudioPlayer(the same player used by the ws/relay proto-audio path).
//

#ifndef PX_RTC_AUDIO_SINK_H
#define PX_RTC_AUDIO_SINK_H

#include <memory>
#include "rtc_client.h"
#include "api/media_stream_interface.h"

namespace px
{

    class Data;

    class RtcAudioSink : public webrtc::AudioTrackSinkInterface {
    public:
        static std::shared_ptr<RtcAudioSink> Make();

        RtcAudioSink() = default;

        void SetOnDataCallback(OnAudioDataCallback&& cbk);

        void OnData(const void* audio_data,
                    int bits_per_sample,
                    int sample_rate,
                    size_t number_of_channels,
                    size_t number_of_frames) override;

    private:
        OnAudioDataCallback data_cbk_;
    };

}

#endif //PX_RTC_AUDIO_SINK_H
