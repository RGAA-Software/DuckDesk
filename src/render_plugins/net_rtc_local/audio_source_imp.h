#pragma once

#include <mutex>

#include <api/media_stream_interface.h>
#include <api/notifier.h>
#include <rtc_base/ref_counted_object.h>

#include "webrtc/common.h"

struct dlca_webrtc_frame;

namespace dl
{

class AudioSourceImp :public webrtc::Notifier<webrtc::AudioSourceInterface>
{
public:
	static rtc::scoped_refptr<AudioSourceImp> Create() {
		rtc::scoped_refptr<AudioSourceImp> source(new rtc::RefCountedObject<AudioSourceImp>());
		return source;
	}
	virtual SourceState state() const override { return kLive; }
	virtual bool remote() const override { return true; }
	virtual void AddSink(webrtc::AudioTrackSinkInterface *sink) override
	{
		std::lock_guard<std::mutex> lock(mSinkLock);
		mSinks.push_back(sink);
	}
	virtual void RemoveSink(webrtc::AudioTrackSinkInterface *sink) override
	{
		std::lock_guard<std::mutex> lock(mSinkLock);
		mSinks.remove(sink);
	}
	void SendAudio(dlca_webrtc_frame* frame);

	std::list<webrtc::AudioTrackSinkInterface*> mSinks;
	std::mutex mSinkLock;
};

} // namespace dl
