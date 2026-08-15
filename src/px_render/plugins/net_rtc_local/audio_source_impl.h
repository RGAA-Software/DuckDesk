#pragma once

#include <cstdint>
#include <list>
#include <mutex>
#include <vector>

#include "px_common_new/webrtc_helper.h"

namespace px
{

    // Local capture → WebRTC outbound audio track.
    // Push interleaved PCM via SendAudio; chunks are sliced to 10ms for WebRTC.
    class AudioSourceImpl : public webrtc::Notifier<webrtc::AudioSourceInterface> {
    public:
        static rtc::scoped_refptr<AudioSourceImpl> Create() {
            rtc::scoped_refptr<AudioSourceImpl> source(new rtc::RefCountedObject<AudioSourceImpl>());
            return source;
        }

        SourceState state() const override { return kLive; }
        bool remote() const override { return false; }

        void AddSink(webrtc::AudioTrackSinkInterface* sink) override {
            std::lock_guard<std::mutex> lock(sink_lock_);
            sinks_.push_back(sink);
        }

        void RemoveSink(webrtc::AudioTrackSinkInterface* sink) override {
            std::lock_guard<std::mutex> lock(sink_lock_);
            sinks_.remove(sink);
        }

        // sample_rate: Hz (e.g. 48000). data is interleaved PCM.
        void SendAudio(const void* data, size_t size_bytes, int sample_rate, int channels, int bits_per_sample);

    private:
        std::list<webrtc::AudioTrackSinkInterface*> sinks_;
        std::mutex sink_lock_;
        // leftover bytes that don't fill a full 10ms chunk yet
        std::vector<uint8_t> pending_;
        uint64_t sent_10ms_chunks_ = 0;
    };

} // namespace px
