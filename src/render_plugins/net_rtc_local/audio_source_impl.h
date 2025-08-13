#pragma once

#include <mutex>
#include "tc_common_new/webrtc_helper.h"

namespace tc
{

    class AudioSourceImpl : public webrtc::Notifier<webrtc::AudioSourceInterface> {
    public:
        static rtc::scoped_refptr<AudioSourceImpl> Create() {
            rtc::scoped_refptr<AudioSourceImpl> source(new rtc::RefCountedObject<AudioSourceImpl>());
            return source;
        }
        SourceState state() const override { return kLive; }
        bool remote() const override { return true; }
        void AddSink(webrtc::AudioTrackSinkInterface *sink) override {
            std::lock_guard<std::mutex> lock(mSinkLock);
            mSinks.push_back(sink);
        }

        void RemoveSink(webrtc::AudioTrackSinkInterface *sink) override {
            std::lock_guard<std::mutex> lock(mSinkLock);
            mSinks.remove(sink);
        }
        void SendAudio(/*AudioFrame* frame*/);

        std::list<webrtc::AudioTrackSinkInterface*> mSinks;
        std::mutex mSinkLock;
    };

} // namespace tc
