#pragma once

#include <media/base/video_broadcaster.h>
#include <pc/video_track_source.h>

namespace dl
{

class VideoSourceImp : public rtc::VideoSourceInterface<webrtc::VideoFrame>
{
public:
	void AddOrUpdateSink(rtc::VideoSinkInterface<webrtc::VideoFrame> *sink, const rtc::VideoSinkWants &wants)
	{
		mBroadcaster.AddOrUpdateSink(sink, wants);
		std::lock_guard<std::mutex> lk(mSinksMutex);
		mSinks.insert(sink);
	}

	void RemoveSink(rtc::VideoSinkInterface<webrtc::VideoFrame> *sink)
	{
		mBroadcaster.RemoveSink(sink);
		std::lock_guard<std::mutex> lk(mSinksMutex);
		mSinks.erase(sink);
	}

	void BroadcastVideoFrame(webrtc::VideoFrame& videoFrame)
	{
		mBroadcaster.OnFrame(videoFrame);
	}

	int GetSinkCount()
	{
		std::lock_guard<std::mutex> lk(mSinksMutex);
		return mSinks.size();
	}

	rtc::VideoBroadcaster mBroadcaster;
	std::mutex mSinksMutex;
	std::set<rtc::VideoSinkInterface<webrtc::VideoFrame>*> mSinks;
};

class VideoTrackSourceImp : public webrtc::VideoTrackSource {
public:
	static rtc::scoped_refptr<VideoTrackSourceImp> Create() {
		std::unique_ptr<VideoSourceImp> capturer = std::make_unique<VideoSourceImp>();
		return new rtc::RefCountedObject<VideoTrackSourceImp>(std::move(capturer));
	}
	
	void OnFrame(webrtc::VideoFrame& videoFrame) {
		mVideoSource->BroadcastVideoFrame(videoFrame);
	}

	int GetSinkCount()
	{
		return mVideoSource->GetSinkCount();
	}
protected:
	explicit VideoTrackSourceImp(std::unique_ptr<VideoSourceImp> capturer)
		: VideoTrackSource(/*remote=*/false), mVideoSource(std::move(capturer)) {

	}
private:
	virtual rtc::VideoSourceInterface<webrtc::VideoFrame>* source() override {
		return mVideoSource.get();
	}
	std::unique_ptr<VideoSourceImp> mVideoSource;
};

} // namespace dl